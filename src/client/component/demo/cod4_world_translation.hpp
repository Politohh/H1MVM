#pragma once
#include "cod4_world_reader.hpp"
#include "cod4_part_translation.hpp"

namespace cod4
{
    using h1_entity = std::array<std::uint8_t, 256>;
    using h1_client = std::array<std::uint8_t, 212>;
    struct entity_snapshot
    {
        std::map<unsigned, h1_entity> entities;
        void add(const h1_entity& entity)
        {
            std::uint16_t number{};
            std::memcpy(&number, entity.data(), sizeof(number));
            if (number >= 2047 || !entities.emplace(number, entity).second)
                throw std::runtime_error("Duplicate or invalid native snapshot entity number");
        }
    };
    inline bool script_model_is_brush(const entity_state& source)
    {
        // COD4 CG_ScriptMover selects an inline BSP brush when solid is
        // 0xFFFFFF. Its index is not an XModel configstring or asset ordinal.
        return source.get(4) == 6 && source.get(152) == 0xFFFFFFu;
    }

    template<std::size_t N, typename T> void write_native(std::array<std::uint8_t, N>& data,
        std::size_t offset, const T& value)
    {
        if (offset + sizeof(T) > N) throw std::runtime_error("Native state offset outside record");
        std::memcpy(data.data() + offset, &value, sizeof(T));
    }

    inline std::uint16_t translate_entity_number(unsigned source)
    {
        if (source >= 1024) throw std::runtime_error("COD4 entity number outside protocol bounds");
        return static_cast<std::uint16_t>(source == 1023 ? 2047 : source == 1022 ? 2046 : source);
    }

    struct trajectory_sample { std::array<float, 3> origin{}, velocity{}; };
    inline unsigned native_trajectory_type(unsigned source)
    {
        // Verified against H1 BG_EvaluateTrajectory (0x2C2750) and
        // PhysicsLaunchClient (0x453FB0). H1 adds low gravity before the
        // eased types; 9 is client physics, 11 is animated root motion.
        if (source <= 5) return source;
        if (source <= 8) return source + 1;
        throw std::runtime_error("Unsupported COD4 script model trajectory");
    }
    inline trajectory_sample sample_trajectory(const entity_state& source, unsigned offset, int time)
    {
        const auto type = source.get(offset);
        const auto start = static_cast<int>(source.get(offset + 4));
        const auto duration = static_cast<int>(source.get(offset + 8));
        float elapsed = static_cast<float>(static_cast<std::int64_t>(time) - start) / 1000.0f;
        float position_factor = elapsed, velocity_factor = 1;
        switch (type)
        {
        case 0: case 1: position_factor = velocity_factor = 0; break;
        case 2: break;
        case 3:
            if (duration < 0) throw std::runtime_error("Invalid COD4 linear-stop duration");
            position_factor = std::clamp(elapsed, 0.0f, duration / 1000.0f);
            velocity_factor = elapsed >= 0 && elapsed < duration / 1000.0f ? 1.0f : 0.0f;
            break;
        case 4:
            if (duration <= 0) throw std::runtime_error("Invalid COD4 sine duration");
            position_factor = std::sin(elapsed * 1000.0f / duration * 6.2831853071795864769f);
            velocity_factor = std::cos(elapsed * 1000.0f / duration * 6.2831853071795864769f)
                * (6283.1853071795864769f / duration);
            break;
        case 5: break;
        default: throw std::runtime_error("Unsupported COD4 projectile trajectory");
        }
        trajectory_sample sample;
        for (unsigned i = 0; i < 3; ++i)
        {
            const float base = std::bit_cast<float>(source.get(offset + 12 + i * 4));
            const float delta = std::bit_cast<float>(source.get(offset + 24 + i * 4));
            if (!std::isfinite(base) || !std::isfinite(delta)) throw std::runtime_error("Invalid COD4 projectile coordinates");
            sample.origin[i] = base + delta * position_factor;
            sample.velocity[i] = delta * velocity_factor;
        }
        if (type == 5)
        {
            // COD4's BG trajectory uses gravity 800; MWR uses 900. Bake the
            // recorded path instead of resimulating the throw with H1 gravity.
            sample.origin[2] -= 400.0f * elapsed * elapsed;
            sample.velocity[2] -= 800.0f * elapsed;
        }
        for (unsigned i = 0; i < 3; ++i)
            if (!std::isfinite(sample.origin[i]) || !std::isfinite(sample.velocity[i]))
                throw std::runtime_error("COD4 projectile trajectory overflow");
        return sample;
    }

    inline h1_entity translate_missile(const entity_state& source, unsigned weapon, int time, bool alternate = false)
    {
        if (source.get(0) >= 1022 || source.get(4) != 4 || !(weapon & 255u) || weapon >= (1u << 27))
            throw std::runtime_error("Invalid missile translation");
        h1_entity result{};
        write_native(result, 0, translate_entity_number(source.get(0)));
        write_native(result, 2, translate_entity_number(source.get(116)));
        write_native(result, 4, translate_entity_number(source.get(124)));
        // H1's CG weapon lookup reads entityState.inAltWeaponMode at +6.
        write_native(result, 6, static_cast<std::uint8_t>(alternate));
        write_native(result, 10, std::uint8_t{4});
        write_native(result, 12, std::int8_t{-1});
        write_native(result, 76, weapon);
        write_native(result, 168, source.get(84)); // lerp.u.missile.launchTime
        const auto position = sample_trajectory(source, 12, time);
        const bool stationary = source.get(12) == 0 || source.get(12) == 1;
        // Every archived camera sample gets a fresh native trajectory. Its
        // instantaneous velocity preserves smooth motion between those samples.
        write_native(result, 96, stationary ? 0u : 2u);
        write_native(result, 100, time);
        write_native(result, 108, position.origin);
        write_native(result, 120, position.velocity);
        // Angular trajectory types 0..4 have matching native semantics.
        if (source.get(48) > 4) throw std::runtime_error("Unsupported COD4 missile rotation");
        for (unsigned i = 0; i < 9; ++i) write_native(result, 132 + i * 4, source.get(48 + i * 4));
        return result;
    }

    inline h1_entity translate_script_model(const entity_state& source, std::uint16_t model,
        const part_mapping* parts = nullptr)
    {
        if (source.get(0) >= 2047 || !model || model >= 1024 || source.get(4) != 6)
            throw std::runtime_error("Invalid script model translation");
        h1_entity result{};
        write_native(result, 0, static_cast<std::uint16_t>(source.get(0)));
        write_native(result, 2, std::uint16_t{2047});
        write_native(result, 4, std::uint16_t{2046});
        write_native(result, 10, std::uint8_t{6});
        write_native(result, 11, std::uint8_t{7});
        write_native(result, 12, std::int8_t{-1});
        // MWR's script-mover renderer reads lerp.u.scriptMover.modelIndex
        // at +172 (CG caller 0x331390), not the generic index at +16.
        write_native(result, 172, static_cast<std::uint32_t>(model));
        source_part_bits source_parts{};
        for (unsigned i = 0; i < source_parts.size(); ++i) source_parts[i] = source.get(228 + i * 4);
        const auto hidden = translate_parts(source_parts, parts);
        // CG's entity update (0xEAC70) passes centity+0x214 to DObjSetHidePartBits
        // (0x5C40B0), which copies eight words. entityState starts at +0x13C.
        for (unsigned i = 0; i < hidden.size(); ++i) write_native(result, 216 + i * 4, hidden[i]);
        // Preserve the recorded launch point/force for native client physics.
        // These are not velocity vectors when COD4 trType is 8.
        for (unsigned i = 0; i < 9; ++i)
        {
            write_native(result, 96 + i * 4, source.get(12 + i * 4));
            write_native(result, 132 + i * 4, source.get(48 + i * 4));
        }
        write_native(result, 96, native_trajectory_type(source.get(12)));
        write_native(result, 132, native_trajectory_type(source.get(48)));
        return result;
    }

    inline h1_client translate_client(const client_state& source, std::uint16_t body,
        const std::array<std::uint16_t, 6>& attachments, std::string_view recorded_name = {})
    {
        if (source.get(0) >= 64 || !body || body >= 1024)
            throw std::runtime_error("Invalid H1 client model translation");
        h1_client result{};
        write_native(result, 0, static_cast<std::uint8_t>(source.get(0)));
        write_native(result, 2, static_cast<std::uint16_t>(source.get(80)));
        write_native(result, 12, std::uint16_t{2047});
        write_native(result, 16, body);
        for (unsigned i = 0; i < attachments.size(); ++i)
            write_native(result, 18 + i * 2, attachments[i]);
        // CG_UpdateClientInfo copies 32 bytes from native clientState +42
        // into bgs.clientinfo.name. The CoD4 name is 16 bytes at +60.
        std::memcpy(result.data() + 42, source.words.data() + 15, 16);
        if (!recorded_name.empty())
        {
            std::memset(result.data() + 42, 0, 32);
            std::memcpy(result.data() + 42, recorded_name.data(), std::min(recorded_name.size(), std::size_t{31}));
        }
        // CG's PLAYER path 0x330839 -> 0x32F1B0 -> 0x32E880 reads
        // clientState.team at +8, maintaining characterInfo +12/+16.
        // The adjacent 0x32E770 path uses AGENT state (+76), not clients.
        write_native(result, 8, source.get(4));
        return result;
    }
}
