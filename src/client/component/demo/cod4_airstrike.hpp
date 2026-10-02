#pragma once
#include "cod4_car_fx.hpp"
#include <charconv>
#include <set>
#include <iomanip>

namespace cod4
{
    // Verified in MWR maps/mp/gametypes/_hardpoints and the loaded FX pool.
    inline constexpr unsigned airstrike_burst_index = 510;
    inline constexpr unsigned jet_engine_index = 509;
    inline constexpr unsigned jet_trail_index = 508;
    inline constexpr std::string_view native_airstrike_burst = "fx/explosions/clusterbomb_mp";
    struct tagged_source_fx { unsigned effect{}; std::string tag, name; };

    inline tagged_source_fx source_tagged_fx(unsigned parameter, const std::map<int, std::string>& configs)
    {
        const auto entry = configs.find(1698 + parameter);
        if (!parameter || parameter >= 100 || entry == configs.end() || entry->second.size() <= 2)
            throw std::runtime_error("Recorded tagged FX has no configstring identity: " + std::to_string(1698 + parameter));
        unsigned index{};
        const auto& text = entry->second;
        const auto parsed = std::from_chars(text.data(), text.data() + 2, index);
        const auto effect = configs.find(1598 + index);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + 2 || !index || index >= 100
            || effect == configs.end() || effect->second.empty())
            throw std::runtime_error("Invalid recorded tagged FX identity");
        return {index, text.substr(2), effect->second};
    }

    inline unsigned jet_effect(const tagged_source_fx& fx)
    {
        if (fx.name == "fire/jet_afterburner"
            && (fx.tag == "tag_engine_right" || fx.tag == "tag_engine_left")) return jet_engine_index;
        if (fx.name == "smoke/jet_contrail"
            && (fx.tag == "tag_right_wingtip" || fx.tag == "tag_left_wingtip")) return jet_trail_index;
        return 0;
    }

    struct airstrike_track
    {
        std::vector<car_fx_event> bursts;
        std::set<unsigned> planes, helicopters;
        std::map<unsigned, std::string> effects;
        std::map<std::pair<unsigned, unsigned>, tagged_source_fx> plane_tags;
    };

    inline airstrike_track collect_airstrikes(const world_track& world, const std::map<int, std::string>& configs)
    {
        airstrike_track result;
        std::map<unsigned, entity_state> previous;
        auto live_configs = configs;
        std::size_t command = 0;
        for (const auto& frame : world.frames)
        {
            // PlayFXOnTag allocates its configstring when first called, which
            // can be after recording begins. Apply only commands already seen
            // at this snapshot; the initial gamestate is not the full registry.
            while (command < world.commands.size() && world.commands[command].time <= frame.time)
            {
                std::istringstream input(world.commands[command++].text);
                std::string opcode, value;
                int index{};
                if (input >> opcode >> index && opcode == "d" && index >= 1598 && index < 1798
                    && input >> std::quoted(value)) live_configs[index] = value;
            }
            for (const auto& [number, entity] : frame.entities)
            {
                const auto type = entity.get(4);
                if (type == 12) result.helicopters.insert(number);
                if (type == 13) result.planes.insert(number);
                if (type != 6 && type != 13) continue;
                const auto carrier = configs.find(830 + entity.get(136));
                if (type == 6 && (carrier == configs.end() || carrier->second != "tag_origin")) continue;
                const auto prior = previous.find(number);
                const auto sequence = entity.get(160) & 255u;
                bool same = prior != previous.end() && prior->second.get(4) == type
                    && prior->second.get(136) == entity.get(136);
                // A stationary FX carrier can leave and re-enter the recording's
                // visibility set. Retain its event history across that gap, but
                // distinguish a reused entity slot at a different launch pose.
                if (same && type == 6)
                    for (unsigned offset = 12; offset < 84; offset += 4)
                        if (prior->second.get(offset) != entity.get(offset)) same = false;
                const auto count = std::min(same ? (sequence - prior->second.get(160)) & 255u : sequence, 4u);
                for (unsigned i = count; i; --i)
                {
                    const auto slot = (sequence - i) & 3u;
                    if (entity.get(164 + slot * 4) != 56) continue; // EV_PLAY_FX_ON_TAG
                    const auto parameter = entity.get(180 + slot * 4);
                    const auto fx = source_tagged_fx(parameter, live_configs);
                    if (type == 13)
                    {
                        if (const auto index = jet_effect(fx))
                        {
                            result.effects[index] = "fx/" + fx.name;
                            const auto key = std::pair{entity.get(136), parameter};
                            const auto [found, inserted] = result.plane_tags.emplace(key, fx);
                            if (!inserted && (found->second.name != fx.name || found->second.tag != fx.tag))
                                throw std::runtime_error("Recorded plane tagged FX identity changed during replay");
                        }
                        continue;
                    }
                    if (fx.name != "explosions/clusterbomb" || fx.tag != "tag_origin") continue;
                    const auto model = configs.find(830 + entity.get(136));
                    // The original and MWR scripts both use a stationary tag_origin
                    // carrier. Its local root is identity, so world EV_PLAY_FX is
                    // equivalent and permits the existing seek/age reconstruction.
                    if (model == configs.end() || model->second != "tag_origin"
                        || entity.get(12) > 1 || entity.get(48) > 1)
                        throw std::runtime_error("Unsupported moving airstrike FX carrier");
                    result.bursts.push_back({frame.time, number, fx.effect,
                        sample_trajectory(entity, 12, frame.time).origin,
                        sample_trajectory(entity, 48, frame.time).origin});
                    result.effects[airstrike_burst_index] = native_airstrike_burst;
                }
            }
            for (const auto& [number, entity] : frame.entities) previous[number] = entity;
        }
        return result;
    }

    using jet_fx_parameters = std::map<std::pair<unsigned, unsigned>, unsigned>; // model, source tag-FX -> native packed payload

    inline h1_entity translate_plane(const entity_state& source, unsigned model,
        const jet_fx_parameters& parameters, const part_mapping* parts = nullptr)
    {
        if (source.get(4) != 13 || source.get(0) >= 1022)
            throw std::runtime_error("Invalid recorded plane");
        // COD4 itself routes ET_PLANE through CG_ScriptMover; replay the
        // authored linear-stop path with MWR's native script-mover renderer.
        auto mover = source;
        mover.set(4, 6);
        auto native = translate_script_model(mover, static_cast<std::uint16_t>(model), parts);
        write_native(native, 40, source.get(160) & 255u);
        for (unsigned slot = 0; slot < 4; ++slot)
        {
            if (source.get(164 + slot * 4) != 56) continue;
            const auto found = parameters.find({source.get(136), source.get(180 + slot * 4)});
            if (found == parameters.end()) continue;
            // H1 EV_PLAY_FX_ON_TAG packs FX index in 9 bits, native bone in 8.
            write_native(native, 44 + slot * 8, 112u);
            write_native(native, 48 + slot * 8, found->second);
        }
        return native;
    }

    inline void preserve_clusterbomb_trajectory(h1_entity& native, const entity_state& source, int time)
    {
        if (source.get(12) != 5) return;
        const auto sampled = sample_trajectory(source, 12, time);
        write_native(native, 96, 2u);
        write_native(native, 100, time);
        write_native(native, 104, 0u);
        write_native(native, 108, sampled.origin);
        write_native(native, 120, sampled.velocity);
    }
}
