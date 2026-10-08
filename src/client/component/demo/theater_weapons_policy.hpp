#pragma once
#include <string>
#include <string_view>
namespace theater_weapons
{
    inline bool firearm(int category) { return category >= 0 && category <= 5; }
    inline bool compatible(int source, int target) { return firearm(source) && source == target; }
    inline bool identifier(std::string_view value)
    { return !value.empty() && value.size() < 64 && value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == value.npos; }
    struct identity { std::string weapon, attachment = "none", furniture = "base"; };
    inline identity split_name(std::string_view value)
    {
        identity result;
        const auto a = value.find("_a#"), f = value.find("_f#");
        result.weapon = value.substr(0, a);
        if (a != value.npos && f != value.npos && f > a) {
            result.attachment = value.substr(a + 3, f - a - 3);
            result.furniture = value.substr(f + 3);
        }
        return result;
    }
    inline const char* category_name(int c)
    {
        constexpr const char* names[]{"Assault rifles", "Sniper rifles", "Light machine guns", "Submachine guns", "Shotguns", "Pistols"};
        return firearm(c) ? names[c] : "Unsupported weapon";
    }
}
