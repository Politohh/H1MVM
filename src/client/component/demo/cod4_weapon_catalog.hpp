#pragma once
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cod4
{
    enum class body_family { rifle, sniper, lmg, smg, shotgun, pistol, launcher, hold };
    struct weapon_mapping
    {
        std::string_view source, base, attachment;
        body_family body;
        bool alternate{};
    };
    // Standard iw_11.iwd MP identities mapped to MWR's native BG weapon
    // definitions and mp/attachkits.csv. No source models or clips are imported.
    // gl_<rifle> is the alternate mode of that rifle's native GL kit.
    inline constexpr std::array<weapon_mapping, 108> weapon_mappings{{
        {"airstrike_mp", "airstrike_mp", "none", body_family::hold, false},
        {"ak47_acog_mp", "h1_ak47_mp", "acog", body_family::rifle, false},
        {"ak47_gl_mp", "h1_ak47_mp", "gl", body_family::rifle, false},
        {"ak47_mp", "h1_ak47_mp", "none", body_family::rifle, false},
        {"ak47_reflex_mp", "h1_ak47_mp", "reflex", body_family::rifle, false},
        {"ak47_silencer_mp", "h1_ak47_mp", "silencer", body_family::rifle, false},
        {"ak74u_acog_mp", "h1_ak74u_mp", "acog", body_family::smg, false},
        {"ak74u_mp", "h1_ak74u_mp", "none", body_family::smg, false},
        {"ak74u_reflex_mp", "h1_ak74u_mp", "reflex", body_family::smg, false},
        {"ak74u_silencer_mp", "h1_ak74u_mp", "silencer", body_family::smg, false},
        {"artillery_mp", "artillery_mp", "none", body_family::hold, false},
        {"barrett_acog_mp", "h1_barrett_mp", "acog", body_family::sniper, false},
        {"barrett_mp", "h1_barrett_mp", "none", body_family::sniper, false},
        {"beretta_mp", "h1_beretta_mp", "none", body_family::pistol, false},
        {"beretta_silencer_mp", "h1_beretta_mp", "silencer", body_family::pistol, false},
        {"briefcase_bomb_defuse_mp", "briefcase_bomb_defuse_mp", "none", body_family::hold, false},
        {"briefcase_bomb_mp", "briefcase_bomb_mp", "none", body_family::hold, false},
        {"c4_mp", "h1_c4_mp", "none", body_family::hold, false},
        {"claymore_mp", "h1_claymore_mp", "none", body_family::hold, false},
        {"colt45_mp", "h1_colt45_mp", "none", body_family::pistol, false},
        {"colt45_silencer_mp", "h1_colt45_mp", "silencer", body_family::pistol, false},
        {"concussion_grenade_mp", "h1_concussiongrenade_mp", "none", body_family::hold, false},
        {"defaultweapon_mp", "defaultweapon_mp", "none", body_family::rifle, false},
        {"deserteaglegold_mp", "h1_deserteagle55_mp", "none", body_family::pistol, false},
        {"deserteagle_mp", "h1_deserteagle_mp", "none", body_family::pistol, false},
        {"dragunov_acog_mp", "h1_dragunov_mp", "acog", body_family::sniper, false},
        {"dragunov_mp", "h1_dragunov_mp", "none", body_family::sniper, false},
        {"flash_grenade_mp", "h1_flashgrenade_mp", "none", body_family::hold, false},
        {"frag_grenade_mp", "h1_fraggrenade_mp", "none", body_family::hold, false},
        {"frag_grenade_short_mp", "h1_fraggrenadeshort_mp", "none", body_family::hold, false},
        {"g36c_acog_mp", "h1_g36c_mp", "acog", body_family::rifle, false},
        {"g36c_gl_mp", "h1_g36c_mp", "gl", body_family::rifle, false},
        {"g36c_mp", "h1_g36c_mp", "none", body_family::rifle, false},
        {"g36c_reflex_mp", "h1_g36c_mp", "reflex", body_family::rifle, false},
        {"g36c_silencer_mp", "h1_g36c_mp", "silencer", body_family::rifle, false},
        {"g3_acog_mp", "h1_g3_mp", "acog", body_family::rifle, false},
        {"g3_gl_mp", "h1_g3_mp", "gl", body_family::rifle, false},
        {"g3_mp", "h1_g3_mp", "none", body_family::rifle, false},
        {"g3_reflex_mp", "h1_g3_mp", "reflex", body_family::rifle, false},
        {"g3_silencer_mp", "h1_g3_mp", "silencer", body_family::rifle, false},
        {"gl_ak47_mp", "h1_ak47_mp", "gl", body_family::rifle, true},
        {"gl_g36c_mp", "h1_g36c_mp", "gl", body_family::rifle, true},
        {"gl_g3_mp", "h1_g3_mp", "gl", body_family::rifle, true},
        {"gl_m14_mp", "h1_m14_mp", "gl", body_family::rifle, true},
        {"gl_m16_mp", "h1_m16_mp", "gl", body_family::rifle, true},
        {"gl_m4_mp", "h1_m4_mp", "gl", body_family::rifle, true},
        {"helicopter_mp", "helicopter_mp", "none", body_family::hold, false},
        {"m1014_grip_mp", "h1_m1014_mp", "grip", body_family::shotgun, false},
        {"m1014_mp", "h1_m1014_mp", "none", body_family::shotgun, false},
        {"m1014_reflex_mp", "h1_m1014_mp", "reflex", body_family::shotgun, false},
        {"m14_acog_mp", "h1_m14_mp", "acog", body_family::rifle, false},
        {"m14_gl_mp", "h1_m14_mp", "gl", body_family::rifle, false},
        {"m14_mp", "h1_m14_mp", "none", body_family::rifle, false},
        {"m14_reflex_mp", "h1_m14_mp", "reflex", body_family::rifle, false},
        {"m14_silencer_mp", "h1_m14_mp", "silencer", body_family::rifle, false},
        {"m16_acog_mp", "h1_m16_mp", "acog", body_family::rifle, false},
        {"m16_gl_mp", "h1_m16_mp", "gl", body_family::rifle, false},
        {"m16_mp", "h1_m16_mp", "none", body_family::rifle, false},
        {"m16_reflex_mp", "h1_m16_mp", "reflex", body_family::rifle, false},
        {"m16_silencer_mp", "h1_m16_mp", "silencer", body_family::rifle, false},
        {"m21_acog_mp", "h1_m21_mp", "acog", body_family::sniper, false},
        {"m21_mp", "h1_m21_mp", "none", body_family::sniper, false},
        {"m40a3_acog_mp", "h1_m40a3_mp", "acog", body_family::sniper, false},
        {"m40a3_mp", "h1_m40a3_mp", "none", body_family::sniper, false},
        {"m4_acog_mp", "h1_m4_mp", "acog", body_family::rifle, false},
        {"m4_gl_mp", "h1_m4_mp", "gl", body_family::rifle, false},
        {"m4_mp", "h1_m4_mp", "none", body_family::rifle, false},
        {"m4_reflex_mp", "h1_m4_mp", "reflex", body_family::rifle, false},
        {"m4_silencer_mp", "h1_m4_mp", "silencer", body_family::rifle, false},
        {"m60e4_acog_mp", "h1_m60e4_mp", "acog", body_family::lmg, false},
        {"m60e4_grip_mp", "h1_m60e4_mp", "grip", body_family::lmg, false},
        {"m60e4_mp", "h1_m60e4_mp", "none", body_family::lmg, false},
        {"m60e4_reflex_mp", "h1_m60e4_mp", "reflex", body_family::lmg, false},
        {"mp44_mp", "h1_mp44_mp", "none", body_family::rifle, false},
        {"mp5_acog_mp", "h1_mp5_mp", "acog", body_family::smg, false},
        {"mp5_mp", "h1_mp5_mp", "none", body_family::smg, false},
        {"mp5_reflex_mp", "h1_mp5_mp", "reflex", body_family::smg, false},
        {"mp5_silencer_mp", "h1_mp5_mp", "silencer", body_family::smg, false},
        {"p90_acog_mp", "h1_p90_mp", "acog", body_family::smg, false},
        {"p90_mp", "h1_p90_mp", "none", body_family::smg, false},
        {"p90_reflex_mp", "h1_p90_mp", "reflex", body_family::smg, false},
        {"p90_silencer_mp", "h1_p90_mp", "silencer", body_family::smg, false},
        {"radar_mp", "radar_mp", "none", body_family::hold, false},
        {"remington700_acog_mp", "h1_remington700_mp", "acog", body_family::sniper, false},
        {"remington700_mp", "h1_remington700_mp", "none", body_family::sniper, false},
        {"rpd_acog_mp", "h1_rpd_mp", "acog", body_family::lmg, false},
        {"rpd_grip_mp", "h1_rpd_mp", "grip", body_family::lmg, false},
        {"rpd_mp", "h1_rpd_mp", "none", body_family::lmg, false},
        {"rpd_reflex_mp", "h1_rpd_mp", "reflex", body_family::lmg, false},
        {"rpg_mp", "h1_rpg_mp", "none", body_family::launcher, false},
        {"saw_acog_mp", "h1_saw_mp", "acog", body_family::lmg, false},
        {"saw_grip_mp", "h1_saw_mp", "grip", body_family::lmg, false},
        {"saw_mp", "h1_saw_mp", "none", body_family::lmg, false},
        {"saw_reflex_mp", "h1_saw_mp", "reflex", body_family::lmg, false},
        {"skorpion_acog_mp", "h1_skorpion_mp", "acog", body_family::smg, false},
        {"skorpion_mp", "h1_skorpion_mp", "none", body_family::smg, false},
        {"skorpion_reflex_mp", "h1_skorpion_mp", "reflex", body_family::smg, false},
        {"skorpion_silencer_mp", "h1_skorpion_mp", "silencer", body_family::smg, false},
        {"smoke_grenade_mp", "h1_smokegrenade_mp", "none", body_family::hold, false},
        {"usp_mp", "h1_usp_mp", "none", body_family::pistol, false},
        {"usp_silencer_mp", "h1_usp_mp", "silencer", body_family::pistol, false},
        {"uzi_acog_mp", "h1_uzi_mp", "acog", body_family::smg, false},
        {"uzi_mp", "h1_uzi_mp", "none", body_family::smg, false},
        {"uzi_reflex_mp", "h1_uzi_mp", "reflex", body_family::smg, false},
        {"uzi_silencer_mp", "h1_uzi_mp", "silencer", body_family::smg, false},
        {"winchester1200_grip_mp", "h1_winchester1200_mp", "grip", body_family::shotgun, false},
        {"winchester1200_mp", "h1_winchester1200_mp", "none", body_family::shotgun, false},
        {"winchester1200_reflex_mp", "h1_winchester1200_mp", "reflex", body_family::shotgun, false},
    }};

    inline const weapon_mapping& map_weapon(std::string_view source)
    {
        for (const auto& mapping : weapon_mappings)
            if (mapping.source == source) return mapping;
        throw std::runtime_error("No remastered equivalent mapped for COD4 weapon: " + std::string(source));
    }

    // H1 reads alternate mode from PS.weapFlags bit 14; it is independent
    // of the native weapon handle (the primary and its GL share that handle).
    inline unsigned native_weapon_flags(unsigned source_flags, bool alternate)
    {
        return (source_flags & 2u) | (alternate ? 0x4000u : 0u);
    }
}
