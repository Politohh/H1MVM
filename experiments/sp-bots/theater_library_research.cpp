#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "theater_library.hpp"
#include "theater_library_metadata.hpp"
#include "theater_launch.hpp"
#include "demo_splitter.hpp"
#include "demo_playback.hpp"
#include "demo_utils.hpp"
#include "cod4_camera.hpp"
#include "cod4_demo_events.hpp"
#include "cod4_weapon_catalog.hpp"
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
        std::optional<unsigned> pending_return_session;
        std::atomic_int split_progress{-1};
        int revision{};
        std::string message;
        std::filesystem::path root;
        std::string event_id;
        std::filesystem::file_time_type event_modified{};
        nlohmann::json event_state;

        std::vector<int> read_bookmarks(const entry& item)
        {
            const auto path = item.path + ".bookmarks.json";
            if (!std::filesystem::exists(path)) return {};
            if (std::filesystem::file_size(path)>16384) throw std::runtime_error("Bookmark file is too large");
            std::ifstream file(path);
            const auto json = nlohmann::json::parse(file);
            if (!json.is_array() || json.size()>500) throw std::runtime_error("Invalid bookmark file");
            std::vector<int> result;
            for (const auto& value : json)
            {
                if (!value.is_number_integer()) throw std::runtime_error("Invalid bookmark time");
                const auto time=value.get<std::int64_t>();
                if (time<0 || time>item.details.duration) throw std::runtime_error("Bookmark is outside recording");
                result.push_back(static_cast<int>(time));
            }
            std::sort(result.begin(),result.end()); result.erase(std::unique(result.begin(),result.end()),result.end());
            return result;
        }

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
            entries = std::move(found); event_id.clear(); ++revision;
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

        // Resolve only resident, real kill-icon materials on the calling UI
        // thread. The file decoder never accesses the game asset database.
        nlohmann::json with_icons(nlohmann::json data)
        {
            std::map<std::string,std::pair<std::string,int>> cache;
            for(auto& event:data["events"])
            {
                if(event.value("kind","")!="kill") continue;
                const auto source=event.value("weapon","");
                const auto cause=event.value("cause",0u);
                const auto key=source+":"+std::to_string(cause);
                if(!cache.contains(key))
                {
                    game::Material* material{};int ratio=1;
                    const auto resident_material=[](const char* name)->game::Material*
                    {
                        return game::DB_XAssetExists(game::ASSET_TYPE_MATERIAL,name) && !game::DB_IsXAssetDefault(game::ASSET_TYPE_MATERIAL,name)
                            ? game::DB_FindXAssetHeader(game::ASSET_TYPE_MATERIAL,name,false).material : nullptr;
                    };
                    if(!source.empty())
                    {
                        const auto mapping=std::find_if(cod4::weapon_mappings.begin(),cod4::weapon_mappings.end(),[&](const auto& m){return m.source==source;});
                        if(mapping!=cod4::weapon_mappings.end())
                        {
                            // Alternate GL kills must never borrow the rifle icon.
                            const auto weapon=std::string(mapping->base);
                            if(mapping->alternate) material=resident_material("hud_icon_40mm_grenade");
                            else if(game::DB_XAssetExists(game::ASSET_TYPE_WEAPON,weapon.c_str()) && !game::DB_IsXAssetDefault(game::ASSET_TYPE_WEAPON,weapon.c_str()))
                            {
                                const auto* def=game::DB_FindXAssetHeader(game::ASSET_TYPE_WEAPON,weapon.c_str(),false).weapon;
                                if(def) { material=def->killIcon;ratio=def->killIconRatio==game::WEAPON_ICON_RATIO_4TO1?4:def->killIconRatio==game::WEAPON_ICON_RATIO_2TO1?2:1; }
                            }
                        }
                    }
                    if(!material && cause)
                    {
                        const auto mod=cause&127u;
                        const auto name=mod==8?"killiconheadshot":mod==7?"killiconmelee":mod==9?"killiconcrush":mod==11?"killiconfalling":mod==12?"killiconsuicide":mod==15?"killiconimpact":"killicondied";
                        material=resident_material(name);
                    }
                    const auto name=material && material->name?material->name:"";
                    cache[key]={name,ratio};
                }
                event["icon"]=cache.at(key).first;event["iconRatio"]=cache.at(key).second;
            }
            return data;
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
            {"splitProgress", split_progress.load()},
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
                {"splittable", item.source || info.converted},
                {"map", info.map}, {"mapTitle", display_map}, {"modeTitle", display_mode}, {"date", item.date},
                {"duration", info.duration}, {"material", thumbnail(info.map)}, {"error", item.error}});
        }
        return result;
    }

    nlohmann::json events(const std::string& id)
    {
        entry selected;
        {
            std::lock_guard lock(mutex);
            const auto it=std::find_if(entries.begin(),entries.end(),[&](const entry& e){return e.id==id;});
            if (it==entries.end() || !it->error.empty()) return {{"ready",true},{"message","Recording unavailable"},{"events",nlohmann::json::array()}};
            if (event_id==id && event_modified==it->modified) return with_icons(event_state);
            if (stopping || busy.exchange(true)) return {{"ready",false}};
            selected=*it;
        }
        if (worker.joinable()) worker.join();
        worker=std::jthread([selected](std::stop_token stop)
        {
            const auto finish=gsl::finally([]{busy=false;});
            nlohmann::json data={{"ready",true},{"events",nlohmann::json::array()},{"message",""}};
            try
            {
                std::vector<demo_events::event> list;
                int first{};
                if (selected.source)
                {
                    const auto source=demo_events::read_source(selected.path);
                    list=source.events;first=source.camera.frames.front().time;
                    data["message"]=cod4::recorded_gametype(source.camera.configstrings)=="sd" ? "Round markers follow the recorded S&D timer." : "Round boundaries unavailable for this game mode.";
                }
                else
                {
                    std::ifstream file(selected.path,std::ios::binary);
                    const auto tail=demo_file::read_footer(file);
                    if (!tail) throw std::runtime_error("Recording has no timeline footer");
                    first=tail->first; list=selected.details.events;
                    const auto index=demo_timeline::decode(tail->bytes,tail->first,tail->last);
                    const bool incomplete=std::any_of(list.begin(),list.end(),[](const auto& e){return e.kind=="kill" && e.attacker.empty();}) || list.empty();
                    auto original=std::filesystem::path(selected.path);original.replace_extension(".dm_1");
                    if(incomplete && index && std::filesystem::is_regular_file(original))
                    {
                        try
                        {
                            const auto source=demo_events::read_source(original);
                            if(demo_events::matches(source,*index,selected.details.map,tail->first,tail->last))
                            { list=source.events;data["message"]="Player names and kill weapons decoded from the verified original recording."; }
                            else data["message"]="Original recording does not match this clip's kill index; its names were not used.";
                        }
                        catch(const std::exception& e) { data["message"]=std::string("Could not read the original recording: ")+e.what(); }
                    }
                    std::size_t kills{};
                    for (const auto& e:list) if(e.kind=="kill" && e.time>=tail->first && e.time<=tail->last) ++kills;
                    // Older conversions retain exact kill times but no names or round data.
                    if (index && kills<index->events.size())
                    {
                        for (std::size_t i=kills;i<index->events.size();++i)
                        {
                            const auto& e=index->events[i];
                            list.push_back({e.time,"kill",(e.attacker==2046?"World":"Player "+std::to_string(e.attacker+1))+" > Player "+std::to_string(e.victim+1)});
                        }
                        if(data["message"]=="") data["message"]="Full kill details need the matching .dm_1 beside this older recording.";
                    }
                    else if (list.empty()) data["message"]="No recorded events available. You can still add bookmarks and cut a range.";
                }
                std::stable_sort(list.begin(),list.end(),[](const auto& a,const auto& b){return a.time<b.time;});
                for (const auto& e:list)
                    if (e.time>=first && static_cast<std::int64_t>(e.time)-first<=selected.details.duration && data["events"].size()<10000)
                        data["events"].push_back({{"time",e.time-first},{"kind",e.kind},{"label",e.label},
                            {"attacker",e.attacker},{"victim",e.victim},{"weapon",e.weapon},{"cause",e.cause}});
                for (const auto time:read_bookmarks(selected))
                    data["events"].push_back({{"time",time},{"kind","bookmark"},{"label","Bookmark"}});
            }
            catch (const std::exception& e) { data["message"]=e.what(); }
            if (stop.stop_requested()) return;
            std::lock_guard lock(mutex);
            event_id=selected.id; event_modified=selected.modified; event_state=std::move(data);
        });
        return {{"ready",false}};
    }

    bool bookmark(const std::string& id, int time)
    {
        std::lock_guard lock(mutex);
        const auto it=std::find_if(entries.begin(),entries.end(),[&](const entry& e){return e.id==id;});
        if (it==entries.end() || busy || time<0 || time>it->details.duration) return false;
        try
        {
            auto marks=read_bookmarks(*it);
            const auto found=std::find(marks.begin(),marks.end(),time);
            if (found!=marks.end()) marks.erase(found);
            else { if(marks.size()>=500) return false; marks.push_back(time); }
            std::sort(marks.begin(),marks.end());
            const auto target=it->path+".bookmarks.json", temporary=target+".tmp";
            std::ofstream file(temporary,std::ios::trunc); file << nlohmann::json(marks).dump(); file.close();
            if (!file || !MoveFileExW(std::filesystem::path(temporary).c_str(),std::filesystem::path(target).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot save bookmarks");
            // Update the cached index without decoding the recording again.
            if (event_id==id)
            {
                auto& list=event_state["events"];
                for (auto e=list.begin();e!=list.end();) if((*e)["kind"]=="bookmark") e=list.erase(e); else ++e;
                for (const auto mark:marks) list.push_back({{"time",mark},{"kind","bookmark"},{"label","Bookmark"}});
            }
            return true;
        }
        catch(const std::exception& e) { message=e.what(); return false; }
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
                pending_return_session=session;
                // The native transition unwinds the game frame. Dispatch it
                // through Cbuf, never from a scheduler callback/destructor.
                command::execute("theater_return_native",false);
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

    bool split(const std::string& id, int start, int end)
    {
        const auto conversion = cod4_camera::get_status();
        if (stopping || root.empty() || demo_playback::playing() || conversion.loading || conversion.active) return false;
        entry selected;
        {
            std::lock_guard lock(mutex);
            const auto it = std::find_if(entries.begin(), entries.end(), [&](const entry& item) { return item.id == id; });
            if (it == entries.end() || !it->error.empty() || (!it->source && !it->details.converted)
                || start < 0 || end <= start || end > it->details.duration || busy.exchange(true)) return false;
            selected = *it;
            message = selected.source ? "Preparing COD4 clip conversion..." : "Splitting recording...";
            split_progress = 0;
        }
        const auto folder = root / "demos" / "clips";
        const auto stem = std::format("{}_{}-{}", std::filesystem::path(selected.path).stem().string(), start, end);
        if (selected.source)
        {
            scheduler::once([selected, folder, stem, start, end]
            {
                const auto accepted = cod4_camera::convert_clip(selected.path, folder / (stem + ".dm_h1"), start, end);
                {
                    std::lock_guard lock(mutex);
                    if (!accepted) message = cod4_camera::get_status().message.empty()
                        ? "Open Play Online and finish initialization before converting a clip." : cod4_camera::get_status().message;
                    else returning = true;
                    split_progress = -1;
                    busy = false;
                }
            }, scheduler::main);
            return true;
        }
        if (worker.joinable()) worker.join();
        worker = std::jthread([selected, folder, stem, start, end](std::stop_token stop)
        {
            const auto finish = gsl::finally([] { split_progress = -1; busy = false; });
            std::filesystem::path temporary;
            try
            {
                std::filesystem::create_directories(folder);
                auto target = folder / (stem + ".dm_h1");
                temporary = std::filesystem::path(target.string() + ".splitting");
                for (unsigned i = 1; std::filesystem::exists(target) || std::filesystem::exists(temporary); ++i)
                {
                    if (i > 9999) throw std::runtime_error("No unused clip filename available");
                    target = folder / std::format("{}.{:04}.dm_h1", stem, i);
                    temporary = std::filesystem::path(target.string() + ".splitting");
                }
                std::ifstream input(selected.path, std::ios::binary);
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (!input || !output) throw std::runtime_error("Cannot open clip input or output");
                const auto saved = demo_splitter::split_native(input, output, start, end, [&](int percent)
                { split_progress = percent; return !stop.stop_requested(); });
                output.flush();
                if (!output) throw std::runtime_error("Cannot finish writing clip");
                output.close();
                if (output.fail()) throw std::runtime_error("Cannot close split recording");
                std::ifstream check(temporary, std::ios::binary);
                const auto details = read_native(check);
                if (details.duration != saved.last - saved.first || !details.converted)
                    throw std::runtime_error("Saved clip metadata verification failed");
                check.close(); input.close();
                if (stop.stop_requested()) throw std::runtime_error("Split cancelled");
                std::filesystem::rename(temporary, target);
                scan(stop);
                std::lock_guard lock(mutex);
                message = std::format("Saved {} ({} frames). The original is unchanged.", target.filename().string(), saved.frames);
            }
            catch (const std::exception& e)
            {
                std::error_code ignored;
                if (!temporary.empty()) std::filesystem::remove(temporary, ignored);
                std::lock_guard lock(mutex); message = e.what(); ++revision;
            }
        });
        return true;
    }

    bool consume_return()
    {
        // Wait for the frontend map and HKS VM before opening the library.
        return !demo_playback::playing() && !cod4_camera::active() && game::VirtualLobby_Loaded() && returning.exchange(false);
    }

    class component final : public component_interface
    {
    public:
        void post_unpack() override
        {
            if (!game::environment::is_mp() || game::environment::is_dedi()) return;
            command::add("theater_return_native",[]
            {
                const auto session=std::exchange(pending_return_session,std::nullopt);
                if(!session || !demo_playback::playing() || demo_playback::session_id()!=*session)return;
                demo_playback::stopplay();
                returning=true;
                // User-requested normal disconnect, without a failure message.
                console::info("[SPAI_REPLAY_EXIT] requested native disconnect\n");
                game::Com_Error(game::ERR_DISCONNECT, "");
            });
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
