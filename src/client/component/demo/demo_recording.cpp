#include "std_include.hpp"
#include "loader/component_loader.hpp"

#include "demo_playback.hpp"
#include "demo_recording.hpp"
#include "theater_ui.hpp"
#include "demo_utils.hpp"

#include "component/command.hpp"
#include "component/console.hpp"
#include "demo_game.hpp"
#include "component/scheduler.hpp"
#include "utils/hook.hpp"

using namespace demo_utils;

namespace // demo class
{
	game::dvar_t* demo_autorecord;

	// NOTE(h1): manual/"user" recording (demostart/demostop, named files) was removed 2026-07-07 --
	// auto-record (demoautorecord dvar) is the only recording path now. It has been the one that
	// actually works end-to-end (position/view/gamestate/weapons), so there is no remaining reason
	// to keep the parallel manual path around.
	class demo_recording_t
	{
		struct stored_times_t
		{
			std::int32_t flush_time{};
			std::int32_t predicted_data_time{};
			std::int32_t cur_svr_time{};
			std::int32_t first_svr_time_auto_file{};
		};

	public:
		demo_recording_t() = default;
		~demo_recording_t();
		demo_recording_t(const demo_recording_t&) = delete;
		demo_recording_t(demo_recording_t&&) noexcept = delete;
		demo_recording_t& operator=(const demo_recording_t&) = delete;
		demo_recording_t& operator=(demo_recording_t&&) noexcept = delete;

		[[nodiscard]] bool is_auto_recording() const;
		[[nodiscard]] bool non_delta_data_requested() const;

		void shutdown_watcher();
		void process_network_data(auto old_connstate, auto new_connstate, std::span<const std::uint8_t> network_data);
		void process_predicted_data();

	private:
		[[nodiscard]] bool non_delta_data_request_acknowledged();
		[[nodiscard]] bool start_auto_file(const std::filesystem::path& path);
		[[nodiscard]] static bool stop_internal(std::ofstream& file, std::int32_t first_svr_time, std::int32_t last_svr_time);

		void flush_filestreams();
		void check_auto_recording();
		void process_old_data();
		void process_new_gamestate(std::span<const std::uint8_t> network_data);
		void process_first_snapshot(std::span<const std::uint8_t> network_data);
		void process_special_network_data(auto old_connstate, auto new_connstate, std::span<const std::uint8_t> network_data);
		void process_helo_pilot_turret_fire();

		std::uint32_t non_delta_count_{};
		stored_times_t times_;
		buffer_t auto_buffer_;
		std::ofstream auto_file_;
	} demo;

	demo_recording_t::~demo_recording_t()
	{
		process_old_data();
	}

	bool demo_recording_t::is_auto_recording() const
	{
		return auto_file_.good() && auto_file_.is_open();
	}

	bool demo_recording_t::non_delta_data_requested() const
	{
		return non_delta_count_ > 0;
	}

	bool demo_recording_t::non_delta_data_request_acknowledged()
	{
		if (non_delta_data_requested())
		{
			const auto delta_num = game::mp::cl->snap.deltaNum;
			if (static constexpr auto no_delta = -1; delta_num == no_delta)
			{
				--non_delta_count_;
				return true;
			}
		}

		return false;
	}

	bool demo_recording_t::start_auto_file(const std::filesystem::path& path)
	{
		std::ofstream file(path, std::ios::binary);
		if (!file.good() || !file.is_open())
		{
			return false;
		}

		const auto buffer = auto_buffer_.get();
		file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());

		auto_file_ = std::move(file);
		auto_buffer_.clear();

		return true;
	}

	bool demo_recording_t::stop_internal(std::ofstream& file, std::int32_t first_svr_time, std::int32_t last_svr_time)
	{
		write_general_footer(file, first_svr_time, last_svr_time,
			theater_ui::recording_index(first_svr_time, last_svr_time));
		write_end_of_file(file);

		file.flush();
		file.close();

		return file.good() && !file.is_open();
	}

	void demo_recording_t::process_old_data()
	{
		if (is_auto_recording() && !stop_internal(auto_file_, times_.first_svr_time_auto_file, times_.cur_svr_time))
		{
			console::error("could not close auto demo\n");
		}

		non_delta_count_ = {};
		times_ = {};
		auto_buffer_.clear();
		assert(!is_auto_recording());
	}

	void demo_recording_t::shutdown_watcher()
	{
		auto execute_on_disconnect = [this, cgame_init = false]() mutable
		{
			if (demo_playback::playing())
			{
				if (is_auto_recording())
				{
					// this is only relevant when playing back demos without disconnecting from the server (instant play)
					cgame_init = false;
					process_old_data();
				}
				return;
			}

			if (!cgame_init && game::CL_IsCgameInitialized())
			{
				cgame_init = true;
			}

			if (cgame_init && !game::CL_IsCgameInitialized())
			{
				cgame_init = false;
				process_old_data();
			}
		};

		scheduler::loop(execute_on_disconnect, scheduler::main);
	}

	void demo_recording_t::flush_filestreams()
	{
		const auto real_time = game::mp::cls->realtime;
		if (real_time >= times_.flush_time + 15'000)
		{
			times_.flush_time = real_time;

			if (is_auto_recording())
			{
				auto_file_.flush();
			}
		}
	}

	void demo_recording_t::check_auto_recording()
	{
		if (!is_auto_recording() && times_.first_svr_time_auto_file && demo_autorecord && demo_autorecord->current.enabled)
		{
			assert(!game::Live_SyncOnlineDataFlags(0) && game::CL_IsCgameInitialized());

			const auto opt_auto_dir = create_directory_auto_demo();
			if (!opt_auto_dir)
			{
				console::error("could not create demo auto directory\n");
				return;
			}

			const auto opt_auto_path = create_path_auto_demo(*opt_auto_dir);
			if (!opt_auto_path)
			{
				console::error("could not create demo auto file\n");
				return;
			}

			if (!start_auto_file(*opt_auto_path))
			{
				// turn auto recording off to prevent spamming the console with errors
				demo_autorecord->current.enabled = false;

				console::error("could not create demo auto file %s\n", opt_auto_path->string().c_str());
				return;
			}
		}
	}

	void demo_recording_t::process_new_gamestate(std::span<const std::uint8_t> network_data)
	{
		theater_ui::begin_recording();
		process_old_data();

		// reserve memory (low size if auto recording is enabled, because then the demo writes almost directly to disk)
		const auto reserve_size = (demo_autorecord && demo_autorecord->current.enabled) ? 8192 : 1024 * 1024;
		auto_buffer_.reserve_memory(reserve_size);

		// write headers to auto buffer (ahead of the gamestate itself)
		write_general_header(auto_buffer_);
		write_mod_header(auto_buffer_);
		write_map_header(auto_buffer_);

		// store the current config strings (custom weapon names etc) so playback can restore them.
		// The real live cursor is the separate game::mp::cs_data_count global (0x2F7876C) -- see
		// gameState_t's comment in demo_game.hpp for the full story of that fix.
		write_gamestate_data(auto_buffer_);
	}

	void demo_recording_t::process_first_snapshot(std::span<const std::uint8_t>)
	{
		assert(!times_.cur_svr_time && !times_.first_svr_time_auto_file);

		times_.first_svr_time_auto_file = game::mp::cl->snap.serverTime;
	}

	void demo_recording_t::process_special_network_data(auto old_connstate, auto new_connstate, std::span<const std::uint8_t> network_data)
	{
		assert(new_connstate >= game::CA_PRIMED);

		if (old_connstate < game::CA_LOADING)
		{
			assert(!game::mp::cl->snap.valid && !game::mp::cl->snap.serverTime);

			process_new_gamestate(network_data);
		}
		else if (old_connstate >= game::CA_PRIMED)
		{
			if (game::mp::cl->snap.valid && game::mp::cl->snap.serverTime && !times_.cur_svr_time)
			{
				process_first_snapshot(network_data);
			}

			times_.cur_svr_time = game::mp::cl->snap.serverTime;
		}
	}

	void demo_recording_t::process_network_data(auto old_connstate, auto new_connstate, std::span<const std::uint8_t> network_data)
	{
		if (new_connstate < game::CA_PRIMED)
		{
			return;
		}

		process_special_network_data(old_connstate, new_connstate, network_data);
		check_auto_recording();

		if (is_auto_recording())
		{
			write_network_data(auto_file_, network_data);
		}
		else
		{
			write_network_data(auto_buffer_, network_data);
		}

		flush_filestreams();
	}

	void demo_recording_t::process_helo_pilot_turret_fire()
	{
		// TODO(h1): iw6 turret-fire flag 0x1419A9394 is Ghosts-only and INVALID on h1
		// (MWR has no helo/turret). Constant 0 = no turret fire.
		const std::uint8_t turret_fire = 0;
		if (turret_fire > 0)
		{
			if (is_auto_recording())
			{
				write_helo_pilot_turret_fire(auto_file_, turret_fire);
			}
			else
			{
				write_helo_pilot_turret_fire(auto_buffer_, turret_fire);
			}
		}
	}

	void demo_recording_t::process_predicted_data()
	{
		process_helo_pilot_turret_fire();

		if (is_auto_recording())
		{
			write_predicted_data(auto_file_);
		}
		else
		{
			write_predicted_data(auto_buffer_);
		}
	}
}

namespace // hooks
{
	utils::hook::detour CL_ParseServerMessage_hook;
	utils::hook::detour CL_SavePredicted_hook;

	// Detour of CL_SavePredictedPlayerInformationForServerTime (0x342D80): the engine has just
	// written the local player's predicted origin/velocity/bobCycle/movementDir into
	// cl->clientArchive[clientArchiveIndex]. Capture that entry into the demo so playback can
	// restore the local player's position via CL_GetPredictedPlayerInformationForServerTime.
	void save_predicted_data(game::mp::clientActive_t* cl, std::int32_t server_time)
	{
		CL_SavePredicted_hook.invoke(cl, server_time);

		// capture only while recording (never during playback, which also drives this fn)
		if (!demo_playback::playing())
		{
			demo.process_predicted_data();
		}
	}

	void capture_data(const std::int32_t client_num, game::msg_t& msg)
	{
		// Once the replay is armed, the freshly-loaded loopback server is still sending its own
		// snapshots/commands. If they reach the client they fight the demo's replayed stream and
		// their snapshot numbers collide with the demo's (-> "CG_ProcessSnapshots: n < latest
		// SnapshotNum"). Drop every live message so the DEMO is the sole source of data. Our own
		// replayed messages come through here too (process_network_data re-enters this hook), so
		// let those pass by checking demo_utils::is_demo_feeding().
		if (demo_playback::playing() && !demo_utils::is_demo_feeding()
			&& static_cast<int>(*game::mp::connstate) >= static_cast<int>(game::CA_PRIMED))
		{
			return;
		}

		const auto old_connstate = *game::mp::connstate;
		CL_ParseServerMessage_hook.invoke(client_num, &msg);
		const auto new_connstate = *game::mp::connstate;

		if (demo_playback::playing())
		{
			return;
		}

		if (new_connstate < game::CA_DISCONNECTED || new_connstate > game::CA_ACTIVE)
		{
			console::warn("invalid connection state after processing data in CL_PacketEvent\n");
			return;
		}

		if (!msg.data || msg.overflowed || static_cast<std::size_t>(msg.cursize) > MAX_SIZE)
		{
			if (demo.is_auto_recording())
			{
				console::warn("invalid data in CL_PacketEvent\n");
			}
			return;
		}

		demo.process_network_data(old_connstate, new_connstate,
			std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(msg.data), msg.cursize));
	}

	void capture_predicted_data(game::msg_t& msg, std::int32_t value, std::uint32_t bits)
	{
		if (!demo_playback::playing())
		{
			demo.process_predicted_data();
		}

		// call MSG_WriteBits that this hook replaces
		game::MSG_WriteBits(&msg, value, bits);
	}

	void request_non_delta_data(game::msg_t& msg, std::int32_t value, std::uint32_t bits)
	{
		if (!demo_playback::playing() && demo.non_delta_data_requested())
		{
			// request a non-delta snapshot from the server to have a valid start for a new demo
			value = 1;
		}

		// call MSG_WriteBits that this hook replaces
		game::MSG_WriteBits(&msg, value, bits);
	}
}

namespace // commands
{
	// NOTE(h1): manual/"user" recording (separate named files, broken end-to-end) was removed
	// 2026-07-07. These commands are just an ergonomic on/off switch for the SAME auto-record
	// path (demoautorecord dvar) that already works -- not a parallel recording system.
	void execute_demo_start_command()
	{
		if (!demo_autorecord)
		{
			return;
		}

		demo_autorecord->current.enabled = true;
		console::info("started auto-recording a demo\n");
	}

	void execute_demo_stop_command()
	{
		if (!demo_autorecord)
		{
			return;
		}

		demo_autorecord->current.enabled = false;
		console::info("stopped auto-recording a demo\n");
	}
}

namespace demo_recording
{
	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::is_mp())
			{
				return;
			}

			// check run-time address assertions
			check_address_assertions();

			// capture incoming packets (STAGE 2b: recording). Safe when idle: the recording
			// path early-returns before CA_PRIMED and only writes to buffers/files. clc/cls
			// are still stubs, so recorded gamestate may be imperfect until they're wired,
			// but the network stream is captured.
			CL_ParseServerMessage_hook.create(game::CL_ParseServerMessage, capture_data);

			// capture client predicted data (local player position). iw6 hooked a MSG_WriteBits
			// call site inside CL_WritePacket (0x1402C21B9), invalid on h1. Instead detour the
			// clientArchive record-writer CL_SavePredictedPlayerInformationForServerTime (0x342D80,
			// s2-style): after it writes clientArchive[idx], capture that entry into the demo.
			CL_SavePredicted_hook.create(game::CL_SavePredictedPlayerInformationForServerTime, save_predicted_data);

			// TODO(h1): request_non_delta_data (MSG_WriteBits site in CL_WritePacket) still TODO.
			// utils::hook::call(<h1 site>, request_non_delta_data);

			// add support to auto record
			demo_autorecord = game::demo_compat::register_bool(
				"demoautorecord", false, game::DVAR_FLAG_NONE, "Automatically start recording a demo.");

			command::add("demostart", execute_demo_start_command);
			command::add("demostop", execute_demo_stop_command);

			scheduler::on_game_initialized([]()
			{
				demo.shutdown_watcher();

				// check if demo directories exist / could be created
				if (!can_create_demo_directories())
				{
					console::error("could not create demo directories\n");
				}
			}, scheduler::main);

			// ---- local-server clock offset (fixes local recordings) --------------------------
			// A local (listen) server starts serverTime near 0, so demos recorded on it collide
			// with the fresh loopback server that PLAYBACK spins up (also ~0) -> the "Lost reliable
			// commands" / "CG_ServerCommand: Requested a command not received" desync. Demos
			// recorded on ONLINE servers work because their serverTime is huge (hours of uptime),
			// leaving the replayed stream unambiguously ahead of the low-time loopback. Push a
			// local server's clock up into that same range once at match start so local recordings
			// behave identically. Skip while a demo plays -- the playback loopback must stay low.
			// Runs on the server thread (only ticks with a server up); SV_Loaded() guards it.
			scheduler::loop([]()
			{
				constexpr int server_time_offset = 600'000'000; // ~7 days, in the "online" range
				if (demo_playback::playing() || !game::SV_Loaded())
				{
					return;
				}
				// serverStatic_t::time (== game::mp::svs->time), confirmed @ 0x14647B280 (RVA 0x647B280)
				auto& svs_time = *reinterpret_cast<int*>(0x647B280_b);
				if (svs_time > 0 && svs_time < server_time_offset)
				{
					svs_time += server_time_offset; // one-shot: next tick it's already above
				}
			}, scheduler::server);
		}
	};
}

REGISTER_COMPONENT(demo_recording::component)
