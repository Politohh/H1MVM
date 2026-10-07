# Native theater timeline

Use the timeline to control playback, then open **EDITOR** to compose your shot.
For importing/converting recordings, see the [library guide](theater-library.md).
For original sniper animations on remastered models, see [classic COD4 snipers](cod4-snipers.md).

The playback timeline uses MWR LUI widgets and its native title font. Transport buttons use the same `UIGenericButton`/`H1ButtonBackground` factory and style as MWR's pause menu, with compact layout dimensions and labels. The panel also uses the native background frame and MWR green, with a thin track, left-aligned playback status and timecode aligned to the far-right inset. The compact panel shows elapsed time, duration, playhead and event/camera-key markers. The panel stays 480 by 82 virtual UI units, with every transport button visible whether controls own the cursor or the mouse drives freecam. Timeline text heights are 10.5 units, including button labels in all focus states. The panel sits at the bottom center, with no filename or camera-editing rows. Small F1/F4 labels sit above the whole frame; two compact native hint lines appear above those in freecam.

## Controls

- **Space:** pause/resume in gameplay and the native theater menu. Holding Space toggles once. Console, chat, other native menus and the advanced editor retain keyboard input.
- **F1:** toggle the game HUD without hiding the timeline. The choice is independent of camera mode and restores the original HUD settings on stop.
- **F3:** open/close the native Theater editor, including when the timeline is hidden.
- **F4:** hide/show the entire timeline and its transport buttons. Hiding releases the cursor; showing enables the clickable controls outside the editor.
- **F2:** cycle first person → third person → free camera → dolly → first person.
- **K:** capture a dolly marker from the current camera and demo time.
- **J:** start/stop dolly playback (starting needs at least two markers).
- **L:** clear all markers only while dolly playback is stopped.
- **← / →:** seek backward/forward five seconds.
- **↑ / ↓:** increase/decrease playback speed through 0.025x, 0.1x, 0.25x, 0.5x, 1x, 2x and 4x. The endpoints do not wrap.
- **F9:** native playback controls during a demo; the existing browser when playback is stopped.
- **Editor:** native MWR editor with Main, Camera, Misc, Bone Camera, SSAO, Fog, DOF, Sun, Lights, Streams, Weapon and Sliders. All category buttons are visible without scrolling. Back returns to the timeline.
- **F10:** existing browser, converter and advanced camera/capture editor.
- **Escape:** demo pause menu, with playback actions, recording details, native options and return-to-Theater routing.
- Click the track or a camera-key marker to seek. Skip buttons move five seconds. Speed and camera buttons cycle the available values.
- Entering Free Camera through the camera button or F2 releases keyboard and mouse input for flight while retaining the visible timeline. F9 switches cursor ownership; Close releases the cursor while retaining the bar. F4 hides/shows the bar itself.
- Freecam uses WASD/mouse, E/Q for height, Shift for boost, **Shift + scroll for zoom**, plain scroll for roll, MMB/K for markers, J for dolly and L to clear markers while stopped. The old ImGui help panel is replaced by small native hints.
- `demotimeline` toggles panel visibility; `demo_native_timeline` stores the visibility preference. `demotheaterstatus` prints playback/UI diagnostics.

## Recording and console commands

`demostart` enables automatic H1 recording and `demostop` disables it. Recording
names are generated automatically. Files use `demos/client/auto/`, or a
mod-specific subdirectory when a mod is active.

| Command | Action |
| --- | --- |
| `demoreplay` | Replay the most recent demo |
| `demopause` | Pause / resume |
| `demoforward 5000` / `demorewind 5000` | Seek five seconds |
| `avi_ffmpeg "C:\Tools\ffmpeg.exe"` | Set the optional video encoder path |

The editor's Capture controls use the configured encoder. Recorded match mode
and team scores feed the demo HUD while regular native matches retain their
own HUD behavior. The XP bar and rank icons are hidden during playback.

## Implementation

### Indexed backward seeking for converted COD4 replays

Since 2026.10.07.3, playback uses the converted file's existing independent full snapshots
to seek backward without restarting from the beginning. Left Arrow, the Back
button, `demorewind 5000` and backward timeline scrubs share this path. The index
builds as packets are played or fast-forwarded, with roughly one anchor per
second and no startup scan, disk cache or format change. Existing conversions
can use it without reconversion.

An anchor records its packet offset, time, sequence and associated camera
prediction. Before jumping, playback validates the file framing, native snapshot
ring and static command baseline. It clears old FX, snapshot pointers, ring
validity and prediction history, then seeds both native snapshot walkers immediately
before the selected sequence. Configstrings, assets and the loaded map remain in
place. Native first-snapshot initialization rebuilds interpolation, followed by
at most 2.5 seconds of demo-time catch-up; pauses and playback speed are preserved.
Unvisited/discontinuous ranges and native H1 delta-coded recordings use the
existing restart fallback. This is not an IW8 native-memory checkpoint port.

`tests/run-demo-seek-index.cmd "<converted .dm_h1>"` tests bounded lookup,
repeated rewinds, session reset, gaps and offsets beyond 4 GiB. On the reported
Crash recording it also checks every compressed native snapshot header,
prediction timestamp and sampled file offset: 137,738 full snapshots, 1,652
anchors and a maximum 2,203 ms catch-up. Native reset offsets were checked against
H1's `CL_ParseSnapshot` (`0x342770`), `CG_ReadNextSnapshot` (`0x32F950`) and
`CL_FirstSnapshot` (`0x33B6B0`). These checks and successful compilation do not
replace in-game confirmation of camera, animation, corpses and effects after a
paused or playing rewind.

`theater_ui.cpp` publishes a playback snapshot on the main scheduler and sends a separate LUI update event at 20 Hz. This event uses wall-clock scheduling, so the panel continues updating while the replay and normal HUD clocks are paused. UI actions run on the main pipeline and carry the playback session ID to reject actions left over from another demo.

`ui_scripts/theater/timeline.lua` attaches to the root when `mp_hud` is constructed, after MWR creates the in-game root and fonts. FlowManager owns the interactive menu. With cursor capture released, the transport row uses display-only native `H1ButtonBackground` frames and text, with no buttons or seek hitboxes that could steal camera input. F1 also gates the complete `mp_hud` parent; native HUD dvars alone only hide part of MWR's drawing. The theater timeline and hints are siblings under the LUI root, so hiding this parent leaves them visible and interactive. Child HUD visibility states continue running and normal visibility is restored on show or playback stop. The native `experienceBar_root` widget (XP track plus rank emblems) is hidden during playback and restored when playback stops; its stock HUD wrapper retains visibility ownership outside demos. The native menu and advanced overlay are mutually exclusive. F10 is handled at the Windows message layer because upstream reserves the game-level F10 event and Windows also treats it as a system key.

New conversions and recordings can append a `TUI1` event index to the existing general footer, after its null-terminated duration string. The optional index stores the remapped POV and obituary timestamps/client IDs, with a 10,000-event limit. Old footers remain readable. Malformed indexes are ignored rather than preventing playback. Older files accumulate markers as obituary events play; reconvert an original CoD4 recording for an index available from the start.

`editor.lua` uses MWR's `LUI.MenuTemplate:AddButton` for native button art, focus, fonts and navigation. Page builders register by category ID. Space uses the verified native LUI input catcher (`0x40`); unrelated menus, console, chat and ImGui keep keyboard ownership. Arrow shortcuts yield to category navigation inside the native editor. Changing between the timeline and editor leaves the previous menu through FlowManager before opening the next one.

Timeline transport uses `MenuBuilder.BuildAddChild`, which installs action properties used by the native button event handler. `BuildRegisteredType` alone draws a button but does not attach those properties. Compact labels are created with the native `ButtonLabelFactory` font-size argument, so default and focused states share the same size.

Compact timeline, category and settings buttons share `editor.fitButtonBackground`. It retains the native horizontal texture caps and uses a continuous vertical texture range instead of compressing the stock top/bottom slices. It applies to the fill, contour, stroke and overlay glow, including images recreated by native focus and lock handlers. The twelve editor categories retain their compact rows and do not use a scrollbar. Settings keep native button shells and slider textures, with explicit compact mouse hit areas and one action per click/key. Dragging sends the latest value per setting to the main pipeline, where it is applied and acknowledged. Pending local edits survive older snapshots; unchanged values are not continuously refreshed.

## Editor pages

The editor follows the T6MVM category structure and exposes supported H1 controls. Settings are discovered from registered native dvars and their domains; unavailable settings are omitted.

| Page | Available controls and limits |
| --- | --- |
| Main | Playback summary and eight CFG presets including settings, custom sliders and placed point lights |
| Camera | Flight speed, sensitivity, boost/precision, FOV, freeze freecam, deterministic shake, dolly offsets, marker actions and camera-path import/export |
| Misc | Full game HUD toggle, player/corpse hiding, effects, bloom, film settings and near clip |
| Bone Camera | Native world-tag attachment, actor/bone selection, local offsets, optional rotation following and detach to freecam |
| SSAO | Native SSAO modes, overrides and tweak values |
| Fog | Copy map atmosphere, override distance/height density, haze, scattering direction, colors and sky-fog controls |
| DOF | Native physical and Gaussian depth-of-field settings |
| Sun | Directional-light color, intensity, elevation/azimuth and Time Of Day; native flare and shadow settings |
| Lights | Selected map-light color/intensity, up to 16 placed point lights with position/color/radius/intensity, native light-grid and shadow settings |
| Streams | Capture current settings or omit players/corpses, In/Out range, encoder settings and capture actions; FFmpeg required |
| Weapon | Native viewmodel visibility; offsets, sway, custom camo, holdgun and animation replacement require further H1 viewmodel/asset hooks |
| Sliders | Up to 20 controls from registered H1 render/camera/effect float/vector controls and color pickers, saved with a preset |

Large settings pages use native Previous/Next buttons. All twelve categories remain visible and no scrollbar is added. RGB settings use a color picker with a saturation/brightness square, hue strip, preview, HSV adjustment and preset swatches. Native RGBA alpha is preserved and remains separately editable. Noncolor vector channels preserve the other components when edited. Only registered settings with a usable native type/domain appear; latched and read-only settings are available through the demo console where applicable, rather than advertised as immediate menu controls.

Editor rows own their mouse input: native MWR dispatches mouse release to a parent before its children, so nested arrow buttons could lose clicks. Decorative arrows now share the row's hit testing, while keyboard left/right remains supported. Sliders have a solid fill anchored at the left, a moving thumb and absolute pointer dragging with capture until mouse release. Headers and Main controls share the center of the settings area; labels, arrows, values and tracks use a consistent vertical center.

**Reset This Tab** restores that category. **Reset All Editor Settings**, available in Main and every settings page, restores all exposed tab settings and selected custom sliders, shows the game HUD, detaches Bone Camera and clears editor point lights. It keeps the demo, dolly markers, custom palette selection and saved preset files. Queued edits are acknowledged/discarded before Reset/Load, so an earlier drag cannot reapply after resetting.

**Time Of Day** selects a continuous 24-hour dynamic sun arc in ten-minute steps: sunrise at 06:00, noon at 12:00, sunset at 18:00. It sets elevation and azimuth together; manual direction edits remain available without an active clock fighting them. Intensity and color remain separate. This is a lighting-direction control, not a sky replacement or a rebake of the map. A hook at the native sun-direction resolver (`0x6503D0`, called by `R_UpdateSunLight` at `0x1C71B0`) supplies the same ray before the shadow direction is calculated. Native sun-shadow caching is temporarily disabled while the override is active and restored when it ends, so moving the sun does not reuse static map shadows. The verified setup call at `0x1C2852` is guarded against incompatible engine builds.

Scene hooks run only during an armed replay. Atmosphere changes go through native `R_SetFog`; sun/map-light edits affect the renderer's per-frame primary-light copy; placed points use native `R_AddOmniLightToScene`. Player/corpse filtering checks the actual native entity pose and type at `R_AddDObjToScene`, leaving props intact. Bone Camera uses the current DObj world-tag matrix, preserves the initial camera offset and detaches if the target disappears. It does not automatically switch from a living player to a separately allocated corpse. Shake uses demo time, so pause and seek remain deterministic.

Sun Light Override and other ON/OFF settings work from the row, arrows or keyboard selection. Direct edits to sun, map-light, fog and supported renderer tweak values automatically enable their corresponding override; loading a preset or resetting a page preserves the saved/default override state. Use **Copy Map Atmosphere** before adjusting fog. Place a point at the current camera, then edit it or cycle to another point. Point lights are cleared when a new replay starts and can be restored from a Main preset. The renderer may admit fewer than 16 editor points when gameplay lights occupy its native 32-light capacity. Sun edits affect dynamic illumination; baked lighting remains baked.

Capture hides the native panel. The advanced editor remains available for file browsing, conversion and detailed camera-key editing. Automatic multiple-pass exports, depth/chroma material streams, custom spotlights, custom weapon camos and T6 shader/animation replacements are not implemented. They require separate H1 renderer/asset work; T6's offsets and shaders cannot be reused.

Armed demo playback allows console changes to dvars whose permission flags would normally block editing, including cheat-protected, replicated and read-only settings. Native type/domain validation and latch behavior still apply. Recorded `sv_cheats` updates cannot disable editing during replay. Stopping playback restores normal permission checks and disables `sv_cheats` through the native setter. The native editor itself exposes a curated settings list rather than every console dvar.

## Checks

Run `tests/test-native-theater.cmd` from the repository after building the Lua dependency. The tests cover footer compatibility and validation, seek bounds, Space input ownership/repeat handling, in-game UI initialization order, paused panel updates and control action wiring.

The Lua contract harness is a mock of LUI, not an MWR renderer or mouse-input test. It checks all twelve categories without a scrollbar, settings pagination, timeline hiding and editor routing, plus background mapping after native focus layers are recreated. It also checks mouse/keyboard override toggles, single-dispatch arrows, bounded pointer dragging, stale/partial acknowledgements, RGB/HSV conversion including HDR brightness, color selection and restoration. These checks do not establish real engine mouse behavior or scene acceptance. A separate test exercises the production JSON-to-Lua converter with populated controls, choices, custom sliders, events and camera keys, using one-based table indices. Release x64 compilation and the regression checks pass. Live screenshots confirmed duration/playhead and the compact button-background correction; startup was checked for the scene build. The earlier bridge array defect explains the empty settings pages and is corrected. Full in-game acceptance of the current scene/settings controls and capture operations remains with the user.

## Native skybox selection

Sun > Skybox opens a native button grid of extracted stock HDR cubemaps,
including multiplayer and campaign skies. Original Map Sky restores the active
map texture. The override changes only sky image bindings during demo playback;
map materials, reflection probes, baked lightmaps and fog remain intact. Use the
Sun and Fog controls to match the selected sky. A new replay resets to Original.
The first selection uploads that texture; textures stay alive until client exit
because native command buffers retain image pointers.

Prepare the local catalog with Python and `lz4`:

```
python tools/prepare-theater-skies.py "<MWR game directory>" "F:/Dev/h1-theater-skies"
```

The extraction validates stock signed/unsigned v66 streams, inline cube headers,
face/mip byte counts, and duplicate image hashes. Its manifest identifies each
source zone and SHA-256. The installer copies only verified `.h1sky` files into
`demos/theater/skies` with rollback and file parity. Game textures are local
generated artifacts and are not included in source releases.

`tests/test-native-theater.cmd` covers format validation and selector/picker
lifecycle. `tests/test-theater-skies.cmd F:/Dev/h1-theater-skies` additionally
creates every native texture and shader view on a hardware D3D11 device. These
checks do not replace visual demo acceptance.

Color-picker images retain explicit bounds in each tint/position state. The
selection marker is created once and closed with the picker; no closed native
image is reattached to the menu.

Editor settings, sky selection and color-picker content share an upward offset
so paging, reset/restore and back buttons stay above the bottom timeline hints.
Category and picker headings use the same native `H1TitleFont` size and
`MW1Title` text style as THEATER EDITOR, with a native green divider. The Lua
contract checks heading/description spacing and content separation from the
timeline; final appearance still requires in-game inspection.
