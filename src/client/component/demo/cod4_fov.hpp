#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace cod4::fov
{
    // COD4 CG_GetViewFov blends cg_fov toward adsZoomFov, then applies
    // cg_fovScale. The weapon uses that projection too. H1 instead multiplies
    // the weapon FOV by (base / cg_fov) * (1 - zoom) + zoom.
    // The engine supplies the scene FOV, including ADS and aspect handling;
    // bypass only H1's additional weapon compensation.
    inline std::optional<float> weapon_projection(bool enabled, bool first_person,
        float scene_fov, bool flat_sniper, float hip_fov)
    {
        if (!enabled || !first_person || !std::isfinite(scene_fov)
            || scene_fov <= 0.f || scene_fov >= 180.f) return {};
        // A flat sniper scope replaces the gun during scene magnification.
        // Keep its 3D optic at hip projection until the visibility cut.
        if (flat_sniper)
        {
            if (!std::isfinite(hip_fov) || hip_fov <= 0.f) return {};
            return std::clamp(hip_fov, 1.f, 170.f); // native scene FOV ceiling
        }
        return scene_fov;
    }
}
