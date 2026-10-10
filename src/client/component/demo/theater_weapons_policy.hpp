#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
namespace theater_weapons
{
    // Native 256-byte entityState: primary, offhand, stowed and player weapon.
    // BG agent/player DObj builders read these at 0x2BC700 / 0x2BB440.
    using world_weapon_state = std::array<unsigned, 4>;
    inline constexpr std::array<std::size_t, 4> world_weapon_offsets{0x4c, 0xbc, 0xc0, 0xc8};
    template<class Lookup>
    world_weapon_state world_weapons_for(const std::uint8_t* entity, Lookup lookup)
    {
        world_weapon_state weapons{};
        if (!entity) return weapons;
        for (std::size_t i = 0; i < weapons.size(); ++i)
        {
            std::memcpy(&weapons[i], entity + world_weapon_offsets[i], sizeof(unsigned));
            if (entity[10] == 1 || entity[10] == 18) weapons[i] = lookup(weapons[i]);
        }
        return weapons;
    }
    inline std::array<std::uint8_t, 256> world_entity(const std::uint8_t* recorded, const world_weapon_state& weapons)
    {
        std::array<std::uint8_t, 256> result;
        std::memcpy(result.data(), recorded, result.size());
        for (std::size_t i = 0; i < weapons.size(); ++i)
            std::memcpy(result.data() + world_weapon_offsets[i], &weapons[i], sizeof(unsigned));
        return result;
    }
    inline bool firearm(int category) { return category >= 0 && category <= 5; }
    inline bool compatible(int source, int target) { return firearm(source) && source == target; }
    // BG_GetWeaponNameComplete extracts the native camo from bits 8..16.
    constexpr unsigned camo_id(unsigned weapon) { return (weapon >> 8) & 511u; }
    constexpr unsigned with_camo(unsigned weapon, unsigned camo)
    { return (weapon & ~0x1ff00u) | ((camo & 511u) << 8); }
    inline bool identifier(std::string_view value)
    { return !value.empty() && value.size() < 64 && value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == value.npos; }
    struct identity { std::string weapon, attachment = "none", furniture = "base"; };
    inline identity split_name(std::string_view value)
    {
        // Native complete names append _camoNNN after the furniture kit.
        if (const auto camo = value.rfind("_camo"); camo != value.npos) value = value.substr(0, camo);
        identity result;
        const auto a = value.find("_a#"), f = value.find("_f#");
        result.weapon = value.substr(0, a);
        if (a != value.npos && f != value.npos && f > a) {
            result.attachment = value.substr(a + 3, f - a - 3);
            result.furniture = value.substr(f + 3);
        }
        return result;
    }
    inline const char* category_name(int c)
    {
        constexpr const char* names[]{"Assault rifles", "Sniper rifles", "Light machine guns", "Submachine guns", "Shotguns", "Pistols"};
        return firearm(c) ? names[c] : "Unsupported weapon";
    }
}
