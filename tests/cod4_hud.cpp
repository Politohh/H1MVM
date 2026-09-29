#include "component/demo/cod4_hud_translation.hpp"
#include "component/demo/cod4_obituary_events.hpp"
#include <iostream>

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("Pass the Strike CoD4 demo");
        std::ifstream file(argv[1], std::ios::binary);
        if (!file) throw std::runtime_error("Cannot open source demo");
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        const auto camera = cod4::decode(bytes);
        const auto players = cod4::decode_players(bytes);
        const auto world = cod4::decode_world(bytes);
        const auto recorded_client = world.frames.front().clients.find(static_cast<unsigned>(world.client_num));
        if (recorded_client == world.frames.front().clients.end())
            throw std::runtime_error("Recorded Strike client name unavailable");
        const auto native_client = cod4::translate_client(recorded_client->second, 1, {});
        if (std::string_view(reinterpret_cast<const char*>(native_client.data() + 42)) != "POLITO")
            throw std::runtime_error("Recorded killer name was lost in H1 client state");
        if (players.size() != 595 || world.hud_frames.size() != players.size())
            throw std::runtime_error("Strike HUD/player frame count changed");
        if (players.front().stats[0] != 100 || players.front().stats[2] != 100
            || players.back().stats[0] != 0)
            throw std::runtime_error("Recorded health/max health was lost");

        unsigned score_changes = 0;
        std::uint32_t prior_fade = 0;
        for (const auto& frame : world.hud_frames)
        {
            if (&world.sample_hud(frame.time) != &frame)
                throw std::runtime_error("HUD sampling changed at an exact frame");
            for (const auto& element : frame.groups[1])
            {
                if (!cod4::is_score_popup(element, camera.configstrings)) continue;
                if (std::bit_cast<float>(element.get(120)) != 5.0f)
                    throw std::runtime_error("Recorded +5 value changed");
                const auto native = cod4::translate_score_popup(element, 148);
                std::uint32_t value = 0, label = 0, fade = 0;
                std::memcpy(&value, native.data() + 128, 4);
                std::memcpy(&label, native.data() + 76, 4);
                std::memcpy(&fade, native.data() + 56, 4);
                std::uint32_t type = 0, screen_anchor = 0;
                std::memcpy(&type, native.data() + 28, 4);
                std::memcpy(&screen_anchor, native.data() + 8, 4);
                if (std::bit_cast<float>(value) != 5.0f || label != 148 || fade != element.get(44)
                    || type != 2 || screen_anchor != 5)
                    throw std::runtime_error("Native score value, label or fade time changed");
                if (fade != prior_fade) { ++score_changes; prior_fade = fade; }
            }
        }
        if (score_changes < 4) throw std::runtime_error("Recorded score popup updates were lost");
        const auto popups = cod4::collect_score_popups(world, camera.configstrings);
        const std::array<int, 5> expected{11071700, 11073650, 11075300, 11082600, 11083900};
        if (popups.size() != expected.size()) throw std::runtime_error("Unexpected Strike popup count");
        for (std::size_t i = 0; i < expected.size(); ++i)
            if (popups[i].time != expected[i] || popups[i].element.get(44) != static_cast<unsigned>(expected[i]))
                throw std::runtime_error("Score popup does not start at its recorded score change");
        const auto obituaries = cod4::collect_obituary_events(world, players);
        if (obituaries.size() != 18 || obituaries[7].time != expected[0]
            || obituaries[7].attacker != 0 || obituaries[7].victim != 3
            || obituaries[7].source_weapon != 17)
            throw std::runtime_error("Recorded Strike obituary events were lost");
        const auto obituary = cod4::translate_obituary_event(obituaries[7], 1500, 0, 3, 27);
        std::uint16_t target{}, attacker{};
        std::memcpy(&target, obituary.data() + 2, sizeof(target));
        std::memcpy(&attacker, obituary.data() + 168, sizeof(attacker));
        std::uint32_t packed{};
        std::memcpy(&packed, obituary.data() + 76, sizeof(packed));
        if (obituary[10] != 160 || target != 3 || attacker != 0
            || packed != (17u | 27u << 17))
            throw std::runtime_error("H1 obituary event payload changed");
        std::cout << "PASS: " << world.hud_frames.size() << " HUD frames, recorded health and "
                  << score_changes << " +5 popup updates, " << obituaries.size() << " obituaries\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
