#pragma once

// Minimal fps helper for demo playback pacing. TODO(h1): wire to real frame
// timing (com_frameTime / cg frametime) for accurate fast-forward pacing.
namespace fps
{
	[[nodiscard]] inline float get_avg_fps()
	{
		return 60.0f;
	}
}
