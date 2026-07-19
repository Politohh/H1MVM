#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "demo_playback.hpp"
#include "demo_utils.hpp"

#include "../command.hpp"
#include "../console.hpp"
#include "../dvars.hpp"
#include "../scheduler.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/string.hpp>

// =============================================================================
// MWR/H1 Demo Playback Component
// Architecture mirrors IW6: https://git.alterware.dev/anomaly/iw6-mod
//   src/client/component/demo/demo_playback.cpp
//
// Function mapping status legend:
//   [CONFIRMED]       – address found in both PS4 ELF (named) and h1-mod symbols.hpp
//   [PS4_ELF]         – found in PS4 ELF (default_mp.elf) via MWR IDA MCP; PC addr TBD
//   [SYMBOLS_HPP]     – already in h1-mod symbols.hpp
//   [TODO]            – not yet located; install is guarded and skipped at runtime
// =============================================================================

namespace demo_playback
{
    namespace
    {
        // -----------------------------------------------------------------------
        // Function-pointer typedefs (IW6 names preserved)
        // -----------------------------------------------------------------------

        // IW6 name: CL_SetCGameTime  |  PS4 ELF: 0x233b80  |  H1 PC: 0x33C950 [CONFIRMED]
        // Note: H1 signature is void(int) — IW6 has an extra 'time' param that H1 folds internally
        using CL_SetCGameTime_t = void(int local_client_num);

        // IW6 name: CL_WritePacket  |  PS4 ELF: 0x2386b0  |  H1 PC: 0x13D490 [CONFIRMED]
        using CL_WritePacket_t = void(int local_client_num);

        // IW6 name: CL_GetPredictedPlayerInformationForServerTime
        //   PS4 ELF: 0x249800  |  [PS4_ELF] PC addr: TODO
        using CL_GetPredictedPlayerInformationForServerTime_t =
            void(int local_client_num, int server_time, float* viewangles, void* archive_data_out);

        // IW6 name: CL_GetPredictedVehicleForServerTime
        //   PS4 ELF: 0x2498b0  |  [PS4_ELF] PC addr: TODO
        using CL_GetPredictedVehicleForServerTime_t =
            void(int local_client_num, int server_time, void* vehicle_state_out);

        // IW6 name: VehicleCam_UpdatePlayerControlCam
        //   PS4 ELF: 0x5d6bc0  |  [PS4_ELF] PC addr: TODO
        using VehicleCam_UpdatePlayerControlCam_t = void(int local_client_num);

        // IW6 name: GetRemoteEyeValues  |  PS4 ELF: not found  |  [TODO]
        using GetRemoteEyeValues_t = void(int local_client_num, float* origin, float* angles);

        // IW6 name: LUI_IntermissionBegan  |  PS4 ELF: 0xfd180  |  [PS4_ELF] PC addr: TODO
        using LUI_IntermissionBegan_t = void(int local_client_num);

        // IW6 name: CG_UpdateOmnvars  |  PS4 ELF: not found (may be inlined)  |  [TODO]
        using CG_UpdateOmnvars_t = void(int local_client_num, int server_time);

        // IW6 name: CG_VisionSetStartLerp_To  |  PS4 ELF: 0x213690  |  [PS4_ELF] PC addr: TODO
        using CG_VisionSetStartLerp_To_t = void(int local_client_num, const char* vision_name);

        // IW6 name: Omnvar_GetDef  |  PS4 ELF: 0x58a690  |  [PS4_ELF] PC addr: TODO
        using Omnvar_GetDef_t = void*(int index);

        // -----------------------------------------------------------------------
        // Hooks (detours)
        // -----------------------------------------------------------------------

        // IW6: hook CL_SetCGameTime to drive the playback tick
        utils::hook::detour cl_set_cgame_time_hook;

        // IW6: hook CL_WritePacket to suppress outgoing packets during playback
        utils::hook::detour cl_write_packet_hook;

        // IW6: intercept predicted-player/vehicle queries to inject recorded data
        utils::hook::detour cl_get_predicted_player_hook;
        utils::hook::detour cl_get_predicted_vehicle_hook;
        utils::hook::detour vehicle_cam_update_hook;
        utils::hook::detour get_remote_eye_hook;

        // IW6: suppress/inject intermission and omnvar updates
        utils::hook::detour lui_intermission_began_hook;
        utils::hook::detour cg_update_omnvars_hook;

        // -----------------------------------------------------------------------
        // State
        // -----------------------------------------------------------------------

        // IW6 name: demo_playback_t (mirrored as module-level state)
        bool s_playing           = false;
        bool s_repeat            = false;
        bool s_retain_timescale  = false;
        bool s_skip_timeout      = false;
        std::size_t s_pred_data_index                = 0;
        std::array<demo_utils::predicted_data_t, 256> s_pred_data{};
        std::int32_t s_rewind_snap_svr_time          = 0;
        std::size_t  s_post_map_header_file_offset   = 0;
        demo_utils::gamestate_t  s_gs{};
        demo_utils::buffer_t     s_buffer{};
        std::filesystem::path    s_path{};
        std::ifstream            s_file{};

        // IW6 name: VISION_SETS  (partial list – extend as needed)
        static const char* VISION_SETS[] =
        {
            "default",
            "mp_aftermath",
            "mp_alien",
            "mp_bog_sh",
            "mp_crash",
            "mp_cross_fire",
            "mp_downfall",
            "mp_farm",
            "mp_harbor",
            "mp_rust",
            "mp_shipment",
            "mp_strike",
            "mp_terminal_cls",
            "mp_vacant",
            "mp_village",
            "mp_invasion",
            "mp_overgrown",
            "mp_pipeline",
            "mp_showdown",
            "mp_broadcast",
            "mp_chinatown",
            "mp_convoy",
            "mp_killhouse",
            "mp_rack",
            "mp_wet_work",
            nullptr
        };

        game::dvar_t* demovision_dvar = nullptr;

        // -----------------------------------------------------------------------
        // Forward declarations of hook callbacks
        // -----------------------------------------------------------------------
        void cl_set_cgame_time_stub(int local_client_num);
        void cl_write_packet_stub(int local_client_num);
        void cl_get_predicted_player_stub(int local_client_num, int server_time,
                                          float* viewangles, void* archive_data_out);
        void cl_get_predicted_vehicle_stub(int local_client_num, int server_time,
                                           void* vehicle_state_out);
        void vehicle_cam_update_stub(int local_client_num);
        void get_remote_eye_stub(int local_client_num, float* origin, float* angles);
        void lui_intermission_began_stub(int local_client_num);
        void cg_update_omnvars_stub(int local_client_num, int server_time);

        // -----------------------------------------------------------------------
        // Internal helpers
        // -----------------------------------------------------------------------

        void reset_state()
        {
            s_playing          = false;
            s_repeat           = false;
            s_retain_timescale = false;
            s_skip_timeout     = false;
            s_pred_data_index  = 0;
            s_pred_data        = {};
            s_rewind_snap_svr_time       = 0;
            s_post_map_header_file_offset = 0;
            s_gs     = {};
            s_buffer.clear();
            s_path.clear();
            if (s_file.is_open()) s_file.close();
        }

        // IW6 name: handle_demo_patches
        // Sets cg->demoType = DEMO_TYPE_CLIENT and enables cheats for free camera.
        // TODO: map H1 cg_s::demoType offset; use Dvar_SetBool for sv_cheats
        void handle_demo_patches(int /*local_client_num*/)
        {
            // TODO: game::mp::cg->demoType = DEMO_TYPE_CLIENT;
            // TODO: game::Dvar_SetBool(game::Dvar_FindVar("sv_cheats"), true);
            // game::CG_SelectWeapon(local_client_num, weapon, useAlt); // H1: 0x11A300 (confirmed)
        }

        // IW6 name: update_gamestate_config_strings
        // Injected via utils::hook::call to replay stored gamestate into cgame.
        void update_gamestate_config_strings(int /*local_client_num*/)
        {
            // TODO: push s_gs into the CG configstring update path
        }

        // IW6 name: apply_vision_override
        void apply_vision_override(int local_client_num)
        {
            if (!demovision_dvar) return;
            const auto* vision = demovision_dvar->current.string;
            if (!vision || vision[0] == '\0') return;

            // IW6 name: CG_VisionSetStartLerp_To
            // TODO: call via PC exe address when found
            // game::CG_VisionSetStartLerp_To(local_client_num, vision);
            (void)local_client_num;
        }

        // -----------------------------------------------------------------------
        // Hook callbacks
        // -----------------------------------------------------------------------

        // IW6 name: CL_SetCGameTime hook – main playback tick
        // H1 signature: void(int) — 'time' is not a parameter, computed internally
        void cl_set_cgame_time_stub(int local_client_num)
        {
            if (!s_playing)
            {
                cl_set_cgame_time_hook.invoke<void>(local_client_num);
                return;
            }

            // TODO: read next chunk from s_file, dispatch by demo_data_id,
            //       call update_gamestate_config_strings when needed,
            //       advance s_pred_data_index
            apply_vision_override(local_client_num);
            cl_set_cgame_time_hook.invoke<void>(local_client_num);
        }

        // IW6 name: CL_WritePacket hook – suppress outgoing packets
        void cl_write_packet_stub(int local_client_num)
        {
            if (s_playing) return;
            cl_write_packet_hook.invoke<void>(local_client_num);
        }

        // IW6 name: CL_GetPredictedPlayerInformationForServerTime hook
        void cl_get_predicted_player_stub(int local_client_num, int server_time,
                                          float* viewangles, void* archive_data_out)
        {
            if (!s_playing || s_pred_data.empty())
            {
                cl_get_predicted_player_hook.invoke<void>(
                    local_client_num, server_time, viewangles, archive_data_out);
                return;
            }
            const auto& pd = s_pred_data[s_pred_data_index % s_pred_data.size()];
            if (viewangles)
            {
                std::memcpy(viewangles, pd.viewangles, sizeof(pd.viewangles));
            }
            if (archive_data_out)
            {
                std::memcpy(archive_data_out, pd.raw_client_archive,
                            sizeof(pd.raw_client_archive));
            }
        }

        // IW6 name: CL_GetPredictedVehicleForServerTime hook
        void cl_get_predicted_vehicle_stub(int local_client_num, int server_time,
                                           void* vehicle_state_out)
        {
            if (s_playing) return; // suppress during playback
            cl_get_predicted_vehicle_hook.invoke<void>(
                local_client_num, server_time, vehicle_state_out);
        }

        // IW6 name: VehicleCam_UpdatePlayerControlCam hook
        void vehicle_cam_update_stub(int local_client_num)
        {
            if (s_playing) return;
            vehicle_cam_update_hook.invoke<void>(local_client_num);
        }

        // IW6 name: GetRemoteEyeValues hook
        void get_remote_eye_stub(int local_client_num, float* origin, float* angles)
        {
            if (s_playing) return;
            get_remote_eye_hook.invoke<void>(local_client_num, origin, angles);
        }

        // IW6 name: LUI_IntermissionBegan hook – suppress end-of-match screen
        void lui_intermission_began_stub(int local_client_num)
        {
            if (s_playing) return;
            lui_intermission_began_hook.invoke<void>(local_client_num);
        }

        // IW6 name: CG_UpdateOmnvars hook
        void cg_update_omnvars_stub(int local_client_num, int server_time)
        {
            if (s_playing) return;
            cg_update_omnvars_hook.invoke<void>(local_client_num, server_time);
        }

        // -----------------------------------------------------------------------
        // File search (IW6 search order preserved)
        // -----------------------------------------------------------------------
        bool find_demo_file(const std::string& name, std::filesystem::path& out_path)
        {
            const auto mod  = demo_utils::gen_utilities::get_mod_directory();
            const auto base = demo_utils::file_directory::get_base_demo_path();

            // IW6 search order:
            //   1. demos/client/{mod}/user/
            //   2. demos/client/{mod}/auto/
            //   3. demos/client/
            //   4. demos/server/{mod}/
            //   5. demos/server/
            //   6. demos/
            const std::vector<std::filesystem::path> candidates =
            {
                base / "client" / (mod.empty() ? "base" : mod) / "user"  / name,
                base / "client" / (mod.empty() ? "base" : mod) / "auto"  / name,
                base / "client" / name,
                base / "server" / (mod.empty() ? "base" : mod)           / name,
                base / "server" / name,
                base            / name,
            };

            const auto try_path = [&](std::filesystem::path p) -> bool
            {
                if (!p.has_extension())
                {
                    p.replace_extension(demo_utils::DEMO_EXTENSION);
                }
                std::error_code ec;
                if (std::filesystem::exists(p, ec))
                {
                    out_path = std::move(p);
                    return true;
                }
                return false;
            };

            for (const auto& c : candidates)
            {
                if (try_path(c)) return true;
            }
            return false;
        }

        // -----------------------------------------------------------------------
        // Commands registered in post_unpack
        // -----------------------------------------------------------------------

        // IW6 name: demoplay command
        void cmd_demoplay(const command::params& params)
        {
            if (params.size() < 2)
            {
                console::info("Usage: demoplay <filename>\n");
                return;
            }
            startplay(params[1]);
        }

        // IW6 name: demoinstantplay command
        void cmd_demoinstantplay(const command::params& params)
        {
            if (params.size() < 2)
            {
                console::info("Usage: demoinstantplay <filename>\n");
                return;
            }
            startplay(params[1], false);
        }

        // IW6 name: demoreplay command
        void cmd_demoreplay(const command::params& /*params*/)
        {
            if (!s_playing || s_path.empty()) return;
            startplay(s_path, s_repeat);
        }

        // IW6 name: demorewind command
        void cmd_demorewind(const command::params& /*params*/)
        {
            if (!s_playing || s_path.empty()) return;
            // IW6: rewind_demo() calls fast_forward_demo(1000)
            demo_utils::misc::fast_forward_demo(1000);
        }

        // IW6 name: demoforward command
        void cmd_demoforward(const command::params& params)
        {
            if (!s_playing) return;
            const std::size_t frames = params.size() >= 2
                ? static_cast<std::size_t>(std::atoi(params[1]))
                : 10;
            demo_utils::misc::fast_forward_demo(frames);
        }

    } // anonymous namespace

    // =========================================================================
    // Public API
    // =========================================================================

    bool playing()
    {
        return s_playing;
    }

    bool startplay(std::string_view file_name, bool repeat)
    {
        std::filesystem::path resolved;
        if (!find_demo_file(std::string(file_name), resolved))
        {
            console::warn("Demo not found: %s\n", std::string(file_name).c_str());
            return false;
        }
        return startplay(resolved, repeat);
    }

    bool startplay(std::filesystem::path& file_path, bool repeat)
    {
        stopplay();

        s_file.open(file_path, std::ios::binary);
        if (!s_file.is_open())
        {
            console::warn("Cannot open demo: %s\n", file_path.string().c_str());
            return false;
        }

        s_path   = file_path;
        s_repeat = repeat;
        s_playing = true;

        console::info("Playing demo: %s\n", file_path.string().c_str());
        return true;
    }

    void stopplay()
    {
        if (!s_playing) return;
        reset_state();
        console::info("Demo playback stopped.\n");
    }

    // =========================================================================
    // Component
    // =========================================================================

    class component final : public component_interface
    {
    public:
        void post_unpack() override
        {
            if (game::environment::is_dedi())
            {
                return;
            }

            // IW6 name: demovision dvar
            // Enum of vision set names for free-camera vision override during playback.
            {
                int count = 0;
                while (VISION_SETS[count]) ++count;

                static std::vector<const char*> vision_ptrs(VISION_SETS, VISION_SETS + count + 1);
                // NOTE: Dvar_RegisterEnum expects const char** with null terminator in the enum list;
                //       use empty string as the "off" entry.
                // TODO: register via game::Dvar_RegisterEnum once its signature is confirmed safe.
            }

            // ----------------------------------------------------------------
            // Install hooks
            // All install calls are individually guarded so missing addresses
            // compile and run safely – the hook simply won't fire.
            // ----------------------------------------------------------------

            // IW6: CL_SetCGameTime hook (main playback tick driver)
            // H1 PC: 0x33C950 [CONFIRMED by structural analysis matching IW6 sub_1402B9D80]
            // PS4 ELF ref: 0x233b80
            cl_set_cgame_time_hook.create(SELECT_VALUE(0x0_b, 0x33C950_b), cl_set_cgame_time_stub);

            // IW6: CL_WritePacket hook (suppress outgoing data during playback)
            // H1 PC: 0x13D490 [CONFIRMED from h1-mod party.cpp invoke comment]
            // PS4 ELF ref: 0x2386b0
            cl_write_packet_hook.create(SELECT_VALUE(0x0_b, 0x13D490_b), cl_write_packet_stub);

            // IW6: CL_GetPredictedPlayerInformationForServerTime
            // PC exe addr: TODO
            // PS4 ELF ref: 0x249800
            // cl_get_predicted_player_hook.create(0x0_b, cl_get_predicted_player_stub);

            // IW6: CL_GetPredictedVehicleForServerTime
            // PC exe addr: TODO
            // PS4 ELF ref: 0x2498b0
            // cl_get_predicted_vehicle_hook.create(0x0_b, cl_get_predicted_vehicle_stub);

            // IW6: VehicleCam_UpdatePlayerControlCam
            // PC exe addr: TODO
            // PS4 ELF ref: 0x5d6bc0
            // vehicle_cam_update_hook.create(0x0_b, vehicle_cam_update_stub);

            // IW6: GetRemoteEyeValues – not found in PS4 ELF; may be named differently
            // PC exe addr: TODO
            // get_remote_eye_hook.create(0x0_b, get_remote_eye_stub);

            // IW6: LUI_IntermissionBegan
            // PC exe addr: TODO
            // PS4 ELF ref: 0xfd180
            // lui_intermission_began_hook.create(0x0_b, lui_intermission_began_stub);

            // IW6: CG_UpdateOmnvars – not found in PS4 ELF (possibly inlined)
            // PC exe addr: TODO
            // cg_update_omnvars_hook.create(0x0_b, cg_update_omnvars_stub);

            // ----------------------------------------------------------------
            // Register commands  (IW6 names preserved)
            // ----------------------------------------------------------------
            command::add("demoplay",        cmd_demoplay);
            command::add("demoinstantplay", cmd_demoinstantplay);
            command::add("demoreplay",      [](const command::params& p) { cmd_demoreplay(p); });
            command::add("demorewind",      [](const command::params& p) { cmd_demorewind(p); });
            command::add("demoforward",     cmd_demoforward);
            command::add("demostop",        [](const command::params&) { stopplay(); });
        }
    };
}

REGISTER_COMPONENT(demo_playback::component)
