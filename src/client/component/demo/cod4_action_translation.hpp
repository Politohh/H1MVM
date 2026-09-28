#pragma once
#include "cod4_ammo_catalog.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace cod4
{
    // COD4 weapAnimNumber -> H1 weapAnimNumber (not XAnim asset slots).
    // H1 BG_WeaponAnimToAnimIndex at RVA 2E8E50 supplies the destination meaning.
    inline unsigned translate_animation(unsigned source)
    {
        constexpr std::array<unsigned, 30> ids{
            0, 1, 2, 3, 4, 5, 6, 7, 10, 10, 17, 18, 19, 20, 21,
            22, 23, 25, 26, 27, 28, 29, 30, 31, 33, 34, 38, 40, 41, 42
        };
        const auto index = source & ~512u;
        if (index >= ids.size()) throw std::runtime_error("Unsupported COD4 weapon animation");
        // Preserve the toggle which restarts consecutive shots of the same type.
        return ids[index] | ((source & 512u) ? 2048u : 0u);
    }

    inline unsigned translate_weapon_state(unsigned source)
    {
        constexpr std::array<unsigned, 27> ids{
            0, 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16,
            17, 18, 19, 20, 21, 24, 25, 26, 27, 28, 32, 33
        };
        if (source >= ids.size()) throw std::runtime_error("Unsupported COD4 weapon state");
        return ids[source];
    }

    struct ammo_slots { unsigned reserve, clip; };
    inline std::vector<ammo_slots> resolve_ammo_slots(const std::vector<std::string>& names)
    {
        std::map<std::string_view, unsigned> reserves, clips;
        std::vector<ammo_slots> result(names.size());
        for (unsigned i = 1; i < names.size(); ++i)
        {
            const auto found = std::find_if(ammo_groups.begin(), ammo_groups.end(),
                [&](const auto& group) { return group.weapon == names[i]; });
            if (found == ammo_groups.end()) throw std::runtime_error("Unknown COD4 ammo metadata: " + names[i]);
            const auto reserve = reserves.try_emplace(found->ammo, static_cast<unsigned>(reserves.size()) + 1).first->second;
            const auto clip = clips.try_emplace(found->clip, i).first->second;
            if (reserve >= 64 || clip >= 128) throw std::runtime_error("COD4 ammo registry exceeds protocol bounds");
            result[i] = {reserve, clip};
        }
        return result;
    }
}
