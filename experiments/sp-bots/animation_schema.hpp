#pragma once
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace spai_probe::animation_schema
{
    inline constexpr unsigned current = 8;
    inline constexpr const char* network_capability = "10";
    inline constexpr const char* current_wire = "9";
    inline constexpr const char* current_recording_marker = "H1SPAI:8";
    inline bool supports_score_wire(std::string_view wire, bool validated_replay)
    {
        if(wire==current_wire)return true;
        // Historical score payloads have their own envelope version/epoch.
        // Allow their published transport IDs only inside a validated replay.
        return validated_replay && (wire=="1" || wire=="2" || wire=="3" ||
            wire=="4" || wire=="5" || wire=="6" || wire=="7" || wire=="8");
    }
    struct state_definition
    {
        const char* name;
        const char* server_slot;
        const char* client_slot;
        size_t campaign_index;
        bool single_cycle;
    };
    // Schema 1 is exactly the first ten entries, in this order. Never replace
    // its vault clip when adding traversal types: recordings contain indices.
    inline constexpr std::array<state_definition, 34> states{{
        {"idle", "mp_stand_idle", "mp_stand_idle", 1, false},
        {"move", "pb_stand_shoot_walk_forward", "pb_stand_shoot_walk_forward", 2, false},
        {"reaction", "mp_crouch_idle", "mp_crouch_idle", 3, false},
        {"cover_left", "mp_stand_reload", "mp_stand_reload", 4, false},
        {"cover_right", "mp_crouch_reload", "mp_crouch_reload", 5, false},
        {"vault", "mp_stand_grenade_throw", "h1_mp_hold_grenade_throw", 6, false},
        {"fire", "mp_prone_idle_iw6", "mp_prone_idle_iw6", 9, true},
        {"aim", "mp_stand_ads_aim_5", "mp_stand_ads_aim_5", 10, false},
        {"reload", "mp_pistol_stand_reload", "mp_pistol_stand_reload", 11, true},
        {"throwback", "mp_stand_grenade_pull_throwback", "mp_stand_grenade_pull_throwback", 12, true},
        {"mantle", "mp_hold_standjump_land", "mp_hold_standjump_land", 13, true},
        {"death", "mp_prone_reload_iw6", "mp_prone_reload_iw6", 16, true},
        {"run", "mp_hold_run", "mp_hold_run", 17, false},
        {"sprint", "pb_sprint_assault", "pb_sprint_assault", 18, false},
        {"crouch_move", "mp_crouch_f", "mp_crouch_f", 19, false},
        {"move_back", "pb_stand_shoot_walk_back", "pb_stand_shoot_walk_back", 20, false},
        {"move_left", "pb_stand_shoot_walk_left", "pb_stand_shoot_walk_left", 21, false},
        {"move_right", "pb_stand_shoot_walk_right", "pb_stand_shoot_walk_right", 22, false},
        {"move_cqb", "mp_pistol_stand_walk_f", "mp_pistol_stand_walk_f", 23, false},
        {"death_stand", "mp_pistol_stand_idle", "mp_pistol_stand_idle", 24, true},
        {"death_stand_alt", "mp_pistol_crouch_idle", "mp_pistol_crouch_idle", 25, true},
        {"death_head", "mp_pistol_prone_idle", "mp_pistol_prone_idle", 26, true},
        {"death_crouch", "mp_pistol_crouch_reload", "mp_pistol_crouch_reload", 27, true},
        {"death_run_front", "mp_pistol_prone_reload", "mp_pistol_prone_reload", 28, true},
        {"react_stand", "mp_pistol_stand_walk_b", "mp_pistol_stand_walk_b", 29, true},
        {"react_stand_alt", "mp_pistol_stand_walk_l", "mp_pistol_stand_walk_l", 30, true},
        {"react_crouch", "mp_pistol_crouch_walk_f", "mp_pistol_crouch_walk_f", 31, true},
        {"react_run", "mp_pistol_stand_run_f", "mp_pistol_stand_run_f", 32, true},
        {"react_stumble", "mp_pistol_stand_run_b", "mp_pistol_stand_run_b", 33, true},
        {"react_cover_left", "mp_pistol_crouch_walk_l", "mp_pistol_crouch_walk_l", 34, true},
        {"react_cover_right", "mp_pistol_crouch_walk_r", "mp_pistol_crouch_walk_r", 35, true},
        {"death_twist", "mp_pistol_stand_run_l", "mp_pistol_stand_run_l", 36, true},
        {"death_flop", "mp_pistol_stand_run_r", "mp_pistol_stand_run_r", 37, true},
        {"death_knees", "mp_pistol_stand_walk_r", "mp_pistol_stand_walk_r", 38, true}
    }};
    constexpr size_t state_count(unsigned schema)
    {
        return schema == 1 ? 10 : (schema == 2 || schema == 3) ? 11 : schema == 4 ? 12 : schema == 5 ? 19 : (schema == 6 || schema == 7) ? 24 : schema == 8 ? states.size() : 0;
    }
    inline unsigned read_marker(const std::vector<std::string>& fields, bool enabled)
    {
        unsigned schema = 0;
        for (const auto& field : fields)
        {
            if (!field.starts_with("H1SPAI:")) continue;
            if (schema || !enabled || (field != "H1SPAI:1" && field != "H1SPAI:2" && field != "H1SPAI:3" && field != "H1SPAI:4" && field != "H1SPAI:5" && field != "H1SPAI:6" && field != "H1SPAI:7" && field != current_recording_marker))
                throw std::runtime_error("Unsupported or duplicate SP Bots replay animation schema");
            schema = field == "H1SPAI:1" ? 1 : field == "H1SPAI:2" ? 2 : field == "H1SPAI:3" ? 3 : field == "H1SPAI:4" ? 4 : field == "H1SPAI:5" ? 5 : field == "H1SPAI:6" ? 6 : field == "H1SPAI:7" ? 7 : 8;
        }
        return schema;
    }
}
