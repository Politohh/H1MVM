#pragma once

#include "cod4_world_translation.hpp"
#include <set>

namespace cod4
{
    inline void resolve_view_client(world_track& world, const std::vector<player_frame>& players)
    {
        if (players.empty()) return;
        const auto source = players.front().integer("ClientNum");
        if (source >= 64 || std::any_of(players.begin(), players.end(), [source](const auto& player)
            { return player.integer("ClientNum") != source; })) return;
        // CoD4x can archive a separate recorder identity while playerState
        // follows the actual player. Do not allocate a phantom local actor.
        for (const auto& frame : world.frames)
            if (const auto client = frame.clients.find(source); client != frame.clients.end() && client->second.get(8))
            { world.client_num = static_cast<int>(source); return; }
    }
    // Keep H1's 18 client rows intact. Additional recorded identities use
    // its 24 native agent rows and character slots, before corpse slot 42.
    struct client_slot_map
    {
        static constexpr unsigned player_capacity = 18;
        static constexpr unsigned capacity = 42;
        static constexpr unsigned source_capacity = 64;
        std::map<unsigned, unsigned> slots;
        std::map<int, std::map<unsigned, unsigned>> frames;
        std::map<int, std::map<unsigned, unsigned>> teleport;

        bool dynamic() const { return !frames.empty(); }
        client_slot_map at(int time) const
        {
            if (!dynamic()) return *this;
            const auto next = frames.upper_bound(time);
            client_slot_map result;
            result.slots = (next == frames.begin() ? next : std::prev(next))->second;
            return result;
        }

        unsigned actor_flags(unsigned source, int time, unsigned flags) const
        {
            if (!dynamic()) return flags;
            const auto next = teleport.upper_bound(time);
            const auto& bits = (next == teleport.begin() ? next : std::prev(next))->second;
            return (flags & ~2u) | bits.at(source);
        }

        std::pair<unsigned, unsigned> obituary(unsigned attacker, unsigned victim, int time) const
        {
            const auto current = at(time);
            const auto a = current.slots.find(attacker), v = current.slots.find(victim);
            unsigned native_a = attacker == 1022 ? 2046u : a == current.slots.end() ? 41u : a->second;
            unsigned native_v = v == current.slots.end() ? 40u : v->second;
            if (attacker == victim) native_a = native_v;
            else if (native_a == native_v)
            {
                if (a == current.slots.end()) native_a = native_v == 41 ? 40 : 41;
                else native_v = native_a == 40 ? 41 : 40;
            }
            return {native_a, native_v};
        }

        unsigned client(unsigned source) const { return slots.at(source); }

        unsigned reference(unsigned source) const
        {
            if (source >= 64 && source < 72) return source - 22;
            if (source >= source_capacity) return source;
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
        if (*identities.rbegin() >= client_slot_map::source_capacity)
            throw std::runtime_error("Unsupported COD4 player slot");

        if (identities.size() > client_slot_map::capacity)
        {
            client_slot_map result;
            std::map<unsigned, unsigned> previous;
            struct binding { unsigned source{64}, source_bit{}, native_bit{}; };
            std::array<binding, client_slot_map::capacity> bindings{};
            const auto recorder = static_cast<unsigned>(world.client_num);
            for (const auto& frame : world.frames)
            {
                std::set<unsigned> required{recorder};
                for (const auto& [number, entity] : frame.entities)
                    if (entity.get(4) == 1) required.insert(entity.get(140));
                if (required.size() > client_slot_map::capacity)
                    throw std::runtime_error("Snapshot at " + std::to_string(frame.time) + " needs "
                        + std::to_string(required.size()) + " simultaneous players; native MWR supports 42");
                std::map<unsigned, unsigned> current{{recorder, 0}};
                std::set<unsigned> occupied{0};
                for (const auto source : required)
                    if (source != recorder && previous.contains(source))
                    { current.emplace(source, previous.at(source)); occupied.insert(previous.at(source)); }
                for (const auto source : required)
                {
                    if (current.contains(source)) continue;
                    unsigned destination = 1;
                    while (occupied.contains(destination)) ++destination;
                    current.emplace(source, destination); occupied.insert(destination);
                }
                // Keep dormant metadata while space permits, without reserving
                // a character slot for a corpse. Corpse models are archived separately.
                for (const auto& [source, destination] : previous)
                    if (!current.contains(source) && !occupied.contains(destination))
                    { current.emplace(source, destination); occupied.insert(destination); }
                for (const auto& [source, client] : frame.clients)
                {
                    if (!client.get(8) || current.contains(source) || occupied.size() == client_slot_map::capacity) continue;
                    unsigned destination = 1;
                    while (occupied.contains(destination)) ++destination;
                    current.emplace(source, destination); occupied.insert(destination);
                }
                std::map<unsigned, unsigned> bits;
                for (const auto& [source, destination] : current)
                {
                    const auto entity = frame.entities.find(source);
                    const auto bit = entity == frame.entities.end() ? 0u : entity->second.get(8) & 2u;
                    auto& old = bindings.at(destination);
                    if (old.source != source) old.native_bit = old.source == 64 ? bit : old.native_bit ^ 2u;
                    else old.native_bit ^= old.source_bit ^ bit;
                    old.source = source; old.source_bit = bit;
                    bits.emplace(source, old.native_bit);
                }
                result.frames.emplace(frame.time, current);
                result.teleport.emplace(frame.time, std::move(bits));
                previous = std::move(current);
            }
            result.slots = result.frames.begin()->second;
            return result;
        }

        client_slot_map result;
        const auto recorder = static_cast<unsigned>(world.client_num);
        result.slots.emplace(recorder, 0);
        std::set<unsigned> occupied{0};
        // Preserve the old recorder/zero swap wherever it already fits.
        for (const auto source : identities)
        {
            if (source == recorder) continue;
            const auto preferred = source == 0 ? recorder : source;
            if (preferred < client_slot_map::player_capacity)
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

    inline void remap_entity_references(h1_entity& entity, const client_slot_map& slots,
        const entity_state* recorded = nullptr)
    {
        for (const unsigned offset : {2u, 4u})
        {
            std::uint16_t source{};
            std::memcpy(&source, entity.data() + offset, sizeof(source));
            if (recorded) source = translate_entity_number(recorded->get(offset == 2 ? 116 : 124));
            write_native(entity, offset, static_cast<std::uint16_t>(slots.reference(source)));
        }
    }
}
