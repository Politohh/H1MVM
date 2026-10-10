#pragma once
#include "theater_weapons_policy.hpp"
#include <json.hpp>
#include <set>
#include <string>
#include <vector>
namespace database { struct XModel; }
namespace theater_weapons
{
    void update();
    void reset();
    bool action(const std::string& name, const std::string& argument);
    nlohmann::json state();
    nlohmann::json choices_page(const std::string& source, const std::string& weapon, int page);
    nlohmann::json camo_choices();
    std::set<std::string> held_models();
    bool different_weapon();
    std::string hud_weapon_label();
    std::vector<database::XModel*> view_models(unsigned original);
    world_weapon_state world_weapons(const std::uint8_t* entity);
}
