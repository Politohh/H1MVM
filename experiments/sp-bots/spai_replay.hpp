#pragma once
#include <string>
#include <vector>
#include "animation_schema.hpp"
namespace spai_probe
{
    // Capability 5 requires body/head preloading before rendering, including
    // subsequent maps on the same connection. Older peers cannot do this safely.
    bool remote_models_ready();
    inline constexpr const char* network_capability=animation_schema::network_capability;
    std::string recording_marker();
    std::string recording_characters();
    void read_replay_marker(const std::vector<std::string>& fields);
    void clear_replay_marker();
    bool replay_requested();
    std::string network_schema_marker();
    bool accept_network_schema(const std::string& marker);
    void clear_network_schema();
    void enforce_peer_capabilities();
}
