#pragma once
#include <string>
#include <set>
#include <json.hpp>
namespace theater_models
{
    void update();
    std::set<std::string> held_models();
    nlohmann::json state();
    nlohmann::json choices();
    nlohmann::json choices_page(const std::string& kind, const std::string& faction, const std::string& role,
        const std::string& collection, int page, const std::string& head, const std::string& body);
    bool action(const std::string& name, const std::string& argument);
    void reset();
}
