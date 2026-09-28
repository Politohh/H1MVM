#include "component/demo/cod4_body_translation.hpp"
#include "component/demo/cod4_world_translation.hpp"
#include <iostream>
#include <set>
#include <sstream>
#include <tuple>

void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<typename T> T native(const cod4::h1_entity& data, unsigned offset)
{ T value; std::memcpy(&value, data.data() + offset, sizeof(value)); return value; }
void near(float value, float expected, const char* message) { check(std::abs(value - expected) < 0.002f, message); }

int main(int argc, char** argv)
{
    try
    {
        check(argc == 3, "Pass source demo and a native H1 animation-name catalog");
        std::ifstream file(argv[1], std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        const auto world = cod4::decode_world(bytes);
        const auto camera = cod4::decode(bytes);
        const auto players = cod4::decode_players(bytes);
        std::vector<std::string> names{""};
        std::istringstream source_names(camera.configstrings.at(2258));
        for (std::string name; source_names >> name;) names.push_back(name);

        std::map<unsigned, std::pair<int, int>> lifetimes;
        std::map<unsigned, unsigned> counts;
        unsigned missile_frames = 0;
        for (const auto& frame : world.frames) for (const auto& [number, entity] : frame.entities)
        {
            if (entity.get(4) != 4) continue;
            ++missile_frames;
            auto& life = lifetimes[number];
            if (!life.first) life.first = frame.time;
            life.second = frame.time;
            ++counts[number];
            check(names.at(entity.get(196)) == (number == 319 ? "frag_grenade_mp" : "concussion_grenade_mp"), "Incorrect recorded projectile identity");
            const unsigned h1_weapon = number == 319 ? 71 : 83; // Arbitrary native registry supplied by the caller.
            const auto missile = cod4::translate_missile(entity, h1_weapon, frame.time);
            check(native<std::uint16_t>(missile, 0) == number && missile[10] == 4
                && native<unsigned>(missile, 76) == h1_weapon, "Missile not encoded with native identity/weapon fields");
            check(native<unsigned>(missile, 168) == entity.get(84), "Projectile launch time lost");
            check(native<std::uint16_t>(missile, 2) == 0, "Projectile owner lost");
            check(native<std::uint16_t>(missile, 4) == (entity.get(124) == 1022 ? 2046 : 2047), "Ground sentinel copied from COD4");
            check(native<unsigned>(missile, 32) == 0, "Source event ordinals copied into H1");
            // Independent reference for this fixture: ballistic flight at 800
            // units/sec^2, or the recorded stationary pose after the last bounce.
            const bool gravity = entity.get(12) == 5;
            const double elapsed = gravity ? (frame.time - static_cast<int>(entity.get(16))) / 1000.0 : 0;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                const double base = std::bit_cast<float>(entity.get(24 + axis * 4));
                const double delta = std::bit_cast<float>(entity.get(36 + axis * 4));
                near(native<float>(missile, 108 + axis * 4), static_cast<float>(base + elapsed * delta
                    - (axis == 2 ? 400 * elapsed * elapsed : 0)), "Converted grenade departs from recorded ballistic path");
                near(native<float>(missile, 120 + axis * 4), static_cast<float>(gravity ? delta - (axis == 2 ? 800 * elapsed : 0) : 0), "Native projectile velocity mismatch");
            }
        }
        check(missile_frames == 138 && lifetimes.size() == 3, "Source projectile framing changed");
        check(lifetimes.at(317) == std::pair{46950,48700} && lifetimes.at(319) == std::pair{48500,51800}
            && lifetimes.at(321) == std::pair{49600,51300}, "Grenade lifetime/removal mismatch");
        const auto bounce = cod4::translate_missile(world.sample(50150).entities.at(319), 71, 50150);
        check(native<unsigned>(bounce, 96) == 2 && native<int>(bounce, 100) == 50150, "Bounce did not start a new native trajectory");
        const auto rest = cod4::translate_missile(world.sample(50800).entities.at(319), 71, 50800);
        check(native<unsigned>(rest, 96) == 0 && native<float>(rest,128) == 0, "Settled grenade still moving");
        const auto prior = cod4::translate_missile(world.sample(46950).entities.at(317), 83, 46950);
        check(!world.sample(48750).entities.contains(317) && !world.sample(51850).entities.contains(319), "Finished grenade reappeared");
        check(cod4::translate_missile(world.sample(46950).entities.at(317), 83, 46950) == prior, "Seek changes grenade state");

        std::ifstream catalog_file(argv[2]);
        std::map<std::string, unsigned> owned_catalog;
        unsigned index;
        for (std::string name; catalog_file >> index >> name;) owned_catalog.emplace(name, index);
        check(owned_catalog.size() > 1000, "Native animation catalog incomplete");
        std::map<std::string_view, unsigned> catalog;
        for (const auto& [name, id] : owned_catalog) catalog[name] = id;
        std::set<std::string_view> used;
        unsigned sprints=0, reloads=0, fires=0, pulls=0, throws=0, restart_changes=0, locomotion=0;
        unsigned prior_torso = 0;
        for (const auto& player : players)
        {
            const auto movement = camera.sample(player.integer("commandTime"));
            const auto action = cod4::select_body_animation(player, movement, names.at(player.integer("weapon")));
            const auto legs = cod4::resolve_body_animation(action.legs, action.legs_restart, catalog);
            const auto torso = cod4::resolve_body_animation(action.torso, action.torso_restart, catalog);
            check((legs & ~2048u) != 0 && (legs & ~2048u) < 2048, "Body left without a native leg pose");
            used.insert(action.legs);
            if (action.legs.find("_run_") != std::string_view::npos || action.legs.find("combatrun_") != std::string_view::npos
                || action.legs.find("_walk_") != std::string_view::npos) ++locomotion;
            if (!action.torso.empty()) used.insert(action.torso);
            if ((torso & ~2048u) == (prior_torso & ~2048u) && torso != prior_torso) ++restart_changes;
            prior_torso = torso;
            if (player.integer("pm_flags") & 0x8000u)
            {
                ++sprints;
                check(action.legs == "pb_sprint_sniper" || action.legs == "mp_pistol_sprint", "Sprint does not select MWR sprint animation");
                check(cod4::translate_movement_flags(player.integer("pm_flags")) & 0x4000u, "Sprint flag copied into wrong native bit");
            }
            const auto state = player.integer("weaponstate");
            if (state == 5 || state == 6) { ++fires; check(action.torso.starts_with("pt_"), "Firing/rechamber torso pose missing"); }
            if (state >= 7 && state <= 11) { ++reloads; check(action.torso.find("reload") != std::string_view::npos, "Reload torso missing"); }
            if (state >= 15 && state <= 17) { ++pulls; check(action.torso.ends_with("grenade_pull"), "Grenade prepare/hold pose missing"); }
            if (state >= 18 && state <= 20) { ++throws; check(action.torso.ends_with("grenade_throw"), "Grenade release pose missing"); }
            // Reorder the destination registry to prove no recorded animation
            // ordinal or hardcoded native ordinal is being used as an asset ID.
            auto shifted = catalog;
            shifted[action.legs] += 17;
            check((cod4::resolve_body_animation(action.legs, action.legs_restart, shifted) & ~2048u)
                == (legs & ~2048u) + 17, "Body mapping assumes fixed animation indices");
        }
        check(sprints == 73 && reloads && fires && pulls && throws && restart_changes && locomotion, "Fixture body actions not covered");
        for (const auto& movement : camera.frames)
        {
            const int packed = cod4::translate_body_heading(movement);
            check(packed >= 0 && packed <= 255, "Body heading outside H1 byte domain");
            const float heading = movement.angles[1] + static_cast<float>(movement.movement_dir);
            const float error = std::remainder(packed * (360.0f / 256.0f) - heading, 360.0f);
            check(std::abs(error) <= 0.704f, "Body heading no longer matches recorded facing");
            const auto next = std::upper_bound(players.begin(), players.end(), movement.time,
                [](int t, const cod4::player_frame& f) { return t < static_cast<int>(f.integer("commandTime")); });
            const auto& player = next == players.begin() ? players.front() : *(next - 1);
            const auto action = cod4::select_body_animation(player, movement, names.at(player.integer("weapon")));
            cod4::resolve_body_animation(action.legs, action.legs_restart, catalog);
            cod4::resolve_body_animation(action.torso, action.torso_restart, catalog);
            used.insert(action.legs);
            if (!action.torso.empty()) used.insert(action.torso);
        }
        // A server snapshot can omit velocity entirely. Locomotion still comes
        // from the demo's local archive; an idle PS must not freeze the body.
        auto omitted = players.front();
        omitted.values[16] = 0; // pm_flags
        auto movement = camera.frames.front();
        movement.velocity = {200, 0, 0}; movement.angles = {0, 0, 0};
        check(cod4::select_body_animation(omitted, movement, "deserteagle_mp").legs == "mp_pistol_stand_run_f",
            "Omitted server velocity froze recorded locomotion");
        for (const auto& [view_yaw, relative, expected] : std::array<std::tuple<float, int, int>, 6>{{
            {90.0f, 0, 64}, {5.0f, -45, 228}, {359.0f, 10, 6},
            {-1.0f, 0, 255}, {180.0f, -90, 64}, {147.18933105f, 17, 117}}})
        {
            movement.angles[1] = view_yaw; movement.movement_dir = relative;
            check(cod4::translate_body_heading(movement) == expected,
                "Relative COD4 facing not converted to absolute packed H1 yaw");
        }
        // Velocity can point backward while the player's body faces forward.
        movement.angles[1] = 90; movement.movement_dir = 0; movement.velocity = {0, -200, 0};
        check(cod4::translate_body_heading(movement) == 64, "Backward movement turned the body away from recorded facing");
        check(cod4::translate_movement_flags(0x8000) == 0x4000 && cod4::translate_movement_flags(0x50) == 0x50, "Native movement flag translation incorrect");
        bool rejected = false;
        try { cod4::resolve_body_animation("missing_mwr_clip", 0, catalog); } catch (const std::exception&) { rejected=true; }
        check(rejected, "Missing MWR animation accepted");
        rejected = false;
        auto corrupt = world.sample(46950).entities.at(317);
        corrupt.set(24, 0x7F800000);
        try { cod4::translate_missile(corrupt, 83, 46950); } catch (const std::exception&) { rejected=true; }
        check(rejected, "Nonfinite grenade position accepted");
        std::cout << "PASS: 3 grenade lifetimes / 138 states, COD4 flight and bounces, native weapon/owner/ground encoding, seek/removal, "
            << players.size() << " player frames / " << camera.frames.size() << " native body headings and archive poses, " << used.size() << " resolved MWR clips (sprint=" << sprints
            << ",fire=" << fires << ",reload=" << reloads << ",pull=" << pulls << ",throw=" << throws << ")\n";
        for (const auto name : used) std::cout << catalog.at(name) << ' ' << name << '\n';
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
