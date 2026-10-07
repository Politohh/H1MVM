#include "component/demo/cod4_airstrike.hpp"
#include "component/demo/cod4_bullet_events.hpp"
#include "component/demo/cod4_obituary_events.hpp"
#include "component/demo/cod4_model_metadata.hpp"
#include <iostream>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<typename F> void rejects(F&& function, const char* message)
{ bool failed{}; try { function(); } catch (const std::runtime_error&) { failed = true; } check(failed, message); }

int main(int argc, char** argv)
{
    try
    {
        // A slot appearing only later must remain reserved during early events.
        cod4::world_track synthetic;
        synthetic.frames.resize(2);
        synthetic.frames[1].entities[72] = {};
        std::map<unsigned, cod4::h1_entity> authored;
        for (unsigned id = 1024; id < 2046; ++id) authored[id] = {};
        auto late = cod4::make_event_entity_pool(synthetic, authored);
        check(late.allocate() == 73, "Future source entity collided with an early event");
        auto empty = cod4::event_entity_pool({});
        for (unsigned id = 1024; id < 2046; ++id)
            check(empty.allocate() == id, "Established high-range allocation changed");
        for (unsigned id = 72; id < 1024; ++id)
            check(empty.allocate() == id, "Fallback allocation used protected or repeated slots");
        rejects([&] { empty.allocate(); }, "Native event capacity overflow accepted");
        for (const unsigned id : {0u, 41u, 71u, 2046u, 2047u})
        {
            check(!cod4::valid_event_entity(id), "Protected event entity accepted");
            rejects([&] { cod4::parse_model_metadata({"@impact:" + std::to_string(id) + ":1000"}); },
                "Metadata accepted a protected entity");
            rejects([&] { cod4::parse_model_metadata({"@agentname:1:A", "@agentname:2:V",
                "@obit:" + std::to_string(id) + ":0:1:1:2:1:2"}); }, "Obituary metadata accepted a protected entity");
        }
        check(argc == 3, "Pass the reported Crash recording and its runtime authored-prop count");
        std::ifstream file(argv[1], std::ios::binary);
        check(bool(file), "Cannot read recording");
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        const auto camera = cod4::decode(bytes);
        auto world = cod4::decode_world(bytes);
        cod4::resolve_view_client(world, cod4::decode_players(bytes));
        const auto clients = cod4::make_client_slots(world);
        const auto grenades = cod4::collect_grenade_events(world);
        const auto cars = cod4::collect_car_fx(world, camera.configstrings);
        const auto airstrikes = cod4::collect_airstrikes(world, camera.configstrings);
        const auto bullets = cod4::collect_bullet_events(world);
        const auto obituaries = cod4::collect_obituary_events(world, camera.configstrings.at(2258));
        check(!grenades.events.empty() && !cars.events.empty() && !bullets.empty() && !obituaries.empty(),
            "Fixture lacks the event categories needed for boundary checks");
        for (const unsigned id : {72u, 1023u, 2045u})
        {
            cod4::translate_grenade_event(grenades.events.front(), id, 17);
            cod4::translate_car_fx(cars.events.front(), id, 511);
            cod4::translate_bullet_event(bullets.front(), id, 17);
            cod4::translate_obituary_event(obituaries.front(), id, 0, 1, 17);
        }
        const auto lower_metadata = cod4::parse_model_metadata({"@grenade:72:1000", "@carfx:73:1050",
            "@scriptfx:74:1100", "@impact:75:1150", "@fx:511:fx/explosions/small_vehicle_explosion"});
        check(lower_metadata.grenade_times.at(72) == 1000 && lower_metadata.car_fx_times.at(73) == 1050
            && lower_metadata.car_fx_times.at(74) == 1100 && lower_metadata.bullet_times.at(75) == 1150,
            "Fallback FX categories lost their timing metadata");
        for (const unsigned id : {71u, 2046u})
        {
            rejects([&] { cod4::translate_grenade_event(grenades.events.front(), id, 17); }, "Grenade entity bound lost");
            rejects([&] { cod4::translate_car_fx(cars.events.front(), id, 511); }, "Car FX entity bound lost");
            rejects([&] { cod4::translate_bullet_event(bullets.front(), id, 17); }, "Impact entity bound lost");
            rejects([&] { cod4::translate_obituary_event(obituaries.front(), id, 0, 1, 17); }, "Obituary entity bound lost");
        }
        // Prop count is captured from the failed native conversion log. Models
        // themselves require H1's asset registry; this test covers slot ownership.
        const auto prop_count = static_cast<unsigned>(std::stoul(argv[2]));
        check(prop_count < 1022, "Authored prop count outside native capacity");
        authored.clear();
        for (unsigned id = 1024; id < 1024 + prop_count; ++id) authored[id] = {};
        auto pool = cod4::make_event_entity_pool(world, authored);
        auto repeated = cod4::make_event_entity_pool(world, authored);
        std::set<unsigned> reserved;
        for (const auto& frame : world.frames)
            for (const auto& [id, entity] : frame.entities) reserved.insert(cod4::translate_entity_number(id));
        for (const auto& [id, entity] : authored) reserved.insert(id);
        cod4::entity_snapshot snapshot;
        for (const auto id : reserved)
        {
            if (id >= 2046) continue;
            cod4::h1_entity placeholder{};
            cod4::write_native(placeholder, 0, static_cast<std::uint16_t>(id));
            snapshot.add(placeholder);
        }
        std::set<unsigned> allocated;
        std::map<unsigned, int> grenade_times, car_times, bullet_times;
        std::vector<std::string> metadata{"@fx:511:fx/explosions/small_vehicle_explosion",
            "@fx:510:fx/explosions/clusterbomb_mp", "@agentname:1:A", "@agentname:2:V"};
        unsigned fallback{};
        const auto allocate = [&]
        {
            const auto id = pool.allocate();
            check(id == repeated.allocate(), "Allocation is not deterministic");
            check(cod4::valid_event_entity(id) && !reserved.contains(id) && allocated.insert(id).second,
                "Event collided with another event, recorded entity or authored prop");
            fallback += id < 1024;
            return id;
        };
        const auto timing = [&](const char* kind, unsigned id, int time, auto& times)
        { times.emplace(id, time); metadata.push_back(std::string(kind) + ':' + std::to_string(id) + ':' + std::to_string(time)); };
        // Match export_demo's complete allocation order, rather than testing
        // each category against an independently chosen constant entity ID.
        for (const auto& event : grenades.events)
        {
            const auto id = allocate(); snapshot.add(cod4::translate_grenade_event(event, id, 17));
            timing("@grenade", id, event.time, grenade_times);
        }
        for (const auto& event : cars.events)
        {
            const auto id = allocate(); snapshot.add(cod4::translate_car_fx(event, id, 511));
            timing("@carfx", id, event.time, car_times);
        }
        for (const auto& event : airstrikes.bursts)
        {
            const auto id = allocate(); snapshot.add(cod4::translate_car_fx(event, id, 510));
            timing("@scriptfx", id, event.time, car_times);
        }
        for (const auto& event : bullets)
        {
            const auto id = allocate(); const auto slots = clients.at(event.time);
            snapshot.add(cod4::translate_bullet_event(event, id, 17, false, 0, &slots));
            timing("@impact", id, event.time, bullet_times);
        }
        for (const auto& event : obituaries)
        {
            const auto id = allocate(); const auto [attacker, victim] = clients.obituary(event.attacker, event.victim, event.time);
            snapshot.add(cod4::translate_obituary_event(event, id, attacker, victim, event.source_weapon ? 17 : 0));
            metadata.push_back("@obit:" + std::to_string(id) + ':' + std::to_string(attacker) + ':'
                + std::to_string(victim) + ":1:2:1:2");
        }
        check(fallback > 0, "Fixture did not reproduce the old high-range overflow");
        const auto parsed = cod4::parse_model_metadata(metadata);
        check(parsed.grenade_times == grenade_times && parsed.car_fx_times == car_times
            && parsed.bullet_times == bullet_times && parsed.obituaries.size() == obituaries.size(),
            "Fallback event metadata lost identity or seek timing");
        rejects([] { cod4::parse_model_metadata({"@impact:72:1000", "@grenade:72:1050"}); },
            "Fallback event identity collision accepted");
        std::cout << "PASS: " << grenades.events.size() << " grenades, " << cars.events.size() << " car FX, "
            << airstrikes.bursts.size() << " airstrike bursts, " << bullets.size() << " impacts, " << obituaries.size()
            << " obituaries; " << allocated.size() << " unique events, " << fallback << " fallback slots, "
            << reserved.size() << " globally reserved slots; zero collisions and metadata roundtrip passed\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
