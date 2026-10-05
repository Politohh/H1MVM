#include "component/demo/demo_timeline.hpp"
#include "component/demo/demo_input.hpp"
#include <cassert>
#include <iostream>
#include <limits>

static std::vector<std::uint8_t> footer(std::string extension = {})
{
    std::string bytes(8, '\0');
    bytes += "[00:00:09]";
    bytes.push_back('\0');
    bytes += extension;
    return {bytes.begin(), bytes.end()};
}

int main()
{
    using key_result = demo_input::shortcut_key::result;
    demo_input::shortcut_key space;
    assert(demo_input::pause_allowed(0, false, false));
    assert(demo_input::pause_allowed(0x40, true, false)); // actual native MWR menu
    assert(!demo_input::pause_allowed(0x40, false, false)); // unrelated native menu
    assert(!demo_input::pause_allowed(0x10, true, false)); // external overlay
    assert(!demo_input::pause_allowed(1, true, false)); // console
    assert(!demo_input::pause_allowed(0x48, true, false)); // chat + native UI
    assert(!demo_input::pause_allowed(0x41, true, false)); // console + native UI
    assert(!demo_input::pause_allowed(0, true, true)); // editor text input
    assert(space.event(true, true) == key_result::toggle);
    assert(space.event(true, true) == key_result::consume); // held key
    assert(space.event(false, false) == key_result::consume); // console opened before release
    assert(space.event(true, true) == key_result::toggle); // next press resumes
    assert(space.event(false, true) == key_result::consume);
    assert(demo_input::step_speed(.6f, 1) == 1.f);
    assert(demo_input::step_speed(.6f, -1) == .5f);
    assert(demo_input::step_speed(1.f, 1) == 2.f);
    assert(demo_input::step_speed(1.f, -1) == .5f);
    assert(demo_input::step_speed(4.f, 1) == 4.f);
    assert(demo_input::step_speed(.025f, -1) == .025f);
    assert(space.event(true, false) == key_result::pass); // typing
    assert(space.event(true, true) == key_result::pass); // focus changes while held
    assert(space.event(false, true) == key_result::pass);
    assert(space.event(true, true) == key_result::toggle);
    assert(space.event(false, true) == key_result::consume);
    std::cout << "PASS shortcut input focus, console/chat/editor isolation, repeat-key handling and bounded speed stepping\n";
    using namespace demo_timeline;
    const std::vector<event> events{{5000, 1, 0}, {1200, 0, 2}, {1200, 0, 2}, {8000, 2046, 0},
        {900, 0, 2}, {10001, 0, 1}, {2000, 50, 0}};
    const auto bytes = footer(encode(0, events, 1000, 10000));
    const auto parsed = decode(bytes, 1000, 10000);
    assert(parsed && parsed->pov == 0 && parsed->events.size() == 3);
    assert(parsed->events[0] == (event{1200, 0, 2}));
    assert(parsed->events[1] == (event{5000, 1, 0}));
    assert(parsed->events[2].attacker == 2046);
    assert(!decode(footer(), 1000, 10000)); // existing recordings
    for (std::size_t i = 0; i < bytes.size(); ++i)
        assert(!decode(std::span(bytes).first(i), 1000, 10000));
    auto invalid = bytes;
    const auto offset = bytes.size() - 3 * 12 - 12;
    invalid[offset + 4] = 42;
    assert(!decode(invalid, 1000, 10000));
    invalid = bytes; invalid[offset + 8] = 255; invalid[offset + 9] = 255;
    assert(!decode(invalid, 1000, 10000));
    invalid = bytes; invalid[offset + 12 + 8] = 42;
    assert(!decode(invalid, 1000, 10000));
    assert(encode(42, events, 1000, 10000).empty());
    assert(clamp_target(-1, 1000, 10000) == 1000);
    assert(clamp_target(20000, 1000, 10000) == 9750);
    assert(clamp_target(1200, 1000, 1100) == 1000);
    const int high = std::numeric_limits<int>::max();
    assert(clamp_target(high, high - 1000, high) == high - 250);
    std::vector<event> many(max_events + 20);
    for (std::size_t i = 0; i < many.size(); ++i) many[i] = {static_cast<int>(i + 1000), 0, 1};
    const auto capped = footer(encode(0, many, 1000, 30000));
    assert(capped.size() < 131072 && decode(capped, 1000, 30000)->events.size() == max_events);
    std::cout << "PASS old footer compatibility, POV event indexing, sorting/deduplication, malformed/truncated indexes, size cap and EOF-safe seek bounds\n";
}
