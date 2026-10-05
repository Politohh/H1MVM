#pragma once
#include "theater_camera.hpp"
#include <string>
#include <map>
#include <set>
#include <optional>

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
    void set_agent_names(const std::map<unsigned, std::string>& names);
    bool has_replay_agents();
    std::optional<std::string> agent_name(unsigned client);
    void validate_model_budget(const std::set<std::string>& models);
    // Register the recorded vehicle slot before native helicopter pose evaluation.
    void prepare_helicopter_vehicle(const std::uint8_t* entity_state);
    // Own the client's snapshot clock while replaying a COD4 recording.
    bool prepare_client_frame();
    void finish_client_frame();
    // Supplies the recorded pose; the existing view hook preserves native client FOV.
    bool view(theater_camera::camera_pose_t& pose);
    void record_applied(const float* origin, float horizontal_fov);
}
