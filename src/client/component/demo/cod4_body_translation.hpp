#pragma once
#include "cod4_playerstate_reader.hpp"
#include "cod4_weapon_catalog.hpp"

namespace cod4
{
    // These are names in MWR's multiplayer animation tree, not COD4 asset
    // ordinals. The converter resolves each name in the loaded H1 BG table.
    struct body_animation
    {
        std::string_view legs, torso;
        unsigned legs_restart{}, torso_restart{};
    };

    inline unsigned translate_movement_flags(unsigned source)
    {
        // Stance, ladder, ADS, backward movement and walking share these bits.
        // COD4's sprint/shellshock bits moved to 0x4000/0x8000 in MWR.
        return (source & 0x7Bu) | ((source & 0x8000u) ? 0x4000u : 0u)
            | ((source & 0x4000u) ? 0x8000u : 0u);
    }

    inline int translate_body_heading(const camera_frame& movement)
    {
        // COD4 movementDir is signed degrees relative to view yaw. H1's
        // PS (+0xC8), player entity and prediction archive instead carry an
        // absolute yaw byte. Its native angle encoder rounds to the nearest
        // 360/256-degree step. Keep the recorded relative heading, including
        // backward/sideways movement, rather than turning the body to velocity.
        const float yaw = movement.angles[1];
        if (!std::isfinite(yaw) || movement.movement_dir < -128 || movement.movement_dir > 127)
            throw std::runtime_error("Invalid recorded body heading");
        const float absolute = std::fmod(yaw, 360.0f) + static_cast<float>(movement.movement_dir);
        const auto packed = static_cast<int>(std::floor(absolute * (256.0f / 360.0f) + 0.5f));
        return packed & 255;
    }

    inline body_animation select_body_animation(const player_frame& player, const camera_frame& movement,
        std::string_view weapon)
    {
        const auto family = weapon.empty() ? body_family::hold : map_weapon(weapon).body;
        const bool pistol = family == body_family::pistol;
        const bool launcher = family == body_family::launcher, hold = family == body_family::hold;
        const auto flags = player.integer("pm_flags");
        const bool prone = (flags & 1u) != 0, crouch = (flags & 2u) != 0;
        // Local prediction fields can be omitted from server snapshots. The
        // demo's archive retains the actual local movement used by its camera.
        const auto vx = movement.velocity[0], vy = movement.velocity[1];
        const float speed = std::hypot(vx, vy);
        const float yaw = movement.angles[1] * (3.14159265358979323846f / 180.0f);
        if (!std::isfinite(speed) || !std::isfinite(yaw))
            throw std::runtime_error("Invalid recorded body movement");
        const float forward = vx * std::cos(yaw) + vy * std::sin(yaw);
        const float left = -vx * std::sin(yaw) + vy * std::cos(yaw);
        // Forward, backward, left, right in the recorded player's frame.
        const unsigned direction = std::abs(forward) >= std::abs(left) ? (forward < 0 ? 1u : 0u)
            : (left > 0 ? 2u : 3u);
        body_animation result;
        result.legs_restart = (player.integer("legsAnim") & 512u) ? 2048u : 0u;
        result.torso_restart = (player.integer("torsoAnim") & 512u) ? 2048u : 0u;
        if (player.integer("groundEntityNum") == 1023 && movement.velocity[2] != 0)
            result.legs = pistol ? (speed > 5 ? "runjump_takeoff_pistol" : "standjump_takeoff_pistol")
                : launcher ? (speed > 5 ? "runjump_takeoff_rpg" : "standjump_takeoff_rpg")
                : hold ? (speed > 5 ? "mp_hold_runjump_takeoff" : "mp_hold_standjump_takeoff")
                : (speed > 5 ? "pb_runjump_takeoff" : "pb_standjump_takeoff");
        else if ((flags & 0x8000u) && !prone && !crouch)
        {
            constexpr std::array<std::string_view, 8> sprints{
                "pb_sprint_assault", "pb_sprint_sniper", "pb_sprint_lmg", "pb_sprint_smg",
                "pb_sprint_shotgun", "mp_pistol_sprint", "mp_rpg_sprint", "pb_sprint_hold"};
            result.legs = sprints[static_cast<unsigned>(family)];
        }
        else if (speed <= 5)
        {
            if (pistol) result.legs = prone ? "mp_pistol_prone_idle" : crouch ? "mp_pistol_crouch_idle" : "mp_pistol_stand_idle";
            else if (launcher) result.legs = prone ? "mp_rpg_prone_idle" : crouch ? "mp_rpg_crouch_idle" : "mp_rpg_idle";
            else if (hold) result.legs = prone ? "pb_prone_hold" : crouch ? "mp_crouch_hold_idle" : "mp_hold_idle";
            else result.legs = prone ? "mp_prone_idle_iw6" : crouch ? "mp_crouch_idle" : "mp_stand_idle";
        }
        else
        {
            constexpr std::array<std::string_view, 4> pistol_prone{
                "mp_pistol_prone_f", "mp_pistol_prone_b", "mp_pistol_prone_l", "mp_pistol_prone_r"};
            constexpr std::array<std::string_view, 4> rifle_prone{
                "pb_prone_crawl", "pb_prone_crawl_back", "pb_prone_crawl_left", "pb_prone_crawl_right"};
            constexpr std::array<std::string_view, 4> pistol_crouch{
                "mp_pistol_crouch_walk_f", "mp_pistol_crouch_walk_b", "mp_pistol_crouch_walk_l", "mp_pistol_crouch_walk_r"};
            constexpr std::array<std::string_view, 4> rifle_crouch{
                "mp_crouch_f", "mp_crouch_b", "mp_crouch_l", "mp_crouch_r"};
            constexpr std::array<std::string_view, 4> pistol_walk{
                "mp_pistol_stand_walk_f", "mp_pistol_stand_walk_b", "mp_pistol_stand_walk_l", "mp_pistol_stand_walk_r"};
            constexpr std::array<std::string_view, 4> rifle_walk{
                "pb_stand_shoot_walk_forward", "pb_stand_shoot_walk_back", "pb_stand_shoot_walk_left", "pb_stand_shoot_walk_right"};
            constexpr std::array<std::string_view, 4> pistol_run{
                "mp_pistol_stand_run_f", "mp_pistol_stand_run_b", "mp_pistol_stand_run_l", "mp_pistol_stand_run_r"};
            constexpr std::array<std::string_view, 4> launcher_prone{
                "mp_rpg_prone_f", "mp_rpg_prone_b", "mp_rpg_prone_l", "mp_rpg_prone_r"};
            constexpr std::array<std::string_view, 4> launcher_crouch{
                "mp_rpg_crouch_walk_f", "mp_rpg_crouch_walk_b", "mp_rpg_crouch_walk_l", "mp_rpg_crouch_walk_r"};
            constexpr std::array<std::string_view, 4> launcher_walk{
                "mp_rpg_walk_f", "mp_rpg_walk_b", "mp_rpg_walk_l", "mp_rpg_walk_r"};
            constexpr std::array<std::string_view, 4> launcher_run{
                "mp_rpg_run_f", "mp_rpg_run_b", "mp_rpg_run_l", "mp_rpg_run_r"};
            constexpr std::array<std::string_view, 4> hold_prone{
                "pb_prone_crawl_hold", "pb_prone_crawl_back_hold", "pb_prone_crawl_left_hold", "pb_prone_crawl_right_hold"};
            constexpr std::array<std::string_view, 4> hold_crouch{
                "mp_crouch_hold_run", "mp_crouch_hold_run_b", "mp_crouch_hold_run_l", "mp_crouch_hold_run_r"};
            constexpr std::array<std::string_view, 4> hold_run{
                "mp_hold_run", "mp_hold_run_b", "mp_hold_run_l", "mp_hold_run_r"};
            constexpr std::array<std::array<std::string_view, 4>, 5> longgun_run{{
                // The native forward assault entry is the centre running aim clip.
                {"mp_run_assault_aim_5", "pb_combatrun_back_loop_assault", "pb_combatrun_left_loop_assault", "pb_combatrun_right_loop_assault"},
                {"pb_combatrun_forward_loop_sniper", "pb_combatrun_back_loop_sniper", "pb_combatrun_left_loop_sniper", "pb_combatrun_right_loop_sniper"},
                {"pb_combatrun_forward_loop_lmg", "pb_combatrun_back_loop_lmg", "pb_combatrun_left_loop_lmg", "pb_combatrun_right_loop_lmg"},
                {"pb_combatrun_forward_loop_smg", "pb_combatrun_back_loop_smg", "pb_combatrun_left_loop_smg", "pb_combatrun_right_loop_smg"},
                {"pb_combatrun_forward_loop_shotgun", "pb_combatrun_back_loop_shotgun", "pb_combatrun_left_loop_shotgun", "pb_combatrun_right_loop_shotgun"}
            }};
            if (prone) result.legs = (pistol ? pistol_prone : launcher ? launcher_prone : hold ? hold_prone : rifle_prone)[direction];
            else if (crouch) result.legs = (pistol ? pistol_crouch : launcher ? launcher_crouch : hold ? hold_crouch : rifle_crouch)[direction];
            else if (hold) result.legs = hold_run[direction];
            else if ((flags & 0x50u) || player.real("fWeaponPosFrac") > 0 || speed < 110)
                result.legs = (pistol ? pistol_walk : launcher ? launcher_walk : rifle_walk)[direction];
            else result.legs = pistol ? pistol_run[direction] : launcher ? launcher_run[direction]
                : longgun_run[static_cast<unsigned>(family)][direction];
        }

        const auto state = player.integer("weaponstate");
        if (state >= 15 && state <= 17)
            result.torso = prone ? "h1_mp_prone_hold_grenade_pull" : crouch ? "h1_mp_crouch_hold_grenade_pull" : "h1_mp_hold_grenade_pull";
        else if (state >= 18 && state <= 20)
            result.torso = prone ? "h1_mp_prone_hold_grenade_throw" : crouch ? "h1_mp_crouch_hold_grenade_throw" : "h1_mp_hold_grenade_throw";
        else if (state >= 7 && state <= 11)
            result.torso = pistol ? (prone ? "mp_pistol_prone_reload" : crouch ? "mp_pistol_crouch_reload" : "mp_pistol_stand_reload")
                : launcher ? (prone ? "mp_rpg_prone_reload" : crouch ? "mp_rpg_crouch_reload" : "mp_rpg_reload")
                : (prone ? "mp_prone_reload_iw6" : crouch ? "mp_crouch_reload" : "mp_stand_reload");
        else if (state >= 12 && state <= 14)
            result.torso = pistol ? (prone ? "h1_mp_pistol_prone_melee_swipe" : crouch ? "h1_mp_pistol_crouch_melee_swipe" : "h1_mp_pistol_stand_melee_swipe")
                : launcher ? (prone ? "h1_mp_rpg_prone_melee_swipe" : crouch ? "h1_mp_rpg_crouch_melee_swipe" : "h1_mp_rpg_stand_melee_swipe")
                : (prone ? "h1_mp_assault_prone_melee_swipe" : crouch ? "h1_mp_assault_crouch_melee_swipe" : "h1_mp_assault_stand_melee_swipe");
        else if (state == 5 || state == 6)
            result.torso = pistol ? (prone ? "pt_prone_shoot_pistol" : crouch ? "pt_crouch_shoot_pistol" : "pt_stand_shoot_pistol")
                : launcher ? (prone ? "pt_prone_shoot_rpg" : crouch ? "h1_mp_rpg_crouch_fire" : "pt_stand_shoot_rpg")
                : (prone ? "pt_prone_shoot_auto" : crouch ? "pt_crouch_shoot" : player.real("fWeaponPosFrac") >= 0.5f ? "pt_stand_shoot_ads" : "pt_rifle_fire");
        // Zero torso animation inherits the native leg clip's upper body pose.
        return result;
    }

    inline unsigned resolve_body_animation(std::string_view name, unsigned restart,
        const std::map<std::string_view, unsigned>& native_catalog)
    {
        if (name.empty()) return restart;
        const auto found = native_catalog.find(name);
        if (found == native_catalog.end() || !found->second || found->second >= 2048)
            throw std::runtime_error("Remastered body animation unavailable: " + std::string(name));
        return found->second | restart;
    }
}
