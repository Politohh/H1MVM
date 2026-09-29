#include "component/demo/cod4_gun_events.hpp"
#include "component/demo/cod4_demo_reader.hpp"
#include <iostream>
#include <map>
#include <cstring>

void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void set(cod4::player_frame& frame, std::string_view name, unsigned value)
{
    for (unsigned i = 0; i < cod4::player_fields.size(); ++i)
        if (cod4::player_fields[i].name == name) { frame.values[i] = value; return; }
    throw std::runtime_error("Unknown test player field");
}
std::array<unsigned, 9> packet(const cod4::gun_event_state& state)
{
    std::array<std::uint8_t, 320> ps{};
    cod4::write_gun_events(state, [&](unsigned offset, unsigned value) { std::memcpy(ps.data() + offset, &value, 4); });
    std::array<unsigned, 9> result{};
    std::memcpy(result.data(), ps.data() + 0x104, 4);
    std::memcpy(result.data() + 1, ps.data() + 0x10C, 32);
    return result;
}
void consume(const cod4::gun_event_state& state, int& prior, std::map<unsigned, unsigned>& counts)
{
    // Native 0x328343..0x32839E ring consumption, independently reproduced
    // to check pause/repeated archives and its 8-bit wrap adjustment.
    const auto ps = packet(state);
    const int sequence = static_cast<int>(ps[0]);
    if (prior > sequence + 64) prior -= 256;
    for (int event = prior; event < sequence; ++event) ++counts[ps[1 + (event & 3) * 2]];
    prior = sequence;
}
int main(int argc, char** argv)
{
    try
    {
        check(argc == 2, "Pass the Backlot firing fixture");
        std::ifstream file(argv[1], std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        const auto camera = cod4::decode(bytes);
        const auto players = cod4::decode_players(bytes);
        const auto track = cod4::collect_gun_events(players);
        check(track.events.size() == 19, "Backlot recorded gun event count changed");
        std::map<unsigned, unsigned> weapons;
        for (const auto& event : track.events)
        {
            check(event.source == 26 && event.native == 51 && event.parameter == 0,
                "Backlot shot did not select the native authoritative firing path");
            ++weapons[event.weapon];
        }
        check(weapons.size() == 2 && weapons[18] == 14 && weapons[12] == 5, "Backlot AK-47/Deagle coverage changed");
        check(track.events.front().time == 54690 && track.events.back().time == 73442, "Recorded firing clock changed");
        check(track.frames.front().sequence == 0, "Pre-recording events replayed as new shots");
        int sequence = 0;
        std::map<unsigned, unsigned> counts;
        for (const auto& frame : camera.frames)
        {
            const auto& state = cod4::gun_events_at(track, frame.time);
            for (unsigned paused = 0; paused < 8; ++paused) consume(state, sequence, counts);
        }
        check(counts.size() == 1 && counts[51] == 19, "Camera archives or pause duplicated/lost native shots");
        const auto first_pass = counts;
        sequence = 0; counts.clear();
        for (const auto& frame : camera.frames) consume(cod4::gun_events_at(track, frame.time), sequence, counts);
        check(counts == first_pass, "Restart changed the firing action stream");
        check(packet(cod4::gun_events_at(track, 53000))[0] == 0
            && packet(cod4::gun_events_at(track, 74000))[0] == 19, "Seek sampling depended on prior playback state");

        // Source sequence wrap, mixed unsupported events, and native wrap.
        std::vector<cod4::player_frame> synthetic(302);
        unsigned source_sequence = 254;
        for (unsigned i = 0; i < synthetic.size(); ++i)
        {
            auto& frame = synthetic[i]; frame.time = 1000 + i * 50;
            set(frame, "commandTime", frame.time - 10); set(frame, "weapon", 18);
            for (unsigned slot = 0; slot < 4; ++slot) set(frame, std::format("events[{}]", slot), 26);
            if (i) ++source_sequence;
            set(frame, "eventSequence", source_sequence & 255);
        }
        const auto wrapped = cod4::collect_gun_events(synthetic);
        check(wrapped.events.size() == 301 && wrapped.frames.back().sequence == 45, "Event wrap lost shots");
        sequence = 0; counts.clear();
        for (const auto& state : wrapped.frames) { consume(state, sequence, counts); consume(state, sequence, counts); }
        check(counts[51] == 301, "Native event sequence wrap duplicated/lost shots");
        set(synthetic[1], "events[2]", 73);
        check(cod4::collect_gun_events(synthetic).events.size() == 300, "Unrelated event became gun FX");
        for (const auto [source, native] : std::array<std::pair<unsigned, unsigned>, 4>{{{26,51},{27,52},{28,42},{29,43}}})
            check(cod4::native_gun_event(source) == native, "Last shot/rechamber/brass semantic mapping changed");
        check(cod4::native_gun_event(39) == 0 && cod4::native_gun_event(45) == 0, "Source turret/grenade event became a gun action");
        set(synthetic[2], "weapon", 0);
        const auto unarmed = cod4::collect_gun_events(synthetic);
        check(unarmed.events.size() == 299 && unarmed.omitted.size() == 1 && unarmed.omitted[0].weapon == 0,
            "Delayed unarmed action failed conversion or guessed a weapon identity");
        bool rejected{};
        set(synthetic[1], "eventSequence", 256);
        try { (void)cod4::collect_gun_events(synthetic); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Malformed recorded sequence accepted");
        std::cout << "PASS: 19 Backlot shots (14 AK-47, 5 Desert Eagle), " << camera.frames.size()
            << " camera samples; native firing path, PS payload, pause/restart, source/native wrap, seek sampling and malformed-input rejection\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
