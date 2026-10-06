#pragma once

#include "demo_game.hpp"

namespace demo_utils
{
	// true only while process_network_data is inside CL_ParseServerMessage feeding the demo's
	// own recorded stream. Lets the CL_ParseServerMessage hook tell "our replayed message" apart
	// from a live/loopback message (which must be dropped during an armed replay).
	bool is_demo_feeding();

	namespace globals
	{
		inline constexpr std::string_view DEMO_CODE_VERSION = "v0.0.1";
		inline constexpr std::string_view DEMO_EXTENSION = ".dm_h1";
		inline constexpr std::size_t MAX_SIZE = 131'072;
	}
	using namespace globals;

	namespace types
	{
		enum struct demo_data_id : std::uint8_t
		{
			mod_header = 0,
			map_header = 1,
			network_data = 2,
			predicted_data = 3,
			update_gamestate_data = 4,
			gen_header = 5,
            gen_footer = 6,
            facial_data = 7,

			eof = 31,

			unused_flag_1 = 0b0010'0000,
			unused_flag_2 = 0b0100'0000,
			one_byte_flag = 0b1000'0000,

			flags = (unused_flag_1 | unused_flag_2 | one_byte_flag)
		};

		demo_data_id operator|(demo_data_id lhs, demo_data_id rhs);
		demo_data_id operator&(demo_data_id lhs, demo_data_id rhs);
		demo_data_id operator~(demo_data_id value);

		enum struct predicted_data_id : std::uint8_t
		{
			player = 0,
			vehicle_gryphon = 1,
			vehicle_helo_pilot = 2,
			vehicle_odin = 3,
			vehicle_unknown = 4,
			vehicle_helo_pilot_turret_fire = 5
		};

		struct persistent_data_t
		{
			// ksr -> killstreak reward
			std::int32_t cur_kill_count{};
			std::int32_t req_kill_count_next_ksr{};
			std::int32_t unknown1{ 1 };
			std::int32_t active_ksr_count{};
			std::int32_t unknown2{ -1 };
			std::int32_t active_ksr_flags{};
			std::int32_t unknown3{};
			std::int32_t first_ksr_icon{};
			std::int32_t second_ksr_icon{};
			std::int32_t third_ksr_icon{};
		};

		struct packed_gamestate_sizes
		{
			std::uint64_t string_data : 20{};
			std::uint64_t string_offsets : 16{};
			std::uint64_t unused : 27{};
			std::uint64_t compressed : 1{};
		};
		static_assert(sizeof(packed_gamestate_sizes) == 8);

		struct predicted_data_t
		{
			game::vec3_t viewangles{}; // h1: cg->predictedPlayerState.delta_angles (verified source)
			game::ClientArchiveData cad{};
		};

		struct gamestate_t
		{
			std::uint32_t svr_cmd_seq{};
			// the config-string append cursor (bytes used in data.stringData). Lives here, not on
			// game::gameState_t, because the REAL live cursor is a separate, non-contiguous global
			// (see game::mp::cs_data_count) -- game::gameState_t only mirrors the truly-contiguous
			// stringOffsets/stringData live memory region.
			std::int32_t dataCount{};
			game::gameState_t data{};
		};

		class buffer_t
		{
		public:
			[[nodiscard]] std::size_t size() const;
			[[nodiscard]] std::span<const std::uint8_t> get() const;
			void reserve_memory(std::size_t size);
			void write(const char* src, std::size_t size);
			void clear();

		private:
			std::vector<std::uint8_t> buffer_;
		};
	}
	using namespace types;

	namespace gen_utilities
	{
		std::string_view get_dvar_string(std::string_view dvar_name, bool valid_string);
		std::string_view get_mod_directory();
		std::string_view get_base_path(bool default_string);
		std::string_view get_mapname(bool default_string);
		std::string get_shortened_mapname_lowercase(bool default_string);
		std::string get_mapname_lowercase(bool default_string);
		std::string_view get_gametype(bool default_string);
		std::string get_gametype_lowercase(bool default_string);
		//std::string get_datetime();
	}
	using namespace gen_utilities;

	namespace file_directory
	{
		std::optional<std::filesystem::path> create_directory_auto_demo();
		bool can_create_demo_directories();
		// mapname + incrementing number (e.g. "farm.0000.dm_h1", "farm.0001.dm_h1", ...) -- the
		// only naming scheme now, since manual/user recording was removed (auto-record only).
		std::optional<std::filesystem::path> create_path_auto_demo(const std::filesystem::path& dir_path);
	}
	using namespace file_directory;

	namespace file_directory_server
	{
		std::optional<std::filesystem::path> sv_create_demo_directory();
		bool sv_can_create_demo_directory();
		std::optional<std::filesystem::path> sv_create_path_server_demo(
			const std::filesystem::path& dir_path, std::string_view client_id);
		std::optional<std::filesystem::path> sv_create_path_server_demo(
			const std::filesystem::path& dir_path, std::string_view demo_name, bool overwrite);
	}
	using namespace file_directory_server;

	namespace serialization
	{
		void write_id_and_size(buffer_t& output, std::size_t size, demo_data_id id);
		void write_id_and_size(std::ofstream& output, std::size_t size, demo_data_id id);

		void write_network_data(buffer_t& output, std::span<const std::uint8_t> network_data);
		void write_network_data(std::ofstream& output, std::span<const std::uint8_t> network_data);

		//void write_predicted_player_data(buffer_t& output, const game::ClientArchiveData& cad,
			//const game::vec3_t& viewangles, std::uint8_t cad_index, predicted_data_id id);
		void write_predicted_player_data(std::ofstream& output, const game::ClientArchiveData& cad,
			const game::vec3_t& viewangles, std::uint8_t cad_index, predicted_data_id id);

		//void write_predicted_vehicle_data(buffer_t& output, const game::ClientArchiveData& cad,
			//const game::vec3_t& viewangles, std::uint8_t cad_index, predicted_data_id id);
		//void write_predicted_vehicle_data(std::ofstream& output, const game::ClientArchiveData& cad,
			//const game::vec3_t& viewangles, std::uint8_t cad_index, predicted_data_id id);

		void write_helo_pilot_turret_fire(buffer_t& output, std::uint8_t fire_count);
		void write_helo_pilot_turret_fire(std::ofstream& output, std::uint8_t fire_count);

		void write_mod_header(buffer_t& output);
		void write_mod_header(std::ofstream& output);

		bool write_map_header(buffer_t& output);
		bool write_map_header(std::ofstream& output);

		bool write_gamestate_data(buffer_t& output);
		bool write_gamestate_data(std::ofstream& output);

		void write_predicted_data(buffer_t& output);
		void write_predicted_data(std::ofstream& output);

		void write_general_header(buffer_t& output);
		void write_general_header(std::ofstream& output);

		//void write_general_footer(buffer_t& output, std::int32_t first_svr_time, std::int32_t last_svr_time);
		void write_general_footer(std::ofstream& output, std::int32_t first_svr_time, std::int32_t last_svr_time,
			std::string_view timeline = {});

		//void write_end_of_file(buffer_t& output);
		void write_end_of_file(std::ofstream& output);
	}
	using namespace serialization;

	namespace serialization_server
	{
		void sv_write_predicted_data(buffer_t& output, const game::mp::playerState_t& ps, std::size_t send_msg_count);
		void sv_write_predicted_data(std::ofstream& output, const game::mp::playerState_t& ps, std::size_t send_msg_count);

		void sv_write_general_header(buffer_t& output, std::string_view player_name);
		void sv_write_general_header(std::ofstream& output, std::string_view player_name);
	}
	using namespace serialization_server;

	namespace deserialization
	{
		bool is_gamestate_valid(game::gameState_t& gs, bool sanitize);
		bool parse_demo(std::ifstream& file, std::vector<std::uint8_t>& buffer,
			std::size_t& post_map_header_file_offset, std::array<predicted_data_t, 256>& pred_data,
			std::size_t& pred_data_index, std::optional<gamestate_t>& gs, bool skip_map_preload, bool& clean_end);

		// [clocklog] cumulative count of chunks consumed by parse_demo's gated loop. See its definition.
		extern int g_demo_chunks_read;

		// Remember the recorded gamestate's server-command baseline (svr_cmd_seq) so playback can
		// keep the REAL cgs command counter seeded at it (see process_server_commands). 0 disables.
		void note_command_baseline(int svr_cmd_seq);
	}
	using namespace deserialization;

	namespace misc
	{
		template <typename To, typename From>
			requires (std::is_trivially_copyable_v<From>&& std::is_trivially_copyable_v<To>)
		[[nodiscard]] To cpy_cast(const From* src)
		{
			To dst;
			std::memcpy(&dst, src, sizeof(To));
			return dst;
		}

		persistent_data_t& get_persistent_data();
		void fast_forward_demo(std::uint32_t msec);
		void check_address_assertions();
	}
	using namespace misc;
}
