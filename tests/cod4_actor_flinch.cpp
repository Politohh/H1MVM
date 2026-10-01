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
        // The long Diablo recording exercises grenade crouch strafes that
        // the short Strike fixture does not. Preserve direction and restarts.
        const std::array<unsigned, 4> crouch_sources{128, 120, 119, 118};
        const std::array<std::string_view, 4> crouch_targets{
            "mp_crouch_hold_run", "mp_crouch_hold_run_b", "mp_crouch_hold_run_l", "mp_crouch_hold_run_r"};
        for (unsigned i = 0; i < crouch_sources.size(); ++i)
            for (unsigned restart : {0u, 512u})
            {
                check(cod4::actor_animation_name(crouch_sources[i] | restart, "frag_grenade_mp", catalog) == crouch_targets[i],
                    "Grenade crouch movement lost its direction");
                const auto resolved = cod4::resolve_actor_animation(crouch_sources[i] | restart, "frag_grenade_mp", catalog);
                check(codec.legs.at(resolved & 2047u) >= 0, "Grenade crouch movement needs a native leg channel");
                check((resolved & 2048u) == (restart << 2), "Grenade crouch restart was lost");
            }
        // Validate both native channels and restart bits for the additional
        // Diablo poses, including all four grenade crawl directions.
        const std::array<std::pair<unsigned, std::string_view>, 8> diablo_clips{{
            {72, "pb_prone_hold"}, {78, "pb_prone_crawl_hold"},
            {69, "pb_prone_crawl_back_hold"}, {71, "pb_prone_crawl_left_hold"},
            {70, "pb_prone_crawl_right_hold"}, {303, "h1_mp_prone_hold_grenade_throw"},
            {283, "mp_pistol_crouch_reload"}, {173, "mp_pistol_crouch_idle"}
        }};
        for (const auto& [source, target] : diablo_clips)
            for (unsigned restart : {0u, 512u})
            {
                const auto weapon = source == 283 || source == 173 ? "deserteagle_mp" : "frag_grenade_mp";
                const auto resolved = cod4::resolve_actor_animation(source | restart, weapon, catalog);
                check(resolved == (names.at(std::string(target)) | (restart << 2)), "Wrong Diablo clip or restart");
                const bool upper = source == 303 || source == 283;
                const auto packed = upper
                    ? cod4::actor_animation(source == 303 ? 72 : 173, source | restart, weapon, catalog, codec)
                    : cod4::actor_animation(source | restart, 0, weapon, catalog, codec);
                const auto encoded = upper ? ((packed >> 1) & 511u) : ((packed >> 11) & 2047u);
                check(encoded == unsigned((upper ? codec.torso : codec.legs).at(resolved & 2047u)), "Wrong Diablo native channel");
                check(upper ? ((packed >> 1) & 512u) == restart : ((packed >> 11) & 2048u) == (restart << 2),
                    "Lost Diablo packed restart");
            }
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
