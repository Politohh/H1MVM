#pragma once
#include "theater_camera.hpp"
#include <string>
#include <map>
#include <set>
#include <optional>
#include <filesystem>
#include "cod4_model_metadata.hpp"

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
    bool convert_clip(const std::filesystem::path& source, const std::filesystem::path& output, int start, int end);
    void bootstrap_demo(const std::string& map, const std::vector<std::uint8_t>& gamestate,
        const std::string& gametype);
    // Saved converted files bind resident remastered assets to unused model slots.
    void set_model_bindings(const std::map<unsigned, std::string>& bindings,
        const std::map<unsigned, std::string>& animations = {});
    void clear_model_bindings();
    void set_facial_data(const std::vector<std::uint8_t>& bytes);
    void set_corpse_data(const std::vector<std::uint8_t>& bytes);
    void set_obituary_identities(const std::map<unsigned, cod4::obituary_identity>& identities);
    std::optional<cod4::obituary_identity> obituary_identity(unsigned entity);
    std::optional<std::string> recorded_name(unsigned token);
    void prepare_corpse(int local_client, const std::uint8_t* entity);
    void set_agent_names(const std::map<unsigned, std::string>& names);
    bool has_replay_agents();
    std::optional<std::string> agent_name(unsigned client);
    void validate_model_budget(const std::set<std::string>& models);
    void validate_extra_model_budget(const std::set<std::string>& models);
    // Register the recorded vehicle slot before native helicopter pose evaluation.
    void prepare_helicopter_vehicle(const std::uint8_t* entity_state);
    // Own the client's snapshot clock while replaying a COD4 recording.
    bool prepare_client_frame();
    void finish_client_frame();
    // Supplies the recorded pose; the existing view hook preserves native client FOV.
    bool view(theater_camera::camera_pose_t& pose);
    void record_applied(const float* origin, float horizontal_fov);
}
