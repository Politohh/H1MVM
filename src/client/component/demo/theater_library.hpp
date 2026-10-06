#pragma once
namespace theater_library
{
    nlohmann::json state(int page, const std::string& filter);
    bool action(const std::string& name, const std::string& id);
}
