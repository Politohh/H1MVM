#pragma once

#include "cod4_world_translation.hpp"

namespace cod4
{
    struct obituary_event
    {
        int time{};
        unsigned entity{}, attacker{}, victim{}, means_of_death{}, source_weapon{};
    };

    inline std::vector<obituary_event> collect_obituary_events(const world_track& world,
        const std::vector<player_frame>& players)
    {
        std::vector<obituary_event> events;
        std::map<unsigned, entity_state> previous;
        std::map<unsigned, unsigned> last_weapon;
        for (const auto& frame : world.frames)
        {
            for (const auto& [number, entity] : frame.entities)
            {
                if (entity.get(4) == 1 && entity.get(196)) last_weapon[number] = entity.get(196);
            }
            for (const auto& [number, entity] : frame.entities)
            {
                // COD4 EV_OBITUARY (66) is a one-shot event entity. An entity
                // can remain in several snapshots, so record its first frame.
                if (entity.get(4) != 17 + 66 || previous.contains(number)) continue;
                // Source netfields name +116 otherEntityNum (victim) and
                // +120 attackerEntityNum; the H1 fields differ.
                const auto victim = entity.get(116), attacker = entity.get(120);
                const auto means = entity.get(156);
                if ((attacker >= 42 && attacker != 1022) || victim >= 42 || means > 255)
                    throw std::runtime_error("Invalid COD4 obituary payload");
                unsigned weapon = last_weapon.contains(attacker) ? last_weapon.at(attacker) : 0;
                if (attacker == static_cast<unsigned>(world.client_num) && !players.empty())
                {
                    const auto next = std::upper_bound(players.begin(), players.end(), frame.time,
                        [](int time, const player_frame& player) { return time < player.time; });
                    const auto& player = next == players.begin() ? players.front() : *(next - 1);
                    weapon = player.integer("weapon");
                }
                events.push_back({frame.time, number, attacker, victim, means, weapon});
            }
            previous = frame.entities;
        }
        return events;
    }

    inline h1_entity translate_obituary_event(const obituary_event& event, unsigned number,
        unsigned attacker, unsigned victim, unsigned native_weapon)
    {
        if (number < 1024 || number >= 2046 || (attacker >= 42 && attacker != 2046)
            || victim >= 42 || native_weapon >= (1u << 27))
            throw std::runtime_error("Invalid H1 obituary client or entity number");
        h1_entity result{};
        write_native(result, 0, static_cast<std::uint16_t>(number));
        write_native(result, 2, static_cast<std::uint16_t>(victim));
        write_native(result, 4, std::uint16_t{2046});
        // H1 CG_EntityEvent routes event 140 to CG_Obituary. Its event
        // entity has eType 20 + event and the attacker in otherEntityNum2.
        write_native(result, 10, std::uint8_t{20 + 140});
        write_native(result, 12, std::int8_t{-1});
        // CG_Obituary reads the low byte as MOD and bits 17..26 as its
        // weapon index. Passing the CoD4 MOD alone selected unrelated icons.
        write_native(result, 76, event.means_of_death | ((native_weapon & 0x3ffu) << 17));
        write_native(result, 168, static_cast<std::uint16_t>(attacker));
        return result;
    }

    inline std::size_t obituary_events_at(const std::vector<obituary_event>& events, int time)
    {
        return static_cast<std::size_t>(std::upper_bound(events.begin(), events.end(), time,
            [](int t, const obituary_event& event) { return t < event.time; }) - events.begin());
    }
}
