#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "runtime_assets.hpp"
#include "cod4_camera.hpp"
#include "demo_playback.hpp"
#include "theater_ui.hpp"
#include "theater_weapons.hpp"
#include "demo_avidemo.hpp"
#include "component/command.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include <utils/flags.hpp>
#include <utils/io.hpp>

// Opt-in release smoke test. Only writes to a new, explicitly supplied output;
// source recordings and existing conversions are never modified.
namespace runtime_bundle_check
{
    class component final : public component_interface
    {
    public:
        void post_unpack() override
        {
            const auto source = utils::flags::get_flag("bundle-playback-check");
            const auto target = utils::flags::get_flag("bundle-output");
            if (!source || !target || !game::environment::is_mp()) return;
            scheduler::schedule([source = *source, target = *target] {
                static const auto started = std::chrono::steady_clock::now();
                static auto due = started + 12s;
                static int stage = 0;
                static nlohmann::json evidence;
                const auto finish = [&](bool ok, const std::string& message) {
                    evidence["ok"] = ok; evidence["message"] = message;
                    evidence["bundleRoot"] = runtime_assets::root().generic_string();
                    evidence["bundledOnly"] = utils::flags::has_flag("bundled-only");
                    utils::io::write_file(target + ".check.json", evidence.dump(2));
                    command::execute("quit", false);
                    return scheduler::cond_end;
                };
                try
                {
                    const auto now = std::chrono::steady_clock::now();
                    if (now - started > 180s) return finish(false, "Timed out: " + cod4_camera::get_status().message);
                    if (now < due) return scheduler::cond_continue;
                    if (stage == 0)
                    {
                        if (!game::VirtualLobby_Loaded() || game::Live_SyncOnlineDataFlags(0) || !game::Sys_IsDatabaseReady2())
                            return scheduler::cond_continue;
                        if (std::filesystem::exists(target) || std::filesystem::exists(target + ".tmp"))
                            return finish(false, "Use a new disposable output filename");
                        command::execute("cod4_facialHeads 1; cod4_snipers 1", false);
                        if (!cod4_camera::convert_clip(source, target, 0, 10000))
                            return finish(false, "Conversion did not start: " + cod4_camera::get_status().message);
                        ++stage;
                    }
                    else if (stage == 1)
                    {
                        if (!std::filesystem::exists(target) || !game::VirtualLobby_Loaded()) return scheduler::cond_continue;
                        evidence["convertedBytes"] = std::filesystem::file_size(target);
                        auto path = std::filesystem::path(target);
                        if (!demo_playback::startplay(path, false)) return finish(false, "Converted playback did not start");
                        ++stage; due = now + 3s;
                    }
                    else if (stage == 2)
                    {
                        if (!demo_playback::converted() || !game::CL_IsCgameInitialized() || game::VirtualLobby_Loaded())
                            return scheduler::cond_continue;
                        if (demo_playback::paused()) demo_playback::toggle_pause();
                        ++stage; due = now + 3s;
                    }
                    else
                    {
                        evidence["uiReady"] = theater_ui::available();
                        evidence["timeline"] = theater_ui::state();
                        evidence["weapons"] = theater_weapons::state();
                        evidence["ffmpeg"] = demo_avidemo::ffmpeg_path();
                        command::execute("cod4_facial_status; cod4_snipers_status", false);
                        const bool ok = demo_playback::converted() && theater_ui::available()
                            && theater_weapons::state().value("weaponSwapCatalogCount", 0) > 0
                            && std::filesystem::path(demo_avidemo::ffmpeg_path()) == runtime_assets::path("tools/ffmpeg.exe");
                        return finish(ok, ok ? "Fresh facial conversion and native playback passed" : "Native playback checks failed");
                    }
                }
                catch (const std::exception& error) { return finish(false, error.what()); }
                return scheduler::cond_continue;
            }, scheduler::main, 100ms);
        }
    };
}
REGISTER_COMPONENT(runtime_bundle_check::component)
