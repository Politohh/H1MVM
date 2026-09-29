#pragma once
#include "cod4_world_translation.hpp"

namespace cod4
{
    inline constexpr std::string_view car_explosion_fx = "explosions/small_vehicle_explosion";
    inline constexpr std::string_view native_car_explosion_fx = "fx/explosions/small_vehicle_explosion";
    inline constexpr unsigned converted_car_fx_index = 511;
    struct car_fx_event
    {
        int time{};
        unsigned entity{}, effect{};
        std::array<float, 3> origin{}, angles{};
    };
    struct car_fx_track
    {
        std::vector<car_fx_event> events;
        std::map<std::string, unsigned> other_effects;
    };

    inline car_fx_track collect_car_fx(const world_track& world, const std::map<int, std::string>& configs)
    {
        car_fx_track result;
        std::map<unsigned, entity_state> previous;
        int previous_time = -1;
        for (const auto& frame : world.frames)
        {
            if (frame.time < 0 || frame.time <= previous_time)
                throw std::runtime_error("Invalid car FX snapshot clock");
            previous_time = frame.time;
            for (const auto& [number, entity] : frame.entities)
            {
                const auto prior = previous.find(number);
                const auto add = [&](unsigned event, unsigned parameter)
                {
                    // Original COD4 EV_PLAY_FX, not a model swap or grenade.
                    if (event != 55) return;
                    const auto effect = configs.find(1598 + parameter);
                    if (!parameter || parameter >= 100 || effect == configs.end() || effect->second.empty())
                        throw std::runtime_error("Recorded scripted FX has no source effect identity");
                    if (effect->second != car_explosion_fx)
                    {
                        ++result.other_effects[effect->second];
                        return;
                    }
                    const auto origin = sample_trajectory(entity, 12, frame.time).origin;
                    const auto angles = sample_trajectory(entity, 48, frame.time).origin;
                    result.events.push_back({frame.time, number, parameter, origin, angles});
                };
                if (entity.get(4) >= 17)
                {
                    if (prior == previous.end() || prior->second.get(4) != entity.get(4))
                        add(entity.get(4) - 17, entity.get(156));
                    continue;
                }
                const unsigned sequence = entity.get(160) & 255;
                const unsigned old = prior == previous.end() ? 0 : prior->second.get(160) & 255;
                const unsigned available = std::min(prior == previous.end() ? sequence : (sequence - old) & 255, 4u);
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

    inline h1_entity translate_car_fx(const car_fx_event& event, unsigned number, unsigned native_effect)
    {
        if (number < 1024 || number >= 2046 || !native_effect || native_effect >= 512 || event.time < 0)
            throw std::runtime_error("Invalid native car FX event");
        for (const auto coordinate : event.origin)
            if (!std::isfinite(coordinate)) throw std::runtime_error("Invalid car FX position");
        for (const auto angle : event.angles)
            if (!std::isfinite(angle)) throw std::runtime_error("Invalid car FX orientation");
        h1_entity result{};
        write_native(result, 0, static_cast<std::uint16_t>(number));
        write_native(result, 2, std::uint16_t{2047});
        write_native(result, 4, std::uint16_t{2046});
        // H1 EV_PLAY_FX (111), CG_EntityEvent -> 0x114360:
        // eventParm at +36 indexes the native 512-entry FX table; apos.trBase
        // at +144 supplies AnglesToAxis. No weapon/surface payload is involved.
        write_native(result, 10, std::uint8_t{131});
        write_native(result, 12, std::int8_t{-1});
        write_native(result, 36, native_effect);
        write_native(result, 108, event.origin);
        write_native(result, 144, event.angles);
        return result;
    }

    inline std::size_t car_fx_at(const std::vector<car_fx_event>& events, int time)
    {
        return static_cast<std::size_t>(std::upper_bound(events.begin(), events.end(), time,
            [](int t, const car_fx_event& event) { return t < event.time; }) - events.begin());
    }
}
