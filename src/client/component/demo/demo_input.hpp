#pragma once
#include <array>

namespace demo_input
{
    // Native MWR LUI uses 0x40 (read from the open theater menu in H1).
    // 0x10 belongs to the external overlay. Console, chat and other menus
    // must retain Space for text entry or navigation.
    constexpr bool pause_allowed(int catchers, bool theater_keyboard, bool editor_open)
    {
        return !editor_open && (catchers == 0 || (catchers == 0x40 && theater_keyboard));
    }

    class shortcut_key
    {
    public:
        enum class result { pass, consume, toggle };
        result event(bool down, bool allowed)
        {
            if (!down)
            {
                const auto previous = captured_;
                held_ = captured_ = false;
                return previous ? result::consume : result::pass;
            }
            if (held_) return captured_ ? result::consume : result::pass;
            held_ = true;
            captured_ = allowed;
            return allowed ? result::toggle : result::pass;
        }
    private:
        bool held_{}, captured_{};
    };

    // Keyboard speed changes stop at the endpoints; custom speeds move to the
    // nearest preset in the requested direction, without wrapping to slow motion.
    constexpr float step_speed(float current, int direction)
    {
        constexpr std::array speeds{.025f, .1f, .25f, .5f, 1.f, 2.f, 4.f};
        if (direction > 0)
        {
            for (const auto speed : speeds) if (speed > current + .001f) return speed;
            return speeds.back();
        }
        for (auto i = speeds.size(); i > 0; --i) if (speeds[i - 1] < current - .001f) return speeds[i - 1];
        return speeds.front();
    }
}
