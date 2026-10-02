#include "component/demo/cod4_transient_budget.hpp"
#include "component/demo/cod4_playerstate_reader.hpp"
#include "component/demo/cod4_weapon_catalog.hpp"
#include <iostream>
#include <set>
#include <sstream>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main(int argc, char** argv)
{
    try
    {
        check(cod4::view_weapon_slot_budget(false, 6, 108) == 6, "Normal match budget changed");
        check(cod4::view_weapon_slot_budget(true, 6, 108) == 64, "Replay retains six-slot limit");
        // Largest native reservation before view weapons: 3*18 characters,
        // 2*52 world weapons (split screen), 4 view hands. Total remains <294.
        check(cod4::view_weapon_slot_budget(true, 6, 162) + 162 <= 294, "Shared slot table overflow");
        check(cod4::view_weapon_slot_budget(true, 6, 280) == 14, "Budget ignores remaining slots");
        check(cod4::view_weapon_slot_budget(true, 6, 288) == 6, "Native minimum changed");
        for (unsigned used : {289u, 294u, 295u, 0xffffffffu})
        {
            bool rejected = false;
            try { (void)cod4::view_weapon_slot_budget(true, 6, used); }
            catch (const std::runtime_error&) { rejected = true; }
            check(rejected, "Invalid reservation accepted");
        }
        check(argc == 2, "Pass demo0011.dm_1");
        std::ifstream input(argv[1], std::ios::binary);
        check(input.good(), "Missing fixture");
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        const auto camera = cod4::decode(bytes);
        const auto players = cod4::decode_players(bytes);
        std::vector<std::string> names{""};
        std::istringstream list(camera.configstrings.at(2258));
        for (std::string name; list >> name;) names.push_back(name);
        std::set<std::string> guns;
        for (const auto& frame : players)
        {
            if (const auto weapon = frame.integer("weapon"))
                guns.emplace(cod4::map_weapon(names.at(weapon)).base);
            for (unsigned source = 1; source < names.size() && source < 128; ++source)
                if (frame.integer(std::format("weapons[{}]", source / 32)) & (1u << (source % 32)))
                    guns.emplace(cod4::map_weapon(names[source]).base);
        }
        check(guns.size() > 6, "Fixture no longer exercises the inventory-union regression");
        std::cout << "PASS native/replay budget, shared-table bounds, invalid reservations\n"
            << "Fixture inventory contains " << guns.size() << " distinct native weapons across " << players.size() << " snapshots:\n";
        for (const auto& gun : guns) std::cout << gun << '\n';
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
