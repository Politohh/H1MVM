#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "theater_library.hpp"
#include "theater_library_metadata.hpp"
#include "theater_launch.hpp"
#include "demo_playback.hpp"
#include "demo_utils.hpp"
#include "cod4_camera.hpp"
#include "component/command.hpp"
#include "component/scheduler.hpp"
#include "component/console.hpp"
#include <commdlg.h>
#include <thread>
#pragma comment(lib, "comdlg32.lib")

namespace theater_library
{
    namespace
    {
        struct entry
        {
            std::string id, path, name, error, date;
            metadata details;
            std::filesystem::file_time_type modified{};
            bool source{};
        };
        std::mutex mutex;
        std::vector<entry> entries;
        std::jthread worker;
        std::atomic_bool busy{}, stopping{}, returning{};
        int revision{};
        std::string message;
        std::filesystem::path root;

        bool supported(const std::filesystem::path& path)
        { return path.extension() == ".dm_1" || path.extension() == ".dm_h1"; }

        bool in_library(const std::filesystem::path& path)
        {
            const auto relative = path.lexically_relative(std::filesystem::weakly_canonical(root / "demos"));
            return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
        }

        std::filesystem::path import_file(const std::filesystem::path& path)
        {
            if (!supported(path)) throw std::runtime_error("Select an H1 or COD4 demo recording");
            const auto source = std::filesystem::weakly_canonical(path);
            std::filesystem::create_directories(root / "demos");
            if (in_library(source)) return source;
            auto target = root / "demos" / source.filename();
            auto temporary = std::filesystem::path(target.string() + ".importing");
            for (unsigned i = 1; std::filesystem::exists(target) || std::filesystem::exists(temporary); ++i)
            {
                target = root / "demos" / std::format("{}.{:04}{}", source.stem().string(), i, source.extension().string());
                temporary = std::filesystem::path(target.string() + ".importing");
            }
            try
            {
                std::filesystem::copy_file(source, temporary);
                if (std::filesystem::file_size(source) != std::filesystem::file_size(temporary))
                    throw std::runtime_error("Incomplete imported demo copy");
                std::filesystem::rename(temporary, target);
            }
            catch (...) { std::error_code ignored; std::filesystem::remove(temporary, ignored); throw; }
            return target;
        }

        void scan(std::stop_token stop)
        {
            std::vector<std::filesystem::path> paths;
            std::set<std::filesystem::path> unique;
            auto add = [&](const std::filesystem::path& path)
            {
                std::error_code ec;
                if (paths.size() >= 4096 || !supported(path) || !std::filesystem::is_regular_file(path, ec)) return;
                const auto canonical = std::filesystem::weakly_canonical(path, ec);
                if (!ec && in_library(canonical) && unique.insert(canonical).second) paths.push_back(canonical);
            };
            std::vector<std::filesystem::path> folders{root / "demos"};
            for (const auto& folder : folders)
            {
                std::error_code ec;
                std::filesystem::recursive_directory_iterator it(folder, std::filesystem::directory_options::skip_permission_denied, ec), end;
                for (; it != end && !stop.stop_requested() && paths.size() < 4096; it.increment(ec))
                {
                    if (ec) { ec.clear(); continue; }
                    if (it.depth() >= 8) it.disable_recursion_pending();
                    add(it->path());
                }
            }
            std::vector<entry> previous;
            { std::lock_guard lock(mutex); previous = entries; }
            std::vector<entry> found;
            for (const auto& path : paths)
            {
                if (stop.stop_requested()) return;
                std::error_code ec;
                entry item;
                item.path = path.string(); item.id = item.path; item.name = path.filename().string();
                item.source = path.extension() == ".dm_1"; item.modified = std::filesystem::last_write_time(path, ec);
                const auto cached = std::find_if(previous.begin(), previous.end(), [&](const entry& e) { return e.path == item.path && e.modified == item.modified; });
                if (cached != previous.end()) { found.push_back(*cached); continue; }
                const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    item.modified - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
                const auto time = std::chrono::system_clock::to_time_t(system_time);
                std::tm local{}; localtime_s(&local, &time);
                char date[32]{}; std::strftime(date, sizeof(date), "%d/%m/%Y  %H:%M", &local); item.date = date;
                try
                {
                    if (item.source) item.details = read_source(path);
                    else { std::ifstream file(path, std::ios::binary); item.details = read_native(file); }
                }
                catch (const std::exception& e) { item.error = e.what(); }
                found.push_back(std::move(item));
            }
            std::sort(found.begin(), found.end(), [](const entry& a, const entry& b)
            { return a.modified != b.modified ? a.modified > b.modified : a.path < b.path; });
            std::lock_guard lock(mutex);
            entries = std::move(found); ++revision;
            message = std::format("{} demos in your library", entries.size());
        }

        bool start_scan(bool picker, bool convert = false)
        {
            if (stopping || root.empty() || busy.exchange(true)) return false;
            // A completed previous worker can now be joined without blocking the UI.
            if (worker.joinable()) worker.join();
            const auto owner = *reinterpret_cast<HWND*>(game::base_address + 0xC9DD2E0);
            worker = std::jthread([picker, convert, owner](std::stop_token stop)
            {
                const auto finish = gsl::finally([] { busy = false; });
                try
                {
                    std::filesystem::path conversion_source;
                    if (picker)
                    {
                        std::vector<wchar_t> paths(65536);
                        OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
                        dialog.lpstrFilter = convert ? L"COD4 recordings (*.dm_1)\0*.dm_1\0\0"
                            : L"Demo recordings (*.dm_1;*.dm_h1)\0*.dm_1;*.dm_h1\0\0";
                        dialog.lpstrFile = paths.data(); dialog.nMaxFile = static_cast<DWORD>(paths.size());
                        dialog.lpstrTitle = convert ? L"Convert a COD4 recording" : L"Import demo recordings into Theater";
                        dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                        if (!convert) dialog.Flags |= OFN_ALLOWMULTISELECT;
                        if (!GetOpenFileNameW(&dialog))
                        {
                            const auto error = CommDlgExtendedError();
                            if (error) throw std::runtime_error(std::format("Import dialog error {}", error));
                            return;
                        }
                        std::filesystem::path first(paths.data());
                        const auto* next = paths.data() + wcslen(paths.data()) + 1;
                        if (!*next || convert) conversion_source = import_file(first);
                        else while (*next && !stop.stop_requested()) { (void)import_file(first / next); next += wcslen(next) + 1; }
                    }
                    scan(stop);
                    if (convert && !stop.stop_requested())
                    {
                        if (conversion_source.extension() != ".dm_1") throw std::runtime_error("Select a COD4 .dm_1 recording");
                        (void)read_source(conversion_source);
                        const auto path = conversion_source.string();
                        if (path.find_first_of("\"\r\n") != std::string::npos) throw std::runtime_error("Unsupported demo path characters");
                        scheduler::once([path]
                        {
                            if (!demo_playback::playing() && !cod4_camera::active() && game::VirtualLobby_Loaded())
                                command::execute(std::format("cod4convert \"{}\"", path));
                        }, scheduler::main);
                    }
                }
                catch (const std::exception& e) { std::lock_guard lock(mutex); message = e.what(); ++revision; }
            });
            return true;
        }

        std::string thumbnail(const std::string& map)
        {
            if (map.empty() || map.size() > 64 || map.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos) return {};
            for (const auto& name : {"preview_" + map, "loadscreen_" + map, "mp_map_" + map})
                if (game::DB_FindXAssetHeader(game::ASSET_TYPE_MATERIAL, name.c_str(), 0).material) return name;
            return {};
        }
    }

    nlohmann::json state(int page, const std::string& filter)
    {
        std::lock_guard lock(mutex);
        std::vector<const entry*> visible;
        for (const auto& item : entries)
            if (filter == "all" || (filter == "source" && item.source) || (filter == "ready" && !item.source)) visible.push_back(&item);
        const auto pages = std::max(1, static_cast<int>((visible.size() + 8) / 9));
        page = std::clamp(page, 1, pages);
        nlohmann::json result = {{"revision", revision}, {"busy", busy.load()}, {"message", message},
            {"page", page}, {"pages", pages}, {"total", visible.size()}, {"entries", nlohmann::json::array()}};
        const auto conversion = cod4_camera::get_status();
        result["converting"] = conversion.loading || conversion.active;
        result["playing"] = demo_playback::playing();
        if ((conversion.loading || conversion.active) && !conversion.message.empty()) result["conversionMessage"] = conversion.message;
        for (int i = (page - 1) * 9; i < std::min(page * 9, static_cast<int>(visible.size())); ++i)
        {
            const auto& item = *visible[i]; const auto& info = item.details;
            const auto* map = info.map.empty() ? "" : game::UI_GetMapDisplayName(info.map.c_str());
            const auto* mode = info.gametype.empty() ? "" : game::UI_GetGameTypeDisplayName(info.gametype.c_str());
            const auto display_map = map && *map && info.map != map ? std::string(map)
                : (info.map_title.empty() ? info.map : info.map_title);
            const auto display_mode = mode && *mode && info.gametype != mode ? std::string(mode)
                : (info.mode_title.empty() ? info.gametype : info.mode_title);
            result["entries"].push_back({{"id", item.id}, {"name", item.name}, {"source", item.source},
                {"map", info.map}, {"mapTitle", display_map}, {"modeTitle", display_mode}, {"date", item.date},
                {"duration", info.duration}, {"material", thumbnail(info.map)}, {"error", item.error}});
        }
        return result;
    }

    bool action(const std::string& name, const std::string& id)
    {
        if (name == "return")
        {
            if (!demo_playback::playing() || !demo_playback::is_replay_armed()) return false;
            const auto session = demo_playback::session_id();
            scheduler::once([session]
            {
                if (!demo_playback::playing() || demo_playback::session_id() != session) return;
                demo_playback::stopplay();
                returning = true;
                command::execute("disconnect");
            }, scheduler::main);
            return true;
        }
        if (name == "refresh" || name == "import") return start_scan(name == "import");
        if (name == "convertfile") return start_scan(true, true);
        if (name != "play" && name != "convert") return false;
        entry selected;
        {
            std::lock_guard lock(mutex);
            const auto it = std::find_if(entries.begin(), entries.end(), [&](const entry& e) { return e.id == id; });
            if (it == entries.end() || !it->error.empty() || (name == "convert") != it->source || busy) return false;
            selected = *it;
        }
        if (selected.path.find_first_of("\"\r\n") != std::string::npos) return false;
        scheduler::once([selected, name]
        {
            if (!can_launch(demo_playback::playing(), cod4_camera::active(), game::CL_IsCgameInitialized(), game::VirtualLobby_Loaded()))
            {
                std::lock_guard lock(mutex);
                message = "The client is still leaving the previous session. Try Play again in a moment.";
                console::warn("Theater launch deferred: %s\n", selected.path.c_str());
                return;
            }
            const auto cmd = name == "convert" ? "cod4convert" : "demoplay";
            // Already on the main scheduler, just like command-line demo launch.
            // Execute Play now so the result check observes the command itself,
            // rather than inspecting playback before Cbuf executes it later.
            command::execute(std::format("{} \"{}\"{}", cmd, selected.path, name == "play" ? " fullpath" : ""), name == "play");
            if (name == "play" && !demo_playback::playing())
            {
                std::lock_guard lock(mutex);
                message = "Playback did not start. Check the console for the reported error.";
            }
        }, scheduler::main);
        return true;
    }

    bool consume_return()
    {
        // Wait for the frontend map and HKS VM before opening the library.
        return !demo_playback::playing() && game::VirtualLobby_Loaded() && returning.exchange(false);
    }

    class component final : public component_interface
    {
    public:
        void post_unpack() override
        {
            if (!game::environment::is_mp() || game::environment::is_dedi()) return;
            scheduler::once([]
            {
                root = std::filesystem::current_path();
                start_scan(false);
            }, scheduler::main);
        }
        void pre_destroy() override
        {
            stopping = true;
            if (worker.joinable()) { worker.request_stop(); worker.join(); }
        }
    };
}
REGISTER_COMPONENT(theater_library::component)
