#include "component/demo/cod4_client_slots.hpp"
#include "component/demo/cod4_bullet_events.hpp"
#include "component/demo/cod4_obituary_events.hpp"
#include "component/demo/cod4_replay_agents.hpp"
#include "component/demo/cod4_model_metadata.hpp"
#include <iostream>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
unsigned short reference(const cod4::h1_entity& entity, unsigned offset)
{ unsigned short value; std::memcpy(&value, entity.data() + offset, 2); return value; }

int main(int argc, char** argv)
{
    try
    {
        cod4::world_track sparse;
        sparse.client_num = 17;
        sparse.frames.resize(1);
        for (unsigned source : {0u,1u,2u,3u,5u,7u,8u,9u,11u,12u,13u,14u,15u,16u,17u,18u,20u})
            sparse.frames[0].clients[source].set(8, 1);
        const auto slots = cod4::make_client_slots(sparse);
        check(slots.client(17) == 0 && slots.client(0) == 17
            && slots.client(18) == 4 && slots.client(20) == 6, "Sparse player slots were not compacted");
        check(slots.reference(2046) == 2046 && slots.reference(2047) == 2047
            && slots.reference(42) == 42, "World, none or corpse reference changed");
        check(slots.reference(19) == 2047, "Unrecorded player reference aliased another player");
        cod4::bullet_event unbound{};
        unbound.source_event = 41; unbound.shooter = 19; unbound.target = 1022; unbound.client = 19;
        const auto unknown = cod4::translate_bullet_event(unbound,1500,1,false,0,&slots);
        check(reference(unknown,2) == 2047 && reference(unknown,4) == 2046 && unknown[12] == 255,
            "Unrecorded impact identity must stay unbound");
        sparse.client_num = 20;
        check(cod4::make_client_slots(sparse).client(20) == 0, "High recorder slot was not made local");
        sparse.frames[0].clients[21].set(8, 1);
        check(cod4::make_client_slots(sparse).slots.size() == 18, "18-player recording rejected");
        sparse.frames[0].clients[22].set(8, 1);
        check(cod4::make_client_slots(sparse).slots.size() == 19, "19-player recording rejected");
        cod4::world_track full;
        full.frames.resize(1);
        for (unsigned source = 0; source < 42; ++source) full.frames[0].clients[source].set(8, 1);
        const auto expanded = cod4::make_client_slots(full);
        check(expanded.slots.size() == 42 && expanded.client(41) == 41, "42 native character slots unavailable");
        full.frames[0].clients[42].set(8, 1);
        bool rejected = false;
        try { (void)cod4::make_client_slots(full); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Player tree capacity overflow accepted");

        cod4::client_state source_client;
        source_client.set(4, 2); source_client.set(8, 7);
        const auto agent = cod4::translate_agent(source_client, 41, 768, {769,770,771,772,773,774}, 1023);
        std::uint16_t agent_number{}; std::memcpy(&agent_number, agent.data(), 2);
        check(agent_number == 41, "Agent entity identity changed");
        unsigned team{}, token{}; std::memcpy(&team, agent.data() + 76, 4); std::memcpy(&token, agent.data() + 124, 4);
        check(team == 2 && token == 1023, "Agent team/name fields lost");
        cod4::h1_entity native_actor{}; native_actor[10] = 1;
        cod4::write_native(native_actor,84,0x123456u);
        cod4::assign_actor_slot(native_actor, 41);
        check(reference(native_actor,0) == 41 && native_actor[10] == 18 && native_actor[12] == 41,
            "Extra actor did not use native agent path");
        unsigned animation{}; std::memcpy(&animation,native_actor.data()+84,4);
        check(animation == 0x123456u, "Agent path replaced the mapped native animation");
        native_actor[10] = 2; cod4::write_native(native_actor,0,std::uint16_t{42});
        cod4::assign_actor_slot(native_actor,41);
        check(reference(native_actor,0) == 42 && native_actor[10] == 2 && native_actor[12] == 41,
            "Agent corpse overwrote its corpse slot");
        const auto metadata = cod4::parse_model_metadata({"@agentname:1:[TAG]:player", "@agentname:1023:^1Other"});
        check(metadata.agent_names.at(1) == "[TAG]:player" && metadata.agent_names.at(1023) == "^1Other",
            "Recorded names were interpreted as asset names");
        for (const auto bad : {"@agentname:0:name", "@agentname:1024:name", "@agentname:1:", "@agentname:x:name"})
        {
            rejected = false;
            try { (void)cod4::parse_model_metadata({bad}); } catch (const std::runtime_error&) { rejected = true; }
            check(rejected, "Malformed recorded name accepted");
        }

        for (int argument = 1; argument < argc; ++argument)
        {
            std::ifstream input(argv[argument], std::ios::binary);
            check(input.good(), "Missing demo fixture");
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
            const auto world = cod4::decode_world(bytes);
            const auto map = cod4::make_client_slots(world);
            std::set<unsigned> destinations;
            for (const auto& [source, destination] : map.slots)
                check(destination < 42 && destinations.insert(destination).second, "Native client collision or overflow");
            check(map.client(world.client_num) == 0, "Recorder identity changed");
            for (const auto& frame : world.frames)
            {
                for (const auto& [number, actor] : frame.entities)
                {
                    if (actor.get(4) != 1 && actor.get(4) != 2) continue;
                    check(map.client(actor.get(140)) < 42, "Actor has no native character slot");
                    if (actor.get(4) == 1)
                        check(map.client(number) == map.client(actor.get(140)), "Player entity/client identity mismatch");
                    cod4::h1_entity native{};
                    cod4::write_native(native, 2, cod4::translate_entity_number(actor.get(116)));
                    cod4::write_native(native, 4, cod4::translate_entity_number(actor.get(124)));
                    cod4::remap_entity_references(native, map);
                    check(reference(native,2) == map.reference(cod4::translate_entity_number(actor.get(116)))
                        && reference(native,4) == map.reference(cod4::translate_entity_number(actor.get(124))),
                        "Actor references diverged from the identity map");
                }
                for (const auto& [number, client] : frame.clients)
                    if (client.get(8)) check(map.client(number) < 42, "Client row has no native slot");
            }
            unsigned impacts = 0, kills = 0;
            for (const auto& event : cod4::collect_bullet_events(world))
            {
                const auto native = cod4::translate_bullet_event(event, 1500, 1, false, 0, &map);
                check(reference(native, 2) == cod4::translate_entity_number(map.reference(event.shooter)),
                    "Impact shooter identity differs from actor");
                check(reference(native, 4) == (event.target
                    ? cod4::translate_entity_number(map.reference(event.target)) : 2046), "Impact target changed");
                ++impacts;
            }
            for (const auto& event : cod4::collect_obituary_events(world))
            {
                const auto attacker = event.attacker == 1022 ? 2046 : map.client(event.attacker);
                const auto victim = map.client(event.victim);
                const auto native = cod4::translate_obituary_event(event,1500,attacker,victim,1);
                check(reference(native,2) == victim && reference(native,168) == attacker,
                    "Killfeed identities differ from client names");
                ++kills;
            }
            std::cout << "PASS " << argv[argument] << " players=" << map.slots.size()
                << " snapshots=" << world.frames.size() << " impacts=" << impacts << " kills=" << kills << '\n';
        }
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
