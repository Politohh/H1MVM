#pragma once
#include <string>
#include <json.hpp>
namespace theater_models
{
    void update();
    nlohmann::json state();
    nlohmann::json choices();
    bool action(const std::string& name, const std::string& argument);
    void reset();
}
