#include "component/demo/cod4_world_translation.hpp"
#include <iostream>
#include <set>

namespace
{
    void check(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }
}

int main(int argc, char** argv)
{
    try
    {
        check(argc == 2, "Pass D1ablo_cic7_infinite.dm_1");
        std::ifstream file(argv[1], std::ios::binary);
        check(static_cast<bool>(file), "Cannot open Diablo fixture");
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        const auto camera = cod4::decode(bytes);
        const auto world = cod4::decode_world(bytes);
        check(camera.map == "mp_strike" && world.frames.size() == 16896,
            "Diablo fixture inventory changed");

        // Both colors reference these exact IW3 v25 xmodelparts in the pinned
        // Mod Tools source, not the intact sedan's different 48-bone layout.
        for (const auto color : {"brn", "green"})
        {
            const auto prefix = std::string("vehicle_80s_sedan1_") + color;
            const auto wreck = cod4::source_bones(prefix + "_destroyed");
            check(wreck.size() == 41 && wreck[17] == "tag_driver" && wreck[18] == "tag_glass_back",
                "Destroyed sedan must use the original destroyed bone order");
            const auto wheel = cod4::source_bones(prefix + "_wheel_lf");
            check(wheel.size() == 1 && wheel.front() == "left_wheel_01_jnt", "Detached wheel bone changed");
        }
        check(cod4::source_bones("vehicle_80s_sedan1_custom_destroyed").empty(),
            "Unknown mod models must not inherit a guessed skeleton");

        std::set<std::string> models;
        unsigned states = 0, tail_bits = 0;
        for (const auto& frame : world.frames) for (const auto& [number, entity] : frame.entities)
        {
            if (entity.get(4) != 6 || !entity.get(136) || cod4::script_model_is_brush(entity)) continue;
            const auto& name = camera.configstrings.at(830 + entity.get(136));
            const auto bones = cod4::source_bones(name);
            if (bones.empty())
            {
                check(!(entity.get(228) || entity.get(232) || entity.get(236) || entity.get(240)),
                    "Recorded model with visibility bits is still missing its bone catalog");
                continue;
            }
            models.insert(name);
            std::vector<std::string_view> native{"native_only_bone"};
            native.insert(native.end(), bones.rbegin(), bones.rend());
            const auto mapping = cod4::map_parts(bones, native);
            const auto translated = cod4::translate_script_model(entity, 768, &mapping);
            for (unsigned bit = 0; bit < 256; ++bit)
            {
                std::uint32_t word{};
                std::memcpy(&word, translated.data() + 216 + bit / 32 * 4, 4);
                const bool valid = bit > 0 && bit <= bones.size();
                const auto source = valid ? static_cast<unsigned>(bones.size()) - bit : 0;
                const bool expected = valid && (entity.get(228 + source / 32 * 4) & (0x80000000u >> (source % 32)));
                check(bool(word & (0x80000000u >> (bit % 32))) == expected,
                    "Native car visibility did not follow bone names");
            }
            for (unsigned bit = static_cast<unsigned>(bones.size()); bit < 128; ++bit)
                tail_bits += bool(entity.get(228 + bit / 32 * 4) & (0x80000000u >> (bit % 32)));
            ++states;
        }
        check(models.size() == 6 && tail_bits > 0, "Fixture must exercise intact/wreck/wheel models and retained mask tails");
        std::cout << "PASS: " << world.frames.size() << " Diablo snapshots; " << models.size()
                  << " car models; " << states << " mapped states; " << tail_bits << " out-of-model bits safely ignored\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
