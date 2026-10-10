#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace theater_focus
{
    constexpr bool input_allowed(int catchers, bool theater_owns_input)
    {
        // The Theater editor uses the same native LUI catcher as the timeline.
        // Console/chat/other menus must still pause scene focus.
        return catchers == 0 || (catchers == 0x40 && theater_owns_input);
    }
    constexpr float world_depth_range = 63.f / 64.f;
    // The projection used for the sampled frame: hardware depth = A + B / viewZ.
    // Works for regular and reversed Z; never use current-camera constants on an older sample.
    inline std::optional<float> view_depth(float depth, float a, float b)
    {
        if (!std::isfinite(depth) || depth < 0 || depth > 1 ||
            !std::isfinite(a) || !std::isfinite(b) || std::abs(b) < 1.e-8f) return {};
        const float denominator = depth - a;
        if (std::abs(denominator) < 1.e-8f) return {}; // sky/infinite far plane: retain focus
        const float z = b / denominator;
        if (!std::isfinite(z) || z <= 0 || z > 1.e7f) return {};
        return z;
    }

    inline float speed_from_response(float seconds)
    {
        return seconds <= 0 ? 1.f : -std::expm1(-1.f / (60.f * seconds));
    }

    inline float smooth_focus(float current, float target, float elapsed, float speed)
    {
        if (speed >= 1 || !std::isfinite(current) || current <= 0) return target;
        // Cinematic-style speed: fraction of the remaining distance at 60 Hz.
        // Normalize by elapsed time so high frame rates do not accelerate focus.
        const float alpha = -std::expm1(std::log1p(-std::clamp(speed, .001f, .9999f)) *
            std::clamp(elapsed, 0.f, .1f) * 60.f);
        return current + (target - current) * alpha;
    }

    inline unsigned pixel(float normalized, unsigned extent)
    {
        return extent ? std::min(extent - 1, static_cast<unsigned>(std::clamp(normalized, 0.f, 1.f) * extent)) : 0;
    }
}
