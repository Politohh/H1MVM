#pragma once

#include <string>

namespace dxvk
{
	// One line: which D3D11 this start uses (bundled DXVK, the player's own DLLs or the system's) and why
	std::string status();
}
