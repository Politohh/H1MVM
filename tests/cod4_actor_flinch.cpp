#include "component/demo/cod4_actor_translation.hpp"
#include <iostream>
#include <sstream>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main(int argc, char** argv)
{
    try
    {
        check(argc >= 3, "Pass native animation codecs and at least one demo");
        std::ifstream table(argv[1]);
        std::map<std::string, unsigned> names;
        cod4::actor_animation_codec codec;
        unsigned index; int leg, torso;
        for (std::string name; table >> index >> leg >> torso >> name;)
        {
            check(index == codec.legs.size(), "Invalid native codec reference");
            codec.legs.push_back(static_cast<std::int16_t>(leg));
            codec.torso.push_back(static_cast<std::int16_t>(torso));
            names.emplace(name, index);
        }
        check(codec.legs.size() > 1000, "Missing native animation catalog");
        std::map<std::string_view, unsigned> catalog;
        for (const auto& [name, id] : names) catalog.emplace(name, id);
        std::map<std::string, unsigned> failures;
        unsigned recorded_flinches{};
        for (int fixture = 2; fixture < argc; ++fixture)
        {
            std::ifstream file(argv[fixture], std::ios::binary);
            check(static_cast<bool>(file), "Cannot read demo fixture");
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
            const auto camera = cod4::decode(bytes);
            const auto world = cod4::decode_world(bytes);
            std::vector<std::string> weapons{""};
            std::istringstream list(camera.configstrings.at(2258));
            for (std::string name; list >> name;) weapons.push_back(name);
            unsigned actors{}, flinches{};
            for (const auto& frame : world.frames)
                for (const auto& [number, entity] : frame.entities)
                {
                    if (entity.get(4) != 1 && entity.get(4) != 2) continue;
                    ++actors;
                    for (unsigned offset : {204u, 208u})
                        flinches += cod4::source_body_animations.at(entity.get(offset) & 511u).starts_with("pt_flinch_");
                    try
                    {
                        const auto packed = cod4::actor_animation(entity.get(204), entity.get(208),
                            weapons.at(entity.get(196)), catalog, codec);
                        check(((packed >> 11) & 2048u) == ((entity.get(204) & 512u) << 2), "Lost leg restart");
                        auto torso_restart = entity.get(208) & 512u;
                        const auto legs = cod4::resolve_actor_animation(entity.get(204), weapons.at(entity.get(196)), catalog) & 2047u;
                        if (!(entity.get(208) & 511u) && codec.legs.at(legs) < 0 && codec.torso.at(legs) >= 0)
                            torso_restart = entity.get(204) & 512u;
                        check(((packed >> 1) & 512u) == torso_restart, "Lost effective torso restart");
                    }
                    catch (const std::exception& error) { ++failures[error.what()]; }
                }
            recorded_flinches += flinches;
            std::cout << "FIXTURE " << std::filesystem::path(argv[fixture]).filename().string()
                << " map=" << camera.map << " snapshots=" << world.frames.size()
                << " actor_states=" << actors << " flinch_states=" << flinches << '\n';
            if (camera.map == "mp_strike")
                std::cout << "SOURCE_INFO " << camera.configstrings.at(0) << '\n';
        }
        for (const auto& [error, count] : failures) std::cout << "MISSING " << count << ' ' << error << '\n';
        check(failures.empty(), "Recorded actor animation mappings failed");
        check(recorded_flinches > 0, "New fixture does not exercise flinch reactions");
        unsigned checked{};
        for (unsigned id = 1; id < cod4::source_body_animations.size(); ++id)
        {
            const auto name = cod4::source_body_animations[id];
            if (!name.starts_with("pt_flinch_")) continue;
            const auto expected = name.starts_with("pt_flinch_grenade_") ? "h1_mp_hold_flinch_a"
                : name.starts_with("pt_flinch_pistol_") ? "mp_flinch_pistol_stand_a" : "mp_flinch_stand_a";
            for (unsigned restart : {0u, 512u})
            {
                check(cod4::actor_animation_name(id | restart, "ak47_mp", catalog) == expected,
                    "Wrong native flinch weapon pose");
                const auto resolved = cod4::resolve_actor_animation(id | restart, "ak47_mp", catalog);
                check(codec.legs.at(resolved & 2047u) < 0 && codec.torso.at(resolved & 2047u) >= 0,
                    "Flinch did not resolve to a native torso channel");
                check((resolved & 2048u) == (restart << 2), "Flinch restart bit was lost");
                // Root legs carry no reaction; the source torso and its
                // restart must survive native packing independently.
                const auto packed = cod4::actor_animation(0, id | restart, "ak47_mp", catalog, codec);
                check(((packed >> 1) & 511u) == unsigned(codec.torso.at(names.at(expected))), "Wrong packed torso clip");
                check(((packed >> 1) & 512u) == restart, "Wrong packed torso restart");
                ++checked;
            }
        }
        check(checked == 24, "Not all twelve directional source flinches were covered");
        auto exact = catalog;
        exact.emplace("pt_flinch_grenade_left", 123);
        check(cod4::actor_animation_name(255, "ak47_mp", exact) == "pt_flinch_grenade_left", "Exact native names lost priority");
        std::cout << "PASS: all recorded actor clips resolve; 12 source flinches use native pose-compatible torso clips and preserve restarts\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
