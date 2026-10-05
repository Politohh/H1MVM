#include "std_include.hpp"
#include "loader/component_loader.hpp"

#include "demo_gui.hpp"
#include "demo_playback.hpp"
#include "demo_utils.hpp"
#include "theater_camera.hpp"
#include "demo_avidemo.hpp"
#include "cod4_camera.hpp"
#include "theater_ui.hpp"

#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "utils/hook.hpp"
#include "utils/concurrency.hpp"

// NOTE(h1): h1-mod's own ImGui/D3D11 overlay (component/gui/gui.cpp + component/d3d11.cpp) is
// entirely #ifdef _DEBUG -- it does not exist in the Release build this mod actually ships/plays.
// Rather than flip that whole (much larger, dev-tooling-focused) system to always-on, this is a
// small, self-contained ImGui bootstrap scoped to just the demo controls window, active ONLY
// outside _DEBUG so it can never install a second, conflicting hook on the same addresses in a
// hypothetical Debug build (which would use the existing gui:: system instead, unmodified).
#ifndef _DEBUG

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace demo_gui
{
	namespace
	{
		struct demo_entry
		{
			std::filesystem::path path;
			std::string display_name;
			std::filesystem::file_time_type write_time;
		};

		bool window_open = false;
		std::vector<demo_entry> demo_files;
		std::size_t selected_index = 0;
		int selected_kf = -1;   // currently selected marker in the editor (-1 = none)
		float g_desired_timescale = 1.0f; // sticky playback speed (see persist_timescale)
		ImFont* g_font_header = nullptr;  // Segoe UI Semibold for section headers (null = default)

		// The `timescale` dvar is REPLICATED, so a demo restart (rewind / cinematic seek) makes
		// the loopback server re-sync it back to 1 -- losing the user's slow-mo. Remember the
		// speed the user chose and re-apply it whenever it gets reset to 1 during playback.
		void persist_timescale()
		{
			if (!demo_playback::playing() || !demo_playback::is_replay_armed())
			{
				return;
			}
			auto* ts = game::Dvar_FindVar("timescale");
			if (!ts)
			{
				return;
			}
			if (ts->current.value != 1.0f)
			{
				g_desired_timescale = ts->current.value; // capture any non-1 speed the user set
			}
			else if (g_desired_timescale != 1.0f)
			{
				ts->current.value = g_desired_timescale; // a restart reset it -> restore
			}
		}

		// A cohesive dark "cinema" theme: amber accent, rounded panels, comfortable spacing.
		void apply_theater_style()
		{
			auto& style = ImGui::GetStyle();
			style.WindowRounding = 8.0f;
			style.ChildRounding = 6.0f;
			style.FrameRounding = 5.0f;
			style.GrabRounding = 5.0f;
			style.PopupRounding = 5.0f;
			style.ScrollbarRounding = 5.0f;
			style.TabRounding = 5.0f;
			style.WindowPadding = ImVec2(16, 14);
			style.FramePadding = ImVec2(9, 6);
			style.ItemSpacing = ImVec2(10, 9);
			style.ItemInnerSpacing = ImVec2(8, 6);
			style.GrabMinSize = 13.0f;
			style.ScrollbarSize = 14.0f;
			style.WindowBorderSize = 1.0f;
			style.FrameBorderSize = 0.0f;
			style.WindowTitleAlign = ImVec2(0.5f, 0.5f);

			const ImVec4 accent(0.96f, 0.66f, 0.18f, 1.00f);       // amber
			const ImVec4 accent_dim(0.78f, 0.53f, 0.15f, 1.00f);
			ImVec4* c = style.Colors;
			c[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.94f, 1.00f);
			c[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.52f, 0.58f, 1.00f);
			c[ImGuiCol_WindowBg] = ImVec4(0.085f, 0.086f, 0.10f, 0.98f);
			c[ImGuiCol_ChildBg] = ImVec4(0.115f, 0.117f, 0.14f, 1.00f);
			c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.13f, 0.98f);
			c[ImGuiCol_Border] = ImVec4(0.26f, 0.26f, 0.32f, 0.55f);
			c[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.17f, 0.21f, 1.00f);
			c[ImGuiCol_FrameBgHovered] = ImVec4(0.23f, 0.23f, 0.28f, 1.00f);
			c[ImGuiCol_FrameBgActive] = ImVec4(0.27f, 0.27f, 0.33f, 1.00f);
			c[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.13f, 1.00f);
			c[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.13f, 0.10f, 1.00f);
			c[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
			c[ImGuiCol_Header] = ImVec4(0.22f, 0.20f, 0.16f, 1.00f);
			c[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.32f);
			c[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.50f);
			c[ImGuiCol_Button] = ImVec4(0.21f, 0.21f, 0.26f, 1.00f);
			c[ImGuiCol_ButtonHovered] = accent_dim;
			c[ImGuiCol_ButtonActive] = accent;
			c[ImGuiCol_SliderGrab] = accent;
			c[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.78f, 0.32f, 1.00f);
			c[ImGuiCol_CheckMark] = accent;
			c[ImGuiCol_Separator] = ImVec4(0.26f, 0.26f, 0.32f, 0.60f);
			c[ImGuiCol_SeparatorHovered] = accent_dim;
			c[ImGuiCol_ResizeGrip] = ImVec4(accent.x, accent.y, accent.z, 0.30f);
			c[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
			c[ImGuiCol_ResizeGripActive] = accent;
			c[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.08f, 0.10f, 0.60f);
			c[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.30f, 0.37f, 1.00f);
			c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.38f, 0.38f, 0.46f, 1.00f);
			c[ImGuiCol_ScrollbarGrabActive] = accent_dim;
		}

		std::string format_mmss(std::int32_t ms)
		{
			if (ms < 0)
			{
				ms = 0;
			}

			const auto total_seconds = ms / 1000;
			return std::format("{:02}:{:02}", total_seconds / 60, total_seconds % 60);
		}

		void refresh_demo_list()
		{
			demo_files.clear();
			selected_index = 0;

			const auto dir = demo_utils::create_directory_auto_demo();
			if (!dir)
			{
				return;
			}

			std::error_code ec;
			for (const auto& entry : std::filesystem::directory_iterator(*dir, ec))
			{
				if (!entry.is_regular_file() || entry.path().extension() != demo_utils::DEMO_EXTENSION)
				{
					continue;
				}

				const auto write_time = entry.last_write_time(ec);
				demo_files.push_back({entry.path(), entry.path().filename().string(), write_time});
			}

			// most-recently-modified first -- filenames are mapname + a per-map incrementing
			// number, so they don't sort meaningfully once more than one map has been recorded.
			std::sort(demo_files.begin(), demo_files.end(), [](const demo_entry& a, const demo_entry& b)
			{
				return a.write_time > b.write_time;
			});
		}

		// seek demo playback to an absolute demo serverTime (reuses demoforward/demorewind).
		void seek_to(int demo_time)
		{
			(void)demo_playback::seek_to(demo_time);
		}

		// Draw the dolly markers (yellow dots + index) and the smooth spline path (blue line)
		// anchored in the 3D world -- the "3D markers". Shown while editing (freecam etc.) and
		// hidden once the cinematic is rolling in dolly mode, so they don't appear in the shot.
		// Uses the foreground draw list so it shows even when the F9 window is closed.
		void draw_markers_overlay()
		{
			if (!demo_playback::playing() || theater_camera::get_mode() == theater_camera::THEATER_CAMERA_DOLLY)
			{
				return;
			}

			const auto& kfs = theater_camera::get_keyframes();
			if (kfs.empty())
			{
				return;
			}

			auto* dl = ImGui::GetForegroundDrawList();

			// smooth spline path (blue) -- sample the same Catmull-Rom the dolly plays,
			// projected from the cached render view (no jitter).
			if (kfs.size() >= 2)
			{
				const int t0 = kfs.front().demo_time;
				const int t1 = kfs.back().demo_time;
				constexpr int steps = 160;
				ImVec2 prev{};
				bool have_prev = false;
				for (int s = 0; s <= steps; ++s)
				{
					const int t = t0 + static_cast<int>(static_cast<std::int64_t>(t1 - t0) * s / steps);
					const auto pose = theater_camera::sample_pose_at(t);
					float sc[2];
					if (theater_camera::project_marker(pose.pos, sc, nullptr))
					{
						const ImVec2 p(sc[0], sc[1]);
						if (have_prev)
						{
							dl->AddLine(prev, p, IM_COL32(90, 190, 255, 220), 2.5f);
						}
						prev = p;
						have_prev = true;
					}
					else
					{
						have_prev = false;
					}
				}
			}

			// markers -- yellow dots sized by distance (read as world objects) + index
			for (std::size_t i = 0; i < kfs.size(); ++i)
			{
				float sc[2];
				float radius = 6.0f;
				if (!theater_camera::project_marker(kfs[i].pose.pos, sc, &radius))
				{
					continue;
				}
				const ImVec2 p(sc[0], sc[1]);
				dl->AddCircleFilled(p, radius, IM_COL32(255, 210, 40, 255));
				dl->AddCircle(p, radius, IM_COL32(0, 0, 0, 220), 0, 2.0f);
				const auto label = std::to_string(i);
				dl->AddText(ImVec2(p.x + radius + 3.0f, p.y - 8.0f), IM_COL32(255, 255, 255, 255), label.c_str());
			}
		}


		// Always-on freecam controls HUD (top-left), shown while flying with the F9 editor closed.
		// Lists every key + live marker/FOV/roll info so you never need the editor open to fly.
		void draw_freecam_hud()
		{
			if (!demo_playback::playing()
				|| theater_camera::get_mode() != theater_camera::THEATER_CAMERA_FREECAM
				|| window_open) // the editor already shows this; don't double up / overlap
			{
				return;
			}

			struct row_t { const char* key; const char* desc; };
			static const row_t rows[] = {
				{ "WASD",       "Move" },
				{ "Mouse",      "Look" },
				{ "E / Q",      "Up / Down" },
				{ "Shift",      "Fly faster" },
				{ "Scroll",     "Camera roll" },
				{ "Alt+Scroll", "Field of view" },
				{ "MMB / K",    "Place marker" },
				{ "J",          "Start / stop campath" },
				{ "L",          "Clear markers (path stopped)" },
				{ "Space",      "Pause / play" },
				{ "F1",         "Show / hide controls" },
				{ "F2",         "Cycle camera modes" },
				{ "F10",        "Open editor" },
			};
			constexpr int n = static_cast<int>(sizeof(rows) / sizeof(rows[0]));

			auto* dl = ImGui::GetForegroundDrawList();
			const float pad_x = 14.0f, pad_y = 11.0f;
			const float ox = 20.0f, oy = 20.0f;
			const float line_h = ImGui::GetTextLineHeightWithSpacing();
			const float key_col_w = 96.0f;

			float max_desc = 0.0f;
			for (const auto& r : rows) { max_desc = std::max(max_desc, ImGui::CalcTextSize(r.desc).x); }
			const float panel_w = pad_x * 2.0f + key_col_w + max_desc;
			const float panel_h = pad_y * 2.0f + line_h * (n + 3.0f) + 16.0f; // header + rows + sep + 2 info

			const ImVec2 p0(ox, oy);
			const ImVec2 p1(ox + panel_w, oy + panel_h);
			dl->AddRectFilled(p0, p1, IM_COL32(14, 14, 18, 212), 9.0f);
			dl->AddRect(p0, p1, IM_COL32(245, 168, 46, 190), 9.0f, 0, 1.5f);

			const float x_key = ox + pad_x;
			const float x_desc = x_key + key_col_w;
			float y = oy + pad_y;

			dl->AddText(ImVec2(x_key, y), IM_COL32(245, 168, 46, 255), "FREECAM CONTROLS");
			y += line_h + 5.0f;
			for (const auto& r : rows)
			{
				dl->AddText(ImVec2(x_key, y), IM_COL32(255, 236, 150, 255), r.key);
				dl->AddText(ImVec2(x_desc, y), IM_COL32(206, 206, 214, 255), r.desc);
				y += line_h;
			}

			y += 5.0f;
			dl->AddLine(ImVec2(x_key, y), ImVec2(p1.x - pad_x, y), IM_COL32(90, 90, 105, 170), 1.0f);
			y += 7.0f;

			const auto& kfs = theater_camera::get_keyframes();
			const auto pose = theater_camera::get_freecam_pose();
			const auto info = std::format("Markers: {}    FOV: {:.0f}    Roll: {:.0f}",
				kfs.size(), pose.fov, pose.angles[2]);
			dl->AddText(ImVec2(x_key, y), IM_COL32(206, 206, 214, 255), info.c_str());
			y += line_h;
			if (kfs.size() < 2)
			{
				dl->AddText(ImVec2(x_key, y), IM_COL32(255, 150, 120, 255), "Place 2+ markers, then press J");
			}
			else
			{
				dl->AddText(ImVec2(x_key, y), IM_COL32(150, 220, 150, 255), "Press J to play the campath");
			}
		}

		// IWXMVM-style timeline: a zoomable bar (Ctrl+scroll) showing the campath range,
		// draggable keyframe diamonds and a scrubbable playhead, plus a full-demo overview
		// strip below it (drag to slide the zoomed view across the whole demo).
		void draw_timeline_widget()
		{
			const auto bounds = demo_playback::time_bounds();
			const auto current = demo_playback::current_time();
			if (!bounds || !current || bounds->second <= bounds->first)
			{
				ImGui::TextDisabled("Timeline unavailable (this demo has no recorded end time)");
				return;
			}

			const int first = bounds->first;
			const int last = bounds->second;
			const int full_span = std::max(1, last - first);

			// persistent zoom/pan window [vs, ve]; reset when the demo (its bounds) changes
			static bool view_init = false;
			static int vs = 0, ve = 0, known_first = 0, known_last = 0;
			if (!view_init || known_first != first || known_last != last || ve <= vs)
			{
				vs = first; ve = last; known_first = first; known_last = last; view_init = true;
			}
			vs = std::clamp(vs, first, last - 1);
			ve = std::clamp(ve, vs + 1, last);
			int view_span = std::max(1, ve - vs);

			auto& io = ImGui::GetIO();
			const float width = ImGui::GetContentRegionAvail().x;
			const auto& kfs = theater_camera::get_keyframes();

			// ===== main (zoomed) timeline =====
			const float height = 78.0f;
			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			const ImVec2 p1(p0.x + width, p0.y + height);
			ImGui::InvisibleButton("##timeline", ImVec2(width, height));
			const bool hovered = ImGui::IsItemHovered();
			auto* dl = ImGui::GetWindowDrawList();

			const auto time_to_x = [&](int t) { return p0.x + (static_cast<float>(std::clamp(t, vs, ve) - vs) / view_span) * width; };
			const auto x_to_time = [&](float x) { return vs + static_cast<int>(std::clamp((x - p0.x) / width, 0.0f, 1.0f) * view_span); };

			// Ctrl + scroll = zoom, centered on the cursor
			if (hovered && io.KeyCtrl && io.MouseWheel != 0.0f)
			{
				const int pivot = x_to_time(io.MousePos.x);
				const float f = (io.MouseWheel > 0.0f) ? 0.80f : 1.25f;
				const int new_span = std::clamp(static_cast<int>(view_span * f), std::min(1000, full_span), full_span);
				const float frac = static_cast<float>(pivot - vs) / view_span;
				vs = pivot - static_cast<int>(frac * new_span);
				ve = vs + new_span;
				if (vs < first) { vs = first; ve = first + new_span; }
				if (ve > last) { ve = last; vs = last - new_span; }
				vs = std::clamp(vs, first, last - 1);
				ve = std::clamp(ve, vs + 1, last);
				view_span = std::max(1, ve - vs);
			}

			dl->AddRectFilled(p0, p1, IM_COL32(19, 19, 25, 255), 6.0f);
			dl->AddRect(p0, p1, IM_COL32(72, 72, 88, 255), 6.0f);

			// ruler: pick a step that gives a sensible number of labels for the current zoom
			const int view_s = std::max(1, view_span / 1000);
			int step_s = 1;
			for (const int cand : { 1, 2, 5, 10, 15, 30, 60, 120, 300, 600 }) { step_s = cand; if (view_s / cand <= 10) break; }
			for (int s = ((vs - first) / 1000 / step_s) * step_s; first + s * 1000 <= ve; s += step_s)
			{
				const int t = first + s * 1000;
				if (t < vs) continue;
				const float x = time_to_x(t);
				dl->AddLine(ImVec2(x, p0.y + 5.0f), ImVec2(x, p0.y + 13.0f), IM_COL32(95, 95, 112, 255), 1.0f);
				const auto lbl = std::format("{}:{:02}", s / 60, s % 60);
				dl->AddText(ImVec2(x + 3.0f, p0.y + 3.0f), IM_COL32(145, 145, 162, 255), lbl.c_str());
			}

			// lane + campath range band
			const float lane_y = p0.y + 28.0f;
			const float lane_h = height - 40.0f;
			dl->AddRectFilled(ImVec2(p0.x + 4, lane_y), ImVec2(p1.x - 4, lane_y + lane_h), IM_COL32(13, 13, 17, 255), 3.0f);
			if (kfs.size() >= 2)
			{
				const float bx0 = std::clamp(time_to_x(kfs.front().demo_time), p0.x + 4.0f, p1.x - 4.0f);
				const float bx1 = std::clamp(time_to_x(kfs.back().demo_time), p0.x + 4.0f, p1.x - 4.0f);
				dl->AddRectFilled(ImVec2(bx0, lane_y), ImVec2(bx1, lane_y + lane_h), IM_COL32(58, 98, 168, 115), 2.0f);
			}

			// interaction: grab the nearest keyframe diamond (retime) or scrub the lane
			static int drag_mode = 0; // 0 none, 1 scrub, 2 keyframe
			static int drag_kf = -1;
			static int drag_time = 0;
			const float handle_y = lane_y + lane_h * 0.5f;

			if (ImGui::IsItemActivated())
			{
				const float mx = io.MousePos.x;
				int best = -1;
				float best_d = 11.0f;
				for (std::size_t i = 0; i < kfs.size(); ++i)
				{
					if (kfs[i].demo_time < vs || kfs[i].demo_time > ve) continue;
					const float d = std::abs(time_to_x(kfs[i].demo_time) - mx);
					if (d < best_d) { best_d = d; best = static_cast<int>(i); }
				}
				if (best >= 0) { drag_mode = 2; drag_kf = best; drag_time = kfs[best].demo_time; selected_kf = best; }
				else { drag_mode = 1; drag_time = x_to_time(mx); }
			}
			if (ImGui::IsItemActive())
			{
				drag_time = x_to_time(io.MousePos.x);
			}
			if (ImGui::IsItemDeactivated())
			{
				if (drag_mode == 1) seek_to(drag_time);
				else if (drag_mode == 2 && drag_kf >= 0)
					selected_kf = static_cast<int>(theater_camera::set_keyframe_time(static_cast<std::size_t>(drag_kf), drag_time));
				drag_mode = 0; drag_kf = -1;
			}

			// keyframe diamonds (only those inside the view)
			for (std::size_t i = 0; i < kfs.size(); ++i)
			{
				const bool dragged = (drag_mode == 2 && drag_kf == static_cast<int>(i));
				const int t = dragged ? drag_time : kfs[i].demo_time;
				if (t < vs || t > ve) continue;
				const float x = time_to_x(t);
				const bool sel = (static_cast<int>(i) == selected_kf);
				const ImU32 col = sel ? IM_COL32(255, 236, 130, 255) : IM_COL32(255, 200, 48, 255);
				const float r = sel ? 7.5f : 6.0f;
				dl->AddQuadFilled(ImVec2(x, handle_y - r), ImVec2(x + r, handle_y), ImVec2(x, handle_y + r), ImVec2(x - r, handle_y), col);
				dl->AddQuad(ImVec2(x, handle_y - r), ImVec2(x + r, handle_y), ImVec2(x, handle_y + r), ImVec2(x - r, handle_y), IM_COL32(0, 0, 0, 210), 1.6f);
			}

			// AVI capture range: green band between In/Out + labelled edge lines
			{
				const int a_in = demo_avidemo::in_point();
				const int a_out = demo_avidemo::out_point();
				const ImU32 avi_col = IM_COL32(78, 200, 120, 255);
				if (demo_avidemo::has_range())
				{
					const float ax0 = std::clamp(time_to_x(a_in), p0.x + 4.0f, p1.x - 4.0f);
					const float ax1 = std::clamp(time_to_x(a_out), p0.x + 4.0f, p1.x - 4.0f);
					dl->AddRectFilled(ImVec2(ax0, lane_y), ImVec2(ax1, lane_y + lane_h), IM_COL32(78, 200, 120, 48), 2.0f);
				}
				if (demo_avidemo::has_in() && a_in >= vs && a_in <= ve)
				{
					const float x = time_to_x(a_in);
					dl->AddLine(ImVec2(x, lane_y), ImVec2(x, lane_y + lane_h), avi_col, 2.0f);
					dl->AddText(ImVec2(x + 2.0f, lane_y + lane_h - 15.0f), avi_col, "IN");
				}
				if (demo_avidemo::has_out() && a_out >= vs && a_out <= ve)
				{
					const float x = time_to_x(a_out);
					dl->AddLine(ImVec2(x, lane_y), ImVec2(x, lane_y + lane_h), avi_col, 2.0f);
					dl->AddText(ImVec2(x + 2.0f, lane_y + lane_h - 15.0f), avi_col, "OUT");
				}
			}

			// playhead (only if inside the view)
			const int show_time = (drag_mode == 1) ? drag_time : *current;
			if (show_time >= vs && show_time <= ve)
			{
				const float px = time_to_x(show_time);
				dl->AddLine(ImVec2(px, lane_y - 5.0f), ImVec2(px, lane_y + lane_h + 5.0f), IM_COL32(255, 74, 74, 255), 2.5f);
				dl->AddTriangleFilled(ImVec2(px - 6.0f, lane_y - 5.0f), ImVec2(px + 6.0f, lane_y - 5.0f), ImVec2(px, lane_y + 4.0f), IM_COL32(255, 74, 74, 255));
			}

			// ===== right-click context menu (Windows-style: delete / go to / insert) =====
			static int ctx_kf = -1;   // marker the menu targets (-1 => empty space)
			static int ctx_time = 0;  // demo time under the cursor when it was opened
			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				const float mx = io.MousePos.x;
				int best = -1; float best_d = 11.0f;
				for (std::size_t i = 0; i < kfs.size(); ++i)
				{
					if (kfs[i].demo_time < vs || kfs[i].demo_time > ve) continue;
					const float d = std::abs(time_to_x(kfs[i].demo_time) - mx);
					if (d < best_d) { best_d = d; best = static_cast<int>(i); }
				}
				ctx_kf = best;
				ctx_time = x_to_time(mx);
				if (best >= 0) selected_kf = best;
				ImGui::OpenPopup("##tl_ctx");
			}
			if (ImGui::BeginPopup("##tl_ctx"))
			{
				if (ctx_kf >= 0 && ctx_kf < static_cast<int>(kfs.size()))
				{
					ImGui::TextDisabled("Marker %d  @ %s", ctx_kf + 1, format_mmss(kfs[ctx_kf].demo_time - first).c_str());
					ImGui::Separator();
					if (ImGui::MenuItem("Go to (seek here)")) seek_to(kfs[ctx_kf].demo_time);
					if (ImGui::MenuItem("Insert marker after"))
					{
						// place it midway to the next node (or 1s past it if this is the last) and
						// seed the pose from the current path so nothing jumps until you edit it.
						const int t = (ctx_kf + 1 < static_cast<int>(kfs.size()))
							? (kfs[ctx_kf].demo_time + kfs[ctx_kf + 1].demo_time) / 2
							: std::min(last, kfs[ctx_kf].demo_time + 1000);
						selected_kf = static_cast<int>(theater_camera::insert_keyframe(t, theater_camera::sample_pose_at(t)));
					}
					ImGui::Separator();
					if (ImGui::MenuItem("Delete"))
					{
						theater_camera::remove_keyframe(static_cast<std::size_t>(ctx_kf));
						if (selected_kf == ctx_kf) selected_kf = -1;
						else if (selected_kf > ctx_kf) --selected_kf;
					}
				}
				else
				{
					ImGui::TextDisabled("Time %s", format_mmss(std::clamp(ctx_time, first, last) - first).c_str());
					ImGui::Separator();
					if (ImGui::MenuItem("Insert marker here"))
					{
						const int t = std::clamp(ctx_time, first, last);
						const auto pose = kfs.empty() ? theater_camera::get_freecam_pose() : theater_camera::sample_pose_at(t);
						selected_kf = static_cast<int>(theater_camera::insert_keyframe(t, pose));
					}
				}

				// AVI capture in/out (movie-capture range) -- offered on marker + empty space
				ImGui::Separator();
				const int avi_t = std::clamp(ctx_time, first, last);
				if (ImGui::MenuItem("Set AVI In here")) demo_avidemo::set_in_point(avi_t);
				if (ImGui::MenuItem("Set AVI Out here")) demo_avidemo::set_out_point(avi_t);
				if ((demo_avidemo::has_in() || demo_avidemo::has_out()) && ImGui::MenuItem("Clear AVI range"))
				{
					demo_avidemo::clear_range();
				}
				ImGui::EndPopup();
			}

			// ===== full-demo overview strip (drag to slide the view across the whole demo) =====
			ImGui::Dummy(ImVec2(0.0f, 1.0f));
			const float ov_h = 16.0f;
			const ImVec2 o0 = ImGui::GetCursorScreenPos();
			const ImVec2 o1(o0.x + width, o0.y + ov_h);
			ImGui::InvisibleButton("##overview", ImVec2(width, ov_h));
			const auto ov_x = [&](int t) { return o0.x + (static_cast<float>(std::clamp(t, first, last) - first) / full_span) * width; };
			dl->AddRectFilled(o0, o1, IM_COL32(15, 15, 20, 255), 3.0f);
			dl->AddRect(o0, o1, IM_COL32(58, 58, 72, 255), 3.0f);
			for (std::size_t i = 0; i < kfs.size(); ++i)
			{
				const float x = ov_x(kfs[i].demo_time);
				dl->AddLine(ImVec2(x, o0.y + 2.0f), ImVec2(x, o1.y - 2.0f), IM_COL32(255, 200, 48, 190), 1.0f);
			}
			dl->AddRectFilled(ImVec2(ov_x(vs), o0.y + 1.0f), ImVec2(ov_x(ve), o1.y - 1.0f), IM_COL32(120, 160, 230, 70));
			dl->AddRect(ImVec2(ov_x(vs), o0.y + 1.0f), ImVec2(ov_x(ve), o1.y - 1.0f), IM_COL32(150, 190, 255, 220));
			dl->AddLine(ImVec2(ov_x(*current), o0.y), ImVec2(ov_x(*current), o1.y), IM_COL32(255, 74, 74, 220), 1.5f);
			if (ImGui::IsItemActive())
			{
				const int mt = first + static_cast<int>(std::clamp((io.MousePos.x - o0.x) / width, 0.0f, 1.0f) * full_span);
				vs = std::clamp(mt - view_span / 2, first, last - view_span);
				ve = vs + view_span;
			}

			ImGui::Text("%s / %s", format_mmss(show_time - first).c_str(), format_mmss(last - first).c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("- click=scrub, drag diamonds=retime, right-click=menu (delete/insert), Ctrl+scroll=zoom, drag strip to pan");
		}

		void draw_playback_controls()
		{
			const bool paused = demo_playback::paused();
			if (ImGui::Button(paused ? "Play##toggle_pause" : "Pause##toggle_pause", ImVec2(64.0f, 0.0f)))
			{
				demo_playback::toggle_pause();
			}
			ImGui::SameLine();
			if (ImGui::Button("<< 5s"))
			{
				if (const auto c = demo_playback::current_time()) seek_to(*c - 5000); // clamped, EOF-safe
			}
			ImGui::SameLine();
			if (ImGui::Button(">> 5s"))
			{
				if (const auto c = demo_playback::current_time()) seek_to(*c + 5000); // clamped, EOF-safe
			}

			// speed = the game's timescale dvar (also slows the demo action -> stays in sync)
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.0f);
			auto* ts = game::Dvar_FindVar("timescale");
			float speed = ts ? ts->current.value : 1.0f;
			// down to 0.025x for slow-mo capture (smoothed sub-0.1x via the Com_TimeScaleMsec
			// pattern fix in demo_timescale.cpp -- the engine alone stutters to ~15fps below 0.1x)
			if (ImGui::SliderFloat("##speed", &speed, 0.025f, 2.0f, "speed %.3fx") && ts)
			{
				ts->current.value = std::clamp(speed, 0.025f, 4.0f);
				g_desired_timescale = ts->current.value; // explicit intent (also lets you set 1.0x)
				(void)theater_ui::action("speed", static_cast<int>(ts->current.value * 1000.f));
			}
		}

		// a small amber section heading with a rule under it
		void section_header(const char* label)
		{
			ImGui::Dummy(ImVec2(0.0f, 3.0f));
			if (g_font_header) { ImGui::PushFont(g_font_header); }
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.66f, 0.18f, 1.0f));
			ImGui::TextUnformatted(label);
			ImGui::PopStyleColor();
			if (g_font_header) { ImGui::PopFont(); }
			ImGui::Separator();
		}

		// camera mode as a segmented button row (active one lit in amber)
		void camera_mode_buttons()
		{
			static const char* names[] = { "First-person", "Third-person", "Freecam", "Dolly" };
			const int cur = static_cast<int>(theater_camera::get_mode());
			const float w = (ImGui::GetContentRegionAvail().x - 3.0f * ImGui::GetStyle().ItemSpacing.x) / 4.0f;
			for (int i = 0; i < 4; ++i)
			{
				if (i)
				{
					ImGui::SameLine();
				}
				const bool active = (i == cur);
				if (active)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.96f, 0.66f, 0.18f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
				}
				if (ImGui::Button(names[i], ImVec2(w, 30.0f)))
				{
					theater_camera::set_mode(static_cast<theater_camera::camera_mode>(i));
				}
				if (active)
				{
					ImGui::PopStyleColor(2);
				}
			}
		}

		// the marker list + per-marker editor
		void draw_marker_editor()
		{
			const auto& kfs = theater_camera::get_keyframes();

			ImGui::Text("%zu markers", kfs.size());
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 214.0f);
			if (ImGui::Button("Capture marker", ImVec2(126.0f, 0.0f)))
			{
				theater_camera::capture_keyframe();
				selected_kf = static_cast<int>(theater_camera::get_keyframes().size()) - 1;
			}
			ImGui::SameLine();
			if (ImGui::Button("Clear all", ImVec2(80.0f, 0.0f)))
			{
				theater_camera::clear_keyframes();
				selected_kf = -1;
			}

			if (!kfs.empty())
			{
				const int start = kfs.front().demo_time;
				if (ImGui::BeginChild("##kf_list", ImVec2(0.0f, 132.0f), true))
				{
					for (int i = 0; i < static_cast<int>(kfs.size()); ++i)
					{
						ImGui::PushID(i);
						const auto label = std::format("  Marker {:<2}   {}", i, format_mmss(kfs[i].demo_time - start));
						if (ImGui::Selectable(label.c_str(), selected_kf == i, ImGuiSelectableFlags_None,
							ImVec2(ImGui::GetContentRegionAvail().x - 76.0f, 0.0f)))
						{
							selected_kf = i;
						}
						ImGui::SameLine();
						if (ImGui::SmallButton("Go"))
						{
							seek_to(kfs[i].demo_time);
						}
						ImGui::SameLine();
						if (ImGui::SmallButton("Del"))
						{
							theater_camera::remove_keyframe(static_cast<std::size_t>(i));
							selected_kf = -1;
							ImGui::PopID();
							break;
						}
						ImGui::PopID();
					}
				}
				ImGui::EndChild();
			}

			// per-marker editor
			if (selected_kf >= 0 && selected_kf < static_cast<int>(kfs.size()))
			{
				section_header("Edit selected marker");

				auto pose = kfs[selected_kf].pose;
				bool changed = false;
				ImGui::SetNextItemWidth(-1.0f);
				changed |= ImGui::DragFloat3("##pos", pose.pos, 2.0f, 0.0f, 0.0f, "pos %.0f");
				ImGui::SetNextItemWidth(-1.0f);
				changed |= ImGui::DragFloat3("##rot", pose.angles, 0.5f, 0.0f, 0.0f, "rot %.1f");
				ImGui::SetNextItemWidth(-1.0f);
				changed |= ImGui::DragFloat("##fov", &pose.fov, 0.5f, 1.0f, 179.0f, "fov %.0f");
				if (changed)
				{
					theater_camera::update_keyframe_pose(static_cast<std::size_t>(selected_kf), pose);
				}

				if (ImGui::Button("Recapture here"))
				{
					theater_camera::update_keyframe_pose(static_cast<std::size_t>(selected_kf), theater_camera::get_freecam_pose());
				}
				ImGui::SameLine();
				if (ImGui::Button("Set time = now"))
				{
					if (const auto t = demo_playback::current_time(); t)
					{
						selected_kf = static_cast<int>(theater_camera::set_keyframe_time(static_cast<std::size_t>(selected_kf), *t));
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("Go to"))
				{
					seek_to(kfs[selected_kf].demo_time);
				}
				ImGui::SameLine();
				if (ImGui::Button("Delete"))
				{
					theater_camera::remove_keyframe(static_cast<std::size_t>(selected_kf));
					selected_kf = -1;
				}
			}
		}

		// avidemo movie capture: in/out range + encode settings + arm. Frames are captured
		// at a fixed timestep and streamed to ffmpeg -> one finished, compressed video file.
		void draw_avi_capture()
		{
			const auto cur = demo_playback::current_time();
			const auto bounds = demo_playback::time_bounds();
			const int first = bounds ? bounds->first : 0;

			// ----- In / Out range -----
			ImGui::BeginDisabled(!cur.has_value());
			if (ImGui::Button("Set In @ playhead") && cur) demo_avidemo::set_in_point(*cur);
			ImGui::SameLine();
			if (ImGui::Button("Set Out @ playhead") && cur) demo_avidemo::set_out_point(*cur);
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Clear range")) demo_avidemo::clear_range();

			if (demo_avidemo::has_in()) ImGui::Text("In  %s", format_mmss(demo_avidemo::in_point() - first).c_str());
			else ImGui::TextDisabled("In  --:--");
			ImGui::SameLine(150.0f);
			if (demo_avidemo::has_out()) ImGui::Text("Out %s", format_mmss(demo_avidemo::out_point() - first).c_str());
			else ImGui::TextDisabled("Out --:--");
			ImGui::TextDisabled("(also right-click the timeline: Set AVI In/Out here)");

			// ----- encode settings -----
			auto* d_fps = game::Dvar_FindVar("avi_fps");
			auto* d_out_fps = game::Dvar_FindVar("avi_out_fps");
			auto* d_crf = game::Dvar_FindVar("avi_crf");
			auto* d_codec = game::Dvar_FindVar("avi_codec");
			auto* d_flip = game::Dvar_FindVar("avi_flip");

			if (d_fps)
			{
				int fps = d_fps->current.integer;
				ImGui::SetNextItemWidth(120.0f);
				if (ImGui::InputInt("capture fps", &fps, 25, 100)) d_fps->current.integer = std::clamp(fps, 1, 1000);
				ImGui::SameLine();
				for (const int p : { 120, 250, 500, 600 })
				{
					if (ImGui::SmallButton(std::to_string(p).c_str())) d_fps->current.integer = p;
					ImGui::SameLine();
				}
				ImGui::NewLine();
			}
			if (d_out_fps)
			{
				int of = d_out_fps->current.integer;
				ImGui::SetNextItemWidth(120.0f);
				if (ImGui::InputInt("video fps", &of, 5, 30)) d_out_fps->current.integer = std::clamp(of, 1, 240);
				if (d_fps && d_out_fps->current.integer > 0)
				{
					const float sm = static_cast<float>(d_fps->current.integer) / static_cast<float>(d_out_fps->current.integer);
					ImGui::SameLine();
					if (sm > 1.01f) ImGui::TextColored(ImVec4(0.96f, 0.66f, 0.18f, 1.0f), "= %.2gx slow-mo", sm);
					else ImGui::TextDisabled("= real-time");
				}
			}

			if (d_codec)
			{
				static const char* codecs[] = { "av1", "h265", "h264", "prores4444" };
				int ci = std::clamp(d_codec->current.integer, 0, 3);
				ImGui::SetNextItemWidth(140.0f);
				if (ImGui::Combo("codec", &ci, codecs, 4)) d_codec->current.integer = ci;
			}
			// CRF only applies to the delivery codecs; prores4444 (index 3) uses codec-default quality.
			if (d_crf && !(d_codec && d_codec->current.integer == 3))
			{
				ImGui::SameLine();
				int crf = d_crf->current.integer;
				ImGui::SetNextItemWidth(180.0f);
				if (ImGui::SliderInt("quality (CRF)", &crf, 0, 51)) d_crf->current.integer = crf;
			}
			if (d_flip)
			{
				bool flip = d_flip->current.enabled;
				if (ImGui::Checkbox("flip vertically (toggle if upside-down)", &flip)) d_flip->current.enabled = flip;
			}
			{
				// ffmpeg path is a module string (NOT a dvar - string dvars leak the script-string pool)
				static char ffbuf[512];
				static bool ff_init = false;
				if (!ff_init) { strncpy_s(ffbuf, demo_avidemo::ffmpeg_path(), _TRUNCATE); ff_init = true; }
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::InputText("##ffmpeg", ffbuf, sizeof(ffbuf), ImGuiInputTextFlags_EnterReturnsTrue)) demo_avidemo::set_ffmpeg_path(ffbuf);
				ImGui::TextDisabled("ffmpeg.exe path (Enter to apply). prores4444 = editing-grade .mov (huge); AV1 needs libsvtav1.");
			}

			// ----- status / arm -----
			if (demo_avidemo::has_range() && d_fps)
			{
				const long long dur = static_cast<long long>(demo_avidemo::out_point()) - demo_avidemo::in_point();
				const int nf = static_cast<int>(dur * d_fps->current.integer / 1000);
				ImGui::TextDisabled("range %s  ->  ~%d frames (capture runs slower than real time)",
					format_mmss(static_cast<int>(dur)).c_str(), nf);
			}

			if (demo_avidemo::is_capturing())
			{
				ImGui::TextColored(ImVec4(0.40f, 0.90f, 0.52f, 1.0f), "CAPTURING  -  %d frames written", demo_avidemo::captured_frames());
				if (ImGui::Button("Abort capture", ImVec2(-1.0f, 30.0f))) demo_avidemo::disarm();
			}
			else if (demo_avidemo::is_armed())
			{
				ImGui::TextColored(ImVec4(0.96f, 0.66f, 0.18f, 1.0f), "Armed - playing into range...");
				if (ImGui::Button("Disarm", ImVec2(-1.0f, 30.0f))) demo_avidemo::disarm();
			}
			else
			{
				ImGui::BeginDisabled(!demo_avidemo::has_range());
				if (ImGui::Button("Start capture (seek to In, record to Out)", ImVec2(-1.0f, 34.0f)))
				{
					seek_to(demo_avidemo::in_point());
					if (demo_playback::paused()) demo_playback::toggle_pause();
					if (demo_avidemo::arm()) window_open = false; // hide overlay so it's not in the shot
				}
				ImGui::EndDisabled();
			}
			ImGui::TextDisabled("Output: demos/avi/<demo>_NNN. Assemble at your target fps for slow-mo.");
		}

		void render_cod4_controls()
        {
            if (!ImGui::CollapsingHeader("Convert COD4 demo")) return;
            static char path[1024]{};
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##cod4path", "Full path to a COD4 .dm_1 demo", path, sizeof(path));
            if (ImGui::Button("Convert to .dm_h1") && path[0] && !std::strpbrk(path, "\"\r\n"))
                command::execute(std::format("cod4convert \"{}\"", path));
            const auto status = cod4_camera::get_status();
            if (!status.message.empty()) ImGui::TextWrapped("%s", status.message.c_str());
            ImGui::TextDisabled("After conversion, refresh the demo list and use the normal Play button.");
        }

		void render_demo_controls()
		{
			if (demo_files.empty())
			{
				refresh_demo_list();
			}

			ImGui::SetNextWindowSize(ImVec2(620, 780), ImGuiCond_FirstUseEver); // a touch bigger for the Segoe UI face
			ImGui::SetNextWindowSizeConstraints(ImVec2(500, 420), ImVec2(1200, 2000));
			if (!ImGui::Begin("Demo Controls", &window_open))
			{
				ImGui::End();
				return;
			}

			if (ImGui::Button("Refresh"))
			{
				refresh_demo_list();
			}

			ImGui::SameLine();

			const auto* preview = demo_files.empty()
				? "(no demos found)"
				: demo_files[std::min(selected_index, demo_files.size() - 1)].display_name.c_str();

			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::BeginCombo("##demo_select", preview))
			{
				for (std::size_t i = 0; i < demo_files.size(); ++i)
				{
					const auto is_selected = (i == selected_index);
					if (ImGui::Selectable(demo_files[i].display_name.c_str(), is_selected))
					{
						selected_index = i;
					}

					if (is_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}

				ImGui::EndCombo();
			}

			ImGui::BeginDisabled(demo_files.empty());
			if (ImGui::Button("Play"))
			{
				// Queue the normal demoplay command for the next game command pump. Running it
				// synchronously here enters party::start_map from inside the ImGui render frame,
				// where the map transition can block the frame that must complete it.
				const auto& path = demo_files[selected_index].path;
				command::execute(std::format(R"(demoplay "{}" fullpath)", path.string()));
			}
			ImGui::EndDisabled();

			ImGui::SameLine();
			ImGui::BeginDisabled(!demo_playback::playing());
			if (ImGui::Button("Stop"))
			{
				demo_playback::stopplay();
			}
			ImGui::EndDisabled();

			ImGui::Separator();

			render_cod4_controls();

			if (!demo_playback::playing())
			{
				ImGui::TextDisabled("No demo playing.");
				ImGui::End();
				return;
			}

			ImGui::Text("Playing: %s", demo_playback::current_path().filename().string().c_str());

			// IWXMVM-style timeline + playback (scrub, play/pause, speed)
			draw_timeline_widget();
			draw_playback_controls();

			// ===== Camera =====
			section_header("Camera");
			camera_mode_buttons();

			if (theater_camera::get_mode() == theater_camera::THEATER_CAMERA_FREECAM)
			{
				float fov = theater_camera::get_freecam_fov();
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::SliderFloat("##fov", &fov, 1.0f, 179.0f, "freecam FOV  %.0f"))
				{
					theater_camera::set_freecam_fov(fov);
				}
				ImGui::TextDisabled("WASD move - mouse look - E/Q up/down - Shift fast - middle-click = marker - Space = pause");
			}

			// ===== Campath =====
			section_header("Campath");
			const auto& keyframes = theater_camera::get_keyframes();

			ImGui::BeginDisabled(keyframes.size() < 2);
			if (ImGui::Button("Play campath from start", ImVec2(-1.0f, 34.0f)))
			{
				theater_camera::start_cinematic();
				window_open = false; // auto-close the overlay so it's not in the shot
			}
			ImGui::EndDisabled();
			if (theater_camera::get_mode() == theater_camera::THEATER_CAMERA_DOLLY)
			{
				if (ImGui::Button("Exit campath (back to freecam)", ImVec2(-1.0f, 0.0f)))
				{
					theater_camera::stop_cinematic();
				}
			}
			ImGui::TextDisabled("Freecam + Capture markers, then switch to Dolly and scrub/play the timeline.");
			ImGui::TextDisabled("Hotkeys outside editor: J = start/stop path   K = marker   L = clear (path stopped)");

			// ===== Markers =====
			section_header("Markers");
			draw_marker_editor();

			// ===== Movie capture (avidemo) =====
			section_header("Movie Capture (avidemo)");
			draw_avi_capture();

			ImGui::End();
		}

		// ===== minimal ImGui/D3D11 bootstrap (see file header comment) =====

		ID3D11Device* device;
		ID3D11DeviceContext* device_context;
		bool imgui_initialized = false;
		bool toggled = false;

		struct wnd_event
		{
			HWND hWnd;
			UINT msg;
			WPARAM wParam;
			LPARAM lParam;
		};

		utils::concurrency::container<std::vector<wnd_event>> event_queue;

		void initialize_imgui_context()
		{
			ImGui::CreateContext();
			ImGui::StyleColorsDark();
			apply_theater_style();

			// Use Segoe UI (the Windows system font) instead of ImGui's built-in ProggyClean pixel
			// font -- crisp, modern, no squinting. Regular for body text, Semibold for the amber
			// section headers. Loaded from the system fonts folder (nothing extra to ship); if it's
			// somehow missing, ImGui keeps its default font.
			{
				auto& io = ImGui::GetIO();
				ImFontConfig cfg;
				cfg.OversampleH = 3;
				cfg.OversampleV = 2;
				const char* const segoe = R"(C:\Windows\Fonts\segoeui.ttf)";
				const char* const segoe_sb = R"(C:\Windows\Fonts\segoeuisb.ttf)";
				if (GetFileAttributesA(segoe) != INVALID_FILE_ATTRIBUTES)
				{
					io.Fonts->AddFontFromFileTTF(segoe, 18.0f, &cfg); // default (body) face
					if (GetFileAttributesA(segoe_sb) != INVALID_FILE_ATTRIBUTES)
					{
						g_font_header = io.Fonts->AddFontFromFileTTF(segoe_sb, 18.0f, &cfg);
					}
				}
			}

			ImGui_ImplWin32_Init(*reinterpret_cast<HWND*>(0xC9DD2E0_b));
			ImGui_ImplDX11_Init(device, device_context);

			imgui_initialized = true;
		}

		void run_event_queue()
		{
			event_queue.access([](std::vector<wnd_event>& queue)
			{
				for (const auto& event : queue)
				{
					ImGui_ImplWin32_WndProcHandler(event.hWnd, event.msg, event.wParam, event.lParam);
				}

				queue.clear();
			});
		}

		void toggle_window()
		{
			if (!toggled)
			{
				*reinterpret_cast<int*>(0xC9DC405_b) = 0;
				*game::keyCatchers |= 0x10;
			}
			else
			{
				*reinterpret_cast<int*>(0xC9DC405_b) = 1;
				*game::keyCatchers &= ~0x10;
			}

			toggled = !toggled;
			window_open = toggled;
		}

		void on_frame()
		{
			if (!game::Sys_IsDatabaseReady2())
			{
				return;
			}

			if (!imgui_initialized)
			{
				console::info("[demo_gui] Initializing ImGui\n");
				initialize_imgui_context();
				return;
			}

			ImGui::GetIO().MouseDrawCursor = toggled;

			// While the overlay is open, freeze the freecam so the mouse only drives the UI
			// cursor (and the camera holds still for a clean capture) -- not both at once.
			theater_camera::set_input_blocked(toggled || theater_ui::interactive());

			// Keep the OS cursor free while the overlay is open so its buttons can be clicked.
			// During demo playback the game clips/captures the cursor to the window for
			// mouse-look; toggle_window() sets byte_C9DC405=0 (stops CL_MouseMove recentering),
			// but the existing clip rectangle still has to be released -- and the game may
			// re-apply it -- so drop it every frame the window is open.
			if (toggled)
			{
				ClipCursor(nullptr);
			}

			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			run_event_queue();

			ImGui::NewFrame();

			// Speed persistence is shared with the native theater UI.

			// world-anchored dolly markers + path (shown even when the F9 window is closed)
			draw_markers_overlay();

			// always-on freecam controls + marker info (top-left), while flying with F9 closed
			draw_freecam_hud();

			if (toggled && window_open)
			{
				render_demo_controls();

				if (!window_open)
				{
					toggle_window();
				}
			}

			ImGui::EndFrame();
			ImGui::Render();
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		}

		utils::hook::detour wnd_proc_hook;
		LRESULT wnd_proc_stub(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			// Windows treats F10 as a system/menu key; it may never reach CL_KeyEvent.
			if (wParam == VK_F10 && (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYUP))
			{
				if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && !(lParam & (1LL << 30)))
					scheduler::once([] { theater_ui::close(); toggle_window(); }, scheduler::main);
				return 0;
			}
			// F9 (the window toggle key) is handled at the game key-event layer (see
			// demo_playback::handle_key_event / input.cpp), not here -- no need to special-case
			// it like gui.cpp does for VK_ESCAPE (which it uses to close its own menu).
			if (toggled)
			{
				event_queue.access([hWnd, msg, wParam, lParam](std::vector<wnd_event>& queue)
				{
					queue.emplace_back(hWnd, msg, wParam, lParam);
				});
			}

			return wnd_proc_hook.invoke<LRESULT>(hWnd, msg, wParam, lParam);
		}

		HRESULT d3d11_create_device_stub(IDXGIAdapter* p_adapter, D3D_DRIVER_TYPE driver_type, HMODULE software,
			UINT flags, const D3D_FEATURE_LEVEL* p_feature_levels, UINT feature_levels, UINT sdk_version,
			ID3D11Device** pp_device, D3D_FEATURE_LEVEL* p_feature_level, ID3D11DeviceContext** pp_immediate_context)
		{
			const auto result = D3D11CreateDevice(p_adapter, driver_type, software, flags, p_feature_levels,
				feature_levels, sdk_version, pp_device, p_feature_level, pp_immediate_context);

			if (pp_device != nullptr && pp_immediate_context != nullptr)
			{
				device = *pp_device;
				device_context = *pp_immediate_context;
			}

			return result;
		}
	}

	class component final : public component_interface
	{
	public:
		void* load_import(const std::string& library, const std::string& function) override
		{
			if (function == "D3D11CreateDevice" && !game::environment::is_dedi() && !game::environment::is_sp())
			{
				return d3d11_create_device_stub;
			}

			return nullptr;
		}

		void post_unpack() override
		{
			if (game::environment::is_dedi() || game::environment::is_sp())
			{
				return;
			}

			// per-frame pump: same call site h1-mod's own (debug-only) gui system uses.
			utils::hook::nop(0x6CB16D_b, 9);
			utils::hook::call(0x6CB170_b, on_frame);

			wnd_proc_hook.create(0x5BFF60_b, wnd_proc_stub);
		}

		void pre_destroy() override
		{
			if (game::environment::is_dedi() || game::environment::is_sp())
			{
				return;
			}

			if (imgui_initialized)
			{
				ImGui_ImplWin32_Shutdown();
				ImGui::DestroyContext();
			}
		}
	};

	void toggle()
	{
		toggle_window();
	}
	void close() { if (toggled) toggle_window(); }
	bool is_open() { return toggled; }
}

REGISTER_COMPONENT(demo_gui::component)

#else

namespace demo_gui
{
	void toggle()
	{
	}
	void close() {}
	bool is_open() { return false; }
}

#endif
