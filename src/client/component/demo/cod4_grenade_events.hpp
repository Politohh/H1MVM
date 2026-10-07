#pragma once
#include "cod4_world_translation.hpp"
#include "cod4_event_entities.hpp"

namespace cod4
{
    // COD4 bg.h and the installed H1 EV name table (0x1410B1510).
    // H1 CG_EntityEvent dispatches 98/103/104 through native weapon FX.
    inline unsigned native_grenade_event(unsigned source)
    {
        switch (source)
        {
        case 45: return 98;  // EV_GRENADE_EXPLODE
        case 48: return 103; // EV_FLASHBANG_EXPLODE
        case 49: return 104; // EV_CUSTOM_EXPLODE (smoke/concussion weapon FX)
        default: return 0;
        }
    }

    struct grenade_event
    {
        int time{};
        unsigned entity{}, source_event{}, parameter{}, weapon{}, surface{};
        std::array<float, 3> origin{};
    };
    struct grenade_events
    {
        std::vector<grenade_event> events;
        std::map<unsigned, unsigned> other_explosions;
    };

    inline grenade_events collect_grenade_events(const world_track& world)
    {
        grenade_events result;
        std::map<unsigned, entity_state> previous;
        int previous_time = -1;
        for (const auto& frame : world.frames)
        {
            if (frame.time < 0 || frame.time <= previous_time)
                throw std::runtime_error("Invalid grenade event snapshot clock");
            previous_time = frame.time;
            for (const auto& [number, entity] : frame.entities)
            {
                const auto prior = previous.find(number);
                const auto add = [&](unsigned event, unsigned parameter)
                {
                    if (!native_grenade_event(event))
                    {
                        if (event == 46 || event == 47 || event == 50 || event == 52)
                            ++result.other_explosions[event];
                        return;
                    }
                    if (parameter >= 162 || entity.get(132) >= 29 || !entity.get(196))
                        throw std::runtime_error("Invalid recorded grenade detonation payload");
                    const auto origin = sample_trajectory(entity, 12, frame.time).origin;
                    result.events.push_back({frame.time, number, event, parameter, entity.get(196), entity.get(132), origin});
                };
                // COD4 temporary events use ET_EVENTS + event, whereas H1
                // uses 20 + event. Also inspect ordinary entity event rings;
                // this sample's actual explosions are ET_GENERAL ring events.
                if (entity.get(4) >= 17)
                {
                    if (prior == previous.end() || prior->second.get(4) != entity.get(4))
                        add(entity.get(4) - 17, entity.get(156));
                    continue;
                }
                const unsigned sequence = entity.get(160) & 255;
                const unsigned old_sequence = prior == previous.end() ? 0 : prior->second.get(160) & 255;
                const unsigned available = prior == previous.end() ? std::min(sequence, 4u)
                    : std::min((sequence - old_sequence) & 255, 4u);
                for (unsigned i = available; i; --i)
                {
                    const auto slot = (sequence - i) & 3;
                    add(entity.get(164 + slot * 4), entity.get(180 + slot * 4));
                }
            }
            // Removal ends a source entity's incarnation. Reused numbers must
            // not inherit its old sequence or suppress a later detonation.
            previous = frame.entities;
        }
        return result;
    }

    inline bool grenade_weapon_matches(unsigned event, std::string_view weapon)
    {
        // EV_GRENADE_EXPLODE also carries planted explosives. Native MWR C4
        // and claymore definitions use impactType 9, just like frag grenades;
        // retain their own weapon identity through the native impact-FX path.
        if (event == 45) return weapon == "frag_grenade_mp" || weapon == "frag_grenade_short_mp"
            || weapon == "claymore_mp" || weapon == "c4_mp";
        if (event == 48) return weapon == "flash_grenade_mp";
        if (event == 49) return weapon == "concussion_grenade_mp" || weapon == "flash_grenade_mp"
            || weapon == "smoke_grenade_mp";
        return false;
    }

    inline std::uint8_t grenade_surface(unsigned source)
    {
        // H1 inserts metal_grate and moves cushion (COD4 stock surface names).
        constexpr std::array<std::uint8_t, 29> surfaces{
            0,1,2,3,4,5,6,7,8,9,10,11,12,13,15,16,17,18,19,20,21,22,23,24,25,26,52,27,28};
        return surfaces.at(source);
    }

    inline bool grenade_effect_available(unsigned event, bool weapon_effect, bool impact_effect)
    {
        // Native frag EV 98 uses CG_GetImpactEffect; projExplosionEffect is
        // optional. Flash/custom EVs use the weapon's own explosion effect.
        return event == 45 ? impact_effect : (event == 48 || event == 49) && weapon_effect;
    }

    inline h1_entity translate_grenade_event(const grenade_event& event, unsigned number, unsigned weapon)
    {
        const auto native_event = native_grenade_event(event.source_event);
        if (!native_event || !valid_event_entity(number) || !(weapon & 255u)
            || weapon >= (1u << 27) || event.time < 0 || event.parameter >= 162 || event.surface >= 29)
            throw std::runtime_error("Invalid native grenade event");
        for (const auto coordinate : event.origin)
            if (!std::isfinite(coordinate)) throw std::runtime_error("Invalid grenade explosion position");
        h1_entity result{};
        write_native(result, 0, static_cast<std::uint16_t>(number));
        write_native(result, 2, std::uint16_t{2047});
        write_native(result, 4, std::uint16_t{2046});
        // CG_AddPacketEntities at 0xEABE6: eType >= 20, dispatch eType-20
        // once per entity incarnation using centity.previousEvent (+0x248).
        write_native(result, 10, static_cast<std::uint8_t>(20 + native_event));
        // COD4's stock surface-name table (iw3mp.exe, file offset 0x31FBBC,
        // 20-byte rows) vs H1 materialSurfType_t: H1 inserts metal_grate at
        // 14 and moves cushion from 26 to 52. Do not copy surface ordinals.
        write_native(result, 11, grenade_surface(event.surface));
        write_native(result, 12, std::int8_t{-1});
        write_native(result, 36, event.parameter);
        write_native(result, 76, weapon);
        write_native(result, 96, 0u); // TR_STATIONARY, encoded securely by the native writer
        write_native(result, 108, event.origin);
        return result;
    }

    // Repeated camera archives and paused draws cannot create a second event.
    // Keep identities resident after their source event entity expires, so a
    // seek reconstructs native FX at their recorded age, even after 300ms.
    inline std::size_t grenade_events_at(const std::vector<grenade_event>& events, int time)
    {
        return static_cast<std::size_t>(std::upper_bound(events.begin(), events.end(), time,
            [](int t, const grenade_event& event) { return t < event.time; }) - events.begin());
    }
}
