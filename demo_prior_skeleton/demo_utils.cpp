#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "demo_utils.hpp"

#include "../command.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/string.hpp>

namespace demo_utils
{
    // ---------------------------------------------------------------------------
    // gen_utilities
    // ---------------------------------------------------------------------------
    namespace gen_utilities
    {
        std::string get_dvar_string(const char* dvar_name)
        {
            const auto* dvar = game::Dvar_FindVar(dvar_name);
            if (!dvar)
            {
                return {};
            }
            return dvar->current.string ? dvar->current.string : "";
        }

        std::string get_mod_directory()
        {
            return get_dvar_string("fs_game");
        }

        std::string get_base_path()
        {
            return get_dvar_string("fs_basepath");
        }

        std::string get_mapname()
        {
            return get_dvar_string("mapname");
        }

        std::string get_shortened_mapname_lowercase()
        {
            auto name = get_mapname();
            // Strip trailing _load if present
            constexpr std::string_view suffix = "_load";
            if (name.size() > suffix.size() &&
                name.substr(name.size() - suffix.size()) == suffix)
            {
                name.erase(name.size() - suffix.size());
            }
            for (auto& c : name)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return name;
        }

        std::string get_gametype()
        {
            return get_dvar_string("g_gametype");
        }

        std::string get_server_address()
        {
            return get_dvar_string("sv_hostname");
        }
    }

    // ---------------------------------------------------------------------------
    // file_directory
    // ---------------------------------------------------------------------------
    namespace file_directory
    {
        static std::filesystem::path s_base_demo_path;

        std::filesystem::path get_base_demo_path()
        {
            if (s_base_demo_path.empty())
            {
                const auto base = gen_utilities::get_base_path();
                s_base_demo_path = std::filesystem::path(base.empty() ? "." : base) / "demos";
            }
            return s_base_demo_path;
        }

        bool create_directory_user_demo(const std::string& mod_dir)
        {
            std::error_code ec;
            const auto path = get_base_demo_path() / "client" / (mod_dir.empty() ? "base" : mod_dir) / "user";
            return std::filesystem::create_directories(path, ec);
        }

        std::filesystem::path create_path_user_demo(const std::string& mod_dir, const std::string& file_name)
        {
            create_directory_user_demo(mod_dir);
            const auto dir = get_base_demo_path() / "client" / (mod_dir.empty() ? "base" : mod_dir) / "user";
            return dir / (file_name + std::string(DEMO_EXTENSION));
        }

        std::filesystem::path create_path_auto_demo(const std::string& mod_dir)
        {
            std::error_code ec;
            const auto dir = get_base_demo_path() / "client" / (mod_dir.empty() ? "base" : mod_dir) / "auto";
            std::filesystem::create_directories(dir, ec);

            const auto now = std::chrono::system_clock::now();
            const auto t   = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            char ts[32];
            std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tm);

            const auto map = gen_utilities::get_shortened_mapname_lowercase();
            const std::string name = map.empty() ? ts : (map + "_" + ts);
            return dir / (name + std::string(DEMO_EXTENSION));
        }
    }

    // ---------------------------------------------------------------------------
    // file_directory_server
    // ---------------------------------------------------------------------------
    namespace file_directory_server
    {
        bool sv_create_demo_directory(const std::string& mod_dir, std::size_t client_num)
        {
            std::error_code ec;
            const auto path = file_directory::get_base_demo_path()
                / "server"
                / (mod_dir.empty() ? "base" : mod_dir)
                / std::to_string(client_num);
            return std::filesystem::create_directories(path, ec);
        }

        std::filesystem::path sv_create_demo_path(const std::string& mod_dir, std::size_t client_num,
                                                   const std::string& file_name)
        {
            sv_create_demo_directory(mod_dir, client_num);
            const auto dir = file_directory::get_base_demo_path()
                / "server"
                / (mod_dir.empty() ? "base" : mod_dir)
                / std::to_string(client_num);
            return dir / (file_name + std::string(DEMO_EXTENSION));
        }
    }

    // ---------------------------------------------------------------------------
    // serialization
    // ---------------------------------------------------------------------------
    namespace serialization
    {
        void write_id_and_size(buffer_t& buf, demo_data_id id, std::uint32_t size)
        {
            buf.write(static_cast<std::uint8_t>(id));
            buf.write(size);
        }

        void write_network_data(buffer_t& buf, const std::uint8_t* data, std::uint32_t size)
        {
            if (!data || !size) return;
            write_id_and_size(buf, demo_data_id::network_data, size);
            buf.write(data, size);
        }

        void write_mod_header(buffer_t& buf, const std::string& mod_dir)
        {
            const auto str = mod_dir.empty() ? std::string("base") : mod_dir;
            const auto sz  = static_cast<std::uint32_t>(str.size() + 1);
            write_id_and_size(buf, demo_data_id::mod_header, sz);
            buf.write(str.data(), sz);
        }

        void write_map_header(buffer_t& buf, const std::string& mapname, const std::string& gametype)
        {
            const auto ms = static_cast<std::uint32_t>(mapname.size() + 1);
            const auto gs = static_cast<std::uint32_t>(gametype.size() + 1);
            write_id_and_size(buf, demo_data_id::map_header, ms + gs);
            buf.write(mapname.data(), ms);
            buf.write(gametype.data(), gs);
        }

        void write_gamestate_data(buffer_t& buf, const gamestate_t& gs)
        {
            const auto sz = static_cast<std::uint32_t>(sizeof(std::int32_t) + sizeof(gs.raw_gamestate));
            write_id_and_size(buf, demo_data_id::update_gamestate_data, sz);
            buf.write(gs.svr_cmd_seq);
            buf.write(gs.raw_gamestate, sizeof(gs.raw_gamestate));
        }

        void write_predicted_data(buffer_t& buf, const predicted_data_t& pd, predicted_data_id type)
        {
            const auto sz = static_cast<std::uint32_t>(sizeof(pd));
            write_id_and_size(buf, demo_data_id::predicted_data, sz + 1);
            buf.write(static_cast<std::uint8_t>(type));
            buf.write(pd.viewangles, sizeof(pd.viewangles));
            buf.write(pd.raw_client_archive, sizeof(pd.raw_client_archive));
        }

        void write_general_header(buffer_t& buf, const std::string& version)
        {
            const auto sz = static_cast<std::uint32_t>(version.size() + 1);
            write_id_and_size(buf, demo_data_id::gen_header, sz);
            buf.write(version.data(), sz);
        }

        void write_general_footer(buffer_t& buf)
        {
            write_id_and_size(buf, demo_data_id::gen_footer, 0);
        }

        void write_end_of_file(buffer_t& buf)
        {
            buf.write(static_cast<std::uint8_t>(demo_data_id::eof));
        }
    }

    // ---------------------------------------------------------------------------
    // serialization_server
    // ---------------------------------------------------------------------------
    namespace serialization_server
    {
        void sv_write_predicted_data(buffer_t& buf, const predicted_data_t& pd, predicted_data_id type)
        {
            serialization::write_predicted_data(buf, pd, type);
        }

        void sv_write_general_header(buffer_t& buf, const std::string& version)
        {
            serialization::write_general_header(buf, version);
        }
    }

    // ---------------------------------------------------------------------------
    // deserialization
    // ---------------------------------------------------------------------------
    namespace deserialization
    {
        bool is_gamestate_valid(const gamestate_t& gs)
        {
            return gs.svr_cmd_seq != 0;
        }

        bool parse_demo(std::filesystem::path& path,
                        std::vector<predicted_data_t>& pred_data_out,
                        gamestate_t& gs_out,
                        std::string& map_out,
                        std::string& mod_out)
        {
            // TODO: implement full parse loop reading chunk id/size/data
            std::string raw;
            if (!utils::io::read_file(path.string(), &raw))
            {
                return false;
            }
            (void)pred_data_out;
            (void)gs_out;
            (void)map_out;
            (void)mod_out;
            return false;
        }
    }

    // ---------------------------------------------------------------------------
    // misc
    // ---------------------------------------------------------------------------
    namespace misc
    {
        bool get_persistent_data(int /*local_client_num*/, persistent_data_t& /*out*/)
        {
            // TODO: read killstreak state from H1 client state
            return false;
        }

        bool fast_forward_demo(std::size_t frames)
        {
            // TODO: integrate with demo_playback tick loop
            (void)frames;
            return false;
        }

        void check_address_assertions()
        {
            // TODO: assert that required H1 function addresses are non-zero at startup
        }
    }
}
