#pragma once
#include <string>
#include <set>
#include <json.hpp>
namespace theater_models
{
    void update();
    std::set<std::string> held_models();
    nlohmann::json state();
    void dump_state(const std::string& label);
    nlohmann::json choices();
    nlohmann::json wardrobe(const std::string& kind, const std::string& category, const std::string& group, int page, const std::string& query, const std::string& team);
    nlohmann::json choices_page(const std::string& kind, const std::string& faction, const std::string& role,
        const std::string& collection, int page, const std::string& head, const std::string& body);
    bool action(const std::string& name, const std::string& argument);
    void reset();
}
