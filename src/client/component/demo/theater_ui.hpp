#pragma once
#include "demo_timeline.hpp"

namespace theater_ui
{
    void set_ready(bool ready);
    [[nodiscard]] bool available();
    [[nodiscard]] bool interactive();
    [[nodiscard]] bool editor_open();
    void set_keyboard_available(bool available);
    [[nodiscard]] bool keyboard_available();
    bool toggle();
    bool handle_key_event(int key, int down);
    void close();
    // Snapshot is produced on the main pipeline; the Lua VM only reads a copy.
    nlohmann::json state(int cached_revision = -1, int cached_keys = -1);
    bool action(const std::string& name, int value);
    void note_obituary(int time, int attacker, int victim);
    void begin_recording();
    std::string recording_index(int first, int last);
}
