#include "component/demo/cod4_hud_translation.hpp"
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
                std::memcpy(&value, native.data() + 136, 4);
                std::memcpy(&label, native.data() + 72, 4);
                std::memcpy(&fade, native.data() + 56, 4);
                if (std::bit_cast<float>(value) != 5.0f || label != 148 || fade != element.get(44))
                    throw std::runtime_error("Native score value, label or fade time changed");
                if (fade != prior_fade) { ++score_changes; prior_fade = fade; }
            }
        }
        if (score_changes < 4) throw std::runtime_error("Recorded score popup updates were lost");
        std::cout << "PASS: " << world.hud_frames.size() << " HUD frames, recorded health and "
                  << score_changes << " +5 popup updates\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
