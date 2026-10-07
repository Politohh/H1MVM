# H1-Mod Demo Client

[![Release](https://img.shields.io/github/v/release/Politohh/h1-cod4-demo)](https://github.com/Politohh/h1-cod4-demo/releases/latest)
[![Build](https://github.com/Politohh/h1-cod4-demo/actions/workflows/build.yml/badge.svg)](https://github.com/Politohh/h1-cod4-demo/actions/workflows/build.yml)
[![License](https://img.shields.io/github/license/Politohh/h1-cod4-demo)](LICENSE)

**An H1-Mod client for Call of Duty: Modern Warfare Remastered with an integrated demo system and a CoD4-to-H1 demo converter.**

This repository contains the full client, including the demo recording, playback and cinematic tools. It is based on [Aurora H1-Mod](https://github.com/auroramod/h1-mod), with additional development focused on playing and filming demos in MWR.

The converter reads original CoD4 `.dm_1` recordings and saves `.dm_h1` demos for playback in this client. Recorded players, weapons, animations and events are mapped to native MWR assets. The HUD uses MWR's interface with recorded CoD4 data.

**[Download the latest Windows x64 release](https://github.com/Politohh/h1-cod4-demo/releases/latest)** · [Changes](CHANGELOG.md) · [Technical documentation](docs/README.md)

## Features

- H1 demo recording and playback, with a native MWR timeline and demo browser.
- Pause, rewind, fast-forward, playback speed and timeline controls.
- First-person, third-person, free camera and dolly camera modes, with editable camera-path markers.
- Video capture through an optional FFmpeg executable.
- CoD4 demo conversion, including tested Promod recordings and CoD4x protocol 19 and 21 decoding.
- Native MWR character and weapon mappings, actor and death animations, gun and blood effects, grenade effects and car explosions.
- Airstrike jet and bomb conversion, attached jet effects, and helicopter flight conversion.
- Recorded data for the native MWR HUD: ammo, grenade inventory, minimap, hitmarkers, score popups and killfeed.
- `developer 1` enabled when loading a demo.
- Developer-style dvar access during demo playback, with native value ranges and latch behavior preserved.

Conversion is experimental. Coverage varies by recording, map and mod; see [Current limitations](#current-limitations).

## Install

You must legally own Call of Duty: Modern Warfare Remastered and have a supported MWR / H1-Mod installation. Game assets and demo recordings are not bundled.

1. Download the **`win64.zip`** asset from the [latest release](https://github.com/Politohh/h1-cod4-demo/releases/latest).
2. Close H1 and back up your existing `h1-mod.exe` and `h1-mod` folder.
3. Extract the ZIP into the game directory, next to `h1_mp64_ship.exe`. Replace matching files and include the bundled **`h1-mod` folder**; its scripts are required for the converted-demo HUD.
4. Launch through your **H1-Mod shortcut**.

The symbols ZIP is optional and contains crash-debugging information. It is not needed to play demos. Release assets include SHA-256 checksums and file manifests.

## Convert a CoD4 demo

1. Open H1 multiplayer and let **Play Online** initialization finish.
2. Open **THEATER**, directly below **Private Match**.
3. Choose **Import Demos** and select the original `.dm_1` file (multiple selection is supported).
4. Select its thumbnail and choose **Convert Demo**. Wait for conversion to finish.
5. Return to **Theater**, choose **Refresh → H1 Demos**, select the converted recording and choose **Play Demo**.

Converted files are saved directly under `demos/` in the game directory. Conversion also produces a `.conversion.txt` report beside the output demo, including any omitted models or unsupported data. **Convert COD4 Demo** is always available in Theater and opens a file picker directly.

**Reconvert the original `.dm_1` after converter updates.** An existing `.dm_h1` retains the data produced by the version that converted it. Keep the original recording.

The converter can also be invoked from the H1 console:

```text
cod4convert "C:\Demos\example.dm_1"
```

## Record and play H1 demos

In an H1 multiplayer match, `demostart` enables automatic recording and `demostop` disables it. Files are named automatically; these commands do not take a custom recording name. Recording locations use `demos/client/auto/`, or a mod-specific subdirectory when a mod is active.

Open **Play Online → Theater → H1 Demos** to browse native H1 recordings and converted demos. The library scans all demos subfolders and shows map previews, modes, dates and durations. Use **Import Demos** for recordings stored elsewhere. During playback, the native timeline starts visible. **F1** toggles the game HUD, **F3** opens/closes the native editor, **F4** hides/shows the timeline, and **F2** cycles camera modes. **F9** switches between cursor controls and camera input. The XP bar and its rank icons are hidden during demos. Click the track to seek, or use the shortcuts below.

| Key | During demo playback |
| --- | --- |
| **F1** | Toggle game HUD (timeline stays visible) |
| **F3** | Open/close native Theater editor |
| **F4** | Hide/show timeline and transport controls |
| **F2** | Cycle first person, third person, free camera and dolly |
| **K** | Add dolly marker |
| **J** | Start/stop dolly playback |
| **L** | Clear all markers while dolly is stopped |
| **Space** | Pause/resume |
| **← / →** | Seek backward/forward five seconds |
| **↑ / ↓** | Increase/decrease playback speed |

Free camera uses WASD and mouse look, E/Q for height, Shift for faster flight, **Shift + scroll for zoom**, and scroll for camera roll. Compact native hints appear above the timeline. Space, seek and speed shortcuts remain available while flying.

**EDITOR** opens the native MWR editor with camera, visual settings, preset, capture and custom-slider pages. All twelve category buttons remain visible without scrolling. Pages expose H1 settings plus native hooks for bone attachment, atmosphere, directional lighting, placed point lights and actor hiding. Large pages use Previous/Next buttons. Back returns to the timeline. **F10** retains the existing browser and advanced camera/capture editor. `demotimeline` shows or hides the native panel. Kill/death markers are indexed in newly converted demos; older files collect them during playback. See [Native theater timeline](docs/native-theater.md).

Useful console commands:

| Command | Action |
| --- | --- |
| `demostart` / `demostop` | Enable / disable H1 automatic recording |
| `demoreplay` | Replay the most recent demo |
| `demopause` | Toggle playback pause |
| `demoforward 5000` | Advance five seconds |
| `demorewind 5000` | Rewind five seconds |
| `cod4_snipers 1` / `cod4_snipers 0` | Enable / disable original COD4 sniper animations, zoom and 2D scopes on remastered models in converted demos |
| `cod4_2dScopes 1` | Enable just the flat sniper scope overlay |
| `cod4_snipers_status` | Show loaded-mode and native pose/overlay diagnostics |
| `avi_ffmpeg "C:\Tools\ffmpeg.exe"` | Set the optional video encoder path |

The sniper toggle is off by default. It requires the local `cod4_snipers` inputs prepared by
`tools/prepare-cod4-sniper-source.py --full` (pinned original animations/rest rigs and weapon definitions/scope from your COD4 installation).
It covers M40A3, R700, Barrett, M21 and Dragunov with standard scopes; ACOG optics keep their own behavior.
ADS uses the original `tag_ads` curves with COD4's 100 ms up/down weight blend. Idle, firing, bolt cycling, segmented/magazine reloads,
weapon switches, sprint and melee use original COD4 clips retargeted onto remastered weapons/hands.
MWR helper bones are reset to their bind poses under the transferred animation. Unsupported/offhand
actions keep native animation; native animation notifications/audio remain in use.
Scene zoom starts at 95% ADS through the native camera zoom calculation, occupying only the final
5% of the transition in either direction; weapon
projection retains its configured hip FOV through the whole transition. The original ADS motion centers
the authored hip pose; detachable-magazine receivers are aligned by their magazine seat, while
bolt-action rifles use their shared scope mount. The full-ADS
endpoint is centered using the native optic's reticle anchor, with correction blended by ADS amount.
Native hand bind geometry preserves arm lengths even when stored tag positions are zero. Gun-part
translation channels are sampled as offsets from their bind pivots. Mechanical parts rotate about
their own remastered pivots and follow the authored movement relative to their shared parent,
preserving bolt and cartridge placement; the separate bolt cap follows the original bolt control.
Detached magazines transfer the authored hand contact through rotation after receiver alignment.
Original hip/ADS recoil parameters are applied for the native frame and restored afterward; COD4
preview keeps the engine's calculated camera rotation so recoil/view animation survives. The Barrett's
original scoped firing slot is idle and relies on camera kick rather than an ADS firing clip.
The 2D scope replaces the 3D viewmodel at COD4's reticle handoff (just after 95% ADS); it cuts away
immediately when leaving full ADS, with no opacity fade. Playback clock and recorded action timers
drive original clip sampling, including paused/slow playback and seeks. Sprint loops start after the
recorded sprint-in duration instead of including that duration in their phase. Timed actions use COD4's
clip-length/action-time rate; fire and bolt cycling retain their authored rate. Visual parity still requires
playback validation for each remastered rig.
The local installer uses `F:\Dev\h1-sniper-source\scope_upscale.png` as the default scope when present,
preserving its RGBA pixels; `-ScopeImage` selects another PNG. The scope uses native alpha blending.

## Current limitations

- Custom CoD4 mod assets and unrecognized mappings can be unsupported. Conversion and playback may report an explicit error rather than substitute an unrelated asset.
- Converted recordings map up to 42 character identities into 18 native player slots and 24 native agent slots. The 39-player Ambush fixture passes offline mapping checks; expanded playback validation is pending. Source player indices above 41 remain unsupported.
- The latest jet-motion and team-emblem corrections in **2026.10.03** still need visual playback confirmation. A successful build or offline fixture check does not establish complete in-game parity.
- Helicopter combat events, damage stages and ownership/compass markers are incomplete. Helicopter flight and recorded loop sound are handled separately.
- General recorded sound-event playback and killstreak inventory HUD are incomplete.
- H1 hooks and native asset mappings target the supported MWR build. Demo compatibility with other client forks or game executables is not guaranteed.

See the [release notes](https://github.com/Politohh/h1-cod4-demo/releases/latest) and [feature documentation](docs/README.md) for validation details.

## Build from source

Use Windows, Git and Visual Studio 2022 with the **Desktop development with C++** workload and a Windows SDK.

```powershell
git clone --recurse-submodules https://github.com/Politohh/h1-cod4-demo.git
cd h1-cod4-demo
.\generate.bat
```

Open `build/h1-mod.sln` and build **Release / x64**. From a Visual Studio developer shell, the equivalent is:

```text
msbuild build\h1-mod.sln /m /p:Configuration=Release /p:Platform=x64
```

The executable is generated at `build/bin/x64/Release/h1-mod.exe`. Install it with the contents of `data/cdata/` copied into the game's `h1-mod/` folder. The TLS helper is embedded in the executable.

To reproduce a release, check out its tag, run `git submodule update --init --recursive`, regenerate the solution and build that revision. Downloading GitHub's source ZIP alone does not include the required submodules.

Implementation lives in [`src/client/component/demo/`](src/client/component/demo/); runtime scripts are in [`data/cdata/`](data/cdata/). Offline regression runners are in [`tests/`](tests/). Some checks require original demo fixtures that are not distributed with the repository.

## Credits and license

- [Aurora H1-Mod](https://github.com/auroramod/h1-mod) — base client.
- [s1-mod](https://git.alterware.dev/alterware/s1-mod) and [h2-mod](https://github.com/alicealys/h2-mod) — upstream codebase and research.
- [momo5502](https://github.com/momo5502) — upstream Arxan and Steam research.
- Dependency authors — see their bundled license notices.

Licensed under [GPL-3.0](LICENSE). Call of Duty and Modern Warfare Remastered belong to their respective owners.
