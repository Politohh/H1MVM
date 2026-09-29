#pragma once
#include "theater_camera.hpp"
#include <string>
#include <map>

namespace cod4_camera
{
    struct status
    {
        bool active{}, loading{}, paused{};
        float seconds{}, duration{}, speed{1}, eye_height{60};
        std::string name, map, message;
    };
    status get_status();
    bool active();
    void bootstrap_demo(const std::string& map, const std::vector<std::uint8_t>& gamestate);
    // Saved converted files bind resident remastered assets to unused model slots.
    void set_model_bindings(const std::map<unsigned, std::string>& bindings,
        const std::map<unsigned, std::string>& animations = {});
    void clear_model_bindings();
    // Own the client's snapshot clock while replaying a COD4 recording.
    bool prepare_client_frame();
    void finish_client_frame();
    // Supplies the recorded pose; the existing view hook preserves native client FOV.
    bool view(theater_camera::camera_pose_t& pose);
    void record_applied(const float* origin, float horizontal_fov);
}
