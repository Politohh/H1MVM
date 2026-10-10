#pragma once
namespace theater_library
{
    nlohmann::json state(int page, const std::string& filter);
    bool action(const std::string& name, const std::string& id);
    bool split(const std::string& id, int start, int end);
    nlohmann::json events(const std::string& id);
    bool bookmark(const std::string& id, int time);
    bool consume_return();
}
