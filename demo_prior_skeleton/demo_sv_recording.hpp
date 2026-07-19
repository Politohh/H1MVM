#pragma once

#include <string>
#include <string_view>
#include <cstddef>

// IW6 name: demo_sv_recording namespace
// Mirrors iw6-mod/src/client/component/demo/demo_sv_recording.hpp exactly.
namespace demo_sv_recording
{
    bool sv_recording(std::size_t client_num);

    bool sv_startrecord(std::size_t client_num,
                        std::string_view file_name = {},
                        bool overwrite = {});
    bool sv_stoprecord(std::size_t client_num);
}
