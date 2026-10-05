#pragma once

namespace theater_editor
{
    // Engine access stays on the main pipeline; Lua receives an immutable copy.
    void update();
    nlohmann::json state(int revision);
    bool set(const std::string& name, double value);
    bool action(const std::string& name, const std::string& argument);
}
