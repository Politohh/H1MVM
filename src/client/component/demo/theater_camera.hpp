// =============================================================================
//  component/demo/theater_camera.hpp
//
//  In-game "theater" camera for demo playback (freecam + dolly). Self-contained:
//  reuses s1-mod's camera-APPLICATION idea (override the render camera each frame)
//  but drives it entirely with in-game input (no external Blender bridge).
//
//  Modes (demo_camera dvar): first_person | third_person | freecam | dolly.
//   * freecam: fly the camera with WASD + mouse-look during playback.
//   * dolly  : interpolate a smooth camera path through captured keyframes,
//              synced to the demo's own serverTime clock.
// =============================================================================
#pragma once

#include <vector>
#include <cstdint>

namespace theater_camera
{
	enum camera_mode
	{
		THEATER_CAMERA_FIRST_PERSON = 0,
		THEATER_CAMERA_THIRD_PERSON = 1,
		THEATER_CAMERA_FREECAM = 2,
		THEATER_CAMERA_DOLLY = 3,
	};

	// A full camera pose. angles are Quake euler [pitch, yaw, roll] in degrees;
	// fov is the horizontal field of view in degrees.
	struct camera_pose_t
	{
		float pos[3];
		float angles[3];
		float fov;
	};

	// A dolly path node: a pose anchored to a demo serverTime.
	struct camera_keyframe_t
	{
		std::int32_t demo_time;
		camera_pose_t pose;
	};

	[[nodiscard]] camera_mode get_mode();
	void set_mode(camera_mode mode);
	// HUD choice is independent of camera mode and restored when playback ends.
	void toggle_hud();
	[[nodiscard]] bool is_hud_hidden();

	// ---- dolly keyframe editing (used by the F9 GUI + console commands) --------
	// Snapshot the CURRENT render camera pose at the current demo time as a new
	// keyframe (kept sorted by demo_time). Returns false if not playing.
	bool capture_keyframe();
	void clear_keyframes();
	void remove_keyframe(std::size_t index);
	[[nodiscard]] const std::vector<camera_keyframe_t>& get_keyframes();

	// ---- keyframe editing (used by the timeline/marker editor) ------------------
	[[nodiscard]] camera_pose_t get_freecam_pose();
	void set_freecam_pose(const camera_pose_t& pose);
	void update_keyframe_pose(std::size_t index, const camera_pose_t& pose);
	std::size_t set_keyframe_time(std::size_t index, int new_time); // retimes + resorts, returns new index
	// insert a new node at an explicit demo_time with the given pose (kept sorted). nodes after
	// it shift up one, so inserting between #4 and #5 makes this the new #5. returns its index.
	std::size_t insert_keyframe(int demo_time, const camera_pose_t& pose);

	// current freecam field of view (degrees), for the GUI to display/edit.
	[[nodiscard]] float get_freecam_fov();
	void set_freecam_fov(float fov);

	// Mouse-wheel input while flying the freecam: wheel = camera roll, Shift+wheel = FOV. The
	// wheel comes in as K_MWHEELUP/DOWN key events, so the input hook forwards them here.
	// Both roll + FOV are captured per-marker, so they're dynamic along the dolly path.
	// Returns true if the wheel was consumed.
	bool handle_wheel(int key);

	// ---- cinematic playback ----------------------------------------------------
	// Roll the camera along the whole spline path on its own timescale-scaled clock
	// (needs >=2 markers). Always completes; no demo seek.
	bool start_cinematic();
	void stop_cinematic();
	[[nodiscard]] bool is_cinematic_playing();

	// Interpolated (Catmull-Rom) camera pose at a marker time -- lets the GUI draw the
	// smooth path curve, not just straight segments between markers.
	[[nodiscard]] camera_pose_t sample_pose_at(int demo_time);

	// ---- overlay helpers (used by demo_gui) ------------------------------------
	// The F9 overlay sets this while open so the freecam fully freezes (mouse + keys),
	// leaving the cursor free for the UI and the camera still for a clean capture.
	void set_input_blocked(bool blocked);

	// Project a world position using the exact view the scene rendered with this frame
	// (no 1-frame lag = no marker jitter). out_screen = px, out_radius = distance-scaled
	// marker radius (may be null). Returns false when behind the camera.
	[[nodiscard]] bool project_marker(const float* world, float* out_screen, float* out_radius);

	// The cached render-view eye position this frame (same source as project_marker).
	// Returns false until the first view has been cached.
	[[nodiscard]] bool get_view_origin(float* out);
}
