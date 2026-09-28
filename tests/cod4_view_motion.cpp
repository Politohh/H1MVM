#include "component/demo/cod4_view_translation.hpp"
#include "component/demo/cod4_body_translation.hpp"
#include <iostream>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<typename F> void rejects(F&& call)
{
    try { call(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Invalid view state was accepted");
}
void set(cod4::player_frame& p, std::string_view name, std::uint32_t value)
{
    for (std::size_t i = 0; i < cod4::player_fields.size(); ++i)
        if (cod4::player_fields[i].name == name) { p.values[i] = value; return; }
    throw std::runtime_error("Unknown test field");
}
using state = std::array<std::uint8_t, 18576>;
state translate(const cod4::player_frame& p, int time, std::uint8_t shock)
{
    state result{};
    cod4::write_view_motion(p, time, shock, [&](unsigned offset, const auto& value)
    { std::memcpy(result.data() + offset, &value, sizeof(value)); });
    return result;
}
template<typename T> T get(const state& s, unsigned offset)
{ T value; std::memcpy(&value, s.data() + offset, sizeof(value)); return value; }

int main(int argc, char** argv)
{
    try
    {
        for (int bob = 0; bob <= 255; ++bob)
            check(std::abs(bob / 255.0 - cod4::translate_bob_cycle(bob) / 65535.0) < 1e-12,
                "Native walking/bob phase changed");
        check(cod4::translate_bob_cycle(255) == 65535 && cod4::translate_bob_cycle(0) == 0, "Bob wrap changed");
        rejects([] { cod4::translate_bob_cycle(-1); }); rejects([] { cod4::translate_bob_cycle(256); });
        cod4::player_frame p;
        const std::map<unsigned, std::string> native_profiles{{2, "flashbang_mp"}, {6, "concussion_grenade_mp"}};
        set(p, "shellshockIndex", 15); set(p, "shellshockTime", 62650); set(p, "shellshockDuration", 3163);
        set(p, "damageEvent", 255); set(p, "damageYaw", 255); set(p, "damagePitch", 128); set(p, "damageCount", 127);
        set(p, "holdBreathScale", std::bit_cast<unsigned>(1.0f)); set(p, "speed", 190); set(p, "gravity", 800);
        const auto catalog = cod4::resolve_shock_profiles({p}, {{1969, "concussion_grenade_mp"}}, native_profiles);
        const auto slot = catalog.at(15).index;
        check(slot == 6, "Native shellshock registry ordinal was not preserved");
        const auto first = translate(p, 63000, slot), paused = translate(p, 63000, slot), later = translate(p, 64000, slot);
        check(first == paused && get<int>(later, 0x5EC) - get<int>(first, 0x5EC) == 1000, "Idle clock fails pause or playback");
        check(get<int>(translate(p, 62000, slot), 0x5EC) == 62000, "Idle clock fails rewind");
        check(get<int>(first, 0x48) == 62650 && get<std::uint16_t>(first, 0x38) == 3163
            && get<std::uint16_t>(first, 0x3A) == 3163, "Recorded effect timing changed");
        check(get<std::uint8_t>(first, 5) == 255 && get<std::uint8_t>(first, 8) == 127, "Damage kick inputs missing");
        check(get<float>(first, 0x1CC) == 1 && get<std::int16_t>(first, 0x36) == 190, "Native motion baseline missing");
        set(p, "pm_flags", 1); check(get<unsigned>(translate(p, 63000, slot), 0x58) == 8, "Prone idle scale uses wrong flag");
        set(p, "pm_flags", 2); check(get<unsigned>(translate(p, 63000, slot), 0x58) == 4, "Crouch idle scale uses wrong flag");
        check(cod4::translate_movement_flags(0x4000) == 0x8000 && cod4::translate_movement_flags(0x8000) == 0x4000,
            "Sprint and stun flags confused");
        rejects([&] { translate(p, 63000, 0); });
        rejects([&] { cod4::resolve_shock_profiles({p}, {{1969, "../default"}}, native_profiles); });
        rejects([&] { cod4::resolve_shock_profiles({p}, {{1969, "concussion_grenade_mp"}}, {}); });
        rejects([&] { cod4::resolve_shock_profiles({p}, {{1969, "concussion_grenade_mp"}}, {{16, "concussion_grenade_mp"}}); });
        rejects([&] { cod4::resolve_shock_profiles({p}, {{1969, "concussion_grenade_mp"}},
            {{2, "concussion_grenade_mp"}, {3, "concussion_grenade_mp"}}); });
        set(p, "shellshockIndex", 1);
        check(cod4::resolve_shock_profiles({p}, {{1955, "flashbang"}}, native_profiles).at(1).index == 2,
            "Flash profile reused the source ordinal");
        const auto ordinary = cod4::complete_shock_registry({p}, {{1955, "flashbang"}},
            {{1, "occupied"}}, false, [](const auto& name) { return name == "flashbang_mp"; });
        check(ordinary.at(1) == "occupied" && ordinary.at(2) == "flashbang_mp", "Native shock slot was overwritten");
        check(cod4::complete_shock_registry({p}, {{1955, "flashbang"}}, ordinary, false,
            [](const auto&) { return false; }) == ordinary, "Existing native shock slot was not reused");
        rejects([&] { cod4::complete_shock_registry({p}, {{1955, "flashbang"}}, {}, true,
            [](const auto&) { return true; }); });
        rejects([&] { cod4::complete_shock_registry({p}, {{1955, "flashbang"}}, {}, false,
            [](const auto&) { return false; }); });
        std::map<unsigned, std::string> full;
        for (unsigned slot = 1; slot < 16; ++slot) full.emplace(slot, "occupied" + std::to_string(slot));
        rejects([&] { cod4::complete_shock_registry({p}, {{1955, "flashbang"}}, full, false,
            [](const auto&) { return true; }); });
        set(p, "shellshockIndex", 0);
        const auto none = translate(p, 63000, 0);
        check(!get<unsigned>(none, 0x48) && !get<unsigned>(none, 0x38), "Effect leaks into unaffected state");
        set(p, "holdBreathScale", 0x7F800000); rejects([&] { translate(p, 63000, 0); });
        check(argc >= 3, "Pass at least two source demos");
        unsigned effected = 0, archive_count = 0;
        for (int i = 1; i < argc; ++i)
        {
            std::ifstream in(argv[i], std::ios::binary);
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
            const auto camera = cod4::decode(bytes);
            const auto players = cod4::decode_players(bytes);
            const auto profiles = cod4::resolve_shock_profiles(players, camera.configstrings, native_profiles);
            const auto dynamic_registry = cod4::complete_shock_registry(players, camera.configstrings, {}, false,
                [](const auto& name) { return name == "flashbang_mp" || name == "concussion_grenade_mp"; });
            const auto dynamic_profiles = cod4::resolve_shock_profiles(players, camera.configstrings, dynamic_registry);
            check(dynamic_profiles.size() == profiles.size(), "Fixture effect lost in ordinary configstring registry");
            // Observed City map registry: original concussion retains its name,
            // and original flashbang maps to MWR's multiplayer profile at slot 4.
            const auto observed_profiles = cod4::resolve_shock_profiles(players, camera.configstrings,
                {{1, "concussion_grenade_mp"}, {2, "damage_mp"}, {3, "default"}, {4, "flashbang_mp"},
                 {5, "frag_grenade_mp"}, {6, "pain"}, {7, "stun_grenade_mp"}, {8, "underwater"}});
            for (const auto& [source, profile] : observed_profiles)
                check(profile.index == (profile.name == "flashbang_mp" ? 4 : 1), "Observed H1 effect slot changed");
            for (const auto& frame : camera.frames) { cod4::translate_bob_cycle(frame.bob_cycle); ++archive_count; }
            for (const auto& player : players)
            {
                const auto source = player.integer("shellshockIndex");
                const auto native = translate(player, player.integer("commandTime"), source ? profiles.at(source).index : 0);
                check(get<float>(native, 0x1CC) == player.real("holdBreathScale"), "Recorded breathing scale changed");
                if (source) { ++effected; check(get<unsigned>(native, 0x48) == player.integer("shellshockTime"), "Fixture effect clock changed"); }
            }
        }
        check(effected > 0, "Fixture lacks the recorded stun case");
        std::cout << "PASS: 256 bob phases; idle pause/rewind; stance/damage/effect bounds; " << archive_count
            << " fixture archives; " << effected << " effected source snapshots\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
