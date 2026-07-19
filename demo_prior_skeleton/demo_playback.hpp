#pragma once

#include <string>
#include <string_view>
#include <filesystem>

// IW6 name: demo_playback namespace
// Mirrors iw6-mod/src/client/component/demo/demo_playback.hpp exactly.
namespace demo_playback
{
    bool playing();

    bool startplay(std::string_view file_name, bool repeat = true);
    bool startplay(std::filesystem::path& file_path, bool repeat = true);

    void stopplay();
}
