#pragma once
#include <array>
#include <span>
#include <string_view>

// IW3 v25 bone order from the original COD4 Mod Tools xmodelparts files.
// Source: cod4mw/CoD4-Mod-Tools at 95c76d6a470bea43fdd5ab591afe8475fac7699d.
// Retains names and relationships only; model data is not included.
namespace cod4 {
inline constexpr std::array<std::string_view, 48> vehicle_bones_0{
    "tag_origin", "tag_body", "tag_wheel_back_left", "tag_wheel_back_right",
    "tag_wheel_front_left", "tag_wheel_front_right", "left_wheel_01_jnt", "left_wheel_02_jnt",
    "right_wheel_01_jnt", "right_wheel_02_jnt", "tag_bumper_back", "tag_bumper_front",
    "tag_death_fx", "tag_door_left_back", "tag_door_left_front", "tag_door_right_back",
    "tag_door_right_front", "tag_glass_back", "tag_glass_back_d", "tag_glass_back_fx",
    "tag_glass_front", "tag_glass_front_d", "tag_glass_front_fx", "tag_hood",
    "tag_hood_fx", "tag_light_left_back", "tag_light_left_back_d", "tag_light_left_front",
    "tag_light_left_front_d", "tag_light_right_back", "tag_light_right_back_d", "tag_light_right_front",
    "tag_light_right_front_d", "tag_trunk", "tag_glass_left_back", "tag_glass_left_back_d",
    "tag_glass_left_back_fx", "tag_glass_left_front", "tag_glass_left_front_d", "tag_glass_left_front_fx",
    "tag_glass_right_back", "tag_glass_right_back_d", "tag_glass_right_back_fx", "tag_glass_right_front",
    "tag_glass_right_front_d", "tag_glass_right_front_fx", "tag_mirror_left", "tag_mirror_right",
};
inline constexpr std::array<std::string_view, 54> vehicle_bones_1{
    "tag_origin", "tag_body", "tag_wheel_back_left", "tag_wheel_back_right",
    "tag_wheel_front_left", "tag_wheel_front_right", "left_wheel_01_jnt", "left_wheel_02_jnt",
    "right_wheel_01_jnt", "right_wheel_02_jnt", "tag_bumper_back", "tag_bumper_front",
    "tag_death_fx", "tag_door_left_back", "tag_door_left_front", "tag_door_right_back",
    "tag_door_right_front", "tag_glass_back", "tag_glass_back_d", "tag_glass_back_fx",
    "tag_glass_front", "tag_glass_front_d", "tag_glass_front_fx", "tag_hood",
    "tag_hood_fx", "tag_light_left_back", "tag_light_left_back_d", "tag_light_left_front",
    "tag_light_left_front_d", "tag_light_right_back", "tag_light_right_back_d", "tag_light_right_front",
    "tag_light_right_front_d", "tag_trunk", "tag_glass_left_back", "tag_glass_left_back2",
    "tag_glass_left_back2_d", "tag_glass_left_back2_fx", "tag_glass_left_back_d", "tag_glass_left_back_fx",
    "tag_glass_left_front", "tag_glass_left_front_d", "tag_glass_left_front_fx", "tag_glass_right_back",
    "tag_glass_right_back2", "tag_glass_right_back2_d", "tag_glass_right_back2_fx", "tag_glass_right_back_d",
    "tag_glass_right_back_fx", "tag_glass_right_front", "tag_glass_right_front_d", "tag_glass_right_front_fx",
    "tag_mirror_left", "tag_mirror_right",
};
inline constexpr std::array<std::string_view, 46> vehicle_bones_2{
    "tag_origin", "tag_body", "tag_wheel_back_left", "tag_wheel_back_right",
    "tag_wheel_front_left", "tag_wheel_front_right", "left_wheel_01_jnt", "left_wheel_02_jnt",
    "right_wheel_01_jnt", "right_wheel_02_jnt", "tag_bumper_back", "tag_bumper_front",
    "tag_death_fx", "tag_door_left_front", "tag_door_right_front", "tag_glass_back",
    "tag_glass_back_d", "tag_glass_back_fx", "tag_glass_front", "tag_glass_front_d",
    "tag_glass_front_fx", "tag_glass_left_back", "tag_glass_left_back_d", "tag_glass_left_back_fx",
    "tag_glass_right_back", "tag_glass_right_back_d", "tag_glass_right_back_fx", "tag_hood",
    "tag_hood_fx", "tag_light_left_back", "tag_light_left_back_d", "tag_light_left_front",
    "tag_light_left_front_d", "tag_light_right_back", "tag_light_right_back_d", "tag_light_right_front",
    "tag_light_right_front_d", "tag_trunk", "tag_glass_left_front", "tag_glass_left_front_d",
    "tag_glass_left_front_fx", "tag_glass_right_front", "tag_glass_right_front_d", "tag_glass_right_front_fx",
    "tag_mirror_left", "tag_mirror_right",
};
inline constexpr std::array<std::string_view, 70> vehicle_bones_3{
    "tag_origin", "tag_body", "tag_wheel_back_left", "tag_wheel_back_right",
    "tag_wheel_front_left", "tag_wheel_front_right", "body_animate_jnt", "left_wheel_01_jnt",
    "left_wheel_02_jnt", "right_wheel_01_jnt", "right_wheel_02_jnt", "front_door_left_jnt",
    "front_door_right_jnt", "rear_door_left_jnt", "rear_door_right_jnt", "steering_wheel_jnt",
    "tag_bumper_back", "tag_bumper_front", "tag_deathfx", "tag_detach",
    "tag_driver", "tag_engine_left", "tag_engine_right", "tag_glass_back",
    "tag_glass_back_d", "tag_glass_back_fx", "tag_glass_front", "tag_glass_front_d",
    "tag_glass_front_fx", "tag_glass_left_back2", "tag_glass_left_back2_d", "tag_glass_left_back2_fx",
    "tag_glass_right_back2", "tag_glass_right_back2_d", "tag_glass_right_back2_fx", "tag_guy0",
    "tag_guy1", "tag_guy2", "tag_guy3", "tag_headlight_left",
    "tag_headlight_right", "tag_hood", "tag_hood_fx", "tag_light_left_back",
    "tag_light_left_back_d", "tag_light_left_front", "tag_light_left_front_d", "tag_light_right_back",
    "tag_light_right_back_d", "tag_light_right_front", "tag_light_right_front_d", "tag_passenger",
    "tag_door_left_back", "tag_door_left_front", "tag_door_right_back", "tag_door_right_front",
    "tag_glass_left_back", "tag_glass_left_back_d", "tag_glass_left_back_fx", "tag_glass_left_front",
    "tag_glass_left_front_d", "tag_glass_left_front_fx", "tag_glass_right_back", "tag_glass_right_back_d",
    "tag_glass_right_back_fx", "tag_glass_right_front", "tag_glass_right_front_d", "tag_glass_right_front_fx",
    "tag_mirror_left", "tag_mirror_right",
};
inline constexpr std::array<std::string_view, 1> vehicle_bones_4{
    "tag_bumper_back",
};
inline constexpr std::array<std::string_view, 1> vehicle_bones_5{
    "tag_bumper_front",
};
inline constexpr std::array<std::string_view, 41> vehicle_bones_6{
    "tag_origin", "tag_body", "tag_wheel_back_left", "tag_wheel_back_right",
    "tag_wheel_front_left", "tag_wheel_front_right", "left_wheel_01_jnt", "left_wheel_02_jnt",
    "right_wheel_01_jnt", "right_wheel_02_jnt", "tag_bumper_back", "tag_bumper_front",
    "tag_death_fx", "tag_door_left_back", "tag_door_left_front", "tag_door_right_back",
    "tag_door_right_front", "tag_driver", "tag_glass_back", "tag_glass_back_fx",
    "tag_glass_front", "tag_glass_front_fx", "tag_hood", "tag_hood_fx",
    "tag_light_left_back", "tag_light_left_front", "tag_light_right_back", "tag_light_right_front",
    "tag_passenger", "tag_trunk", "tag_trunk_strap", "tag_glass_left_back",
    "tag_glass_left_back_fx", "tag_glass_left_front", "tag_glass_left_front_fx", "tag_glass_right_back",
    "tag_glass_right_back_fx", "tag_glass_right_front", "tag_glass_right_front_fx", "tag_mirror_left",
    "tag_mirror_right",
};
inline constexpr std::array<std::string_view, 3> vehicle_bones_7{
    "tag_door_right_back", "tag_glass_right_back", "tag_glass_right_back_fx",
};
inline constexpr std::array<std::string_view, 4> vehicle_bones_8{
    "tag_door_right_front", "tag_glass_right_front", "tag_glass_right_front_fx", "tag_mirror_right",
};
inline constexpr std::array<std::string_view, 1> vehicle_bones_9{
    "tag_hood",
};
inline constexpr std::array<std::string_view, 1> vehicle_bones_10{
    "tag_mirror_left",
};
inline constexpr std::array<std::string_view, 1> vehicle_bones_11{
    "tag_mirror_right",
};
inline constexpr std::array<std::string_view, 1> vehicle_bones_12{
    "left_wheel_01_jnt",
};
inline constexpr std::array<std::string_view, 42> vehicle_bones_13{
    "tag_origin", "tag_body", "tag_wheel_back_left", "tag_wheel_back_right",
    "tag_wheel_front_left", "tag_wheel_front_right", "left_wheel_01_jnt", "left_wheel_02_jnt",
    "right_wheel_01_jnt", "right_wheel_02_jnt", "tag_bumper_back", "tag_bumper_front",
    "tag_death_fx", "tag_door_left_back", "tag_door_left_front", "tag_door_right_back",
    "tag_door_right_front", "tag_glass_back", "tag_glass_back_fx", "tag_glass_front",
    "tag_glass_front_fx", "tag_hood", "tag_hood_fx", "tag_light_left_back",
    "tag_light_left_front", "tag_light_right_back", "tag_light_right_front", "tag_trunk",
    "tag_glass_left_back", "tag_glass_left_back2", "tag_glass_left_back2_fx", "tag_glass_left_back_fx",
    "tag_glass_left_front", "tag_glass_left_front_fx", "tag_glass_right_back", "tag_glass_right_back2",
    "tag_glass_right_back2_fx", "tag_glass_right_back_fx", "tag_glass_right_front", "tag_glass_right_front_fx",
    "tag_mirror_left", "tag_mirror_right",
};
struct vehicle_bone_profile { std::string_view model; std::span<const std::string_view> bones; };
inline constexpr std::array<vehicle_bone_profile, 62> vehicle_bone_profiles{{
    {"vehicle_80s_hatch1_brn_destructible_mp", vehicle_bones_0},
    {"vehicle_80s_hatch1_green_destructible_mp", vehicle_bones_0},
    {"vehicle_80s_hatch1_red_destructible_mp", vehicle_bones_0},
    {"vehicle_80s_hatch1_silv_destructible_mp", vehicle_bones_0},
    {"vehicle_80s_hatch1_tan_destructible_mp", vehicle_bones_0},
    {"vehicle_80s_hatch1_yel_destructible_mp", vehicle_bones_0},
    {"vehicle_80s_hatch2_brn_destructible_mp", vehicle_bones_0},
    {"vehicle_80s_hatch2_green_destructible_mp", vehicle_bones_0},
    {"vehicle_80s_hatch2_red_destructible_mp", vehicle_bones_0},
    {"vehicle_80s_hatch2_silv_destructible_mp", vehicle_bones_0},
    {"vehicle_80s_hatch2_tan_destructible_mp", vehicle_bones_0},
    {"vehicle_80s_hatch2_yel_destructible_mp", vehicle_bones_0},
    {"vehicle_80s_sedan1_brn_destructible_mp", vehicle_bones_0},
    {"vehicle_80s_sedan1_green_destructible_mp", vehicle_bones_0},
    {"vehicle_80s_sedan1_red_destroyed", vehicle_bones_6},
    {"vehicle_80s_sedan1_red_destructible_mp", vehicle_bones_0},
    {"vehicle_80s_sedan1_red_wheel_lf", vehicle_bones_12},
    {"vehicle_80s_sedan1_silv_bumper_b", vehicle_bones_4},
    {"vehicle_80s_sedan1_silv_bumper_f", vehicle_bones_5},
    {"vehicle_80s_sedan1_silv_destroyed", vehicle_bones_6},
    {"vehicle_80s_sedan1_silv_destructible_mp", vehicle_bones_0},
    {"vehicle_80s_sedan1_silv_door_rb", vehicle_bones_7},
    {"vehicle_80s_sedan1_silv_door_rf", vehicle_bones_8},
    {"vehicle_80s_sedan1_silv_hood", vehicle_bones_9},
    {"vehicle_80s_sedan1_silv_mirror_l", vehicle_bones_10},
    {"vehicle_80s_sedan1_silv_mirror_r", vehicle_bones_11},
    {"vehicle_80s_sedan1_silv_wheel_lf", vehicle_bones_12},
    {"vehicle_80s_sedan1_tan_destructible_mp", vehicle_bones_0},
    {"vehicle_80s_sedan1_yel_bumper_b", vehicle_bones_4},
    {"vehicle_80s_sedan1_yel_bumper_f", vehicle_bones_5},
    {"vehicle_80s_sedan1_yel_destroyed", vehicle_bones_6},
    {"vehicle_80s_sedan1_yel_destructible_mp", vehicle_bones_0},
    {"vehicle_80s_sedan1_yel_door_rb", vehicle_bones_7},
    {"vehicle_80s_sedan1_yel_door_rf", vehicle_bones_8},
    {"vehicle_80s_sedan1_yel_hood", vehicle_bones_9},
    {"vehicle_80s_sedan1_yel_mirror_l", vehicle_bones_10},
    {"vehicle_80s_sedan1_yel_mirror_r", vehicle_bones_11},
    {"vehicle_80s_sedan1_yel_wheel_lf", vehicle_bones_12},
    {"vehicle_80s_wagon1_brn_bumper_b", vehicle_bones_4},
    {"vehicle_80s_wagon1_brn_bumper_f", vehicle_bones_5},
    {"vehicle_80s_wagon1_brn_destroyed", vehicle_bones_13},
    {"vehicle_80s_wagon1_brn_destructible_mp", vehicle_bones_1},
    {"vehicle_80s_wagon1_brn_door_rb", vehicle_bones_7},
    {"vehicle_80s_wagon1_brn_hood", vehicle_bones_9},
    {"vehicle_80s_wagon1_green_destructible_mp", vehicle_bones_1},
    {"vehicle_80s_wagon1_red_destructible_mp", vehicle_bones_1},
    {"vehicle_80s_wagon1_silv_destructible_mp", vehicle_bones_1},
    {"vehicle_80s_wagon1_tan_destructible_mp", vehicle_bones_1},
    {"vehicle_80s_wagon1_yel_destructible_mp", vehicle_bones_1},
    {"vehicle_pickup_destructible_mp", vehicle_bones_2},
    {"vehicle_small_hatch_blue_destructible_mp", vehicle_bones_0},
    {"vehicle_small_hatch_green_destructible_mp", vehicle_bones_0},
    {"vehicle_small_hatch_turq_destructible_mp", vehicle_bones_0},
    {"vehicle_small_hatch_white_destructible_mp", vehicle_bones_0},
    {"vehicle_small_wagon_blue_destructible_mp", vehicle_bones_0},
    {"vehicle_small_wagon_green_destructible_mp", vehicle_bones_0},
    {"vehicle_small_wagon_turq_destructible_mp", vehicle_bones_0},
    {"vehicle_small_wagon_white_destructible_mp", vehicle_bones_0},
    {"vehicle_uaz_fabric_destructible_mp", vehicle_bones_3},
    {"vehicle_uaz_hardtop_destructible_mp", vehicle_bones_3},
    {"vehicle_uaz_light_destructible_mp", vehicle_bones_3},
    {"vehicle_uaz_open_destructible_mp", vehicle_bones_3},
}};
}
