#pragma once

namespace theater_editor
{
    // Engine access stays on the main pipeline; Lua receives an immutable copy.
    void update();
    nlohmann::json state(int revision);
    int set(const std::string& name, double value);
    int set_color(const std::string& name, double red, double green, double blue);
    bool action(const std::string& name, const std::string& argument);
}
