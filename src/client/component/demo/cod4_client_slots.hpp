#pragma once

#include "cod4_world_translation.hpp"
#include <set>

namespace cod4
{
    // CG_CreatePlayerAnimTrees (H1 0x31C890) allocates exactly 18 player
    // trees. The 42 characterInfo records also include non-player slots;
    // accepting a source client index below 42 is not sufficient.
    struct client_slot_map
    {
        static constexpr unsigned capacity = 18;
        std::map<unsigned, unsigned> slots;

        unsigned client(unsigned source) const { return slots.at(source); }

        unsigned reference(unsigned source) const
        {
            if (source >= 42) return source;
            const auto found = slots.find(source);
            return found == slots.end() ? 2047u : found->second;
        }
    };

    inline client_slot_map make_client_slots(const world_track& world)
    {
        std::set<unsigned> identities{static_cast<unsigned>(world.client_num)};
        for (const auto& frame : world.frames)
        {
            for (const auto& [number, client] : frame.clients)
                if (client.get(8)) identities.insert(number);
            for (const auto& [number, entity] : frame.entities)
                if (entity.get(4) == 1 || entity.get(4) == 2)
                    identities.insert(entity.get(140));
        }
        if (identities.size() > client_slot_map::capacity)
            throw std::runtime_error("Demo contains more than 18 distinct player slots; native MWR playback supports 18");
        if (*identities.rbegin() >= 42)
            throw std::runtime_error("Unsupported COD4 player slot");

        client_slot_map result;
        const auto recorder = static_cast<unsigned>(world.client_num);
        result.slots.emplace(recorder, 0);
        std::set<unsigned> occupied{0};
        // Preserve the old recorder/zero swap wherever it already fits.
        for (const auto source : identities)
        {
            if (source == recorder) continue;
            const auto preferred = source == 0 ? recorder : source;
            if (preferred < client_slot_map::capacity)
            {
                result.slots.emplace(source, preferred);
                occupied.insert(preferred);
            }
        }
        for (const auto source : identities)
        {
            if (result.slots.contains(source)) continue;
            unsigned destination = 1;
            while (occupied.contains(destination)) ++destination;
            result.slots.emplace(source, destination);
            occupied.insert(destination);
        }
        return result;
    }

    inline void remap_entity_references(h1_entity& entity, const client_slot_map& slots)
    {
        for (const unsigned offset : {2u, 4u})
        {
            std::uint16_t source{};
            std::memcpy(&source, entity.data() + offset, sizeof(source));
            write_native(entity, offset, static_cast<std::uint16_t>(slots.reference(source)));
        }
    }
}
