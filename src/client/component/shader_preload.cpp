#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include <algorithm>
#include <ctime>
#include <numeric>

#include "console.hpp"
#include "scheduler.hpp"
#include "shader_preload.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/nt.hpp>
#include <utils/properties.hpp>
#include <utils/string.hpp>
#include <utils/thread.hpp>

// Shader preloading ("ShaderUpload") on h1_mp64_ship. When r_preloadShaders is on, every
// material the database loads is queued, and the render thread draws each of its techniques
// with dummy geometry so the driver compiles them before gameplay; level loading waits for the
// queue to drain.
//
// The stock wait after each technique (0x6E7A60) ends an event query and spins
// GetData(flags 0) + Sleep(0) until the GPU is idle: one full CPU<->GPU round trip per
// technique, ~100 per material and hundreds of thousands per map. Any failing HRESULT
// (a removed or hung device) counts as "not ready yet", so the render thread spins forever
// and the loading screen with it. Sleep(0) on the THREAD_PRIORITY_HIGHEST render thread only
// yields to threads of equal or higher priority, never to the driver's own workers.
//
// Here:
// - The wait never blocks past the frame's budget. One event query stays in flight as a fence;
//   once r_preloadShadersSyncInterval more techniques are queued behind it, the next technique
//   waits for it, but only until the budget is spent, so the loading screen keeps its frames.
//   A failing HRESULT, or 2 s without an answer, abandons the preload instead of hanging.
// - Materials are queued only while a map loads (0x6E8260 until the load wait 0x399E10 returns)
//   or while the engine's "every map" job runs (the startup ShaderCacheDialog). Nothing uploads
//   during gameplay or in the menus, where it only cost frames.
// - The load wait, `while (!done) Sleep(1)` on the main thread once the map is loaded, gives up
//   after r_preloadShadersLoadWait ms. What is left compiles on first use, as with preloading
//   off, and the join isn't held up.
// - The dummy geometry and the query are released as soon as the queue is empty.
// - r_preloadShadersMode 2 (the default) skips the preload on DXVK. DXVK compiles every shader on
//   its own worker threads as soon as the database creates it, and once that is done a first
//   draw costs ~0.1 ms, so the preload's draws add only work. Native D3D11 keeps the preload.
// Each map load appends its cost, and the frame times of the minute after it, to
// players2/shader-preload.log. On DXVK it also logs how long DXVK kept compiling.
//
// Measured on an RX 9070 XT: the stock wait costs 64-71 us per technique, against 2.6 us with
// one sync every 64 techniques.
namespace shader_preload
{
	namespace
	{
		using clock = std::chrono::steady_clock;

		utils::hook::detour upload_wait_hook;
		utils::hook::detour upload_frame_hook;
		utils::hook::detour enqueue_hook;
		utils::hook::detour level_start_hook;
		utils::hook::detour load_wait_hook;

		game::dvar_t* r_preload_shaders_sync_interval = nullptr;
		game::dvar_t* r_preload_shaders_load_wait = nullptr;
		game::dvar_t* r_preload_shaders_mode = nullptr;

		constexpr auto gpu_timeout = 2000ms;
		constexpr auto log_file = "players2/shader-preload.log";
		constexpr auto watch_ms = 60000;
		constexpr auto watch_settle_ms = 5000;
		constexpr auto level_limit_ms = 120000; // a load that never reaches the load wait

		// Main thread -> database and render threads
		std::atomic<bool> level_loading = false;
		std::atomic<int> load_wait_start_ms = 0; // 0 outside the load wait

		// Render and database threads -> main thread
		std::atomic<int> techniques_total = 0;
		std::atomic<int> gpu_failures_total = 0;

		struct level_counters
		{
			std::atomic<int> materials;
			std::atomic<int> skipped;
			std::atomic<int> not_needed; // r_preloadShadersMode left them out
			std::atomic<int> upload_ms;
			std::atomic<int> gpu_wait_ms;
			std::atomic<int> frames;
			std::atomic<int> slow_frames;
			std::atomic<int> busiest_frame_ms;
			std::atomic<int> dropped;
			std::atomic<bool> gpu_lost;
		};

		level_counters level{};

		// Main thread only
		struct level_state
		{
			bool active;
			std::string map;
			int start_ms;
			int techniques_base;
		};

		level_state current_level{};

		struct frame_watch
		{
			bool active;
			std::string map;
			int start_ms;
			int skipped_base;
			clock::time_point last;
			std::vector<float> settle;
			std::vector<float> play;
		};

		frame_watch watch{};

		// Render thread only
		struct fence_state
		{
			ID3D11Query* query;
			bool pending;
			clock::time_point issued;
		};

		fence_state fence{};
		int techniques_since_fence = 0;
		bool abort_requested = false;

		ID3D11DeviceContext* immediate_context()
		{
			return *reinterpret_cast<ID3D11DeviceContext**>(0x12DFC00_b);
		}

		ID3D11Query* upload_query()
		{
			return *reinterpret_cast<ID3D11Query**>(0x111FEE70_b);
		}

		bool queue_pending()
		{
			return queue_read() < queue_write();
		}

		// Set by ShaderUpload's begin (dummy geometry and query created), cleared by its abort
		bool upload_resources_alive()
		{
			return *reinterpret_cast<bool*>(0x111FEDFC_b);
		}

		// The engine's "every map" job: the startup ShaderCacheDialog
		bool frontend_job_running()
		{
			return *reinterpret_cast<bool*>(0x2E87F50_b);
		}

		bool uploads_allowed()
		{
			return level_loading || frontend_job_running();
		}

		// IDXGIVkInteropDevice: only DXVK's devices answer QueryInterface for it
		constexpr GUID dxvk_interop_device{0xe2ef5fa5, 0xdc21, 0x4af7, {0x90, 0xc4, 0xf6, 0x7e, 0xf6, 0xa0, 0x93, 0x23}};

		enum class renderer_kind
		{
			unknown,
			d3d11,
			dxvk,
		};

		std::atomic<renderer_kind> renderer = renderer_kind::unknown;

		// Any thread (the device is free-threaded); unknown until the device exists
		renderer_kind detect_renderer()
		{
			if (const auto known = renderer.load(); known != renderer_kind::unknown)
			{
				return known;
			}

			const auto device = *reinterpret_cast<ID3D11Device**>(0x12DFBF8_b);
			if (!device)
			{
				return renderer_kind::unknown;
			}

			IUnknown* interop = nullptr;
			const auto dxvk = SUCCEEDED(device->QueryInterface(dxvk_interop_device, reinterpret_cast<void**>(&interop))) && interop;
			if (interop)
			{
				interop->Release();
			}

			renderer = dxvk ? renderer_kind::dxvk : renderer_kind::d3d11;
			return renderer;
		}

		// Whether a map load should queue its materials (the "every map" job always does)
		bool preload_wanted()
		{
			const auto mode = r_preload_shaders_mode->current.integer;
			return mode == 1 || (mode == 2 && detect_renderer() != renderer_kind::dxvk);
		}

		// DXVK's shader compiler threads ("dxvk-shader-n" and "dxvk-shader-l" in 3.0). Their CPU time
		// shows how long DXVK keeps compiling a map's shaders after the map has loaded. Main thread only.
		struct compiler_watch
		{
			std::vector<HANDLE> threads;
			double level_base_ms;
			double load_ms;
			double sample_ms;
			int sample_time;
			bool busy;
			int busy_until;
			int slow_frames;
			int slow_frames_busy;
		};

		compiler_watch compiler{};

		std::wstring thread_name(const HANDLE thread)
		{
			static const utils::nt::library kernel32("kernel32.dll");
			static const auto get_description = kernel32.get_proc<HRESULT(WINAPI*)(HANDLE, PWSTR*)>("GetThreadDescription");

			PWSTR name = nullptr;
			if (!get_description || FAILED(get_description(thread, &name)) || !name)
			{
				return {};
			}

			std::wstring result = name;
			LocalFree(name);
			return result;
		}

		void find_compiler_threads()
		{
			for (const auto thread : compiler.threads)
			{
				CloseHandle(thread);
			}

			compiler.threads.clear();
			for (const auto id : utils::thread::get_thread_ids())
			{
				const auto thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, id);
				if (!thread)
				{
					continue;
				}

				if (thread_name(thread).starts_with(L"dxvk-shader"))
				{
					compiler.threads.push_back(thread);
				}
				else
				{
					CloseHandle(thread);
				}
			}
		}

		double compiler_cpu_ms()
		{
			const auto ms = [](const FILETIME& time)
			{
				return static_cast<double>((static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime) / 10000.0;
			};

			auto total = 0.0;
			for (const auto thread : compiler.threads)
			{
				FILETIME created{}, exited{}, kernel{}, user{};
				if (GetThreadTimes(thread, &created, &exited, &kernel, &user))
				{
					total += ms(kernel) + ms(user);
				}
			}

			return total;
		}

		void store_max(std::atomic<int>& value, const int candidate)
		{
			if (candidate > value)
			{
				value = candidate;
			}
		}

		int elapsed_ms(const clock::time_point since)
		{
			return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - since).count());
		}

		// Same rule as the stock wait: 15 ms per frame while 3D frames are drawn, 50 ms behind the
		// loading screen or the blocking "preloading shaders" popup.
		bool budget_spent(const int start_ms)
		{
			const auto blocking = *reinterpret_cast<bool*>(0x111FEDFD_b);
			const auto short_budget = !blocking && utils::hook::invoke<bool>(0x655000_b) && !utils::hook::invoke<bool>(0x654FD0_b);
			const auto budget = short_budget ? 15 : 50;
			return game::Sys_Milliseconds() - start_ms > budget;
		}

		enum class fence_result
		{
			done,
			pending,
			failed,
		};

		fence_result poll_fence(ID3D11DeviceContext* context)
		{
			if (!fence.pending)
			{
				return fence_result::done;
			}

			// Released and recreated since: whatever it covered has long run
			const auto query = upload_query();
			if (query != fence.query)
			{
				fence.pending = false;
				return fence_result::done;
			}

			const auto result = context->GetData(query, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (result == S_OK)
			{
				fence.pending = false;
				return fence_result::done;
			}

			if (FAILED(result) || clock::now() - fence.issued > gpu_timeout)
			{
				return fence_result::failed;
			}

			return fence_result::pending;
		}

		// Waits for the fence in flight, but no longer than the frame's budget
		fence_result wait_for_fence(ID3D11DeviceContext* context, const int start_ms)
		{
			const auto start = clock::now();
			auto result = poll_fence(context);
			while (result == fence_result::pending && !budget_spent(start_ms))
			{
				if (clock::now() - start < 1ms)
				{
					SwitchToThread();
				}
				else
				{
					std::this_thread::sleep_for(1ms);
				}

				result = poll_fence(context);
			}

			level.gpu_wait_ms += elapsed_ms(start);
			return result;
		}

		void issue_fence(ID3D11DeviceContext* context)
		{
			const auto query = upload_query();
			context->End(query);
			context->Flush();
			fence = {query, true, clock::now()};
		}

		// bool ShaderUpload_WaitGpu(int startMs, bool checkBudget): called with checkBudget false
		// before a frame's batch (its result is ignored) and true after each technique; false ends
		// this frame's batch.
		bool upload_wait_stub(const int start_ms, const bool check_budget)
		{
			if (abort_requested)
			{
				return false;
			}

			if (!check_budget)
			{
				return true;
			}

			++techniques_total;

			const auto context = immediate_context();
			if (context && upload_query() && ++techniques_since_fence >= r_preload_shaders_sync_interval->current.integer)
			{
				const auto result = wait_for_fence(context, start_ms);
				if (result == fence_result::failed)
				{
					abort_requested = true;
					return false;
				}

				// Out of budget with the fence still in flight: the next technique waits again
				if (result == fence_result::pending)
				{
					return false;
				}

				issue_fence(context);
				techniques_since_fence = 0;
			}

			return !budget_spent(start_ms);
		}

		// The engine's ShaderUpload_Abort: empties the queue, releases the dummy geometry and the query
		void abort_upload()
		{
			fence = {};
			techniques_since_fence = 0;
			*reinterpret_cast<int*>(0x111FEDF8_b) = 0; // technique the next material resumes at
			utils::hook::invoke<void>(0x6E7E20_b);
		}

		// void ShaderUpload_Frame(): the render thread's per-frame preload step.
		void upload_frame_stub()
		{
			if (abort_requested)
			{
				abort_requested = false;
				abort_upload();
				++gpu_failures_total;
				level.gpu_lost = true;
				return;
			}

			if (!queue_pending())
			{
				if (upload_resources_alive())
				{
					abort_upload();
				}

				return;
			}

			// Materials left over from a load that has ended
			if (!uploads_allowed())
			{
				level.dropped += static_cast<int>(queue_write() - queue_read());
				abort_upload();
				return;
			}

			// The map is loaded and only waits for this queue: give up on the rest
			const auto wait_start = load_wait_start_ms.load();
			if (wait_start && game::Sys_Milliseconds() - wait_start > r_preload_shaders_load_wait->current.integer)
			{
				level.dropped += static_cast<int>(queue_write() - queue_read());
				abort_upload();
				return;
			}

			const auto start = clock::now();
			upload_frame_hook.invoke<void>();
			const auto ms = elapsed_ms(start);

			level.upload_ms += ms;
			++level.frames;
			store_max(level.busiest_frame_ms, ms);
			if (ms > 50)
			{
				++level.slow_frames;
			}

			if (!queue_pending())
			{
				if (const auto context = immediate_context())
				{
					context->Flush();
				}

				abort_upload();
			}
		}

		// void ShaderUpload_QueueMaterial(Material*): called by the database for each material it adds
		void enqueue_stub(void* material)
		{
			if (!utils::hook::invoke<bool>(0x6796F0_b)) // r_preloadShaders || r_preloadShadersFrontend
			{
				return;
			}

			if (!uploads_allowed())
			{
				++level.skipped;
				return;
			}

			if (!frontend_job_running() && !preload_wanted())
			{
				++level.not_needed;
				return;
			}

			++level.materials;
			enqueue_hook.invoke<void>(material);
		}

		void write_log(const std::string& line)
		{
			console::info("%s\n", line.data());

			const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
			tm local{};
			localtime_s(&local, &time);
			char stamp[32]{};
			std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
			utils::io::write_file(log_file, utils::string::va("[%s] ", stamp) + line + "\n", true);
		}

		// void ShaderUpload_SetLevel(const char* mapName): the map's load starts (main thread)
		void level_start_stub(const char* map)
		{
			level_start_hook.invoke<void>(map);

			level.materials = 0;
			level.skipped = 0;
			level.not_needed = 0;
			level.upload_ms = 0;
			level.gpu_wait_ms = 0;
			level.frames = 0;
			level.slow_frames = 0;
			level.busiest_frame_ms = 0;
			level.dropped = 0;
			level.gpu_lost = false;

			current_level = {true, map ? map : "?", game::Sys_Milliseconds(), techniques_total};
			watch.active = false;
			level_loading = true;

			if (detect_renderer() == renderer_kind::dxvk)
			{
				find_compiler_threads();
				compiler.level_base_ms = compiler_cpu_ms();
			}
		}

		void finish_level(const int wait_ms)
		{
			if (!current_level.active)
			{
				return;
			}

			current_level.active = false;
			const auto load_ms = game::Sys_Milliseconds() - current_level.start_ms;

			std::string line;
			if (level.materials == 0 && level.not_needed > 0)
			{
				line = utils::string::va("%s: loaded in %.1f s, preload skipped (%s)", current_level.map.data(), load_ms / 1000.0,
					r_preload_shaders_mode->current.integer == 0 ? "r_preloadShadersMode 0" : "DXVK compiles shaders in the background");
			}
			else if (!utils::hook::invoke<bool>(0x6796F0_b) && level.materials == 0)
			{
				line = utils::string::va("%s: loaded in %.1f s, preloading off", current_level.map.data(), load_ms / 1000.0);
			}
			else
			{
				line = utils::string::va("%s: loaded in %.1f s; preload %d materials, %d techniques, %.1f s of upload frames, "
					"waited %.1f s after the map loaded",
					current_level.map.data(), load_ms / 1000.0, level.materials.load(), techniques_total - current_level.techniques_base,
					level.upload_ms / 1000.0, wait_ms / 1000.0);
				line += utils::string::va("; busiest frame %d ms, %d of %d frames over 50 ms, %.1f s waiting on the GPU",
					level.busiest_frame_ms.load(), level.slow_frames.load(), level.frames.load(), level.gpu_wait_ms / 1000.0);

				if (level.dropped > 0)
				{
					line += utils::string::va("; %d materials left to compile in game (r_preloadShadersLoadWait %d ms)",
						level.dropped.load(), r_preload_shaders_load_wait->current.integer);
				}

				if (level.gpu_lost)
				{
					line += "; abandoned: the GPU stopped answering";
				}
			}

			write_log(line);

			watch = {};
			watch.active = true;
			watch.map = current_level.map;
			watch.start_ms = game::Sys_Milliseconds();
			watch.skipped_base = level.skipped;

			const auto cpu = compiler_cpu_ms();
			compiler.load_ms = cpu - compiler.level_base_ms;
			compiler.sample_ms = cpu;
			compiler.sample_time = watch.start_ms;
			compiler.busy = !compiler.threads.empty();
			compiler.busy_until = watch.start_ms;
			compiler.slow_frames = 0;
			compiler.slow_frames_busy = 0;
		}

		// Every 250 ms during the watch: is DXVK still compiling (over a fifth of a core)?
		void sample_compiler(const int now)
		{
			if (compiler.threads.empty() || now - compiler.sample_time < 250)
			{
				return;
			}

			const auto cpu = compiler_cpu_ms();
			compiler.busy = cpu - compiler.sample_ms > 0.2 * (now - compiler.sample_time);
			if (compiler.busy)
			{
				compiler.busy_until = now;
			}

			compiler.sample_ms = cpu;
			compiler.sample_time = now;
		}

		std::string describe_compiler()
		{
			const auto after_ms = compiler_cpu_ms() - compiler.level_base_ms - compiler.load_ms;
			const auto busy_for = compiler.busy_until - watch.start_ms;
			return utils::string::va("%s, DXVK: %.0f s of shader compiling (CPU, all threads) during the load and %.0f s after it; %s; "
				"%d of %d frames over 50 ms came while it was compiling",
				watch.map.data(), compiler.load_ms / 1000.0, after_ms / 1000.0,
				compiler.busy ? "still compiling a minute after the load" : utils::string::va("done %.1f s after the load", busy_for / 1000.0),
				compiler.slow_frames_busy, compiler.slow_frames);
		}

		// void R_WaitForShaderUpload(): the main thread's wait once the map's zones are loaded
		void load_wait_stub()
		{
			const auto start = game::Sys_Milliseconds();
			load_wait_start_ms = start ? start : 1;
			load_wait_hook.invoke<void>();
			load_wait_start_ms = 0;
			level_loading = false;

			finish_level(game::Sys_Milliseconds() - start);
		}

		std::string describe_frames(const std::vector<float>& frames)
		{
			if (frames.empty())
			{
				return "no frames";
			}

			auto sorted = frames;
			std::ranges::sort(sorted);
			const auto total = std::accumulate(sorted.begin(), sorted.end(), 0.0);
			const auto count_over = [&](const float ms)
			{
				return static_cast<int>(sorted.end() - std::ranges::upper_bound(sorted, ms));
			};

			return utils::string::va("%d frames, average %.1f ms, 99th percentile %.1f ms, worst %.0f ms, %d over 50 ms, %d over 100 ms",
				static_cast<int>(sorted.size()), total / sorted.size(), sorted[sorted.size() * 99 / 100], sorted.back(),
				count_over(50.0f), count_over(100.0f));
		}

		// Main thread, every frame: frame times of the minute after a map load
		void watch_frames()
		{
			// A load without the load wait (if there is one) must not keep queueing forever
			if (current_level.active && !load_wait_start_ms && game::Sys_Milliseconds() - current_level.start_ms > level_limit_ms)
			{
				current_level.active = false;
				level_loading = false;
				write_log(utils::string::va("%s: no load wait within %d s of the load starting; preload stopped",
					current_level.map.data(), level_limit_ms / 1000));
			}

			if (!watch.active)
			{
				return;
			}

			sample_compiler(game::Sys_Milliseconds());

			const auto now = clock::now();
			if (watch.last != clock::time_point{})
			{
				const auto ms = std::chrono::duration<float, std::milli>(now - watch.last).count();
				auto& bucket = game::Sys_Milliseconds() - watch.start_ms < watch_settle_ms ? watch.settle : watch.play;
				bucket.push_back(ms);

				if (ms > 50.0f)
				{
					++compiler.slow_frames;
					compiler.slow_frames_busy += compiler.busy;
				}
			}

			watch.last = now;

			if (game::Sys_Milliseconds() - watch.start_ms < watch_ms)
			{
				return;
			}

			watch.active = false;
			write_log(utils::string::va("%s, first 5 s: %s", watch.map.data(), describe_frames(watch.settle).data()));
			write_log(utils::string::va("%s, 5-60 s: %s; materials loaded in that minute and not preloaded: %d",
				watch.map.data(), describe_frames(watch.play).data(), level.skipped - watch.skipped_base));

			if (!compiler.threads.empty())
			{
				write_log(describe_compiler());
			}
		}

		// H1-Mod saved r_preloadShaders as 0 for everyone while preloading was broken; turn it on
		// once for those configs now that it works.
		void enable_preload_once()
		{
			const auto marker = (utils::properties::get_appdata_path() / "shader-preload-fixed").generic_string();
			if (utils::io::file_exists(marker))
			{
				return;
			}

			game::Dvar_SetFromStringByNameFromSource("r_preloadShaders", "1", game::DvarSetSource::DVAR_SOURCE_INTERNAL);
			utils::io::write_file(marker, "r_preloadShaders turned on once by the shader preload fix\n");
		}
	}

	int techniques_drawn()
	{
		return techniques_total;
	}

	int gpu_failures()
	{
		return gpu_failures_total;
	}

	unsigned int queue_read()
	{
		return *reinterpret_cast<unsigned int*>(0x111FEDF0_b);
	}

	unsigned int queue_write()
	{
		return *reinterpret_cast<unsigned int*>(0x111FEDF4_b);
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

			r_preload_shaders_sync_interval = dvars::register_int("r_preloadShadersSyncInterval", 64, 1, 4096, game::DVAR_FLAG_SAVED,
				"Shader preload techniques queued behind the GPU before waiting for it (1 = the stock wait after every technique)");
			r_preload_shaders_load_wait = dvars::register_int("r_preloadShadersLoadWait", 5000, 0, 600000, game::DVAR_FLAG_SAVED,
				"Longest a loaded map waits for its shader preload, in ms; the rest compiles in game");
			r_preload_shaders_mode = dvars::register_int("r_preloadShadersMode", 2, 0, 2, game::DVAR_FLAG_SAVED,
				"Map loads preload shaders: 0 never, 1 whenever r_preloadShaders is on, 2 like 1 except on DXVK, which compiles them in the background");

			upload_wait_hook.create(0x6E7A60_b, upload_wait_stub);
			upload_frame_hook.create(0x6E7B90_b, upload_frame_stub);
			enqueue_hook.create(0x6E8530_b, enqueue_stub);
			level_start_hook.create(0x6E8260_b, level_start_stub);
			load_wait_hook.create(0x399E10_b, load_wait_stub);

			scheduler::loop(watch_frames, scheduler::pipeline::main);
			scheduler::on_game_initialized(enable_preload_once, scheduler::pipeline::main);
		}
	};
}

REGISTER_COMPONENT(shader_preload::component)
