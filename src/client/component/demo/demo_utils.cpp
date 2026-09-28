#include "std_include.hpp"

#include "demo_utils.hpp"

#include "component/console.hpp"
#include "component/party.hpp"
#include "utils/compression.hpp"
#include "utils/string.hpp"

namespace demo_utils
{
	namespace { bool g_demo_feeding = false; }
	bool is_demo_feeding() { return g_demo_feeding; }

	namespace types
	{
		std::size_t buffer_t::size() const
		{
			return buffer_.size();
		}

		std::span<const std::uint8_t> buffer_t::get() const
		{
			return std::span<const std::uint8_t>(buffer_);
		}

		void buffer_t::reserve_memory(std::size_t size)
		{
			buffer_.reserve(size);
		}

		void buffer_t::write(const char* src, std::size_t size)
		{
			buffer_.insert(buffer_.end(), src, src + size);
		}

		void buffer_t::clear()
		{
			buffer_.clear();
		}

		demo_data_id operator|(demo_data_id lhs, demo_data_id rhs)
		{
			using type = std::underlying_type_t<demo_data_id>;
			return demo_data_id(static_cast<type>(lhs) | static_cast<type>(rhs));
		}

		demo_data_id operator&(demo_data_id lhs, demo_data_id rhs)
		{
			using type = std::underlying_type_t<demo_data_id>;
			return demo_data_id(static_cast<type>(lhs) & static_cast<type>(rhs));
		}

		demo_data_id operator~(demo_data_id value)
		{
			using type = std::underlying_type_t<demo_data_id>;
			return demo_data_id(~static_cast<type>(value));
		}
	}

	namespace gen_utilities
	{
		std::string_view get_dvar_string(std::string_view dvar_name, bool valid_string)
		{
			const auto* dvar = game::Dvar_FindVar(dvar_name.data());

			if (!dvar || !dvar->current.string || dvar->current.string[0] == '\0')
			{
				return (valid_string)
					? std::string_view("")
					: std::string_view();
			}

			return dvar->current.string;
		}

		std::string_view get_mod_directory()
		{
			return get_dvar_string("fs_game", true);
		}

		std::string_view get_base_path(bool default_string)
		{
			const auto string = get_dvar_string("fs_basepath", true);
			if (string.empty() && default_string)
			{
				return "unknown_basepath";
			}

			return string;
		}

		std::string_view get_mapname(bool default_string)
		{
			const auto string = get_dvar_string("mapname", true);
			if (string.empty() && default_string)
			{
				return "unknown_mapname";
			}

			return string;
		}

		std::string get_shortened_mapname_lowercase(bool default_string)
		{
			const auto mapname = get_mapname(default_string);
			if (mapname.starts_with("mp_"))
			{
				return utils::string::to_lower(std::string(mapname.begin() + 3, mapname.end()));
			}

			return utils::string::to_lower(std::string(mapname));
		}

		std::string get_mapname_lowercase(bool default_string)
		{
			return utils::string::to_lower(std::string(get_mapname(default_string)));
		}

		std::string_view get_gametype(bool default_string)
		{
			const auto string = get_dvar_string("g_gametype", true);
			if (string.empty() && default_string)
			{
				return "unknown_gametype";
			}

			return string;
		}

		std::string get_gametype_lowercase(bool default_string)
		{
			return utils::string::to_lower(std::string(get_gametype(default_string)));
		}

		std::string_view get_ui_mapname()
		{
			static constexpr std::string_view unknown_mapname = "unknown_ui_mapname";

			const auto dvar_mapname = get_mapname(false);
			if (dvar_mapname.empty())
			{
				return unknown_mapname;
			}

			const auto* ui_mapname = game::UI_LocalizeMapname(dvar_mapname.data());
			if (!ui_mapname)
			{
				return unknown_mapname;
			}

			const auto mapname = std::string_view(ui_mapname);
			if (mapname.empty() || mapname.size() >= 64)
			{
				return unknown_mapname;
			}

			return mapname;
		}

		std::string_view get_ui_gametype()
		{
			static constexpr std::string_view unknown_gametype = "unknown_ui_gametype";

			const auto dvar_gametype = get_gametype(false);
			if (dvar_gametype.empty())
			{
				return unknown_gametype;
			}

			const auto* ui_gametype = game::UI_LocalizeGametype(dvar_gametype.data());
			if (!ui_gametype)
			{
				return unknown_gametype;
			}

			const auto gametype = std::string_view(ui_gametype);
			if (gametype.empty() || gametype.size() >= 64)
			{
				return unknown_gametype;
			}

			return gametype;
		}

		std::string get_datetime()
		{
			const auto now = std::chrono::system_clock::now();
			const auto time_zone = std::chrono::current_zone();
			const auto local_time = time_zone->to_local(now);
			const auto time_point = std::chrono::time_point_cast<std::chrono::days>(local_time);
			const auto yy_mm_dd = std::chrono::year_month_day(time_point);
			const auto hh_mm_ss = std::chrono::hh_mm_ss<std::chrono::seconds>
				(std::chrono::duration_cast<std::chrono::seconds>(local_time.time_since_epoch()) % std::chrono::days(1));

			return std::format("[{:04}.{:02}.{:02}][{:02}.{:02}.{:02}]",
				yy_mm_dd.year().operator int(),
				yy_mm_dd.month().operator unsigned int(),
				yy_mm_dd.day().operator unsigned int(),
				hh_mm_ss.hours().count(),
				hh_mm_ss.minutes().count(),
				hh_mm_ss.seconds().count()
			);
		}
	}

	namespace file_directory
	{
		std::optional<std::filesystem::path> create_directory_auto_demo()
		{
			const auto fs_basepath = get_base_path(false);
			if (fs_basepath.empty())
			{
				console::error("could not find fs_basepath\n");
				return std::nullopt;
			}

			const std::optional<std::filesystem::path> opt(
				std::filesystem::path(fs_basepath) / std::format("demos/client/{}/auto", get_mod_directory())
			);

			const auto& path = *opt;
			if (!std::filesystem::exists(path) && !std::filesystem::create_directories(path))
			{
				console::error("could not create auto demo directory %s\n", path.string().c_str());
				return std::nullopt;
			}

			return opt;
		}

		bool can_create_demo_directories()
		{
			return create_directory_auto_demo().has_value();
		}

		// mapname + incrementing number (e.g. "farm.0000.dm_h1", "farm.0001.dm_h1", ...) -- the
		// naming scheme the old manual/user recording path used, now the ONLY scheme since
		// manual recording was removed (auto-record only).
		std::optional<std::filesystem::path> create_path_auto_demo(const std::filesystem::path& dir_path)
		{
			const auto mapname = get_shortened_mapname_lowercase(true);

			for (std::size_t index = 0; index < 10'000; ++index)
			{
				if (index < 5000 && index % 100 == 0)
				{
					// look ahead
					const auto demo_path = dir_path / std::format("{}.{:04}{}", mapname, index + 100, DEMO_EXTENSION);

					if (std::filesystem::exists(demo_path))
					{
						index += 99;
						continue;
					}
				}

				const std::optional<std::filesystem::path> demo_path =
					dir_path / std::format("{}.{:04}{}", mapname, index, DEMO_EXTENSION);

				if (std::filesystem::exists(*demo_path))
				{
					continue;
				}

				return demo_path;
			}

			return std::nullopt;
		}
	}

	namespace file_directory_server
	{
		std::optional<std::filesystem::path> sv_create_demo_directory()
		{
			const auto fs_basepath = get_base_path(false);
			if (fs_basepath.empty())
			{
				console::error("could not find fs_basepath\n");
				return std::nullopt;
			}

			const std::optional<std::filesystem::path> result(
				std::filesystem::path(fs_basepath) / std::format("demos/server/{}/", get_mod_directory())
			);

			const auto& svr_dir_path = *result;
			if (!std::filesystem::exists(svr_dir_path) && !std::filesystem::create_directories(svr_dir_path))
			{
				console::error("could not create demos directory %s\n", svr_dir_path.string().c_str());
				return std::nullopt;
			}

			return result;
		}

		bool sv_can_create_demo_directory()
		{
			return sv_create_demo_directory().has_value();
		}

		std::optional<std::filesystem::path> sv_create_path_server_demo(
			const std::filesystem::path& dir_path, std::string_view client_id)
		{
			const auto mapname = get_shortened_mapname_lowercase(true);

			const std::optional<std::filesystem::path> demo_path = (!client_id.empty())
				? dir_path / std::format("{}[{}][{}]{}", get_datetime(), mapname, client_id, DEMO_EXTENSION)
				: dir_path / std::format("{}[{}]{}", get_datetime(), mapname, DEMO_EXTENSION);

			if (std::filesystem::exists(*demo_path))
			{
				return std::nullopt;
			}

			return demo_path;
		}

		std::optional<std::filesystem::path> sv_create_path_server_demo(
			const std::filesystem::path& dir_path, std::string_view demo_name, bool overwrite)
		{
			const std::optional<std::filesystem::path> demo_path = dir_path / std::format("{}{}", demo_name, DEMO_EXTENSION);
			if (!overwrite && std::filesystem::exists(*demo_path))
			{
				return std::nullopt;
			}

			return demo_path;
		}
	}

	namespace serialization
	{
		// controllable vehicles - killstreak rewards
		namespace
		{
			const auto hash_gryphon = std::hash<std::string_view>()("remote_uav_mp");
			bool is_gryphon_ks_reward(std::string_view name)
			{
				return hash_gryphon == std::hash<std::string_view>()(name);
			}

			const auto hash_helo_pilot = std::hash<std::string_view>()("heli_pilot_mp");
			bool is_helo_pilot_ks_reward(std::string_view name)
			{
				return hash_helo_pilot == std::hash<std::string_view>()(name);
			}

			const auto hash_odin = std::hash<std::string_view>()("odin_mp");
			bool is_odin_ks_reward(std::string_view name)
			{
				return hash_odin == std::hash<std::string_view>()(name);
			}

			predicted_data_id get_predicted_vehicle_type(const game::mp::playerState_t& ps)
			{
				const auto** vehicle_def = reinterpret_cast<const char**>(game::CG_GetVehicleDef(&ps));
				const auto* vehicle_name = (vehicle_def) ? *vehicle_def : nullptr;

				if (vehicle_name)
				{
					if (is_gryphon_ks_reward(vehicle_name))
					{
						return predicted_data_id::vehicle_gryphon;
					}
					else if (is_helo_pilot_ks_reward(vehicle_name))
					{
						return predicted_data_id::vehicle_helo_pilot;
					}
					else if (is_odin_ks_reward(vehicle_name))
					{
						return predicted_data_id::vehicle_odin;
					}
				}

				assert(false);
				return predicted_data_id::vehicle_unknown;
			}
		}

		void write_id_and_size_(auto& output, std::size_t size, demo_data_id id)
		{
			assert(id <= demo_data_id::eof);

			if (size < 256)
			{
				const auto complete_id = id | demo_data_id::one_byte_flag;
				output.write(reinterpret_cast<const char*>(&complete_id), 1);
				output.write(reinterpret_cast<const char*>(&size), 1);
			}
			else
			{
				const auto complete_id = id;
				output.write(reinterpret_cast<const char*>(&complete_id), 1);
				output.write(reinterpret_cast<const char*>(&size), 4);
			}
		}

		void write_id_and_size(buffer_t& output, std::size_t size, demo_data_id id)
		{
			write_id_and_size_(output, size, id);
		}

		void write_id_and_size(std::ofstream& output, std::size_t size, demo_data_id id)
		{
			write_id_and_size_(output, size, id);
		}

		void write_network_data_(auto& output, std::span<const std::uint8_t> network_data)
		{
			write_id_and_size(output, network_data.size(), demo_data_id::network_data);
			output.write(reinterpret_cast<const char*>(network_data.data()), network_data.size());
		}

		void write_network_data(buffer_t& output, std::span<const std::uint8_t> network_data)
		{
			write_network_data_(output, network_data);
		}

		void write_network_data(std::ofstream& output, std::span<const std::uint8_t> network_data)
		{
			write_network_data_(output, network_data);
		}

		void write_predicted_player_data_(auto& output, const game::ClientArchiveData& cad,
			const game::vec3_t& viewangles, std::uint8_t cad_index, predicted_data_id id)
		{
			write_id_and_size(output, 50, demo_data_id::predicted_data);
			output.write(reinterpret_cast<const char*>(&id), 1);
			output.write(reinterpret_cast<const char*>(&cad_index), 1);

			output.write(reinterpret_cast<const char*>(&cad.serverTime), 4);
			output.write(reinterpret_cast<const char*>(&cad.origin[0]), 12);
			output.write(reinterpret_cast<const char*>(&cad.velocity[0]), 12);
			output.write(reinterpret_cast<const char*>(&cad.bobCycle), 4);
			output.write(reinterpret_cast<const char*>(&cad.movementDir), 4);
			output.write(reinterpret_cast<const char*>(&viewangles[0]), 12); // h1: cg->ps.delta_angles
		}

		void write_predicted_player_data(buffer_t& output, const game::ClientArchiveData& cad,
			const game::vec3_t& viewangles, std::uint8_t cad_index, predicted_data_id id)
		{
			write_predicted_player_data_(output, cad, viewangles, cad_index, id);
		}

		void write_predicted_player_data(std::ofstream& output, const game::ClientArchiveData& cad,
			const game::vec3_t& viewangles, std::uint8_t cad_index, predicted_data_id id)
		{
			write_predicted_player_data_(output, cad, viewangles, cad_index, id);
		}

		void write_predicted_vehicle_data_(auto& output, const game::ClientArchiveData& cad,
			const game::vec3_t& viewangles, std::uint8_t cad_index, predicted_data_id id)
		{
			const auto size = (id != predicted_data_id::vehicle_gryphon) ? 58 : 82;

			write_id_and_size(output, size, demo_data_id::predicted_data);
			output.write(reinterpret_cast<const char*>(&id), 1);
			output.write(reinterpret_cast<const char*>(&cad_index), 1);

			static const game::PlayerVehicleState vehicle{}; /* MWR: no vehicle in clientArchive */
			output.write(reinterpret_cast<const char*>(&cad.serverTime), 4);
			output.write(reinterpret_cast<const char*>(&cad.origin[0]), 12);
			output.write(reinterpret_cast<const char*>(&viewangles[0]), 12);
			output.write(reinterpret_cast<const char*>(&vehicle.flags), 4);
			output.write(reinterpret_cast<const char*>(&vehicle.origin[0]), 12);
			output.write(reinterpret_cast<const char*>(&vehicle.angles[0]), 12);

			if (id == predicted_data_id::vehicle_gryphon)
			{
				output.write(reinterpret_cast<const char*>(&vehicle.velocity[0]), 12);
				output.write(reinterpret_cast<const char*>(&vehicle.angVelocity[0]), 12);
			}

			assert(!vehicle.splineId);
		}

		void write_predicted_vehicle_data(buffer_t& output, const game::ClientArchiveData& cad,
			const game::vec3_t& viewangles, std::uint8_t cad_index, predicted_data_id id)
		{
			write_predicted_vehicle_data_(output, cad, viewangles, cad_index, id);
		}

		void write_predicted_vehicle_data(std::ofstream& output, const game::ClientArchiveData& cad,
			const game::vec3_t& viewangles, std::uint8_t cad_index, predicted_data_id id)
		{
			write_predicted_vehicle_data_(output, cad, viewangles, cad_index, id);
		}

		void write_helo_pilot_turret_fire_(auto& output, std::uint8_t fire_count)
		{
			static constexpr auto id = predicted_data_id::vehicle_helo_pilot_turret_fire;

			const auto snap_svr_time = game::mp::cl->snap.serverTime;

			write_id_and_size(output, 6, demo_data_id::predicted_data);
			output.write(reinterpret_cast<const char*>(&id), 1);
			output.write(reinterpret_cast<const char*>(&snap_svr_time), 4);
			output.write(reinterpret_cast<const char*>(&fire_count), 1);
		}

		void write_helo_pilot_turret_fire(buffer_t& output, std::uint8_t fire_count)
		{
			write_helo_pilot_turret_fire_(output, fire_count);
		}

		void write_helo_pilot_turret_fire(std::ofstream& output, std::uint8_t fire_count)
		{
			write_helo_pilot_turret_fire_(output, fire_count);
		}

		void write_mod_header_(auto& output)
		{
			const auto mod = get_mod_directory();
			if (mod.size())
			{
				write_id_and_size(output, mod.size() + 1, demo_data_id::mod_header);

				static constexpr auto null_terminator = '\0';
				output.write(mod.data(), mod.size());
				output.write(&null_terminator, 1);
			}
		}

		void write_mod_header(buffer_t& output)
		{
			write_mod_header_(output);
		}

		void write_mod_header(std::ofstream& output)
		{
			write_mod_header_(output);
		}

		bool write_map_header_(auto& output)
		{
			const auto mapname = get_mapname_lowercase(false);
			const auto gametype = get_gametype_lowercase(false);

			const auto size = mapname.size() + gametype.size() + 2;
			if (mapname.empty() || gametype.empty() || size < 4 || size > 127)
			{
				return false;
			}

			write_id_and_size(output, size, demo_data_id::map_header);

			static constexpr auto null_terminator = '\0';
			output.write(mapname.c_str(), mapname.size());
			output.write(&null_terminator, 1);
			output.write(gametype.c_str(), gametype.size());
			output.write(&null_terminator, 1);

			return true;
		}

		bool write_map_header(buffer_t& output)
		{
			return write_map_header_(output);
		}

		bool write_map_header(std::ofstream& output)
		{
			return write_map_header_(output);
		}

		bool write_gamestate_data_(auto& output)
		{
			const auto& persistent_data = get_persistent_data();
			const auto& gs = game::mp::cls->gameState;
			const auto svr_cmd_seq = game::mp::clc->serverCommandSequence;
			// the REAL cursor is a separate, non-contiguous global (see gameState_t comment /
			// cs_data_count_accessor) -- gs.dataCount doesn't exist as a member anymore.
			const auto strings_size = static_cast<std::size_t>(static_cast<int>(game::mp::cs_data_count));

			assert(is_gamestate_valid(game::mp::cls->gameState, false));
			if (!strings_size || strings_size >= sizeof(game::gameState_t::stringData))
			{
				return false;
			}

			const std::span<const std::uint8_t> sd(
				reinterpret_cast<const std::uint8_t*>(&gs.stringData[0]), strings_size);
			const std::span<const std::uint8_t> so(
				reinterpret_cast<const std::uint8_t*>(&gs.stringOffsets[0]), sizeof(game::gameState_t::stringOffsets));

			const auto string_data = utils::compression::zlib::compress(sd, 2048);
			const auto string_offsets = utils::compression::zlib::compress(so, 1024);

			assert(std::equal(sd.begin(), sd.end(),
				utils::compression::zlib::decompress(string_data, 4096).begin()));
			assert(std::equal(so.begin(), so.end(),
				utils::compression::zlib::decompress(string_offsets, sizeof(game::gameState_t::stringOffsets)).begin()));

			if (string_data.empty() || string_data.size() >= sizeof(game::gameState_t::stringData))
			{
				return false;
			}
			if (string_offsets.empty() || string_offsets.size() > sizeof(game::gameState_t::stringOffsets))
			{
				return false;
			}

			const packed_gamestate_sizes pgs{
				.string_data = string_data.size(),
				.string_offsets = string_offsets.size(),
				.compressed = 1
			};

			assert(pgs.string_data == string_data.size() && pgs.string_offsets == string_offsets.size());
			const auto size = 12 + string_data.size() + string_offsets.size() + sizeof(persistent_data_t);

			write_id_and_size(output, size, demo_data_id::update_gamestate_data);
			output.write(reinterpret_cast<const char*>(&svr_cmd_seq), 4);
			output.write(reinterpret_cast<const char*>(&pgs), 8);
			output.write(reinterpret_cast<const char*>(string_data.data()), string_data.size());
			output.write(reinterpret_cast<const char*>(string_offsets.data()), string_offsets.size());
			output.write(reinterpret_cast<const char*>(&persistent_data), sizeof(persistent_data_t));

			return true;
		}

		bool write_gamestate_data(buffer_t& output)
		{
			return write_gamestate_data_(output);
		}

		bool write_gamestate_data(std::ofstream& output)
		{
			return write_gamestate_data_(output);
		}

		void write_predicted_data_(auto& output)
		{
			// h1: capture the just-saved clientArchive entry (VERIFIED offsets: cl->clientArchive
			// @cl+0x6B80 stride 36, clientArchiveIndex @cl+0x8F80). This gives the local player's
			// predicted origin/velocity/bobCycle/movementDir -> position replay.
			// VIEW ANGLES: capture cg->predictedPlayerState.delta_angles (verified offset 0x12C =
			// cg+300; confirmed via CG_CalcViewValues/sub_13DF80 decompile: cg+300 is copied to
			// cg+912912 then AnglesToAxis'd into cg->refdef.view.axis every frame — this IS the
			// real first-person view-angle source h1 reads, unlike iw6's cg->refdefViewAngles
			// which doesn't apply here). MWR has no vehicles, so always the player path.
			const auto cad_index = static_cast<std::uint8_t>(game::mp::cl->clientArchiveIndex - 1);
			const auto cad_array = std::span(game::mp::cl->clientArchive);

			if (const auto& cad = cad_array[cad_index]; cad.serverTime)
			{
				write_predicted_player_data(output, cad, game::mp::cg->ps.delta_angles, cad_index, predicted_data_id::player);
			}
		}

		void write_predicted_data(buffer_t& output)
		{
			write_predicted_data_(output);
		}

		void write_predicted_data(std::ofstream& output)
		{
			write_predicted_data_(output);
		}

		void write_general_header_internal(auto& output,
			std::string_view demo_description, std::string_view svr_name_sv, std::string_view player_name_sv)
		{
			const auto datetime = get_datetime();

			const auto protocol = std::to_string(PROTOCOL);
			const auto* build_num_ptr = game::LiveStorage_FetchFFotD();
			const auto build_num = (build_num_ptr)
				? std::string_view(build_num_ptr)
				: "unknown_build_number";

			const auto svr_name = utils::string::strip(svr_name_sv, true);
			const auto player_name = utils::string::strip(player_name_sv, true);

			const auto ui_mapname = get_ui_mapname();
			const auto ui_gametype = get_ui_gametype();

			const auto size = datetime.size() + 1
				+ demo_description.size() + 1
				+ DEMO_CODE_VERSION.size() + 1
				+ protocol.size() + 1
				+ build_num.size() + 1
				+ svr_name.size() + 1
				+ player_name.size() + 1
				+ ui_mapname.size() + 1
				+ ui_gametype.size() + 1;

			write_id_and_size(output, size, demo_data_id::gen_header);

			static constexpr auto null_terminator = '\0';
			output.write(datetime.c_str(), datetime.size());
			output.write(&null_terminator, 1);
			output.write(demo_description.data(), demo_description.size());
			output.write(&null_terminator, 1);
			output.write(DEMO_CODE_VERSION.data(), DEMO_CODE_VERSION.size());
			output.write(&null_terminator, 1);
			output.write(protocol.c_str(), protocol.size());
			output.write(&null_terminator, 1);
			output.write(build_num.data(), build_num.size());
			output.write(&null_terminator, 1);
			output.write(svr_name.data(), svr_name.size());
			output.write(&null_terminator, 1);
			output.write(player_name.data(), player_name.size());
			output.write(&null_terminator, 1);
			output.write(ui_mapname.data(), ui_mapname.size());
			output.write(&null_terminator, 1);
			output.write(ui_gametype.data(), ui_gametype.size());
			output.write(&null_terminator, 1);
		}

		void write_general_header_(auto& output)
		{
			static constexpr std::string_view description = "Call of Duty: Modern Warfare Remastered - client demo";

			const auto* host_bptr = game::mp::cgs->szHostName;
			const auto* host_eptr = std::find(host_bptr, host_bptr + 128, '\0');
			const auto host_name = (std::distance(host_bptr, host_eptr) > 0 && std::distance(host_bptr, host_eptr) < 128)
				? std::string_view(host_bptr, host_eptr)
				: "unknown_server_host_name";

			std::array<char, 64> buffer{};
			std::string_view player_name;

			const auto success = game::CL_GetClientNameColorize(0, game::mp::cg->clientNum, buffer.data(), static_cast<int>(buffer.size()));
			if (success)
			{
				player_name = std::string_view(buffer.data());
			}

			if (player_name.empty())
			{
				player_name = get_dvar_string("name", true);
			}

			if (player_name.empty())
			{
				player_name = "unknown_player_name";
			}

			write_general_header_internal(output, description, host_name, player_name);
		}

		void write_general_header(buffer_t& output)
		{
			write_general_header_(output);
		}

		void write_general_header(std::ofstream& output)
		{
			write_general_header_(output);
		}

		void write_general_footer_(auto& output, std::int32_t first_svr_time, std::int32_t last_svr_time)
		{
			assert(first_svr_time > 0 && last_svr_time > 0 && last_svr_time > first_svr_time);

			const auto duration = (last_svr_time - first_svr_time) / 1000;
			const auto fmt_duration = std::format("[{:02}:{:02}:{:02}]", duration / 3600, (duration % 3600) / 60, duration % 60);
			const auto size = 2 * 4 + fmt_duration.size() + 1;

			write_id_and_size(output, size, demo_data_id::gen_footer);

			static constexpr auto null_terminator = '\0';
			output.write(reinterpret_cast<const char*>(&first_svr_time), 4);
			output.write(reinterpret_cast<const char*>(&last_svr_time), 4);
			output.write(fmt_duration.c_str(), fmt_duration.size());
			output.write(&null_terminator, 1);
		}

		void write_general_footer(buffer_t& output, std::int32_t first_svr_time, std::int32_t last_svr_time)
		{
			write_general_footer_(output, first_svr_time, last_svr_time);
		}

		void write_general_footer(std::ofstream& output, std::int32_t first_svr_time, std::int32_t last_svr_time)
		{
			write_general_footer_(output, first_svr_time, last_svr_time);
		}

		void write_end_of_file_(auto& output)
		{
			write_id_and_size(output, 0, demo_data_id::eof);
		}

		void write_end_of_file(buffer_t& output)
		{
			write_end_of_file_(output);
		}

		void write_end_of_file(std::ofstream& output)
		{
			write_end_of_file_(output);
		}
	}

	namespace serialization_server
	{
		void sv_write_predicted_player_data_(auto& output, const game::mp::playerState_t& ps,
			std::uint8_t cad_index, predicted_data_id id)
		{
			write_id_and_size(output, 50, demo_data_id::predicted_data);
			output.write(reinterpret_cast<const char*>(&id), 1);
			output.write(reinterpret_cast<const char*>(&cad_index), 1);

			output.write(reinterpret_cast<const char*>(&ps.commandTime), 4);
			output.write(reinterpret_cast<const char*>(&ps.origin[0]), 12);
			output.write(reinterpret_cast<const char*>(&ps.velocity[0]), 12);
			output.write(reinterpret_cast<const char*>(&ps.bobCycle), 4);
			output.write(reinterpret_cast<const char*>(&ps.movementDir), 4);
			output.write(reinterpret_cast<const char*>(&ps.viewangles[0]), 12);
		}

		void sv_write_predicted_player_data(buffer_t& output, const game::mp::playerState_t& ps,
			std::uint8_t cad_index, predicted_data_id id)
		{
			sv_write_predicted_player_data_(output, ps, cad_index, id);
		}

		void sv_write_predicted_player_data(std::ofstream& output, const game::mp::playerState_t& ps,
			std::uint8_t cad_index, predicted_data_id id)
		{
			sv_write_predicted_player_data_(output, ps, cad_index, id);
		}

		void sv_write_predicted_vehicle_data_(auto& output, const game::mp::playerState_t& ps,
			std::uint8_t cad_index, predicted_data_id id)
		{
			const auto size = (id != predicted_data_id::vehicle_gryphon) ? 58 : 82;

			write_id_and_size(output, size, demo_data_id::predicted_data);
			output.write(reinterpret_cast<const char*>(&id), 1);
			output.write(reinterpret_cast<const char*>(&cad_index), 1);

			const auto& vehicle = ps.vehicleState;
			output.write(reinterpret_cast<const char*>(&ps.commandTime), 4);
			output.write(reinterpret_cast<const char*>(&ps.origin[0]), 12);
			output.write(reinterpret_cast<const char*>(&ps.viewangles[0]), 12);
			output.write(reinterpret_cast<const char*>(&vehicle.flags), 4);
			output.write(reinterpret_cast<const char*>(&vehicle.origin[0]), 12);
			output.write(reinterpret_cast<const char*>(&vehicle.angles[0]), 12);

			if (id == predicted_data_id::vehicle_gryphon)
			{
				output.write(reinterpret_cast<const char*>(&vehicle.velocity[0]), 12);
				output.write(reinterpret_cast<const char*>(&vehicle.angVelocity[0]), 12);
			}

			assert(!vehicle.splineId);
		}

		void sv_write_predicted_vehicle_data(buffer_t& output, const game::mp::playerState_t& ps,
			std::uint8_t cad_index, predicted_data_id id)
		{
			sv_write_predicted_vehicle_data_(output, ps, cad_index, id);
		}

		void sv_write_predicted_vehicle_data(std::ofstream& output, const game::mp::playerState_t& ps,
			std::uint8_t cad_index, predicted_data_id id)
		{
			sv_write_predicted_vehicle_data_(output, ps, cad_index, id);
		}

		void sv_write_predicted_data_(auto& output, const game::mp::playerState_t& ps, std::size_t send_msg_count)
		{
			const auto vehicle_in_use = (ps.vehicleState.entity && ps.vehicleState.entity != 2047);
			if (!vehicle_in_use)
			{
				sv_write_predicted_player_data(output, ps, static_cast<std::uint8_t>(send_msg_count), predicted_data_id::player);
			}
			else
			{
				const auto data_type = get_predicted_vehicle_type(ps);
				if (data_type == predicted_data_id::vehicle_helo_pilot)
				{
					const auto is_firing = static_cast<bool>(ps.eFlags & 0x800000);
					if (is_firing && send_msg_count % 3 == 0)
					{
						write_helo_pilot_turret_fire(output, 1);
					}
				}

				sv_write_predicted_vehicle_data(output, ps, static_cast<std::uint8_t>(send_msg_count), data_type);
			}
		}

		void sv_write_predicted_data(buffer_t& output, const game::mp::playerState_t& ps, std::size_t send_msg_count)
		{
			sv_write_predicted_data_(output, ps, send_msg_count);
		}

		void sv_write_predicted_data(std::ofstream& output, const game::mp::playerState_t& ps, std::size_t send_msg_count)
		{
			sv_write_predicted_data_(output, ps, send_msg_count);
		}

		void sv_write_general_header_(auto& output, std::string_view player_name)
		{
			static constexpr std::string_view description = "Call of Duty: Modern Warfare Remastered - server demo";

			const auto sv_hostname = get_dvar_string("sv_hostname", true);
			const auto host_name = (sv_hostname.size() < 128)
				? sv_hostname
				: "unknown_server_host_name";

			if (player_name.empty())
			{
				player_name = "unknown_player_name";
			}

			write_general_header_internal(output, description, host_name, player_name);
		}

		void sv_write_general_header(buffer_t& output, std::string_view player_name)
		{
			sv_write_general_header_(output, player_name);
		}

		void sv_write_general_header(std::ofstream& output, std::string_view player_name)
		{
			sv_write_general_header_(output, player_name);
		}
	}

	namespace deserialization
	{
		void process_mod_header(std::span<const std::uint8_t> buffer)
		{
			const std::string_view mod(reinterpret_cast<const char*>(buffer.data()), buffer.size());
			if (mod.size() > 5 && mod.starts_with("mods/"))
			{
				if (const auto* fs_game = game::Dvar_FindVar("fs_game"); fs_game)
				{
					game::Dvar_SetString(fs_game, mod.data() + 5);
				}
			}
		}

		void process_map_header(std::span<const std::uint8_t> buffer)
		{
			assert(!game::Live_SyncOnlineDataFlags(0));

			const std::string_view strings(reinterpret_cast<const char*>(buffer.data()), buffer.size());
			const std::string_view mapname(strings.begin(), std::find(strings.begin(), strings.end(), '\0'));
			const auto begin = strings.begin() + mapname.size() + ((mapname.size() > 0) ? 1 : 0);
			const std::string_view gametype(begin, std::find(begin, strings.end(), '\0'));

			// preload the map so the replayed stream renders. h1-mod has no dummy-party
			// connect; start_map spins a local session for the map. TODO(h1): gametype
			// is dropped (start_map takes only mapname) — set it via a dvar if needed.
			(void)gametype;
			party::start_map(std::string(mapname));
		}

		void process_network_data(std::span<const std::uint8_t> buffer)
		{
			if (buffer.size() > 8)
			{
				const auto svr_msg_sequence = cpy_cast<std::uint32_t>(buffer.data());
				const auto reliable_acknowledge = cpy_cast<std::uint32_t>(buffer.data() + 4);
				const std::span data(buffer.begin() + 8, buffer.end());

				// store server message sequence; is used by the game for delta snapshots
				game::mp::clc->serverMessageSequence = svr_msg_sequence;

				// to prevent 'Client command overflow' error (EXE_ERR_CLIENT_CMD_OVERFLOW)
				game::mp::clc->reliableAcknowledge = game::mp::clc->reliableSequence;

				// NOTE(h1): CL_ParseServerMessage reads the zlib/compression flag from msg+0x30,
				// which is h1-mod's msg_t.targetLocalNetID (its .useZlib sits at 0x34 where the
				// parser never looks). Set the 0x30 field or compressed gamestate/snapshot packets
				// are fed to the parser undecompressed -> garbage -> CL_ParseSnapshot never runs.
				const std::int32_t use_zlib = static_cast<std::int32_t>(reliable_acknowledge >> 31);
				game::msg_t msg{
					.data = reinterpret_cast<char*>(const_cast<std::uint8_t*>(data.data())),
					.cursize = static_cast<std::int32_t>(std::ssize(data)),
					.targetLocalNetID = static_cast<game::netsrc_t>(use_zlib),
					.useZlib = use_zlib
				};

				// parse the network data
				g_demo_feeding = true;
				game::CL_ParseServerMessage(0, &msg);
				g_demo_feeding = false;

				// NOTE(h1): do NOT force-ack lastExecutedServerCommand here. Reliable server
				// commands (e.g. setConfigstring for dynamically-registered custom weapon variants
				// in public matches) must actually be EXECUTED via CG_ExecuteNewServerCommands
				// (see process_server_commands, called from continue_demo_reading each iteration),
				// not just marked done -- force-acking silently discarded them, which is why public-
				// match demos with custom weapons never resolved (private/bot matches only use stock
				// weapons from the base gamestate, so the missing execution went unnoticed). The
				// original EXE_LOSTRELIABLECOMMANDS this hack worked around was actually caused by
				// the msg_t useZlib offset bug (garbage sequence numbers from failed decompression),
				// which is now fixed, so the existing 100-command trailing-window flush should work.
			}
		}

		void trigger_helo_pilot_turret_fire(std::size_t fire_count)
		{
			const std::array entity_nums{ game::mp::cg->ps.viewlocked_entNum, game::mp::cg->ps.vehicleState.entity };
			for (const auto entity_num : entity_nums)
			{
				if (entity_num >= 2047 || entity_num <= 0)
				{
					continue;
				}

				for (std::size_t i = 0; i < std::min(fire_count, std::size_t(10)); ++i)
				{
					game::CG_HandleTurretFire(0, &game::mp::centities[entity_num], 58, 0, false);
				}

				break;
			}
		}

		void process_predicted_data(std::span<const std::uint8_t> buffer,
			std::array<predicted_data_t, 256>& p_data, std::size_t& p_data_index)
		{
			static constexpr auto array_size = std::tuple_size_v<std::remove_reference_t<decltype(p_data)>>;

			if (buffer.size() < 6)
			{
				return;
			}

			auto inc_offset = [offset = std::size_t(0)](std::size_t add_offset) mutable
			{
				const auto old_offset = offset;
				offset += add_offset;

				return old_offset;
			};

			const auto id = cpy_cast<predicted_data_id>(buffer.data() + inc_offset(1));

			if (id == predicted_data_id::vehicle_helo_pilot_turret_fire)
			{
				if (buffer.size() == 6)
				{
					[[maybe_unused]] const auto svr_time = cpy_cast<std::int32_t>(buffer.data() + inc_offset(4));
					const auto fire_count = cpy_cast<std::uint8_t>(buffer.data() + inc_offset(1));

					if (fire_count)
					{
						trigger_helo_pilot_turret_fire(fire_count);
					}
				}
			}
			else
			{
				[[maybe_unused]] const auto index = cpy_cast<std::uint8_t>(buffer.data() + inc_offset(1));
				const auto svr_time = cpy_cast<std::int32_t>(buffer.data() + inc_offset(4));

				auto& data = p_data[p_data_index % array_size];
				game::PlayerVehicleState vehicle{}; /* MWR: no vehicle */

				if (id == predicted_data_id::player)
				{
					if (buffer.size() == 50)
					{
						data.cad.serverTime = svr_time;
						std::memcpy(&data.cad.origin[0], buffer.data() + inc_offset(12), 12);
						std::memcpy(&data.cad.velocity[0], buffer.data() + inc_offset(12), 12);
						std::memcpy(&data.cad.bobCycle, buffer.data() + inc_offset(4), 4);
						std::memcpy(&data.cad.movementDir, buffer.data() + inc_offset(4), 4);
						std::memcpy(&data.viewangles[0], buffer.data() + inc_offset(12), 12); // h1: cg->ps.delta_angles

						++p_data_index %= array_size;
					}
				}
				else
				{
					auto process_vehicle_data = [&inc_offset, &data, &vehicle, &p_data_index, buffer, svr_time](std::size_t expected_buffer_size)
					{
						if (buffer.size() != expected_buffer_size)
						{
							return false;
						}

						data.cad.serverTime = svr_time;
						std::memcpy(&data.cad.origin[0], buffer.data() + inc_offset(12), 12);
						std::memcpy(&data.viewangles[0], buffer.data() + inc_offset(12), 12);
						std::memcpy(&vehicle.flags, buffer.data() + inc_offset(4), 4);
						std::memcpy(&vehicle.origin[0], buffer.data() + inc_offset(12), 12);
						std::memcpy(&vehicle.angles[0], buffer.data() + inc_offset(12), 12);

						vehicle.angles[0] = data.viewangles[0];
						vehicle.angles[1] = data.viewangles[1];

						data.cad.velocity[0] = {};
						data.cad.velocity[1] = {};
						data.cad.velocity[2] = {};
						data.cad.bobCycle = {};
						data.cad.movementDir = {};

						++p_data_index %= array_size;
						return true;
					};

					if (id == predicted_data_id::vehicle_gryphon)
					{
						if (process_vehicle_data(82))
						{
							std::memcpy(&vehicle.velocity[0], buffer.data() + inc_offset(12), 12);
							std::memcpy(&vehicle.angVelocity[0], buffer.data() + inc_offset(12), 12);
						};
					}
					else if (id == predicted_data_id::vehicle_helo_pilot)
					{
						if (process_vehicle_data(58))
						{
							vehicle.targetEntity = -1;
							vehicle.gunAngles[0] = -2.0f;
							vehicle.gunAngles[1] = 0.75f;
						};
					}
					else if (id == predicted_data_id::vehicle_odin)
					{
						if (process_vehicle_data(58))
						{
							vehicle.angles[0] = {};
							vehicle.angles[2] = {};
						};
					}
					else if (id == predicted_data_id::vehicle_unknown)
					{
						process_vehicle_data(58);
					}
				}
			}

			assert(inc_offset(0) == buffer.size());
		}

		bool is_gamestate_valid(game::gameState_t& gs, bool sanitize)
		{
			// attempt to validate the data because CoD command / config strings are notoriously susceptible to exploits

			const auto new_string_data = std::span(gs.stringData);
			const auto new_string_offsets = std::span(gs.stringOffsets);

			if (new_string_data.front() != '\0' || new_string_data.back() != '\0')
			{
				return false;
			}

			for (const auto signed_index : new_string_offsets)
			{
				const auto unsigned_index = static_cast<std::size_t>(signed_index);
				if (unsigned_index >= new_string_data.size())
				{
					return false;
				}

				const std::span str(new_string_data.begin() + unsigned_index,
					std::find(new_string_data.begin() + unsigned_index, new_string_data.end(), '\0'));
				if (str.size() >= 8192)
				{
					return false;
				}

				if (sanitize)
				{
					for (auto& c : str)
					{
						if (c == static_cast<char>(146))
						{
							// CoD string sanitization: [�] -> [']
							c = static_cast<char>(39);
						}
						else if (c <= static_cast<char>(30) || c >= static_cast<char>(127))
						{
							// Quake3 string sanitization: non char -> [.]
							c = static_cast<char>(46);
						}
					}
				}
				else
				{
					const auto non_char_count = std::count_if(str.begin(), str.end(), [](char c)
					{
						return (c <= static_cast<char>(30) || c >= static_cast<char>(127));
					});

					if ((unsigned_index >= 2 && non_char_count >= 16) || non_char_count >= 32)
					{
						return false;
					}
				}
			}

			return true;
		}

		std::optional<game::gameState_t> create_gamestate(std::span<const std::uint8_t> string_data, std::span<const std::uint8_t> string_offsets)
		{
			if (string_data.size() >= sizeof(game::gameState_t::stringData))
			{
				return {};
			}
			if (string_offsets.size() != sizeof(game::gameState_t::stringOffsets))
			{
				return {};
			}

			std::optional<game::gameState_t> opt(std::in_place_t{});

			auto& gs = *opt;
			std::memcpy(&gs.stringData[0], string_data.data(), string_data.size());
			std::memcpy(&gs.stringOffsets[0], string_offsets.data(), string_offsets.size());

			if (!is_gamestate_valid(gs, true))
			{
				return {};
			}

			return opt;
		}

		void process_gamestate_data(std::span<const std::uint8_t> buffer, std::optional<gamestate_t>& opt_gs)
		{
			if (buffer.size() < 12)
			{
				return;
			}

			auto inc_offset = [offset = std::size_t(0)](std::size_t add_offset) mutable
			{
				const auto old_offset = offset;
				offset += add_offset;

				return old_offset;
			};

			const auto svr_cmd_seq = cpy_cast<std::uint32_t>(buffer.data() + inc_offset(4));
			const auto pgs = cpy_cast<packed_gamestate_sizes>(buffer.data() + inc_offset(8));
			const auto size = inc_offset(0) + pgs.string_data + pgs.string_offsets + sizeof(persistent_data_t);

			if (!pgs.compressed || buffer.size() != size)
			{
				return;
			}

			namespace zlib = utils::compression::zlib;
			const std::span<const std::uint8_t> sd(buffer.data() + inc_offset(pgs.string_data), pgs.string_data);
			const auto string_data = zlib::decompress(sd, 4096);

			const std::span<const std::uint8_t> so(buffer.data() + inc_offset(pgs.string_offsets), pgs.string_offsets);
			const auto string_offsets = zlib::decompress(so, sizeof(game::gameState_t::stringOffsets));

			const auto gs = create_gamestate(string_data, string_offsets);
			if (!gs)
			{
				return;
			}

			opt_gs.emplace(gamestate_t{
				.svr_cmd_seq = svr_cmd_seq,
				.dataCount = static_cast<std::int32_t>(string_data.size()),
				.data = *gs
				});

			const auto* src = buffer.data() + inc_offset(sizeof(persistent_data_t));
			auto& dst = get_persistent_data();
			std::memcpy(&dst, src, sizeof(persistent_data_t));

			assert(inc_offset(0) == buffer.size());
		}

		bool read_demo_data(std::ifstream& file, std::vector<std::uint8_t>& buffer, bool one_byte)
		{
			buffer.clear();

			std::size_t size{};
			file.read(reinterpret_cast<char*>(&size), (one_byte) ? 1 : 4);

			if (size > MAX_SIZE)
			{
				return false;
			}

			buffer.resize(size);
			file.read(reinterpret_cast<char*>(buffer.data()), size);

			return file.good() && file.is_open() && !file.eof();
		}

		namespace { int g_command_baseline = 0; }

		void note_command_baseline(int svr_cmd_seq)
		{
			g_command_baseline = svr_cmd_seq;
			// Reset the REAL cgs command counter to the baseline now, matching clc, so a low-baseline
			// (local) demo doesn't leave it above clc (which would make the walker request commands
			// clc hasn't seen -> EXE_ERR_NOT_RECEIVED).
			if (auto* real_cgs = game::CG_GetServerCommandsState())
			{
				*reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(real_cgs) + 0x18) = svr_cmd_seq;
			}
		}

		void process_server_commands()
		{
			static_assert(std::size(game::mp::clientConnection_t{}.serverCommands) > 100);
			assert(game::mp::clc->lastExecutedServerCommand <= game::mp::clc->serverCommandSequence);

			// A demo recorded MID-MATCH bakes in the accumulated reliable-command count (e.g. 612) as
			// its gamestate baseline; recorded right after connecting it's 0 (why only mid-match demos
			// broke). The native per-snapshot CG_ExecuteNewServerCommands walks the REAL cgs->
			// serverCommandSequence (cgs+0x18) up to each snapshot's target, but CG init leaves that
			// real counter at 0, and the h1-mod cgs is only a stub so the gamestate-apply couldn't
			// seed it. Keep the real counter (a) at least at the demo's baseline and (b) no more than
			// the 256-entry ring behind the latest received command (clc->serverCommandSequence) --
			// otherwise the walker requests a command already overwritten -> Com_Error "a reliable
			// command was cycled out". Pre-baseline commands live in the gamestate, and anything >256
			// behind is unrecoverable regardless, so snapping up only skips commands already gone.
			if (auto* real_cgs = game::CG_GetServerCommandsState())
			{
				const int clc_seq = game::mp::clc->serverCommandSequence;
				const int lo = (std::max)(g_command_baseline, clc_seq - 200);
				auto& real_seq = *reinterpret_cast<int*>(
					reinterpret_cast<std::uintptr_t>(real_cgs) + 0x18);
				if (real_seq < lo) real_seq = lo;                // too far behind -> "cycled out"
				else if (real_seq > clc_seq) real_seq = clc_seq; // ahead of received -> "not received"
			}

			// NOTE(h1): the original iw6 100-command threshold assumed a LIVE match's command
			// rate; our replayed matches accumulate serverCommandSequence far more slowly (verified
			// via diagnostics: stayed in the 20s-30s across an entire clip), so the threshold was
			// never reached and reliable commands (e.g. setConfigstring for public-match custom
			// weapons) never executed. CG_ExecuteNewServerCommands(0, serverCommandSequence) is a
			// safe no-op when there's nothing new, so just call it whenever there IS a backlog,
			// regardless of size.
			//
			// NOTE(h1): CG_ExecuteNewServerCommands is bound to 0xFC3D0 (see demo_game.hpp), the
			// real per-snapshot wrapper that loops from cgs->serverCommandSequence up to the target
			// -- NOT 0x11A410, which is only its per-command inner helper. Calling 0x11A410 directly
			// (an earlier version of this code) advanced clc->lastExecutedServerCommand without
			// advancing cgs's own counter, so native per-snapshot processing would later try to
			// catch cgs up from a stale index and trip "Lost reliable commands" once the gap grew
			// past the 256-entry ring window.
			if (game::mp::clc->lastExecutedServerCommand < game::mp::clc->serverCommandSequence)
			{
				game::CG_ExecuteNewServerCommands(0, game::mp::clc->serverCommandSequence);
			}
		}

		bool continue_demo_reading()
		{
			process_server_commands();

			// this affects the playback speed
			return game::mp::cl->snap.serverTime <= game::mp::cl->serverTime;
		}

		std::tuple<demo_data_id, demo_data_id, bool, bool, bool> read_demo_data_id(std::ifstream& file)
		{
			auto id(demo_data_id::eof | demo_data_id::one_byte_flag);
			file.read(reinterpret_cast<char*>(&id), 1);

			return {
				id,
				id & ~demo_data_id::flags,
				static_cast<bool>(id & demo_data_id::unused_flag_1),
				static_cast<bool>(id & demo_data_id::unused_flag_2),

				// check if the id byte contains the flag to indicate the size is one byte instead of four
				static_cast<bool>(id & demo_data_id::one_byte_flag)
			};
		}

		// [clocklog] Chunks consumed by the loop below, cumulative. demo_playback samples the delta
		// per frame and logs it as rd=. THE decisive measurement for the stall/burst bug: bursts were
		// measured with cl->serverTime completely stationary, i.e. with this loop's gate CLOSED --
		// so if snap.serverTime still leaps while rd==0, something outside this feed is writing it.
		int g_demo_chunks_read = 0;

		bool parse_demo(std::ifstream& file, std::vector<std::uint8_t>& buffer,
			std::size_t& post_map_header_file_offset, std::array<predicted_data_t, 256>& pred_data,
			std::size_t& pred_data_index, std::optional<gamestate_t>& gs, bool skip_map_preload, bool& clean_end)
		{
			clean_end = false;
			while (continue_demo_reading())
			{
				++g_demo_chunks_read;
				const auto [full_id, id, flag1, flag2, one_byte] = read_demo_data_id(file);

				if (id == demo_data_id::eof)
				{
					clean_end = file.good();
					return false;
				}
				if (flag1 || flag2)
				{
					console::warn("could not parse demo data id %d\n", full_id);
					return false;
				}
				if (!read_demo_data(file, buffer, one_byte))
				{
					console::warn("could not parse demo data\n");
					return false;
				}

				switch (id)
				{
				case demo_data_id::mod_header:
					process_mod_header(buffer);
					continue;
				case demo_data_id::map_header:
					// store the file offset here to skip map preloading when rewinding
					post_map_header_file_offset = static_cast<std::size_t>(file.tellg());

					// skip_map_preload: demoinstantplay reuses the current live map, so it must
					// NOT call party::start_map (that spawns a second local server via
					// SV_SpawnServer while a match is already running -> crash at 0x554932).
					if (!skip_map_preload)
					{
						process_map_header(buffer);
					}
					continue;
				case demo_data_id::network_data:
					process_network_data(buffer);
					continue;
				case demo_data_id::predicted_data:
					process_predicted_data(buffer, pred_data, pred_data_index);
					continue;
				case demo_data_id::update_gamestate_data:
					process_gamestate_data(buffer, gs);
					continue;

				case demo_data_id::gen_header:
				case demo_data_id::gen_footer:
					continue;
				}

				assert(false);
				continue;
			}

			return true;
		}
	}

	namespace misc
	{
		persistent_data_t& get_persistent_data()
		{
			// not sure if this is strictly well-defined behavior
			static persistent_data_t s{}; return s; /* TODO(h1): real MWR persistentData global (iw6 abs 0x1445A71A4 invalid) */
		}

		void fast_forward_demo(std::uint32_t msec)
		{
			// NOTE(h1): CL_SetCGameTime (0x33C950) recomputes the playback clock every frame as
			// cl->serverTime = max(cl->serverTime, cl->serverTimeDelta + com_frameTime), where
			// com_frameTime (dword_2ED2080) is the global real-time clock that advances each
			// frame. So a direct `cl->serverTime += msec` does NOT fast-forward: the natural
			// term (serverTimeDelta + com_frameTime) is still msec behind the bumped value, so
			// the max() clamp FREEZES serverTime at the bumped value for msec of real time --
			// this was the "stuck after forward/rewind" bug (the pause pin was the only thing
			// nudging serverTime forward, which is exactly why space-mashing eventually unstuck
			// it, stepping one snapshot per toggle). The correct lever is serverTimeDelta (the
			// h1 analog of iw6's cls.realtime, which the iw6 original bumped): raising it by msec
			// makes serverTime = com_frameTime + (delta + msec) jump forward by msec AND keep
			// advancing at real-time rate. continue_demo_reading's gate (snap.serverTime <=
			// serverTime) then has parse_demo burst-read chunks until the freshly-read snapshots
			// catch up to the new clock -- a real fast-forward with no freeze.
			if (*game::mp::connstate == game::CA_ACTIVE && game::mp::cl.get())
			{
				game::mp::cl->serverTimeDelta += static_cast<std::int32_t>(msec);
			}
		}

		void check_address_assertions()
		{
#if 0 // iw6 address/offset asserts — INVALID on h1 (MWR layout differs). TODO(h1): re-verify against DEMO_SYSTEM_PORT.md.
			assert(std::bit_cast<std::size_t>(game::mp::connstate.get()) == 0x1419E1AE0);
			assert(std::bit_cast<std::size_t>(&game::mp::cls->realtime) == 0x141D1AC80);

			static_assert(offsetof(game::mp::clientStatic_t, gameState) == 0x80EE0);
			assert(std::bit_cast<std::size_t>(&game::mp::cls->gameState) == 0x141D9BA40);

			static_assert(offsetof(game::mp::clientActive_t, snap) == 0x8 && offsetof(game::mp::clSnapshot_t, serverTime) == 0x347C);
			assert(std::bit_cast<std::size_t>(&game::mp::cl->snap.serverTime) == 0x1419E50F4);

			static_assert(offsetof(game::mp::clientActive_t, snap) == 0x8 && offsetof(game::mp::clSnapshot_t, deltaNum) == 0x3484);
			assert(std::bit_cast<std::size_t>(&game::mp::cl->snap.deltaNum) == 0x1419E50FC);

			static_assert(offsetof(game::mp::clientActive_t, serverTime) == 0x34D0);
			assert(std::bit_cast<std::size_t>(&game::mp::cl->serverTime) == 0x1419E5140);

			static_assert(offsetof(game::mp::clientActive_t, newSnapshots) == 0x34DC);
			assert(std::bit_cast<std::size_t>(&game::mp::cl->newSnapshots) == 0x1419E514C);

			static_assert(offsetof(game::mp::clientActive_t, clientArchive) == 0x542C);
			assert(std::bit_cast<std::size_t>(&game::mp::cl->clientArchive) == 0x1419E709C);

			static_assert(offsetof(game::mp::clientActive_t, clientArchiveIndex) == 0xE02C);
			assert(std::bit_cast<std::size_t>(&game::mp::cl->clientArchiveIndex) == 0x1419EFC9C);

			static_assert(offsetof(game::mp::clientConnection_t, reliableSequence) == 0x12C);
			assert(std::bit_cast<std::size_t>(&game::mp::clc->reliableSequence) == 0x141CB547C);

			static_assert(offsetof(game::mp::clientConnection_t, reliableAcknowledge) == 0x130);
			assert(std::bit_cast<std::size_t>(&game::mp::clc->reliableAcknowledge) == 0x141CB5480);

			static_assert(offsetof(game::mp::clientConnection_t, serverCommandSequence) == 0x20138);
			assert(std::bit_cast<std::size_t>(&game::mp::clc->serverCommandSequence) == 0x141CD5488);

			static_assert(offsetof(game::mp::clientConnection_t, lastExecutedServerCommand) == 0x2013C);
			assert(std::bit_cast<std::size_t>(&game::mp::clc->lastExecutedServerCommand) == 0x141CD548C);

			static_assert(offsetof(game::mp::cgs_t, serverCommandSequence) == 0x18);
			assert(std::bit_cast<std::size_t>(&game::mp::cgs->serverCommandSequence) == 0x14187EB98);

			static_assert(offsetof(game::mp::cgs_t, szHostName) == 0x44);
			assert(std::bit_cast<std::size_t>(&game::mp::cgs->szHostName) == 0x14187EBC4);

			static_assert(offsetof(game::mp::cg_t, clientNum) == 0x3360);
			assert(std::bit_cast<std::size_t>(&game::mp::cg->clientNum) == 0x141771F60);

			static_assert(offsetof(game::mp::cg_t, demoType) == 0x3368);
			assert(std::bit_cast<std::size_t>(&game::mp::cg->demoType) == 0x141771F68);

			static_assert(offsetof(game::mp::cg_t, refdefViewAngles) == 0xA9A90);
			assert(std::bit_cast<std::size_t>(&game::mp::cg->refdefViewAngles) == 0x141818690);

			static_assert(offsetof(game::mp::cg_t, weaponSelect) == 0xB2768);
			assert(std::bit_cast<std::size_t>(&game::mp::cg->weaponSelect) == 0x141821368);

			static_assert(offsetof(game::mp::cg_t, inKillCam) == 0xB537C);
			assert(std::bit_cast<std::size_t>(&game::mp::cg->inKillCam) == 0x141823F7C);

			static_assert(offsetof(game::mp::cg_t, bgs) == 0xB54C8);
			assert(std::bit_cast<std::size_t>(&game::mp::cg->bgs) == 0x1418240C8);

			static_assert(offsetof(game::mp::cg_t, cvsData) == 0x10AD28);
			assert(std::bit_cast<std::size_t>(&game::mp::cg->cvsData) == 0x141879928);

			static_assert(offsetof(game::mp::cg_t, ps) == 0
				&& offsetof(game::mp::playerState_t, vehicleState) == 0xBC
				&& offsetof(game::PlayerVehicleState, entity) == 0);
			assert(std::bit_cast<std::size_t>(&game::mp::cg->ps.vehicleState.entity) == 0x14176ECBC);

			static_assert(offsetof(game::mp::cg_t, ps) == 0 && offsetof(game::mp::playerState_t, otherFlags) == 0x10);
			assert(std::bit_cast<std::size_t>(&game::mp::cg->ps.otherFlags) == 0x14176EC10);

			static_assert(offsetof(game::mp::cg_t, ps) == 0 && offsetof(game::mp::playerState_t, viewlocked_entNum) == 0x1E8);
			assert(std::bit_cast<std::size_t>(&game::mp::cg->ps.viewlocked_entNum) == 0x14176EDE8);

			static_assert(offsetof(game::mp::playerState_t, bobCycle) == 0x18);
			static_assert(offsetof(game::mp::playerState_t, origin) == 0x1C);
			static_assert(offsetof(game::mp::playerState_t, velocity) == 0x28);
			static_assert(offsetof(game::mp::playerState_t, movementDir) == 0xA8);
			static_assert(offsetof(game::mp::playerState_t, viewangles) == 0x184);

			static_assert(offsetof(game::msg_t, useZlib) == 0x30);

			static_assert(offsetof(game::mp::client_t, gentity) == 0x41E68);
			static_assert(offsetof(game::mp::client_t, name) == 0x41E70);
			static_assert(offsetof(game::mp::client_t, lastConnectTime) == 0x41E84);
			static_assert(offsetof(game::mp::client_t, playerGuid) == 0x41EB4);
			static_assert(offsetof(game::mp::client_t, testClient) == 0x41ECC);

			static_assert(offsetof(game::mp::serverStatic_t, time) == 0x1681D00);
			assert(std::bit_cast<std::size_t>(&game::mp::svs->time) == 0x14647B280);

			static_assert(offsetof(game::mp::serverStatic_t, snapFlagServerBit) == 0x1681D08);
			assert(std::bit_cast<std::size_t>(&game::mp::svs->snapFlagServerBit) == 0x14647B288);

			static_assert(offsetof(game::mp::serverStatic_t, clientCount) == 0x1681D0C);
			assert(std::bit_cast<std::size_t>(&game::mp::svs->clientCount) == 0x14647B28C);

			static_assert(offsetof(game::mp::serverStatic_t, clients) == 0x1681D10);
			assert(std::bit_cast<std::size_t>(&game::mp::svs->clients) == 0x14647B290);
#endif
		}
	}
}
