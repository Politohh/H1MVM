#pragma once

namespace demo_gui
{
	// toggles the demo controls window. No-op in _DEBUG builds (h1-mod's own gui:: system
	// owns the overlay there instead) -- see demo_gui.cpp for why.
	void toggle();
}
