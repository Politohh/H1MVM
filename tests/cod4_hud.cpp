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
        for (unsigned team : {0u, 1u, 2u, 3u})
        {
            const auto stored = cod4::native_team_selection(team, -1);
            if (static_cast<std::int64_t>(stored) - 1 != team)
                throw std::runtime_error("Native integer omnvar lost recorded team enum");
        }
        for (unsigned team : {0u, 1u, 2u, 3u})
        {
            auto client = recorded_client->second;
            client.set(4, team);
            const auto native = cod4::translate_client(client, 1, {});
            unsigned native_team{}, agent_team_slot{}, costume{};
            std::memcpy(&native_team, native.data() + 8, 4);
            std::memcpy(&agent_team_slot, native.data() + 76, 4);
            std::memcpy(&costume, native.data() + 96, 4);
            if (native_team != team || agent_team_slot != 0 || costume != 0)
                throw std::runtime_error("Team membership corrupted native character/costume data");
        }
        const std::array<std::string_view, 7> offhand_names{"", "ak47_mp", "frag_grenade_mp",
            "flash_grenade_mp", "smoke_grenade_mp", "concussion_grenade_mp", "c4_mp"};
        const auto name = [&](unsigned weapon) { return offhand_names.at(weapon); };
        if (cod4::select_hud_offhands(std::vector<unsigned>{1, 2, 3}, 1, name) != std::pair{2u, 3u}
            || cod4::select_hud_offhands(std::vector<unsigned>{1, 4, 2}, 0, name) != std::pair{2u, 4u}
            || cod4::select_hud_offhands(std::vector<unsigned>{5, 2, 1}, 0, name) != std::pair{2u, 5u}
            || cod4::select_hud_offhands(std::vector<unsigned>{1, 6}, 1, name) != std::pair{0u, 0u}
            || cod4::select_hud_offhands(std::vector<unsigned>{2, 3, 4}, 0, name) != std::pair{2u, 4u})
            throw std::runtime_error("Native HUD offhand selection lost recorded inventory/secondary class");
        if (cod4::native_faction(cod4::character_family::usmc_desert) != "marines"
            || cod4::native_faction(cod4::character_family::arab_desert) != "opfor"
            || cod4::native_faction(cod4::character_family::sas_woodland) != "sas"
            || cod4::native_faction(cod4::character_family::spetsnaz_urban) != "russian")
            throw std::runtime_error("Recorded character faction mismatch");
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
        const std::array<int, 5> expected{11071750, 11073700, 11075350, 11082650, 11083950};
        if (popups.size() != expected.size()) throw std::runtime_error("Unexpected Strike popup count");
        for (std::size_t i = 0; i < expected.size(); ++i)
            if (popups[i].time != expected[i] || popups[i].element.get(44) != static_cast<unsigned>(expected[i]))
                throw std::runtime_error("Score popup does not start at its recorded kill");
        std::map<int, std::string> configs{{8, camera.configstrings.at(12)}};
        cod4::translate_minimap_configstrings(camera.configstrings, configs);
        if (configs.at(1228) != camera.configstrings.at(823)
            || configs.at(8) != camera.configstrings.at(12) || configs.at(1227) != "0")
            throw std::runtime_error("Native compass material/bounds/north mapping changed");
        const auto obituaries = cod4::collect_obituary_events(world);
        if (obituaries.size() != 18 || obituaries[7].time != expected[0] - 50
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
            || packed != 27u)
            throw std::runtime_error("H1 obituary event payload changed");
        std::cout << "PASS: " << world.hud_frames.size() << " HUD frames, recorded health and "
                  << score_changes << " +5 popup updates, " << obituaries.size() << " obituaries\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
