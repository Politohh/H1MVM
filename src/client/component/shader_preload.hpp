#pragma once

namespace shader_preload
{
	// Techniques drawn, and preloads abandoned because the GPU stopped answering, since startup
	int techniques_drawn();
	int gpu_failures();

	// ShaderUpload's queue: materials taken and materials queued (both reset when it empties)
	unsigned int queue_read();
	unsigned int queue_write();
}
