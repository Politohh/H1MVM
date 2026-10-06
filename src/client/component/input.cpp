#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "game_console.hpp"
#include "gui/gui.hpp"
#include "game/ui_scripting/execution.hpp"
#include "demo/demo_playback.hpp"
#include "demo/demo_gui.hpp"
#include "demo/theater_camera.hpp"
#include "demo/theater_ui.hpp"

#include <utils/hook.hpp>

namespace input
{
	namespace
	{
		utils::hook::detour cl_char_event_hook;
		utils::hook::detour cl_key_event_hook;
		utils::hook::detour cl_mouse_move_hook;

		void cl_char_event_stub(const int local_client_num, const int key)
		{
			if (!game_console::console_char_event(local_client_num, key))
			{
				return;
			}

#ifdef _DEBUG
			if (!gui::gui_char_event(local_client_num, key))
			{
				return;
			}
#endif

			cl_char_event_hook.invoke<void>(local_client_num, key);
		}

		void cl_key_event_stub(const int local_client_num, const int key, const int down)
		{
			// Release the playback latch even if the console opened while Space was held.
			if (key == game::K_SPACE && !down && demo_playback::handle_key_event(key, down)) return;
			if (!down && theater_ui::handle_key_event(key, down)) return;
			if (!game_console::console_key_event(local_client_num, key, down))
			{
				return;
			}

#ifdef _DEBUG
			if (!gui::gui_key_event(local_client_num, key, down))
			{
				return;
			}
#endif

			if (key == game::K_F9)
			{
				if (down)
				{
					if (theater_ui::available() && demo_playback::playing()) demo_gui::close();
					if (!theater_ui::toggle()) demo_gui::toggle();
				}
				return;
			}
			if (key == game::K_F10)
			{
				if (down) { theater_ui::close(); demo_gui::toggle(); }
				return;
			}

			if (demo_playback::handle_key_event(key, down))
			{
				return;
			}
			if (theater_ui::handle_key_event(key, down)) return;

			// freecam mouse-wheel: roll / Shift+FOV (wheel arrives as K_MWHEELUP/DOWN key events)
			if (!theater_ui::interactive() && down && theater_camera::handle_wheel(key))
			{
				return;
			}

			cl_key_event_hook.invoke<void>(local_client_num, key, down);
		}

#ifdef _DEBUG
		void cl_mouse_move_stub(const int local_client_num, int x, int y)
		{
			if (!gui::gui_mouse_event(local_client_num, x, y))
			{
				return;
			}

			cl_mouse_move_hook.invoke<void>(local_client_num, x, y);
		}
#endif
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedi())
			{
				return;
			}

			cl_char_event_hook.create(SELECT_VALUE(0x1AB8F0_b, 0x12C8F0_b), cl_char_event_stub);
			cl_key_event_hook.create(SELECT_VALUE(0x1ABC20_b, 0x135A70_b), cl_key_event_stub);
#ifdef _DEBUG
			if (game::environment::is_sp())
			{
				cl_mouse_move_hook.create(SELECT_VALUE(0x0_b, 0x27B310_b), cl_mouse_move_stub);
			}
#endif
		}
	};
}

REGISTER_COMPONENT(input::component)
