#include "component/demo/cod4_actor_translation.hpp"
#include <iostream>
#include <set>

void check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv)
{
    try
    {
        check(argc == 3, "Pass native actor codec fixture and output TSV");
        std::ifstream input(argv[1]);
        std::map<std::string, unsigned> names;
        cod4::actor_animation_codec codec;
        unsigned index; int leg, torso;
        for (std::string name; input >> index >> leg >> torso >> name;)
        {
            check(index == codec.legs.size(), "Native catalog index mismatch");
            codec.legs.push_back(static_cast<std::int16_t>(leg));
            codec.torso.push_back(static_cast<std::int16_t>(torso));
            names.emplace(name, index);
        }
        check(codec.legs.size() == 1709, "Missing verified native catalog");
        std::map<std::string_view, unsigned> catalog;
        for (const auto& [name, id] : names) catalog.emplace(name, id);
        constexpr std::array<std::string_view, 8> weapons{
            "m4_mp", "remington700_mp", "m60e4_mp", "mp5_mp", "winchester1200_mp", "beretta_mp", "rpg_mp", "frag_grenade_mp"};
        std::ofstream report(argv[2]);
        check(report.good(), "Cannot create coverage report");
        report << "source_index\tsource_name\tweapon_family\tnative_name\tmatch\tnative_index\tleg_channel\ttorso_channel\n";
        unsigned clips{}, checks{}, unused{};
        std::set<std::string_view> aliases;
        for (unsigned id = 1; id < cod4::source_body_animations.size(); ++id)
        {
            const auto source = cod4::source_body_animations[id];
            if (source == "unused")
            {
                bool rejected = false;
                try { cod4::resolve_actor_animation(id, weapons[0], catalog); }
                catch (const std::runtime_error&) { rejected = true; }
                check(rejected, "Unused source slot silently accepted");
                ++unused; continue;
            }
            ++clips;
            for (auto weapon : weapons)
            {
                const auto target = cod4::actor_animation_name(id, weapon, catalog);
                const auto resolved = cod4::resolve_actor_animation(id, weapon, catalog);
                const bool upper = source.starts_with("pt_");
                const bool transferred = !upper && codec.legs.at(resolved) < 0;
                if (source != target) aliases.insert(source);
                report << id << '\t' << source << '\t' << static_cast<unsigned>(cod4::map_weapon(weapon).body)
                    << '\t' << target << '\t' << (source == target ? "exact" : "native replacement") << '\t'
                    << resolved << '\t' << codec.legs.at(resolved) << '\t' << codec.torso.at(resolved) << '\n';
                for (unsigned restart : {0u, 512u})
                {
                    const auto packed = upper ? cod4::actor_animation(0, id | restart, weapon, catalog, codec)
                        : cod4::actor_animation(id | restart, 0, weapon, catalog, codec);
                    const auto expected = unsigned((upper || transferred ? codec.torso : codec.legs).at(resolved));
                    check((upper || transferred ? (packed >> 1) & 511u : (packed >> 11) & 2047u) == expected,
                        "Wrong native body channel for " + std::string(source));
                    check(upper || transferred ? ((packed >> 1) & 512u) == restart : ((packed >> 11) & 2048u) == (restart << 2),
                        "Lost restart for " + std::string(source));
                    if (transferred)
                    {
                        // A recorded full-body pain/throw clip must keep its
                        // stance when MWR supplies only an upper-body reaction.
                        const auto lower = (packed >> 11) & 2047u;
                        if (source.starts_with("pb_prone_pain"))
                        {
                            const auto base = weapon == "beretta_mp" ? "mp_pistol_prone_idle" : weapon == "rpg_mp" ? "mp_rpg_prone_idle"
                                : weapon == "frag_grenade_mp" ? "pb_prone_hold" : "mp_prone_idle_iw6";
                            check(lower == unsigned(codec.legs.at(names.at(base))), "Prone pain stood up");
                        }
                        if (source == "pb_crouch_pain_holdstomach")
                        {
                            const auto base = weapon == "beretta_mp" ? "mp_pistol_crouch_idle" : weapon == "rpg_mp" ? "mp_rpg_crouch_idle"
                                : weapon == "frag_grenade_mp" ? "mp_crouch_hold_idle" : "mp_crouch_idle";
                            check(lower == unsigned(codec.legs.at(names.at(base))), "Crouch pain stood up");
                        }
                        const auto explicit_upper = cod4::actor_animation(id | restart, 316, weapon, catalog, codec);
                        check(((explicit_upper >> 1) & 511u) == unsigned(codec.torso.at(names.at("pt_stand_shoot"))),
                            "Full-body replacement overwrote recorded torso animation");
                    }
                    ++checks;
                }
            }
        }
        check(clips == 247 && unused == 70, "Source animation table coverage changed");
        for (unsigned invalid : {318u, 511u, 1024u})
        {
            bool rejected = false;
            try { cod4::resolve_actor_animation(invalid, weapons[0], catalog); }
            catch (const std::runtime_error&) { rejected = true; }
            check(rejected, "Unknown animation ID silently accepted");
        }
        auto exact = catalog; exact.emplace("pb_hold_idle", names.at("mp_hold_idle"));
        check(cod4::actor_animation_name(168, "frag_grenade_mp", exact) == "pb_hold_idle", "Exact match no longer preferred");
        check(cod4::resolve_actor_animation(168, "", catalog) == names.at("mp_hold_idle"), "Unarmed hold idle missing");
        std::cout << "PASS all " << clips << " named source clips, " << weapons.size() << " weapon families, " << checks
            << " native channel/restart checks; " << aliases.size() << " clips use explicit native replacements; "
            << unused << " unused slots rejected\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
