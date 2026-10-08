#pragma once
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
    std::set<std::string> held_models();
    bool different_weapon();
    std::vector<database::XModel*> view_models(unsigned original);
}
