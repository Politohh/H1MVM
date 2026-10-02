#pragma once
#include "cod4_world_translation.hpp"
#include <iomanip>
#include <set>

namespace cod4
{
    inline std::string_view native_helicopter_definition(std::string_view model)
    {
        if (model == "vehicle_cobra_helicopter_fly") return "cobra_mp";
        if (model == "h1_vehicle_mi24_hind") return "hind_mp";
        throw std::runtime_error("Unmapped native helicopter model: " + std::string(model));
    }

    template<class Lookup, class Register>
    bool ensure_helicopter_definition(unsigned index, std::string_view expected, Lookup lookup, Register register_definition)
    {
        if (!index || index >= 64 || (expected != "cobra_mp" && expected != "hind_mp"))
            throw std::runtime_error("Invalid replay helicopter definition binding");
        if (lookup(index) == expected) return false;
        register_definition(expected, index);
        if (lookup(index) != expected)
            throw std::runtime_error("Native helicopter definition registration failed: " + std::string(expected));
        return true;
    }

    inline std::string_view helicopter_definition(std::string_view model)
    {
        if (model == "vehicle_cobra_helicopter_fly") return "cobra_mp";
        if (model == "vehicle_mi24p_hind_desert") return "hind_mp";
        throw std::runtime_error("Unmapped recorded helicopter model: " + std::string(model));
    }

    struct helicopter_frame
    {
        int time{};
        std::map<unsigned, unsigned> slots;
        std::map<unsigned, std::string> loops;
    };
    struct helicopter_track
    {
        std::vector<helicopter_frame> frames;
        std::map<unsigned, std::string> definitions; // Source model index -> native vehicle definition.
        std::set<unsigned> entities;
        std::set<std::string> sounds;
        std::size_t event_states{};
        const helicopter_frame& at(int time) const
        {
            const auto next = std::upper_bound(frames.begin(), frames.end(), time,
                [](int t, const helicopter_frame& f) { return t < f.time; });
            if (frames.empty()) throw std::runtime_error("Empty helicopter world track");
            return next == frames.begin() ? frames.front() : *(next - 1);
        }
    };

    inline helicopter_track collect_helicopters(const world_track& world, const std::map<int, std::string>& configs)
    {
        helicopter_track result;
        std::map<unsigned, unsigned> slots;
        auto live_configs = configs;
        std::size_t command = 0;
        for (const auto& frame : world.frames)
        {
            // Loop aliases can be registered by a server command at spawn time.
            while (command < world.commands.size() && world.commands[command].time <= frame.time)
            {
                std::istringstream input(world.commands[command++].text);
                std::string opcode, name;
                int index{};
                if (input >> opcode >> index && opcode == "d" && index > 1342 && index < 1598
                    && input >> std::quoted(name)) live_configs[index] = name;
            }
            std::map<unsigned, std::string> loops;
            std::erase_if(slots, [&](const auto& pair)
            {
                const auto found = frame.entities.find(pair.first);
                return found == frame.entities.end() || found->second.get(4) != 12;
            });
            std::array<bool, 8> used{};
            for (const auto& [number, slot] : slots) used.at(slot) = true;
            for (const auto& [number, entity] : frame.entities)
            {
                if (entity.get(4) != 12) continue;
                if (number != entity.get(0) || number < 72 || number >= 1022)
                    throw std::runtime_error("Invalid recorded helicopter entity number");
                const auto model = entity.get(216); // un2, not index at +136.
                const auto found = configs.find(830 + model);
                if (!model || model >= 512 || found == configs.end())
                    throw std::runtime_error("Recorded helicopter has no model identity");
                result.definitions[model] = helicopter_definition(found->second);
                result.entities.insert(number);
                if (const auto sound = entity.get(128))
                {
                    const auto alias = live_configs.find(1342 + sound);
                    if (sound >= 256 || alias == live_configs.end() || alias->second.empty())
                        throw std::runtime_error("Recorded helicopter has no loop sound identity");
                    loops[number] = alias->second;
                    result.sounds.insert(alias->second);
                }
                if (entity.get(160)) ++result.event_states;
                if (slots.contains(number)) continue;
                const auto free = std::find(used.begin(), used.end(), false);
                if (free == used.end()) throw std::runtime_error("Recording exceeds H1's eight simultaneous vehicle slots");
                const auto slot = static_cast<unsigned>(free - used.begin());
                slots[number] = slot;
                used[slot] = true;
            }
            if (result.frames.empty() || result.frames.back().slots != slots || result.frames.back().loops != loops)
                result.frames.push_back({frame.time, slots, std::move(loops)});
        }
        return result;
    }

    // Resample the recorded 20 Hz flight path at each archived camera sample.
    // Never interpolate across absence, model replacement or teleport.
    inline entity_state sample_helicopter(const world_track& world, unsigned number, int time)
    {
        const auto& frame = world.sample(time);
        auto source = frame.entities.at(number);
        if (source.get(4) != 12) throw std::runtime_error("Expected recorded helicopter");
        const auto next = std::upper_bound(world.frames.begin(), world.frames.end(), time,
            [](int t, const world_frame& f) { return t < f.time; });
        const entity_state* following = nullptr;
        if (next != world.frames.end() && time > frame.time && next->time - frame.time <= 250)
        {
            const auto found = next->entities.find(number);
            if (found != next->entities.end() && found->second.get(4) == 12
                && found->second.get(216) == source.get(216) && !((found->second.get(8) ^ source.get(8)) & 2u))
                following = &found->second;
        }
        for (unsigned offset : {12u, 48u})
        {
            auto pose = sample_trajectory(source, offset, time).origin;
            if (following && source.get(offset) == 1 && following->get(offset) == 1)
            {
                const auto target = sample_trajectory(*following, offset, next->time).origin;
                const auto amount = static_cast<float>(time - frame.time) / (next->time - frame.time);
                for (unsigned i = 0; i < 3; ++i)
                    pose[i] += (offset == 48 ? std::remainder(target[i] - pose[i], 360.0f) : target[i] - pose[i]) * amount;
            }
            source.set(offset, 1);
            source.set(offset + 4, 0);
            source.set(offset + 8, 0);
            for (unsigned i = 0; i < 3; ++i)
            {
                source.set(offset + 12 + i * 4, std::bit_cast<unsigned>(pose[i]));
                source.set(offset + 24 + i * 4, 0);
            }
        }
        return source;
    }

    inline h1_entity translate_helicopter(const entity_state& source, unsigned model,
        unsigned definition, unsigned slot, unsigned loop_sound = 0, const part_mapping* parts = nullptr)
    {
        if (source.get(4) != 12 || source.get(0) < 72 || source.get(0) >= 1022
            || !model || model >= 1024 || !definition || definition >= 64 || slot >= 8 || loop_sound >= 512)
            throw std::runtime_error("Invalid native helicopter binding");
        h1_entity result{};
        write_native(result, 0, translate_entity_number(source.get(0)));
        write_native(result, 2, std::uint16_t{2047});
        write_native(result, 4, std::uint16_t{2046});
        write_native(result, 8, static_cast<std::uint16_t>(loop_sound));
        write_native(result, 10, std::uint8_t{12}); // Native CG vehicle / helicopter renderer.
        write_native(result, 12, std::int8_t{-1});
        write_native(result, 24, source.get(112)); // Native rotor input, consumed at CG_VehiclePose +0x5FD6E2.
        write_native(result, 92, source.get(8) & 2u); // Teleport toggle has matching semantics.
        for (unsigned i = 0; i < 9; ++i)
        {
            write_native(result, 96 + i * 4, source.get(12 + i * 4));
            write_native(result, 132 + i * 4, source.get(48 + i * 4));
        }
        // H1 CG_ResetVehicle (0x5FD2C0): low 3 bits select its eight runtime
        // records, bits 3..8 select the map's registered VehicleDef.
        write_native(result, 168, (definition << 3) | slot);
        // H1 vehicle flags remain zero: normal running engine. In particular
        // bit 5 means engine-off. COD4's helicopterStage is not this bitfield.
        for (const auto& [from, to] : std::array<std::pair<unsigned, unsigned>, 6>{{
            {84,176}, {88,180}, {92,184}, {100,188}, {104,192}, {96,196}}})
            write_native(result, to, source.get(from));
        // Native CG_Vehicle reads un2.modelIndex, not scriptMover.modelIndex.
        write_native(result, 208, model);
        source_part_bits hidden{};
        for (unsigned i = 0; i < hidden.size(); ++i) hidden[i] = source.get(228 + i * 4);
        const auto native_parts = translate_parts(hidden, parts);
        for (unsigned i = 0; i < native_parts.size(); ++i) write_native(result, 216 + i * 4, native_parts[i]);
        return result;
    }
}
