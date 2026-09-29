#include "component/demo/cod4_bullet_events.hpp"
#include "component/demo/cod4_model_metadata.hpp"
#include "component/demo/cod4_demo_reader.hpp"
#include <iostream>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<typename F> void rejects(F&& function, const char* message)
{ bool failed{}; try { function(); } catch (const std::exception&) { failed = true; } check(failed, message); }
template<typename T> T native(const cod4::h1_entity& data, unsigned offset)
{ T value{}; std::memcpy(&value, data.data() + offset, sizeof(value)); return value; }
cod4::entity_state impact(unsigned event = 41)
{
    cod4::entity_state result;
    result.set(0, 312); result.set(4, 17 + event); result.set(156, 104);
    result.set(196, 8); result.set(132, 7); result.set(116, 8); result.set(124, 1023);
    result.set(140, 6); result.set(212, 2);
    result.set(24, std::bit_cast<unsigned>(675.0f));
    result.set(28, std::bit_cast<unsigned>(-445.0f));
    result.set(32, std::bit_cast<unsigned>(203.0f));
    result.set(84, std::bit_cast<unsigned>(100.0f));
    return result;
}
int main(int argc, char** argv)
{
    try
    {
        auto source = impact();
        cod4::world_track world;
        world.frames = {{1000,1,{{312,source}},{}}, {1050,2,{{312,source}},{}},
            {1100,3,{},{}}, {1150,4,{{312,source}},{}}};
        auto events = cod4::collect_bullet_events(world);
        check(events.size() == 2 && events[0].time == 1000 && events[1].time == 1150,
            "Temporary impact duplication or entity reuse failed");
        source.set(4, 1); source.set(160, 255); source.set(172, 41); source.set(188, 104);
        world.frames = {{1000,1,{{312,source}},{}}, {1050,2,{{312,source}},{}}};
        world.frames[1].entities.at(312).set(160,0);
        world.frames[1].entities.at(312).set(176,42);
        world.frames[1].entities.at(312).set(192,0);
        events = cod4::collect_bullet_events(world);
        check(events.size() == 2 && events[0].source_event == 41 && events[1].source_event == 42,
            "Event ring wrap failed");
        for (unsigned event : {41,42,43})
        {
            source = impact(event);
            world.frames = {{1000,1,{{312,source}},{}}};
            const auto hit = cod4::collect_bullet_events(world).at(0);
            const auto data = cod4::translate_bullet_event(hit,1500,17,true);
            check(data[10] == (event == 41 ? 97 : event == 42 ? 100 : 101)
                && native<unsigned short>(data,2) == 8 && native<unsigned short>(data,4) == 2047
                && data[6] == 1 && data[11] == 7 && data[12] == 6 && data[28] == 1
                && native<unsigned>(data,76) == 17 && native<unsigned>(data,36) == 104
                && native<std::array<float,3>>(data,108) == std::array<float,3>{675,-445,203}
                && native<std::array<float,3>>(data,168) == std::array<float,3>{100,0,0}
                && native<unsigned>(data,204) == (event == 41 ? 2u : 0u), "Native impact payload is wrong");
            rejects([&] { cod4::translate_bullet_event(hit,2046,17); }, "Entity capacity overflow accepted");
            rejects([&] { cod4::translate_bullet_event(hit,1500,0); }, "Missing native weapon accepted");
        }
        source = impact(); source.set(124,0);
        world.frames = {{1000,1,{{312,source}},{}}};
        check(native<unsigned short>(cod4::translate_bullet_event(cod4::collect_bullet_events(world).at(0),1500,17),4) == 2046,
            "Absent hit identity became player zero");
        source = impact(); source.set(132,14);
        world.frames = {{1000,1,{{312,source}},{}}};
        const auto metal = cod4::collect_bullet_events(world).at(0);
        check(cod4::translate_bullet_event(metal,1500,17)[11] == 15, "Nonflesh surface became blood");
        check(cod4::bullet_events_at(events,999) == 0 && cod4::bullet_events_at(events,1000) == 1
            && cod4::bullet_events_at(events,1050) == 2 && cod4::bullet_events_at(events,999) == 0,
            "Impact pause/seek presence failed");
        source = impact(); source.set(156,162); world.frames = {{1000,1,{{312,source}},{}}};
        rejects([&] { cod4::collect_bullet_events(world); }, "Invalid impact normal accepted");
        source = impact(); source.set(84, std::bit_cast<unsigned>(std::numeric_limits<float>::infinity()));
        world.frames = {{1000,1,{{312,source}},{}}};
        rejects([&] { cod4::collect_bullet_events(world); }, "Nonfinite start accepted");
        const auto metadata = cod4::parse_model_metadata({"@impact:1500:1000", "@grenade:1499:1050"});
        check(metadata.bullet_times.at(1500) == 1000 && metadata.names.empty(), "Impact timing metadata lost");
        rejects([] { cod4::parse_model_metadata({"@impact:1500:1000","@grenade:1500:1050"}); },
            "Impact/grenade identity collision accepted");
        rejects([] { cod4::parse_model_metadata({"@carfx:1500:1000","@impact:1500:1050"}); },
            "Impact/car identity collision accepted");
        rejects([] { cod4::parse_model_metadata({"@impact:1500:-1"}); }, "Invalid impact time accepted");
        check(argc >= 2, "Pass the Promod recording, then optional regression recordings");
        for (int fixture = 1; fixture < argc; ++fixture)
        {
            std::ifstream file(argv[fixture],std::ios::binary);
            check(static_cast<bool>(file), "Cannot open bullet fixture");
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
            const auto track = cod4::decode_world(bytes);
            const auto hits = cod4::collect_bullet_events(track);
            std::map<unsigned,unsigned> counts;
            unsigned flesh{};
            for (std::size_t i = 0; i < hits.size(); ++i)
            {
                const auto& hit = hits[i];
                ++counts[hit.source_event]; flesh += hit.surface == 7;
                const auto data = cod4::translate_bullet_event(hit,1024 + static_cast<unsigned>(i),17);
                check(data[10] == 20 + cod4::native_bullet_event(hit.source_event), "Recorded impact translation failed");
            }
            if (fixture == 1)
            {
                check(hits.size() == 25 && counts[41] == 21 && counts[42] == 0 && counts[43] == 4 && flesh == 25,
                    "Promod bullet impact coverage changed");
                const auto& hit = hits.front();
                check(hit.time == 52429800 && hit.entity == 312 && hit.shooter == 8 && hit.weapon == 8
                    && hit.parameter == 104 && hit.origin == std::array<float,3>{675,-445,203}
                    && hit.start == std::array<float,3>{0,0,0}, "Recorded blood position/clock/weapon changed");
                const auto incoming = std::find_if(hits.begin(), hits.end(), [](const auto& e) { return e.time == 52470950; });
                check(incoming != hits.end() && incoming->source_event == 43
                    && incoming->client == 4 && incoming->shooter == 3, "Recorded incoming-hit identity changed");
            }
            std::cout << "PASS: bullet fixture " << std::filesystem::path(argv[fixture]).filename().string()
                << " impacts=" << hits.size() << " general=" << counts[41] << " small=" << counts[42]
                << " large=" << counts[43] << " flesh=" << flesh << '\n';
        }
        std::cout << "PASS: native bullet payloads, normal/surface mapping, ring wrap/reuse, seek timing and malformed data\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
