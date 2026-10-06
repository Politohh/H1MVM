#include "component/demo/cod4_facial_models.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

void check(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
int main()
{
    try
    {
        const std::array<std::array<const char*, 7>, 4> table{{
            {{"head_arab_desert_assault_mp", "head", "1", "", "", "", "head_arab_desert_assault_mp_lobby"}},
            {{"head_usmc_desert_sniper_mp_patch", "head", "1", "", "", "", "head_usmc_desert_sniper_mp_lobby_patch"}},
            {{"head_arab_desert_assault_mp", "head", "0", "", "", "", "head_unselected_dlc"}},
            {{"head_arab_desert_assault_mp", "body", "1", "", "", "", "body_unselected"}}
        }};
        const auto get = [&](int row, int col) { return table.at(row).at(col); };
        check(cod4::is_lobby_facial_head("head_usmc_desert_sniper_mp_lobby_patch")
            && cod4::is_lobby_facial_head("head_arab_desert_assault_mp_lobby")
            && !cod4::is_lobby_facial_head("head_usmc_desert_sniper_mp_patch"),
            "Patched lobby head was excluded from facial playback");
        check(cod4::lobby_head_for("head_arab_desert_assault_mp", 4, 7, get) == "head_arab_desert_assault_mp_lobby",
            "Wrong stock lobby head selected");
        check(cod4::lobby_head_for("head_usmc_desert_sniper_mp_patch", 4, 7, get) == "head_usmc_desert_sniper_mp_lobby_patch",
            "Patch identity was fabricated from a suffix");
        check(!cod4::lobby_head_for("head_custom", 4, 7, get), "Custom head was replaced");
        check(!cod4::lobby_head_for("body_usmc", 4, 7, get), "Body was treated as a head");
        check(!cod4::lobby_head_for("head_custom", 4, 6, get), "Incomplete table was indexed");
        bool rejected{};
        try { cod4::lobby_head_for("head_arab_desert_assault_mp", 2, 7,
            [&](int row, int col) { return col == 6 ? (row ? "head_conflict" : "head_first") : table[0][col]; }); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Ambiguous mapping was accepted");
        rejected = false;
        try { cod4::lobby_head_for("head_arab_desert_assault_mp", 1, 7,
            [&](int row, int col) { return col == 6 ? "../invalid:head" : get(row,col); }); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Invalid native identity was accepted");
        std::cout << "PASS native stock lobby head selection, patch names, custom heads and invalid table records\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
