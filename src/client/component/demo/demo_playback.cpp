#include "std_include.hpp"
#include "loader/component_loader.hpp"

#include "demo_playback.hpp"
#include "demo_utils.hpp"
#include "cod4_camera.hpp"
#include "cod4_model_metadata.hpp"
#include "cod4_obituary_events.hpp"

#include "component/command.hpp"
#include "component/console.hpp"
#include "component/party.hpp"
#include "component/scheduler.hpp"
#include "game/dvars.hpp"
#include "utils/command_line.hpp"
#include "utils/hook.hpp"


using namespace demo_utils;

namespace // demo class
{
    std::ofstream converted_trace;
    std::uint64_t converted_draws{};
    std::uint64_t converted_packets_blocked{};
    std::chrono::steady_clock::time_point converted_trace_time{};
    game::dvar_t* cod4_playback_hud{};

    void trace_converted(const char* stage)
    {
        if (!converted_trace.is_open()) return;
        const auto* cl = game::mp::cl.get();
        converted_trace << stage << " state=" << static_cast<int>(*game::mp::connstate)
            << " server=" << game::SV_Loaded() << " lobby=" << game::VirtualLobby_Loaded()
            << " cgame=" << game::CL_IsCgameInitialized()
            << " onlineFlags=" << game::Live_SyncOnlineDataFlags(0)
            << " time=" << (cl ? cl->serverTime : -1)
            << " snap=" << (cl ? cl->snap.serverTime : -1)
            << " draws=" << converted_draws << " blockedPackets=" << converted_packets_blocked
            << std::endl;
    }

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
		[[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> peek_duration();
		void bootstrap_map_load();
        bool is_converted() const { return converted_; }
        bool has_legacy_obituaries() const { return legacy_obituaries_; }
        const std::map<unsigned, int>& grenade_times() const { return grenade_times_; }
        const std::map<unsigned, std::string>& fx_bindings() const { return fx_bindings_; }
        void reset_grenade_fx();
        bool claim_bullet_event(unsigned number) { return fired_bullet_events_.insert(number).second; }
        bool rebuilding_grenade_fx() const { return rebuilding_grenade_fx_; }
        void finish_grenade_fx_rebuild() { if (!seeking_) rebuilding_grenade_fx_ = false; }
        bool is_bootstrapping() const { return bootstrapping_; }
        bool converted_models_ready() const
        {
            // Converted playback bypasses the normal frontend map-preload
            // transition. Advance the native transient queue across batches.
            utils::hook::invoke<void>(0x757A0_b);
            for (const auto& name : converted_models_)
                if (!utils::hook::invoke<bool>(0x75470_b, name.c_str())) return false;
            return true;
        }
		[[nodiscard]] std::optional<std::reference_wrapper<std::filesystem::path>> restart();
		template <typename Func, typename... Args> auto use_predicted_data(Func func, Args&&... args);
		template <typename Func, typename... Args> auto handle_fast_forward(Func func, Args&&... args);

		[[nodiscard]] bool is_paused() const { return paused_; }
		void set_paused(bool value) { paused_ = value; }
		void toggle_paused()
        {
            if (ended_) { (void)rewind_demo(std::nullopt); paused_ = false; }
            else paused_ = !paused_;
        }

		// A user scrub/seek (demoforward/demorewind) must still complete while paused, so mark
		// one in progress; handle_demo_reading then keeps parsing until it settles at the new
		// spot and re-freezes. seek_ttl_ gives the fast-forward machinery a few frames to apply.
		void begin_seek() { reset_grenade_fx(); seeking_ = true; seek_ttl_ = 4; }
		[[nodiscard]] bool is_seeking() const { return seeking_; }
		void advance_seek();
		[[nodiscard]] std::optional<std::int32_t> get_current_time() const;
		[[nodiscard]] const std::optional<std::pair<std::int32_t, std::int32_t>>& get_time_bounds() const { return time_bounds_; }
		[[nodiscard]] const std::filesystem::path& get_path() const { return path_; }

		void parse_demo_wrapper();
		void stop();

	private:
		void repeat();

		bool converted_{}, bootstrapping_{}, legacy_obituaries_{};
        std::set<std::string> converted_models_;
        std::map<unsigned, int> grenade_times_;
        std::map<unsigned, std::string> fx_bindings_;
        std::set<unsigned> fired_bullet_events_;
        bool rebuilding_grenade_fx_{};
        bool repeat_{};
		bool skip_timeout_{};
		bool retain_timescale_{};
		bool skip_map_preload_{}; // demoinstantplay: reuse current live map, don't start_map
		bool replay_armed_{};     // becomes true once the client reaches CA_ACTIVE (handshake done)
		bool gamestate_applied_{}; // becomes true once the recorded gamestate has been applied once
		bool ended_{};
        bool rewind_pending_{};
		bool paused_{};           // freezes cl->serverTime + stops feeding new chunks (see handle_demo_reading)
		bool seeking_{};          // a scrub/seek is settling (keep parsing even while paused)
		int seek_ttl_{};          // min frames to let fast_forward_demo apply before a seek can settle
		std::size_t post_map_header_file_offset_{};
		std::size_t pred_data_index_{};
		std::array<predicted_data_t, 256> pred_data_{};
		std::optional<std::int32_t> rewind_snap_svr_time_;
		std::optional<std::pair<std::int32_t, std::int32_t>> time_bounds_; // {first_svr_time, last_svr_time}, from gen_footer
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

	std::optional<std::int32_t> demo_playback_t::get_current_time() const
	{
		if (!is_demo_playing() || !replay_armed_ || !game::mp::cl.get())
		{
			return std::nullopt;
		}

		return game::mp::cl->snap.serverTime;
	}

	void demo_playback_t::advance_seek()
	{
		// give the fast-forward machinery a few frames to bump serverTimeDelta + have it reflected
		// in serverTime, and (for a rewind) to consume its target, before we call the seek settled.
		if (seek_ttl_ > 0)
		{
			--seek_ttl_;
			return;
		}
		const auto* cl = game::mp::cl.get();
		if (cl && !rewind_snap_svr_time_.has_value() && cl->snap.serverTime >= cl->serverTime)
		{
			seeking_ = false; // freshly-read snapshots caught up to the sought clock -> re-freeze
		}
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
		ended_ = false;
		rewind_pending_ = true;
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

		game::Dvar_SetFromStringByNameFromSource("developer", "1", game::DVAR_SOURCE_INTERNAL);

		file_ = std::move(file);
		assert(file_.good() && file_.is_open());

		// (re)set data
		repeat_ = repeat;
		paused_ = false;
		gs_.reset();
		path_ = std::move(path);
		get_persistent_data() = {};

		// cache the demo's [first_svr_time, last_svr_time] (from its gen_footer chunk, written
		// when the original recording stopped) once, up front, so a timeline UI can show total
		// duration without re-scanning the file every frame. Absent on a demo that never got a
		// clean gen_footer (e.g. the recorder crashed mid-write); callers must handle nullopt.
		time_bounds_ = peek_duration();

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

	// Scan the demo file for its gen_footer chunk ({first_svr_time, last_svr_time}, written once
	// when the original recording stopped) so a timeline UI can show total duration up front,
	// without decoding/decompressing any network_data payloads. Restores the file position
	// afterward (safe to call at any time, not just before playback starts).
	std::optional<std::pair<std::int32_t, std::int32_t>> demo_playback_t::peek_duration()
	{
		if (!file_.good() || !file_.is_open())
		{
			return std::nullopt;
		}

		const auto restore_pos = file_.tellg();
		file_.clear();
		file_.seekg(0, std::ios::beg);

		std::optional<std::pair<std::int32_t, std::int32_t>> result;

		for (int guard = 0; guard < 1'000'000 && !result; ++guard)
		{
			std::uint8_t id_byte = 0;
			file_.read(reinterpret_cast<char*>(&id_byte), 1);
			if (!file_.good())
			{
				break;
			}

			const bool one_byte = (id_byte & 0x80) != 0; // one_byte_flag
			const int id = id_byte & 0x1F;               // strip flag bits (0xE0)

			if (id == static_cast<int>(demo_data_id::eof))
			{
				break;
			}

			std::size_t size = 0;
			file_.read(reinterpret_cast<char*>(&size), one_byte ? 1 : 4);
			if (!file_.good() || size > MAX_SIZE)
			{
				break;
			}

			if (id == static_cast<int>(demo_data_id::gen_footer) && size >= 8)
			{
				std::int32_t first_time = 0;
				std::int32_t last_time = 0;
				file_.read(reinterpret_cast<char*>(&first_time), 4);
				file_.read(reinterpret_cast<char*>(&last_time), 4);
				if (file_.good())
				{
					result = std::make_pair(first_time, last_time);
				}
				break;
			}

			file_.seekg(static_cast<std::streamoff>(size), std::ios::cur);
		}

		file_.clear();
		file_.seekg(restore_pos);

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
        // Converted files contain a complete native gamestate packet. Bootstrap
        // that client connection directly; no loopback server or match is needed.
        std::ifstream input(path_, std::ios::binary);
        std::vector<std::uint8_t> initial;
        std::vector<std::string> model_names;
        std::map<unsigned, std::string> model_bindings;
        std::map<unsigned, std::string> animation_bindings;
        for (int guard = 0; guard < 8 && input && initial.empty(); ++guard)
        {
            std::uint8_t id{}; std::uint32_t size{};
            input.read(reinterpret_cast<char*>(&id), 1);
            input.read(reinterpret_cast<char*>(&size), (id & 128) ? 1 : 4);
            if (!input || size > MAX_SIZE) break;
            std::vector<std::uint8_t> data(size);
            input.read(reinterpret_cast<char*>(data.data()), size);
            if (!input) break;
            if ((id & 31) == 5)
            {
                std::vector<std::string> fields;
                for (std::size_t offset = 0; offset < data.size();)
                {
                    const auto end = std::find(data.begin() + offset, data.end(), 0);
                    if (end == data.end()) break;
                    fields.emplace_back(data.begin() + offset, end);
                    offset = static_cast<std::size_t>(end - data.begin()) + 1;
                }
                converted_ = fields.size() >= 9 && (fields[1] == "H1 COD4 conversion v1"
                    || fields[1] == "H1 COD4 conversion v2" || fields[1] == "H1 COD4 conversion v3");
                legacy_obituaries_ = converted_ && fields[1] == "H1 COD4 conversion v2";
                if (cod4_playback_hud) cod4_playback_hud->current.enabled = converted_;
                if (converted_)
                {
                    const auto models = cod4::parse_model_metadata({fields.begin() + 9, fields.end()});
                    model_names.assign(models.names.begin(), models.names.end());
                    model_bindings = models.bindings;
                    animation_bindings = models.animation_bindings;
                    grenade_times_ = models.grenade_times;
                    grenade_times_.insert(models.car_fx_times.begin(), models.car_fx_times.end());
                    grenade_times_.insert(models.bullet_times.begin(), models.bullet_times.end());
                    fx_bindings_ = models.fx_bindings;
                }
            }
            if ((id & 31) == 2 && converted_ && data.size() > 8)
            {
                if ((data[7] & 128) == 0) throw std::runtime_error("Converted gamestate must use zlib");
                initial = utils::compression::zlib::decompress(std::span(data).subspan(8), MAX_SIZE);
                post_map_header_file_offset_ = static_cast<std::size_t>(input.tellg());
            }
        }
        if (!converted_)
        {
            party::start_map(*mapname);
            return;
        }
        if (initial.empty() || initial.size() > MAX_SIZE || (initial[0] & 7) != 0)
            throw std::runtime_error("Converted demo has no valid native gamestate");
        bootstrapping_ = true;
        const auto clear_bootstrap = gsl::finally([this] { bootstrapping_ = false; });
        converted_trace.close();
        converted_trace.open("demos/converted-playback-last.log", std::ios::trunc);
        converted_draws = converted_packets_blocked = 0;
        converted_trace_time = {};
        trace_converted("bootstrap begin");
        cod4_camera::set_model_bindings(model_bindings, animation_bindings);
        cod4_camera::bootstrap_demo(*mapname, initial);
        trace_converted("bootstrap complete");
        for (const auto& name : model_names)
        {
            if (name.empty() || name.size() > 128) throw std::runtime_error("Invalid converted demo model name");
            if (converted_models_.insert(name).second) utils::hook::invoke<void>(0x75720_b, name.c_str());
        }
        file_.clear();
        file_.seekg(post_map_header_file_offset_, std::ios::beg);
        console::info("Converted .dm_h1 opened through native demo player: server=%d\n", game::SV_Loaded());
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
        if (ended_) return;
        bool clean_end{};
		if (!parse_demo(file_, buffer_, post_map_header_file_offset_, pred_data_, pred_data_index_, gs_, skip_map_preload_, clean_end))
		{
			if ((is_demo_repeating() || rewind_pending_) && *game::mp::connstate != game::CA_DISCONNECTED)
			{ // don't repeat demo if it was stopped manually
				repeat();
			}
			else if (clean_end && replay_armed_ && *game::mp::connstate == game::CA_ACTIVE && game::mp::cl.get())
            {
                ended_ = paused_ = true;
                seeking_ = false;
                auto* cl = game::mp::cl.get();
                cl->serverTimeDelta -= cl->serverTime - cl->snap.serverTime;
                cl->serverTime = cl->snap.serverTime;
                console::info("Demo finished. Holding the last frame; rewind or resume to replay.\n");
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
		ended_ = false;
        rewind_pending_ = false;
		skip_timeout_ = {};
		retain_timescale_ = {};
		skip_map_preload_ = {};
		replay_armed_ = {};
		gamestate_applied_ = {};
		paused_ = {};
		seeking_ = {};
		seek_ttl_ = {};
		post_map_header_file_offset_ = {};
		pred_data_index_ = {};
		pred_data_.fill({});
		rewind_snap_svr_time_ = {};
		time_bounds_.reset();
		gs_.reset();
		buffer_.clear();
		file_.clear();
        file_.close();
        for (const auto& name : converted_models_) utils::hook::invoke<void>(0x76850_b, name.c_str());
        converted_models_.clear();
        grenade_times_.clear();
        fired_bullet_events_.clear();
        fx_bindings_.clear();
        rebuilding_grenade_fx_ = false;
        if (converted_) cod4_camera::clear_model_bindings();
        converted_ = false;
        legacy_obituaries_ = false;
        if (cod4_playback_hud) cod4_playback_hud->current.enabled = false;

		// ensure that sv_cheats is always disabled after demo playback
		if (auto* sv_cheats = game::Dvar_FindVar("sv_cheats"); sv_cheats)
		{
			sv_cheats->current.enabled = false;
		}
	}

    void demo_playback_t::reset_grenade_fx()
    {
        // Converted gun-only recordings also own native flashes and smoke.
        // Clear them on seek/restart even without grenade/car timing metadata.
        if (!converted_) return;
        rebuilding_grenade_fx_ = true;
        fired_bullet_events_.clear();
        if (*game::mp::connstate != game::CA_ACTIVE) return;
        // Native FX_KillAllEffects takes the full int local-client argument,
        // acquires the system write lock and clears entity FX references.
        utils::hook::invoke<void>(0x3DBA80_b, 0);
        for (const auto& [number, time] : grenade_times_)
        {
            auto* entity = utils::hook::invoke<std::uint8_t*>(0x6BFF0_b, 0, static_cast<int>(number));
            if (entity) *reinterpret_cast<int*>(entity + 0x248) = 0;
        }
    }

	void demo_playback_t::repeat()
	{
        reset_grenade_fx();
        rewind_pending_ = false;
		assert(post_map_header_file_offset_ > 0);

		if (auto& connstate = *game::mp::connstate; connstate == game::CA_ACTIVE)
		{
			// prevent old snapshots from being processed; would mess up the server command sequences
			connstate = game::CA_PRIMED;
		}

        // CG_ProcessSnapshots (0x32F750) and CG_ReadNextSnapshot (0x32F950)
        // own a second set of sequence counters, separate from the client clock.
        // Discard both snapshot pointers so native first-snapshot initialization
        // rebuilds interpolation/animation state instead of comparing to the future.
        if (auto* cg = reinterpret_cast<std::uint8_t*>(game::CG_GetLocalClientGlobals()))
        {
            *reinterpret_cast<int*>(cg + 0x48E4) = 0; // latestSnapshotNum
            *reinterpret_cast<int*>(cg + 0x48E8) = 0; // latestSnapshotTime
            *reinterpret_cast<void**>(cg + 0x48F0) = nullptr; // snap
            *reinterpret_cast<void**>(cg + 0x48F8) = nullptr; // nextSnap
        }
        if (auto* cgs = reinterpret_cast<std::uint8_t*>(game::CG_GetServerCommandsState()))
        {
            *reinterpret_cast<int*>(cgs + 0x18) = 0; // serverCommandSequence
            *reinterpret_cast<int*>(cgs + 0x1C) = 0; // processedSnapshotNum
        }
        pred_data_.fill({});

		// reset game data
		game::mp::clc->serverCommandSequence = {};
		game::mp::clc->lastExecutedServerCommand = {};
		game::mp::cgs->serverCommandSequence = {};
		get_persistent_data() = {};

		// NOTE(h1): continue_demo_reading()'s gate is `cl->snap.serverTime <= cl->serverTime`.
		// Neither side of that was ever reset here, so whatever cl->serverTime happened to be
		// clamped to at the moment of restart (now that fast_forward_demo actually writes it --
		// e.g. rewind_demo's own "workaround" fast_forward_demo(1000) call right before this
		// runs) can permanently outrun cl->snap.serverTime, which itself stays at its stale
		// pre-restart value since nothing feeds a fresh snapshot to update it. Once that happens
		// the gate never reopens -- parse_demo's while-loop body never executes again (it exits
		// immediately every single frame), and the demo is stuck at the restart point forever;
		// pausing/unpausing doesn't help because the same permanently-closed gate applies either
		// way. Reset both sides to a consistent, ungated state (matching what a truly fresh
		// bootstrap session already has for free) so feeding resumes immediately.
		if (auto* cl = game::mp::cl.get(); cl)
		{
			cl->serverTime = 0;
			cl->oldFrameServerTime = 0;
			cl->snap.serverTime = 0;
			cl->snap.valid = 0;
            // CL_GetSnapshot (0x33B940) trusts the ring entry's valid flag;
            // it does not compare the requested sequence to the entry's sequence.
            // Old ring slots otherwise supply future snapshots during replay startup.
            auto* raw = reinterpret_cast<std::uint8_t*>(cl);
            const auto count = *reinterpret_cast<int*>(raw + 0x8F84);
            auto* snapshots = *reinterpret_cast<std::uint8_t**>(raw + 0x8FA0);
            if (snapshots && count > 0 && count <= 4096)
                for (int i = 0; i < count; ++i)
                    *reinterpret_cast<int*>(snapshots + static_cast<std::size_t>(i) * 0x4A78 + 0x4A24) = 0;

		}

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
	utils::hook::detour CG_CalcViewValues_hook;
    utils::hook::detour CG_EntityEvent_hook;
    utils::hook::detour BG_PlayerAnimation_hook;
    utils::hook::detour CG_PlaySound_hook;
    thread_local bool suppress_grenade_sound{};
    utils::hook::detour FX_PlayOrientedEffectWithMarkEntity_hook;
    utils::hook::detour CG_ShouldCullImpactFX_hook;
    thread_local unsigned bullet_effect_entity{};
    thread_local bool unbound_flesh_impact{};

    bool bullet_should_cull_impact(const int local_client_num, const float* origin,
        const float* normal, const unsigned short target)
    {
        if (!unbound_flesh_impact || target != 2046 || !origin || !normal)
            return CG_ShouldCullImpactFX_hook.invoke<bool>(local_client_num, origin, normal, target);
        // COD4 records flesh but not its victim identity. H1's world branch
        // backface-culls oriented wall marks; its player branch skips that
        // angle test for volumetric blood. Use the same native PVS test as
        // the player branch without inventing a victim or changing a dvar.
        const auto* enabled = *reinterpret_cast<game::dvar_t**>(0x390BA98_b);
        bool culled{};
        if (enabled && enabled->current.enabled)
        {
            const std::array<float, 3> point{origin[0] + normal[0], origin[1] + normal[1], origin[2] + normal[2]};
            const auto leaf = utils::hook::invoke<int>(0x65B970_b, point.data());
            const auto* cg = reinterpret_cast<const std::uint8_t*>(game::CG_GetLocalClientGlobals());
            int view_leaf = -1;
            if (cg) std::memcpy(&view_leaf, cg + 0x177750, sizeof(view_leaf));
            culled = leaf >= 0 && view_leaf >= 0 && !utils::hook::invoke<int>(0x6602B0_b, leaf, view_leaf);
        }
        if (converted_trace.is_open())
            converted_trace << "native bullet visibility entity=" << bullet_effect_entity
                << " culled=" << culled << " unbound_flesh=1" << std::endl;
        return culled;
    }

    game::FxEffect* bullet_play_oriented_effect(const int local_client_num, const game::FxEffectDef* effect,
        const int time, const float* origin, const float* axis, const unsigned short mark_entity, const bool viewmodel)
    {
        // Passive evidence of the engine's selected blood asset and allocation.
        // The event handler still performs all effect selection and spawning.
        auto* result = FX_PlayOrientedEffectWithMarkEntity_hook.invoke<game::FxEffect*>(local_client_num,
            effect, time, origin, axis, mark_entity, viewmodel);
        if (bullet_effect_entity && converted_trace.is_open() && effect && origin)
            converted_trace << "native bullet FX entity=" << bullet_effect_entity << " effect=" << effect->name
                << " time=" << time << " spawned=" << (result != nullptr)
                << " origin=" << origin[0] << ',' << origin[1] << ',' << origin[2] << std::endl;
        return result;
    }

    int grenade_play_sound(const int local_client_num, const int entity_num, const float* origin, const void* alias)
    {
        // The native wrapper at 0x31D040 returns the int sound ID from
        // 0x31D430 (-1 for no sound). Only silence a converted burst being
        // reconstructed by a seek; ordinary gameplay and demo audio pass through.
        if (suppress_grenade_sound) return -1;
        return CG_PlaySound_hook.invoke<int>(local_client_num, entity_num, origin, alias);
    }

    void player_animation(const int local_client, std::uint8_t* entity, std::uint8_t* character,
        const float* angles, const float* origin, const int time, const int torso, const int legs,
        const bool update_time)
    {
        // Older conversions preserved sparse COD4 client numbers, but H1
        // only allocates player trees for slots 0..17 (0x31C890). Reject
        // those files before either sequence selection or XAnim playback
        // dereferences an uninitialized tree. Fresh conversions compact all
        // player identities and their references into native player slots.
        if (demo.is_converted() && character && entity
            && (entity[10] == 1 || entity[10] == 2) && entity[12] >= 18)
        {
            game::Com_Error(game::ERR_DROP,
                "This converted demo uses unsupported player slots. Reconvert the original COD4 demo with this build.");
            return;
        }
        BG_PlayerAnimation_hook.invoke<void>(local_client, entity, character, angles, origin,
            time, torso, legs, update_time);
    }

    void grenade_entity_event(const int local_client_num, std::uint8_t* entity, const unsigned event, const unsigned flags)
    {
        if (demo.is_converted() && demo.has_legacy_obituaries() && entity && event == 140 && entity[0x146] == 160)
        {
            // Dispatch the repaired v2 payload straight to native CG_Obituary;
            // the generic event preamble treats its old MOD byte as a weapon.
            cod4::h1_entity corrected{};
            std::memcpy(corrected.data(), entity + 0x13C, corrected.size());
            cod4::repair_legacy_obituary(corrected);
            unsigned parameter{};
            std::memcpy(&parameter, corrected.data() + 36, sizeof(parameter));
            utils::hook::invoke<void>(0x10B750_b, local_client_num, corrected.data(), parameter);
            if (converted_trace.is_open())
                converted_trace << "legacy obituary repaired parameter=" << parameter << std::endl;
            return;
        }
        // Recorded local PS and remote entity gun rings use their respective
        // native firing paths. Do not turn skipped shots into flashes while
        // scrubbing. Ordinary H1 demos and gameplay keep the native behavior.
        if (demo.is_converted() && (event == 42 || event == 43
            || ((flags & 1u) ? (event == 51 || event == 52) : (event == 39 || event == 40))))
        {
            if (demo.is_seeking() || demo.rebuilding_grenade_fx()) return;
            CG_EntityEvent_hook.invoke<void>(local_client_num, entity, event, flags);
            if (converted_trace.is_open())
            {
                const auto* cg = reinterpret_cast<const std::uint8_t*>(game::CG_GetLocalClientGlobals());
                converted_trace << "native gun event=" << event << " flags=" << flags
                    << " time=" << (cg ? *reinterpret_cast<const int*>(cg + 0x92BE8) : -1) << std::endl;
            }
            return;
        }
        const auto& times = demo.grenade_times();
        unsigned short number{};
        if (entity) std::memcpy(&number, entity + 0x13C, sizeof(number));
        const auto found = times.find(number);
        if (!demo.is_converted() || found == times.end() || !entity
            || entity[0x146] != 20 + event || (event != 98 && event != 103 && event != 104 && event != 111
                && event != 77 && event != 80 && event != 81))
        {
            CG_EntityEvent_hook.invoke<void>(local_client_num, entity, event, flags);
            return;
        }
        // Native centity event history can reset during a player respawn.
        // Stable converted impact identities fire once until seek/restart
        // explicitly clears both the effect system and this dispatch history.
        if ((event == 77 || event == 80 || event == 81) && !demo.claim_bullet_event(number)) return;
        // CG_EntityEvent supplies cg.time to native FX_PlayEffect. Use the
        // original event clock, so skipped snapshots and seeks create
        // the MWR burst or impact with its recorded age.
        auto* cg = reinterpret_cast<std::uint8_t*>(game::CG_GetLocalClientGlobals());
        if (!cg) return;
        auto& time = *reinterpret_cast<int*>(cg + 0x92BE8);
        game::FxEffectDef** effect_slot{};
        game::FxEffectDef* saved_effect{};
        if (event == 111)
        {
            unsigned index{};
            std::memcpy(&index, entity + 0x160, sizeof(index));
            const auto binding = demo.fx_bindings().find(index);
            if (binding == demo.fx_bindings().end() || !index || index >= 512
                || !game::DB_XAssetExists(game::ASSET_TYPE_FX, binding->second.c_str())
                || game::DB_IsXAssetDefault(game::ASSET_TYPE_FX, binding->second.c_str()))
            {
                game::Com_Error(game::ERR_DROP, "Converted car FX native asset unavailable");
                return;
            }
            const auto asset = game::DB_FindXAssetHeader(game::ASSET_TYPE_FX, binding->second.c_str(), false).fx;
            if (!asset)
            {
                game::Com_Error(game::ERR_DROP, "Converted car FX native definition unavailable");
                return;
            }
            effect_slot = reinterpret_cast<game::FxEffectDef**>(0x3909D00_b) + index;
            saved_effect = *effect_slot;
            *effect_slot = asset;
        }
        const auto saved = time;
        const auto saved_sound = suppress_grenade_sound;
        const auto saved_bullet = bullet_effect_entity;
        const auto saved_flesh = unbound_flesh_impact;
        bullet_effect_entity = event == 77 || event == 80 || event == 81 ? number : 0;
        unsigned short target{};
        std::memcpy(&target, entity + 0x140, sizeof(target));
        unbound_flesh_impact = event == 77 && entity[0x147] == 7 && target == 2046;
        suppress_grenade_sound = demo.rebuilding_grenade_fx();
        time = found->second;
        const auto restore = gsl::finally([&time, saved, saved_sound, saved_bullet, saved_flesh, effect_slot, saved_effect]
        {
            time = saved;
            suppress_grenade_sound = saved_sound;
            bullet_effect_entity = saved_bullet;
            unbound_flesh_impact = saved_flesh;
            if (effect_slot) *effect_slot = saved_effect;
        });
        CG_EntityEvent_hook.invoke<void>(local_client_num, entity, event, flags);
        if (converted_trace.is_open())
            converted_trace << (event == 111 ? "native car FX event entity=" : bullet_effect_entity
                ? "native bullet impact entity=" : "native grenade event entity=") << number << " event=" << event
                << " recorded=" << found->second << " playback=" << saved
                << " age=" << static_cast<std::int64_t>(saved) - found->second
                << " reconstruct=" << suppress_grenade_sound << std::endl;
    }

	// Render time captured by draw_active_frame_stub, consumed by cg_calc_view_values_stub (which
	// gets no time argument of its own). draw_active_frame runs immediately before CG_CalcViewValues.
	int g_demo_view_time = 0;

	utils::hook::detour CL_SetCGameTime_hook;
	utils::hook::detour CL_WritePacket_hook;
	utils::hook::detour GetRemoteEyeValues_hook;
	utils::hook::detour LUI_IntermissionBegan_hook;
	utils::hook::detour VehicleCam_UpdatePlayerControlCam_hook;
	utils::hook::detour CG_ExecuteNewServerCommands_hook;


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
		// NOTE(h1): held weapon is ps+0x394 (game::cg_held_weapon), NOT the old iw6 ps.weapon@+0x144
		// which always reads 0 on MWR.
		if (const int held = game::cg_held_weapon();
			game::mp::cg->weaponSelect.data != static_cast<std::size_t>(held))
		{
			game::CG_SelectWeapon(0, static_cast<unsigned int>(held), 0);
		}
	}


	void handle_demo_reading(const std::int32_t client_num)
	{
        if (demo.is_bootstrapping()) return;
        if (demo.is_converted())
        {
            const auto now = std::chrono::steady_clock::now();
            if (now - converted_trace_time >= std::chrono::seconds(1))
            {
                trace_converted("frame");
                converted_trace_time = now;
            }
        }
        if (demo.is_converted() && !demo.converted_models_ready()) return;
		if (cod4_camera::active())
		{
			if (!cod4_camera::prepare_client_frame())
			{
				CL_SetCGameTime_hook.invoke<void>(client_num);
				cod4_camera::finish_client_frame();
			}
			return;
		}
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
			}

			const bool seeking = demo.is_seeking();

			if (demo.is_paused() && !seeking)
			{
				// Freeze playback: don't feed any further chunks, and pin cl->serverTime to the
				// latest applied snapshot each frame. Pinning serverTime alone is NOT enough:
				// CL_SetCGameTime derives serverTime = max(serverTime, serverTimeDelta +
				// com_frameTime), and com_frameTime keeps advancing in real time while paused. If
				// we only pinned serverTime, then on UNPAUSE the max() clamp would let serverTime
				// jump forward by the entire paused wall-clock duration -- a burst catch-up
				// through everything that "would have played". To prevent that, fold this frame's
				// real-time advance into serverTimeDelta: subtracting (serverTime - snap.serverTime)
				// after CL_SetCGameTime keeps serverTimeDelta == (snap.serverTime - com_frameTime),
				// so the clock stays frozen now AND resumes seamlessly (advancing by only ~one
				// frame, not the whole paused duration) the instant we unpause.
				if (auto* cl = game::mp::cl.get(); cl)
				{
					cl->serverTimeDelta -= (cl->serverTime - cl->snap.serverTime);
					cl->serverTime = cl->snap.serverTime;
				}
			}
			else
			{
				// Playing, OR paused-but-seeking: feed chunks so the clock/seek can advance. A
				// seek requested while paused MUST run here -- the freeze pin above would otherwise
				// undo fast_forward_demo's serverTimeDelta bump, so the scrub could never land.
				demo.parse_demo_wrapper();
			}

			// let an in-flight scrub settle, then it re-freezes at the new spot (if still paused)
			if (seeking)
			{
				demo.advance_seek();
			}
		}
	}

	void write_packet(const std::int32_t client_num)
	{
		if (cod4_camera::active()) return;
        // Converted playback has no peer. Suppress packets during its entire
        // lifetime, including the synchronous map load before replay is armed.
        if (demo.is_converted())
        {
            ++converted_packets_blocked;
            return;
        }
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

		// EMPTY-GUN FIX: populate the FULL playerstate from the real prediction first, so weapon/
		// health/flags/stance come from the fed snapshot ps. Without this, ps.weapon stays 0 and the
		// viewmodel setup (sub_128360 -> held weapon @ps+0x394) draws NOTHING ("no viewmodel set").
		// override ONLY the view-driving fields (origin/velocity/delta_angles/bobCycle) below.
		const bool native_found = CL_GetPredictedPlayerInformationForServerTime_hook.invoke<bool>(&cl, ps_time, &ps);

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

		// Without a matching archive, preserve the native failure result so the
		// engine can use its snapshot fallback instead of accepting stale prediction.
		return found || native_found;
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
		std::int32_t frame_time = server_time;
        if (demo.is_converted()) ++converted_draws;

		if (cod4_camera::active())
		{
			demo_type = game::mp::DEMO_TYPE_CLIENT;
			if (auto* cl = game::mp::cl.get()) frame_time = cl->serverTime;
		}

		if (demo.is_demo_playing() && demo.is_replay_armed())
		{
			demo_type = game::mp::DEMO_TYPE_CLIENT;

			// Freeze the client-game clock while paused so animations/FX/interpolation stop too
			// (pinning cl->serverTime isn't enough -- CG_DrawActiveFrame's server_time arg is what
			// drives cg->time, and it can still carry the frame's real-time advance). Don't freeze
			// during a scrub/seek: the render must follow the demo to the new spot.
			if (demo.is_paused() && !demo.is_seeking())
			{
				if (auto* cl = game::mp::cl.get(); cl)
				{
					frame_time = cl->snap.serverTime;
				}
			}

			// Hand the render time to cg_calc_view_values_stub, which is where the angles actually
			// get applied (see the note there -- writing them HERE is too early, CG_DrawActiveFrame
			// re-predicts cg->ps from the snapshot and wipes them before CG_CalcViewValues reads).
			g_demo_view_time = frame_time;
		}

		const auto result = CG_DrawActiveFrame_hook.invoke<std::int32_t>(
			local_client_num, frame_time, demo_type, cubemap_shot, cubemap_size, render_screen, a7);
        demo.finish_grenade_fx_rebuild();
        return result;
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

	void control_ui_element(const std::int32_t client_num, const void* old_snap, void* new_snap)
	{
        const std::uint8_t* popup_def = nullptr;
        const std::uint32_t* popup_record = nullptr;
        const std::uint8_t* hitmarker_def = nullptr;
        const std::uint32_t* hitmarker_record = nullptr;
        const auto snap_time = *reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(new_snap) + 0x4C);
        if (demo.is_demo_playing())
        {
            // MWR CG_UpdateOmnvars: global records at snapshot+0x4A24,
            // player records at PS+0x4540. Each record is {timeModified, value}.
            // The old iw6-sized snapshot and fixed indices wrote unrelated PS data.
            const auto globals = utils::hook::invoke<unsigned>(0x5A47B0_b);
            const auto per_player = utils::hook::invoke<unsigned>(0x5A4790_b);
            for (unsigned i = 0; i < globals + per_player; ++i)
            {
                const auto* def = static_cast<const std::uint8_t*>(game::Omnvar_GetDef(i));
                const auto* name = *reinterpret_cast<const char* const*>(def);
                if (!name) continue;
                const std::string_view key(name);
                if (demo.is_converted() && key == "ui_points_popup" && (def[8] & 8))
                {
                    popup_def = def;
                    popup_record = reinterpret_cast<const std::uint32_t*>(
                        static_cast<const std::uint8_t*>(new_snap) + 0x4540 + (i - globals) * 8);
                }
                if (demo.is_converted() && key == "damage_feedback" && (def[8] & 8) && i >= globals)
                {
                    hitmarker_def = def;
                    hitmarker_record = reinterpret_cast<const std::uint32_t*>(
                        static_cast<const std::uint8_t*>(new_snap) + 0x4540 + (i - globals) * 8);
                }
                const bool selected = key == "ui_loadout_selected" || key == "ui_team_selected";
                const bool hidden = key == "ui_options_menu" || key == "ui_spectator_selected"
                    || key == "ui_alien_show_eog_score";
                if (!selected && !hidden) continue;
                const auto offset = (def[8] & 8) ? 0x4540 + (i - globals) * 8 : 0x4A24 + i * 8;
                auto* record = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(new_snap) + offset);
                // A selected class/team prevents LUI from asking for a selection.
                const auto value = selected ? 1u : 0u;
                const auto* current = utils::hook::invoke<const std::uint32_t*>(0x5A4250_b, client_num, i);
                if (current[1] != value)
                    record[0] = std::max(current[0] + 1, *reinterpret_cast<const std::uint32_t*>(
                        static_cast<const std::uint8_t*>(new_snap) + 0x4C));
                record[1] = value;
            }
        }
        CG_UpdateOmnvars_hook.invoke<void>(client_num, old_snap, new_snap);
        // Client demos update the playerState omnvar before this hook. The
        // native old/new snapshot path can therefore miss the UI edge, even
        // though Game.GetOmnvar already reads the recorded value. Dispatch the
        // same native omnvar notification once per recorded score change.
        static std::uint32_t last_time{}, last_popup_marker{}, last_hitmarker{};
        if (snap_time < last_time || !demo.is_converted()) last_popup_marker = last_hitmarker = 0;
        last_time = snap_time;
        if (demo.is_converted() && popup_def && popup_record && popup_record[0]
            && popup_record[1] > 0 && popup_record[0] != last_popup_marker
            && *game::hks::lua_state)
        {
            last_popup_marker = popup_record[0];
            utils::hook::invoke<void>(0x26E430_b, client_num, popup_def, popup_record,
                *game::hks::lua_state);
        }
        // The native damagefeedbackhud.lua listens to this string omnvar and
        // plays MWR's own material/animation. Repeated equal hits are distinct
        // timeModified edges; never retrigger a held snapshot or a stale seek.
        if (demo.is_converted() && hitmarker_def && hitmarker_record && hitmarker_record[0]
            && hitmarker_record[0] != last_hitmarker && *game::hks::lua_state)
        {
            last_hitmarker = hitmarker_record[0];
            if (snap_time >= hitmarker_record[0] && snap_time - hitmarker_record[0] <= 250)
                utils::hook::invoke<void>(0x26E430_b, client_num, hitmarker_def, hitmarker_record,
                    *game::hks::lua_state);
        }
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
				game::mp::cgs->serverCommandSequence = gs->svr_cmd_seq; // (h1-mod stub; kept for our reads)
				// The stub cgs above does NOT reach the game -- record the baseline so playback keeps
				// the REAL cgs->serverCommandSequence seeded to it each feed (fixes mid-match demos'
				// "Lost reliable commands"; see process_server_commands / CG_GetServerCommandsState).
				note_command_baseline(gs->svr_cmd_seq);
				demo.set_gamestate_applied(true);
			}
		}

		// call MSG_ReadLong that this hook replaces
		return game::MSG_ReadLong(&msg);
	}

	// Clamp the reliable-command walker's target to what clc has actually received, so a loopback
	// GSC snapshot referencing a command the demo's (reset-to-baseline) clc never reaches can't
	// Com_Error EXE_ERR_NOT_RECEIVED. Only acts during playback; a no-op for our own explicit call
	// (which already passes clc->serverCommandSequence) and for normal live play.
	void exec_server_commands_clamped(int local_client_num, int latest_sequence)
	{
		if (demo.is_demo_playing())
		{
			const int clc_seq = game::mp::clc->serverCommandSequence;
			if (latest_sequence > clc_seq)
			{
				latest_sequence = clc_seq;
			}
		}
		CG_ExecuteNewServerCommands_hook.invoke<void>(local_client_num, latest_sequence);
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
			// NOTE(h1): MWR keeps the "virtual lobby" hub world loaded (CL_IsCgameInitialized()
			// stays true) even after backing all the way out to the main menu -- gating on that
			// alone made demoplay permanently unusable outside of an actual match. The virtual
			// lobby isn't a real match/server (see game::VirtualLobby_Loaded(), the same check
			// used throughout h1-mod to tell the hub apart from a real connection), so only block
			// when genuinely still connected to a match.
			if (!demo.is_demo_playing() && game::CL_IsCgameInitialized() && !game::VirtualLobby_Loaded())
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
			return;
		}

		// NOTE(h1): restart() reopens the file with skip_map_preload_ cleared (stop() resets
		// it), so parse_demo will naturally call process_map_header -> party::start_map when
		// it reaches that chunk. But we're still connected (connstate >= CA_PRIMED) from the
		// demo we just replaced, so the very next handle_demo_reading frame re-arms replay
		// BEFORE parse_demo has a chance to run start_map -- then the moment start_map's
		// disconnect/reconnect drops connstate to CA_DISCONNECTED, the (already-armed)
		// disconnect guard stops the demo mid-reconnect, leaving the client stuck on the
		// loading screen forever with nothing left to drive it. Explicitly bootstrap the map
		// load here (same as demoplay) so skip_map_preload_ is set and start_map runs BEFORE
		// any frame gets a chance to arm.
		demo.bootstrap_map_load();
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

			demo.begin_seek(); // so it completes even if the demo is paused
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
			demo.begin_seek(); // so it completes even if the demo is paused
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
    bool converted() { return demo.is_demo_playing() && demo.is_converted(); }

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

	bool paused()
	{
		return demo.is_paused();
	}

	void toggle_pause()
	{
		demo.toggle_paused();
	}

	std::optional<std::int32_t> current_time()
	{
		return demo.get_current_time();
	}

	std::optional<std::pair<std::int32_t, std::int32_t>> time_bounds()
	{
		return demo.get_time_bounds();
	}

	const std::filesystem::path& current_path()
	{
		return demo.get_path();
	}

	bool handle_key_event(const int key, const int down)
	{
		// Space (K_SPACE) normally binds to +gostand; harmless but pointless to also forward
		// while a demo plays (local input is already suppressed via CG_DrawActiveFrame's
		// demoType override), so swallow it here instead and use it to toggle pause.
		if (key == game::K_SPACE && down && demo.is_demo_playing() && demo.is_replay_armed())
		{
			// ...but only when focus is on gameplay. If the console / chat / a menu owns the
			// keyboard (keyCatchers != 0) the user is typing -- let the space through so it types
			// a space instead of pausing the demo.
			if (*game::keyCatchers != 0)
			{
				return false;
			}

			demo.toggle_paused();
			return true;
		}

		return false;
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
			cod4_playback_hud = dvars::register_bool("demo_cod4PlaybackHud", false,
				game::DVAR_FLAG_READ, "Use the converted COD4 replay HUD layout");

			// primary hook for demo playback; read demo data after server time has been updated
			CL_SetCGameTime_hook.create(game::CL_SetCGameTime, handle_demo_reading);

			// don't write packets when playing a demo
			CL_WritePacket_hook.create(game::CL_WritePacket, write_packet);

			// force the engine's own demoType==CLIENT path during playback -- this is what
			// actually suppresses local input (movement/fire/ADS), not a hand-written cg field.
			CG_DrawActiveFrame_hook.create(game::CG_DrawActiveFrame, draw_active_frame_stub);
            CG_EntityEvent_hook.create(0xF9400_b, grenade_entity_event);
            BG_PlayerAnimation_hook.create(0x2B8EF0_b, player_animation);
            CG_PlaySound_hook.create(0x31D040_b, grenade_play_sound);
            FX_PlayOrientedEffectWithMarkEntity_hook.create(0x3D66D0_b, bullet_play_oriented_effect);
            CG_ShouldCullImpactFX_hook.create(0xF36B0_b, bullet_should_cull_impact);

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

			// Never let the reliable-command walker request a command the client hasn't received.
			// During playback the loopback server runs the map's GSC (custom_weapons/classes ->
			// setConfigstring reliable commands), and its per-snapshot serverCommandSequence can sit
			// ABOVE the demo's own clc (which is reset to the recorded baseline). Walking up to that
			// target hits Com_Error EXE_ERR_NOT_RECEIVED (server shutdown mid-load) -- only bites
			// low-baseline (local) demos, since a high baseline already sits above those commands.
			CG_ExecuteNewServerCommands_hook.create(game::CG_ExecuteNewServerCommands, exec_server_commands_clamped);



			// NOTE: do NOT hook CG_CalcViewValues (0x13DF80). Tried it to apply the demo view angles
			// after CG_DrawActiveFrame's internal re-prediction -- it HUNG the game at startup
			// (process spinning, Responding=False, 234s CPU before it even reached the main menu).
			// It's a hot render-path function; we now write the SNAPSHOT's delta_angles instead and
			// let the engine's own prediction carry them into cg->ps (see draw_active_frame_stub).

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

			// pause/unpause demo playback (also bound to Space, see input.cpp)
			command::add("demopause", []
			{
				if (!demo.is_demo_playing())
				{
					console::error("demo must be playing to pause\n");
					return;
				}

				demo.toggle_paused();
			});

            // Read-only native corpse diagnostics for animation/seek regressions.
            command::add("demoactorstate", []
            {
                const auto* cl = game::mp::cl.get();
                if (!demo.is_demo_playing() || !demo.is_converted() || !cl || !game::CL_IsCgameInitialized())
                {
                    console::error("a converted demo must be active to inspect its actors\n");
                    return;
                }
                const auto* cgs = static_cast<const std::uint8_t*>(game::CG_GetServerCommandsState());
                if (!cgs) return;
                console::info("demoactorstate time=%d cgs=%p\n", cl->serverTime, cgs);
                // CG_Corpse (0xF28C0) owns eight 0x740-byte character records
                // at cgs+0x26D0. XAnimGetTime (0x5CC5B0) only reads their tree.
                for (unsigned slot = 0; slot < 8; ++slot)
                {
                    const auto* info = cgs + 0x26D0 + slot * 0x740;
                    const auto* tree = *reinterpret_cast<void* const*>(info + 0x4B0);
                    const auto animation = *reinterpret_cast<const unsigned*>(info + 0x4C8) & 2047u;
                    if (!tree || !animation) continue;
                    const auto phase = utils::hook::invoke<float>(0x5CC5B0_b, tree, animation);
                    console::info("demoactorstate corpse=%u client=%u legs=%u phase=%.6f\n", 42 + slot,
                        *reinterpret_cast<const unsigned*>(info), animation, phase);
                }
            });

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
