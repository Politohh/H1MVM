#include "std_include.hpp"
#include "loader/component_loader.hpp"

#include "demo_game.hpp"
#include "cod4_camera.hpp"
#include "demo_playback.hpp"
#include "theater_ui.hpp"

#include "game/dvars.hpp"
#include "component/scheduler.hpp"
#include "utils/hook.hpp"
#include "utils/string.hpp"

#include "component/console.hpp"

#include <deque>
#include <string>
#include <mutex>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <unordered_set>

// =============================================================================
//  Custom CoD4-style killfeed for MWR that slows/freezes with the DEMO clock
//  (timescale slow-mo AND avidemo capture), because the stock LUI killfeed fades
//  on a real-time clock buried in compiled Lua that we can't reach from C++.
//
//  Data source: hook LUI_Obituary (the CG->LUI bridge, sub_26E6F0) -- it hands us
//  every kill pre-resolved: attacker clientNum, victim clientNum, the weapon-icon
//  Material*, and a flip flag. We stamp each with cl->serverTime and draw our own
//  feed at CG_Draw2D, fading on (serverTime - entry.time). Bridged from the named
//  Ghosts IW6 IDB (same engine); names read directly from the bgs.clientinfo array.
// =============================================================================
namespace killfeed
{
	namespace
	{
		// the R_AddCmdDrawText macro (symbols.hpp) expands to an UNQUALIFIED H1_AddBaseDrawTextCmd
		// (it's meant to be used from inside namespace game), so pull it in here.
		using game::H1_AddBaseDrawTextCmd;

		// MWR bgs.clientinfo entry (@ CG_GetLocalClientGlobals()+0x174DA8), layout from HorizonMW
		// (same binary). Full layout kept so the array STRIDE is correct; we only read .name.
		struct clientInfo_t
		{
			char clientNum;
			char name[32];
			int rank_mp;
			int prestige_mp;
			int rank_alien;
			int prestige_alien;
			char clanAbbrev[8];
			int location;
			int health;
			unsigned int playerCardPatch;
			unsigned int playerCardBackground;
			unsigned __int8 use_elite_clan_tag;
			char elite_clan_tag_text[5];
			int elite_clan_level;
			bool isMLGSpectator;
			int game_extrainfo;
			game::Material* rankIconHandle;
			std::uint32_t rankDisplayLevel;
			bool braggingRights;
		};

		constexpr std::uintptr_t CLIENTINFO_OFFSET = 0x174DA8;

		// MWR bgs.characterinfo: a client's TEAM lives here. Bridged from
		// LUI_LuaCall_Game_GetPlayerTeam (sub_2837E0), which reads:
		//   valid = *(int*)(cg + 1856*clientNum + 1449260);
		//   team  = *(int*)(cg + 1856*clientNum + 1449268);   // else TEAM_FREE
		// (cg = CG_GetLocalClientGlobals() = sub_5CB80, the same base client_name uses)
		constexpr std::uintptr_t CHARACTERINFO_STRIDE = 1856;
		constexpr std::uintptr_t CHARACTERINFO_VALID_OFFSET = 1449260;
		constexpr std::uintptr_t CHARACTERINFO_TEAM_OFFSET = 1449268;

		// team_t (this binary): TEAM_FREE=0, TEAM_AXIS=1, TEAM_ALLIES=2, TEAM_SPECTATOR=3
		enum { TEAM_FREE = 0, TEAM_AXIS = 1, TEAM_ALLIES = 2 };

		utils::hook::detour lui_obituary_hook;    // sub_26E6F0 = LUI_Obituary
		utils::hook::detour cg_draw2d_hook;       // 0xF57D0   = CG_Draw2D (free in Release)
		utils::hook::detour player_joined_hook_a; // sub_138A60 -> fires "player_joined" LUI event
		utils::hook::detour player_joined_hook_b; // sub_1B5890 -> fires "player_joined" LUI event

		game::dvar_t* kf_enable = nullptr;
		game::dvar_t* kf_replace = nullptr;
		game::dvar_t* kf_time = nullptr;
		game::dvar_t* kf_scale = nullptr;
		game::dvar_t* kf_icon_w = nullptr;   // icon width as a multiple of its height
		game::dvar_t* kf_icon_h = nullptr;   // icon height as a multiple of text height
		game::dvar_t* kf_icon_yoff = nullptr;// icon vertical fine-tune (px)
		game::dvar_t* kf_icon_flip = nullptr;// mirror the weapon icon (barrel direction)

		game::dvar_t* kf_lines = nullptr;    // fully-visible line count (con_gameMsgWindow0 = 4)
		game::dvar_t* kf_overflow_fade = nullptr; // ms for an overflow line to fade itself
		game::dvar_t* kf_x = nullptr;        // left edge as a fraction of screen width
		game::dvar_t* kf_y = nullptr;        // bottom-entry baseline as a fraction of screen height
		game::dvar_t* kf_safe = nullptr;     // title-safe inset (fraction) clamping the feed
		game::dvar_t* kf_team_colors = nullptr; // colour names by team
		game::dvar_t* kf_col_axis = nullptr;    // axis (team 1) name colour RGBA
		game::dvar_t* kf_col_allies = nullptr;  // allies (team 2) name colour RGBA
		game::dvar_t* kf_col_neutral = nullptr; // neutral/other name colour RGBA
		game::dvar_t* kf_mute_connect = nullptr;// mute "X connected" player-join feed messages

		float dvar_f(game::dvar_t* d, float def) { return d ? d->current.value : def; }

		struct entry_t
		{
			int time;
			std::string attacker;
			std::string victim;
			game::Material* icon;
			bool flip;
			int attacker_team;
			int victim_team;
			int overflow_start; // serverTime this line was pushed past killfeed_lines (0 = not yet)
		};

		std::mutex g_mutex;
		std::deque<entry_t> g_entries;

		int now_time()
		{
			if (auto* cl = game::mp::cl.get(); cl) return cl->serverTime;
			return 0;
		}

		// Material name is at offset 0 in IW6 (MaterialInfo.name). Used to classify icon aspect
		// (weapon icons are wide rectangles; death icons -- headshot/suicide/melee/grenade -- are square).
		const char* material_name(game::Material* m)
		{
			if (!m) return nullptr;
			return *reinterpret_cast<const char* const*>(m);
		}

		std::unordered_set<std::string> g_logged_icons;

		bool icon_is_square(game::Material* m)
		{
			const char* name = material_name(m);
			if (!name) return true;
			// log each unique icon material once (debug aid)
			if (g_logged_icons.insert(name).second)
			{
				console::info("[killfeed] icon material: %s\n", name);
			}
			// weapon kill-icons are named hud_icon* (wide rectangles); everything else
			// (killiconheadshot, death_helicopter, ...) is a square icon.
			return std::strstr(name, "hud_icon") == nullptr;
		}

		std::string client_name(int client_num)
		{
			if (client_num < 0 || client_num >= 42) return "";
			if (client_num >= 18)
			{
				char name[42]{};
				utils::hook::invoke<int>(0x3439D0_b, 0, static_cast<unsigned char>(client_num),
					static_cast<unsigned short>(2047), name, static_cast<int>(sizeof(name)));
				return name;
			}
			auto* cg = game::CG_GetLocalClientGlobals();
			if (!cg) return "";
			auto* info = reinterpret_cast<clientInfo_t*>(reinterpret_cast<std::uintptr_t>(cg) + CLIENTINFO_OFFSET);
			info[client_num].name[31] = '\0';
			return std::string(info[client_num].name);
		}

		// Read a client's absolute team (axis/allies) from bgs.characterinfo, mirroring
		// LUI_LuaCall_Game_GetPlayerTeam: TEAM_FREE unless the entry's valid flag is set.
		int client_team(int client_num)
		{
			if (client_num < 0 || client_num >= 42) return TEAM_FREE;
			auto* cg = game::CG_GetLocalClientGlobals();
			if (!cg) return TEAM_FREE;
			const auto p = reinterpret_cast<std::uintptr_t>(cg) + CHARACTERINFO_STRIDE * static_cast<std::uintptr_t>(client_num);
			if (*reinterpret_cast<const int*>(p + CHARACTERINFO_VALID_OFFSET) == 0) return TEAM_FREE;
			return *reinterpret_cast<const int*>(p + CHARACTERINFO_TEAM_OFFSET);
		}

		// Fill an RGBA colour for a name based on its team; alpha folds in the fade.
		void team_color(int team, float alpha, float out[4])
		{
			const float* src = nullptr;
			if (kf_team_colors && kf_team_colors->current.enabled)
			{
				if (team == TEAM_AXIS && kf_col_axis) src = kf_col_axis->current.vector;
				else if (team == TEAM_ALLIES && kf_col_allies) src = kf_col_allies->current.vector;
				else if (kf_col_neutral) src = kf_col_neutral->current.vector;
			}
			if (src)
			{
				out[0] = src[0]; out[1] = src[1]; out[2] = src[2];
				out[3] = src[3] * alpha;
			}
			else
			{
				out[0] = out[1] = out[2] = 1.0f;
				out[3] = alpha;
			}
		}

		// LUI_Obituary(controllerIndex, attacker, victim, Material* icon, flip, luaVM): capture every kill
		// (serverTime-stamped) for our feed; optionally suppress the stock LUI feed.
		void lui_obituary_stub(std::int64_t controller_index, int attacker, int victim,
			game::Material* icon, char flip, std::int64_t lua_vm)
		{
			entry_t e{};
			e.time = now_time();
			theater_ui::note_obituary(e.time, attacker, victim);
			e.attacker = client_name(attacker);
			e.victim = client_name(victim);
			e.icon = icon;
			e.flip = flip != 0;
			e.attacker_team = client_team(attacker); // capture team at kill time (teams can change)
			e.victim_team = client_team(victim);
			{
				// original con_gameMsgWindow0: maxLines = lineCount + 3 (default 4 + 3 = 7)
				const int lc = kf_lines ? std::clamp(kf_lines->current.integer, 1, 7) : 4;
				const std::size_t max_lines = static_cast<std::size_t>(lc) + 3;
				std::lock_guard<std::mutex> lock(g_mutex);
				g_entries.push_back(std::move(e));
				while (g_entries.size() > max_lines) g_entries.pop_front();
			}

			if (demo_playback::converted() || !(kf_replace && kf_replace->current.enabled))
			{
				lui_obituary_hook.invoke<void>(controller_index, attacker, victim, icon, flip, lua_vm);
			}
		}

		void draw()
		{
			if (demo_playback::converted()) return;
			if (!kf_enable || !kf_enable->current.enabled) return;

			const auto font = game::R_RegisterFont("fonts/defaultBold.otf", 32);
			if (!font) return;
			auto* sp = game::ScrPlace_GetActivePlacement();
			if (!sp) return;

			const int now = now_time();
			const int dur = kf_time ? kf_time->current.integer : 5000;

			const float sw = sp->realViewportSize[0];
			const float sh = sp->realViewportSize[1];
			if (sh < 1.0f) return;

			// Everything below (font, icon, gaps, y-offset) is in real pixels, so a fixed size
			// looks ~2x bigger at 1080p than at 4K. The defaults were tuned at 4K, so anchor there
			// and scale every pixel dimension by the actual vertical resolution -> the feed keeps
			// the same on-screen proportion at 1080p / 1440p / 4K without re-tuning killfeed_scale.
			const float res_scale = sh / 2160.0f;

			const float scale = (kf_scale ? kf_scale->current.value : 0.9f) * res_scale;
			const float text_h = static_cast<float>(game::R_GetFontHeight(font)) * scale;
			const float gap = 8.0f * res_scale;
			const float line_h = text_h + gap;
			const float icon_h = text_h * dvar_f(kf_icon_h, 1.0f);
			const float wide_aspect = dvar_f(kf_icon_w, 2.0f);
			const float icon_yoff = dvar_f(kf_icon_yoff, -8.0f) * res_scale;
			const bool mirror = !kf_icon_flip || kf_icon_flip->current.enabled;

			// Title-safe placement -- all fractions of the screen, so it's resolution-independent.
			// The feed is clamped to stay within a safe inset from the bottom/top edges.
			const float safe = std::clamp(dvar_f(kf_safe, 0.05f), 0.0f, 0.2f);
			const float left = sw * dvar_f(kf_x, 0.018f);
			const float safe_top = sh * safe;
			const float safe_bottom = sh * (1.0f - safe) - text_h * 0.25f; // leave descender room
			float y = std::min(sh * dvar_f(kf_y, 0.77f), safe_bottom);

			const int line_count = kf_lines ? std::clamp(kf_lines->current.integer, 1, 7) : 4;
			const float overflow_fade = static_cast<float>(kf_overflow_fade ? kf_overflow_fade->current.integer : 500);

			std::lock_guard<std::mutex> lock(g_mutex);
			// drop lines that have lived out their full lifetime (oldest first)
			while (!g_entries.empty() && (now - g_entries.front().time) > dur) g_entries.pop_front();

			// con_gameMsgWindow0 behaviour: only the newest `line_count` lines are stable; any line
			// pushed beyond it (up to maxLines = line_count + 3) fades ON ITS OWN regardless of age.
			// That is what lets a quad-feed + collateral show "6 on screen" briefly before the extras
			// fade -- the fade runs on serverTime, so it also slows with timescale / avidemo capture.
			{
				const int total = static_cast<int>(g_entries.size());
				for (int i = 0; i < total; ++i)
				{
					const int pos = total - 1 - i; // 0 = newest (bottom)
					if (pos >= line_count)
					{
						if (g_entries[i].overflow_start == 0) g_entries[i].overflow_start = now ? now : 1;
					}
					else
					{
						g_entries[i].overflow_start = 0; // back inside the visible count (e.g. lines raised)
					}
				}
				// remove overflow lines whose self-fade has completed (always the oldest = front)
				while (!g_entries.empty())
				{
					const auto& f = g_entries.front();
					if (f.overflow_start != 0 && static_cast<float>(now - f.overflow_start) >= overflow_fade)
						g_entries.pop_front();
					else
						break;
				}
			}

			// CoD4-style: bottom-left, left-aligned, newest at the bottom, older stacking upward
			for (auto it = g_entries.rbegin(); it != g_entries.rend(); ++it)
			{
				if (y < safe_top) break; // don't stack past the safe-top inset
				const auto& e = *it;
				const float age = static_cast<float>(now - e.time);
				if (age < 0.0f || age > static_cast<float>(dur)) { continue; }

				float a = 1.0f; // fade in first 150ms, out last 600ms (of GAME time)
				if (age < 150.0f) a = age / 150.0f;
				else if (age > static_cast<float>(dur) - 600.0f) a = (static_cast<float>(dur) - age) / 600.0f;
				// overflow lines (beyond line_count) fade themselves over overflow_fade ms of GAME time
				if (e.overflow_start != 0 && overflow_fade > 0.0f)
				{
					const float ov = 1.0f - static_cast<float>(now - e.overflow_start) / overflow_fade;
					a = std::min(a, ov);
				}
				a = std::clamp(a, 0.0f, 1.0f);

				// names tinted by team (axis/allies, configurable); the icon stays white.
				float acol[4]; team_color(e.attacker_team, a, acol);
				float vcol[4]; team_color(e.victim_team, a, vcol);
				float icol[4] = { 1.0f, 1.0f, 1.0f, a };

				float x = left;
				// attacker
				R_AddCmdDrawText(e.attacker.c_str(), 0x7FFFFFFF, font, x, y, scale, scale, 0.0f, acol, 0);
				x += static_cast<float>(game::R_TextWidth(e.attacker.c_str(), 0x7FFFFFFF, font)) * scale + gap;
				// per-icon aspect: square for death icons (headshot/suicide/grenade...), wide for weapons.
				const float icon_w = icon_h * (icon_is_square(e.icon) ? 1.0f : wide_aspect);
				// weapon icon -- vertically centered on the text (text y is the BASELINE, glyphs draw
				// upward). mirror so the barrel points attacker -> victim (right). All tunable via dvars.
				if (e.icon)
				{
					const float s0 = mirror ? 1.0f : 0.0f;
					const float s1 = mirror ? 0.0f : 1.0f;
					const float iy = y - text_h * 0.80f - (icon_h - text_h) * 0.5f + icon_yoff;
					game::R_AddCmdDrawStretchPic(x, iy, icon_w, icon_h, s0, 0.0f, s1, 1.0f, icol, e.icon);
				}
				x += icon_w + gap;
				// victim
				R_AddCmdDrawText(e.victim.c_str(), 0x7FFFFFFF, font, x, y, scale, scale, 0.0f, vcol, 0);

				y -= line_h; // next (older) entry sits above
			}
		}

		void cg_draw2d_stub(int local_client_num)
		{
			// The local match is only a renderer for the imported camera. Its
			// countdown and HUD are unrelated to the recorded COD4 session.
			// Gate the existing draw hook from the first map frame, including
			// frames before the camera track becomes ready. F9 draws separately.
			if (cod4_camera::active() && !game::VirtualLobby_Loaded()) return;
			cg_draw2d_hook.invoke<void>(local_client_num); // stock HUD first
			draw();                                        // our killfeed on top
		}

		// The "X connected" feed spam is a "player_joined" LUI event fired from these two
		// dispatchers (sub_138A60 / sub_1B5890). Skipping them entirely suppresses the message
		// with no side effects on the obituary feed. Toggle via killfeed_mute_connect.
		bool mute_connect() { return kf_mute_connect && kf_mute_connect->current.enabled; }

		std::int64_t player_joined_stub_a(unsigned int a1)
		{
			if (mute_connect()) return 0;
			return player_joined_hook_a.invoke<std::int64_t>(a1);
		}

		std::int64_t player_joined_stub_b(unsigned int a1)
		{
			if (mute_connect()) return 0;
			return player_joined_hook_b.invoke<std::int64_t>(a1);
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

			kf_enable = dvars::register_bool("killfeed_custom", true, game::DVAR_FLAG_SAVED,
				"draw a custom killfeed that slows/freezes with timescale slow-mo and avidemo capture");
			kf_replace = dvars::register_bool("killfeed_replace_lui", false, game::DVAR_FLAG_SAVED,
				"suppress the stock LUI killfeed (leave off to show both while testing)");
			kf_time = dvars::register_int("killfeed_time", 5000, 500, 60000, game::DVAR_FLAG_SAVED,
				"custom killfeed entry lifetime in GAME-time ms (slows with the demo clock)");
			kf_scale = dvars::register_float("killfeed_scale", 1.7f, 0.1f, 4.0f, game::DVAR_FLAG_SAVED,
				"custom killfeed text scale");
			kf_icon_w = dvars::register_float("killfeed_icon_w", 2.0f, 0.5f, 6.0f, game::DVAR_FLAG_SAVED,
				"weapon-icon width as a multiple of its height (wide for weapons, ~1 for square icons)");
			kf_icon_h = dvars::register_float("killfeed_icon_h", 1.0f, 0.3f, 3.0f, game::DVAR_FLAG_SAVED,
				"weapon-icon height as a multiple of the text height");
			kf_icon_yoff = dvars::register_float("killfeed_icon_yoff", -8.0f, -50.0f, 50.0f, game::DVAR_FLAG_SAVED,
				"weapon-icon vertical fine-tune in pixels (- = up)");
			kf_icon_flip = dvars::register_bool("killfeed_icon_flip", true, game::DVAR_FLAG_SAVED,
				"mirror the weapon icon so the barrel points attacker -> victim (toggle if backwards)");

			// con_gameMsgWindow0-style line fade: newest `killfeed_lines` stay solid; extras (up to
			// +3 = 7 total) fade themselves so a quad + collateral shows "6 on screen" then thins out.
			kf_lines = dvars::register_int("killfeed_lines", 4, 1, 7, game::DVAR_FLAG_SAVED,
				"fully-visible killfeed lines; extra lines (up to +3, 7 max) fade on their own");
			kf_overflow_fade = dvars::register_int("killfeed_overflow_fade", 500, 50, 5000, game::DVAR_FLAG_SAVED,
				"ms for a line pushed beyond killfeed_lines to fade out (game-time; slows with timescale)");

			// position/size are resolution-independent: killfeed_scale is tuned at 4K and auto-scales
			// down at 1080p/1440p; x/y are screen fractions; safe keeps it inside a title-safe inset.
			kf_x = dvars::register_float("killfeed_x", 0.018f, 0.0f, 1.0f, game::DVAR_FLAG_SAVED,
				"killfeed left edge as a fraction of screen width");
			kf_y = dvars::register_float("killfeed_y", 0.77f, 0.0f, 1.0f, game::DVAR_FLAG_SAVED,
				"killfeed bottom-entry baseline as a fraction of screen height (lower value = higher up)");
			kf_safe = dvars::register_float("killfeed_safe", 0.05f, 0.0f, 0.2f, game::DVAR_FLAG_SAVED,
				"title-safe inset (fraction) keeping the feed off the screen edges");

			// team colours (axis/allies) -- vec4 RGBA dvars are safe to SAVE (no script-string
			// interning like string dvars, which leak the pool and crash at the menu)
			kf_team_colors = dvars::register_bool("killfeed_team_colors", true, game::DVAR_FLAG_SAVED,
				"colour killfeed names by team (axis/allies)");
			kf_col_axis = dvars::register_vec4("killfeed_color_axis", 1.0f, 0.42f, 0.42f, 1.0f,
				0.0f, 1.0f, game::DVAR_FLAG_SAVED, "axis (team 1) name colour RGBA");
			kf_col_allies = dvars::register_vec4("killfeed_color_allies", 0.45f, 0.72f, 1.0f, 1.0f,
				0.0f, 1.0f, game::DVAR_FLAG_SAVED, "allies (team 2) name colour RGBA");
			kf_col_neutral = dvars::register_vec4("killfeed_color_neutral", 1.0f, 1.0f, 1.0f, 1.0f,
				0.0f, 1.0f, game::DVAR_FLAG_SAVED, "neutral/other name colour RGBA");
			kf_mute_connect = dvars::register_bool("killfeed_mute_connect", true, game::DVAR_FLAG_SAVED,
				"mute the 'X connected' player-join messages in the feed");

			lui_obituary_hook.create(0x26E6F0_b, lui_obituary_stub); // LUI_Obituary -> capture kills
			cg_draw2d_hook.create(0xF57D0_b, cg_draw2d_stub);        // CG_Draw2D    -> draw our feed
			player_joined_hook_a.create(0x138A60_b, player_joined_stub_a); // suppress "X connected"
			player_joined_hook_b.create(0x1B5890_b, player_joined_stub_b);
		}
	};
}

REGISTER_COMPONENT(killfeed::component)
