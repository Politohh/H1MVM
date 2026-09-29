#pragma once
#include "cod4_car_fx.hpp"

namespace cod4
{
    inline constexpr std::string_view native_car_destroy_animation = "vehicle_80s_sedan1_destroy";
    inline constexpr unsigned converted_car_animation_index = 254;
    // Native CG_UpdateEntityScriptedAnimation adds 0x10CE to animIndex.
    // Animations end at 4556; custom weapon slot zero is at 4557.
    inline constexpr unsigned native_script_animation_config_base = 0x10CE;
    inline unsigned script_animation_configstring(unsigned index)
    {
        if (!index || index >= 255) throw std::runtime_error("Invalid native scripted animation index");
        return native_script_animation_config_base + index;
    }
    inline const char* script_animation_binding(const std::map<unsigned, std::string>& bindings, int config)
    {
        if (config <= static_cast<int>(native_script_animation_config_base)
            || config >= static_cast<int>(native_script_animation_config_base + 255)) return nullptr;
        const auto found = bindings.find(static_cast<unsigned>(config) - native_script_animation_config_base);
        return found == bindings.end() ? nullptr : found->second.c_str();
    }
    struct car_animation
    {
        unsigned entity{}, model{};
        int time{};
        std::array<float, 3> origin{}, angles{};
    };

    inline std::vector<car_animation> collect_car_animations(const world_track& world,
        const std::map<int, std::string>& configs, const std::vector<car_fx_event>& effects)
    {
        std::vector<car_animation> result;
        std::map<unsigned, unsigned> previous;
        for (const auto& frame : world.frames)
        {
            std::map<unsigned, unsigned> current;
            for (const auto& [number, entity] : frame.entities)
            {
                if (entity.get(4) != 6 || !entity.get(136) || script_model_is_brush(entity)) continue;
                const auto model = entity.get(136);
                current[number] = model;
                if (previous.contains(number) && previous.at(number) == model) continue;
                const auto found = configs.find(830 + model);
                if (found == configs.end()) throw std::runtime_error("Car model configstring missing");
                // These native ScriptableDef root death states all reference
                // this exact animation. Other destroyed scenery stays ordinary.
                if (found->second != "vehicle_80s_sedan1_yel_destroyed"
                    && found->second != "vehicle_80s_sedan1_silv_destroyed"
                    && found->second != "vehicle_80s_wagon1_brn_destroyed") continue;
                car_animation animation{number, model, frame.time};
                for (unsigned i = 0; i < 3; ++i)
                {
                    animation.origin[i] = std::bit_cast<float>(entity.get(24 + i * 4));
                    animation.angles[i] = std::bit_cast<float>(entity.get(60 + i * 4));
                    if (!std::isfinite(animation.origin[i]) || !std::isfinite(animation.angles[i]))
                        throw std::runtime_error("Invalid car animation placement");
                }
                // Do not animate pre-existing wrecks or infer a death from a
                // model name alone. Require the recorded car explosion nearby.
                const auto event = std::find_if(effects.begin(), effects.end(), [&](const auto& fx)
                {
                    if (fx.time != frame.time) return false;
                    float distance{};
                    for (unsigned i = 0; i < 3; ++i)
                        distance += (fx.origin[i] - animation.origin[i]) * (fx.origin[i] - animation.origin[i]);
                    return distance < 4096.0f;
                });
                if (event != effects.end()) result.push_back(animation);
            }
            previous = std::move(current);
        }
        return result;
    }

    inline void apply_car_animation(h1_entity& native, const car_animation& animation, int duration = 2000)
    {
        if (native[10] != 6 || animation.entity >= 1022 || animation.time < 0 || duration <= 0)
            throw std::runtime_error("Invalid native car animation");
        // CG_UpdateEntityScriptedAnimation (0x2F28E0) builds and owns a native
        // simple XAnim tree from animIndex +184 and start time +188.
        // H1 TR_ANIMATED (11) evaluates the clip's root motion and also creates
        // the native animation tree before evaluating placement (0x11BFC0).
        // The source movement is removed, so it cannot double the native pop.
        for (const auto offset : {96u, 132u})
            for (unsigned i = 0; i < 9; ++i) write_native(native, offset + i * 4, 0u);
        write_native(native, 108, animation.origin);
        write_native(native, 144, animation.angles);
        write_native(native, 96, 11u);
        write_native(native, 100, animation.time);
        write_native(native, 104, duration);
        write_native(native, 184, converted_car_animation_index);
        write_native(native, 188, animation.time);
    }
}
