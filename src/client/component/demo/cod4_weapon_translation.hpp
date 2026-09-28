#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>

namespace cod4
{
    // COD4 bg_weapons.h weapAnimNumber_t -> H1's logical animation number.
    // H1 0x2E8E50 resolves logical numbers to the named WeaponAnimNumber slots
    // in game/database.hpp. These are not XAnim asset indices.
    inline unsigned h1_weapon_animation(unsigned source)
    {
        constexpr std::array<unsigned, 30> mapping{
            0, 1, 2, 3, 4, 5, 6, 7, 10, 10, 17, 18, 19, 20, 21,
            22, 23, 25, 26, 27, 28, 29, 30, 31, 33, 34, 38, 40, 41, 42
        };
        const auto index = source & ~512u;
        if (index >= mapping.size()) throw std::runtime_error("Unsupported COD4 weapon animation");
        return mapping[index] | ((source & 512) ? 2048u : 0u);
    }

    inline unsigned h1_weapon_state(unsigned source)
    {
        // Semantic enum mapping: COD4 weaponstate_t -> H1 weaponstate_t.
        constexpr std::array<unsigned, 27> mapping{
            0,1,2,3,4,6,8,9,10,11,12,13,14,15,16,17,18,19,20,21,24,25,26,27,28,32,33
        };
        if (source >= mapping.size()) throw std::runtime_error("Unsupported COD4 weapon state");
        return mapping[source];
    }
}
