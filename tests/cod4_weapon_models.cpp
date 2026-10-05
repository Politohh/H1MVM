#include "component/demo/cod4_weapon_models.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main()
{
    try
    {
        // Rows read from the installed MWR mp/furniturekits/base.csv on 2026-10-04.
        const std::array<std::array<const char*, 8>, 4> rows{{
            {{"h1_m16_mp", "base", "1", "wpn_h1_asl_m16a4_vm_camo", "wpn_h1_asl_m16a4_npc_camo", "",
                "wpn_h1_asl_m16a4_vm_wet_camo", "wpn_h1_asl_m16a4_npc_wet_camo"}},
            {{"h1_m16_mp", "gl", "0", "attach_h1_handguard_m16_m203_vm_patch_camo_mp",
                "attach_h1_handguard_m16_m203_npc_patch_camo_mp", "",
                "attach_h1_handguard_m16_m203_vm_patch_wet_camo_mp", "attach_h1_handguard_m16_m203_npc_patch_wet_camo_mp"}},
            {{"h1_m16_mp", "acog", "0", "attach_h1_scope_acog_vm_mp", "attach_h1_scope_acog_npc_mp", "",
                "attach_h1_scope_acog_vm_wet_mp", "attach_h1_scope_acog_npc_wet_mp"}},
            {{"h1_m16_mp", "defaultgrip", "1", "attach_h1_handguard_m16_default_vm_patch_camo_mp",
                "attach_h1_handguard_m16_default_npc_patch_camo_mp", "",
                "attach_h1_handguard_m16_default_vm_patch_wet_camo_mp", "attach_h1_handguard_m16_default_npc_patch_wet_camo_mp"}}
        }};
        const auto collect = [&](const char* weapon, const char* attachment, bool view, int count = 4)
        {
            return cod4::collect_weapon_kit_models(weapon, attachment, view, count,
                [&](int row, int col) { return rows.at(row).at(col); });
        };
        const auto gl = collect("h1_m16_mp", "gl", true);
        check(gl.base_view && gl.base_world && gl.attachment_view && gl.attachment_world,
            "M16 GL must use kit geometry for both views");
        check(gl.names.size() == 12 && gl.names.contains("attach_h1_handguard_m16_m203_npc_patch_camo_mp"),
            "M16 GL lost native dry/wet geometry");
        check(!gl.names.contains("attach_h1_scope_acog_npc_mp"), "Unselected attachment requested");
        const auto remote = collect("h1_m16_mp", "gl", false);
        check(remote.names.size() == 6 && !remote.base_view && !remote.attachment_view
            && remote.base_world && remote.attachment_world, "Remote rifle requested view geometry");
        const auto none = collect("h1_m16_mp", "none", true);
        check(none.names.size() == 8 && none.base_view && none.base_world
            && !none.attachment_view && !none.attachment_world, "Base furniture suppressed attachment fallback");
        const auto absent = collect("h1_m4_mp", "gl", true);
        check(absent.names.empty() && !absent.base_view && !absent.base_world
            && !absent.attachment_view && !absent.attachment_world, "Unmapped kit suppressed native models");
        const auto empty = collect("h1_m16_mp", "gl", true, 0);
        check(empty.names.empty() && !empty.attachment_world, "Missing table suppressed native models");
        auto view_only = rows;
        view_only[1][4] = view_only[1][7] = "";
        const auto partial = cod4::collect_weapon_kit_models("h1_m16_mp", "gl", true, 4,
            [&](int row, int col) { return view_only.at(row).at(col); });
        check(partial.attachment_view && !partial.attachment_world,
            "View-only kit incorrectly suppressed world-model fallback");
        std::cout << "PASS M16 M203 kit geometry, dry/wet variants, remote-only requests and native fallback\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
