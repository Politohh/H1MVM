#include "std_include.hpp"
#include "loader/component_loader.hpp"

#include "demo_playback.hpp"
#include "demo_utils.hpp"

#include "component/command.hpp"
#include "component/console.hpp"
#include "component/party.hpp"
#include "component/scheduler.hpp"
#include "utils/command_line.hpp"
#include "utils/hook.hpp"

using namespace demo_utils;

namespace // demo class
{
	class demo_playback_t
	{
	public:
		demo_playback_t();

		[[nodiscard]] bool is_demo_playing() const;
		[[nodiscard]] bool is_demo_repeating() const;
		[[nodiscard]] const std::optional<gamestate_t>& get_gamestate() const;
		[[nodiscard]] bool rewind_demo(std::optional<std::uint32_t> msec);
		[[nodiscard]] bool start(std::filesystem::path& path, bool repeat);
		void set_skip_map_preload(bool value) { skip_map_preload_ = value; }
		[[nodiscard]] bool is_replay_armed() const { return replay_armed_; }
		void set_replay_armed(bool value) { replay_armed_ = value; }
		[[nodiscard]] bool is_gamestate_applied() const { return gamestate_applied_; }
		void set_gamestate_applied(bool value) { gamestate_applied_ = value; }
		[[nodiscard]] std::optional<std::string> peek_mapname();
		void bootstrap_map_load();
		[[nodiscard]] std::optional<std::reference_wrapper<std::filesystem::path>> restart();
		template <typename Func, typename... Args> auto use_predicted_data(Func func, Args&&... args);
		template <typename Func, typename... Args> auto handle_fast_forward(Func func, Args&&... args);

		void parse_demo_wrapper();
		void stop();

	private:
		void repeat();

		bool repeat_{};
		bool skip_timeout_{};
		bool retain_timescale_{};
		bool skip_map_preload_{}; // demoinstantplay: reuse current live map, don't start_map
		bool replay_armed_{};     // becomes true once the client reaches CA_ACTIVE (handshake done)
		bool gamestate_applied_{}; // becomes true once the recorded gamestate has been applied once
		std::size_t post_map_header_file_offset_{};
		std::size_t pred_data_index_{};
		std::array<predicted_data_t, 256> pred_data_{};
		std::optional<std::int32_t> rewind_snap_svr_time_;
		std::optional<gamestate_t> gs_;
		std::vector<std::uint8_t> buffer_;
		std::filesystem::path path_;
		std::ifstream file_;
	} demo;

	demo_playback_t::demo_playback_t()
	{
		buffer_.reserve(MAX_SIZE);
	}

	bool demo_playback_t::is_demo_playing() const
	{
		return file_.good() && file_.is_open();
	}

	bool demo_playback_t::is_demo_repeating() const
	{
		return repeat_;
	}

	const std::optional<gamestate_t>& demo_playback_t::get_gamestate() const
	{
		return gs_;
	}

	template <typename Func, typename ...Args>
	auto demo_playback_t::use_predicted_data(Func func, Args&& ...args)
	{
		return func(pred_data_, pred_data_index_, std::forward<Args>(args)...);
	}

	template <typename Func, typename ...Args>
	auto demo_playback_t::handle_fast_forward(Func func, Args&& ...args)
	{
		return func(rewind_snap_svr_time_, skip_timeout_, std::forward<Args>(args)...);
	}

	bool demo_playback_t::rewind_demo(std::optional<std::uint32_t> msec)
	{
		if (!is_demo_playing())
		{
			return false;
		}

		// trigger demo end-of-file which will repeat the demo
		repeat_ = true;
		file_.seekg(0, std::ios::end);

		if (msec)
		{
			retain_timescale_ = true;

			const auto snap_svr_time = game::mp::cl->snap.serverTime;
			if (const auto delta_time = snap_svr_time - static_cast<std::int32_t>(*msec) - 50; delta_time > 0)
			{
				rewind_snap_svr_time_ = delta_time;
			}
		}

		// workaround to get rewinding working without delay at low timescales
		fast_forward_demo(1000);

		return true;
	}

	bool demo_playback_t::start(std::filesystem::path& path, bool repeat)
	{
		// check if the file can be opened before taking any action with the demo file stream
		std::ifstream file(path, std::ios::binary);

		const auto can_open = file.good() && file.is_open();
		if (!can_open)
		{
			return false;
		}

		stop();

		file_ = std::move(file);
		assert(file_.good() && file_.is_open());

		// (re)set data
		repeat_ = repeat;
		gs_.reset();
		path_ = std::move(path);
		get_persistent_data() = {};

		if (auto* demo_timescale = game::Dvar_FindVar("demotimescale"); demo_timescale)
		{
			demo_timescale->current.value = 1.0f;
		}

		if (game::Live_SyncOnlineDataFlags(0))
		{
			console::info("the demo should start to load in a few seconds\n");
		}

		return true;
	}

	// Read the demo's map_header chunk to learn its map name, then rewind the file so the
	// normal parse_demo re-reads from the start. Chunk framing: [id:1][size:1 or 4][data].
	std::optional<std::string> demo_playback_t::peek_mapname()
	{
		if (!file_.good() || !file_.is_open())
		{
			return std::nullopt;
		}

		file_.clear();
		file_.seekg(0, std::ios::beg);

		std::optional<std::string> result;

		for (int guard = 0; guard < 32 && !result; ++guard)
		{
			std::uint8_t id_byte = 0;
			file_.read(reinterpret_cast<char*>(&id_byte), 1);
			if (!file_.good())
			{
				break;
			}

			const bool one_byte = (id_byte & 0x80) != 0; // one_byte_flag
			const int id = id_byte & 0x1F;               // strip flag bits (0xE0)

			std::size_t size = 0;
			file_.read(reinterpret_cast<char*>(&size), one_byte ? 1 : 4);
			if (!file_.good() || size > MAX_SIZE)
			{
				break;
			}

			if (id == 1) // map_header : "mapname\0gametype\0"
			{
				std::string buf(size, '\0');
				file_.read(buf.data(), static_cast<std::streamsize>(size));
				if (!file_.good())
				{
					break;
				}
				if (const auto nul = buf.find('\0'); nul != std::string::npos)
				{
					buf.resize(nul);
				}
				result = std::move(buf);
			}
			else
			{
				file_.seekg(static_cast<std::streamoff>(size), std::ios::cur);
			}
		}

		// rewind so the normal parse_demo re-reads from the beginning
		file_.clear();
		file_.seekg(0, std::ios::beg);

		return result;
	}

	// demoplay bootstrap: at the menu CL_SetCGameTime is dead so parse_demo never runs
	// process_map_header. Drive the map load here from a clean (disconnected) state so the
	// demo replays into a FRESH session (its snapshot numbers exceed the empty session's,
	// so the demo's snapshots win the CG_ProcessSnapshots race instead of being rejected).
	void demo_playback_t::bootstrap_map_load()
	{
		const auto mapname = peek_mapname();
		if (!mapname || mapname->empty())
		{
			return;
		}

		// we drive the map load ourselves -> parse_demo must NOT call start_map again
		skip_map_preload_ = true;
		party::start_map(*mapname);
	}

	std::optional<std::reference_wrapper<std::filesystem::path>> demo_playback_t::restart()
	{
		if (!start(path_, repeat_))
		{
			return path_;
		}

		return std::nullopt;
	}

	void demo_playback_t::parse_demo_wrapper()
	{
		if (!parse_demo(file_, buffer_, post_map_header_file_offset_, pred_data_, pred_data_index_, gs_, skip_map_preload_))
		{
			if (is_demo_repeating() && *game::mp::connstate != game::CA_DISCONNECTED)
			{ // don't repeat demo if it was stopped manually
				repeat();
			}
			else
			{
				stop();
			}
		}

		assert(*game::mp::connstate != game::CA_DISCONNECTED || !is_demo_playing());
	}

	void demo_playback_t::stop()
	{
		skip_timeout_ = {};
		retain_timescale_ = {};
		skip_map_preload_ = {};
		replay_armed_ = {};
		gamestate_applied_ = {};
		post_map_header_file_offset_ = {};
		pred_data_index_ = {};
		pred_data_.fill({});
		rewind_snap_svr_time_ = {};
		gs_.reset();
		buffer_.clear();
		file_.clear();
		file_.close();

		// ensure that sv_cheats is always disabled after demo playback
		if (auto* sv_cheats = game::Dvar_FindVar("sv_cheats"); sv_cheats)
		{
			sv_cheats->current.enabled = false;
		}
	}

	void demo_playback_t::repeat()
	{
		assert(post_map_header_file_offset_ < 256);

		if (auto& connstate = *game::mp::connstate; connstate == game::CA_ACTIVE)
		{
			// prevent old snapshots from being processed; would mess up the server command sequences
			connstate = game::CA_PRIMED;
		}

		// reset game data
		game::mp::clc->serverCommandSequence = {};
		game::mp::clc->lastExecutedServerCommand = {};
		game::mp::cgs->serverCommandSequence = {};
		get_persistent_data() = {};

		if (auto* demo_timescale = game::Dvar_FindVar("demotimescale"); demo_timescale)
		{
			if (!retain_timescale_)
			{
				demo_timescale->current.value = 1.0f;
			}
		}

		// reset class data
		skip_timeout_ = {};
		retain_timescale_ = {};
		pred_data_index_ = {};

		file_.clear();
		file_.seekg(post_map_header_file_offset_, std::ios::beg);
	}
}

namespace // hooks
{
	utils::hook::detour CG_UpdateOmnvars_hook;
	utils::hook::detour CG_VisionSetStartLerp_To_hook;
	utils::hook::detour CG_DrawActiveFrame_hook;
	utils::hook::detour CL_GetPredictedPlayerInformationForServerTime_hook;
	utils::hook::detour CL_GetPredictedVehicleForServerTime_hook;
	utils::hook::detour CL_SetCGameTime_hook;
	utils::hook::detour CL_WritePacket_hook;
	utils::hook::detour GetRemoteEyeValues_hook;
	utils::hook::detour LUI_IntermissionBegan_hook;
	utils::hook::detour VehicleCam_UpdatePlayerControlCam_hook;

	game::dvar_t* demo_vision;

	static constexpr std::array VISION_SETS{
		"disabled", "no vision", "default",

		"ac130", "ac130_enhanced_mp", "ac130_inverted", "aftermath", "aftermath_glow", "aftermath_post", "black_bw",
		"cheat_bw", "coup_sunblind", "default_night", "default_night_mp", "end_game", "missilecam", "mpintro",
		"mpnuke", "mpnuke_aftermath", "mpoutro", "near_death", "near_death_mp", "nuke_global_flash", "thermal_mp", "thermal_snowlevel_mp",

		"aftermath_map", static_cast<const char*>(nullptr)
	};
	static_assert(
		std::string_view(VISION_SETS[0]) == "disabled" &&
		std::string_view(VISION_SETS[1]) == "no vision" &&
		std::string_view(VISION_SETS[2]) == "default" &&
		std::string_view(VISION_SETS[VISION_SETS.size() - 2]) == "aftermath_map" &&
		VISION_SETS.back() == nullptr // enum dvars require a nullptr as last value
		);

	bool handle_vision_override(game::mp::ClientVisionSetData& cvs_data, std::int32_t index, const char* vision_name, std::int32_t duration, std::int32_t time)
	{
		if (demo.is_demo_playing())
		{
			const auto vision_set = static_cast<std::size_t>((demo_vision) ? demo_vision->current.integer : 0);

			if (vision_set == 1)
			{
				// enable 'post apply' vision
				cvs_data.visionSetLerpData[index].style = 1;
				cvs_data.postApplyLerpDest = 1.0f;

				return true;
			}
			else if (vision_set >= 2 && vision_set + 2 <= VISION_SETS.size())
			{
				// enable 'post apply' vision
				cvs_data.visionSetLerpData[index].style = 0;
				cvs_data.postApplyLerpDest = 1.0f;

				if (vision_set + 2 == VISION_SETS.size())
				{
					if (const auto mapname = get_mapname(false); mapname.empty())
					{
						return CG_VisionSetStartLerp_To_hook.invoke<bool>(&cvs_data, index, "aftermath_post", duration, time);
					}
					else
					{
						// a small number of maps have a vision called 'aftermath_mp_mapname'
						return CG_VisionSetStartLerp_To_hook.invoke<bool>(&cvs_data, index, std::format("aftermath_{}", mapname).c_str(), duration, time);
					}
				}
				else
				{
					return CG_VisionSetStartLerp_To_hook.invoke<bool>(&cvs_data, index, VISION_SETS[vision_set], duration, time);
				}
			}
		}

		return CG_VisionSetStartLerp_To_hook.invoke<bool>(&cvs_data, index, vision_name, duration, time);
	}

	void handle_vision()
	{
		if (demo_vision && demo_vision->modified)
		{
			const auto vision_set = demo_vision->current.integer;

			for (auto i = 0; i < game::CS_VISIONSET_COUNT; ++i)
			{
				const std::string_view vision_name = (!vision_set)
					? game::mp::cg->cvsData.visionName[i]
					: "";

				handle_vision_override(game::mp::cg->cvsData, i, vision_name.data(), 1, game::mp::cl->serverTime);
			}

			if (!vision_set)
			{
				// disable 'post apply' vision if the game hasn't used it yet
				if (game::mp::cg->cvsData.visionName[5][0] == '\0')
				{
					game::mp::cg->cvsData.postApplyLerpDest = 0.0f;
				}
			}
			else if (vision_set == 1)
			{
				std::memset(&game::mp::cg->cvsData.visionSetCurrent, 0, sizeof(game::mp::cg->cvsData.visionSetCurrent));
			}

			// reset modified flag to avoid doing the same work each frame
			demo_vision->modified = false;
		}
	}

	void handle_fast_forward(auto old_connstate)
	{
		// On MWR the demo playback path can observe CA_ACTIVE while the clientActive_t
		// pointer (*(0x2EC84F0)) is still NULL (no live client connection). The iw6
		// original assumed cl!=null whenever connstate>=PRIMED; guard the cl-> reads to
		// avoid a null-cl access violation (crash: read at 0x4A84 = offsetof cl->serverTime).
		if (const auto new_connstate = *game::mp::connstate; new_connstate == game::CA_ACTIVE && game::mp::cl.get())
		{
			const auto server_time = game::mp::cl->serverTime;
			const auto snap_svr_time = game::mp::cl->snap.serverTime;

			demo.handle_fast_forward([old_connstate, server_time, snap_svr_time](
				std::optional<std::int32_t>& rewind_snap_svr_time, bool& skip_timeout)
			{
				if (old_connstate == game::CA_PRIMED)
				{
					if (rewind_snap_svr_time)
					{
						// handle fast forward after rewinding
						if (*rewind_snap_svr_time > snap_svr_time)
						{
							fast_forward_demo(*rewind_snap_svr_time - snap_svr_time);
						}

						rewind_snap_svr_time = {};
					}
				}
				else if (old_connstate == game::CA_ACTIVE && !skip_timeout)
				{
					if (const auto delta_time = snap_svr_time - server_time; delta_time >= 1000)
					{
						// skip timeout between first and second snapshots (for user demos)
						fast_forward_demo(delta_time);
						skip_timeout = true;
					}
				}
			});
		}
	}

	void handle_demo_patches()
	{
		// set cg_t->demoType to DEMO_TYPE_CLIENT to disable mouse input and avoid frame interpolation issues
		game::mp::cg->demoType = game::mp::DEMO_TYPE_CLIENT;

		// ensure that sv_cheats is always enabled during demo playback
		if (auto* sv_cheats = game::Dvar_FindVar("sv_cheats"); sv_cheats)
		{
			sv_cheats->current.enabled = true;
		}

		// sometimes the hud isn't updated after a weapon change; force update the weapon name and ammo (clip) when changing weapons
		// various other Call of Duty games are also affected by this bug
		if (game::mp::cg->weaponSelect.data != static_cast<std::size_t>(game::mp::cg->ps.weapon))
		{
			game::CG_SelectWeapon(0, game::mp::cg->ps.weapon, 0);
		}
	}

	void handle_demo_reading(const std::int32_t client_num)
	{
		// TODO(h1): iw6 also gated the playing branch on Live_SyncOnlineDataFlags(0)==0,
		// but on MWR that bitmask stays non-zero (menu + local match), so the playing
		// branch never ran. Gating only on is_demo_playing() so playback can actually run.
		if (!demo.is_demo_playing())
		{
			// STAGE 1: not playing a demo -> clean pass-through. The original iw6 wrote
			// cg->demoType = NONE here EVERY frame, but this fires before cgame is
			// initialized (menu) where cg is invalid, and cg->demoType's MWR offset
			// (iw6 0x598C) is unverified -> access violation. TODO(h1): restore once the
			// demoType offset is confirmed AND guarded by game::CL_IsCgameInitialized().
			// game::mp::cg->demoType = game::mp::DEMO_TYPE_NONE;

			CL_SetCGameTime_hook.invoke<void>(client_num);
		}
		else
		{
			// ARM gate: after a demoplay bootstrap the client is still completing the loopback
			// handshake (connstate 0->4->9->10). We must let that finish BEFORE feeding the demo
			// or suppressing outgoing packets — otherwise the client can never send its connect
			// packets, the handshake stalls, and the premature feed breaks the server-command
			// sequence ("Lost reliable commands"). Only take over once CA_ACTIVE is reached.
			// IMPORTANT: connstate is transiently 0 (DISCONNECTED) for a frame right after
			// start_map, so we must NOT treat 0 as a user-disconnect until we are armed.
			if (!demo.is_replay_armed())
			{
				// Arm at CA_PRIMED (not ACTIVE): we must take over BEFORE the loopback server's
				// first snapshot is processed (which would set the CGAME latestSnapshotNum ahead
				// of the demo's snapshot numbers). Dropping loopback snapshots at >=PRIMED (see
				// capture_data) then lets the demo's own gamestate+snapshot drive PRIMED->ACTIVE
				// with fresh snapshot numbering. PRIMED already means assets are loaded.
				if (static_cast<int>(*game::mp::connstate) >= static_cast<int>(game::CA_PRIMED))
				{
					demo.set_replay_armed(true);
				}
				else
				{
					// let the connection handshake proceed normally (no feed, no suppression)
					CL_SetCGameTime_hook.invoke<void>(client_num);
					return;
				}
			}

			// armed: if the user disconnected, stop the demo so it can't re-drive a map load /
			// start_map after disconnect (the reload-on-disconnect crasher).
			if (*game::mp::connstate == game::CA_DISCONNECTED)
			{
				demo.stop();
				CL_SetCGameTime_hook.invoke<void>(client_num);
				return;
			}

			const auto old_connstate = *game::mp::connstate;
			if (old_connstate == game::CA_ACTIVE && game::mp::cl.get())
			{
				// reset cl->newSnapshots to prevent CL_SetCGameTime from making a call to CL_AdjustTimeDelta,
				// which interferes with the timescale
				// (guard cl.get(): on MWR connstate can read CA_ACTIVE with a null clientActive_t pointer)
				game::mp::cl->newSnapshots = 0;
			}

			CL_SetCGameTime_hook.invoke<void>(client_num);

			handle_fast_forward(old_connstate);

			// handle_vision + handle_demo_patches write cg fields; cg (CG_GetLocalClientGlobals)
			// is null/invalid until cgame is initialized (a demo map is loaded). Guard to avoid
			// the null-cg access violation. TODO(h1): also verify cg field offsets (demoType/
			// weaponSelect/cvsData/refdefViewAngles) once a demo map actually loads.
			if (game::CL_IsCgameInitialized())
			{
				handle_vision(); // inert unless the demovision dvar is modified
				// handle_demo_patches(); // TODO(h1): cosmetic (writes cg->demoType/weaponSelect
				// at UNVERIFIED cg offsets). Disabled so the core network replay can be tested
				// without cg-write corruption. Confirmed so far: cg->weaponSelect @ +0xE99F8.
			}

			demo.parse_demo_wrapper();
		}
	}

	void write_packet(const std::int32_t client_num)
	{
		// Only suppress outgoing packets once the replay is armed (client is ACTIVE). During the
		// demoplay bootstrap handshake the client MUST be able to send its connect/challenge
		// packets, or connstate never advances past ~4 and the demo feed corrupts the sequence.
		if (!demo.is_demo_playing() || !demo.is_replay_armed())
		{
			CL_WritePacket_hook.invoke<void>(client_num);
		}
	}


	bool get_predicted_player_data(const game::mp::clientActive_t& cl, std::int32_t ps_time, game::mp::playerState_t& ps)
	{
		if (!demo.is_demo_playing() || !demo.is_replay_armed())
		{
			return CL_GetPredictedPlayerInformationForServerTime_hook.invoke<bool>(&cl, ps_time, &ps);
		}

		bool found = false;
		demo.use_predicted_data([&ps, ps_time, &found](const std::array<predicted_data_t, 256>& pred_data, std::size_t pred_data_index)
		{
			static constexpr auto pred_data_size = std::tuple_size_v<std::remove_reference_t<decltype(pred_data)>>;

			// find most recent predicted player data
			for (std::size_t count = 0, index = pred_data_index - 1; count < pred_data_size; ++count, --index)
			{
				const auto& p_data = pred_data[index % pred_data_size];
				if (p_data.cad.serverTime <= ps_time && ps_time - p_data.cad.serverTime < 500)
				{
					std::memcpy(&ps.origin[0], &p_data.cad.origin[0], sizeof(ps.origin));
					std::memcpy(&ps.velocity[0], &p_data.cad.velocity[0], sizeof(ps.velocity));
					// ps.delta_angles (verified offset 0x12C = cg+300) is the REAL h1 view-angle
					// source (CG_CalcViewValues copies it to cg+912912 then AnglesToAxis's it into
					// cg->refdef.view.axis every frame) — NOT ps.viewangles (an unverified iw6-
					// ported field the engine never reads; that was the whole bug).
					std::memcpy(&ps.delta_angles[0], &p_data.viewangles[0], sizeof(ps.delta_angles));
					ps.bobCycle = p_data.cad.bobCycle;
					ps.movementDir = p_data.cad.movementDir;
					found = true;

					break;
				}
			}
		});

		static_cast<void>(found); // false only if no predicted_data chunk is near ps_time (old demos)

		// must return true otherwise branch is taken that undoes the work of this function
		return true;
	}

	std::int32_t draw_active_frame_stub(const std::int32_t local_client_num, const std::int32_t server_time,
		std::int32_t demo_type, const std::int32_t cubemap_shot, const std::int32_t cubemap_size,
		const std::int32_t render_screen, const unsigned int a7)
	{
		// force the engine's own DEMO_TYPE_CLIENT path (its 3rd param, confirmed by h1-mod's own
		// symbols.hpp signature, matching the AW-family s1-mod demo mod's identical override) --
		// this is what actually suppresses local mouse/keyboard input (movement, fire, ADS) and
		// avoids frame-interpolation issues, entirely inside the engine's own logic. Replaces the
		// old (unverified-offset, never-enabled) approach of writing cg->demoType by hand.
		if (demo.is_demo_playing() && demo.is_replay_armed())
		{
			demo_type = game::mp::DEMO_TYPE_CLIENT;
		}

		return CG_DrawActiveFrame_hook.invoke<std::int32_t>(
			local_client_num, server_time, demo_type, cubemap_shot, cubemap_size, render_screen, a7);
	}

	bool get_predicted_vehicle_data(const game::mp::clientActive_t& cl, std::int32_t ps_time, game::PlayerVehicleState& ps_vehicle)
	{
		if (!demo.is_demo_playing() || !demo.is_replay_armed())
		{
			return CL_GetPredictedVehicleForServerTime_hook.invoke<bool>(&cl, ps_time, &ps_vehicle);
		}

		demo.use_predicted_data([&ps_vehicle, ps_time](const std::array<predicted_data_t, 256>& pred_data, std::size_t pred_data_index)
		{
			static constexpr auto pred_data_size = std::tuple_size_v<std::remove_reference_t<decltype(pred_data)>>;

			// find most recent predicted vehicle data
			for (std::size_t count = 0, index = pred_data_index - 1; count < pred_data_size; ++count, --index)
			{
				const auto& p_data = pred_data[index % pred_data_size];
				if (p_data.cad.serverTime <= ps_time && ps_time - p_data.cad.serverTime < 500)
				{
					// MWR has no vehicle prediction (ClientArchiveData carries no vehicle
					// state), so there is nothing to restore. Kept for iw6 parity.
					break;
				}
			}
		});

		// must return true otherwise branch is taken that undoes the work of this function
		return true;
	}

	bool show_intermission_menu(const std::int32_t client_num, void* lua_vm)
	{
		// it appeared easier to prevent the intermission menu from being shown instead of closing or hiding it

		if (!demo.is_demo_playing() || !demo.is_demo_repeating())
		{
			return LUI_IntermissionBegan_hook.invoke<bool>(client_num, lua_vm);
		}

		// rewind demo and don't display intermission menu for repeated demos
		static_cast<void>(demo.rewind_demo(std::nullopt));

		return false;
	}

	void control_ui_element(const std::int32_t client_num, const game::mp::snapshot_t& old_snap, game::mp::snapshot_t& new_snap)
	{
		// it appeared easier to prevent these menus from being shown instead of closing or hiding them

		if (demo.is_demo_playing())
		{
			// skip team selection menu (and loadout selection menu)
			assert(std::string_view(*reinterpret_cast<const char**>(game::Omnvar_GetDef(103))) == "ui_options_menu");
			new_snap.omnvars[103] = {};

			if (demo.is_demo_repeating())
			{
				// skip extinction mode end-of-game score / stats menu
				assert(std::string_view(*reinterpret_cast<const char**>(game::Omnvar_GetDef(194))) == "ui_alien_show_eog_score");
				new_snap.omnvars[194] = {};
			}
		}

		CG_UpdateOmnvars_hook.invoke<void>(client_num, &old_snap, &new_snap);
	}

	std::int32_t update_gamestate_config_strings(game::msg_t& msg)
	{
		// NOTE(h1): the native function this hook is spliced into (sub_342770, dispatch case 6)
		// fires MANY times per clip (measured: ~30 times in a short session, roughly once per
		// server-command increment), NOT once at connect as originally assumed. Applying the
		// recorded gamestate on every call would repeatedly reset clc's sequence counters back to
		// the initial recorded value, corrupting the sequence progression established by the real
		// server-command/snapshot processing that happens in between. Apply it exactly ONCE.
		if (demo.is_demo_playing() && !demo.is_gamestate_applied())
		{
			if (const auto& gs = demo.get_gamestate(); gs)
			{
				game::mp::cls->gameState = gs->data; // stringOffsets + stringData only (contiguous)
				game::mp::cs_data_count = gs->dataCount; // the REAL, separately-addressed cursor
				game::mp::clc->serverCommandSequence = gs->svr_cmd_seq;
				game::mp::clc->lastExecutedServerCommand = gs->svr_cmd_seq;
				game::mp::cgs->serverCommandSequence = gs->svr_cmd_seq;
				demo.set_gamestate_applied(true);
			}
		}

		// call MSG_ReadLong that this hook replaces
		return game::MSG_ReadLong(&msg);
	}

	void ignore_mouse_input_killstreak_reward_1(const std::int32_t client_num, game::mp::cg_t& cg, game::vec3_t& out_view_origin, game::vec3_t& out_view_angles)
	{
		auto& in_killcam = cg.inKillCam;

		if (!demo.is_demo_playing() || in_killcam)
		{
			VehicleCam_UpdatePlayerControlCam_hook.invoke<void>(client_num, &cg, &out_view_origin, &out_view_angles);
		}
		else
		{
			// required for e.g. the Gryphon killstreak reward
			in_killcam = 1;
			VehicleCam_UpdatePlayerControlCam_hook.invoke<void>(client_num, &cg, &out_view_origin, &out_view_angles);
			in_killcam = 0;
		}
	}

	bool ignore_mouse_input_killstreak_reward_2(const std::int32_t client_num)
	{
		auto& other_flags = game::mp::cg->ps.otherFlags;

		if (!demo.is_demo_playing() || !(other_flags & 0x4000))
		{
			return GetRemoteEyeValues_hook.invoke<bool>(client_num);
		}
		else
		{
			// required for e.g. the Trinity Rocket killstreak reward
			other_flags &= ~0x4000;
			const auto result = GetRemoteEyeValues_hook.invoke<bool>(client_num);
			other_flags |= 0x4000;

			return result;
		}
	}
}

namespace // command execution
{
	bool execute_playback_command_internal(std::filesystem::path& file_path, bool repeat, bool print_error)
	{
		if (demo.start(file_path, repeat))
		{
			return true;
		}

		if (print_error)
		{
			console::error("could not play demo %s\n", file_path.string().c_str());
		}

		return false;
	}

	bool execute_playback_command_internal(std::string_view file_name, bool repeat)
	{
		const auto fs_basepath = get_base_path(false);
		if (fs_basepath.empty())
		{
			console::error("could not find fs_basepath\n");
			return false;
		}

		const auto mod = get_mod_directory();

		auto file_path1 = std::filesystem::path(fs_basepath) / std::format("demos/client/{2}/user/{0}{1}", file_name, DEMO_EXTENSION, mod);
		if (execute_playback_command_internal(file_path1, repeat, false))
		{
			return true;
		}

		auto file_path2 = std::filesystem::path(fs_basepath) / std::format("demos/client/{2}/auto/{0}{1}", file_name, DEMO_EXTENSION, mod);
		if (execute_playback_command_internal(file_path2, repeat, false))
		{
			return true;
		}

		auto file_path3 = std::filesystem::path(fs_basepath) / std::format("demos/client/{0}{1}", file_name, DEMO_EXTENSION);
		if (execute_playback_command_internal(file_path3, repeat, false))
		{
			return true;
		}

		auto file_path4 = std::filesystem::path(fs_basepath) / std::format("demos/server/{2}/{0}{1}", file_name, DEMO_EXTENSION, mod);
		if (execute_playback_command_internal(file_path4, repeat, false))
		{
			return true;
		}

		auto file_path5 = std::filesystem::path(fs_basepath) / std::format("demos/server/{0}{1}", file_name, DEMO_EXTENSION);
		if (execute_playback_command_internal(file_path5, repeat, false))
		{
			return true;
		}

		auto file_path6 = std::filesystem::path(fs_basepath) / std::format("demos/{0}{1}", file_name, DEMO_EXTENSION);
		if (execute_playback_command_internal(file_path6, repeat, false))
		{
			return true;
		}

		console::error("could not play demo %s\n", file_path1.string().c_str());
		console::error("could not play demo %s\n", file_path2.string().c_str());
		console::error("could not play demo %s\n", file_path3.string().c_str());
		console::error("could not play demo %s\n", file_path4.string().c_str());
		console::error("could not play demo %s\n", file_path5.string().c_str());
		console::error("could not play demo %s\n", file_path6.string().c_str());

		return false;
	}

	template <bool force_playback>
	bool execute_playback_command(const command::params& params)
	{
		struct args_t
		{
			bool fullpath{};
			bool repeat{};
		};

		auto parsed_arguments = [&params]() -> std::optional<args_t>
		{
			if (params.size() == 3)
			{
				if (std::string_view(params.get(2)) == "fullpath")
				{
					return args_t{ true, false };
				}
				else if (std::string_view(params.get(2)) == "repeat")
				{
					return args_t{ false, true };
				}
				else
				{
					return {};
				}
			}
			else if (params.size() == 4)
			{
				if (std::string_view(params.get(2)) == "fullpath" && std::string_view(params.get(3)) == "repeat")
				{
					return args_t{ true, true };
				}
				else
				{
					return {};
				}
			}
			else
			{
				return args_t{ false, false };
			}
		}();

		if (params.size() < 2 || params.size() > 4 || !parsed_arguments)
		{
			console::info("usage: demoplay <file_name>, fullpath(optional), repeat(optional)\n");
			return false;
		}

		if constexpr (!force_playback)
		{
			if (!demo.is_demo_playing() && game::CL_IsCgameInitialized())
			{
				console::warn("could not play demo; please disconnect first\n");
				return false;
			}
		}

		bool started{};
		if (!parsed_arguments->fullpath)
		{
			started = execute_playback_command_internal(params.get(1), parsed_arguments->repeat);
		}
		else
		{
			auto file_path = std::filesystem::path(params.get(1));
			started = execute_playback_command_internal(file_path, parsed_arguments->repeat, true);
		}

		if constexpr (!force_playback)
		{
			// demoplay: drive the map load ourselves (bootstrap), because at the menu
			// CL_SetCGameTime never ticks to run process_map_header. instant-play skips this.
			if (started)
			{
				demo.bootstrap_map_load();
			}
		}

		return started;
	}

	void execute_playback_instant_play_command(const command::params& params)
	{
		// the demo recording code (demo_recording_t::shutdown_watcher) should take care of properly closing any recording demo files!

		const auto demo_playing = demo.is_demo_playing();
		if (execute_playback_command<true>(params))
		{
			// instant play reuses the currently loaded map -> do NOT let parse_demo run
			// party::start_map on the demo's map_header chunk (spawning a second local
			// server while a match is running crashes in SV_SpawnServer @0x554932).
			demo.set_skip_map_preload(true);

			// NOTE(h1): the iw6 original force-cleared the sv_running dvar here. On MWR that
			// DESYNCS the server bookkeeping (the local server is still really running) and the
			// subsequent /disconnect -> menu/vlobby load crashes in SV_SpawnServer @0x554932 on
			// the dirty svs.clients array. Leaving sv_running alone; the demo cannot cleanly
			// coexist with a live local server yet, but at least disconnect no longer crashes.
			// TODO(h1): to truly take over from a live local match, shut the server down via the
			// game's own clean path instead of lying about the dvar.

			if (!demo_playing)
			{
				if (auto& connstate = *game::mp::connstate; connstate == game::CA_ACTIVE)
				{
					// prevent old snapshots from being processed,
					// would mess up the server command sequences and server times
					connstate = game::CA_PRIMED;
				}
			}
		}
	}

	void execute_playback_replay_command(const command::params& params)
	{
		if (params.size() != 1)
		{
			console::info("usage: demoreplay\n");
			return;
		}

		if (const auto path = demo.restart(); path)
		{
			console::error("could not play demo %s\n", path->get().string().c_str());
		}
	}

	void execute_playback_rewind_command(const command::params& params)
	{
		if (params.size() < 1 || params.size() > 2)
		{
			console::info("usage: demorewind <milliseconds>(optional)\n");
			return;
		}

		if (params.size() == 2)
		{
			const auto msec = std::atoi(params.get(1));
			if (msec <= 0)
			{
				console::error("demorewind invalid value %s\n", params.get(1));
				return;
			}

			if (!demo.rewind_demo(msec))
			{
				console::error("demo must be playing to rewind\n");
			}
		}
		else if (!demo.rewind_demo(std::nullopt))
		{
			console::error("demo must be playing to rewind\n");
		}
	}

	void execute_playback_fast_forward_command(const command::params& params)
	{
		if (params.size() != 2)
		{
			console::info("usage: demoforward <milliseconds>\n");
			return;
		}

		const auto msec = std::atoi(params.get(1));
		if (msec <= 0)
		{
			console::error("demoforward invalid value %s\n", params.get(1));
			return;
		}

		if (demo.is_demo_playing())
		{
			fast_forward_demo(static_cast<std::uint32_t>(msec));
		}
		else
		{
			console::error("demo must be playing to fast forward\n");
		}
	}
}

namespace demo_playback
{
	bool playing()
	{
		return demo.is_demo_playing();
	}

	bool is_replay_armed()
	{
		return demo.is_replay_armed();
	}

	bool startplay(std::string_view file_name, bool repeat)
	{
		return execute_playback_command_internal(file_name, repeat);
	}

	bool startplay(std::filesystem::path& file_name, bool repeat)
	{
		return execute_playback_command_internal(file_name, repeat, true);
	}

	void stopplay()
	{
		demo.stop();
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			if (!game::environment::is_mp())
			{
				return;
			}

			// check run-time address assertions
			check_address_assertions();

			// primary hook for demo playback; read demo data after server time has been updated
			CL_SetCGameTime_hook.create(game::CL_SetCGameTime, handle_demo_reading);

			// don't write packets when playing a demo
			CL_WritePacket_hook.create(game::CL_WritePacket, write_packet);

			// force the engine's own demoType==CLIENT path during playback -- this is what
			// actually suppresses local input (movement/fire/ADS), not a hand-written cg field.
			CG_DrawActiveFrame_hook.create(game::CG_DrawActiveFrame, draw_active_frame_stub);

			// ===== STAGE 2a: real playback-support hooks. All verified pass-through when
			// no demo is playing, so they install on valid addresses and stay inert until
			// demoplay. The Vehicle/VehicleCam/GetRemoteEye hooks are DELETED — MWR has no
			// vehicles and they targeted our own inline stubs (invalid to detour). =====

			// replace predicted-player getter so playback restores viewangles
			CL_GetPredictedPlayerInformationForServerTime_hook.create(
				game::CL_GetPredictedPlayerInformationForServerTime, get_predicted_player_data);

			// optional skip of intermission menu (only acts when a repeating demo is playing)
			LUI_IntermissionBegan_hook.create(game::LUI_IntermissionBegan, show_intermission_menu);

			// suppress team/loadout menus during playback (only acts when a demo is playing)
			CG_UpdateOmnvars_hook.create(game::CG_UpdateOmnvars, control_ui_element);

			// allow user to override visual settings
			// TODO(h1): re-enable with the h1 CG_VisionSetStartLerp address (iw6 0x1402A3200 is INVALID on h1).
			// CG_VisionSetStartLerp_To_hook.create(<h1 addr>, handle_vision_override);
			demo_vision = game::demo_compat::register_enum(
				"demovision", const_cast<const char**>(VISION_SETS.data()), 0, game::DVAR_FLAG_NONE, "Override vision set for demos");

			// update the gamestate config strings with 'live' (recorded) data from the demo, at the
			// exact point CL_ParseGamestate reads the server-command-sequence baseline (call site
			// verified via IDA: sub_342770 @0x3427DD calls the real MSG_ReadLong 0x4EB7D0).
			utils::hook::call(0x3427DD_b, update_gamestate_config_strings);

#if 0 // iw6 absolute-address patches — INVALID on h1 (MWR). demoType solved differently (CG_DrawActiveFrame arg
      // override); cg.bgs-TLS patch unneeded/unverified.
			// don't let the game set cg_t->demoType in CG_DrawActiveFrame,
			// otherwise demo playback will have mouse input and suffer from frame interpolation issues
			utils::hook::nop(0x14029981C, 6);

			// replace thread local storage lookup for cg.bgs in function CG_EmissiveBlendCommand with hardcoded address
			static constexpr auto patch_address = 0x14028844B;
			static constexpr std::array<std::uint8_t, 7> patch{ 0x48, 0x8D, 0x1D, 0x68, 0xBC, 0x59, 0x01 };

			utils::hook::nop(patch_address, 0xE);
			utils::hook::nop(patch_address + 0x1C, 4);
			utils::hook::nop(patch_address + 0x22, 4);
			utils::hook::copy(patch_address + 0xE, patch.data(), patch.size());
#endif

			// add console command support to play demos
			command::add("demoplay", execute_playback_command<false>);

			// add experimental command console support to play demos,
			// skip disconnecting from local or online server and skip map (re)loading
			command::add("demoinstantplay", execute_playback_instant_play_command);

			// replay most recent demo (if any)
			command::add("demoreplay", execute_playback_replay_command);

			// rewind demo (by N msec)
			command::add("demorewind", execute_playback_rewind_command);

			// fast forward demo by N msec
			command::add("demoforward", execute_playback_fast_forward_command);

			// add command line support to play demos; this makes it possible to double click on a demo file to play it
			const auto& args = utils::command_line::get_args();
			for (const auto& arg : args)
			{
				if (arg.extension() == DEMO_EXTENSION)
				{
					scheduler::on_game_initialized([cmd = std::format(R"(demoplay "{}" fullpath repeat)", arg.string())]() mutable
					{
						command::execute(std::move(cmd), true);
					}, scheduler::main);

					console::info("the demo should start to load in a few seconds\n");
					break;
				}
			}
		}
	};
}

REGISTER_COMPONENT(demo_playback::component)
