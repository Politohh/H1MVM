#pragma once
#include "cod4_client_slots.hpp"
#include "cod4_grenade_events.hpp"

namespace cod4
{
    inline unsigned native_bullet_event(unsigned source)
    {
        // Stock COD4 CG_EntityEvent and the installed H1 EV dispatch table.
        switch (source)
        {
        case 41: return 77; // EV_BULLET_HIT: surface/normal impact, including flesh
        case 42: return 80; // EV_BULLET_HIT_CLIENT_SMALL: incoming hit sound/trail
        case 43: return 81; // EV_BULLET_HIT_CLIENT_LARGE: incoming hit sound/trail
        default: return 0;
        }
    }

    struct bullet_event
    {
        int time{};
        unsigned entity{}, source_event{}, parameter{}, weapon{}, surface{};
        unsigned shooter{}, target{}, client{}, impact_flags{};
        std::array<float, 3> origin{}, start{};
    };

    inline std::vector<bullet_event> collect_bullet_events(const world_track& world)
    {
        std::vector<bullet_event> result;
        std::map<unsigned, entity_state> previous;
        int previous_time = -1;
        for (const auto& frame : world.frames)
        {
            if (frame.time < 0 || frame.time <= previous_time)
                throw std::runtime_error("Invalid bullet impact snapshot clock");
            previous_time = frame.time;
            for (const auto& [number, entity] : frame.entities)
            {
                const auto prior = previous.find(number);
                const auto add = [&](unsigned event, unsigned parameter)
                {
                    if (!native_bullet_event(event)) return;
                    if ((event == 41 && parameter >= 162) || !entity.get(196)
                        || entity.get(132) >= 29 || entity.get(116) >= 1024
                        || entity.get(124) >= 1024 || entity.get(140) >= 64)
                        throw std::runtime_error("Invalid recorded bullet impact payload");
                    // COD4 general impacts pass the low byte of un1 to
                    // CG_BulletHitEvent. Client hits have no impact-flag argument.
                    const auto impact_flags = event == 41 ? entity.get(212) & 255u : 0u;
                    if (impact_flags > 128)
                        throw std::runtime_error("Unsupported recorded bullet impact flags");
                    std::array<float, 3> start{};
                    for (unsigned i = 0; i < 3; ++i)
                        start[i] = std::bit_cast<float>(entity.get(84 + i * 4));
                    const auto origin = sample_trajectory(entity, 12, frame.time).origin;
                    for (const auto& vector : {origin, start})
                        for (const auto coordinate : vector)
                            if (!std::isfinite(coordinate))
                                throw std::runtime_error("Invalid recorded bullet impact position");
                    result.push_back({frame.time, number, event, parameter, entity.get(196), entity.get(132),
                        entity.get(116), entity.get(124), entity.get(140), impact_flags, origin, start});
                };
                if (entity.get(4) >= 17)
                {
                    if (prior == previous.end() || prior->second.get(4) != entity.get(4))
                        add(entity.get(4) - 17, entity.get(156));
                    continue;
                }
                const auto sequence = entity.get(160) & 255;
                const auto old_sequence = prior == previous.end() ? 0 : prior->second.get(160) & 255;
                const auto available = std::min(prior == previous.end() ? sequence : (sequence - old_sequence) & 255, 4u);
                for (unsigned i = available; i; --i)
                {
                    const auto slot = (sequence - i) & 3;
                    add(entity.get(164 + slot * 4), entity.get(180 + slot * 4));
                }
            }
            previous = frame.entities;
        }
        return result;
    }

    inline h1_entity translate_bullet_event(const bullet_event& event, unsigned number,
        unsigned weapon, bool alternate = false, unsigned recorded_client = 0,
        const client_slot_map* slots = nullptr)
    {
        const auto native_event = native_bullet_event(event.source_event);
        if (!native_event || number < 1024 || number >= 2046 || !(weapon & 255u)
            || weapon >= (1u << 27) || event.time < 0 || event.surface >= 29
            || (event.source_event == 41 && event.parameter >= 162) || event.client >= 64
            || event.impact_flags > 128 || recorded_client >= (slots ? 64u : 42u))
            throw std::runtime_error("Invalid native bullet impact event");
        for (const auto& vector : {event.origin, event.start})
            for (const auto coordinate : vector)
                if (!std::isfinite(coordinate)) throw std::runtime_error("Invalid native bullet impact position");
        h1_entity result{};
        // Player entities and client state swap the recorder with H1 client
        // zero. Impacts must use the same identities for native muzzle/tracer
        // and victim lookups, especially when somebody else occupies slot 0.
        const auto remap = [recorded_client, slots](unsigned client)
        { return slots ? slots->reference(client) : client == recorded_client ? 0u : client == 0 ? recorded_client : client; };
        const auto entity_reference = [&](unsigned source)
        {
            return slots ? static_cast<std::uint16_t>(slots->reference(translate_entity_number(source)))
                : translate_entity_number(remap(source));
        };
        write_native(result, 0, static_cast<std::uint16_t>(number));
        write_native(result, 2, entity_reference(event.shooter));
        // Stock Bullet_ImpactEffect leaves groundEntityNum at zero; it
        // does not record a victim identity. MWR interprets entity 0 as a
        // player and adds a second player-impact pass. Preserve an explicit
        // nonzero target, but encode an unbound impact as ENTITYNUM_WORLD.
        // ENTITYNUM_NONE explicitly suppresses H1's impact-effect selection.
        write_native(result, 4, event.target ? entity_reference(event.target) : std::uint16_t{2046});
        write_native(result, 6, static_cast<std::uint8_t>(alternate));
        write_native(result, 10, static_cast<std::uint8_t>(20 + native_event));
        write_native(result, 11, grenade_surface(event.surface));
        const auto client = remap(event.client);
        write_native(result, 12, static_cast<std::int8_t>(client < 18 ? client : -1));
        // COD4 records one impact per event; it has no H1 shot-count field.
        write_native(result, 28, std::uint8_t{1});
        write_native(result, 36, event.parameter);
        write_native(result, 76, weapon);
        write_native(result, 108, event.origin);
        write_native(result, 168, event.start);
        write_native(result, 204, event.impact_flags);
        return result;
    }

    inline std::size_t bullet_events_at(const std::vector<bullet_event>& events, int time)
    {
        return static_cast<std::size_t>(std::upper_bound(events.begin(), events.end(), time,
            [](int t, const bullet_event& event) { return t < event.time; }) - events.begin());
    }
}
