#pragma once

namespace cod4_snipers
{
    // Called at the existing native render and local-pose boundaries.
    void begin_frame();
    void prepare_pose();
    void end_frame();
    void apply_pose(void* object, const std::uint32_t* fresh);
    void draw_scope(int local_client);
    bool preserve_camera_motion();
}
