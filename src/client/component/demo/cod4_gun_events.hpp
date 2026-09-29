#pragma once
#include "cod4_playerstate_reader.hpp"
#include <format>
#include <climits>

namespace cod4
{
    inline unsigned native_gun_event(unsigned source)
    {
        // H1 CG_CheckPlayerstateEvents (0x328290) dispatches with flags=1.
        // EV_FIRE_WEAPON 39 skips that path; SV_FIRE_WEAPON 51 is the native
        // authoritative shot for this recorded, non-predicting local player.
        switch (source)
        {
        case 26: return 51;
        case 27: return 52;
        case 28: return 42;
        case 29: return 43;
        default: return 0;
        }
    }

    struct gun_event
    {
        int time{}, snapshot_time{};
        unsigned source{}, native{}, parameter{}, weapon{};
    };
    struct gun_event_state
    {
        int time{};
        unsigned sequence{};
        std::array<std::array<unsigned, 2>, 4> ring{};
    };
    struct gun_event_track
    {
        std::vector<gun_event> events;
        std::vector<gun_event> omitted;
        std::vector<gun_event_state> frames;
    };

    inline gun_event_track collect_gun_events(const std::vector<player_frame>& players)
    {
        gun_event_track result;
        gun_event_state state;
        unsigned previous{};
        int previous_snapshot = -1;
        for (const auto& player : players)
        {
            const auto sequence = player.integer("eventSequence");
            const auto command = player.integer("commandTime");
            if (sequence > 255 || command > INT_MAX || player.time <= previous_snapshot
                || (!result.frames.empty() && static_cast<int>(command) < state.time))
                throw std::runtime_error("Invalid recorded gun event clock or sequence");
            // The initial ring contains pre-recording history with no known
            // event time. Use it as a baseline, never as new firing actions.
            const auto count = result.frames.empty() ? 0u : std::min((sequence - previous) & 255u, 4u);
            for (unsigned i = count; i; --i)
            {
                const auto slot = (sequence - i) & 3u;
                const auto source = player.integer(std::format("events[{}]", slot));
                const auto native = native_gun_event(source);
                if (!native) continue;
                const auto parameter = player.integer(std::format("eventParms[{}]", slot));
                const auto weapon = player.integer("weapon");
                if (parameter > 255) throw std::runtime_error("Invalid recorded gun event payload");
                if (!weapon)
                {
                    // Some recorded death/switch frames carry delayed actions
                    // after clearing the held weapon. Do not guess its identity
                    // or reject an otherwise readable recording for optional FX.
                    result.omitted.push_back({static_cast<int>(command), player.time, source, native, parameter, weapon});
                    continue;
                }
                state.ring[state.sequence & 3u] = {native, parameter};
                state.sequence = (state.sequence + 1) & 255u;
                result.events.push_back({static_cast<int>(command), player.time, source, native, parameter, weapon});
            }
            state.time = static_cast<int>(command);
            result.frames.push_back(state);
            previous = sequence;
            previous_snapshot = player.time;
        }
        return result;
    }

    inline const gun_event_state& gun_events_at(const gun_event_track& track, int time)
    {
        if (track.frames.empty()) throw std::runtime_error("Missing recorded gun event track");
        const auto next = std::upper_bound(track.frames.begin(), track.frames.end(), time,
            [](int t, const gun_event_state& frame) { return t < frame.time; });
        return next == track.frames.begin() ? track.frames.front() : *(next - 1);
    }

    inline void write_gun_events(const gun_event_state& state, auto&& put)
    {
        // Native PS unpredictable event ring, verified in CG_CheckPlayerstateEvents.
        // The native history consumes each sequence once, even when several
        // camera archives carry the same server action or playback is paused.
        put(0x104, state.sequence);
        for (unsigned slot = 0; slot < 4; ++slot)
        {
            put(0x10C + slot * 8, state.ring[slot][0]);
            put(0x110 + slot * 8, state.ring[slot][1]);
        }
    }
}
