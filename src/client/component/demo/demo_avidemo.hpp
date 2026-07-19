// =============================================================================
//  component/demo/demo_avidemo.hpp
//
//  "avidemo"-style movie capture for demo playback (the modern CoD4 frag-movie
//  workflow). Instead of slowing the HUD clock, we DECOUPLE game time from real
//  time: while capturing, every rendered frame advances the demo clock by a fixed
//  1000/fps ms (via the Com_TimeScaleMsec hook), and we read the framebuffer back
//  and stream it straight into an ffmpeg subprocess that encodes a single video
//  file. Capture as fast/slow as the disk allows -> perfectly smooth output where
//  HUD, animation and particles are all correct, because every frame is a real
//  fixed-timestep game frame.
//
//  The capture range is an [in, out] pair of demo serverTimes ("AVI In/Out"
//  markers on the timeline). Arm it, play through the range, get a .mp4/.mkv.
// =============================================================================
#pragma once

#include <cstdint>

namespace demo_avidemo
{
	// ---- in/out capture range (demo serverTimes; INT32_MIN = unset) ------------
	void set_in_point(int demo_time);
	void set_out_point(int demo_time);
	void clear_range();
	[[nodiscard]] bool has_in();
	[[nodiscard]] bool has_out();
	[[nodiscard]] int in_point();   // INT32_MIN if unset
	[[nodiscard]] int out_point();  // INT32_MIN if unset
	[[nodiscard]] bool has_range(); // both set and out > in

	// ---- capture control -------------------------------------------------------
	// Arm capture: from now until disarmed, when the demo clock enters [in,out]
	// we spawn ffmpeg and grab a frame every rendered frame; at out (or on
	// stop/disarm) we close the pipe and finalize the file. Returns false if the
	// range/ffmpeg/output can't be set up (reason is printed to console).
	bool arm();
	void disarm(); // also aborts an in-progress capture (finalizes the partial file)

	[[nodiscard]] bool is_armed();
	[[nodiscard]] bool is_capturing();     // ffmpeg running, frames flowing
	[[nodiscard]] int captured_frames();   // frames written to the current/last capture
	[[nodiscard]] int capture_fps();       // configured capture rate (avi_fps)

	// ---- ffmpeg path (a plain module string, NOT a dvar) -----------------------
	// MWR string dvars intern their value into the script-string pool and leak it on
	// SAVED config re-execs (-> the MT_AllocIndex menu crash), so the ffmpeg path is
	// kept out of the dvar system entirely and persisted to demos/avi/ffmpeg_path.txt.
	[[nodiscard]] const char* ffmpeg_path();
	void set_ffmpeg_path(const char* path);

	// ---- Com_TimeScaleMsec bridge (used by demo_timescale.cpp) -----------------
	// While capturing, returns true and yields the fixed per-frame advance in ms
	// (with a fractional accumulator so e.g. 600fps -> 1.667ms averages exactly).
	[[nodiscard]] bool fixed_timestep_msec(int& out_msec);
}
