#include "component/demo/cod4_grenade_events.hpp"
#include "component/demo/cod4_model_metadata.hpp"
#include "component/demo/cod4_weapon_catalog.hpp"
#include <iostream>
#include <sstream>

void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template <typename F> void rejects(F&& f, const char* message)
{
    bool failed = false;
    try { f(); } catch (const std::exception&) { failed = true; }
    check(failed, message);
}
template<typename T> T native(const cod4::h1_entity& data, unsigned offset)
{ T value; std::memcpy(&value, data.data() + offset, sizeof(value)); return value; }

cod4::entity_state explosion(unsigned sequence, unsigned event = 45)
{
    cod4::entity_state entity;
    entity.set(0, 300); entity.set(4, 0); entity.set(196, 40);
    entity.set(132, 16); entity.set(160, sequence);
    const auto slot = (sequence - 1) & 3;
    entity.set(164 + slot * 4, event); entity.set(180 + slot * 4, 5);
    return entity;
}

int main(int argc, char** argv)
{
    try
    {
        for (const auto weapon : {"claymore_mp", "c4_mp"})
        {
            check(cod4::grenade_weapon_matches(45, weapon), "Planted explosive rejected as a grenade event");
            check(!cod4::grenade_weapon_matches(48, weapon) && !cod4::grenade_weapon_matches(49, weapon),
                "Planted explosive accepted as a flash/custom detonation");
            check(cod4::map_weapon(weapon).base == std::string("h1_") + weapon,
                "Planted explosive substituted another weapon");
        }
        check(!cod4::grenade_weapon_matches(45, "ak47_mp") && !cod4::grenade_weapon_matches(45, "custom_bomb_mp"),
            "Unverified explosive weapon accepted");
        cod4::world_track synthetic;
        synthetic.frames = {{1000,1,{{300,explosion(255,44)}},{}},
            {1050,2,{{300,explosion(0)}},{}}, {1100,3,{{300,explosion(0)}},{}}};
        auto events = cod4::collect_grenade_events(synthetic).events;
        check(events.size() == 1 && events[0].time == 1050, "Wrapping source sequence lost or duplicated a detonation");
        synthetic.frames.push_back({1150,4,{},{}});
        synthetic.frames.push_back({1200,5,{{300,explosion(1,48)}},{}});
        events = cod4::collect_grenade_events(synthetic).events;
        check(events.size() == 2 && events[1].source_event == 48, "Reused entity number suppressed a later event");
        auto temporary = explosion(0);
        temporary.set(4, 17 + 48); temporary.set(156, 5);
        synthetic.frames = {{1000,1,{{300,temporary}},{}}, {1050,2,{{300,temporary}},{}}};
        synthetic.frames[1].entities.at(300).set(24, std::bit_cast<unsigned>(10.0f));
        check(cod4::collect_grenade_events(synthetic).events.size() == 1, "Changed temp-entity pose retriggered its event");
        const auto translated = cod4::translate_grenade_event(events[0], 1500, 71);
        check(translated[10] == 118 && translated[11] == 17 && native<unsigned>(translated,36) == 5
            && native<unsigned>(translated,76) == 71 && native<std::uint16_t>(translated,0) == 1500,
            "Native grenade event layout, direction, surface or weapon changed");
        check(cod4::translate_grenade_event(events[1],1501,83)[10] == 123, "Flash ordinal copied from COD4");
        auto cushion = events[0]; cushion.surface = 26;
        check(cod4::translate_grenade_event(cushion,1502,71)[11] == 52, "Cushion reused COD4 surface ordinal");
        check(cod4::grenade_events_at(events,1049) == 0 && cod4::grenade_events_at(events,1050) == 1
            && cod4::grenade_events_at(events,1050) == 1 && cod4::grenade_events_at(events,1400) == 2
            && cod4::grenade_events_at(events,1049) == 0, "Pause/seek event presence differs from recorded clock");
        check(!cod4::grenade_weapon_matches(45,"rpg_mp") && cod4::grenade_weapon_matches(48,"flash_grenade_mp")
            && cod4::grenade_weapon_matches(49,"concussion_grenade_mp"), "Unverified explosion weapon accepted");
        check(cod4::grenade_effect_available(45,false,true), "Native frag rejected its valid surface FX without optional weapon FX");
        check(!cod4::grenade_effect_available(45,true,false), "Native frag accepted missing surface FX");
        check(cod4::grenade_effect_available(48,true,false) && !cod4::grenade_effect_available(48,false,true),
            "Native flash validation followed the frag surface path");
        check(cod4::grenade_effect_available(49,true,false), "Concussion weapon FX rejected");
        const auto metadata = cod4::parse_model_metadata({"@model:768:body", "@grenade:1500:1050"});
        check(metadata.names.size() == 1 && metadata.grenade_times.at(1500) == 1050, "Grenade metadata became a model request");
        for (const auto entry : {"@grenade:1023:1", "@grenade:2046:1", "@grenade:2047:1", "@grenade:1500:-1", "@grenade:1500:1x", "@grenade:1500:"})
            rejects([&] { cod4::parse_model_metadata({entry}); }, "Malformed grenade metadata accepted");
        rejects([&] { cod4::parse_model_metadata({"@grenade:1500:1", "@grenade:1500:2"}); }, "Duplicate FX clock accepted");
        auto corrupt = events[0]; corrupt.parameter = 255;
        rejects([&] { cod4::translate_grenade_event(corrupt,1500,71); }, "Invalid direction accepted");
        rejects([&] { cod4::translate_grenade_event(events[0],2046,71); }, "Reserved world FX entity accepted");
        rejects([&] { cod4::translate_grenade_event(events[0],2047,71); }, "FX entity overflow accepted");

        for (int arg = 1; arg < argc; ++arg)
        {
            std::ifstream file(argv[arg],std::ios::binary);
            check(static_cast<bool>(file), "Cannot open source fixture");
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
            const auto world = cod4::decode_world(bytes);
            const auto camera = cod4::decode(bytes);
            const auto grenades = cod4::collect_grenade_events(world);
            std::vector<std::string> weapons{""};
            std::istringstream names(camera.configstrings.at(2258));
            for (std::string name; names >> name;) weapons.push_back(name);
            std::cout << argv[arg] << ": " << grenades.events.size() << " detonation(s)\n";
            for (const auto& event : grenades.events)
            {
                check(cod4::grenade_weapon_matches(event.source_event,weapons.at(event.weapon)), "Fixture explosion has unsupported weapon");
                std::cout << "time=" << event.time << " relative=" << (event.time-camera.frames.front().time)/1000.0
                    << " entity=" << event.entity << " event=" << event.source_event << " -> " << cod4::native_grenade_event(event.source_event)
                    << " weapon=" << weapons.at(event.weapon) << " origin=" << event.origin[0] << ',' << event.origin[1] << ',' << event.origin[2]
                    << " direction=" << event.parameter << " surface=" << event.surface << '\n';
            }
            if (std::filesystem::path(argv[arg]).filename() == "cod4_to_H1.dm_1")
            {
                check(grenades.events.size() == 3 && grenades.events[0].time == 48750
                    && grenades.events[1].time == 51350 && grenades.events[2].time == 51850
                    && grenades.events[0].source_event == 49 && grenades.events[1].source_event == 49
                    && grenades.events[2].source_event == 45, "Crash explosion inventory changed");
                unsigned state_changes = 0; std::size_t prior = 0;
                for (const auto& frame : camera.frames)
                {
                    const auto count = cod4::grenade_events_at(grenades.events,frame.time);
                    state_changes += static_cast<unsigned>(count - prior); prior = count;
                    for (std::size_t i = 0; i < count; ++i)
                        cod4::translate_grenade_event(grenades.events[i],1500 + static_cast<unsigned>(i),71);
                }
                check(state_changes == 3, "Camera archive frequency duplicated native explosions");
            }
            if (std::filesystem::path(argv[arg]).filename() == "backlot_cod4_to_h1.dm_1")
                check(grenades.events.size() == 2 && grenades.events[0].time == 62550
                    && grenades.events[1].time == 70350 && grenades.events[1].source_event == 45,
                    "Backlot explosion inventory changed");
            if (std::filesystem::path(argv[arg]).filename() == "demo0011.dm_1")
            {
                check(grenades.events.size() == 25, "Protocol-21 detonation inventory changed");
                std::vector<int> claymores;
                for (const auto& event : grenades.events)
                    if (weapons.at(event.weapon) == "claymore_mp")
                    {
                        check(event.weapon == 39 && event.source_event == 45, "Recorded claymore identity changed");
                        const auto native_event = cod4::translate_grenade_event(event, 1500, 55);
                        check(native_event[10] == 118 && native<unsigned>(native_event,76) == 55
                            && native<unsigned>(native_event,36) == event.parameter,
                            "Claymore native event lost its weapon/direction");
                        claymores.push_back(event.time);
                    }
                check(claymores == std::vector<int>{105977100, 105996600}, "Recorded claymore detonations lost");
            }
            if (std::filesystem::path(argv[arg]).filename() == "city_cod4_to_h1_test_player_____.dm_1")
                check(grenades.events.size() == 5 && grenades.events[1].time == 102050
                    && grenades.events[1].source_event == 48 && grenades.events[2].time == 103000
                    && grenades.events[2].source_event == 45, "City flash/frag explosion inventory changed");
        }
        std::cout << "PASS: event inventory, source sequence wrap/reuse, temp-entity deduplication, native payload and surface mapping, pause/seek presence and metadata validation\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
