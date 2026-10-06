#pragma once
#include "theater_camera.hpp"

namespace theater_scene
{
    struct setting { const char* page; const char* name; const char* label; double step; };
    const std::vector<setting>& settings();
    nlohmann::json state();
    bool action(const std::string& name);
    void setting_changed(const std::string& name);
    void reset_editor();
    nlohmann::json saved_lights();
    void restore_lights(const nlohmann::json& data, bool validate_only = false);
    // Called after the normal camera has been calculated, on the render pipeline.
    void camera(theater_camera::camera_pose_t& pose);
}
