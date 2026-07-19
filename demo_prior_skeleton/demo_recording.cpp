#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "demo_recording.hpp"
#include "demo_utils.hpp"
#include "demo_playback.hpp"

#include "../command.hpp"
#include "../console.hpp"
#include "../scheduler.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/string.hpp>

// =============================================================================
// MWR/H1 Demo Recording Component (client-side)
// Architecture mirrors IW6: https://git.alterware.dev/anomaly/iw6-mod
//   src/client/component/demo/demo_recording.cpp
//
// Client-side recording hooks into the message-receive path to capture
// every server snapshot and predicted playerState for replay.
// =============================================================================

namespace demo_recording
{
    namespace
    {
        // -----------------------------------------------------------------------
        // State
        // -----------------------------------------------------------------------
        bool s_recording = false;
        std::filesystem::path s_path{};
        std::ofstream         s_file{};
        demo_utils::buffer_t  s_frame_buffer{};
        std::int32_t          s_svr_cmd_seq_at_start{};

        // IW6: hook CL_SetCGameTime to capture each frame's predicted data
        utils::hook::detour cl_set_cgame_time_hook;

        // IW6: hook the configstring/gamestate path to snapshot initial gamestate
        utils::hook::detour gamestate_update_hook;

        // -----------------------------------------------------------------------
        // Hook callbacks
        // -----------------------------------------------------------------------

        // IW6 name: CL_SetCGameTime hook – capture frame during recording
        // H1 signature: void(int) — 'time' not a parameter, computed internally
        void cl_set_cgame_time_stub(int local_client_num)
        {
            cl_set_cgame_time_hook.invoke<void>(local_client_num);

            if (!s_recording || !s_file.is_open()) return;

            // TODO: read current viewangles and ClientArchiveData from
            //       CL_GetPredictedPlayerInformationForServerTime and write via
            //       demo_utils::serialization::write_predicted_data()
            demo_utils::predicted_data_t pd{};
            // TODO: populate pd.viewangles from game::mp::cl->snap angles
            // TODO: populate pd.raw_client_archive from client archive
            (void)pd;
        }

        // IW6 name: gamestate update hook – capture initial gameState_t
        // IW6 addr: 0x1402CCD56 (call-patch target in IW6 demo_playback.cpp)
        // H1 addr: TODO
        void gamestate_update_stub(int local_client_num)
        {
            gamestate_update_hook.invoke<void>(local_client_num);
            if (!s_recording) return;

            // TODO: snapshot current gameState_t from game::mp::cls->gameState
            // and write via demo_utils::serialization::write_gamestate_data()
        }

        // -----------------------------------------------------------------------
        // Internal helpers
        // -----------------------------------------------------------------------

        void reset_state()
        {
            s_recording = false;
            if (s_file.is_open()) s_file.close();
            s_path.clear();
            s_frame_buffer.clear();
            s_svr_cmd_seq_at_start = 0;
        }

        bool open_file_for_record(const std::string& file_name, bool overwrite)
        {
            const auto mod = demo_utils::gen_utilities::get_mod_directory();
            std::filesystem::path path;

            if (file_name.empty())
            {
                path = demo_utils::file_directory::create_path_auto_demo(mod);
            }
            else
            {
                path = demo_utils::file_directory::create_path_user_demo(mod, file_name);
            }

            std::error_code ec;
            if (!overwrite && std::filesystem::exists(path, ec))
            {
                console::warn("Demo already exists: %s\n", path.string().c_str());
                return false;
            }

            s_file.open(path, std::ios::binary | std::ios::trunc);
            if (!s_file.is_open())
            {
                console::warn("Cannot open demo for writing: %s\n", path.string().c_str());
                return false;
            }

            s_path = path;
            return true;
        }

        void write_file_header()
        {
            demo_utils::buffer_t hdr;
            demo_utils::serialization::write_general_header(hdr, std::string(demo_utils::DEMO_CODE_VERSION));
            demo_utils::serialization::write_mod_header(hdr, demo_utils::gen_utilities::get_mod_directory());
            demo_utils::serialization::write_map_header(
                hdr,
                demo_utils::gen_utilities::get_mapname(),
                demo_utils::gen_utilities::get_gametype());
            s_file.write(reinterpret_cast<const char*>(hdr.get()), static_cast<std::streamsize>(hdr.size()));
        }

        void write_file_footer()
        {
            demo_utils::buffer_t ftr;
            demo_utils::serialization::write_general_footer(ftr);
            demo_utils::serialization::write_end_of_file(ftr);
            s_file.write(reinterpret_cast<const char*>(ftr.get()), static_cast<std::streamsize>(ftr.size()));
        }

        // -----------------------------------------------------------------------
        // Commands
        // -----------------------------------------------------------------------

        // IW6 name: demorecord command
        void cmd_demorecord(const command::params& params)
        {
            if (s_recording)
            {
                stoprecord();
                return;
            }

            const std::string name = params.size() >= 2 ? params[1] : "";
            startrecord(name, false);
        }

    } // anonymous namespace

    // =========================================================================
    // Public API
    // =========================================================================

    bool recording()
    {
        return s_recording;
    }

    bool startrecord(std::string_view file_name, bool overwrite)
    {
        if (s_recording)
        {
            console::warn("Already recording a demo.\n");
            return false;
        }

        if (demo_playback::playing())
        {
            console::warn("Cannot record during demo playback.\n");
            return false;
        }

        // Check connected state
        if (!game::CL_IsCgameInitialized())
        {
            console::warn("Must be in-game to record a demo.\n");
            return false;
        }

        if (!open_file_for_record(std::string(file_name), overwrite))
        {
            return false;
        }

        s_recording = true;
        write_file_header();

        console::info("Recording demo: %s\n", s_path.string().c_str());
        return true;
    }

    bool stoprecord()
    {
        if (!s_recording) return false;

        write_file_footer();
        const auto saved_path = s_path.string();
        reset_state();

        console::info("Demo saved: %s\n", saved_path.c_str());
        return true;
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

            // IW6: CL_SetCGameTime hook – capture each frame's predicted state
            // H1 PC: 0x33C950 [CONFIRMED]  |  PS4 ELF ref: 0x233b80
            // NOTE: demo_playback.cpp owns the CL_SetCGameTime hook. Recording captures
            //       predicted data via the scheduler or a dedicated per-frame callback
            //       from the playback hook's chain. Do NOT double-hook same address here.
            // cl_set_cgame_time_hook.create(SELECT_VALUE(0x0_b, 0x33C950_b), cl_set_cgame_time_stub);

            // IW6: gamestate update call-patch (IW6 addr: 0x1402CCD56)
            // H1 PC exe addr: TODO
            // utils::hook::call(0x0_b, gamestate_update_stub);

            command::add("demorecord", cmd_demorecord);
        }
    };
}

REGISTER_COMPONENT(demo_recording::component)
