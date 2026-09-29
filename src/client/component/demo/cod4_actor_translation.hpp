#pragma once
#include "cod4_actor_animation.hpp"
#include "cod4_actor_events.hpp"

namespace cod4
{
    struct actor_animation_codec
    {
        std::vector<std::int16_t> legs, torso;
        unsigned pack(unsigned leg_animation, unsigned torso_animation) const
        {
            const auto leg = legs.at(leg_animation & 2047u);
            const auto upper = torso.at(torso_animation & 2047u);
            if (leg < 0 || leg >= 2048 || upper < 0 || upper >= 512)
                throw std::runtime_error("Native actor animation has incompatible body channel");
            // BG_GetLegsAnimation (0x2B5D30), BG_GetTorsoAnimation (0x2B5DD0).
            // Bit zero selects ASM; converted actors use the native script tree.
            return ((static_cast<unsigned>(leg) | (leg_animation & 2048u)) << 11)
                | ((static_cast<unsigned>(upper) | ((torso_animation & 2048u) >> 2)) << 1);
        }
    };
    inline unsigned actor_animation(unsigned source_legs, unsigned source_torso, std::string_view weapon,
        const std::map<std::string_view, unsigned>& catalog, const actor_animation_codec& codec)
    {
        auto legs = resolve_actor_animation(source_legs, weapon, catalog);
        auto torso = resolve_actor_animation(source_torso, weapon, catalog);
        if (codec.legs.at(legs & 2047u) < 0 && codec.torso.at(legs & 2047u) >= 0)
        {
            // MWR implements grenade handling and stun reactions as upper-body
            // clips. Keep their recorded restart while supplying a native base.
            if (!(source_torso & 511u)) torso = legs;
            const auto source_name = source_body_animations.at(source_legs & 511u);
            const auto base = source_name.find("grenade") != std::string_view::npos ? "mp_hold_idle" : "mp_stand_idle";
            legs = resolve_body_animation(base, (source_legs & 512u) ? 2048u : 0u, catalog);
        }
        return codec.pack(legs, torso);
    }
    inline std::uint16_t actor_entity_number(unsigned source)
    {
        // H1 CG_Corpse (0xF28C0) indexes eight corpse records by number - 42.
        // COD4 reserves its corresponding slots after 64 player entities.
        if (source >= 64 && source < 72) return static_cast<std::uint16_t>(source - 22);
        return translate_entity_number(source);
    }
    inline unsigned actor_flags(unsigned source)
    {
        // H1 PS-to-entity (0x2C3CF0) places dead at bit 18 and ADS at 19.
        // COD4 BG_RunLerpFrameRate tests bit 19 before playing a death clip;
        // H1 BG_SetLerpFrameAnimation (0x2BA5E5) uses bit 20. Without it H1
        // sets the clip time to 1.0, skipping the death animation entirely.
        return (source & 0x1ffffu) | ((source & 0x20000u) ? 0x40000u : 0u)
            | ((source & 0x40000u) ? 0x80000u : 0u) | ((source & 0x80000u) ? 0x100000u : 0u);
    }
    struct actor_motion
    {
        std::array<float, 3> origin{}, angles{};
    };
    inline actor_motion sample_actor_motion(const world_track& world, unsigned number, int time)
    {
        const auto& frame = world.sample(time);
        const auto& source = frame.entities.at(number);
        actor_motion motion;
        for (unsigned i = 0; i < 3; ++i)
        {
            motion.origin[i] = std::bit_cast<float>(source.get(24 + i * 4));
            motion.angles[i] = std::bit_cast<float>(source.get(60 + i * 4));
            if (!std::isfinite(motion.origin[i]) || !std::isfinite(motion.angles[i]))
                throw std::runtime_error("Invalid COD4 actor pose");
        }
        // Corpses carry ragdoll launch data, not an ordinary root velocity.
        // Let native corpse physics consume it rather than interpolating it.
        if (source.get(4) != 1) return motion;
        const auto next = std::upper_bound(world.frames.begin(), world.frames.end(), time,
            [](int t, const world_frame& value) { return t < value.time; });
        if (next == world.frames.end() || time <= frame.time || next->time - frame.time > 250) return motion;
        const auto found = next->entities.find(number);
        if (found == next->entities.end() || found->second.get(4) != 1
            || found->second.get(140) != source.get(140) || ((found->second.get(8) ^ source.get(8)) & 2u)) return motion;
        const auto amount = static_cast<float>(time - frame.time) / (next->time - frame.time);
        for (unsigned i = 0; i < 3; ++i)
        {
            const auto position = std::bit_cast<float>(found->second.get(24 + i * 4));
            if (!std::isfinite(position)) throw std::runtime_error("Invalid COD4 actor interpolation");
            if (std::abs(position - motion.origin[i]) >= 256) return motion;
        }
        for (unsigned i = 0; i < 3; ++i)
        {
            motion.origin[i] = std::lerp(motion.origin[i], std::bit_cast<float>(found->second.get(24 + i * 4)), amount);
            const auto angle = std::bit_cast<float>(found->second.get(60 + i * 4));
            if (!std::isfinite(angle)) throw std::runtime_error("Invalid COD4 actor angle interpolation");
            motion.angles[i] += std::remainder(angle - motion.angles[i], 360.0f) * amount;
        }
        return motion;
    }
    inline h1_entity translate_actor(const entity_state& source, const actor_motion& motion,
        unsigned weapon, unsigned animation, const gun_event_state* events = nullptr, bool alternate = false)
    {
        const auto type = source.get(4), number = source.get(0), client = source.get(140);
        if ((type != 1 && type != 2) || client >= 42 || (type == 1 && number >= 42)
            || (type == 2 && (number < 64 || number >= 72))) throw std::runtime_error("Invalid COD4 actor identity");
        h1_entity result{};
        write_native(result, 0, type == 2 ? actor_entity_number(number) : translate_entity_number(number));
        write_native(result, 2, actor_entity_number(source.get(116)));
        write_native(result, 4, actor_entity_number(source.get(124)));
        write_native(result, 6, static_cast<std::uint8_t>(alternate));
        write_native(result, 10, static_cast<std::uint8_t>(type));
        write_native(result, 11, static_cast<std::uint8_t>(source.get(132)));
        write_native(result, 12, static_cast<std::int8_t>(client));
        write_native(result, 32, source.get(152));
        write_native(result, 76, weapon);
        write_native(result, 84, animation);
        write_native(result, 92, actor_flags(source.get(8)));
        write_native(result, 96, 1u);
        write_native(result, 108, motion.origin);
        write_native(result, 132, 1u);
        write_native(result, 144, motion.angles);
        if (type == 2)
        {
            for (unsigned off : {12u, 48u})
            {
                const auto trajectory = source.get(off);
                if (trajectory > 5 && (trajectory < 9 || trajectory > 11)) throw std::runtime_error("Unsupported COD4 corpse trajectory");
                const auto target = off == 12 ? 96u : 132u;
                for (unsigned i = 0; i < 9; ++i) write_native(result, target + i * 4, source.get(off + i * 4));
                write_native(result, target, trajectory >= 9 ? trajectory + 3 : trajectory); // native ragdoll 12..14
            }
        }
        else
        {
            camera_frame heading;
            heading.angles = motion.angles;
            heading.movement_dir = static_cast<int>(source.get(88));
            write_native(result, 168, translate_body_heading(heading));
            write_native(result, 172, source.get(84));
            if (events)
            {
                write_native(result, 40, events->sequence);
                for (unsigned i = 0; i < 4; ++i)
                {
                    write_native(result, 44 + i * 8, events->ring[i][0]);
                    write_native(result, 48 + i * 8, events->ring[i][1]);
                }
            }
        }
        write_native(result, 180, source.get(220));
        write_native(result, 184, source.get(224));
        return result;
    }
    inline unsigned item_weapon_index(const entity_state& source)
    {
        const auto index = source.get(136);
        // COD4 CG_Item packs weapon identity in the low seven bits and the
        // world-model/camo variant in the remaining four (index < 2048).
        if (source.get(4) != 3 || index >= 2048 || !(index & 127u))
            throw std::runtime_error("Invalid COD4 weapon pickup index");
        return index & 127u;
    }
    inline h1_entity translate_item(const entity_state& source, unsigned weapon, int time, bool alternate = false)
    {
        if (source.get(4) != 3 || !(weapon & 255u)) throw std::runtime_error("Invalid COD4 weapon pickup");
        // CG_Item (0x107290) gets its world model from the native weapon handle.
        auto missile = source;
        missile.set(4, 4);
        auto result = translate_missile(missile, weapon, time, alternate);
        write_native(result, 10, std::uint8_t{3});
        return result;
    }
}
