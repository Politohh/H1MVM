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
        unsigned total{}, headshots{};
        for (int arg = 1; arg < argc; ++arg)
        {
            std::ifstream file(argv[arg], std::ios::binary);
            check(bool(file), "Cannot read demo");
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
            const auto world = cod4::decode_world(bytes);
            const auto events = cod4::collect_obituary_events(world);
            unsigned special{};
            for (const auto& event : events)
            {
                const auto native = cod4::translate_obituary_event(event, 1500,
                    event.attacker == 1022 ? 2046 : event.attacker, event.victim, 51u | (9u << 17), true);
                if (event.event_parameter >= 128)
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
            << " headshots; native payloads and legacy crash regression\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
