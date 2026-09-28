#include "component/demo/cod4_body_translation.hpp"
#include "component/demo/cod4_world_translation.hpp"
#include "component/demo/cod4_action_translation.hpp"
#include <iostream>
#include <set>
#include <sstream>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void set(cod4::player_frame& player, std::string_view field, unsigned value)
{
    for (unsigned i = 0; i < cod4::player_fields.size(); ++i)
        if (cod4::player_fields[i].name == field) { player.values[i] = value; return; }
    throw std::runtime_error("Unknown test field");
}

int main(int argc, char** argv)
{
    try
    {
        check(argc == 3, "Pass native animation catalog and Backlot source demo");
        std::ifstream input(argv[1]);
        std::map<std::string, unsigned> owned;
        unsigned index;
        for (std::string name; input >> index >> name;) owned.emplace(name, index);
        std::map<std::string_view, unsigned> native;
        for (const auto& [name, id] : owned) native[name] = id;
        check(native.size() > 1000, "Incomplete native animation catalog");
        std::set<std::string_view> sources, clips;
        unsigned poses = 0;
        for (const auto& mapping : cod4::weapon_mappings)
        {
            check(sources.insert(mapping.source).second, "Duplicate source identity");
            check(!mapping.base.empty(), "Missing remastered identity");
            if (mapping.alternate) check(mapping.attachment == "gl", "Underbarrel lost its attachment");
            for (const auto stance : {0u, 1u, 2u, 0x8000u, 0x40u})
                for (const auto state : {0u, 5u, 6u, 7u, 11u, 12u, 15u, 18u})
                    for (const auto direction : {0u, 1u, 2u, 3u, 4u, 5u})
                    {
                        cod4::player_frame player;
                        set(player, "pm_flags", stance); set(player, "weaponstate", state);
                        set(player, "groundEntityNum", direction == 5 ? 1023 : 1022);
                        set(player, "legsAnim", 512); set(player, "torsoAnim", 512);
                        cod4::camera_frame movement;
                        movement.velocity = direction == 0 ? std::array<float,3>{200,0,0}
                            : direction == 1 ? std::array<float,3>{-200,0,0}
                            : direction == 2 ? std::array<float,3>{0,200,0}
                            : direction == 3 ? std::array<float,3>{0,-200,0}
                            : direction == 5 ? std::array<float,3>{200,0,150} : std::array<float,3>{};
                        const auto action = cod4::select_body_animation(player, movement, mapping.source);
                        const auto legs = cod4::resolve_body_animation(action.legs, action.legs_restart, native);
                        cod4::resolve_body_animation(action.torso, action.torso_restart, native);
                        check(legs & 2048, "Restart bit lost");
                        clips.insert(action.legs); if (!action.torso.empty()) clips.insert(action.torso);
                        if (stance == 0x8000 && direction != 5)
                        {
                            check(action.legs.find("sprint") != action.legs.npos, "Sprint family missing");
                            if (mapping.body == cod4::body_family::rifle) check(action.legs == "pb_sprint_assault", "Rifle using another weapon family's sprint");
                            if (mapping.body == cod4::body_family::smg) check(action.legs == "pb_sprint_smg", "SMG sprint family incorrect");
                        }
                        ++poses;
                    }
        }
        check(cod4::map_weapon("ak47_mp").base == "h1_ak47_mp", "AK47 identity wrong");
        check(cod4::map_weapon("m14_mp").body == cod4::body_family::rifle, "M14 must follow native MWR class");
        check(cod4::map_weapon("deserteaglegold_mp").base == "h1_deserteagle55_mp", "Golden Deagle identity wrong");
        check(cod4::map_weapon("gl_ak47_mp").base == cod4::map_weapon("ak47_gl_mp").base
            && cod4::map_weapon("gl_ak47_mp").alternate, "Primary and alternate launcher identity mismatch");
        check(cod4::native_weapon_flags(2, true) == 0x4002 && cod4::native_weapon_flags(0x4000, false) == 0, "Source alternate flags copied into H1");
        cod4::entity_state launcher;
        launcher.set(0, 100); launcher.set(4, 4); launcher.set(116, 0); launcher.set(124, 1023);
        check(cod4::translate_missile(launcher, 50, 1000, true)[6] == 1
            && cod4::translate_missile(launcher, 50, 1000)[6] == 0, "Underbarrel projectile lost native alternate mode");
        bool rejected = false;
        try { cod4::map_weapon("made_up_mod_gun_mp"); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Custom gun silently substituted");

        // A differently ordered native skeleton proves that vehicle variants
        // use names rather than carrying COD4's hidden-part ordinals into MWR.
        for (const auto& profile : cod4::vehicle_bone_profiles)
        {
            std::vector<std::string_view> reordered(profile.bones.rbegin(), profile.bones.rend());
            const auto mapping = cod4::map_parts(profile.bones, reordered);
            for (unsigned i = 0; i < profile.bones.size(); ++i)
            {
                cod4::source_part_bits source{};
                source[i / 32] |= 0x80000000u >> (i % 32);
                const auto target = cod4::translate_parts(source, &mapping);
                const unsigned destination = static_cast<unsigned>(profile.bones.size()) - i - 1;
                for (unsigned word = 0; word < target.size(); ++word)
                    check(target[word] == (word == destination/32 ? 0x80000000u >> (destination%32) : 0), "Vehicle part name translation wrong");
            }
        }
        std::ifstream demo(argv[2], std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(demo)), {});
        const auto camera = cod4::decode(bytes);
        const auto players = cod4::decode_players(bytes);
        const auto world = cod4::decode_world(bytes);
        check(camera.map == "mp_backlot", "Backlot fixture identity changed");
        std::vector<std::string> names{""};
        std::istringstream strings(camera.configstrings.at(2258));
        for (std::string name; strings >> name;) names.push_back(name);
        const auto ammo_slots = cod4::resolve_ammo_slots(names);
        check(ammo_slots.at(18).reserve == 6 && ammo_slots.at(18).clip == 17,
            "Backlot turret registration shifted AK47 ammo");
        check(ammo_slots.at(12).reserve == 9 && ammo_slots.at(12).clip == 11,
            "Backlot turret registration shifted Deagle ammo");
        check(players.front().clips.at(ammo_slots.at(18).clip) == 30
            && players.front().clips.at(ammo_slots.at(12).clip) == 7, "Backlot initial magazines lost");
        const auto alias_slots = cod4::resolve_ammo_slots({"", "defaultweapon_mp", "saw_bipod_crouch_mp",
            "frag_grenade_mp", "frag_grenade_short_mp", "deserteagle_mp"});
        check(alias_slots[2].reserve == 0 && alias_slots[2].clip == 0
            && alias_slots[3].clip == 2 && alias_slots[4].clip == 2 && alias_slots[5].clip == 3,
            "Empty or shared clip names consume a new registration slot");
        std::set<std::string> used;
        for (const auto& movement : camera.frames)
        {
            const auto next = std::upper_bound(players.begin(), players.end(), movement.time,
                [](int time, const cod4::player_frame& frame) { return time < static_cast<int>(frame.integer("commandTime")); });
            const auto& player = next == players.begin() ? players.front() : *(next - 1);
            const auto& name = names.at(player.integer("weapon"));
            used.insert(name);
            const auto action = cod4::select_body_animation(player, movement, name);
            cod4::resolve_body_animation(action.legs, action.legs_restart, native);
            cod4::resolve_body_animation(action.torso, action.torso_restart, native);
        }
        for (const auto& frame : world.frames) for (const auto& [number, entity] : frame.entities)
        {
            if (entity.get(4) == 4) cod4::map_weapon(names.at(entity.get(196)));
            if (entity.get(4) == 6 && entity.get(136))
                check(!cod4::source_bones(camera.configstrings.at(830 + entity.get(136))).empty(), "Backlot vehicle skeleton unavailable");
        }
        check(used.contains("ak47_mp") && used.contains("deserteagle_mp"), "Fixture weapon changes missing");
        std::cout << "PASS: " << sources.size() << " source weapon identities; " << poses << " poses resolve to " << clips.size()
            << " native MWR clips; 37 vehicle skeleton profiles; Backlot " << camera.frames.size() << " archive poses / " << players.size() << " snapshots\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
