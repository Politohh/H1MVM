#pragma once
#include <algorithm>
#include <cmath>
#include <string_view>
#include "cod4_facial_native.hpp"

namespace cod4::sniper
{
    inline int rifle(std::string_view name)
    {
        constexpr std::string_view names[]{"h1_m40a3_mp", "h1_remington700_mp", "h1_barrett_mp",
            "h1_m21_mp", "h1_dragunov_mp"};
        for (int i = 0; i < 5; ++i) if (name == names[i]) return i;
        return -1;
    }
    // Scene zoom occupies only the final 5% of ADS. COD4's CG_GetWeapReticleZoom
    // replaces the weapon when (ADS - (1 - adsZoomInFrac)) / adsZoomInFrac
    // exceeds .01. All five original sniper definitions use a .05 fraction.
    constexpr float zoom_start = .95f;
    inline float zoom_fraction(float ads)
    { return !std::isfinite(ads) ? 0.f : std::clamp((ads - zoom_start) / (1.f - zoom_start), 0.f, 1.f); }
    inline float scope_alpha(float ads, bool lowering)
    {
        if (!std::isfinite(ads)) return 0.f;
        // User-requested hard cuts: no transparent overlap with the 3D lens,
        // and no COD4/MWR opacity ramp on release.
        return (lowering ? ads >= 1.f : (ads - .95f) / .05f > .01f) ? 1.f : 0.f;
    }
    inline bool scope_replaces_gun(float ads, bool lowering) { return scope_alpha(ads, lowering) > 0.f; }
    struct ads_direction
    {
        unsigned session{}, weapon{};
        int last{-1};
        float fraction{};
        bool lowering{};
        bool sample(unsigned new_session, unsigned new_weapon, int time, float ads)
        {
            if (!std::isfinite(ads)) return false;
            if (new_session != session || new_weapon != weapon || last < 0 || time < last || time - last > 250)
                lowering = false;
            else if (ads < fraction - .00001f) lowering = true;
            else if (ads > fraction + .00001f) lowering = false;
            session = new_session; weapon = new_weapon; last = time; fraction = ads;
            return lowering;
        }
    };
    inline float clip_seconds(float ads, bool lowering, unsigned frames, unsigned fps)
    { return (lowering ? 1.f - std::clamp(ads, 0.f, 1.f) : std::clamp(ads, 0.f, 1.f)) * frames / fps; }
    struct ads_curve_blend
    {
        int begin{}, last{-1};
        float from{}, goal{}, weight{};
        float sample(int time, bool lowering, bool reset)
        {
            const float target = lowering ? 1.f : 0.f;
            if (reset || last < 0 || time < last)
                from = goal = weight = target;
            else
            {
                // COD4 PlayADSAnim blends the up/down clip weights over .1s.
                // Both clips sample their current ADS time, even on reversal.
                weight = from + (goal - from) * std::clamp((time - begin) / 100.f, 0.f, 1.f);
                if (goal != target) { from = weight; goal = target; begin = time; }
            }
            last = time;
            return weight;
        }
    };
}
