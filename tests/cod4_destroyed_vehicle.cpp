#include "component/demo/cod4_world_translation.hpp"
#include <iostream>
#include <set>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<typename T> T native(const cod4::h1_entity& entity, unsigned offset)
{ T value; std::memcpy(&value,entity.data()+offset,sizeof(value)); return value; }

int main(int argc, char** argv)
{
    try
    {
        check(argc == 2, "Pass car_explo_to_h1.dm_1");
        std::ifstream file(argv[1],std::ios::binary);
        check(static_cast<bool>(file), "Cannot open car fixture");
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
        const auto camera = cod4::decode(bytes);
        const auto world = cod4::decode_world(bytes);
        std::set<std::string> models, destroyed;
        unsigned states=0, destroyed_states=0, retained_tail_bits=0;
        for (const auto& frame : world.frames) for (const auto& [number,entity] : frame.entities)
        {
            if (entity.get(4) != 6 || !entity.get(136)) continue;
            const auto& name = camera.configstrings.at(830+entity.get(136));
            models.insert(name);
            const auto bones = cod4::source_bones(name);
            check(!bones.empty(), "Recorded car/debris source bone profile missing");
            if (name.ends_with("_destroyed"))
            {
                destroyed.insert(name); ++destroyed_states;
                check(bones.size() == (name.find("wagon") != name.npos ? 42u : 41u), "Destroyed source skeleton changed");
            }
            // Reorder the destination and add a native-only bone to verify
            // name mapping rather than ordinal copying or mask truncation.
            std::vector<std::string_view> target{"native_extra"};
            target.insert(target.end(),bones.rbegin(),bones.rend());
            const auto mapping = cod4::map_parts(bones,target);
            const auto translated = cod4::translate_script_model(entity,768,&mapping);
            for (unsigned source=0; source<128; ++source)
                if (source >= bones.size() && (entity.get(228+source/32*4) & (0x80000000u >> (source%32))))
                    ++retained_tail_bits;
            for (unsigned destination=0; destination<256; ++destination)
            {
                const bool valid = destination > 0 && destination <= bones.size();
                const auto source = valid ? static_cast<unsigned>(bones.size())-destination : 0;
                const bool expected = valid && (entity.get(228+source/32*4) & (0x80000000u >> (source%32)));
                check(bool(native<unsigned>(translated,216+destination/32*4) & (0x80000000u >> (destination%32))) == expected,
                    "Destroyed/debris visibility changed or retained tail hid native geometry");
            }
            ++states;
        }
        check(camera.map == "mp_backlot" && world.frames.size() == 625 && camera.frames.size() == 7990,
            "Car fixture inventory changed");
        check(models.size() == 26 && destroyed.size() == 3 && destroyed_states && retained_tail_bits,
            "Fixture did not cover all car swaps, debris and retained mask tails");
        const auto sedan = cod4::source_bones("vehicle_80s_sedan1_yel_destroyed");
        check(sedan[17] == "tag_driver" && sedan[18] == "tag_glass_back", "Intact skeleton aliased to destroyed sedan");
        auto target = std::vector<std::string_view>(sedan.begin(),sedan.end());
        target[18] = "missing_active_bone";
        const auto incomplete = cod4::map_parts(sedan,target);
        cod4::source_part_bits hidden{}; hidden[0] = 0x80000000u >> 18;
        bool rejected=false;
        try { cod4::translate_parts(hidden,&incomplete); } catch (const std::runtime_error&) { rejected=true; }
        check(rejected,"Missing in-model native bone was silently ignored");
        std::cout << "PASS: " << models.size() << " car/debris models; " << states << " states; " << destroyed_states
            << " destroyed states; " << retained_tail_bits << " retained out-of-model bits; 7990 camera archives\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
