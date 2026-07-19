#include "std_include.hpp"
#include "loader/component_loader.hpp"

#include "demo_playback.hpp"
#include "demo_avidemo.hpp"
#include "demo_game.hpp"

#include "component/scheduler.hpp"
#include "component/console.hpp"
#include "component/command.hpp"
#include "game/dvars.hpp"

#include <atomic>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <format>
#include <cmath>

#include <Windows.h>

// =============================================================================
//  avidemo movie capture -- see demo_avidemo.hpp for the concept.
//
//  All capture work (resolution latch, ffmpeg spawn, framebuffer readback, pipe
//  write, finalize) runs on the RENDERER pipeline (scheduler::renderer fires at
//  R_EndFrame, once per rendered frame). The Com_TimeScaleMsec hook in
//  demo_timescale.cpp queries fixed_timestep_msec() so each of those frames
//  advances the demo clock by a fixed 1000/fps ms -> deterministic, smooth output.
// =============================================================================
namespace demo_avidemo
{
	namespace
	{
		constexpr int UNSET = INT32_MIN;

		// ---- config dvars --------------------------------------------------------
		// NOTE: NO string dvars here. MWR's Dvar_RegisterString interns the value into the
		// script-string pool and leaks it on SAVED config re-execs, which exhausts the pool
		// and crashes at the menu (MT_AllocIndex). So codec/container are INT dvars (index into
		// the tables below) and the ffmpeg path is a plain module string persisted to a file.
		game::dvar_t* avi_fps = nullptr;      // capture rate (frames sampled per demo-second)
		game::dvar_t* avi_out_fps = nullptr;  // video playback fps (slow-mo = avi_fps / avi_out_fps)
		game::dvar_t* avi_codec = nullptr;    // int index into kCodecs
		game::dvar_t* avi_crf = nullptr;      // quality (lower = better/bigger)
		game::dvar_t* avi_container = nullptr;// int index into kContainers
		game::dvar_t* avi_flip = nullptr;     // vertical-flip readback (D3D readback orientation)

		// codec index -> encoder + output settings.
		//  - av1/h265/h264 are DELIVERY codecs: small 8-bit yuv420 files, quality via -crf.
		//  - prores4444 is the EDITING-GRADE intermediate the IWXMVM montage workflow uses: near-
		//    lossless 10-bit 4:4:4 ProRes 4444 (ap4h) that Premiere/AE/Resolve edit natively. It
		//    ignores -crf, forces a .mov container, and produces HUGE files (~0.5 GB/s of footage).
		//    Reconstructed from a friend's known-good ffmpeg log: rawvideo -> `-c:v prores
		//    -pix_fmt yuv444p10le` yields ap4h (the pixel format drives the 4444 profile).
		struct codec_t
		{
			const char* name;        // display
			const char* encoder;     // ffmpeg -c:v
			const char* out_pix_fmt; // output pixel format
			const char* force_ext;   // forced container (nullptr = use avi_container dvar)
			bool use_crf;            // -crf QUALITY (delivery codecs) vs codec-default (prores)
			const char* extra;       // extra encoder args
		};
		constexpr codec_t kCodecs[] = {
			{"av1",        "libsvtav1", "yuv420p",     nullptr, true,  ""},
			{"h265",       "libx265",   "yuv420p",     nullptr, true,  ""},
			{"h264",       "libx264",   "yuv420p",     nullptr, true,  ""},
			{"prores4444", "prores",    "yuv444p10le", "mov",   false, ""},
		};
		constexpr const char* kContainers[] = { "mp4", "mkv" };
		constexpr int kCodecDefault = 3;      // prores4444 (IWXMVM editing standard)
		constexpr int kContainerDefault = 0;  // mp4 (only used by the delivery codecs)

		std::string g_ffmpeg_path = "ffmpeg"; // free-form path -> kept OUT of the dvar system

		// ---- capture state (touched from the renderer thread) --------------------
		std::atomic<bool> armed_{ false };
		std::atomic<bool> capturing_{ false };
		std::atomic<int> in_point_{ UNSET };
		std::atomic<int> out_point_{ UNSET };
		std::atomic<int> frames_{ 0 };

		// process / pipe (renderer thread only once capturing_)
		HANDLE ff_proc_ = nullptr;
		HANDLE ff_stdin_ = nullptr;   // our write end of ffmpeg's stdin
		HANDLE ff_log_ = nullptr;     // ffmpeg stderr/stdout log file
		int cap_w_ = 0, cap_h_ = 0;
		std::vector<std::uint8_t> frame_buf_;
		std::string out_path_;

		int dvar_int(game::dvar_t* d, int def) { return d ? d->current.integer : def; }
		int codec_index() { return std::clamp(dvar_int(avi_codec, kCodecDefault), 0, static_cast<int>(std::size(kCodecs)) - 1); }
		int container_index() { return std::clamp(dvar_int(avi_container, kContainerDefault), 0, static_cast<int>(std::size(kContainers)) - 1); }

		std::filesystem::path avi_dir()
		{
			std::error_code ec;
			auto dir = std::filesystem::current_path(ec) / "demos" / "avi";
			std::filesystem::create_directories(dir, ec);
			return dir;
		}

		// ffmpeg path persistence (a plain text file, so it survives restarts without a dvar)
		std::filesystem::path ffmpeg_path_file() { return avi_dir() / "ffmpeg_path.txt"; }

		void load_ffmpeg_path()
		{
			std::error_code ec;
			const auto f = ffmpeg_path_file();
			if (!std::filesystem::exists(f, ec)) return;
			std::ifstream in(f);
			std::string line;
			if (std::getline(in, line) && !line.empty()) g_ffmpeg_path = line;
		}

		// next free demos/avi/<stem>_NNN.<ext>
		std::string next_output_path(const std::string& ext)
		{
			std::string stem = "demo";
			const auto& p = demo_playback::current_path();
			if (!p.empty()) stem = p.stem().string();

			const auto dir = avi_dir();
			for (int i = 0; i < 100000; ++i)
			{
				auto cand = dir / (stem + "_" + std::format("{:03}", i) + "." + ext);
				std::error_code ec;
				if (!std::filesystem::exists(cand, ec)) return cand.string();
			}
			return (dir / (stem + "." + ext)).string();
		}

		// current backbuffer resolution. Primary: the render-target table the engine's
		// own screenshot uses (off_10AA5D00[rt].{+48 w, +50 h}, u16); fallback: the
		// active ScreenPlacement's real viewport size. Logs what it picked.
		bool latch_resolution()
		{
			int w = 0, h = 0;
			auto* rt = reinterpret_cast<std::uint8_t*>(0x10AA5D00_b);
			for (int idx = 1; idx >= 0 && (w == 0 || h == 0); --idx)
			{
				w = *reinterpret_cast<std::uint16_t*>(rt + idx * 64 + 48);
				h = *reinterpret_cast<std::uint16_t*>(rt + idx * 64 + 50);
			}
			if (w == 0 || h == 0 || w > 16384 || h > 16384)
			{
				if (auto* sp = game::ScrPlace_GetActivePlacement(); sp)
				{
					w = static_cast<int>(std::lround(sp->realViewportSize[0]));
					h = static_cast<int>(std::lround(sp->realViewportSize[1]));
				}
			}
			// ffmpeg needs even dimensions for yuv420p
			w &= ~1; h &= ~1;
			if (w < 2 || h < 2 || w > 16384 || h > 16384)
			{
				console::error("[avidemo] could not determine a valid capture resolution (%ix%i)\n", w, h);
				return false;
			}
			cap_w_ = w; cap_h_ = h;
			return true;
		}

		std::string build_ffmpeg_cmd(const std::string& exe)
		{
			// out_fps = the video's playback framerate. We captured avi_fps frames per demo-second,
			// so timing them at out_fps stretches the clip by (avi_fps / out_fps) = the slow-mo factor.
			const int out_fps = std::clamp(dvar_int(avi_out_fps, 60), 1, 1000);
			const int crf = std::clamp(dvar_int(avi_crf, 23), 0, 63);
			const auto& c = kCodecs[codec_index()];

			const bool flip = avi_flip && avi_flip->current.enabled;

			// raw rgb24 frames on stdin -> encoded file. -an: no audio (video-only capture).
			std::string cmd = "\"" + exe + "\"";
			cmd += " -y -f rawvideo -pixel_format rgb24";
			cmd += " -video_size " + std::to_string(cap_w_) + "x" + std::to_string(cap_h_);
			cmd += " -framerate " + std::to_string(out_fps); // frames timed for playback speed
			cmd += " -i -";
			if (flip) cmd += " -vf vflip";
			cmd += " -an -c:v " + std::string(c.encoder);
			if (c.use_crf) cmd += " -crf " + std::to_string(crf); // prores has no CRF (codec-default quality)
			if (c.extra && *c.extra) cmd += " " + std::string(c.extra);
			cmd += " -r " + std::to_string(out_fps);
			cmd += " -pix_fmt " + std::string(c.out_pix_fmt);
			cmd += " \"" + out_path_ + "\"";
			return cmd;
		}

		void close_handles()
		{
			if (ff_stdin_) { CloseHandle(ff_stdin_); ff_stdin_ = nullptr; }
			if (ff_proc_) { CloseHandle(ff_proc_); ff_proc_ = nullptr; }
			if (ff_log_) { CloseHandle(ff_log_); ff_log_ = nullptr; }
		}

		bool start_capture()
		{
			if (!latch_resolution()) return false;

			const std::string exe = g_ffmpeg_path;
			const auto& codec = kCodecs[codec_index()];
			// ProRes must live in .mov; the delivery codecs use the avi_container dvar.
			const std::string ext = codec.force_ext ? std::string(codec.force_ext)
				: std::string(kContainers[container_index()]);
			out_path_ = next_output_path(ext);

			// pipe: rd = ffmpeg's stdin, wr = our write end (not inherited)
			SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
			HANDLE rd = nullptr, wr = nullptr;
			if (!CreatePipe(&rd, &wr, &sa, 1 << 20))
			{
				console::error("[avidemo] CreatePipe failed (%lu)\n", GetLastError());
				return false;
			}
			SetHandleInformation(wr, HANDLE_FLAG_INHERIT, 0);

			// ffmpeg stderr/stdout -> log file (keeps its verbose output off the game console)
			std::error_code ec;
			const auto log_path = (avi_dir() / "ffmpeg_last.log").string();
			ff_log_ = CreateFileA(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

			std::string cmd = build_ffmpeg_cmd(exe);

			STARTUPINFOA si{}; si.cb = sizeof(si);
			si.dwFlags = STARTF_USESTDHANDLES;
			si.hStdInput = rd;
			si.hStdOutput = (ff_log_ != INVALID_HANDLE_VALUE) ? ff_log_ : nullptr;
			si.hStdError = (ff_log_ != INVALID_HANDLE_VALUE) ? ff_log_ : nullptr;
			PROCESS_INFORMATION pi{};

			std::vector<char> cmdline(cmd.begin(), cmd.end());
			cmdline.push_back('\0');

			const BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr,
				TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
			CloseHandle(rd); // ffmpeg owns its read end now

			if (!ok)
			{
				const auto err = GetLastError();
				CloseHandle(wr);
				if (ff_log_ && ff_log_ != INVALID_HANDLE_VALUE) { CloseHandle(ff_log_); ff_log_ = nullptr; }
				console::error("[avidemo] failed to launch ffmpeg (err %lu). Set 'avi_ffmpeg' to your ffmpeg.exe path.\n", err);
				console::error("[avidemo]   tried: %s\n", cmd.c_str());
				return false;
			}
			CloseHandle(pi.hThread);
			ff_proc_ = pi.hProcess;
			ff_stdin_ = wr;

			frame_buf_.assign(static_cast<size_t>(cap_w_) * cap_h_ * 3 + 64, 0);
			frames_ = 0;
			capturing_ = true;
			console::info("[avidemo] recording %ix%i @ %d fps -> %s\n",
				cap_w_, cap_h_, std::clamp(dvar_int(avi_fps, 250), 1, 1000), out_path_.c_str());
			return true;
		}

		void finish_capture(bool aborted)
		{
			if (!capturing_) return;
			capturing_ = false;

			if (ff_stdin_) { CloseHandle(ff_stdin_); ff_stdin_ = nullptr; } // EOF -> ffmpeg finalizes
			if (ff_proc_)
			{
				WaitForSingleObject(ff_proc_, 30000);
			}
			const int n = frames_.load();
			close_handles();
			console::info("[avidemo] %s: %d frames -> %s\n",
				aborted ? "aborted" : "done", n, out_path_.c_str());
		}

		// write one framebuffer to the ffmpeg pipe. Returns false if the pipe broke.
		bool write_frame()
		{
			const size_t bytes = static_cast<size_t>(cap_w_) * cap_h_ * 3;
			if (frame_buf_.size() < bytes) frame_buf_.resize(bytes + 64);

			if (!game::R_TakeScreenshot(0, 1, 0, 0, cap_w_, cap_h_, 3, frame_buf_.data()))
			{
				return true; // couldn't grab this frame; skip it, keep going
			}

			const std::uint8_t* p = frame_buf_.data();
			size_t left = bytes;
			while (left)
			{
				DWORD wrote = 0;
				if (!WriteFile(ff_stdin_, p, static_cast<DWORD>(left), &wrote, nullptr) || wrote == 0)
				{
					console::error("[avidemo] ffmpeg pipe closed early (encoder error?) - see demos/avi/ffmpeg_last.log\n");
					return false;
				}
				p += wrote; left -= wrote;
			}
			++frames_;
			return true;
		}

		// runs once per rendered frame (R_EndFrame)
		void on_render_frame()
		{
			if (!armed_.load()) return;

			if (!demo_playback::playing())
			{
				if (capturing_) { finish_capture(true); armed_ = false; }
				return;
			}
			if (demo_playback::paused()) return; // frozen frame, nothing new to capture

			const auto ct = demo_playback::current_time();
			if (!ct) return;
			const int t = *ct;
			const int in = in_point_.load(), out = out_point_.load();
			if (in == UNSET || out == UNSET) return;

			if (!capturing_)
			{
				if (t < in || t >= out) return;        // not in range yet
				if (!start_capture()) { armed_ = false; return; }
			}

			if (t >= out)
			{
				finish_capture(false);
				armed_ = false;                        // one-shot: disarm after the range
				return;
			}

			if (!write_frame())
			{
				finish_capture(true);
				armed_ = false;
			}
		}
	}

	// ---- in/out range ----------------------------------------------------------
	void set_in_point(int demo_time) { in_point_ = demo_time; }
	void set_out_point(int demo_time) { out_point_ = demo_time; }
	void clear_range() { in_point_ = UNSET; out_point_ = UNSET; }
	bool has_in() { return in_point_.load() != UNSET; }
	bool has_out() { return out_point_.load() != UNSET; }
	int in_point() { return in_point_.load(); }
	int out_point() { return out_point_.load(); }
	bool has_range()
	{
		const int in = in_point_.load(), out = out_point_.load();
		return in != UNSET && out != UNSET && out > in;
	}

	// ---- control ---------------------------------------------------------------
	bool arm()
	{
		if (!demo_playback::playing())
		{
			console::error("[avidemo] no demo playing\n");
			return false;
		}
		if (!has_range())
		{
			console::error("[avidemo] set both AVI In and AVI Out markers first (Out must be after In)\n");
			return false;
		}
		frames_ = 0;
		armed_ = true;
		console::info("[avidemo] armed - play through the range to capture\n");
		return true;
	}

	void disarm()
	{
		armed_ = false;
		if (capturing_) finish_capture(true);
	}

	bool is_armed() { return armed_.load(); }
	bool is_capturing() { return capturing_.load(); }
	int captured_frames() { return frames_.load(); }
	int capture_fps() { return std::clamp(dvar_int(avi_fps, 250), 1, 1000); }

	const char* ffmpeg_path() { return g_ffmpeg_path.c_str(); }
	void set_ffmpeg_path(const char* path)
	{
		g_ffmpeg_path = (path && *path) ? path : "ffmpeg";
		std::error_code ec;
		std::ofstream out(ffmpeg_path_file(), std::ios::trunc);
		if (out) out << g_ffmpeg_path;
	}

	// ---- Com_TimeScaleMsec bridge ----------------------------------------------
	bool fixed_timestep_msec(int& out_msec)
	{
		if (!capturing_.load()) return false;
		if (demo_playback::paused()) return false; // hold the clock while paused (matches the capture-skip)
		const int fps = capture_fps();
		static double accum = 0.0;
		accum += 1000.0 / static_cast<double>(fps);
		int ms = static_cast<int>(accum);
		accum -= ms;
		if (ms < 1) ms = 1; // never 0 (that would pause the demo clock)
		out_msec = ms;
		return true;
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

			// int/bool dvars only (string dvars leak the script-string pool on MWR -> menu crash)
			avi_fps = dvars::register_int("avi_fps", 250, 1, 1000, game::DVAR_FLAG_SAVED,
				"avidemo capture rate (frames sampled per demo-second); higher = smoother + more slow-mo headroom");
			avi_out_fps = dvars::register_int("avi_out_fps", 60, 1, 1000, game::DVAR_FLAG_SAVED,
				"avidemo video playback fps; slow-mo factor = avi_fps / avi_out_fps (set equal to avi_fps for "
				"1:1 all-frames capture to retime in an editor, IWXMVM-style)");
			avi_codec = dvars::register_int("avi_codec", kCodecDefault, 0, static_cast<int>(std::size(kCodecs)) - 1,
				game::DVAR_FLAG_SAVED, "avidemo codec: 0=av1, 1=h265, 2=h264 (delivery, small), "
				"3=prores4444 (editing-grade 10-bit 4:4:4, forces .mov, HUGE files)");
			avi_crf = dvars::register_int("avi_crf", 23, 0, 63, game::DVAR_FLAG_SAVED,
				"avidemo quality (CRF) for delivery codecs: lower = better / bigger (ignored by prores4444)");
			avi_container = dvars::register_int("avi_container", kContainerDefault, 0, static_cast<int>(std::size(kContainers)) - 1,
				game::DVAR_FLAG_SAVED, "avidemo output container: 0 = mp4, 1 = mkv");
			avi_flip = dvars::register_bool("avi_flip", false, game::DVAR_FLAG_SAVED,
				"vertically flip captured frames (toggle if the video comes out upside-down)");

			// ffmpeg path: loaded from demos/avi/ffmpeg_path.txt (NOT a dvar). Also a console command.
			load_ffmpeg_path();
			command::add("avi_ffmpeg", [](const command::params& params)
			{
				if (params.size() < 2)
				{
					console::info("[avidemo] ffmpeg path: %s\n", g_ffmpeg_path.c_str());
					return;
				}
				set_ffmpeg_path(params.get(1));
				console::info("[avidemo] ffmpeg path set to: %s\n", g_ffmpeg_path.c_str());
			});

			// capture at R_EndFrame, once per rendered frame
			scheduler::loop(on_render_frame, scheduler::renderer);
		}

		void pre_destroy() override
		{
			disarm();
		}
	};
}

REGISTER_COMPONENT(demo_avidemo::component)
