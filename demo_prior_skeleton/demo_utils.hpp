#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <filesystem>

#include "game/game.hpp"

// MWR/H1 demo system – shared types, constants, and utilities.
// Architecture mirrors IW6: https://git.alterware.dev/anomaly/iw6-mod
// All IW6 type/enum names are preserved with "// IW6 name: <X>" annotations.

namespace demo_utils
{
    // IW6 name: DEMO_CODE_VERSION
    constexpr std::string_view DEMO_CODE_VERSION = "v0.0.1";
    // IW6 name: DEMO_EXTENSION
    constexpr std::string_view DEMO_EXTENSION = ".dm_mwr";
    // IW6 name: MAX_SIZE
    constexpr std::size_t DEMO_MAX_SIZE = 131072;

    // IW6 name: demo_data_id
    enum class demo_data_id : std::uint8_t
    {
        mod_header             = 0,
        map_header             = 1,
        network_data           = 2,
        predicted_data         = 3,
        update_gamestate_data  = 4,
        gen_header             = 5,
        gen_footer             = 6,
        eof                    = 31,
        // flags
        unused_flag_1          = 0x20,
        unused_flag_2          = 0x40,
        one_byte_flag          = 0x80,
    };

    // IW6 name: predicted_data_id
    enum class predicted_data_id : std::uint8_t
    {
        player                         = 0,
        vehicle                        = 1,
        vehicle_cam                    = 2,
        remote_eye                     = 3,
        vehicle_helo_pilot             = 4,
        vehicle_helo_pilot_turret_fire = 5,
    };

    // IW6 name: persistent_data_t
    struct persistent_data_t
    {
        std::int32_t cur_kill_count{};
        std::int32_t req_kill_count_next_ksr{};
        std::int32_t unknown1{1};
        std::int32_t active_ksr_count{};
        std::int32_t unknown2{-1};
        std::int32_t active_ksr_flags{};
        std::int32_t unknown3{};
        std::int32_t first_ksr_icon{};
        std::int32_t second_ksr_icon{};
        std::int32_t third_ksr_icon{};
    };

    // IW6 name: packed_gamestate_sizes
    // NOTE: H1 gamestate structure may differ; validate offsets before use.
    struct packed_gamestate_sizes
    {
        std::uint64_t num_clients   : 4;  // 0
        std::uint64_t msg_data_size : 60; // rest
    };
    static_assert(sizeof(packed_gamestate_sizes) == 8);

    // IW6 name: predicted_data_t
    // Mirrors IW6 ClientArchiveData — H1 equivalent struct name TBD.
    // TODO: verify ClientArchiveData layout in H1 structs.hpp
    struct predicted_data_t
    {
        float viewangles[3]{};
        std::uint8_t raw_client_archive[512]{}; // TODO: replace with game::ClientArchiveData when confirmed
    };

    // IW6 name: gamestate_t
    struct gamestate_t
    {
        std::int32_t svr_cmd_seq{};
        std::uint8_t raw_gamestate[131072]{}; // TODO: replace with game::gameState_t when confirmed
    };

    // IW6 name: buffer_t
    class buffer_t
    {
    public:
        [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
        [[nodiscard]] const std::uint8_t* get() const noexcept { return data_.data(); }
        [[nodiscard]] std::uint8_t* get() noexcept { return data_.data(); }

        void reserve_memory(std::size_t n) { data_.reserve(n); }
        void clear() noexcept { data_.clear(); }

        template <typename T>
        void write(const T& value)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(&value);
            data_.insert(data_.end(), p, p + sizeof(T));
        }

        void write(const void* src, std::size_t len)
        {
            const auto* p = static_cast<const std::uint8_t*>(src);
            data_.insert(data_.end(), p, p + len);
        }

    private:
        std::vector<std::uint8_t> data_;
    };

    // IW6 name: gen_utilities namespace
    namespace gen_utilities
    {
        std::string get_dvar_string(const char* dvar_name);
        std::string get_mod_directory();
        std::string get_base_path();
        std::string get_mapname();
        std::string get_shortened_mapname_lowercase();
        std::string get_gametype();
        std::string get_server_address();
    }

    // IW6 name: file_directory namespace (client-side paths)
    namespace file_directory
    {
        bool create_directory_user_demo(const std::string& mod_dir);
        std::filesystem::path create_path_user_demo(const std::string& mod_dir, const std::string& file_name);
        std::filesystem::path create_path_auto_demo(const std::string& mod_dir);
        std::filesystem::path get_base_demo_path();
    }

    // IW6 name: file_directory_server namespace (server-side paths)
    namespace file_directory_server
    {
        bool sv_create_demo_directory(const std::string& mod_dir, std::size_t client_num);
        std::filesystem::path sv_create_demo_path(const std::string& mod_dir, std::size_t client_num,
                                                   const std::string& file_name);
    }

    // IW6 name: serialization namespace
    namespace serialization
    {
        void write_id_and_size(buffer_t& buf, demo_data_id id, std::uint32_t size);
        void write_network_data(buffer_t& buf, const std::uint8_t* data, std::uint32_t size);
        void write_mod_header(buffer_t& buf, const std::string& mod_dir);
        void write_map_header(buffer_t& buf, const std::string& mapname, const std::string& gametype);
        void write_gamestate_data(buffer_t& buf, const gamestate_t& gs);
        void write_predicted_data(buffer_t& buf, const predicted_data_t& pd, predicted_data_id type);
        void write_general_header(buffer_t& buf, const std::string& version);
        void write_general_footer(buffer_t& buf);
        void write_end_of_file(buffer_t& buf);
    }

    // IW6 name: serialization_server namespace
    namespace serialization_server
    {
        void sv_write_predicted_data(buffer_t& buf, const predicted_data_t& pd, predicted_data_id type);
        void sv_write_general_header(buffer_t& buf, const std::string& version);
    }

    // IW6 name: deserialization namespace
    namespace deserialization
    {
        bool is_gamestate_valid(const gamestate_t& gs);
        bool parse_demo(std::filesystem::path& path,
                        std::vector<predicted_data_t>& pred_data_out,
                        gamestate_t& gs_out,
                        std::string& map_out,
                        std::string& mod_out);
    }

    // IW6 name: misc namespace
    namespace misc
    {
        template <typename To, typename From>
        To cpy_cast(const From& src)
        {
            static_assert(sizeof(To) <= sizeof(From));
            To result;
            std::memcpy(&result, &src, sizeof(To));
            return result;
        }

        bool get_persistent_data(int local_client_num, persistent_data_t& out);
        bool fast_forward_demo(std::size_t frames);
        void check_address_assertions();
    }
}
