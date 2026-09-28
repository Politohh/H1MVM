#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>

namespace cod4
{
    using source_part_bits = std::array<std::uint32_t, 4>;
    using native_part_bits = std::array<std::uint32_t, 8>;

    struct part_mapping
    {
        std::array<int, 128> destination{};
        part_mapping() { destination.fill(-1); }
    };

    // Source order verified against the resident IW3 models on mp_crash.
    // H1 has additional bones; destination ordinals must be resolved by name.
    inline constexpr std::array<std::string_view, 48> sedan_bones{
        "tag_origin", "tag_body", "tag_wheel_back_left", "tag_wheel_back_right",
        "tag_wheel_front_left", "tag_wheel_front_right", "left_wheel_01_jnt",
        "left_wheel_02_jnt", "right_wheel_01_jnt", "right_wheel_02_jnt",
        "tag_bumper_back", "tag_bumper_front", "tag_death_fx", "tag_door_left_back",
        "tag_door_left_front", "tag_door_right_back", "tag_door_right_front",
        "tag_glass_back", "tag_glass_back_d", "tag_glass_back_fx", "tag_glass_front",
        "tag_glass_front_d", "tag_glass_front_fx", "tag_hood", "tag_hood_fx",
        "tag_light_left_back", "tag_light_left_back_d", "tag_light_left_front",
        "tag_light_left_front_d", "tag_light_right_back", "tag_light_right_back_d",
        "tag_light_right_front", "tag_light_right_front_d", "tag_trunk",
        "tag_glass_left_back", "tag_glass_left_back_d", "tag_glass_left_back_fx",
        "tag_glass_left_front", "tag_glass_left_front_d", "tag_glass_left_front_fx",
        "tag_glass_right_back", "tag_glass_right_back_d", "tag_glass_right_back_fx",
        "tag_glass_right_front", "tag_glass_right_front_d", "tag_glass_right_front_fx",
        "tag_mirror_left", "tag_mirror_right"
    };

    inline std::span<const std::string_view> source_bones(std::string_view model)
    {
        for (const auto name : {"vehicle_80s_sedan1_red_destructible_mp",
            "vehicle_80s_sedan1_brn_destructible_mp", "vehicle_80s_sedan1_green_destructible_mp"})
            if (model == name) return sedan_bones;
        return {};
    }

    inline part_mapping map_parts(std::span<const std::string_view> source,
        std::span<const std::string_view> destination)
    {
        if (source.empty() || source.size() > 128 || destination.empty() || destination.size() > 256)
            throw std::runtime_error("Invalid model bone table for part translation");
        part_mapping result;
        for (std::size_t i = 0; i < source.size(); ++i)
        {
            if (source[i].empty() || std::count(source.begin(), source.end(), source[i]) != 1)
                throw std::runtime_error("Ambiguous source model bone name");
            const auto found = std::find(destination.begin(), destination.end(), source[i]);
            if (found == destination.end()) continue;
            if (std::count(destination.begin(), destination.end(), source[i]) != 1)
                throw std::runtime_error("Ambiguous remastered model bone name");
            result.destination[i] = static_cast<int>(found - destination.begin());
        }
        return result;
    }

    inline native_part_bits translate_parts(const source_part_bits& source, const part_mapping* mapping)
    {
        native_part_bits result{};
        for (unsigned bone = 0; bone < 128; ++bone)
        {
            // IW3 and H1 number these bits from the most significant bit.
            if (!(source[bone / 32] & (0x80000000u >> (bone % 32)))) continue;
            if (!mapping || mapping->destination[bone] < 0 || mapping->destination[bone] >= 256)
                throw std::runtime_error("Recorded hidden model part has no remastered bone mapping");
            const auto target = static_cast<unsigned>(mapping->destination[bone]);
            result[target / 32] |= 0x80000000u >> (target % 32);
        }
        return result;
    }
}
