#include "component/demo/cod4_obituary_events.hpp"
#include <iostream>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
unsigned word(const cod4::h1_entity& entity, unsigned offset)
{
    unsigned value{}; std::memcpy(&value, entity.data() + offset, 4); return value;
}

int main(int argc, char** argv)
{
    try
    {
        check(argc >= 2, "Pass source demo fixtures");
        unsigned total{}, headshots{}, environmental{};
        for (int arg = 1; arg < argc; ++arg)
        {
            std::ifstream file(argv[arg], std::ios::binary);
            check(bool(file), "Cannot read demo");
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
            const auto world = cod4::decode_world(bytes);
            const auto camera = cod4::decode(bytes);
            const auto events = cod4::collect_obituary_events(world, camera.configstrings.at(2258));
            const auto raw_events = cod4::collect_obituary_events(world);
            check(events.size() == raw_events.size(), "Weapon classification dropped a kill notification");
            for (std::size_t i = 0; i < events.size(); ++i)
                check(events[i].time == raw_events[i].time && events[i].entity == raw_events[i].entity
                    && events[i].attacker == raw_events[i].attacker && events[i].victim == raw_events[i].victim
                    && events[i].event_parameter == raw_events[i].event_parameter,
                    "Weapon classification changed recorded obituary attribution or timing");
            unsigned special{};
            for (const auto& event : events)
            {
                const auto native = cod4::translate_obituary_event(event, 1500,
                    event.attacker == 1022 ? 2046 : event.attacker, event.victim, 51u | (9u << 17), true);
                if (event.environmental)
                {
                    ++environmental;
                    check(!event.source_weapon && event.event_parameter > 0 && event.event_parameter < 128,
                        "Environmental death lost its original source parameter");
                    check(!word(native, 76) && !native[6] && word(native, 36) == 200,
                        "Car death still dereferences a weapon or substitutes an unrelated kill icon");
                }
                else if (event.event_parameter >= 128)
                {
                    ++special;
                    check(!event.source_weapon && !word(native, 76) && !native[6], "Special death was interpreted as a weapon");
                    check(word(native, 36) >= 200, "Special death must use native eventParm");
                    if (event.event_parameter == 136)
                    {
                        ++headshots;
                        check(word(native, 36) == 209, "Headshot must use H1 headshot icon");
                    }
                }
                else
                {
                    check(event.source_weapon == event.event_parameter, "Kill weapon was inferred from held weapon");
                    check(word(native, 76) == (51u | (9u << 17)) && native[6] == 1 && !word(native, 36),
                        "Native weapon, attachment or alternate mode changed");
                }
                ++total;
            }
            std::cout << std::filesystem::path(argv[arg]).filename().string() << ": " << events.size()
                << " obituaries, " << special << " special deaths\n";
        }
        check(headshots > 0, "Fixtures do not exercise the reported headshot crash");
        cod4::world_track car_world;
        car_world.frames.resize(2);
        car_world.frames[0].time = 1000;
        auto& car = car_world.frames[0].entities[100];
        car.set(4, 17 + 66); car.set(116, 2); car.set(120, 1); car.set(156, 1);
        car_world.frames[1] = car_world.frames[0];
        car_world.frames[1].time = 1050;
        const auto car_kills = cod4::collect_obituary_events(car_world, "destructible_car ak47_mp");
        check(car_kills.size() == 1 && car_kills[0].environmental && !car_kills[0].source_weapon
            && car_kills[0].time == 1000 && car_kills[0].attacker == 1 && car_kills[0].victim == 2,
            "Car death duplicated or lost its recorded time/attacker/victim");
        check(cod4::obituary_events_at(car_kills, 999) == 0 && cod4::obituary_events_at(car_kills, 1000) == 1,
            "Environmental kill timing changed during a seek");
        const auto normal_kills = cod4::collect_obituary_events(car_world, "ak47_mp destructible_car");
        check(!normal_kills[0].environmental && normal_kills[0].source_weapon == 1,
            "Environmental weapon ordinal was hardcoded or a normal gun was suppressed");
        const auto unknown_kills = cod4::collect_obituary_events(car_world, "made_up_mod_gun_mp");
        check(!unknown_kills[0].environmental && unknown_kills[0].source_weapon == 1,
            "Unknown weapon was silently classified as an environmental death");
        car_world.frames[0].entities[100].set(156, 2);
        bool invalid_weapon{};
        try { cod4::collect_obituary_events(car_world, "destructible_car"); }
        catch (const std::runtime_error&) { invalid_weapon = true; }
        check(invalid_weapon, "Out-of-range obituary weapon accepted");
        cod4::h1_entity legacy{};
        cod4::write_native(legacy, 76, 0x660088u); // Exact crashing dump's weapon value.
        cod4::repair_legacy_obituary(legacy);
        check(word(legacy, 76) == 0 && word(legacy, 36) == 209, "Legacy Crossfire headshot still dereferences weapon 136");
        cod4::write_native(legacy, 76, 17u | (27u << 17));
        cod4::repair_legacy_obituary(legacy);
        check(word(legacy, 76) == 27 && word(legacy, 36) == 0, "Legacy weapon notification changed");
        const std::array<unsigned, 7> source{135,136,137,139,140,141,143};
        const std::array<unsigned, 7> native{208,209,212,213,214,215,217};
        for (unsigned i = 0; i < source.size(); ++i)
            check(cod4::native_obituary_death(source[i]) == native[i], "Special death icon mapping changed");
        std::cout << "PASS: " << total << " recorded obituaries including " << headshots
            << " headshots and " << environmental << " car deaths; native payloads, environmental attribution/timing and legacy crash regression\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
