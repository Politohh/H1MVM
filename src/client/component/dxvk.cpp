#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "console.hpp"
#include "dxvk.hpp"
#include "dxvk_probe.hpp"
#include "scheduler.hpp"

#include "runtime_assets.hpp"

#include "game/game.hpp"

#include <ctime>
#include <delayimp.h>

#include <utils/flags.hpp>
#include <utils/io.hpp>
#include <utils/nt.hpp>
#include <utils/string.hpp>

// DXVK (D3D11 on Vulkan) for every player whose GPU can run it, native D3D11 for everyone else.
//
// h1-mod.exe delay-loads d3d11.dll (premake5.lua) so that this choice comes first. post_start runs
// before the game binary is loaded and loads the chosen dxgi.dll and d3d11.dll at once by full
// path: the bundled DXVK, a d3d11.dll/dxgi.dll next to the exe (the player's own DXVK or wrapper,
// which always wins), or System32's. Nothing is left to name lookup afterwards: the delay-load
// hook hands demo_gui's D3D11CreateDevice call that d3d11.dll, and load_import answers the game's
// dxgi.dll import from that dxgi.dll. (Leaving it to Windows mixed copies: other modules can load
// System32's d3d11.dll before the first device, and a by-name load then picked that one even with
// DXVK's dxgi.dll already in use: a crash in d3d11.dll at the first device.)
// Nothing is hidden or renamed: DXVK's modules appear under their own paths in every module list.
//
// Order of precedence: -nodxvk / -dxvk on the command line, then players2/dxvk.txt (auto, on or
// off), then auto. Auto uses DXVK when the Vulkan probe (dxvk_probe.cpp) finds that the GPU DXVK
// would pick can run DXVK 3, and the last start on DXVK on this GPU and driver reached its first
// frames. Settings: players2/dxvk.conf. Shader cache: DXVK's default, %LOCALAPPDATA%\dxvk.
// Each start appends its choice to players2/renderer.log.
namespace dxvk
{
	namespace
	{
		enum class mode
		{
			automatic,
			on,
			off,
		};

		enum class source
		{
			none,         // dedicated server or Wine: left to Windows
			system,
			game_folder,
			bundled,
		};

		struct choice
		{
			source from;
			HMODULE d3d11;     // what the game uses for D3D11 and DXGI, loaded in post_start
			HMODULE dxgi;
			bool guarded;      // players2/dxvk-starting.txt is waiting for the first frames
			bool game_loaded;  // the game binary was loaded after the choice
			std::string line;
		};

		choice chosen{};

		constexpr auto first_frames = 120;

		constexpr auto default_mode_file =
			"auto\n"
			"# DXVK (D3D11 on Vulkan): auto, on or off. auto uses DXVK when your GPU and driver can run it.\n"
			"# -dxvk / -nodxvk on the command line override this file. Changes apply at the next start.\n";

		constexpr auto default_config =
			"# DXVK settings for MWR, read by the DXVK that ships with the client.\n"
			"# Every option is listed in https://github.com/doitsujin/dxvk/blob/master/dxvk.conf\n";

		std::filesystem::path players2(const std::string& name)
		{
			return utils::nt::library{}.get_folder() / "players2" / name;
		}

		std::string read_text(const std::filesystem::path& path)
		{
			std::string data;
			utils::io::read_file(path.generic_string(), &data);
			return data;
		}

		void write_text(const std::filesystem::path& path, const std::string& data)
		{
			utils::io::write_file(path.generic_string(), data);
		}

		void delete_file(const std::filesystem::path& path)
		{
			std::error_code error;
			std::filesystem::remove(path, error);
		}

		mode read_mode(std::string& source)
		{
			if (utils::flags::has_flag("nodxvk"))
			{
				source = "-nodxvk";
				return mode::off;
			}

			if (utils::flags::has_flag("dxvk"))
			{
				source = "-dxvk";
				return mode::on;
			}

			const auto file = players2("dxvk.txt");
			if (!std::filesystem::exists(file))
			{
				write_text(file, default_mode_file);
			}

			source = "players2/dxvk.txt";
			std::istringstream stream(read_text(file));
			std::string word;
			stream >> word;
			word = utils::string::to_lower(word);
			return word == "on" ? mode::on : word == "off" ? mode::off : mode::automatic;
		}

		std::string gpu_key(const dxvk_probe::result& probe)
		{
			return utils::string::va("%04x:%04x:%08x", probe.vendor, probe.device_id, probe.driver_version);
		}

		bool load_bundled(const std::filesystem::path& folder, std::string& error)
		{
			const auto config = players2("dxvk.conf");
			if (!std::filesystem::exists(config))
			{
				write_text(config, default_config);
			}

			if (!GetEnvironmentVariableW(L"DXVK_CONFIG_FILE", nullptr, 0))
			{
				SetEnvironmentVariableW(L"DXVK_CONFIG_FILE", config.wstring().data());
			}

			// dxgi.dll first: DXVK's d3d11.dll imports it by name
			const auto dxgi = LoadLibraryExW((folder / "dxgi.dll").wstring().data(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
			const auto d3d11 = dxgi ? LoadLibraryExW((folder / "d3d11.dll").wstring().data(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) : nullptr;
			if (!d3d11)
			{
				error = utils::string::va("Windows could not load the bundled DXVK (error %lu)", GetLastError());
				if (dxgi)
				{
					FreeLibrary(dxgi);
				}

				return false;
			}

			chosen.dxgi = dxgi;
			chosen.d3d11 = d3d11;
			return true;
		}

		// The game folder's copy where there is one (a player's own DXVK or wrapper), System32's otherwise.
		// dxgi.dll first, so a d3d11.dll that imports it by name binds to the same one.
		void load_unbundled()
		{
			const auto game_folder = utils::nt::library{}.get_folder();
			const auto load = [&](const wchar_t* name)
			{
				const auto local = game_folder / name;
				return std::filesystem::exists(local)
					? LoadLibraryExW(local.wstring().data(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)
					: LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
			};

			chosen.dxgi = load(L"dxgi.dll");
			chosen.d3d11 = load(L"d3d11.dll");
		}

		void log_choice()
		{
			const auto time = std::time(nullptr);
			tm local{};
			localtime_s(&local, &time);
			char stamp[32]{};
			std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
			utils::io::write_file(players2("renderer.log").generic_string(), utils::string::va("[%s] %s\n", stamp, chosen.line.data()), true);
		}

		void choose()
		{
			if (utils::flags::has_flag("dedicated"))
			{
				chosen.line = "dedicated server: no renderer";
				return;
			}

			if (utils::nt::is_wine())
			{
				chosen.line = "native D3D11: running under Wine, which brings its own D3D11";
				return;
			}

			chosen.from = source::system;
			const auto game_folder = utils::nt::library{}.get_folder();
			if (std::filesystem::exists(game_folder / "d3d11.dll") || std::filesystem::exists(game_folder / "dxgi.dll"))
			{
				chosen.from = source::game_folder;
				chosen.line = "the d3d11.dll/dxgi.dll in the game folder: a player's own DXVK or wrapper always wins over the bundled DXVK";
				return;
			}

			std::string source;
			const auto requested = read_mode(source);
			if (requested == mode::off)
			{
				chosen.line = "native D3D11: DXVK is off (" + source + ")";
				return;
			}

			if (GetModuleHandleW(L"d3d11.dll") || GetModuleHandleW(L"dxgi.dll"))
			{
				chosen.line = "native D3D11: the system d3d11.dll or dxgi.dll was loaded before the choice";
				return;
			}

			std::filesystem::path folder;
			try
			{
				folder = runtime_assets::path("dxvk/x64");
			}
			catch (const std::exception&)
			{
			}

			if (folder.empty() || !std::filesystem::exists(folder / "d3d11.dll") || !std::filesystem::exists(folder / "dxgi.dll"))
			{
				chosen.line = "native D3D11: this build has no bundled DXVK";
				return;
			}

			std::string device;
			if (requested == mode::automatic)
			{
				const auto probe = dxvk_probe::run();
				device = probe.device;
				if (!probe.suitable)
				{
					chosen.line = utils::string::va("native D3D11: %s%s%s (Vulkan probe, %d ms)", probe.device.empty() ? "" : probe.device.data(),
						probe.device.empty() ? "" : ": ", probe.reason.data(), probe.milliseconds);
					return;
				}

				// A start on DXVK that never reached its first frames
				const auto starting = players2("dxvk-starting.txt");
				const auto failed = players2("dxvk-failed.txt");
				if (std::filesystem::exists(starting))
				{
					write_text(failed, read_text(starting));
					delete_file(starting);
				}

				if (std::filesystem::exists(failed))
				{
					if (read_text(failed).starts_with(gpu_key(probe)))
					{
						chosen.line = "native D3D11: the last start on DXVK with this GPU and driver didn't reach its first frames "
							"(delete players2/dxvk-failed.txt, or start with -dxvk, to try again)";
						return;
					}

					delete_file(failed);
				}

				write_text(starting, gpu_key(probe) + " " + probe.device + "\n");
				chosen.guarded = true;
			}

			std::string error;
			if (!load_bundled(folder, error))
			{
				delete_file(players2("dxvk-starting.txt"));
				chosen.guarded = false;
				chosen.line = "native D3D11: " + error;
				return;
			}

			chosen.from = source::bundled;
			chosen.line = requested == mode::on
				? "DXVK (bundled), forced on by " + source
				: "DXVK (bundled) on " + device;
		}

		void clear_guard()
		{
			if (chosen.guarded)
			{
				chosen.guarded = false;
				delete_file(players2("dxvk-starting.txt"));
			}
		}
	}

	std::string status()
	{
		return chosen.line;
	}

	HMODULE chosen_d3d11()
	{
		return chosen.d3d11;
	}

	class component final : public component_interface
	{
	public:
		void post_start() override
		{
			choose();
			if (chosen.from == source::system || chosen.from == source::game_folder)
			{
				load_unbundled();
			}

			if (chosen.from != source::none)
			{
				const auto path = [](const HMODULE module)
				{
					return module ? utils::nt::library(module).get_path().generic_string() : std::string("not loaded");
				};

				chosen.line += " [d3d11.dll: " + path(chosen.d3d11) + "; dxgi.dll: " + path(chosen.dxgi) + "]";
			}

			log_choice();
		}

		void* load_import(const std::string& library, const std::string& function) override
		{
			if (chosen.dxgi && _stricmp(library.data(), "dxgi.dll") == 0)
			{
				return GetProcAddress(chosen.dxgi, function.data());
			}

			// demo_gui answers D3D11CreateDevice for multiplayer and reaches d3d11.dll through the delay-load hook
			const auto demo_gui_answers = function == "D3D11CreateDevice" && !game::environment::is_dedi() && !game::environment::is_sp();
			if (chosen.d3d11 && _stricmp(library.data(), "d3d11.dll") == 0 && !demo_gui_answers)
			{
				return GetProcAddress(chosen.d3d11, function.data());
			}

			return nullptr;
		}

		void post_load() override
		{
			chosen.game_loaded = true;
		}

		void post_unpack() override
		{
			console::info("Renderer: %s\n", chosen.line.data());

			if (chosen.guarded)
			{
				scheduler::schedule([]
				{
					static auto frames = 0;
					if (++frames < first_frames)
					{
						return scheduler::cond_continue;
					}

					clear_guard();
					return scheduler::cond_end;
				}, scheduler::pipeline::renderer);
			}

			command::add("dxvk_status", []
			{
				console::info("Renderer: %s\n", chosen.line.data());
			});

			command::add("dxvk", [](const command::params& params)
			{
				const std::string value = params.size() > 1 ? utils::string::to_lower(params.get(1)) : "";
				if (value != "auto" && value != "on" && value != "off")
				{
					console::info("usage: dxvk <auto|on|off>  (now: %s)\n", chosen.line.data());
					return;
				}

				write_text(players2("dxvk.txt"), value + std::string(default_mode_file).substr(std::string(default_mode_file).find('\n')));
				console::info("players2/dxvk.txt is now \"%s\"; it applies at the next start\n", value.data());
			});
		}

		void pre_destroy() override
		{
			// Closing the launcher before the game ever loaded is not a DXVK failure
			if (!chosen.game_loaded)
			{
				clear_guard();
			}
		}
	};
}

// The first call into h1-mod.exe's delay-loaded d3d11.dll (demo_gui's D3D11CreateDevice) gets the
// d3d11.dll chosen in post_start, whatever else has loaded a d3d11.dll by then
namespace
{
	FARPROC WINAPI delay_load_hook(const unsigned notification, PDelayLoadInfo info)
	{
		if (notification == dliNotePreLoadLibrary && info && info->szDll && _stricmp(info->szDll, "d3d11.dll") == 0)
		{
			return reinterpret_cast<FARPROC>(dxvk::chosen_d3d11());
		}

		return nullptr;
	}
}

extern "C" const PfnDliHook __pfnDliNotifyHook2 = delay_load_hook;

REGISTER_COMPONENT(dxvk::component)
