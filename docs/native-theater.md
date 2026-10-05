# Native theater timeline

The playback timeline uses MWR LUI widgets and its native title font. Transport buttons use the same `UIGenericButton`/`H1ButtonBackground` factory and style as MWR's pause menu, with compact layout dimensions and labels. The panel also uses the native background frame and MWR green, with a thin track, left-aligned playback status and timecode aligned to the far-right inset. The compact panel shows elapsed time, duration, playhead and event/camera-key markers. The passive panel is 480 by 46 virtual UI units; opening controls increases its height to 82. Timeline text heights are 10.5 units, including button labels in all focus states. The panel sits at the bottom center, with no filename, repeated shortcut instructions or camera-editing rows.

## Controls

- **Space:** pause/resume in gameplay and the native theater menu. Holding Space toggles once. Console, chat, other native menus and the advanced editor retain keyboard input.
- **F1:** hide the whole native panel, or show it with its controls enabled. The passive bar is hidden as well.
- **F2:** cycle first person → third person → free camera → dolly → first person.
- **K:** capture a dolly marker from the current camera and demo time.
- **J:** start/stop dolly playback (starting needs at least two markers).
- **L:** clear all markers only while dolly playback is stopped.
- **← / →:** seek backward/forward five seconds.
- **↑ / ↓:** increase/decrease playback speed through 0.025x, 0.1x, 0.25x, 0.5x, 1x, 2x and 4x. The endpoints do not wrap.
- **F9:** native playback controls during a demo; the existing browser when playback is stopped.
- **Editor:** native MWR editor with Main, Camera, Misc, Bone Camera, SSAO, Fog, DOF, Sun, Lights, Streams, Weapon and Sliders. All category buttons are visible without scrolling. Back returns to the timeline.
- **F10:** existing browser, converter and advanced camera/capture editor.
- Click the track or a camera-key marker to seek. Skip buttons move five seconds. Speed and camera buttons cycle the available values.
- `demotimeline` toggles panel visibility; `demo_native_timeline` stores the visibility preference. `demotheaterstatus` prints playback/UI diagnostics.

## Implementation

`theater_ui.cpp` publishes a playback snapshot on the main scheduler and sends a separate LUI update event at 20 Hz. This event uses wall-clock scheduling, so the panel continues updating while the replay and normal HUD clocks are paused. UI actions run on the main pipeline and carry the playback session ID to reject actions left over from another demo.

`ui_scripts/theater/timeline.lua` attaches to the root when `mp_hud` is constructed, after MWR creates the in-game root and fonts. FlowManager owns the interactive menu. The native menu and advanced overlay are mutually exclusive. F10 is handled at the Windows message layer because upstream reserves the game-level F10 event and Windows also treats it as a system key.

New conversions and recordings can append a `TUI1` event index to the existing general footer, after its null-terminated duration string. The optional index stores the remapped POV and obituary timestamps/client IDs, with a 10,000-event limit. Old footers remain readable. Malformed indexes are ignored rather than preventing playback. Older files accumulate markers as obituary events play; reconvert an original CoD4 recording for an index available from the start.

`editor.lua` uses MWR's `LUI.MenuTemplate:AddButton` for native button art, focus, fonts and navigation. Page builders register by category ID. Space uses the verified native LUI input catcher (`0x40`); unrelated menus, console, chat and ImGui keep keyboard ownership. Arrow shortcuts yield to category navigation inside the native editor. Changing between the timeline and editor leaves the previous menu through FlowManager before opening the next one.

Timeline transport uses `MenuBuilder.BuildAddChild`, which installs action properties used by the native button event handler. `BuildRegisteredType` alone draws a button but does not attach those properties. Compact labels are created with the native `ButtonLabelFactory` font-size argument, so default and focused states share the same size.

Compact timeline, category and settings buttons share `editor.fitButtonBackground`. It retains the native horizontal texture caps and uses a continuous vertical texture range instead of compressing the stock top/bottom slices. It applies to the fill, contour, stroke and overlay glow, including images recreated by native focus and lock handlers. The twelve editor categories retain their compact rows and do not use a scrollbar.

## Editor pages

The editor follows the T6MVM category structure and exposes supported H1 controls. Settings are discovered from registered native dvars and their domains; unavailable settings are omitted.

| Page | Available controls and limits |
| --- | --- |
| Main | Playback summary and eight numeric-setting presets saved as CFG files |
| Camera | Flight speed, sensitivity, boost/precision, FOV, dolly offsets, marker actions and camera-path import/export |
| Misc | Native HUD, effects and film settings |
| Bone Camera | Availability information; bone attachment needs an additional world-tag camera hook |
| SSAO | Native SSAO modes, overrides and tweak values |
| Fog | Native fog and sky-fog settings; T6 height/color presets are not portable |
| DOF | Native physical and Gaussian depth-of-field settings |
| Sun | Sun-flare and shadow settings; changing map sun direction/color is not implemented |
| Lights | Existing lighting/light-grid settings; custom light placement is not implemented |
| Streams | Single beauty-video capture range, encoder settings and capture actions; FFmpeg required |
| Weapon | Registered viewmodel settings; T6 camo, holdgun and animation hooks are not implemented |
| Sliders | A custom palette of supported scalar float settings, saved with a preset |

Capture hides the native panel. The advanced editor remains available for file browsing, conversion and detailed camera-key editing. Multi-pass capture, depth/greenscreen streams and custom spawned lights are not implemented.

Armed demo playback allows console changes to dvars whose permission flags would normally block editing, including cheat-protected, replicated and read-only settings. Native type/domain validation and latch behavior still apply. Recorded `sv_cheats` updates cannot disable editing during replay. Stopping playback restores normal permission checks and disables `sv_cheats` through the native setter. The native editor itself exposes a curated settings list rather than every console dvar.

## Checks

Run `tests/test-native-theater.cmd` from the repository after building the Lua dependency. The tests cover footer compatibility and validation, seek bounds, Space input ownership/repeat handling, in-game UI initialization order, paused panel updates and control action wiring.

The Lua contract harness is a mock of LUI, not an MWR renderer or mouse-input test. It also checks all twelve categories without a scrollbar and background mapping after native focus layers are recreated. Release x64 compilation and the regression checks pass. Live screenshots confirmed the duration and playhead, and the compact button-background correction was confirmed in playback after `lui_restart`. Full in-game acceptance of every settings page, capture operation and dvar permission path remains pending.
