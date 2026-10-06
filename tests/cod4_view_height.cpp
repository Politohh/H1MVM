#include "component/demo/cod4_playerstate_reader.hpp"
#include <iostream>

namespace
{
    void check(bool value, const char* message)
    { if (!value) throw std::runtime_error(message); }
    void set(cod4::player_frame& p, std::string_view name, unsigned value)
    {
        for (std::size_t i = 0; i < cod4::player_fields.size(); ++i)
            if (cod4::player_fields[i].name == name) { p.values[i] = value; return; }
        throw std::runtime_error("Unknown field");
    }
    cod4::player_frame frame(int time, float height, int target)
    {
        cod4::player_frame p{};
        set(p, "commandTime", time); set(p, "viewHeightTarget", target);
        set(p, "viewHeightCurrent", std::bit_cast<unsigned>(height));
        return p;
    }
}

int main(int argc, char** argv)
{
    try
    {
        check(argc == 2, "Pass the crouch/prone fixture");
        std::vector<cod4::player_frame> p{frame(1000, 60, 60), frame(1050, 50, 40),
            frame(1100, 54, 60), frame(1150, 40, 40), frame(1200, 11, 11)};
        check(cod4::interpolate_view_height(p, 900) == 60 && cod4::interpolate_view_height(p, 1300) == 11,
            "Track endpoints changed");
        check(cod4::interpolate_view_height(p, 1025) == 55
            && cod4::interpolate_view_height(p, 1075) == 52
            && cod4::interpolate_view_height(p, 1175) == 25.5f, "Stance reversal/prone interpolation failed");
        check(cod4::interpolate_view_height(p, 1025.5) == 54.9f, "Preview loses sub-millisecond time");
        const auto paused = cod4::interpolate_view_height(p, 1075);
        (void)cod4::interpolate_view_height(p, 1190);
        check(cod4::interpolate_view_height(p, 1075) == paused, "Pause/rewind depends on earlier evaluations");
        for (const auto name : {"ClientNum", "pm_type", "eFlags"})
        {
            auto broken = p; set(broken[1], name, name == std::string_view("eFlags") ? 2u : 1u);
            check(cod4::interpolate_view_height(broken, 1025) == 60, "Blended across viewpoint discontinuity");
        }
        auto broken = p; set(broken[1], "origin[2]", std::bit_cast<unsigned>(300.0f));
        check(cod4::interpolate_view_height(broken, 1025) == 60, "Blended across teleport");
        broken = {frame(1000,60,60),frame(1400,11,11)};
        check(cod4::interpolate_view_height(broken,1200) == 60, "Blended across missing interval");
        bool rejected{};
        try { (void)cod4::interpolate_view_height({}, 1000); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Accepted empty player track");

        std::ifstream in(argv[1], std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
        const auto camera = cod4::decode(bytes);
        const auto players = cod4::decode_players(bytes);
        unsigned changes{}, airborne{}, reversals{}, prone{}, intermediates{};
        float old_jump{}, new_jump{}, old_height{}, new_height{};
        int previous_time{};
        for (std::size_t i = 1; i < players.size(); ++i)
        {
            const auto& a = players[i - 1]; const auto& b = players[i];
            check(cod4::interpolate_view_height(players, b.integer("commandTime")) == b.real("viewHeightCurrent"),
                "Recorded player sample was altered");
            if (b.real("viewHeightCurrent") != a.real("viewHeightCurrent"))
            {
                ++changes;
                if (a.integer("groundEntityNum") == 1023) ++airborne;
                if (b.integer("viewHeightTarget") != a.integer("viewHeightTarget")) ++reversals;
                if (b.integer("viewHeightTarget") == 11) ++prone;
            }
        }
        for (const auto& f : camera.frames)
        {
            const auto next = std::upper_bound(players.begin(), players.end(), f.time,
                [](int t,const auto& item){return t < static_cast<int>(item.integer("commandTime"));});
            const auto& a = next == players.begin() ? players.front() : *(next - 1);
            const float original = a.real("viewHeightCurrent");
            const float corrected = cod4::interpolate_view_height(players, f.time);
            check(std::isfinite(corrected) && corrected >= 0 && corrected <= 60, "Invalid fixture eye height");
            if (corrected != original) ++intermediates;
            if (previous_time && f.time - previous_time <= 10)
            {
                old_jump = std::max(old_jump,std::abs(original - old_height));
                new_jump = std::max(new_jump,std::abs(corrected - new_height));
            }
            old_height = original; new_height = corrected; previous_time = f.time;
        }
        check(changes > 100 && airborne > 20 && reversals > 20 && prone > 20 && intermediates > 1000,
            "Fixture did not exercise rapid airborne crouch and prone");
        check(new_jump < old_jump * 0.25f, "Dense archive eye-height jumps remain");
        std::cout << "PASS: endpoints, reversals, pause/rewind, discontinuities; " << camera.frames.size()
            << " archives, " << changes << " height changes, " << airborne << " airborne, " << reversals
            << " reversals, " << prone << " prone; max adjacent height jump " << old_jump << " -> " << new_jump << '\n';
    }
    catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
