#pragma once

#include "cod4_world_translation.hpp"

namespace cod4
{
    using h1_hud_element = std::array<std::uint8_t, 192>;

    // A CoD4 HE_TYPE_VALUE carrying MP_PLUS is the recorded score popup.
    // H1 renders the element through its own HUD renderer and localized string.
    // The other CoD4 HUD elements need separate asset/layout mapping and are
    // deliberately not copied as raw structs: H1's element is 192, not 160 bytes.
    inline h1_hud_element translate_score_popup(const wire_state<160>& source,
        unsigned native_plus_label)
    {
        if (source.get(0) != 2 || !native_plus_label || native_plus_label >= 2048)
            throw std::runtime_error("Invalid native score popup mapping");
        h1_hud_element target{};
        const auto put = [&](std::size_t offset, auto value)
        {
            if (offset + sizeof(value) > target.size()) throw std::runtime_error("Invalid H1 HUD offset");
            std::memcpy(target.data() + offset, &value, sizeof(value));
        };
        // Offsets/widths are from the H1 PC HudElem netfield table
        // (0x12D7CB0). Only fields needed for a value element are populated.
        put(0, static_cast<std::uint16_t>(translate_entity_number(source.get(16))));
        put(4, source.get(0));
        put(8, source.get(28));
        put(12, source.get(32));
        put(16, source.get(4));
        put(20, source.get(8));
        put(24, source.get(12));
        put(28, source.get(24));
        put(32, source.get(20));
        put(48, source.get(36));
        put(52, source.get(40));
        put(56, source.get(44));
        put(60, source.get(48));
        put(72, native_plus_label);
        put(136, source.get(120));
        return target;
    }

    inline bool is_score_popup(const wire_state<160>& source,
        const std::map<int, std::string>& configstrings)
    {
        if (source.get(0) != 2 || !source.get(52)) return false;
        const auto found = configstrings.find(309 + source.get(52));
        return found != configstrings.end() && found->second == "MP_PLUS";
    }
}
