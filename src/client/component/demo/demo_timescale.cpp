#include "std_include.hpp"
#include "loader/component_loader.hpp"

#include "demo_playback.hpp"
#include "demo_avidemo.hpp"

#include "fps.hpp"
#include "demo_game.hpp"
#include "component/scheduler.hpp"
#include "utils/hook.hpp"
#include "utils/string.hpp"

#include <vector>
#include <cmath>

namespace demo_timescale
{
	namespace
	{
		utils::hook::detour Com_TimeScaleMsec_hook;
		utils::hook::detour LUI_Layout_hook;
		utils::hook::detour UI_UpdateTime_hook;

		game::dvar_t* demo_timescale;

		// ===== killfeed / HUD slow-mo clock ======================================
		// The killfeed obituary (and the .menu/expression HUD) times off uiDC.realTime, which
		// UI_UpdateTime feeds from the real UI clock -> it runs at real speed and races past in
		// slow-mo (obituaries fade on ABSOLUTE timestamps, so scaling LUI deltaTime alone does
		// nothing). Point uiDC.realTime at the demo's serverTime whenever a demo plays, so the
		// killfeed slows with BOTH `timescale` slow-mo AND avidemo capture, and freezes on pause
		// (serverTime is pinned when paused).
		void UI_UpdateTime_stub(int local_client_num, int realtime)
		{
			if (demo_playback::playing())
			{
				if (auto* cl = game::mp::cl.get(); cl)
				{
					realtime = cl->serverTime;
				}
			}
			UI_UpdateTime_hook.invoke<void>(local_client_num, realtime);
		}

		// LUI HUD animations (hitmarker etc.) run off a per-frame deltaTime. During capture feed the
		// fixed capture timestep; during `timescale` slow-mo scale the real deltaTime by timescale
		// (fractional accumulator so sub-1ms steps don't round to a freeze); paused -> 0.
		int scale_lui_delta(int delta_time)
		{
			if (delta_time <= 0)
			{
				return delta_time;
			}
			if (demo_avidemo::is_capturing())
			{
				const int fps = demo_avidemo::capture_fps();
				return std::max(1, 1000 / (fps > 0 ? fps : 250));
			}
			if (!demo_playback::playing())
			{
				return delta_time;
			}
			if (demo_playback::paused())
			{
				return 0;
			}
			static game::dvar_t* ts = nullptr;
			if (!ts) { ts = game::Dvar_FindVar("timescale"); }
			const float scale = ts ? ts->current.value : 1.0f;
			if (scale >= 0.999f && scale <= 1.001f)
			{
				return delta_time;
			}
			static float accum = 0.0f;
			accum += static_cast<float>(delta_time) * scale;
			const int scaled = static_cast<int>(accum);
			accum -= static_cast<float>(scaled);
			return scaled;
		}

		void LUI_Layout_stub(int local_client_num, const char* root_name, int delta_time, void* lua_vm)
		{
			LUI_Layout_hook.invoke<void>(local_client_num, root_name, scale_lui_delta(delta_time), lua_vm);
		}

		void generate_pattern(std::array<std::uint8_t, 1000>& pattern, std::size_t ones_count, std::size_t zeros_count)
		{
			assert(ones_count + zeros_count == pattern.size());

			for (std::size_t i = 0, zeros = 1, ones = 1; i < pattern.size(); ++i)
			{
				if (ones * zeros_count < zeros * ones_count)
				{
					++ones;
					pattern[i] = 1;
				}
				else
				{
					++zeros;
					pattern[i] = 0;
				}
			}

			assert(std::accumulate(pattern.begin(), pattern.end(), 0) > 0);
		}

		void calculate_pattern(std::array<std::uint8_t, 1000>& pattern, float fps, float timescale)
		{
			// Com_TimeScaleMsec is called once per frame, so the number of calls it takes to advance 1000 ms
			// can be calculated by using the following formula: fps / timescale

			// example: 500 fps and 0.01 timescale -> 500 / 0.01 = 50'000
			// a pattern needs to be generated where 1000 * 1ms and 49'000 * 0ms are interleaved,
			// and fit in an array of size 1000

			const auto call_count_per_sec = static_cast<std::size_t>(std::clamp(fps / timescale, 1000.0f, 1'000'000.0f));
			const auto ones_count = static_cast<std::size_t>(pattern.size() / (call_count_per_sec / static_cast<float>(pattern.size())));
			const auto zeros_count = pattern.size() - ones_count;

			generate_pattern(pattern, ones_count, zeros_count);
		}

		std::int32_t Com_TimeScaleMsec_stub(std::int32_t msec)
		{
			// avidemo capture owns the clock: decouple game time from real time by advancing a
			// FIXED 1000/fps ms per rendered frame, so every captured frame is deterministic and
			// the encoded video is perfectly smooth no matter how slow the render/disk is.
			if (int fixed; demo_avidemo::fixed_timestep_msec(fixed))
			{
				return fixed;
			}

			// The demo GUI drives the game's `timescale` dvar (registered in gameplay.cpp). Below
			// ~0.1x the stock engine frame-time math collapses to ~15fps (it works in whole ms, and
			// timescale*rawMsec rounds to 0 most frames). Rebuild the fractional advance as a pattern
			// of 0ms/1ms steps (the iw6/anomaly fix) so 0.05x / 0.025x etc. play back smoothly. Only
			// active during demo playback; normal gameplay falls through to the original.
			static game::dvar_t* ts = nullptr;
			if (!ts) { ts = game::Dvar_FindVar("timescale"); }

			if (!demo_playback::playing() || !ts || ts->current.value == 1.0f)
			{
				return Com_TimeScaleMsec_hook.invoke<std::int32_t>(msec);
			}
			if (ts->current.value == 0.0f)
			{
				return 0; // pause game
			}

			// the code below generates a pattern of interleaved 0s and 1s based on calculated avg fps
			// a new pattern is generated every 1000 frames, or after timescale or maxfps changes
			// the pattern determines the speed at which the game advances

			const auto timescale = ts->current.value;
			const auto avg_fps = fps::get_avg_fps();
			const auto frame_time = timescale * 1000.0f / avg_fps;
			const auto* com_maxfps = game::Dvar_FindVar("com_maxfps");
			const auto maxfps = (com_maxfps) ? com_maxfps->current.integer : static_cast<std::int32_t>(avg_fps);

			static auto last_timescale = timescale;
			static auto last_maxfps = maxfps;
			static std::size_t pattern_index;
			static std::array<std::uint8_t, 1000> pattern;

			if (last_timescale != timescale || last_maxfps != maxfps)
			{
				last_timescale = timescale;
				last_maxfps = maxfps;

				// update pattern using the maxfps instead of avg fps for now
				calculate_pattern(pattern, static_cast<float>(maxfps), timescale);

				// update the pattern again in the near future when the average fps is more accurate
				pattern_index = 95 * pattern.size() / 100;
			}

			if (frame_time > 1.0f)
			{
				const auto i_frame_time = static_cast<std::int32_t>(frame_time);
				const auto ones_count = static_cast<std::size_t>(frame_time * 1000 - i_frame_time * 1000);
				if (ones_count <= 1)
				{
					return i_frame_time;
				}

				if (pattern_index % pattern.size() == 0)
				{
					const auto zeros_count = pattern.size() - ones_count;
					generate_pattern(pattern, ones_count, zeros_count);
				}

				return i_frame_time + pattern[pattern_index++ % pattern.size()];
			}
			else if (pattern_index % pattern.size() == 0)
			{
				calculate_pattern(pattern, avg_fps, timescale);
			}

			// advance (1ms) or pause (0ms) based on the pattern
			return pattern[pattern_index++ % pattern.size()];
		}
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

			// Sub-0.1x smooth slow-mo for demo playback. h1-mod controls timescale by INJECTING
			// its `timescale` dvar's value into Com_TimeScaleMsec at +0x10 (gameplay.cpp), not by
			// detouring it -- so detouring the function entry here coexists: our stub owns the
			// fractional-advance math while a demo plays, and falls through to the original (which
			// still reads the injected dvar) otherwise. This is what makes 0.05x / 0.025x usable,
			// and it's the same lever avidemo uses to force a fixed capture timestep.
			Com_TimeScaleMsec_hook.create(game::Com_TimeScaleMsec, Com_TimeScaleMsec_stub);

			// Slow the killfeed/HUD with the demo: drive uiDC.realTime off serverTime + scale LUI
			// deltaTime, so the killfeed slows/freezes with BOTH `timescale` slow-mo and avidemo
			// capture. Inert when no demo is playing.
			LUI_Layout_hook.create(game::LUI_Layout, LUI_Layout_stub);
			UI_UpdateTime_hook.create(game::UI_UpdateTime, UI_UpdateTime_stub);

			demo_timescale = game::demo_compat::register_float(
				"demotimescale", 1.0f, 0.0f, 1000.0f, game::DVAR_FLAG_NONE, "Set playback speed for demos");
		}
	};
}

REGISTER_COMPONENT(demo_timescale::component)
