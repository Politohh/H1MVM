#pragma once
#include "cod4_animation_catalog.hpp"
#include "cod4_body_translation.hpp"

namespace cod4
{
    inline std::string_view actor_animation_name(unsigned animation, std::string_view weapon,
        const std::map<std::string_view, unsigned>& catalog)
    {
        const auto index = animation & 511u;
        if (animation > 1023 || index >= source_body_animations.size())
            throw std::runtime_error("Unsupported COD4 actor animation index: " + std::to_string(animation));
        if (!index) return {};
        const auto name = source_body_animations[index];
        if (name == "unused") throw std::runtime_error("Unused COD4 actor animation index: " + std::to_string(index));
        if (catalog.contains(name)) return name;
        const auto family = weapon.empty() ? body_family::hold : map_weapon(weapon).body;
        const bool pistol = family == body_family::pistol;
        const bool launcher = family == body_family::launcher;
        // MWR splits COD4's shared locomotion clips by held weapon family.
        if (name == "pb_sprint")
        {
            constexpr std::array<std::string_view, 8> clips{"pb_sprint_assault", "pb_sprint_sniper",
                "pb_sprint_lmg", "pb_sprint_smg", "pb_sprint_shotgun", "mp_pistol_sprint", "mp_rpg_sprint", "pb_sprint_hold"};
            return clips.at(static_cast<unsigned>(family));
        }
        constexpr std::array<std::string_view, 4> source_runs{"pb_combatrun_forward_loop", "pb_combatrun_back_loop",
            "pb_combatrun_left_loop", "pb_combatrun_right_loop"};
        constexpr std::array<std::array<std::string_view, 4>, 8> native_runs{{
            {"mp_run_assault_aim_5", "pb_combatrun_back_loop_assault", "pb_combatrun_left_loop_assault", "pb_combatrun_right_loop_assault"},
            {"pb_combatrun_forward_loop_sniper", "pb_combatrun_back_loop_sniper", "pb_combatrun_left_loop_sniper", "pb_combatrun_right_loop_sniper"},
            {"pb_combatrun_forward_loop_lmg", "pb_combatrun_back_loop_lmg", "pb_combatrun_left_loop_lmg", "pb_combatrun_right_loop_lmg"},
            {"pb_combatrun_forward_loop_smg", "pb_combatrun_back_loop_smg", "pb_combatrun_left_loop_smg", "pb_combatrun_right_loop_smg"},
            {"pb_combatrun_forward_loop_shotgun", "pb_combatrun_back_loop_shotgun", "pb_combatrun_left_loop_shotgun", "pb_combatrun_right_loop_shotgun"},
            {"mp_pistol_stand_run_f", "mp_pistol_stand_run_b", "mp_pistol_stand_run_l", "mp_pistol_stand_run_r"},
            {"mp_rpg_run_f", "mp_rpg_run_b", "mp_rpg_run_l", "mp_rpg_run_r"},
            {"mp_hold_run", "mp_hold_run_b", "mp_hold_run_l", "mp_hold_run_r"}
        }};
        for (unsigned i = 0; i < source_runs.size(); ++i)
            if (name == source_runs[i]) return native_runs.at(static_cast<unsigned>(family))[i];
        // Verified Promod/stock names and MWR's live locomotion channels.
        constexpr std::array<std::string_view, 4> directions{"forward", "back", "left", "right"};
        constexpr std::array<std::string_view, 4> crouch{"mp_crouch_f", "mp_crouch_b", "mp_crouch_l", "mp_crouch_r"};
        constexpr std::array<std::string_view, 4> grenade_crouch{"mp_crouch_hold_run", "mp_crouch_hold_run_b", "mp_crouch_hold_run_l", "mp_crouch_hold_run_r"};
        constexpr std::array<std::string_view, 4> pistol_crouch{"mp_pistol_crouch_walk_f", "mp_pistol_crouch_walk_b", "mp_pistol_crouch_walk_l", "mp_pistol_crouch_walk_r"};
        constexpr std::array<std::string_view, 4> pistol_walk{"mp_pistol_stand_walk_f", "mp_pistol_stand_walk_b", "mp_pistol_stand_walk_l", "mp_pistol_stand_walk_r"};
        for (unsigned i = 0; i < directions.size(); ++i)
        {
            const auto direction = std::string(directions[i]);
            if (name == "pb_crouch_run_" + direction || name == "pb_crouch_shoot_run_" + direction) return crouch[i];
            if (name == "pb_crouch_run_" + direction + "_grenade") return grenade_crouch[i];
            if (name == "pb_crouch_run_" + direction + "_pistol" || name == "pb_crouch_walk_" + direction + "_pistol") return pistol_crouch[i];
            if (name == "pb_combatwalk_" + direction + "_loop_pistol") return pistol_walk[i];
            if (name == "pb_combatrun_" + direction + "_loop_pistol") return native_runs[static_cast<unsigned>(body_family::pistol)][i];
            if (name == "pb_combatrun_" + direction + "_loop_grenade") return native_runs[static_cast<unsigned>(body_family::hold)][i];
        }
        if (name == "pb_combatrun_forward_loop_stickgrenade") return "mp_hold_run";
        if (name == "pb_sprint_pistol") return "mp_pistol_sprint";
        if (name == "pb_pistol_run_fast") return "mp_pistol_stand_run_f";
        if (name == "pb_standjump_land_pistol") return "standjump_land_pistol";
        if (name == "pb_prone_aim") return family == body_family::sniper ? "pb_prone_aim_sniper" : "mp_prone_idle_iw6";
        if (name == "pb_prone_aim_pistol") return "mp_pistol_prone_idle";
        if (name == "pb_prone_aim_grenade") return "pb_prone_hold";
        if (name == "pb_prone_grenade_crawl") return "pb_prone_crawl_hold";
        if (name == "pb_prone_grenade_crawl_back") return "pb_prone_crawl_back_hold";
        if (name == "pb_prone_grenade_crawl_left") return "pb_prone_crawl_left_hold";
        if (name == "pb_prone_grenade_crawl_right") return "pb_prone_crawl_right_hold";
        if (name == "pb_prone2crouch") return pistol ? "mp_pistol_prone_2_crouch" : launcher ? "mp_rpg_prone_2_crouch" : "mp_prone_2_crouch";
        if (name == "pb_prone2crouchrun") return pistol ? "mp_pistol_prone_2_crouch_f" : launcher ? "mp_rpg_prone_2_crouch_f" : "mp_iw6_prone_2_crouch_f";
        // Native authored reactions replace these two legacy death clips.
        if (name == "pb_prone_death_quickdeath") return "h1_prone_death_quickdeath";
        if (name == "pb_stand_death_frontspin") return "pb_stand_death_chest_spin";
        if (name == "pb_stand_alert") return "mp_stand_idle";
        if (name == "pb_stand_ads") return "mp_stand_ads_aim_5";
        if (name == "pb_crouch_alert") return "mp_crouch_idle";
        if (name == "pb_crouch_ads") return "mp_crouch_idle_ads";
        // COD4 labels this an unfinished placeholder, used by jumpbk while
        // crouching/prone. MWR has no equivalent; retain the crouched pose.
        if (name == "pb_chicken_dance_crouch") return pistol ? "mp_pistol_crouch_idle"
            : launcher ? "mp_rpg_crouch_idle" : family == body_family::hold ? "mp_crouch_hold_idle" : "mp_crouch_idle";
        if (name == "pb_crouch_alert_pistol" || name == "pb_crouch_ads_pistol") return "mp_pistol_crouch_idle";
        if (name == "pb_stand__shoot_ads_pistol") return "pt_stand_shoot_pistol";
        if (name == "pb_stand_alert_pistol" || name == "pb_stand_ads_pistol") return "mp_pistol_stand_idle";
        if (name == "pb_stand_alert_rpg" || name == "pb_stand_ads_rpg") return "mp_rpg_idle";
        if (name == "pb_stand_grenade_pullpin") return "h1_mp_hold_grenade_pull";
        if (name == "pb_stand_grenade_throw") return "h1_mp_hold_grenade_throw";
        if (name == "pb_crouch_grenade_pullpin") return "h1_mp_crouch_hold_grenade_pull";
        if (name == "pb_crouch_grenade_throw") return "h1_mp_crouch_hold_grenade_throw";
        if (name == "pb_crouch_bombplant") return "mp_srchdstry_crouch_bomb_idle";
        if (name == "pb_prone_bombplant") return "mp_srchdstry_prone_bomb_idle";
        if (name == "mp_mantle_over_mid") return "mp_mantle_over_33";
        if (name == "pt_stand_grenade_throw") return "h1_mp_hold_grenade_throw";
        if (name == "pt_crouch_grenade_throw") return "h1_mp_crouch_hold_grenade_throw";
        if (name == "pt_prone_grenade_throw") return "h1_mp_prone_hold_grenade_throw";
        if (name == "pt_rifle_fire_ads") return "pt_stand_shoot_ads";
        if (name == "pt_stand_shoot_shotgun") return "pt_stand_shoot";
        // COD4's directional flinches have no one-to-one directional MWR clip.
        // MWR provides authored variants by weapon pose; the A clips have a
        // compatible upper-body channel and preserve the source restart bit.
        if (name.starts_with("pt_flinch_grenade_")) return "h1_mp_hold_flinch_a";
        if (name.starts_with("pt_flinch_pistol_")) return "mp_flinch_pistol_stand_a";
        if (name.starts_with("pt_flinch_")) return launcher ? "mp_rpg_stand_flinch_a" : "mp_flinch_stand_a";
        if (name.starts_with("pt_melee_crouch_")) return pistol ? "h1_mp_pistol_crouch_melee_swipe"
            : launcher ? "h1_mp_rpg_crouch_melee_swipe" : "h1_mp_assault_crouch_melee_swipe";
        if (name.starts_with("pt_melee_")) return pistol ? "h1_mp_pistol_stand_melee_swipe"
            : launcher ? "h1_mp_rpg_stand_melee_swipe" : "h1_mp_assault_stand_melee_swipe";
        if (name == "pt_crouch_shoot_ads") return "pt_crouch_shoot";
        if (name == "pt_crouch_shoot_ads_pistol") return "pt_crouch_shoot_pistol";
        if (name == "pt_stand_pullout_pose") return pistol ? "mp_stand_weapon_swap_lg2pistol" : "pt_stand_core_pullout";
        if (name == "pt_crouch_pullout_pose") return pistol ? "mp_crouch_weapon_swap_lg2pistol" : "mp_crouch_weapon_swap_iw6";
        if (name == "pt_prone_pullout_pose") return pistol ? "mp_prone_weapon_swap_lg2pistol" : "mp_prone_weapon_switch_iw6";
        if (name.starts_with("pt_reload_stand_")) return pistol ? "mp_pistol_stand_reload" : launcher ? "mp_rpg_reload" : "mp_stand_reload";
        if (name.starts_with("pt_reload_crouch_")) return pistol ? "mp_pistol_crouch_reload" : launcher ? "mp_rpg_crouch_reload" : "mp_crouch_reload";
        if (name == "pt_reload_crouchwalk_pistol") return "mp_pistol_crouch_reload";
        if (name.starts_with("pt_reload_prone_")) return pistol ? "mp_pistol_prone_reload" : launcher ? "mp_rpg_prone_reload" : "mp_prone_reload_iw6";
        // The remaster replaces COD4's stun stumble with its own flinch clips.
        if (name.starts_with("pb_stumble_")) return pistol ? "mp_flinch_pistol_stand_a" : launcher ? "mp_rpg_stand_flinch_a" : "mp_flinch_stand_a";
        throw std::runtime_error("Remastered actor animation mapping unavailable: " + std::string(name));
    }

    inline unsigned resolve_actor_animation(unsigned source, std::string_view weapon,
        const std::map<std::string_view, unsigned>& catalog)
    {
        return resolve_body_animation(actor_animation_name(source, weapon, catalog),
            (source & 512u) ? 2048u : 0u, catalog);
    }
}
