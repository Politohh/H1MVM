#include "std_include.hpp"
#include "loader/component_loader.hpp"

#include "theater_camera.hpp"
#include "cod4_camera.hpp"
#include "demo_game.hpp"     // game::refdef, game::CG_CalcViewValues, game::AxisToAngles, demo_compat::register_enum
#include "demo_playback.hpp" // demo_playback::playing()/is_replay_armed()/current_time()

#include "component/command.hpp"
#include "component/console.hpp"
#include "utils/hook.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>

// In-game freecam + dolly camera for demo playback. See theater_camera.hpp.
//
// Approach (self-contained, no external Blender bridge): hook CG_CalcViewValues and,
// once it has computed the normal view, OVERWRITE the render camera
// (*game::refdef)->view (origin + axis + fov) with our freecam/dolly pose. This is the
// same application idea Airyzz/s1-mod used; the input + keyframing here are original
// (s1-mod sourced those from a Blender addon over UDP, which we deliberately don't need).

namespace theater_camera
{
	namespace
	{
		constexpr float PI = 3.14159265358979323846f;
		constexpr float DEG2RAD = PI / 180.0f;
		constexpr float RAD2DEG = 180.0f / PI;

		game::dvar_t* demo_camera = nullptr;      // enum: first/third/freecam/dolly
		game::dvar_t* demo_camera_speed = nullptr; // freecam units/second
		game::dvar_t* demo_camera_sens = nullptr;  // mouse-look sensitivity
		game::dvar_t* demo_cod4_fov = nullptr;     // fov for CoD4 first-person playback
		game::dvar_t* demo_camera_boost{}, *demo_camera_slow{};
		std::array<game::dvar_t*, 3> dolly_position{}, dolly_rotation{};

		const char* mode_names[] = { "first_person", "third_person", "freecam", "dolly", nullptr };

		utils::hook::detour cg_calc_view_values_hook;
		utils::hook::detour in_mouse_move_hook;
		utils::hook::detour r_setup_lod_params_hook;

		// ---- freecam state -------------------------------------------------------
		bool freecam_seeded = false;               // seed pose from live view on first freecam frame
		float freecam_pos[3] = {};
		float freecam_angles[3] = {};              // [pitch, yaw, roll] degrees
		float freecam_fov = 65.0f;

		int last_update_ms = 0;                    // for frame-time-independent movement

		// raw mouse delta mirrored from the game's own WM_MOUSEMOVE accumulator (see
		// in_mouse_move_stub); consumed each frame by update_freecam. This gives smooth,
		// real mouse movement -- the old GetCursorPos/recenter approach fought the game's
		// own recentering and jittered.
		int g_mouse_dx = 0;
		int g_mouse_dy = 0;

		// set by the F9 overlay while it's open: freezes ALL freecam input (mouse + keys)
		// so the mouse only drives the UI cursor and the camera holds still for a clean
		// capture. Uses an explicit flag rather than keyCatchers, which the game clears
		// each frame during demo playback (why the camera still turned with F9 open).
		bool input_blocked = false;
		bool prev_capture_key = false;             // middle-mouse edge for the drop-marker hotkey

		std::vector<camera_keyframe_t> keyframes;

		camera_mode g_last_mode = THEATER_CAMERA_FIRST_PERSON;
		int saved_thirdperson = -1;                // prior cg_thirdPerson value to restore

		// HUD toggle dvars saved/restored around the movie modes. MWR's HUD is LUI, so
		// cg_draw2D alone (the old 2D layer) doesn't hide it -- hud_enable/hud_drawHud do.
		const char* const hud_dvar_names[] = { "hud_enable", "hud_drawHud", "cg_draw2D" };
		int saved_hud[3] = { -1, -1, -1 };
		int saved_cheats = -1;                     // sv_cheats original (cg_draw2D is cheat-locked)

		// The exact render view applied THIS frame (cached at the end of the CG_CalcView
		// hook). The marker overlay projects from this, not a live refdef read, so markers
		// stay locked to the world with no 1-frame projection lag (the jitter).
		bool g_view_valid = false;
		float g_view_org[3] = {};
		float g_view_axis[3][3] = {};
		float g_view_thfx = 1.0f;
		float g_view_thfy = 1.0f;

		// =========================================================================
		//  math (reimplemented so we need no game math addresses)
		// =========================================================================
		// Quake AngleVectors: euler [pitch,yaw,roll] deg -> forward/right/up.
		void angle_vectors(const float* angles, float* forward, float* right, float* up)
		{
			const float sy = std::sin(angles[1] * DEG2RAD), cy = std::cos(angles[1] * DEG2RAD);
			const float sp = std::sin(angles[0] * DEG2RAD), cp = std::cos(angles[0] * DEG2RAD);
			const float sr = std::sin(angles[2] * DEG2RAD), cr = std::cos(angles[2] * DEG2RAD);

			if (forward)
			{
				forward[0] = cp * cy;
				forward[1] = cp * sy;
				forward[2] = -sp;
			}
			if (right)
			{
				right[0] = -sr * sp * cy + cr * sy;
				right[1] = -sr * sp * sy - cr * cy;
				right[2] = -sr * cp;
			}
			if (up)
			{
				up[0] = cr * sp * cy + sr * sy;
				up[1] = cr * sp * sy - sr * cy;
				up[2] = cr * cp;
			}
		}

		// CoD AnglesToAxis: axis[0]=forward, axis[1]=-right (left), axis[2]=up.
		void angles_to_axis(const float* angles, float axis[3][3])
		{
			float forward[3], right[3], up[3];
			angle_vectors(angles, forward, right, up);
			for (int i = 0; i < 3; ++i)
			{
				axis[0][i] = forward[i];
				axis[1][i] = -right[i];
				axis[2][i] = up[i];
			}
		}

		float normalize_angle(float a)
		{
			while (a > 180.0f) a -= 360.0f;
			while (a < -180.0f) a += 360.0f;
			return a;
		}

		// =========================================================================
		//  render-camera access
		// =========================================================================
		game::mp::refdef_t* get_refdef()
		{
			return *game::refdef; // global holds the active render refdef_t* (see h1-mod xmodel.cpp)
		}

		// read the current render pose (used to seed freecam + capture keyframes)
		bool read_current_pose(camera_pose_t& out)
		{
			auto* rd = get_refdef();
			if (!rd)
			{
				return false;
			}

			for (int i = 0; i < 3; ++i)
			{
				out.pos[i] = rd->view.org[i];
			}
			game::AxisToAngles(rd->view.axis, out.angles); // game's inverse of angles_to_axis
			out.fov = 2.0f * std::atan(rd->view.tanHalfFovX) * RAD2DEG;
			return true;
		}

		void apply_pose_to_view(const camera_pose_t& pose, game::mp::RefdefView& view)
		{
			for (int i = 0; i < 3; ++i)
			{
				view.org[i] = pose.pos[i];
			}
			angles_to_axis(pose.angles, view.axis);

			// preserve aspect (tanHalfFovX / tanHalfFovY) while setting horizontal fov
			const float aspect = (view.tanHalfFovY != 0.0f)
				? (view.tanHalfFovX / view.tanHalfFovY)
				: (16.0f / 9.0f);
			const float thf = std::tan(std::clamp(pose.fov, 1.0f, 179.0f) * 0.5f * DEG2RAD);
			view.tanHalfFovX = thf;
			view.tanHalfFovY = thf / aspect;
		}

		void apply_pose_to_refdef(const camera_pose_t& pose)
		{
			if (auto* rd = get_refdef(); rd) apply_pose_to_view(pose, rd->view);
		}

		// =========================================================================
		//  input (freecam) -- Win32 only, no game internals required
		// =========================================================================
		HWND game_window()
		{
			return *reinterpret_cast<HWND*>(game::base_address + 0xC9DD2E0);
		}

		bool game_has_focus()
		{
			const auto hwnd = game_window();
			return hwnd && GetForegroundWindow() == hwnd;
		}

		// true when the freecam may consume input: the F9 overlay isn't open, no menu/console
		// is up, and the game window has focus. input_blocked (set by the overlay) is the
		// reliable gate -- keyCatchers alone isn't, because the game zeroes it every frame
		// during demo playback, which let the camera keep turning with F9 open.
		bool input_available()
		{
			return !input_blocked && *game::keyCatchers == 0 && game_has_focus();
		}

		bool key_down(int vk)
		{
			return (GetAsyncKeyState(vk) & 0x8000) != 0;
		}

		// Hook the game's WM_MOUSEMOVE delta accumulator (sub_5BA950) and mirror the exact
		// same per-frame delta the game computes into our own accumulator. The game's own
		// mouse pipeline (clip + recenter, consumed via CL_MouseMove) keeps running normally,
		// so the cursor never drifts to a screen edge and we get its smooth, real delta with
		// no GetCursorPos/recenter fight (the old jitter). Only taps while the freecam owns
		// input (playing + armed + freecam mode + no menu/overlay catching keys).
		void in_mouse_move_stub(int x, int y)
		{
			if (demo_playback::playing() && demo_playback::is_replay_armed()
				&& demo_camera && demo_camera->current.integer == THEATER_CAMERA_FREECAM
				&& input_available())
			{
				const int last_x = *reinterpret_cast<int*>(game::base_address + 0xC9DC3F4);
				const int last_y = *reinterpret_cast<int*>(game::base_address + 0xC9DC3F8);
				if (last_x != -100000) // the game's recenter sentinel -> skip the teleport jump
				{
					g_mouse_dx += x - last_x;
					g_mouse_dy += y - last_y;
				}
			}

			in_mouse_move_hook.invoke<void>(x, y);
		}

		void update_freecam()
		{
			// frame-time-independent step
			const int now = game::Sys_Milliseconds();
			float dt = (last_update_ms != 0) ? (now - last_update_ms) / 1000.0f : 0.0f;
			last_update_ms = now;
			dt = std::clamp(dt, 0.0f, 0.1f);

			if (!input_available())
			{
				g_mouse_dx = 0; // drop any delta captured while input was unavailable
				g_mouse_dy = 0;
				return;
			}

			// ---- look: real game mouse delta (tapped in in_mouse_move_stub) + arrow keys ----
			const float sens = demo_camera_sens ? demo_camera_sens->current.value : 0.06f;
			freecam_angles[1] -= g_mouse_dx * sens;                                            // yaw
			freecam_angles[0] = std::clamp(freecam_angles[0] + g_mouse_dy * sens, -89.0f, 89.0f); // pitch
			g_mouse_dx = 0;
			g_mouse_dy = 0;

			const float key_look = 90.0f * dt; // deg/sec for arrow-key look
			if (key_down(VK_LEFT))  freecam_angles[1] += key_look;
			if (key_down(VK_RIGHT)) freecam_angles[1] -= key_look;
			if (key_down(VK_UP))    freecam_angles[0] = std::clamp(freecam_angles[0] - key_look, -89.0f, 89.0f);
			if (key_down(VK_DOWN))  freecam_angles[0] = std::clamp(freecam_angles[0] + key_look, -89.0f, 89.0f);

			// ---- movement ----
			float forward[3], right[3], up[3];
			angle_vectors(freecam_angles, forward, right, up);

			float speed = (demo_camera_speed ? demo_camera_speed->current.value : 500.0f) * dt;
			if (key_down(VK_SHIFT)) speed *= demo_camera_boost ? demo_camera_boost->current.value : 4.0f;
			if (key_down(VK_MENU)) speed *= demo_camera_slow ? demo_camera_slow->current.value : .25f;

			auto add = [&](const float* v, float s)
			{
				freecam_pos[0] += v[0] * s;
				freecam_pos[1] += v[1] * s;
				freecam_pos[2] += v[2] * s;
			};

			if (key_down('W')) add(forward, speed);
			if (key_down('S')) add(forward, -speed);
			if (key_down('D')) add(right, speed);
			if (key_down('A')) add(right, -speed);
			// vertical is on E/Q (+ Ctrl for down) -- NOT Space, so Space is free to pause
			// the demo without also flying the camera up (the old double-binding).
			if (key_down('E'))                             freecam_pos[2] += speed; // up
			if (key_down('Q') || key_down(VK_CONTROL))     freecam_pos[2] -= speed; // down

			// ---- fov (=/- keys) ----
			if (key_down(VK_OEM_PLUS))  freecam_fov = std::clamp(freecam_fov - 60.0f * dt, 1.0f, 179.0f);
			if (key_down(VK_OEM_MINUS)) freecam_fov = std::clamp(freecam_fov + 60.0f * dt, 1.0f, 179.0f);
		}

		// =========================================================================
		//  dolly interpolation (smooth Catmull-Rom spline through the keyframes)
		// =========================================================================
		// Uniform Catmull-Rom: C1-continuous, passes through every control point with a
		// smooth (continuous-velocity) tangent -> no kinks/speed-pops at the markers.
		float catmull_rom(float p0, float p1, float p2, float p3, float t)
		{
			const float t2 = t * t;
			const float t3 = t2 * t;
			return 0.5f * (2.0f * p1
				+ (-p0 + p2) * t
				+ (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
				+ (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
		}

		// bring b onto the same continuous branch as ref (no 360-degree seam jumps)
		float unwrap_angle(float ref, float b)
		{
			return ref + normalize_angle(b - ref);
		}

		camera_pose_t sample_dolly_base(int time)
		{
			const auto n = keyframes.size();
			if (n == 0)
			{
				camera_pose_t p{};
				p.fov = freecam_fov;
				return p;
			}
			if (n == 1 || time <= keyframes.front().demo_time)
			{
				return keyframes.front().pose;
			}
			if (time >= keyframes.back().demo_time)
			{
				return keyframes.back().pose;
			}

			// find the segment [i, i+1] that contains time
			std::size_t i = 0;
			for (; i + 1 < n; ++i)
			{
				if (time >= keyframes[i].demo_time && time < keyframes[i + 1].demo_time)
				{
					break;
				}
			}

			const auto& k1 = keyframes[i].pose;
			const auto& k2 = keyframes[i + 1].pose;
			const auto& k0 = keyframes[i > 0 ? i - 1 : i].pose;          // duplicate the ends
			const auto& k3 = keyframes[i + 2 < n ? i + 2 : i + 1].pose;

			const float span = static_cast<float>(keyframes[i + 1].demo_time - keyframes[i].demo_time);
			const float t = (span > 0.0f) ? std::clamp((time - keyframes[i].demo_time) / span, 0.0f, 1.0f) : 0.0f;

			camera_pose_t r{};
			for (int k = 0; k < 3; ++k)
			{
				r.pos[k] = catmull_rom(k0.pos[k], k1.pos[k], k2.pos[k], k3.pos[k], t);
			}
			for (int k = 0; k < 3; ++k)
			{
				const float a1 = k1.angles[k];
				const float a0 = unwrap_angle(a1, k0.angles[k]);
				const float a2 = unwrap_angle(a1, k2.angles[k]);
				const float a3 = unwrap_angle(a2, k3.angles[k]);
				r.angles[k] = normalize_angle(catmull_rom(a0, a1, a2, a3, t));
			}
			r.fov = catmull_rom(k0.fov, k1.fov, k2.fov, k3.fov, t);
			return r;
		}

		camera_pose_t sample_dolly(int time)
		{
			auto pose = sample_dolly_base(time);
			for (int axis = 0; axis < 3; ++axis)
			{
				if (dolly_position[axis]) pose.pos[axis] += dolly_position[axis]->current.value;
				if (dolly_rotation[axis]) pose.angles[axis] = normalize_angle(pose.angles[axis] + dolly_rotation[axis]->current.value);
			}
			return pose;
		}

		// Smooth per-frame playback clock: cl->serverTime advances every render frame,
		// unlike demo_playback::current_time() (= snap.serverTime) which steps at the ~20Hz
		// snapshot rate -> that stepping was the bulk of the "unsmooth" dolly. Same demo
		// timeline the keyframes are captured on.
		std::optional<int> playback_clock()
		{
			if (auto* c = game::mp::cl.get(); c)
			{
				return c->serverTime;
			}
			return std::nullopt;
		}

		void r_setup_lod_params_stub(const game::mp::RefdefView* view, void* params, float scale)
		{
			// CG_DrawActive builds dynamic-model LOD parameters before it calls
			// CG_CalcViewValues. Replacing only the later render view leaves these
			// distances anchored to the recorded player (notably for grenades).
			// Both scene-model and static-model builders use this native helper.
			if (view && demo_camera && demo_playback::playing() && demo_playback::is_replay_armed())
			{
				const auto mode = static_cast<camera_mode>(demo_camera->current.integer);
				std::optional<camera_pose_t> pose;
				if (mode == THEATER_CAMERA_FREECAM && freecam_seeded && g_last_mode == mode)
				{
					camera_pose_t flying{};
					for (int i = 0; i < 3; ++i) { flying.pos[i] = freecam_pos[i]; flying.angles[i] = freecam_angles[i]; }
					flying.fov = freecam_fov;
					pose = flying;
				}
				else if (mode == THEATER_CAMERA_DOLLY && !keyframes.empty())
				{
					if (const auto t = playback_clock(); t) pose = sample_dolly(*t);
				}
				if (pose)
				{
					auto camera_view = *view;
					apply_pose_to_view(*pose, camera_view);
					r_setup_lod_params_hook.invoke<void>(&camera_view, params, scale);
					return;
				}
			}
			r_setup_lod_params_hook.invoke<void>(view, params, scale);
		}

		// Snapshot the current render camera pose as a marker at the current demo time
		// (kept sorted; a capture within 50ms of an existing marker replaces it).
		bool capture_marker_internal()
		{
			if (!demo_playback::playing() || !demo_playback::is_replay_armed())
			{
				return false;
			}
			const auto t = playback_clock();
			if (!t)
			{
				return false;
			}
			camera_pose_t pose{};
			if (demo_camera && demo_camera->current.integer == THEATER_CAMERA_FREECAM)
			{
				// Capture the freecam pose DIRECTLY. Reading the render view here is wrong:
				// on the middle-click path capture runs before the hook applies the freecam to
				// the refdef that frame, so the view still holds the game's PLAYER camera --
				// which is why markers landed on the player instead of the freecam.
				for (int i = 0; i < 3; ++i) { pose.pos[i] = freecam_pos[i]; pose.angles[i] = freecam_angles[i]; }
				pose.fov = freecam_fov;
			}
			else if (!read_current_pose(pose))
			{
				// first/third-person: the game's view is the camera, so read it
				return false;
			}

			const camera_keyframe_t kf{ *t, pose };
			const auto it = std::lower_bound(keyframes.begin(), keyframes.end(), kf.demo_time,
				[](const camera_keyframe_t& k, std::int32_t time) { return k.demo_time < time; });

			if (it != keyframes.end() && std::abs(it->demo_time - kf.demo_time) < 50)
			{
				*it = kf;
			}
			else
			{
				keyframes.insert(it, kf);
			}
			console::info("[theater] marker captured (%zu total)\n", keyframes.size());
			return true;
		}

		// =========================================================================
		//  third-person management (so the local recorded player's body renders)
		// =========================================================================
		void set_thirdperson(bool on)
		{
			auto* cg_tp = game::Dvar_FindVar("cg_thirdPerson");
			if (!cg_tp)
			{
				return;
			}

			if (on)
			{
				if (saved_thirdperson < 0)
				{
					saved_thirdperson = cg_tp->current.enabled ? 1 : 0;
				}
				cg_tp->current.enabled = true;
			}
			else if (saved_thirdperson >= 0)
			{
				cg_tp->current.enabled = (saved_thirdperson != 0);
				saved_thirdperson = -1;
			}
		}

		// hide/show the HUD (LUI hud_enable/hud_drawHud + old cg_draw2D) so the freecam/
		// cinematic gets a clean frame. Called every frame (idempotent) so it re-hides even
		// if the game flips a dvar back; originals captured on the first hide, restored on show.
		void set_hud_hidden(bool hidden)
		{
			// cg_draw2D is cheat-protected: it only sticks while sv_cheats == 1, otherwise the
			// engine snaps it back to its default every frame. Force cheats on while hidden so
			// the HUD (and the menu it would otherwise draw) stays gone; restore it on show.
			if (auto* cheats = game::Dvar_FindVar("sv_cheats"))
			{
				if (hidden)
				{
					if (saved_cheats < 0) saved_cheats = cheats->current.enabled ? 1 : 0;
					cheats->current.enabled = true;
				}
				else if (saved_cheats >= 0)
				{
					cheats->current.enabled = (saved_cheats != 0);
					saved_cheats = -1;
				}
			}

			for (int i = 0; i < 3; ++i)
			{
				auto* d = game::Dvar_FindVar(hud_dvar_names[i]);
				if (!d)
				{
					continue;
				}

				if (hidden)
				{
					if (saved_hud[i] < 0)
					{
						saved_hud[i] = d->current.enabled ? 1 : 0;
					}
					d->current.enabled = false;
				}
				else if (saved_hud[i] >= 0)
				{
					d->current.enabled = (saved_hud[i] != 0);
					saved_hud[i] = -1;
				}
			}
		}

		void on_mode_changed(camera_mode mode)
		{
			// entering freecam: seed the flying pose from wherever the view currently is,
			// so the camera doesn't jump on the first frame.
			if (mode == THEATER_CAMERA_FREECAM)
			{
				camera_pose_t p{};
				if (read_current_pose(p))
				{
					for (int i = 0; i < 3; ++i) { freecam_pos[i] = p.pos[i]; freecam_angles[i] = p.angles[i]; }
					freecam_fov = p.fov;
				}
				freecam_seeded = true;
				last_update_ms = 0;
				g_mouse_dx = 0;
				g_mouse_dy = 0;
			}

			// third/freecam/dolly all want the player body visible; first-person doesn't.
			set_thirdperson(mode != THEATER_CAMERA_FIRST_PERSON);

			// hide the 2D HUD for the "movie" modes (freecam + dolly) for a clean frame;
			// keep it in first/third-person spectating.
			set_hud_hidden(mode == THEATER_CAMERA_FREECAM || mode == THEATER_CAMERA_DOLLY);
		}

		// =========================================================================
		//  the hook: override the render camera after CG_CalcViewValues computes it
		// =========================================================================
        std::uint64_t calc_view_values_stub(int local_client_num, void* a2)
        {
            if (demo_playback::converted() && !game::VirtualLobby_Loaded())
            {
                // Repair v1 converted files created with a zeroed linked-view
                // entity. Native 0xEA4B0 reads ps+0x2C and composes that entity's
                // rotation unless it is ENTITYNUM_NONE. These files have no
                // entity attachments; use the same sentinel as the converter.
                auto* ps = reinterpret_cast<std::uint8_t*>(game::CG_GetLocalClientGlobals());
                if (ps) *reinterpret_cast<std::uint16_t*>(ps + 0x2C) = 2047;
            }
			// H1 multiplies cg_fov/cg_fovScale by a gameplay omnvar. COD4 has no
			// corresponding override, and the synthetic snapshot's zero value clamps
			// the view to cg_fovMin. Supply a neutral multiplier only for this native
			// view calculation; preserve the user's dvars and the underlying snapshot.
			float* replay_fov_multiplier = nullptr;
			float saved_fov_multiplier{};
			if ((cod4_camera::active() || demo_playback::converted()) && !game::VirtualLobby_Loaded())
			{
				// Name and accessors are the same ones used at native CG_CalcFov
				// 0x1045C3..0x1045F4 (PC). OmnvarData is {time, value}, 8 bytes.
				const auto index = utils::hook::invoke<int>(0x5A44A0_b, reinterpret_cast<const char*>(0x8E1190_b));
				if (index >= 0)
				{
					auto* data = utils::hook::invoke<std::uint8_t*>(0x5A4250_b, local_client_num, index);
					replay_fov_multiplier = reinterpret_cast<float*>(data + 4);
					saved_fov_multiplier = *replay_fov_multiplier;
					*replay_fov_multiplier = 1.0f;
				}
			}
			const auto result = cg_calc_view_values_hook.invoke<std::uint64_t>(local_client_num, a2);
			if (replay_fov_multiplier) *replay_fov_multiplier = saved_fov_multiplier;

			camera_pose_t cod4_pose{};
			if (cod4_camera::view(cod4_pose))
			{
				set_thirdperson(false);
				set_hud_hidden(true);
				g_last_mode = THEATER_CAMERA_DOLLY; // restore saved dvars when the track stops
				if (auto* rd = get_refdef(); rd && rd->view.tanHalfFovY > 0)
				{
					// Keep the client's native FOV, including its configured scale and ADS.
					for (int i = 0; i < 3; ++i) rd->view.org[i] = cod4_pose.pos[i];
					angles_to_axis(cod4_pose.angles, rd->view.axis);
					cod4_camera::record_applied(rd->view.org, 2.0f * std::atan(rd->view.tanHalfFovX) * RAD2DEG);
				}
				return result;
			}

			if (!demo_playback::playing() || !demo_playback::is_replay_armed() || !demo_camera)
			{
				if (g_last_mode != THEATER_CAMERA_FIRST_PERSON)
				{
					set_thirdperson(false);
					set_hud_hidden(false);
					g_last_mode = THEATER_CAMERA_FIRST_PERSON;
				}

				if (auto* rd = get_refdef(); rd)
				{
					for (int i = 0; i < 3; ++i)
					{
						g_view_org[i] = rd->view.org[i];
						for (int j = 0; j < 3; ++j) g_view_axis[i][j] = rd->view.axis[i][j];
					}
					g_view_thfx = rd->view.tanHalfFovX;
					g_view_thfy = rd->view.tanHalfFovY;
					g_view_valid = true;
				}
				return result;
			}

			const auto mode = static_cast<camera_mode>(demo_camera->current.integer);
			if (mode != g_last_mode)
			{
				on_mode_changed(mode);
				g_last_mode = mode;
			}

			// keep the HUD hidden every frame while in a movie mode (freecam/dolly)
			set_hud_hidden(mode == THEATER_CAMERA_FREECAM || mode == THEATER_CAMERA_DOLLY);

			// J/K/L are handled once by the game key-event bridge, including while
			// native theater controls are open. Keep middle-mouse capture below.

			if (mode == THEATER_CAMERA_FREECAM)
			{
				update_freecam();

				// middle-mouse drops a marker while flying (edge-detected) -- no need to
				// open the F9 panel mid-flight.
				const bool cap = input_available() && (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
				if (cap && !prev_capture_key)
				{
					capture_marker_internal();
				}
				prev_capture_key = cap;

				camera_pose_t pose{};
				for (int i = 0; i < 3; ++i) { pose.pos[i] = freecam_pos[i]; pose.angles[i] = freecam_angles[i]; }
				pose.fov = freecam_fov;
				apply_pose_to_refdef(pose);
			}
			else if (mode == THEATER_CAMERA_DOLLY)
			{
				// Campath camera (IWXMVM model): interpolate the keyframes at the CURRENT demo
				// time, sampled per render frame (cl->serverTime) so it's smooth and stays
				// locked to the action. Playing/scrubbing the demo drives it; slow-mo works
				// for free because cl->serverTime is itself timescale-scaled by the engine.
				// Before the first / after the last keyframe it holds that end pose.
				if (const auto t = playback_clock(); t)
				{
					apply_pose_to_refdef(sample_dolly(*t));
				}
			}
			// first_person / third_person: leave the engine's computed view as-is
			// (third_person is driven by the cg_thirdPerson chase cam set in on_mode_changed).


			// Cache the exact view this frame renders with, so the marker overlay projects
			// from it (no 1-frame lag = no marker jitter).
			if (auto* rd = get_refdef(); rd)
			{
				for (int i = 0; i < 3; ++i)
				{
					g_view_org[i] = rd->view.org[i];
					for (int j = 0; j < 3; ++j) g_view_axis[i][j] = rd->view.axis[i][j];
				}
				g_view_thfx = rd->view.tanHalfFovX;
				g_view_thfy = rd->view.tanHalfFovY;
				g_view_valid = true;
			}

			return result;
		}
	}

	// =============================================================================
	//  public API (theater_camera.hpp)
	// =============================================================================
	camera_mode get_mode()
	{
		return demo_camera ? static_cast<camera_mode>(demo_camera->current.integer) : THEATER_CAMERA_FIRST_PERSON;
	}

	void set_mode(camera_mode mode)
	{
		if (demo_camera)
		{
			demo_camera->current.integer = static_cast<int>(mode);
		}
	}

	bool capture_keyframe()
	{
		return capture_marker_internal();
	}

	void clear_keyframes()
	{
		keyframes.clear();
		console::info("[theater] cleared dolly keyframes\n");
	}

	void remove_keyframe(std::size_t index)
	{
		if (index < keyframes.size())
		{
			keyframes.erase(keyframes.begin() + index);
		}
	}

	const std::vector<camera_keyframe_t>& get_keyframes()
	{
		return keyframes;
	}

	// current freecam pose (for the editor's "recapture" + display)
	camera_pose_t get_freecam_pose()
	{
		camera_pose_t p{};
		for (int i = 0; i < 3; ++i) { p.pos[i] = freecam_pos[i]; p.angles[i] = freecam_angles[i]; }
		p.fov = freecam_fov;
		return p;
	}

	void set_freecam_pose(const camera_pose_t& pose)
	{
		set_mode(THEATER_CAMERA_FREECAM);
		on_mode_changed(THEATER_CAMERA_FREECAM);
		g_last_mode = THEATER_CAMERA_FREECAM;
		for (int i = 0; i < 3; ++i) { freecam_pos[i] = pose.pos[i]; freecam_angles[i] = pose.angles[i]; }
		freecam_fov = std::clamp(pose.fov, 1.f, 179.f);
		freecam_seeded = true;
		g_mouse_dx = g_mouse_dy = 0;
	}

	// overwrite a keyframe's pose (position/rotation/fov), keeping its time
	void update_keyframe_pose(std::size_t index, const camera_pose_t& pose)
	{
		if (index < keyframes.size())
		{
			keyframes[index].pose = pose;
		}
	}

	// retime a keyframe and keep the list sorted; returns its new index
	std::size_t set_keyframe_time(std::size_t index, int new_time)
	{
		if (index >= keyframes.size())
		{
			return index;
		}
		auto kf = keyframes[index];
		kf.demo_time = new_time;
		keyframes.erase(keyframes.begin() + index);
		const auto it = std::lower_bound(keyframes.begin(), keyframes.end(), new_time,
			[](const camera_keyframe_t& k, int t) { return k.demo_time < t; });
		const auto new_index = static_cast<std::size_t>(it - keyframes.begin());
		keyframes.insert(it, kf);
		return new_index;
	}

	// insert a brand-new node at an explicit demo_time (kept sorted); returns its index.
	// nodes after it shift up by one, so inserting between #4 and #5 makes this the new #5.
	std::size_t insert_keyframe(int demo_time, const camera_pose_t& pose)
	{
		camera_keyframe_t kf{ demo_time, pose };
		auto it = std::lower_bound(keyframes.begin(), keyframes.end(), kf.demo_time,
			[](const camera_keyframe_t& k, int t) { return k.demo_time < t; });
		// don't land right on top of an existing node -- keep every marker independently retimable
		if (it != keyframes.end() && std::abs(it->demo_time - kf.demo_time) < 50)
		{
			kf.demo_time = it->demo_time + 50;
			++it;
		}
		it = keyframes.insert(it, kf);
		return static_cast<std::size_t>(it - keyframes.begin());
	}

	float get_freecam_fov() { return freecam_fov; }
	void set_freecam_fov(float fov) { freecam_fov = std::clamp(fov, 1.0f, 179.0f); }

	// Mouse wheel while flying the freecam: wheel = camera roll (Dutch tilt), Alt+wheel = FOV.
	// Both are part of the captured pose, so they interpolate along the dolly path = dynamic
	// roll + FOV per marker. The wheel arrives as K_MWHEELUP/DOWN key events (not pollable per
	// frame), so the input hook forwards them here. Returns true if consumed.
	bool handle_wheel(int key)
	{
		if (get_mode() != THEATER_CAMERA_FREECAM || !input_available())
		{
			return false;
		}
		if (key != game::K_MWHEELUP && key != game::K_MWHEELDOWN)
		{
			return false;
		}
		const bool up = (key == game::K_MWHEELUP);
		if (GetAsyncKeyState(VK_MENU) & 0x8000) // Alt held -> FOV (up = zoom in = narrower)
		{
			freecam_fov = std::clamp(freecam_fov + (up ? -2.0f : 2.0f), 1.0f, 179.0f);
		}
		else // camera roll (Dutch angle), kept in [-180, 180]
		{
			freecam_angles[2] += (up ? 2.0f : -2.0f);
			if (freecam_angles[2] > 180.0f) freecam_angles[2] -= 360.0f;
			else if (freecam_angles[2] < -180.0f) freecam_angles[2] += 360.0f;
		}
		return true;
	}

	// interpolated camera pose at a demo time (for the GUI to draw the smooth path)
	camera_pose_t sample_pose_at(int demo_time)
	{
		return sample_dolly(demo_time);
	}

	bool start_cinematic()
	{
		if (!demo_playback::playing() || !demo_playback::is_replay_armed() || keyframes.size() < 2)
		{
			return false;
		}

		set_mode(THEATER_CAMERA_DOLLY);

		// NB: leave `timescale` exactly as the user set it -- the cinematic plays at their
		// chosen speed (slow-mo etc.). Do not reset it here.

		// seek the demo to the first keyframe so the campath plays from the start, then let
		// the demo roll -- the camera follows the demo clock through the path (IWXMVM model).
		if (const auto t = demo_playback::current_time(); t)
		{
			const int delta = keyframes.front().demo_time - *t;
			if (delta > 0)
			{
				command::execute(std::format("demoforward {}", delta), true);
			}
			else if (delta < 0)
			{
				command::execute(std::format("demorewind {}", -delta), true);
			}
		}

		if (demo_playback::paused())
		{
			demo_playback::toggle_pause();
		}

		console::info("[theater] playing campath from start (%zu markers)\n", keyframes.size());
		return true;
	}

	void stop_cinematic()
	{
		set_mode(THEATER_CAMERA_FREECAM);
	}

	bool is_cinematic_playing()
	{
		return get_mode() == THEATER_CAMERA_DOLLY;
	}

	void set_input_blocked(bool blocked)
	{
		input_blocked = blocked;
	}

	// Project a world position using the exact view the scene rendered with this frame
	// (cached in the hook) -> no 1-frame lag. Returns screen px + a screen radius sized
	// by distance so markers read as fixed-size world objects. false if behind the camera.
	bool project_marker(const float* world, float* out_screen, float* out_radius)
	{
		if (!g_view_valid)
		{
			return false;
		}

		const float local[3] = { world[0] - g_view_org[0], world[1] - g_view_org[1], world[2] - g_view_org[2] };
		const float fwd = local[0] * g_view_axis[0][0] + local[1] * g_view_axis[0][1] + local[2] * g_view_axis[0][2];
		if (fwd < 1.0f)
		{
			return false; // at/behind the camera
		}
		const float rgt = local[0] * g_view_axis[1][0] + local[1] * g_view_axis[1][1] + local[2] * g_view_axis[1][2];
		const float up = local[0] * g_view_axis[2][0] + local[1] * g_view_axis[2][1] + local[2] * g_view_axis[2][2];

		auto* place = game::ScrPlace_GetViewPlacement();
		const float w = place->realViewportSize[0];
		const float h = place->realViewportSize[1];
		out_screen[0] = place->realViewportPosition[0] + (w * 0.5f) * (1.0f - rgt / g_view_thfx / fwd);
		out_screen[1] = place->realViewportPosition[1] + (h * 0.5f) * (1.0f - up / g_view_thfy / fwd);

		if (out_radius)
		{
			// screen radius of a ~4-unit world sphere at this distance, clamped for sanity
			constexpr float world_size = 4.0f;
			*out_radius = std::clamp(world_size * (h * 0.5f) / (g_view_thfy * fwd), 3.5f, 26.0f);
		}
		return true;
	}

	bool get_view_origin(float* out)
	{
		if (!g_view_valid)
		{
			return false;
		}
		out[0] = g_view_org[0];
		out[1] = g_view_org[1];
		out[2] = g_view_org[2];
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

			cg_calc_view_values_hook.create(game::CG_CalcViewValues, calc_view_values_stub);
			// Verified native R_SetupLodParams(view, out, scale), called by both
			// R_UpdateLodParams (0x1C3030) and scene-view setup (0x1C286B).
			r_setup_lod_params_hook.create(0x1C7380_b, r_setup_lod_params_stub);

			// tap the game's real per-frame mouse delta for smooth freecam look (no jitter)
			in_mouse_move_hook.create(game::IN_MouseMove, in_mouse_move_stub);

			demo_camera = game::demo_compat::register_enum(
				"demo_camera", mode_names, THEATER_CAMERA_FIRST_PERSON, game::DVAR_FLAG_NONE,
				"Demo playback camera: first_person | third_person | freecam | dolly");
			demo_camera_speed = game::demo_compat::register_float(
				"demo_camera_speed", 500.0f, 10.0f, 20000.0f, game::DVAR_FLAG_NONE,
				"Freecam movement speed (units/second)");
			demo_camera_sens = game::demo_compat::register_float(
				"demo_camera_sens", 0.06f, 0.001f, 5.0f, game::DVAR_FLAG_NONE,
				"Freecam mouse-look sensitivity");
			demo_cod4_fov = game::demo_compat::register_float(
				"demo_cod4_fov", 65.0f, 40.0f, 160.0f, game::DVAR_FLAG_NONE,
				"Field of view for CoD4 first-person demo playback");
			demo_camera_boost = game::demo_compat::register_float("demo_camera_boost", 4.f, 1.f, 20.f,
				game::DVAR_FLAG_NONE, "Freecam Shift speed multiplier");
			demo_camera_slow = game::demo_compat::register_float("demo_camera_slow", .25f, .01f, 1.f,
				game::DVAR_FLAG_NONE, "Freecam Alt precision speed multiplier");
			const char* position_names[] = {"demo_dolly_x", "demo_dolly_y", "demo_dolly_z"};
			const char* rotation_names[] = {"demo_dolly_pitch", "demo_dolly_yaw", "demo_dolly_roll"};
			for (int axis = 0; axis < 3; ++axis)
			{
				dolly_position[axis] = game::demo_compat::register_float(position_names[axis], 0, -5000, 5000,
					game::DVAR_FLAG_NONE, "Dolly path world-space position offset");
				dolly_rotation[axis] = game::demo_compat::register_float(rotation_names[axis], 0, -360, 360,
					game::DVAR_FLAG_NONE, "Dolly path angle offset in degrees");
			}

			// console convenience commands (the F9 GUI drives these too)
			command::add("demo_camera_capture", []() { capture_keyframe(); });
			command::add("demo_camera_clear", []() { clear_keyframes(); });
			command::add("demo_cinematic", []() { start_cinematic(); });
		}
	};
}

REGISTER_COMPONENT(theater_camera::component)
