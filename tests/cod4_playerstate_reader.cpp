#include "component/demo/cod4_playerstate_reader.hpp"
#include "component/demo/cod4_action_translation.hpp"
#include <iostream>
#include <set>
#include <sstream>

void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

int main(int argc, char** argv)
{
    try
    {
        if (argc != 3) throw std::runtime_error("Usage: reader demo output.csv");
        std::ifstream file(argv[1], std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        const auto camera = cod4::decode(bytes);
        const auto frames = cod4::decode_players(bytes);
        check(frames.size() == 405 && frames.front().time == 36250 && frames.back().time == 56450, "Snapshot fixture bounds changed");
        std::set<unsigned> weapons, offhands;
        bool full_ads = false, hip = false, firing = false, sprint = false, grenade = false;
        unsigned previous_command = 0;
        for (const auto& f : frames)
        {
            const auto command = f.integer("commandTime");
            check(command > previous_command && command <= static_cast<unsigned>(f.time), "Invalid player command chronology");
            previous_command = command;
            weapons.insert(f.integer("weapon")); offhands.insert(f.integer("offHandIndex"));
            check(f.real("viewHeightCurrent") == 60, "Unexpected fixture view height");
            full_ads |= f.real("fWeaponPosFrac") == 1; hip |= f.real("fWeaponPosFrac") == 0;
            firing |= f.integer("weaponstate") == 5;
            sprint |= f.integer("weaponstate") == 23;
            grenade |= f.integer("weaponstate") == 19;
        }
        check(weapons == std::set<unsigned>{14,31} && offhands == std::set<unsigned>{40,43}, "Incorrect source weapon identities");
        check(full_ads && hip && firing && sprint && grenade, "Recorded action states missing");
        unsigned ads_intervals = 0;
        for (std::size_t i = 1; i < frames.size(); ++i)
        {
            const auto& a = frames[i - 1]; const auto& b = frames[i];
            const int start = a.integer("commandTime"), end = b.integer("commandTime");
            check(cod4::interpolate_ads(frames, start) == a.real("fWeaponPosFrac"), "ADS sample moved at original timestamp");
            if (a.integer("weapon") != b.integer("weapon") || end - start > 250) continue;
            if (a.real("fWeaponPosFrac") == b.real("fWeaponPosFrac")) continue;
            ++ads_intervals;
            const int mid = start + (end - start) / 2;
            const float expected = a.real("fWeaponPosFrac") + (b.real("fWeaponPosFrac") - a.real("fWeaponPosFrac")) * float(mid - start) / float(end - start);
            check(std::abs(cod4::interpolate_ads(frames, mid) - expected) < 0.00001f, "ADS interpolation is stepped or time-shifted");
        }
        check(ads_intervals > 0, "Fixture contains no ADS transition intervals");
        check(cod4::interpolate_ads(frames, 0) == frames.front().real("fWeaponPosFrac"), "ADS start clamp failed");
        check(cod4::interpolate_ads(frames, 100000) == frames.back().real("fWeaponPosFrac"), "ADS end clamp failed");
        std::cout << "Validated " << ads_intervals << " ADS transition intervals\n";
        std::vector<std::string> names{""};
        std::istringstream registry(camera.configstrings.at(2258));
        for (std::string name; registry >> name;) names.push_back(name);
        const auto slots = cod4::resolve_ammo_slots(names);
        check(slots[14].reserve == 11 && slots[14].clip == 14, "Deagle ammo registration mismatch");
        check(slots[31].reserve == 2 && slots[31].clip == 31, "M40 ammo registration mismatch");
        check(frames.front().clips[14] == 7 && frames.front().clips[31] == 5, "Initial magazines incorrect");
        unsigned shots = 0;
        for (std::size_t i = 1; i < frames.size(); ++i)
        {
            const auto& f = frames[i];
            if (f.time >= 41400 && f.time <= 42350 && frames[i-1].clips[14] - f.clips[14] == 1) ++shots;
            check((cod4::translate_animation(f.integer("weapAnim")) & 2048u) == ((f.integer("weapAnim") & 512u) << 2),
                "Animation restart toggle lost");
            cod4::translate_weapon_state(f.integer("weaponstate"));
        }
        check(shots == 7, "Recorded Deagle shot/ammo sequence lost");
        check(cod4::translate_animation(514) == 2050 && cod4::translate_animation(14) == 21
            && cod4::translate_animation(536) == 2081, "Shot/reload/sprint destination IDs incorrect");
        // Mixed raw-byte/bit payload and signed integer XOR, independent of fixture values.
        const std::array<std::uint8_t, 3> signed_delta{3, 0xFF, 0xFF};
        cod4::detail::reader delta_reader{signed_delta};
        check(static_cast<int>(cod4::detail::player_delta(delta_reader, 0, {"timer", -16, 0}, 0)) == -1,
            "Signed delta decoding failed");
        bool rejected = false;
        try { cod4::decode_players(std::span(bytes).first(bytes.size()-1)); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Truncated player stream accepted");
        std::cout << "Source hands: " << frames.front().integer("viewmodelIndex") << ' '
            << camera.configstrings.at(830 + frames.front().integer("viewmodelIndex")) << '\n';
        for (unsigned i = 0; i < 128; ++i)
        {
            std::set<int> counts;
            for (const auto& f : frames) counts.insert(f.clips[i]);
            if (counts != std::set<int>{0}) { std::cout << "clip " << i << ":"; for (auto c : counts) std::cout << ' ' << c; std::cout << '\n'; }
        }
        for (unsigned i = 0; i < 64; ++i)
        {
            std::set<int> counts;
            for (const auto& f : frames) counts.insert(f.ammo[i]);
            if (counts != std::set<int>{0}) { std::cout << "ammo " << i << ":"; for (auto c : counts) std::cout << ' ' << c; std::cout << '\n'; }
        }
        std::ofstream out(argv[2]);
        out << "time,commandTime,weapon,offHandIndex,weaponstate,weapAnim,weaponTime,weaponDelay,ads,height,pm_flags,eventSequence,event0,event1,event2,event3\n";
        for (const auto& f : frames)
        {
            out << f.time;
            for (auto n : {"commandTime", "weapon", "offHandIndex", "weaponstate", "weapAnim", "weaponTime", "weaponDelay"})
                out << ',' << static_cast<int>(f.integer(n));
            out << ',' << f.real("fWeaponPosFrac") << ',' << f.real("viewHeightCurrent");
            for (auto n : {"pm_flags", "eventSequence", "events[0]", "events[1]", "events[2]", "events[3]"}) out << ',' << f.integer(n);
            out << '\n';
        }
        std::cout << "Decoded " << frames.size() << " player snapshots: " << frames.front().time << ".." << frames.back().time
            << "\nWeapons: " << camera.configstrings.at(2258) << '\n';
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
