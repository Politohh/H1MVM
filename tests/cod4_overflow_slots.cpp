#include "component/demo/cod4_client_slots.hpp"
#include "component/demo/cod4_actor_translation.hpp"
#include "component/demo/cod4_corpse_session.hpp"
#include "component/demo/cod4_replay_agents.hpp"
#include "component/demo/cod4_obituary_events.hpp"
#include "component/demo/cod4_model_metadata.hpp"
#include <iostream>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv)
{
    try
    {
        check(argc == 2, "Supply the overflow COD4 demo fixture");
        std::ifstream file(argv[1], std::ios::binary);
        check(file.good(), "Missing overflow demo");
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        auto world = cod4::decode_world(bytes);
        const auto players = cod4::decode_players(bytes);
        cod4::resolve_view_client(world, players);
        check(world.client_num == 42, "Separate CoD4x recorder became the local character");
        const auto slots = cod4::make_client_slots(world);
        check(slots.dynamic(), "Overflow recording did not use per-snapshot slots");
        std::set<unsigned> all_players;
        std::map<unsigned, std::pair<unsigned,unsigned>> previous;
        unsigned peak{}, swaps{}, corpses{}, unbound_corpses{};
        cod4::corpse_session data;
        for (const auto& frame : world.frames)
        {
            const auto current = slots.at(frame.time);
            check(current.client(42) == 0 && current.slots.size() <= 42, "Local POV or native character capacity changed");
            std::set<unsigned> destinations;
            unsigned native_players{}, native_agents{}, live = 1;
            for (const auto& [source,destination] : current.slots)
            {
                check(destination < 42 && destinations.insert(destination).second, "Two characters share a native slot");
                destination < 18 ? ++native_players : ++native_agents;
                const auto entity = frame.entities.find(source);
                const auto flags = slots.actor_flags(source, frame.time, entity == frame.entities.end() ? 0u : entity->second.get(8));
                const auto old = previous.find(destination);
                if (old != previous.end() && old->second.first != source)
                { check((flags ^ old->second.second) & 2u, "Reused entity did not restart native interpolation"); ++swaps; }
                previous.insert_or_assign(destination, std::pair{source, flags});
            }
            check(native_players <= 18 && native_agents <= 24, "Native snapshot row overflow");
            cod4::corpse_frame saved; saved.time = frame.time;
            const auto identities = cod4::clients_with_corpse_identities(world, frame.time);
            for (const auto& [number, actor] : frame.entities)
            {
                if (actor.get(4) == 1)
                {
                    const auto source = actor.get(140);
                    check(current.client(source) < 42, "Visible player lost its native slot");
                    all_players.insert(source); ++live;
                }
                if (actor.get(4) != 2) continue;
                const auto& identity = identities.at(actor.get(140));
                cod4::corpse_identity corpse;
                corpse.entity = cod4::actor_entity_number(number); corpse.team = identity.get(4);
                corpse.body = static_cast<std::uint16_t>(identity.get(8));
                for (unsigned i = 0; i < 6; ++i) corpse.attachments[i] = static_cast<std::uint16_t>(identity.get(12 + 4 * i));
                saved.corpses.push_back(corpse); ++corpses;
                if (!current.slots.contains(actor.get(140))) ++unbound_corpses;
            }
            peak = std::max(peak, live);
            data.frames.push_back(std::move(saved));
        }
        all_players.insert(42);
        check(all_players.size() == 43 && peak <= 42 && swaps > 0 && unbound_corpses > 0,
            "Fixture no longer exercises preserved players, reuse and independent corpse identities");
        const auto encoded = cod4::encode_corpses(data);
        const auto decoded = cod4::decode_corpses(encoded);
        for (auto frame = data.frames.rbegin(); frame != data.frames.rend(); ++frame)
            for (const auto& corpse : frame->corpses)
                check(decoded.sample(corpse.entity, frame->time) && *decoded.sample(corpse.entity, frame->time) == corpse,
                    "Corpse model changed after a backward seek or serialization");
        auto corrupt = encoded; corrupt.pop_back();
        bool rejected{}; try { cod4::decode_corpses(corrupt); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Truncated corpse data accepted");
        corrupt = encoded; corrupt.push_back(1); rejected = false;
        try { cod4::decode_corpses(corrupt); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Trailing corpse data accepted");
        const auto names = cod4::collect_agent_names(world, slots);
        std::vector<std::string> metadata;
        for (const auto& [name,token] : names) metadata.push_back("@agentname:" + std::to_string(token) + ":" + name);
        unsigned event_number = 1500, kills{};
        for (const auto& event : cod4::collect_obituary_events(world))
        {
            const auto [attacker,victim] = slots.obituary(event.attacker,event.victim,event.time);
            const auto& frame = world.sample(event.time);
            const auto token = [&](unsigned source)
            {
                return source == 1022 ? 0u : names.at(cod4::recorded_actor_name(frame.clients.at(source), frame.client_names.at(source)));
            };
            metadata.push_back("@obit:" + std::to_string(event_number++) + ":" + std::to_string(attacker) + ":"
                + std::to_string(victim) + ":" + std::to_string(token(event.attacker)) + ":" + std::to_string(token(event.victim))
                + ":" + std::to_string(event.attacker == 1022 ? 0 : frame.clients.at(event.attacker).get(4)) + ":"
                + std::to_string(frame.clients.at(event.victim).get(4)));
            ++kills;
        }
        check(cod4::parse_model_metadata(metadata).obituaries.size() == kills, "Recorded kill names did not survive serialization");
        cod4::world_track high; high.client_num = 63; high.frames.resize(1);
        high.frames[0].clients[63].set(8,1); high.frames[0].clients[42].set(8,1);
        const auto compact = cod4::make_client_slots(high);
        check(compact.client(63) == 0 && compact.reference(42) < 42 && compact.reference(64) == 42,
            "High source player and corpse references collided");
        std::cout << "PASS real_players=" << all_players.size() << " snapshots=" << world.frames.size()
            << " peak_live=" << peak << " swaps=" << swaps << " corpse_samples=" << corpses
            << " independent_corpse_samples=" << unbound_corpses << " names=" << names.size() << " kills=" << kills << '\n';
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
