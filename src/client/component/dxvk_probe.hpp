#pragma once

#include <cstdint>
#include <string>

namespace dxvk_probe
{
	struct result
	{
		bool suitable = false;
		std::string device;  // the GPU DXVK would pick (its first adapter)
		std::string reason;  // why not, when unsuitable
		std::uint32_t vendor = 0;
		std::uint32_t device_id = 0;
		std::uint32_t driver_version = 0;
		std::uint32_t api_version = 0;
		int milliseconds = 0;
	};

	// Asks the Vulkan driver whether DXVK 3 can run on the GPU it would choose. Loads vulkan-1.dll
	// for the duration of the check only and never touches d3d11.dll or dxgi.dll.
	result run();
}
