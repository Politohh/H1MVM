#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "demo_sv_recording.hpp"
#include "demo_utils.hpp"

#include "../command.hpp"
#include "../console.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/string.hpp>

// =============================================================================
// MWR/H1 Server-Side Demo Recording Component
// Architecture mirrors IW6: https://git.alterware.dev/anomaly/iw6-mod
//   src/client/component/demo/demo_sv_recording.cpp
//
// Hooks SV_WriteSnapshotToClient / SV_BuildClientSnapshot to capture raw
// server snapshots per-client for higher-fidelity replay.
//
// Max clients: game::MAX_CLIENTS (typically 18 in H1 MP)
// =============================================================================

namespace demo_sv_recording
{
    namespace
    {
        constexpr std::size_t MAX_CLIENTS = 18;

        // -----------------------------------------------------------------------
        // Per-client state
        // -----------------------------------------------------------------------
        struct client_record_state
        {
            bool                  recording  = false;
            std::filesystem::path path{};
            std::ofstream         file{};
            demo_utils::buffer_t  frame_buffer{};
        };

        std::array<client_record_state, MAX_CLIENTS> s_clients{};

        // -----------------------------------------------------------------------
        // Hooks
        // -----------------------------------------------------------------------

        // IW6 name: SV_WriteSnapshotToClient hook – capture each server snapshot
        // PS4 ELF ref: 0x50c7e0  |  PC exe addr: TODO
        utils::hook::detour sv_write_snapshot_hook;

        // IW6 name: SV_BuildClientSnapshot hook
        // PS4 ELF ref: 0x508ce0  |  PC exe addr: TODO
        utils::hook::detour sv_build_client_snapshot_hook;

        void sv_write_snapshot_stub(int client_num, void* msg)
        {
            sv_write_snapshot_hook.invoke<void>(client_num, msg);

            if (static_cast<std::size_t>(client_num) >= MAX_CLIENTS) return;
            auto& cs = s_clients[client_num];
            if (!cs.recording || !cs.file.is_open()) return;

            // TODO: extract raw snapshot bytes from msg (game::msg_t*) and write
            // via demo_utils::serialization_server::sv_write_predicted_data()
        }

        void sv_build_client_snapshot_stub(int client_num)
        {
            sv_build_client_snapshot_hook.invoke<void>(client_num);

            if (static_cast<std::size_t>(client_num) >= MAX_CLIENTS) return;
            auto& cs = s_clients[client_num];
            if (!cs.recording) return;

            // TODO: capture built snapshot for the client
        }

        // -----------------------------------------------------------------------
        // Helpers
        // -----------------------------------------------------------------------

        void reset_client(std::size_t client_num)
        {
            if (client_num >= MAX_CLIENTS) return;
            auto& cs       = s_clients[client_num];
            cs.recording   = false;
            if (cs.file.is_open()) cs.file.close();
            cs.path.clear();
            cs.frame_buffer.clear();
        }

        bool open_file_for_client(std::size_t client_num, const std::string& file_name, bool overwrite)
        {
            const auto mod  = demo_utils::gen_utilities::get_mod_directory();
            const auto path = file_name.empty()
                ? demo_utils::file_directory_server::sv_create_demo_path(
                    mod, client_num,
                    demo_utils::gen_utilities::get_shortened_mapname_lowercase() + "_sv")
                : demo_utils::file_directory_server::sv_create_demo_path(mod, client_num, file_name);

            std::error_code ec;
            if (!overwrite && std::filesystem::exists(path, ec))
            {
                return false;
            }

            auto& cs = s_clients[client_num];
            cs.file.open(path, std::ios::binary | std::ios::trunc);
            if (!cs.file.is_open()) return false;
            cs.path = path;
            return true;
        }

        void write_sv_header(std::size_t client_num)
        {
            auto& cs = s_clients[client_num];
            demo_utils::buffer_t hdr;
            demo_utils::serialization_server::sv_write_general_header(
                hdr, std::string(demo_utils::DEMO_CODE_VERSION));
            demo_utils::serialization::write_mod_header(
                hdr, demo_utils::gen_utilities::get_mod_directory());
            demo_utils::serialization::write_map_header(
                hdr,
                demo_utils::gen_utilities::get_mapname(),
                demo_utils::gen_utilities::get_gametype());
            cs.file.write(reinterpret_cast<const char*>(hdr.get()),
                          static_cast<std::streamsize>(hdr.size()));
        }

        void write_sv_footer(std::size_t client_num)
        {
            auto& cs = s_clients[client_num];
            demo_utils::buffer_t ftr;
            demo_utils::serialization::write_general_footer(ftr);
            demo_utils::serialization::write_end_of_file(ftr);
            cs.file.write(reinterpret_cast<const char*>(ftr.get()),
                          static_cast<std::streamsize>(ftr.size()));
        }

        // -----------------------------------------------------------------------
        // Commands
        // -----------------------------------------------------------------------

        // IW6 name: sv_demorecord command (server-side)
        void cmd_sv_demorecord(const command::params& params)
        {
            if (params.size() < 2)
            {
                console::info("Usage: sv_demorecord <client_num> [file_name]\n");
                return;
            }

            const std::size_t client_num = static_cast<std::size_t>(std::atoi(params[1]));
            const std::string file_name  = params.size() >= 3 ? params[2] : "";

            if (demo_sv_recording::sv_recording(client_num))
            {
                sv_stoprecord(client_num);
            }
            else
            {
                sv_startrecord(client_num, file_name, false);
            }
        }

        void cmd_sv_demostop(const command::params& params)
        {
            if (params.size() < 2)
            {
                // Stop all
                for (std::size_t i = 0; i < MAX_CLIENTS; ++i)
                {
                    sv_stoprecord(i);
                }
                return;
            }
            const std::size_t client_num = static_cast<std::size_t>(std::atoi(params[1]));
            sv_stoprecord(client_num);
        }

    } // anonymous namespace

    // =========================================================================
    // Public API
    // =========================================================================

    bool sv_recording(std::size_t client_num)
    {
        if (client_num >= MAX_CLIENTS) return false;
        return s_clients[client_num].recording;
    }

    bool sv_startrecord(std::size_t client_num, std::string_view file_name, bool overwrite)
    {
        if (client_num >= MAX_CLIENTS) return false;

        if (s_clients[client_num].recording)
        {
            console::warn("Already recording demo for client %zu\n", client_num);
            return false;
        }

        if (!game::environment::is_dedi() && !game::environment::is_mp())
        {
            console::warn("Server-side recording requires MP/dedi mode.\n");
            return false;
        }

        if (!open_file_for_client(client_num, std::string(file_name), overwrite))
        {
            return false;
        }

        s_clients[client_num].recording = true;
        write_sv_header(client_num);

        console::info("SV recording client %zu -> %s\n",
            client_num, s_clients[client_num].path.string().c_str());
        return true;
    }

    bool sv_stoprecord(std::size_t client_num)
    {
        if (client_num >= MAX_CLIENTS) return false;
        if (!s_clients[client_num].recording) return false;

        write_sv_footer(client_num);
        const auto saved = s_clients[client_num].path.string();
        reset_client(client_num);

        console::info("SV demo saved: %s\n", saved.c_str());
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
            // IW6: SV_WriteSnapshotToClient hook
            // PC exe addr: TODO  |  PS4 ELF ref: 0x50c7e0
            // sv_write_snapshot_hook.create(0x0_b, sv_write_snapshot_stub);

            // IW6: SV_BuildClientSnapshot hook
            // PC exe addr: TODO  |  PS4 ELF ref: 0x508ce0
            // sv_build_client_snapshot_hook.create(0x0_b, sv_build_client_snapshot_stub);

            command::add_sv("sv_demorecord", [](int, const command::params_sv& p)
            {
                if (p.size() < 2) return;
                const std::size_t cn = static_cast<std::size_t>(std::atoi(p[1]));
                const std::string fn = p.size() >= 3 ? p[2] : "";
                if (sv_recording(cn)) sv_stoprecord(cn); else sv_startrecord(cn, fn, false);
            });

            command::add_sv("sv_demostop", [](int, const command::params_sv& p)
            {
                if (p.size() >= 2)
                {
                    sv_stoprecord(static_cast<std::size_t>(std::atoi(p[1])));
                }
                else
                {
                    for (std::size_t i = 0; i < MAX_CLIENTS; ++i) sv_stoprecord(i);
                }
            });
        }
    };
}

REGISTER_COMPONENT(demo_sv_recording::component)
