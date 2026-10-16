# H1MVM

**Record, replay and create cinematics in Call of Duty: Modern Warfare Remastered.**

[![Release](https://img.shields.io/github/v/release/Politohh/H1MVM?label=download&sort=date)](https://github.com/Politohh/H1MVM/releases/latest)
[![Windows x64](https://img.shields.io/badge/platform-Windows%20x64-0078D4)](#installation)
[![GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)

**[Download H1MVM v1.0](https://github.com/Politohh/H1MVM/releases/tag/v1.0)** · **[Documentation](docs/README.md)** · **[Changelog](CHANGELOG.md)** · **[Report a bug](https://github.com/Politohh/H1MVM/issues/new/choose)**

H1MVM is a cinematic-focused client built on [Aurora H1-Mod](https://github.com/auroramod/h1-mod). It adds native demo recording, COD4 demo conversion and a Theater editor to MWR. **v1.0 is the first release under the H1MVM name**, incorporating the latest Theater work. Earlier `demo-*` releases are historical builds of this project.

## Installation

You need **Windows x64 and an installed, supported copy of Modern Warfare Remastered**. H1MVM is a client modification, not a standalone game.

1. Download **`h1-mod.exe`** from the [v1.0 release](https://github.com/Politohh/H1MVM/releases/tag/v1.0).
2. Close the game and back up your existing client EXE, if present.
3. Put the downloaded EXE in the game directory, beside **`h1_mp64_ship.exe`**.
4. Launch **`h1-mod.exe`**. Multiplayer initialization runs automatically during startup and opens the multiplayer lobby.

The download is **one EXE**. Do not install Python, FFmpeg or a separate client support folder to play. The filename remains `h1-mod.exe` for shortcut and tool compatibility; its in-game label is **H1MVM v1.0**. Update by closing the game and replacing that EXE. The inherited upstream updater is disabled.

The EXE contains Theater scripts, classic sniper/facial animation support and the capture encoder. Models, camos, portraits and native map assets still come from your installed game. Support files are verified and extracted to `%LOCALAPPDATA%/h1-mod/bundles/` on first launch; demos and settings are kept separately. See [single-file installation and caching](docs/single-file-release.md).

Compare your download against `SHA256SUMS.txt` from the release:

```powershell
Get-FileHash .\h1-mod.exe -Algorithm SHA256
```

## What you can do

| Tool | Features |
| --- | --- |
| Demo library | Import, browse, convert, play and split demos; persistent selection by click. |
| COD4 conversion | Replay `.dm_1` recordings in MWR, including tested Promod and COD4x protocol 19/21 recordings. |
| Playback | Pause, seek, adjust speed, hide HUD and switch camera modes. |
| Cameras | First person, third person, free camera and dolly paths with a prepared start frame. |
| Shot editor | Camera/FOV, lighting, fog, SSAO, depth of field and continuous mouse autofocus. Toggle background dim to see adjustments clearly. |
| Model Swapper | Browse looks with available native portraits; select a head, team and class; preview, keep/cancel and save demo-specific swaps. |
| Weapon and Camo Swappers | Replace supported replay weapons and apply native MWR camos. |
| Vision and skies | Apply available vision presets and select indexed native sky presets. Vision does not transplant another map's baked lighting or geometry. |
| Demo splitter | Choose a clip range using events, decoded player names and weapon icons. Older conversions may need reconverting for complete event data. |
| Classic sniper playback | Original COD4 action clips on remastered sniper weapons, late ADS zoom and flat scopes. |
| Video capture | Bundled FFmpeg capture support for H.264, H.265, AV1 and ProRes. |

## Your first replay

1. From the multiplayer lobby, open **THEATER**.
2. Use **Import Demos**, or select a COD4 recording and **Convert COD4 Demo**.
3. Select the H1 recording or conversion result, then **Play Demo**.
4. Open **EDITOR** to change the camera, scene or player appearance.

Converted demos use `.dm_h1`. A `.conversion.txt` report records conversion details. Original recordings are preserved. For console conversion:

```text
cod4convert "C:\Demos\example.dm_1"
```

Use `demostart` to enable native automatic demo recording and `demostop` to disable it. Browse the results in Theater. Keep original COD4 recordings: converter changes require reconversion, while many playback/editor fixes work with existing converted demos.

## Camera and playback controls

| Key | Action |
| --- | --- |
| Space | Pause/resume; start a prepared dolly cinematic |
| Left / right arrow | Seek backward / forward five seconds |
| Up / down arrow | Change playback speed |
| F1 | Toggle game HUD |
| F2 | Cycle camera modes |
| F3 | Open / close the native editor |
| F4 | Toggle timeline/cursor controls |
| F9 | Switch cursor/camera input |
| F10 | Advanced camera/capture editor |

Free camera uses **WASD + mouse**, **E/Q** for height and **Shift** for faster movement. **Shift + scroll** changes zoom; scroll changes roll. **K / middle mouse** adds a dolly marker. **J** prepares the first dolly frame and shows **“Press Space to play cinematic”**; press Space when ready. **L** clears markers while stopped. Free camera and dolly suppress player-name overlays.

Enable classic sniper playback with `cod4_snipers 1`. This supports standard-scope **M40A3, R700, Barrett, M21 and Dragunov** in supported replay paths. ACOG and normal live matches retain native behavior. Use `cod4_fov 1` for COD4-style FOV. See [snipers](docs/cod4-snipers.md), [FOV](docs/cod4-fov.md) and [Theater controls](docs/native-theater.md).

## SP Bots experiment — separate working WIP

The repository also preserves **SP Bots Preview 19**, an experiment for one player versus bots in local Private Match, followed by demo recording. It adapts selected campaign animations to MP agents and adds scripted movement, combat, cover, reactions and grenade behavior.

**SP Bots is not included in the standard v1.0 EXE.** Development is paused and it is not a complete campaign AI port. Poses, animation variety, decision-making, cover use, compatibility and stability need further work. Build the isolated overlay explicitly if you want to investigate it.

**[Complete SP Bots README: setup, build, behavior, limitations, evidence and implementation](experiments/sp-bots/README.md)**

## Known limitations

- Conversion depends on the original map, mod, protocol and recording. Custom assets and unknown mappings may be unsupported; read the conversion report.
- Converted recordings support up to 42 character identities. Larger source player indices are unsupported, and expanded-slot visual coverage needs more testing.
- Helicopter combat/damage stages, some sound events and killstreak inventory HUD remain incomplete.
- Native portraits are only available where the installed game provides them. Vision presets and sky extraction depend on compatible installed assets.
- Hooks target the supported MWR executable/data layout. Other game revisions and client forks are not guaranteed compatible.
- Build and automated test passes do not establish visual parity for every demo, model or map. Please report reproducible problems with the release version, timestamp and relevant logs.

## Build from source

Use Windows, Git, Visual Studio 2022 with the C++ desktop workload, and a Windows SDK. Clone with submodules and prepare the required runtime inputs before generating the project:

```powershell
git clone --recurse-submodules https://github.com/Politohh/H1MVM.git
cd H1MVM
```

Follow [CONTRIBUTING.md](CONTRIBUTING.md) and the [runtime bundle build instructions](docs/single-file-release.md#building). Original asset inputs are not included in a source ZIP. Public CI runs offline regression checks; a complete release build additionally requires verified runtime inputs. Release packaging rejects dirty trees, untagged builds and stale embedded payloads.

## Credits and license

- [Aurora H1-Mod](https://github.com/auroramod/h1-mod) — base client.
- [s1-mod](https://git.alterware.dev/alterware/s1-mod), [h2-mod](https://github.com/alicealys/h2-mod) and [momo5502](https://github.com/momo5502) — upstream work and research.
- Dependency authors — see their license notices, also retained in the runtime bundle.

Client source is licensed under [GPL-3.0](LICENSE). Bundled dependencies retain their own licenses; the capture encoder's build recipe is documented in [capture-ffmpeg.md](docs/capture-ffmpeg.md). Call of Duty and Modern Warfare Remastered belong to their respective owners. H1MVM is an independent community project.
