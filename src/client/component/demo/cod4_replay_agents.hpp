#pragma once
#include "cod4_client_slots.hpp"

namespace cod4
{
    using h1_agent = std::array<std::uint8_t, 128>;

    inline bool agent_slot(unsigned destination)
    {
        return destination >= client_slot_map::player_capacity && destination < client_slot_map::capacity;
    }

    inline std::string recorded_actor_name(const client_state& client, std::string_view full_name)
    {
        if (!full_name.empty()) return std::string(full_name.substr(0, 31));
        const auto* text = reinterpret_cast<const char*>(client.words.data() + 15);
        const auto end = std::find(text, text + 16, '\0');
        if (end == text) throw std::runtime_error("Recorded agent has no player name");
        return std::string(text, end);
    }

    inline std::map<std::string, unsigned> collect_agent_names(const world_track& world, const client_slot_map& slots)
    {
        std::map<std::string, unsigned> names;
        for (const auto& frame : world.frames)
            for (const auto& [number, client] : frame.clients)
            {
                if (!client.get(8) || !agent_slot(slots.client(number))) continue;
                const auto full = frame.client_names.find(number);
                const auto name = recorded_actor_name(client,
                    full == frame.client_names.end() ? std::string_view{} : std::string_view(full->second));
                if (names.contains(name)) continue;
                if (names.size() >= 1023) throw std::runtime_error("Too many recorded agent names");
                names.emplace(name, static_cast<unsigned>(names.size() + 1));
            }
        return names;
    }

    inline void assign_actor_slot(h1_entity& actor, unsigned destination)
    {
        if (destination >= client_slot_map::capacity || (actor[10] != 1 && actor[10] != 2))
            throw std::runtime_error("Invalid replay actor slot");
        if (actor[10] == 1)
        {
            write_native(actor, 0, static_cast<std::uint16_t>(destination));
            if (agent_slot(destination)) actor[10] = 18; // ET_AGENT, same native player animation fields.
        }
        write_native(actor, 12, static_cast<std::int8_t>(destination));
    }

    inline h1_agent translate_agent(const client_state& source, unsigned destination, std::uint16_t body,
        const std::array<std::uint16_t, 6>& attachments, unsigned name)
    {
        if (!agent_slot(destination) || !body || body >= 1024 || !name || name >= 1024)
            throw std::runtime_error("Invalid native replay agent identity");
        h1_agent result{};
        // MSG_WriteDeltaAgent (0x4F3330), CG_UpdateAgentInfo (0x32EE10).
        write_native(result, 0, static_cast<std::uint16_t>(destination));
        write_native(result, 2, body);
        write_native(result, 14, std::uint16_t{2047}); // No owning player entity.
        for (unsigned i = 0; i < attachments.size(); ++i)
            write_native(result, 16 + i * 2, attachments[i]);
        write_native(result, 76, source.get(4)); // Recorded team, not a clientState offset.
        write_native(result, 124, name); // Recorded name token, resolved by the replay's native name hook.
        return result;
    }
}
