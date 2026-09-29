#include "component/demo/cod4_world_translation.hpp"
#include "component/demo/cod4_model_metadata.hpp"
#include <iostream>

void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<typename T, std::size_t N> T native(const std::array<std::uint8_t, N>& data, unsigned offset)
{ T value; std::memcpy(&value, data.data() + offset, sizeof(value)); return value; }

int main(int argc, char** argv)
{
    try
    {
        check(argc == 2, "Pass the source COD4 demo");
        std::ifstream file(argv[1], std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        const auto world = cod4::decode_world(bytes);
        const auto camera = cod4::decode(bytes);
        check(world.frames.size() == 405 && world.client_num == 0 && world.baselines.size() == 62, "World framing mismatch");
        check(world.frames.front().entities.size() == 11 && world.frames.front().clients.size() == 1, "First world state incomplete");
        const auto& brown = world.frames.front().entities.at(92);
        const auto& green = world.frames.front().entities.at(154);
        check(brown.get(4) == 6 && camera.configstrings.at(830 + brown.get(136)) == "vehicle_80s_sedan1_brn_destructible_mp", "Wrong brown car asset");
        check(std::bit_cast<float>(brown.get(24)) == 408 && std::bit_cast<float>(brown.get(28)) == 25, "Wrong brown car placement");
        check(std::bit_cast<float>(green.get(24)) == 213 && std::bit_cast<float>(green.get(28)) == -314, "Wrong green car placement");
        const auto& client = world.frames.front().clients.at(0);
        check(camera.configstrings.at(830 + client.get(8)) == "body_mp_usmc_sniper"
            && camera.configstrings.at(830 + client.get(12)) == "head_mp_usmc_tactical_baseball_cap", "Wrong source player identity");
        for (std::size_t i = 1; i < world.frames.size(); ++i) check(world.frames[i].time > world.frames[i-1].time, "Nonmonotonic world frames");
        for (const auto& frame : world.frames)
        {
            check(&world.sample(frame.time) == &frame, "World sample changed at an exact timestamp");
            check(frame.entities.contains(92) && frame.entities.contains(154), "Recorded cars lost during source decode");
        }
        // Destination order captured from resident H1 assets. Driver, passenger
        // and trunk-strap bones are additions absent from the COD4 source model.
        const std::array<std::string_view, 51> h1_bones{
            "tag_origin", "tag_body", "tag_wheel_back_left", "tag_wheel_back_right",
            "tag_wheel_front_left", "tag_wheel_front_right", "left_wheel_01_jnt",
            "left_wheel_02_jnt", "right_wheel_01_jnt", "right_wheel_02_jnt",
            "tag_bumper_back", "tag_bumper_front", "tag_death_fx", "tag_door_left_back",
            "tag_door_left_front", "tag_door_right_back", "tag_door_right_front",
            "tag_driver", "tag_glass_back", "tag_glass_back_d", "tag_glass_back_fx",
            "tag_glass_front", "tag_glass_front_d", "tag_glass_front_fx", "tag_hood",
            "tag_hood_fx", "tag_light_left_back", "tag_light_left_back_d",
            "tag_light_left_front", "tag_light_left_front_d", "tag_light_right_back",
            "tag_light_right_back_d", "tag_light_right_front", "tag_light_right_front_d",
            "tag_passenger", "tag_trunk", "tag_trunk_strap", "tag_glass_left_back",
            "tag_glass_left_back_d", "tag_glass_left_back_fx", "tag_glass_left_front",
            "tag_glass_left_front_d", "tag_glass_left_front_fx", "tag_glass_right_back",
            "tag_glass_right_back_d", "tag_glass_right_back_fx", "tag_glass_right_front",
            "tag_glass_right_front_d", "tag_glass_right_front_fx", "tag_mirror_left", "tag_mirror_right"
        };
        const auto parts = cod4::map_parts(cod4::source_bones(camera.configstrings.at(830 + brown.get(136))), h1_bones);
        const auto car = cod4::translate_script_model(brown, 768, &parts);
        check(native<std::uint16_t>(car, 0) == 92 && native<unsigned>(car, 172) == 768
            && native<unsigned>(car, 16) == 0
            && native<unsigned>(car, 108) == brown.get(24) && native<unsigned>(car, 148) == brown.get(64), "H1 car state offsets wrong");
        for (const auto& frame : world.frames) for (const auto& [number, entity] : frame.entities)
        {
            if (entity.get(4) != 6 || !entity.get(136)) continue;
            const auto name = camera.configstrings.at(830 + entity.get(136));
            check(!cod4::source_bones(name).empty(), "Fixture car bone catalog missing");
            const auto mapping = cod4::map_parts(cod4::source_bones(name), h1_bones);
            const auto translated = cod4::translate_script_model(entity, 768, &mapping);
            // All fixture cars remain intact. Hide exactly their ten damaged
            // glass/light variants, allowing the six intact windows to render.
            for (unsigned i = 0; i < 256; ++i)
            {
                const bool expected = i == 19 || i == 22 || i == 27 || i == 29 || i == 31
                    || i == 33 || i == 38 || i == 41 || i == 44 || i == 47;
                check(bool(native<unsigned>(translated, 216 + i / 32 * 4) & (0x80000000u >> (i % 32))) == expected,
                    "Intact car visibility differs from recorded source");
            }
        }
        // Model a shot through the left front window: hide intact glass and
        // reveal its damaged variant, preserving all other recorded parts.
        auto shot = brown;
        shot.set(232, (shot.get(232) | (0x80000000u >> 5)) & ~(0x80000000u >> 6));
        const auto damaged = cod4::translate_script_model(shot, 768, &parts);
        check((native<unsigned>(damaged, 220) & (0x80000000u >> 8))
            && !(native<unsigned>(damaged, 220) & (0x80000000u >> 9)), "Shot window did not switch from intact to damaged");
        // Bone-name mapping must still work when the destination changes order.
        auto reversed = h1_bones;
        std::reverse(reversed.begin(), reversed.end());
        const auto reordered = cod4::map_parts(cod4::sedan_bones, reversed);
        const auto reordered_car = cod4::translate_script_model(brown, 768, &reordered);
        check(native<unsigned>(reordered_car, 216) & (0x80000000u >> 31), "Part translation copied engine ordinals");
        auto incomplete = h1_bones;
        incomplete[19] = "different_bone";
        const auto missing = cod4::map_parts(cod4::sedan_bones, incomplete);
        bool rejected = false;
        try { cod4::translate_script_model(brown, 768, &missing); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Missing recorded hidden bone accepted");
        rejected = false;
        try { cod4::translate_script_model(brown, 768); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Recorded visibility silently dropped without mapping");
        auto outside = brown;
        outside.set(240, 1); // Source bone 127 is outside this 48-bone model.
        check(cod4::translate_script_model(outside, 768, &parts) == car,
            "Out-of-model mask tail hid unrelated native geometry");
        const auto body = cod4::translate_client(client, 769, {770,0,0,0,0,0});
        check(native<std::uint16_t>(body,16) == 769 && native<std::uint16_t>(body,18) == 770
            && native<unsigned>(body,96) == 2 && native<std::uint16_t>(body,12) == 2047, "H1 client state offsets wrong");
        const auto metadata = cod4::parse_model_metadata({"vehicle_80s_sedan1_green_destructible_mp", "@model:768:body_usmc_desert_sniper_mp_camo"});
        check(metadata.names.size() == 2 && metadata.bindings.at(768) == "body_usmc_desert_sniper_mp_camo", "Saved model metadata lost");
        for (const auto& entry : {"@model:767:body", "@model:1024:body", "@model:768:", "@model:768:a:b", "@model:x:body"})
        {
            rejected = false;
            try { cod4::parse_model_metadata({entry}); } catch (const std::exception&) { rejected = true; }
            check(rejected, "Invalid converted binding accepted");
        }
        rejected = false;
        try { cod4::parse_model_metadata({"@model:768:a", "@model:768:b"}); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Conflicting model bindings accepted");
        for (const auto n : {std::size_t(0), std::size_t(8), bytes.size()-1})
        {
            rejected = false;
            try { cod4::decode_world(std::span(bytes).first(n)); } catch (const std::exception&) { rejected = true; }
            check(rejected, "Incomplete source world accepted");
        }
        std::cout << "PASS: 405 world snapshots, 11 intact cars, recorded glass damage, bone-name remapping, USMC sniper identity, native PC translation, saved bindings and malformed-input rejection\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
