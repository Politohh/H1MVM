#include "component/demo/cod4_hud_translation.hpp"
#include <iostream>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main(int argc, char** argv)
{
    try
    {
        // Same-value awards have new visible edges but reuse the previous
        // fadeStartTime. A fade-out and a clip's initial HUD are not awards.
        const std::map<int, std::string> strings{{310, "MP_PLUS"}};
        cod4::wire_state<160> popup;
        popup.set(0, 2); popup.set(52, 1); popup.set(120, std::bit_cast<unsigned>(5.0f));
        popup.set(36, 0xd880ffff); popup.set(44, 900);
        cod4::world_track world;
        world.hud_frames.push_back({1000, {{{}, {popup}}}});
        popup.set(36, 0x0080ffff); popup.set(44, 1100);
        world.hud_frames.push_back({1100, {{{}, {popup}}}});
        popup.set(36, 0xd880ffff);
        world.hud_frames.push_back({1200, {{{}, {popup}}}});
        world.hud_frames.push_back({1250, {{{}, {popup}}}});
        popup.set(36, 0x0080ffff); popup.set(44, 2200);
        world.hud_frames.push_back({2200, {{{}, {popup}}}});
        popup.set(36, 0xd880ffff);
        world.hud_frames.push_back({2300, {{{}, {popup}}}});
        auto events = cod4::collect_score_popups(world, strings);
        check(events.size() == 2 && events[0].time == 1200 && events[1].time == 2300,
            "Popup start confused with fade, repeated value or initial HUD");

        std::map<int, std::string> native{{8, "1 2 3"}};
        cod4::translate_minimap_configstrings({{822, "90"}, {823, "\"compass_map_mp_crash\" 10 20 -30 -40"}}, native);
        check(native.at(1227) == "90" && native.at(1228) == "\"compass_map_mp_crash\" 10 20 -30 -40"
            && native.at(8) == "1 2 3", "Minimap orientation/bounds or map center changed");

        check(argc == 3, "Pass Crossfire and Diablo fixture paths");
        for (int arg = 1; arg < argc; ++arg)
        {
            std::ifstream file(argv[arg], std::ios::binary);
            check(file.good(), "Fixture unavailable");
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
            const auto camera = cod4::decode(bytes);
            const auto recorded = cod4::decode_world(bytes);
            const auto popups = cod4::collect_score_popups(recorded, camera.configstrings);
            const auto kills = cod4::collect_obituary_events(recorded);
            unsigned matched = 0, assists = 0;
            for (const auto& event : popups)
            {
                const auto value = std::bit_cast<float>(event.element.get(120));
                if (value == 5)
                {
                    check(std::ranges::any_of(kills, [&](const auto& kill) {
                        return kill.time == event.time && kill.attacker == static_cast<unsigned>(recorded.client_num);
                    }), "Kill popup still delayed from recorded obituary");
                    ++matched;
                }
                else
                {
                    check(arg == 2 && value == 3 && (event.time == 36838400 || event.time == 37191800),
                        "Non-kill award lost its recorded time/value");
                    ++assists;
                }
            }
            check(matched == (arg == 1 ? 3u : 23u) && assists == (arg == 1 ? 0u : 2u),
                "Missing or duplicate score awards");
            std::cout << "PASS: " << matched << " kill awards at obituary time, " << assists << " recorded assists\n";
        }
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
