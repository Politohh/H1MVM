# Replay HUD and mouse input

The center action-slot widget uses native controller-style D-pad artwork even with gamepad input disabled. Only that widget's construction and configuration callback select this layout; mouse/keyboard input and all other menus retain their actual device setting.

F1 hides or restores the game HUD independently of F4's timeline visibility. F1 remains available in native menus even when Theater does not own text/navigation keys; console, chat and the advanced overlay retain their input. Showing the HUD repairs disabled renderer HUD gates, and the wall-clock UI update reapplies the requested parent visibility after native animations.

Weapon names refresh from the current viewmodel selector on the same wall-clock update, including paused playback, seeks and Weapon Swapper replacements. The native weapon widget keeps its own formatting and ammunition logic; recordings and ammunition values are not rewritten.

## High polling-rate compatibility

The Release client defaults to `in_mouseHighPolling 1`. It combines up to 64 consecutive queued `WM_MOUSEMOVE` messages for the same window and unchanged button/modifier state into their final absolute position. It stops at other queued events, adds no timer or smoothing delay, and leaves buttons and wheel events to the native message pump. The mouse's configured USB polling rate and DPI are unchanged. Free-camera movement is transferred through atomic accumulators so consuming a frame cannot erase a concurrent mouse delta.

Use `in_mouseHighPolling 0` in the game console for an immediate comparison with native message dispatch, or `in_mouseHighPolling 1` to reenable it. This setting is saved. `mouseinputstatus` reports received and combined motion counts; zero combined messages means Windows did not supply a consecutive backlog during that sample. The batching currently uses the Release client's existing window-procedure hook; Debug builds use their separate overlay/input path.

Validation includes synthetic 1,000/8,000-message queues, preservation of clicks, wheel/key/window/modifier boundaries, a 64-message batch limit, concurrent producer/consumer movement totals, and Lua HUD/input-switch regressions. These are correctness checks, not a hardware performance measurement. OP1 8K testing at 8,000 Hz, visual HUD acceptance and native playback/seek validation remain pending. No FPS improvement or elimination of OS/driver stalls is claimed without that test.

Windows reference: [WM_MOUSEMOVE](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mousemove) and [PeekMessageW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-peekmessagew).
