#pragma once
#include <array>
#include <string_view>

namespace cod4
{
    // COD4 Mod Tools character/mptype/_teams scripts at 95c76d6; native
    // MWR mp/costumemodeltable.csv default male class rows. Each target
    // was verified resident or in the installed transient model registry.
    enum class character_family { usmc_desert, arab_desert, sas_ct, sas_woodland, spetsnaz_urban, spetsnaz_woodland };
    enum class character_role { assault, smg, lmg, shotgun, sniper };
    struct character_assets { std::string_view body, head, hands; };
    inline constexpr std::array<character_assets, 30> native_characters{{
        {"body_usmc_desert_assault_mp_camo", "head_usmc_desert_assault_mp", "viewhands_h1_usmc_desert_mp_camo"}, // usmc_desert assault
        {"body_usmc_desert_smg_mp_camo", "head_usmc_desert_smg_mp", "viewhands_h1_usmc_desert_mp_camo"}, // usmc_desert smg
        {"body_usmc_desert_lmg_mp_camo", "head_usmc_desert_lmg_mp", "viewhands_h1_usmc_desert_mp_camo"}, // usmc_desert lmg
        {"body_usmc_desert_shotgun_mp_camo", "head_usmc_desert_shotgun_mp", "viewhands_h1_usmc_desert_mp_camo"}, // usmc_desert shotgun
        {"body_usmc_desert_sniper_mp_camo", "head_usmc_desert_sniper_mp_patch", "viewhands_h1_usmc_desert_mp_camo"}, // usmc_desert sniper
        {"body_arab_desert_assault_mp_camo", "head_arab_desert_assault_mp", "viewhands_h1_arab_desert_mp_camo"}, // arab_desert assault
        {"body_arab_desert_smg_mp_camo", "head_arab_desert_smg_mp", "viewhands_h1_arab_desert_mp_camo"}, // arab_desert smg
        {"body_arab_desert_lmg_mp_camo", "head_arab_desert_lmg_mp", "viewhands_h1_arab_desert_mp_camo"}, // arab_desert lmg
        {"body_arab_desert_shotgun_mp_camo", "head_arab_desert_shotgun_mp", "viewhands_h1_arab_desert_mp_camo"}, // arab_desert shotgun
        {"body_arab_desert_sniper_mp_camo", "head_arab_desert_sniper_mp", "viewhands_h1_arab_desert_mp_camo"}, // arab_desert sniper
        {"body_sas_ct_assault_mp_camo", "head_sas_ct_assault_mp", "viewhands_h1_sas_ct_mp_camo"}, // sas_ct assault
        {"body_sas_ct_smg_mp_camo", "head_sas_ct_smg_mp", "viewhands_h1_sas_ct_mp_camo"}, // sas_ct smg
        {"body_sas_ct_lmg_mp_camo", "head_sas_ct_lmg_mp", "viewhands_h1_sas_ct_mp_camo"}, // sas_ct lmg
        {"body_sas_ct_shotgun_mp_camo", "head_sas_ct_shotgun_mp", "viewhands_h1_sas_ct_mp_camo"}, // sas_ct shotgun
        {"body_sas_ct_sniper_mp_camo", "head_sas_ct_sniper_mp", "viewhands_h1_sas_ct_mp_camo"}, // sas_ct sniper
        {"body_sas_woodland_assault_mp_camo", "head_sas_woodland_assault_mp_patch", "viewhands_h1_sas_woodland_mp_camo"}, // sas_woodland assault
        {"body_sas_woodland_smg_mp_camo", "head_sas_woodland_smg_mp", "viewhands_h1_sas_woodland_mp_camo"}, // sas_woodland smg
        {"body_sas_woodland_lmg_mp_camo", "head_sas_woodland_lmg_mp", "viewhands_h1_sas_woodland_mp_camo"}, // sas_woodland lmg
        {"body_sas_woodland_shotgun_mp_camo", "head_sas_woodland_shotgun_mp", "viewhands_h1_sas_woodland_mp_camo"}, // sas_woodland shotgun
        {"body_sas_woodland_sniper_mp_camo", "head_sas_woodland_sniper_mp", "viewhands_h1_sas_woodland_ghillie_mp_camo"}, // sas_woodland sniper
        {"body_spetsnaz_urban_assault_mp_camo", "head_spetsnaz_urban_assault_mp", "viewhands_h1_spetsnaz_urban_mp_camo"}, // spetsnaz_urban assault
        {"body_spetsnaz_urban_smg_mp_camo", "head_spetsnaz_urban_smg_mp", "viewhands_h1_spetsnaz_urban_mp_camo"}, // spetsnaz_urban smg
        {"body_spetsnaz_urban_lmg_mp_camo", "head_spetsnaz_urban_lmg_mp", "viewhands_h1_spetsnaz_urban_mp_camo"}, // spetsnaz_urban lmg
        {"body_spetsnaz_urban_shotgun_mp_camo", "head_spetsnaz_urban_shotgun_mp", "viewhands_h1_spetsnaz_urban_mp_camo"}, // spetsnaz_urban shotgun
        {"body_spetsnaz_urban_sniper_mp_camo", "head_spetsnaz_urban_sniper_mp", "viewhands_h1_spetsnaz_urban_mp_camo"}, // spetsnaz_urban sniper
        {"body_spetsnaz_woodland_assault_mp_camo", "head_spetsnaz_woodland_assault_mp", "viewhands_h1_spetsnaz_mp_camo"}, // spetsnaz_woodland assault
        {"body_spetsnaz_woodland_smg_mp_camo", "head_spetsnaz_woodland_smg_mp", "viewhands_h1_spetsnaz_mp_camo"}, // spetsnaz_woodland smg
        {"body_spetsnaz_woodland_lmg_mp_camo", "head_spetsnaz_woodland_lmg_mp", "viewhands_h1_spetsnaz_mp_camo"}, // spetsnaz_woodland lmg
        {"body_spetsnaz_woodland_shotgun_mp_camo", "head_spetsnaz_woodland_shotgun_mp", "viewhands_h1_spetsnaz_mp_camo"}, // spetsnaz_woodland shotgun
        {"body_spetsnaz_woodland_sniper_mp_camo", "head_spetsnaz_woodland_sniper_mp", "viewhands_h1_spetsnaz_ghillie_mp_camo"}, // spetsnaz_woodland sniper
    }};
    struct source_character
    {
        std::string_view body, head, hands;
        character_family family;
        character_role role;
    };
    inline constexpr std::array<source_character, 30> source_characters{{
        {"body_mp_arab_regular_assault", "head_mp_arab_regular_suren", "viewhands_desert_opfor", character_family::arab_desert, character_role::assault},
        {"body_mp_arab_regular_cqb", "head_mp_arab_regular_headwrap", "viewhands_desert_opfor", character_family::arab_desert, character_role::smg},
        {"body_mp_arab_regular_engineer", "head_mp_arab_regular_ski_mask", "viewhands_desert_opfor", character_family::arab_desert, character_role::shotgun},
        {"body_mp_arab_regular_sniper", "head_mp_arab_regular_sadiq", "viewhands_desert_opfor", character_family::arab_desert, character_role::sniper},
        {"body_mp_arab_regular_support", "head_mp_arab_regular_asad", "viewhands_desert_opfor", character_family::arab_desert, character_role::lmg},
        {"body_mp_opforce_assault", "head_mp_opforce_headwrap", "viewhands_op_force", character_family::spetsnaz_woodland, character_role::assault},
        {"body_mp_opforce_cqb", "head_mp_opforce_gasmask", "viewhands_op_force", character_family::spetsnaz_woodland, character_role::smg},
        {"body_mp_opforce_eningeer", "head_mp_opforce_gasmask", "viewhands_op_force", character_family::spetsnaz_woodland, character_role::shotgun},
        {"body_mp_opforce_sniper", "head_mp_opforce_ghillie", "viewhands_marine_sniper", character_family::spetsnaz_woodland, character_role::sniper},
        {"body_mp_opforce_support", "head_mp_opforce_3hole_mask", "viewhands_op_force", character_family::spetsnaz_woodland, character_role::lmg},
        {"body_mp_opforce_sniper_urban", "head_mp_opforce_justin", "viewhands_op_force", character_family::spetsnaz_urban, character_role::sniper},
        {"body_mp_sas_urban_assault", "", "viewhands_black_kit", character_family::sas_ct, character_role::assault},
        {"body_mp_sas_urban_recon", "", "viewhands_black_kit", character_family::sas_ct, character_role::shotgun},
        {"body_mp_sas_urban_sniper", "", "viewhands_black_kit", character_family::sas_ct, character_role::sniper},
        {"body_mp_sas_urban_specops", "", "viewhands_black_kit", character_family::sas_ct, character_role::smg},
        {"body_mp_sas_urban_support", "", "viewhands_black_kit", character_family::sas_ct, character_role::lmg},
        {"body_mp_usmc_assault", "head_mp_usmc_tactical_mich", "viewmodel_base_viewhands", character_family::usmc_desert, character_role::assault},
        {"body_mp_usmc_cqb", "", "viewmodel_base_viewhands", character_family::usmc_desert, character_role::smg},
        {"body_mp_usmc_engineer", "", "viewmodel_base_viewhands", character_family::usmc_desert, character_role::shotgun},
        {"body_mp_usmc_grenadier", "", "viewmodel_base_viewhands", character_family::usmc_desert, character_role::assault},
        {"body_mp_usmc_recon", "head_mp_usmc_nomex", "viewmodel_base_viewhands", character_family::usmc_desert, character_role::shotgun},
        {"body_mp_usmc_rifleman", "", "viewmodel_base_viewhands", character_family::usmc_desert, character_role::assault},
        {"body_mp_usmc_sniper", "head_mp_usmc_tactical_baseball_cap", "viewmodel_base_viewhands", character_family::usmc_desert, character_role::sniper},
        {"body_mp_usmc_specops", "head_mp_usmc_tactical_mich_stripes_nomex", "viewmodel_base_viewhands", character_family::usmc_desert, character_role::smg},
        {"body_mp_usmc_support", "head_mp_usmc_shaved_head", "viewmodel_base_viewhands", character_family::usmc_desert, character_role::lmg},
        {"body_mp_usmc_woodland_assault", "head_mp_usmc_tactical_mich", "viewhands_sas_woodland", character_family::sas_woodland, character_role::assault},
        {"body_mp_usmc_woodland_recon", "head_mp_usmc_nomex", "viewhands_sas_woodland", character_family::sas_woodland, character_role::shotgun},
        {"body_mp_usmc_woodland_sniper", "head_mp_usmc_ghillie", "viewhands_marine_sniper", character_family::sas_woodland, character_role::sniper},
        {"body_mp_usmc_woodland_specops", "head_mp_usmc_tactical_mich_stripes_nomex", "viewhands_sas_woodland", character_family::sas_woodland, character_role::smg},
        {"body_mp_usmc_woodland_support", "head_mp_usmc_shaved_head", "viewhands_sas_woodland", character_family::sas_woodland, character_role::lmg},
    }};
}
