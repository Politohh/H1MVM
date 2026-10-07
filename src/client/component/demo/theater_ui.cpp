#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "theater_ui.hpp"
#include "demo_playback.hpp"
#include "demo_game.hpp"
#include "demo_gui.hpp"
#include "demo_input.hpp"
#include "demo_avidemo.hpp"
#include "theater_camera.hpp"
#include "theater_editor.hpp"
#include "component/scheduler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/ui_scripting/execution.hpp"
#include "component/ui_scripting.hpp"
#include "game/dvars.hpp"

namespace theater_ui
{
    namespace
    {
        std::atomic_bool ready{}, opened{}, editor{}, pending{}, keyboard{};
        std::atomic_bool visible{true};
        std::mutex mutex;
        nlohmann::json snapshot = {{"playing", false}, {"open", false}};
        std::vector<demo_timeline::event> observed, recorded;
        std::string session_path;
        int recording_pov{-1}, selected_key{-1};
        unsigned revision{}, session{};
        unsigned key_revision{};
        std::string key_signature;
        float desired_speed = 1.f;
        game::dvar_t* native_timeline{};

        int pov()
        {
            if (const auto& index = demo_playback::timeline_index(); index) return index->pov;
            // H1's snapshot playerState starts with the byte client number;
            // conversion's feed_snapshot writes the remapped POV into this byte.
            const auto* cl = game::mp::cl.get();
            return cl && cl->snap.valid ? *reinterpret_cast<const std::uint8_t*>(&cl->snap.ps) : -1;
        }

        void update()
        {
            const bool playing = demo_playback::playing();
            const auto path = playing ? demo_playback::current_path().string() : std::string{};
            if (path != session_path || session != demo_playback::session_id())
            {
                session_path = path;
                std::lock_guard lock(mutex);
                observed.clear(); ++revision; session = demo_playback::session_id();
                selected_key = -1; opened = false; editor = false; pending = false; visible = true;
            }
            if (!playing || demo_avidemo::is_capturing()) close();
            const auto now = demo_playback::current_time().value_or(0);
            const auto bounds = demo_playback::time_bounds();
            const int first = bounds ? bounds->first : 0;
            const int duration = bounds ? std::max(0, bounds->second - first) : 0;
            const auto* cl = game::mp::cl.get();
            const int presented = playing && cl && !demo_playback::paused() && !demo_playback::seeking()
                ? std::clamp(cl->serverTime, first, first + duration) : now;
            auto* speed = game::Dvar_FindVar("timescale");
            if (playing && speed)
            {
                // Rewinding can reset the replicated timescale to the server's 1x.
                if (speed->current.value != 1.f) desired_speed = speed->current.value;
                else if (desired_speed != 1.f) speed->current.value = desired_speed;
            }
            nlohmann::json data = {{"playing", playing}, {"open", opened && available()}, {"editor", editor_open()},
                {"enabled", (!native_timeline || native_timeline->current.enabled) && !demo_avidemo::is_capturing()}, {"session", session},
                {"visible", visible.load()},
                {"paused", demo_playback::paused()}, {"hudHidden", theater_camera::is_hud_hidden()},
                {"seeking", pending.load() || demo_playback::seeking()},
                {"name", playing ? demo_playback::current_path().filename().string() : ""},
                {"elapsed", std::clamp(presented - first, 0, duration)}, {"duration", duration},
                {"speed", speed ? speed->current.value : 1.f}, {"camera", static_cast<int>(theater_camera::get_mode())},
                {"cinematic", theater_camera::is_cinematic_playing()}, {"selected", selected_key},
                {"keys", nlohmann::json::array()}, {"events", nlohmann::json::array()}};
            if (playing)
            {
                const auto* map = game::Dvar_FindVar("mapname");
                const auto* mode = game::Dvar_FindVar("ui_gametype");
                const auto* map_title = map && map->current.string ? game::UI_GetMapDisplayName(map->current.string) : nullptr;
                const auto* mode_title = mode && mode->current.string ? game::UI_GetGameTypeDisplayName(mode->current.string) : nullptr;
                data["mapTitle"] = map_title ? map_title : "";
                data["modeTitle"] = mode_title ? mode_title : "";
                const auto& keys = theater_camera::get_keyframes();
                std::string signature;
                for (std::size_t i = 0; i < keys.size(); ++i)
                {
                    data["keys"].push_back({{"time", keys[i].demo_time - first}, {"index", i}});
                    signature += std::format("{}:{};", i, keys[i].demo_time);
                }
                if (signature != key_signature) { key_signature = signature; ++key_revision; }
                const int player = pov();
                std::lock_guard lock(mutex);
                const auto& index = demo_playback::timeline_index();
                const auto& events = index ? index->events : observed;
                for (const auto& e : events)
                {
                    const bool death = e.victim == player;
                    if ((death || e.attacker == player) && e.time >= first && e.time <= first + duration)
                        data["events"].push_back({{"time", e.time - first}, {"death", death}});
                }
                data["revision"] = revision;
            }
            data["keyRevision"] = key_revision;
            theater_editor::update();
            std::lock_guard lock(mutex);
            snapshot = std::move(data);
        }
    }

    void set_ready(bool value) { ready = value; if (!value) { close(); keyboard = false; } }
    void set_keyboard_available(bool value) { keyboard = value; }
    bool keyboard_available() { return ready && keyboard; }
    bool available() { return ready && (!native_timeline || native_timeline->current.enabled); }
    bool editor_open() { return editor && available(); }
    bool interactive() { return (opened || editor) && available(); }
    bool toggle()
    {
        if (!available() || !demo_playback::playing()) return false;
        editor = false; opened = !opened.load(); if (opened) visible = true; return true;
    }
    void close() { opened = false; editor = false; }
    bool handle_key_event(int key, int down)
    {
        // CL_KeyEvent normally supplies lowercase letters; accept either case.
        if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
        struct binding { int key; const char* action; int value; };
        static constexpr std::array bindings{
            binding{game::K_F1, "hud", 0}, binding{game::K_F4, "controls", 0},
            binding{game::K_F2, "camerastep", 0},
            binding{game::K_F3, "editortoggle", 0},
            binding{'k', "add", 0}, binding{'j', "cinematic", 0}, binding{'l', "clear", 0},
            binding{game::K_LEFTARROW, "skip", -5000}, binding{game::K_RIGHTARROW, "skip", 5000},
            binding{game::K_UPARROW, "speedstep", 1}, binding{game::K_DOWNARROW, "speedstep", -1}};
        static std::array<demo_input::shortcut_key, bindings.size()> keys;
        for (std::size_t i = 0; i < bindings.size(); ++i)
        {
            if (key != bindings[i].key) continue;
            // Arrow keys belong to the native editor's category navigation.
            const bool allowed = ready && (!editor_open() || key == game::K_F1 || key == game::K_F3 || key == game::K_F4) && demo_playback::playing() && demo_playback::is_replay_armed() &&
                demo_input::pause_allowed(*game::keyCatchers, keyboard_available(), demo_gui::is_open());
            const auto result = keys[i].event(down != 0, allowed);
            if (result == demo_input::shortcut_key::result::toggle)
            {
                (void)action(bindings[i].action, bindings[i].value);
            }
            return result != demo_input::shortcut_key::result::pass;
        }
        return false;
    }
    nlohmann::json state(int cached_revision, int cached_keys)
    {
        std::lock_guard lock(mutex);
        auto result = snapshot;
        // Avoid constructing thousands of fresh HKS tables at every UI poll.
        if (result.value("revision", -1) == cached_revision) result.erase("events");
        if (result.value("keyRevision", -1) == cached_keys) result.erase("keys");
        return result;
    }

    bool action(const std::string& name, int value)
    {
        if (name == "close") { close(); return true; }
        if (!demo_playback::playing() || !demo_playback::is_replay_armed()) return false;
        if (name == "controls")
        {
            if (!available()) return false;
            visible = !visible.load();
            opened = visible && !editor.load();
            return true;
        }
        if (name == "editortoggle")
        {
            if (!available()) return false;
            editor = !editor.load(); opened = !editor.load() && visible.load();
            return true;
        }
        if (name == "editor" || name == "timeline")
        {
            if (!available()) return false;
            editor = name == "editor"; opened = name == "timeline";
            if (opened) visible = true;
            return true;
        }
        static const std::set<std::string> commands{"hud", "pause", "seek", "skip", "speed", "speedstep", "camera", "camerastep", "add", "select",
            "delete", "edit", "retime", "clear", "cinematic", "advanced", "stop"};
        if (!commands.contains(name)) return false;
        if (name == "clear" && theater_camera::is_cinematic_playing()) return false;
        const bool seek = name == "seek" || name == "skip" || name == "edit";
        if (seek && (demo_playback::seeking() || pending.exchange(true))) return false;
        const auto path = demo_playback::current_path();
        const auto session_id = demo_playback::session_id();
        scheduler::once([name, value, path, session_id, seek]
        {
            const auto reset_pending = gsl::finally([seek] { if (seek) pending = false; });
            if (!demo_playback::playing() || demo_playback::current_path() != path || demo_playback::session_id() != session_id) return;
            const auto bounds = demo_playback::time_bounds();
            const auto current = demo_playback::current_time();
            const auto& keys = theater_camera::get_keyframes();
            if (name == "hud") theater_camera::toggle_hud();
            else if (name == "pause" && !demo_playback::seeking()) demo_playback::toggle_pause();
            else if ((name == "seek" || name == "skip") && bounds && current)
            {
                const auto target = name == "seek" ? static_cast<std::int64_t>(bounds->first) + value
                    : static_cast<std::int64_t>(*current) + value;
                (void)demo_playback::seek_to(static_cast<int>(std::clamp<std::int64_t>(target, bounds->first, bounds->second)));
            }
            else if (name == "speed")
            {
                if (auto* dvar = game::Dvar_FindVar("timescale"))
                    dvar->current.value = desired_speed = std::clamp(value / 1000.f, .025f, 4.f);
            }
            else if (name == "speedstep")
            {
                if (auto* dvar = game::Dvar_FindVar("timescale"))
                    dvar->current.value = desired_speed = demo_input::step_speed(dvar->current.value, value);
            }
            else if (name == "camerastep")
                theater_camera::set_mode(static_cast<theater_camera::camera_mode>((static_cast<int>(theater_camera::get_mode()) + 1) % 4));
            else if (name == "camera") theater_camera::set_mode(static_cast<theater_camera::camera_mode>(std::clamp(value, 0, 3)));
            else if (name == "add") { if (theater_camera::capture_keyframe()) selected_key = static_cast<int>(keys.size()) - 1; }
            else if (name == "select" && value >= 0 && value < keys.size()) selected_key = value;
            else if (name == "delete" && selected_key >= 0 && selected_key < keys.size())
            { theater_camera::remove_keyframe(selected_key); selected_key = -1; }
            else if (name == "edit" && selected_key >= 0 && selected_key < keys.size())
            {
                const auto key = keys[selected_key];
                theater_camera::set_freecam_pose(key.pose);
                (void)demo_playback::seek_to(key.demo_time);
            }
            else if (name == "retime" && selected_key >= 0 && selected_key < keys.size() && current)
                selected_key = static_cast<int>(theater_camera::set_keyframe_time(selected_key, *current));
            else if (name == "clear" && !theater_camera::is_cinematic_playing()) { theater_camera::clear_keyframes(); selected_key = -1; }
            else if (name == "cinematic")
            { if (theater_camera::is_cinematic_playing()) theater_camera::stop_cinematic(); else (void)theater_camera::start_cinematic(); }
            else if (name == "advanced") { close(); demo_gui::toggle(); }
            else if (name == "stop") { close(); demo_playback::stopplay(); }
        }, scheduler::main);
        return true;
    }

    void note_obituary(int time, int attacker, int victim)
    {
        demo_timeline::event e{time, attacker == 1022 ? 2046 : attacker, victim};
        if (!demo_timeline::valid_event(e, 0, INT_MAX)) return;
        std::lock_guard lock(mutex);
        if (!demo_playback::playing()) recording_pov = pov();
        auto& events = demo_playback::playing() ? observed : recorded;
        // Replayed snapshots can deliver an obituary more than once after a seek.
        if (events.size() < demo_timeline::max_events && std::find(events.begin(), events.end(), e) == events.end())
        { events.push_back(e); ++revision; }
    }
    void begin_recording()
    { std::lock_guard lock(mutex); recorded.clear(); recording_pov = pov(); }
    std::string recording_index(int first, int last)
    { std::lock_guard lock(mutex); return demo_timeline::encode(recording_pov, recorded, first, last); }

    class component final : public component_interface
    {
    public:
        void post_unpack() override
        {
            if (!game::environment::is_mp() || game::environment::is_dedi()) return;
            native_timeline = dvars::register_bool("demo_native_timeline", true, game::DVAR_FLAG_SAVED,
                "Native MWR theater timeline; F1 toggles HUD, F3 opens editor, F4 hides/shows controls");
            scheduler::loop(update, scheduler::main, 50ms);
            // Wall-clock scheduler: unlike replay-scaled LUI timers it runs during pause.
            scheduler::loop([]
            {
                if (ui_scripting::lui_running()) ui_scripting::notify("h1_theater_update", {});
            }, scheduler::lui, 50ms);
            command::add("demotheater", [] { (void)toggle(); });
            command::add("demotimeline", []
            {
                if (native_timeline) native_timeline->current.enabled = !native_timeline->current.enabled;
                close();
            });
            command::add("demotheaterstatus", []
            {
                const auto data = state();
                console::info("[theater] ready=%d playing=%d open=%d paused=%d seeking=%d time=%d/%d events=%zu keys=%zu editor=%d keyboard=%d catchers=0x%x\n",
                    available(), data.value("playing", false), interactive(), data.value("paused", false),
                    data.value("seeking", false), data.value("elapsed", 0), data.value("duration", 0),
                    data.contains("events") ? data["events"].size() : 0, data.contains("keys") ? data["keys"].size() : 0,
                    editor_open(), keyboard_available(), *game::keyCatchers);
            });
        }
    };
}
REGISTER_COMPONENT(theater_ui::component)
