#pragma once
#include "cod4_world_translation.hpp"
#include <set>

namespace cod4
{
    // Keep both native characters/corpses and COD4's original client range
    // reserved. Other event entities may use any unoccupied native slot.
    inline constexpr unsigned event_entity_min = 72;
    inline constexpr unsigned event_entity_end = 2046;

    inline bool valid_event_entity(unsigned number)
    {
        return number >= event_entity_min && number < event_entity_end;
    }

    class event_entity_pool
    {
        std::vector<unsigned> available;
        std::size_t cursor{};
    public:
        explicit event_entity_pool(const std::set<unsigned>& reserved)
        {
            // Preserve the established upper-range IDs when they fit. Use
            // globally unused source-range holes for longer recordings; each
            // event still has a unique identity for pause/seek FX and killfeed.
            for (const auto [first, last] : {std::pair{1024u, event_entity_end},
                std::pair{event_entity_min, 1024u}})
                for (unsigned number = first; number < last; ++number)
                    if (!reserved.contains(number)) available.push_back(number);
        }

        unsigned allocate()
        {
            if (cursor == available.size())
                throw std::runtime_error("COD4 recording exceeds available H1 event entity slots");
            return available[cursor++];
        }
    };

    inline event_entity_pool make_event_entity_pool(const world_track& world,
        const std::map<unsigned, h1_entity>& authored)
    {
        std::set<unsigned> reserved;
        for (const auto& frame : world.frames)
            for (const auto& [number, entity] : frame.entities)
                reserved.insert(translate_entity_number(number));
        for (const auto& [number, entity] : authored) reserved.insert(number);
        return event_entity_pool(reserved);
    }
}
