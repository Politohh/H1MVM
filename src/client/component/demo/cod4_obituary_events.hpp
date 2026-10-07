#pragma once

#include "cod4_world_translation.hpp"
#include "cod4_event_entities.hpp"

namespace cod4
{
    struct obituary_event
    {
        int time{};
        unsigned entity{}, attacker{}, victim{}, event_parameter{}, source_weapon{};
        bool environmental{};
    };

    inline unsigned native_obituary_death(unsigned source_parameter)
    {
        // COD4 uses 0x80 | MOD; H1 uses 200 + its own MOD ordinal.
        // Verified native icon branches: melee, headshot, crush, fall,
        // suicide, generic death and impact. Unnamed causes use died.
        switch (source_parameter & 127u)
        {
        case 7: return 208;
        case 8: return 209;
        case 9: return 212;
        case 11: return 213;
        case 12: return 214;
        case 13: return 215;
        case 15: return 217;
        default: return 200;
        }
    }

    inline std::vector<obituary_event> collect_obituary_events(const world_track& world,
        std::string_view weapon_table = {})
    {
        std::vector<obituary_event> events;
        std::vector<std::string> weapons{""};
        std::istringstream names{std::string(weapon_table)};
        for (std::string name; names >> name;) weapons.push_back(name);
        std::map<unsigned, entity_state> previous;
        for (const auto& frame : world.frames)
        {
            for (const auto& [number, entity] : frame.entities)
            {
                // COD4 EV_OBITUARY (66) is a one-shot event entity. An entity
                // can remain in several snapshots, so record its first frame.
                if (entity.get(4) != 17 + 66 || previous.contains(number)) continue;
                // Source netfields name +116 otherEntityNum (victim) and
                // +120 attackerEntityNum; the H1 fields differ.
                const auto victim = entity.get(116), attacker = entity.get(120);
                const auto parameter = entity.get(156);
                if ((attacker >= 64 && attacker != 1022) || victim >= 64 || parameter > 255)
                    throw std::runtime_error("Invalid COD4 obituary payload");
                // A low parameter is the kill weapon itself, not MOD or the
                // attacker's currently held weapon (which can already change).
                auto weapon = parameter < 128 ? parameter : 0;
                if (!weapon_table.empty() && weapon >= weapons.size())
                    throw std::runtime_error("COD4 obituary weapon index outside its name table");
                // A car explosion can be credited to a player with the damage
                // weapon destructible_car. H1's MP weapon registry has no
                // equivalent gun; retain the kill as a native generic death
                // with a null weapon, rather than loading an unrelated weapon.
                const bool environmental = weapon && !weapon_table.empty()
                    && weapons[weapon] == "destructible_car";
                events.push_back({frame.time, number, attacker, victim, parameter,
                    environmental ? 0 : weapon, environmental});
            }
            previous = frame.entities;
        }
        return events;
    }

    inline h1_entity translate_obituary_event(const obituary_event& event, unsigned number,
        unsigned attacker, unsigned victim, unsigned native_weapon, bool alternate = false)
    {
        if (!valid_event_entity(number) || (attacker >= 42 && attacker != 2046)
            || victim >= 42 || native_weapon >= (1u << 27) || event.event_parameter > 255)
            throw std::runtime_error("Invalid H1 obituary client or entity number");
        h1_entity result{};
        write_native(result, 0, static_cast<std::uint16_t>(number));
        write_native(result, 2, static_cast<std::uint16_t>(victim));
        write_native(result, 4, std::uint16_t{2046});
        // H1 CG_EntityEvent routes event 140 to CG_Obituary. Its event
        // entity has eType 20 + event and the attacker in otherEntityNum2.
        write_native(result, 10, std::uint8_t{20 + 140});
        write_native(result, 12, std::int8_t{-1});
        // CG_EntityEvent dereferences the low weapon byte before dispatching
        // CG_Obituary. Keep the complete native weapon/attachment value here.
        // Special deaths use eventParm >= 200 and the safe null weapon.
        const bool special = event.environmental || event.event_parameter >= 128;
        write_native(result, 6, static_cast<std::uint8_t>(!special && alternate));
        write_native(result, 36, event.environmental ? 200u : special ? native_obituary_death(event.event_parameter) : 0u);
        write_native(result, 76, special ? 0u : native_weapon);
        write_native(result, 168, static_cast<std::uint16_t>(attacker));
        return result;
    }

    inline void repair_legacy_obituary(h1_entity& entity)
    {
        // v2 stored source eventParm in the weapon byte and shifted a native
        // weapon index into attachment bits. Keep old files playable; fresh
        // conversions retain the exact recorded kill weapon and attachments.
        std::uint32_t packed{};
        std::memcpy(&packed, entity.data() + 76, sizeof(packed));
        const auto parameter = packed & 255u;
        write_native(entity, 36, parameter >= 128 ? native_obituary_death(parameter) : 0u);
        write_native(entity, 76, parameter >= 128 ? 0u : (packed >> 17) & 0x3ffu);
    }

    inline std::size_t obituary_events_at(const std::vector<obituary_event>& events, int time)
    {
        return static_cast<std::size_t>(std::upper_bound(events.begin(), events.end(), time,
            [](int t, const obituary_event& event) { return t < event.time; }) - events.begin());
    }
}
