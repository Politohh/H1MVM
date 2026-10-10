#pragma once
#include <cstdint>
#include <string_view>

namespace theater_editor::dof
{
    // H1 MP excludes the DOF render targets when bit 5 is set. Checking the
    // dvar alone is insufficient between changing quality and applying video settings.
    constexpr bool resources_ready(int quality, std::uint32_t render_flags)
    {
        return quality > 0 && (render_flags & 0x20u) == 0;
    }

    constexpr bool activates_override(std::string_view name)
    {
        return name.starts_with("r_dof_") && name != "r_dof_enable" &&
            name != "r_dof_tweak" && name != "r_dof_physical_enable";
    }

    constexpr bool allows_edit(std::string_view name, int enum_value, bool ready)
    {
        // Native r_dof_tweak: 0 = Off (map DOF), 1 = On, 2 = Force Disable.
        return name != "r_dof_tweak" || enum_value != 1 || ready;
    }
}
