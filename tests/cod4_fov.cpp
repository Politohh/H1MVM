#include "component/demo/cod4_fov.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
    bool near(float a, float b) { return std::abs(a - b) < .0001f; }
    float projection(float scene, bool flat = false, float hip = 0.f)
    {
        const auto result = cod4::fov::weapon_projection(true, true, scene, flat, hip);
        check(result.has_value(), "Valid projection rejected");
        return *result;
    }
    // COD4's 4:3 reference FOV: tanHalfFovY = .75 * tan(fov / 2),
    // tanHalfFovX = aspect * tanHalfFovY. Compare a fixed weapon point.
    float screen_point(float fov, float aspect)
    { return .1f / (aspect * .75f * std::tan(fov * .008726646259971648f)); }
}
int main()
{
    try
    {
        for (const auto aspect : {4.f / 3.f, 16.f / 9.f, 21.f / 9.f})
        {
            check(near(projection(65.f), 65.f), "Default hip framing changed");
            check(near(projection(80.f), 80.f), "Hip weapon still locked to default FOV");
            check(screen_point(projection(80.f), aspect) < screen_point(projection(65.f), aspect),
                "Increasing hip FOV did not move the weapon point toward screen center");
            // Native scene already blended cg_fov to a fixed 40-degree ADS
            // endpoint. Both cg_fov 65 and 80 must leave that endpoint intact.
            for (const auto hip : {65.f, 80.f})
                for (const auto scale : {1.f, 1.125f, 1.25f})
                    for (const auto zoom : {0.f, .25f, .5f, .95f, 1.f})
                    {
                        const auto scene = (hip + (40.f - hip) * zoom) * scale;
                        check(near(projection(scene), scene), "Weapon and scene projections diverged during ADS");
                        if (zoom == 1.f) check(near(projection(scene), 40.f * scale), "ADS depends on hip FOV");
                    }
        }
        // Existing flat-scope sniper mode delays scene zoom to the final 5%
        // of ADS; its model must retain configured hip framing until hidden.
        for (const auto scene : {80.f, 50.f, 15.f})
            check(near(projection(scene, true, 90.f), 90.f), "Flat sniper lens magnified with scene zoom");
        check(near(projection(170.f, true, 250.f), 170.f), "Unsafe hip projection was not bounded");
        check(!cod4::fov::weapon_projection(false, true, 80.f, false, 0.f), "Disabled mode overrode native FOV");
        check(!cod4::fov::weapon_projection(true, false, 80.f, false, 0.f), "Editor camera overridden");
        for (const auto invalid : {0.f, -1.f, 180.f, std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN()})
        {
            check(!cod4::fov::weapon_projection(true, true, invalid, false, 0.f), "Invalid scene projection accepted");
            if (invalid != 180.f)
                check(!cod4::fov::weapon_projection(true, true, 65.f, true, invalid), "Invalid hip projection accepted");
        }
        std::cout << "PASS COD4 FOV: hip framing, ADS/scale, aspect ratios, flat scopes, disable/fallback\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
