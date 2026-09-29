#pragma once
#include "cod4_world_translation.hpp"
#include "cod4_gun_events.hpp"
#include <set>

namespace cod4
{
    struct actor_event_frame
    {
        int time{};
        std::map<unsigned, gun_event_state> actors;
    };
    struct actor_event_track
    {
        std::vector<actor_event_frame> frames;
        unsigned actions{};
    };
    inline unsigned native_actor_gun_event(unsigned source)
    {
        // Remote entities use CG_CheckEvents' ordinary world firing path.
        if (source == 26) return 39;
        if (source == 27) return 40;
        return native_gun_event(source);
    }
    inline actor_event_track collect_actor_events(const world_track& world)
    {
        actor_event_track result;
        std::map<unsigned, gun_event_state> states;
        std::map<unsigned, unsigned> sequences;
        std::map<unsigned, unsigned> clients;
        for (const auto& frame : world.frames)
        {
            actor_event_frame native{frame.time, {}};
            std::set<unsigned> present;
            for (const auto& [number, entity] : frame.entities)
            {
                if (entity.get(4) != 1 || number == static_cast<unsigned>(world.client_num)) continue;
                present.insert(number);
                const auto sequence = entity.get(160);
                if (sequence > 255) throw std::runtime_error("Invalid COD4 actor event sequence");
                auto& state = states[number];
                const bool contiguous = sequences.contains(number) && clients.at(number) == entity.get(140);
                if (!contiguous) state.ring = {};
                const auto count = contiguous ? std::min((sequence - sequences.at(number)) & 255u, 4u) : 0u;
                for (unsigned i = count; i; --i)
                {
                    const auto slot = (sequence - i) & 3u;
                    const auto event = native_actor_gun_event(entity.get(164 + slot * 4));
                    if (!event || !entity.get(196)) continue;
                    const auto parameter = entity.get(180 + slot * 4);
                    if (parameter > 255) throw std::runtime_error("Invalid COD4 actor gun payload");
                    state.ring[state.sequence & 3u] = {event, parameter};
                    state.sequence = (state.sequence + 1) & 255u;
                    ++result.actions;
                }
                state.time = frame.time;
                native.actors.emplace(number, state);
                sequences[number] = sequence;
                clients[number] = entity.get(140);
            }
            // Reappearance begins a new source baseline. Retain the filtered
            // native counter so respawning does not replay old firing history.
            std::erase_if(sequences, [&](const auto& value) { return !present.contains(value.first); });
            result.frames.push_back(std::move(native));
        }
        return result;
    }
    inline const actor_event_frame& actor_events_at(const actor_event_track& track, int time)
    {
        if (track.frames.empty()) throw std::runtime_error("Missing COD4 actor event track");
        const auto next = std::upper_bound(track.frames.begin(), track.frames.end(), time,
            [](int t, const actor_event_frame& frame) { return t < frame.time; });
        return next == track.frames.begin() ? track.frames.front() : *(next - 1);
    }
}
