#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace theater_scene::math
{
    using vector = std::array<float, 3>;
    using axis = std::array<vector, 3>;
    inline vector local(const vector& world, const axis& basis)
    {
        vector out{};
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) out[i] += world[j] * basis[i][j];
        return out;
    }
    inline vector world(const vector& local, const axis& basis)
    {
        vector out{};
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) out[j] += local[i] * basis[i][j];
        return out;
    }
    inline vector direction(float pitch, float yaw)
    {
        constexpr float rad = 3.14159265358979323846f / 180.f;
        return {std::cos(pitch * rad) * std::cos(yaw * rad),
            std::cos(pitch * rad) * std::sin(yaw * rad), -std::sin(pitch * rad)};
    }
    // A continuous 24-hour arc: sunrise 06:00, noon 12:00, sunset 18:00.
    // This controls the dynamic light only; the map's baked sky stays unchanged.
    inline vector day_angles(float hours)
    {
        constexpr float rad = 3.14159265358979323846f / 180.f;
        const float hour_angle = (hours - 12.f) * 15.f * rad;
        const vector sun{std::cos(hour_angle) * .70710678118f,
            std::sin(hour_angle), std::cos(hour_angle) * .70710678118f};
        return {-std::asin(std::clamp(sun[2], -1.f, 1.f)) / rad,
            std::atan2(sun[1], sun[0]) / rad, 0.f};
    }
    inline axis angles(const float* angle)
    {
        constexpr float rad = 3.14159265358979323846f / 180.f;
        const float sy = std::sin(angle[1]*rad), cy = std::cos(angle[1]*rad);
        const float sp = std::sin(angle[0]*rad), cp = std::cos(angle[0]*rad);
        const float sr = std::sin(angle[2]*rad), cr = std::cos(angle[2]*rad);
        return {{{cp*cy,cp*sy,-sp}, {sr*sp*cy-cr*sy,sr*sp*sy+cr*cy,sr*cp},
            {cr*sp*cy+sr*sy,cr*sp*sy-sr*cy,cr*cp}}};
    }
    // H1's ColorFloatToBytes is BGRA, not RGBA (0xE9B00).
    inline std::uint32_t color(float r, float g, float b)
    {
        const auto byte = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
        return byte(b) | (byte(g) << 8) | (byte(r) << 16) | 0xff000000u;
    }
    struct fog
    {
        std::array<std::uint8_t, 156> bytes{};
        template<class T> void set(std::size_t offset, T value) { std::memcpy(bytes.data() + offset, &value, sizeof(value)); }
        template<class T> T get(std::size_t offset) const { T value{}; std::memcpy(&value, bytes.data() + offset, sizeof(value)); return value; }
    };
    static_assert(sizeof(fog) == 156);
}
