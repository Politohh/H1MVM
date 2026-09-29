#pragma once
#include "cod4_playerstate_reader.hpp"
#include <map>

namespace cod4
{
    inline std::uint16_t translate_bob_cycle(int source)
    {
        if (source < 0) throw std::runtime_error("Invalid recorded bob cycle");
        // COD4 BG_GetBobCycle reads only the low byte of the predicted int
        // before dividing by 255. Archives can retain a carry above 255.
        // MWR divides its uint16 by 65535; retain the native wrapped phase.
        return static_cast<std::uint16_t>((source & 255) * 257);
    }

    struct shock_profile { std::uint8_t index{}; std::string name; };
    inline const std::string& recorded_shock_name(const std::map<int, std::string>& configs, unsigned source)
    {
        if (!source || source >= 16) throw std::runtime_error("Invalid recorded shellshock index");
        const auto found = configs.find(1954 + source);
        if (found == configs.end() || found->second.empty() || found->second.size() > 63
            || found->second.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
            throw std::runtime_error("Invalid recorded shellshock profile");
        return found->second;
    }

    inline std::string remastered_shock_name(const std::string& source)
    {
        // MWR's multiplayer flash profile is flashbang_mp (native SHOCK
        // registry), while original COD4 records it as flashbang.
        return source == "flashbang" ? "flashbang_mp" : source;
    }

    template<typename Available> std::map<unsigned, std::string> complete_shock_registry(
        const std::vector<player_frame>& players, const std::map<int, std::string>& configs,
        std::map<unsigned, std::string> registry, bool const_strings, Available&& available)
    {
        for (const auto& player : players)
        {
            const auto source = player.integer("shellshockIndex");
            if (!source) continue;
            const auto name = remastered_shock_name(recorded_shock_name(configs, source));
            if (std::any_of(registry.begin(), registry.end(), [&](const auto& entry) { return entry.second == name; })) continue;
            // With no SHOCK net-constant table H1 uses ordinary configstrings.
            // Register the native asset in an unused slot; source ordinals differ.
            if (const_strings || !available(name))
                throw std::runtime_error("Remastered shellshock profile unavailable: " + name);
            unsigned slot = 1;
            while (slot < 16 && registry.contains(slot)) ++slot;
            if (slot == 16) throw std::runtime_error("No unused H1 shellshock slot");
            registry.emplace(slot, name);
        }
        return registry;
    }

    inline std::map<unsigned, shock_profile> resolve_shock_profiles(const std::vector<player_frame>& players,
        const std::map<int, std::string>& configs, const std::map<unsigned, std::string>& native_profiles)
    {
        std::map<unsigned, shock_profile> result;
        for (const auto& player : players)
        {
            const auto source = player.integer("shellshockIndex");
            if (!source || result.contains(source)) continue;
            const auto target = remastered_shock_name(recorded_shock_name(configs, source));
            unsigned slot = 0;
            for (const auto& [index, name] : native_profiles)
            {
                if (name != target) continue;
                if (!index || index >= 16 || slot)
                    throw std::runtime_error("Invalid remastered shellshock registry");
                slot = index;
            }
            if (!slot) throw std::runtime_error("Remastered shellshock profile unavailable: " + target);
            result.emplace(source, shock_profile{static_cast<std::uint8_t>(slot), target});
        }
        return result;
    }

    template<typename Put> void write_view_motion(const player_frame& player, int time, std::uint8_t shock, Put&& put)
    {
        const auto byte = [&](const char* name)
        {
            const auto value = player.integer(name);
            if (value > 255) throw std::runtime_error("Invalid recorded view effect byte");
            return static_cast<std::uint8_t>(value);
        };
        const auto word = [&](const char* name, unsigned limit)
        {
            const auto value = player.integer(name);
            if (value > limit) throw std::runtime_error("Recorded view motion field exceeds H1's range");
            return static_cast<std::uint16_t>(value);
        };
        const auto real = [&](const char* name)
        {
            const float value = player.real(name);
            if (!std::isfinite(value) || value < 0) throw std::runtime_error("Invalid recorded view motion scale");
            return value;
        };
        if (time < 0 || shock >= 16 || (!shock && player.integer("shellshockIndex")))
            throw std::runtime_error("Invalid translated view effect state");
        // Verified PC netfield offsets and BG_CalculateView/WeaponMovement
        // consumers. These inputs let MWR produce its native motion.
        put(4, shock);
        put(5, byte("damageEvent"));
        put(6, byte("damageYaw"));
        put(7, byte("damagePitch"));
        put(8, byte("damageCount"));
        put(0x34, static_cast<std::int16_t>(word("gravity", 32767)));
        put(0x36, static_cast<std::int16_t>(word("speed", 511)));
        const auto duration = shock ? word("shellshockDuration", 65535) : std::uint16_t{};
        put(0x38, duration); // COD4 has one effect duration; H1 has flash/move spans.
        put(0x3A, duration);
        put(0x48, shock ? player.integer("shellshockTime") : 0u);
        const auto stance = player.integer("pm_flags");
        put(0x58, std::uint32_t{((stance & 1u) ? 8u : 0u) | ((stance & 2u) ? 4u : 0u)});
        put(0x1CC, real("holdBreathScale"));
        put(0x1D0, static_cast<unsigned>(word("holdBreathTimer", 65535)));
        put(0x3A4, real("aimSpreadScale"));
        // weaponIdleTime is a local prediction clock, absent from COD4's wire
        // state. Use the recorded command clock so MWR's native idle/scope
        // motion progresses, freezes on pause and restores on seek.
        put(0x5EC, time);
    }
}
