#pragma once
#include <set>
#include <string>
#include <string_view>

namespace cod4
{
    struct weapon_kit_models
    {
        std::set<std::string> names;
        bool base_view{}, base_world{}, attachment_view{}, attachment_world{};
    };

    // Furniture rows replace the corresponding base or attachment geometry.
    // They do not replace reticles or model-valued weapon fields (projectiles).
    template <typename Column>
    weapon_kit_models collect_weapon_kit_models(std::string_view weapon, std::string_view attachment,
        bool needs_view, int rows, Column column)
    {
        weapon_kit_models result;
        for (int row = 0; row < rows; ++row)
        {
            if (weapon != column(row, 0)) continue;
            const bool base = std::string_view(column(row, 2)) == "1";
            if (!base && attachment != column(row, 1)) continue;
            for (int col : {3, 4, 6, 7})
            {
                const bool view = col == 3 || col == 6;
                if (view && !needs_view) continue;
                const std::string name(column(row, col));
                if (name.empty()) continue;
                (base ? (view ? result.base_view : result.base_world)
                    : (view ? result.attachment_view : result.attachment_world)) = true;
                result.names.insert(name);
            }
        }
        return result;
    }
}
