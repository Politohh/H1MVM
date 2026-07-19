#pragma once

#include <string>
#include <string_view>

// IW6 name: demo_recording namespace
// Mirrors iw6-mod/src/client/component/demo/demo_recording.hpp exactly.
namespace demo_recording
{
    bool recording();

    bool startrecord(std::string_view file_name = {}, bool overwrite = {});
    bool stoprecord();
}
