#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "theater_focus.hpp"
#include "theater_focus_gpu.hpp"
#include "theater_editor.hpp"
#include "theater_camera.hpp"
#include "theater_ui.hpp"
#include "demo_playback.hpp"
#include "demo_game.hpp"
#include "component/scheduler.hpp"
#include "component/command.hpp"
#include <utils/flags.hpp>
#include <utils/io.hpp>
#include <utils/hook.hpp>

namespace theater_focus
{
    namespace
    {
        game::dvar_t* mouse_focus{}, *focus_speed{};
        utils::hook::detour dof_pass;
        thread_local std::uint8_t* current_view{};
        depth_reader reader; // render thread only
        struct request { bool active{}; float x{.5f}, y{.5f}; unsigned epoch{}; };
        struct sample { float distance{}; unsigned epoch{}; ULONGLONG time{}; };
        std::mutex mutex;
        request requested;
        sample measured;
        bool depth_available{};
        unsigned render_epoch{}, session{}, focused_epoch{~0u};
        std::chrono::steady_clock::time_point last_update{};
        float point_x{.5f}, point_y{.5f};
        std::string feedback;
        bool check_run{};
        unsigned active_updates{};
        std::atomic<unsigned> render_calls{};
        std::string depth_detail;

        void render_sample(std::uint8_t* view, ID3D11DeviceContext* locked_context)
        {
            ++render_calls;
            request r;
            { std::lock_guard lock(mutex); r = requested; }
            if (!r.active) { reader.reset(); return; }
            if (render_epoch != r.epoch) { reader.reset(); render_epoch = r.epoch; }
            const auto now = GetTickCount64();
            // H1 MP RT 8 ($floatz): R32_TYPELESS resource sampled as R32_FLOAT depth.
            // RT table stride 64, GfxImage* at +0; verified allocation at 0x6AAE23.
            auto* image = *reinterpret_cast<game::GfxImage**>(0x10AA5F00_b);
            if (!image || !image->name || std::strcmp(image->name, "$floatz") != 0 || !image->texture.map)
            {
                reader.reset();
                std::lock_guard lock(mutex); depth_available = false;
                depth_detail = image && image->name ? image->name : "Missing depth image";
                return;
            }
            D3D11_TEXTURE2D_DESC desc{}; image->texture.map->GetDesc(&desc);
            if ((desc.Format != DXGI_FORMAT_R32_FLOAT && desc.Format != DXGI_FORMAT_R32_TYPELESS) || desc.SampleDesc.Count != 1)
            {
                reader.reset();
                std::lock_guard lock(mutex); depth_available = false;
                depth_detail = "Unsupported depth format " + std::to_string(desc.Format);
                return;
            }
            ID3D11Device* owner{}; image->texture.map->GetDevice(&owner);
            ID3D11DeviceContext* immediate{}; owner->GetImmediateContext(&immediate);
            const bool owns_context = immediate == locked_context;
            immediate->Release(); owner->Release();
            if (!owns_context)
            {
                std::lock_guard lock(mutex); depth_available = false;
                depth_detail = "DOF pass does not own the immediate context";
                return;
            }
            // H1 world depth is reversed Z in the lower 63/64 of the depth range;
            // the remaining 1/64 belongs to viewmodels. Invert the world CoC zero
            // from 0x934C0 -> 0x96EB0: focusZ = view[0x148] * (63/64) / depth.
            // view[0x158] is the optical focal length, NOT a projection coefficient.
            const float b = *reinterpret_cast<float*>(view + 0x148) * world_depth_range;
            const auto z = reader.sample(image->texture.map, r.x, r.y, 0.f, b);
            std::lock_guard lock(mutex);
            depth_available = true;
            if (check_run) depth_detail = std::format("{} {}x{} format {} bindings {} coefficient {}", image->name, desc.Width, desc.Height, static_cast<int>(desc.Format), desc.BindFlags, b);
            if (z && requested.active && requested.epoch == r.epoch) measured = {*z, r.epoch, now};
        }

        void dof_pass_stub(std::uint8_t* view)
        {
            const auto previous = current_view;
            current_view = view;
            dof_pass.invoke<void>(view);
            current_view = previous;
        }

        void begin_dof_commands_stub(void* source, void* state)
        {
            // H1 acquires its immediate-context lock INSIDE the DOF pass. A hook
            // at the pass entry runs too early and can race another render worker.
            utils::hook::invoke<void>(0x93920_b, source, state);
            if (current_view) render_sample(current_view, *static_cast<ID3D11DeviceContext**>(state));
        }
    }

    void update()
    {
        const auto now = GetTickCount64();
        const auto tick = std::chrono::steady_clock::now();
        const float dt = last_update.time_since_epoch().count() ? std::chrono::duration<float>(tick - last_update).count() : 0.f;
        last_update = tick;
        const bool enabled = mouse_focus && mouse_focus->current.enabled;
        const auto mode = theater_camera::get_mode();
        const bool cinematic = mode == theater_camera::THEATER_CAMERA_FREECAM || mode == theater_camera::THEATER_CAMERA_DOLLY;
        const auto hwnd = *reinterpret_cast<HWND*>(0xC9DD2E0_b);
        const bool foreground = hwnd && (check_run || GetForegroundWindow() == hwnd);
        const bool input_ready = input_allowed(*game::keyCatchers, theater_ui::keyboard_available());
        const bool active = enabled && cinematic && demo_playback::playing() && demo_playback::is_replay_armed() &&
            foreground && input_ready && theater_editor::dof_available();
        if (session != demo_playback::session_id())
        {
            session = demo_playback::session_id(); point_x = point_y = .5f;
            std::lock_guard lock(mutex); ++requested.epoch; measured = {};
        }
        // The engine recenters the Win32 cursor while flying. Only read its position
        // when the Theater cursor is actually free; otherwise retain the last focus point.
        if (active && theater_ui::interactive() && !check_run)
        {
            POINT cursor{}; RECT client{};
            if (GetCursorPos(&cursor) && ScreenToClient(hwnd, &cursor) && GetClientRect(hwnd, &client) && client.right > 0 && client.bottom > 0)
            {
                const auto* placement = game::ScrPlace_GetViewPlacement();
                if (placement && placement->realViewportSize[0] > 0 && placement->realViewportSize[1] > 0)
                {
                    point_x = std::clamp((cursor.x - placement->realViewportPosition[0]) / placement->realViewportSize[0], 0.f, 1.f);
                    point_y = std::clamp((cursor.y - placement->realViewportPosition[1]) / placement->realViewportSize[1], 0.f, 1.f);
                }
            }
        }
        sample value; bool available;
        unsigned epoch;
        {
            std::lock_guard lock(mutex);
            if (active != requested.active) { ++requested.epoch; measured = {}; }
            requested.active = active; requested.x = point_x; requested.y = point_y;
            value = measured; available = depth_available; epoch = requested.epoch;
        }
        feedback.clear();
        if (!enabled) { feedback = "Manual focus. Enable Mouse Autofocus to follow the cursor."; return; }
        if (!cinematic) feedback = "Mouse autofocus paused: switch to Free Camera or Dolly.";
        else if (!foreground) feedback = "Mouse autofocus paused: return to H1.";
        else if (!input_ready) feedback = "Mouse autofocus paused: close the console, chat or other menu.";
        else feedback = "Mouse autofocus: waiting for scene depth.";
        if (!active) return;
        if (check_run) ++active_updates;
        // The editor's resource guard is mandatory before any forced native DOF.
        for (const auto* name : {"r_dof_enable", "r_dof_physical_enable"})
            if (auto* d = game::Dvar_FindVar(name))
                if (std::strcmp(game::Dvar_ValueToString(d, true, d->current), "1") != 0)
                    game::Dvar_SetFromStringFromSource(d, "1", game::DVAR_SOURCE_INTERNAL);
        if (auto* d = game::Dvar_FindVar("r_dof_tweak"))
            if (d->current.integer != 1) game::Dvar_SetFromStringFromSource(d, "On", game::DVAR_SOURCE_INTERNAL);
        if (!available || value.epoch != epoch || !value.time || now - value.time > 250) return;
        auto* focus = game::Dvar_FindVar("r_dof_physical_focusDistance");
        if (!focus || (focus->type != game::value && focus->type != game::value_hashed)) return;
        const float target = std::clamp(value.distance, focus->domain.value.min, focus->domain.value.max);
        const float current = std::stof(game::Dvar_ValueToString(focus, true, focus->current));
        // A new demo/camera activation has no valid previous focus. Seed from
        // its first depth sample instead of racking from the old scene's plane.
        const float distance = focused_epoch != epoch ? target :
            smooth_focus(current, target, dt, focus_speed ? focus_speed->current.value : .2f);
        focused_epoch = epoch;
        const auto text = std::format("{:.6f}", distance);
        game::Dvar_SetFromStringFromSource(focus, text.c_str(), game::DVAR_SOURCE_INTERNAL);
        feedback = std::format("Mouse focus ACTIVE: {:.2f} m. {}", distance * .0254f,
            theater_ui::interactive() ? "Following the cursor, including behind the editor." : "F4 frees the cursor to move the focus point.");
    }

    std::string status() { return feedback; }

    class component final : public component_interface
    {
        void post_unpack() override
        {
            if (!game::environment::is_mp()) return;
            mouse_focus = game::demo_compat::register_bool("demo_dof_mouse", false, game::DVAR_FLAG_NONE,
                "Continuous scene-depth autofocus at the Theater cursor in freecam/dolly");
            focus_speed = game::demo_compat::register_float("demo_dof_speed", .2f, .001f, 1.f, game::DVAR_FLAG_NONE,
                "Mouse autofocus speed; higher is faster, one is instant");
            dof_pass.create(0x92640_b, dof_pass_stub);
            utils::hook::call(0x9267B_b, begin_dof_commands_stub);
            // Cursor sampling and focus interpolation must not inherit the 20 Hz
            // editor JSON refresh. GPU work remains inside the locked DOF pass.
            scheduler::loop(update, scheduler::main);
            check_run = utils::flags::has_flag("mousefocuscheck");
            if (check_run) scheduler::schedule([]
            {
                static int stage{};
                static auto started = GetTickCount64(), due = started + 12000;
                static std::string quality;
                static std::array<std::string, 2> names;
                static ULONGLONG focus_started{};
                static nlohmann::json report = {{"ok", false}};
                const auto get = [](const char* name)
                {
                    auto* d = game::Dvar_FindVar(name);
                    return d ? std::string(game::Dvar_ValueToString(d, true, d->current)) : std::string{};
                };
                const auto set = [](const char* name, const std::string& value)
                {
                    if (auto* d = game::Dvar_FindVar(name)) game::Dvar_SetFromStringFromSource(d, value.c_str(), game::DVAR_SOURCE_INTERNAL);
                };
                const auto finish = [&]
                {
                    set("demo_dof_mouse", "0"); set("r_dof_tweak", "Off");
                    theater_camera::set_mode(theater_camera::THEATER_CAMERA_FIRST_PERSON);
                    if (!names[0].empty()) set("cg_DrawFriendlyNames", names[0]);
                    if (!names[1].empty()) set("cg_DrawCrosshairNames", names[1]);
                    if (!quality.empty()) set("r_dof_limit", quality);
                    utils::io::write_file("mouse-focus-check.json", report.dump(2));
                    command::execute("quit", false);
                    return scheduler::cond_end;
                };
                const auto now = GetTickCount64();
                if (now - started > 120000) { report["error"] = "Timed out at stage " + std::to_string(stage); return finish(); }
                if (now < due) return scheduler::cond_continue;
                if (stage == 0)
                {
                    if (!game::VirtualLobby_Loaded()) return scheduler::cond_continue;
                    quality = get("r_dof_limit"); report["originalQuality"] = quality;
                    // Do not recreate a working D3D device just to run a focus check.
                    if (!theater_editor::dof_available())
                    { set("r_dof_limit", "2"); command::execute("vid_restart", false); }
                    ++stage; due = now + 5000;
                }
                else if (stage == 1)
                {
                    if (!game::VirtualLobby_Loaded() || !theater_editor::dof_available()) return scheduler::cond_continue;
                    command::execute("demoplay demos/theater_mouse_focus_check.dm_h1 fullpath", false);
                    ++stage; due = now + 6000;
                }
                else if (stage == 2)
                {
                    if (!demo_playback::playing() || !demo_playback::is_replay_armed() || demo_playback::seeking()) return scheduler::cond_continue;
                    if (demo_playback::current_path().filename() != "theater_mouse_focus_check.dm_h1")
                    { report["error"] = "Wrong test recording"; return finish(); }
                    if (!demo_playback::paused()) demo_playback::toggle_pause();
                    names = {get("cg_DrawFriendlyNames"), get("cg_DrawCrosshairNames")};
                    report["namesBefore"] = names;
                    theater_camera::set_mode(theater_camera::THEATER_CAMERA_FREECAM);
                    set("r_dof_enable", "0"); set("r_dof_physical_enable", "0");
                    set("r_dof_physical_fstop", "441.4"); // reproduce the unusable old slider range
                    set("demo_dof_mouse", "1"); set("demo_dof_speed", "1");
                    if (!theater_ui::action("editor", 0))
                    { report["error"] = "Could not open the native Theater editor"; return finish(); }
                    active_updates = 0; focus_started = now;
                    ++stage; due = now + 5000;
                }
                else if (stage == 3)
                {
                    report["editorOpen"] = theater_ui::editor_open();
                    { std::lock_guard lock(mutex); report["focusActiveInEditor"] = requested.active && measured.time && now - measured.time <= 250; }
                    if (!report["editorOpen"].get<bool>() || !report["focusActiveInEditor"].get<bool>() ||
                        !theater_editor::action("dof_cinematic", ""))
                    { report["error"] = "Mouse focus or cinematic defaults unavailable in the editor"; return finish(); }
                    ++stage; due = now + 3000;
                }
                else if (stage == 4 || stage == 5)
                {
                    sample value; bool available;
                    { std::lock_guard lock(mutex); value = measured; available = depth_available;
                        report["renderDetail"] = depth_detail; report["requestActive"] = requested.active; }
                    report["renderCalls"] = render_calls.load();
                    report["focusUpdatesPerSecond"] = active_updates * 1000. / std::max<ULONGLONG>(1, now - focus_started);
                    report["catchers"] = *game::keyCatchers;
                    report["mode"] = static_cast<int>(theater_camera::get_mode());
                    report["mouse"] = get("demo_dof_mouse"); report["override"] = get("r_dof_tweak");
                    report["resources"] = theater_editor::dof_available();
                    report["physicalEnabled"] = get("r_dof_physical_enable") == "1" && get("r_dof_enable") == "1";
                    report["namesDuring"] = {get("cg_DrawFriendlyNames"), get("cg_DrawCrosshairNames")};
                    report["aperture"] = get("r_dof_physical_fstop");
                    report["bokehQuality"] = get("r_dof_physical_bokehPreset");
                    const auto editor_state = theater_editor::state(-1);
                    for (const auto& c : editor_state.at("controls"))
                    {
                        if (c.at("name") == "r_dof_physical_fstop") report["apertureRange"] = {c.at("min"),c.at("max")};
                        if (c.at("name") == "r_dof_physical_focusDistance") report["logarithmicFocus"] = c.value("logarithmic", false);
                    }
                    report[stage == 4 ? "center" : "right"] = {{"depthAvailable", available}, {"distance", value.distance},
                        {"sampleAge", value.time ? now - value.time : 0}, {"focus", get("r_dof_physical_focusDistance")}};
                    const bool hidden = get("cg_DrawFriendlyNames") == "0" && get("cg_DrawCrosshairNames") == "0";
                    if (!available || !value.time || now - value.time > 500 || !hidden || !report["physicalEnabled"].get<bool>())
                    { report["error"] = "Depth unavailable, stale or names visible"; return finish(); }
                    report["namesHidden"] = hidden;
                    if (stage == 4) point_x = .8f;
                    else { set("demo_dof_mouse", "0"); theater_camera::set_mode(theater_camera::THEATER_CAMERA_FIRST_PERSON); theater_ui::close(); }
                    ++stage; due = now + 3000;
                }
                else
                {
                    report["namesAfter"] = {get("cg_DrawFriendlyNames"), get("cg_DrawCrosshairNames")};
                    report["ok"] = get("cg_DrawFriendlyNames") == names[0] && get("cg_DrawCrosshairNames") == names[1] &&
                        std::abs(std::stof(report["aperture"].get<std::string>()) - 2.8f) < .01f &&
                        report["apertureRange"] == nlohmann::json::array({1,22}) && report.value("logarithmicFocus",false);
                    return finish();
                }
                report["stage"] = stage;
                report["renderCalls"] = render_calls.load();
                { std::lock_guard lock(mutex); report["requestActive"] = requested.active; }
                utils::io::write_file("mouse-focus-progress.json", report.dump(2));
                return scheduler::cond_continue;
            }, scheduler::main, 100ms);
        }
    };
}

REGISTER_COMPONENT(theater_focus::component)
