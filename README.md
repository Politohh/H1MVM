# H1-Mod Demo Client

**Record in H1. Replay COD4. Create cinematics in Modern Warfare Remastered.**

[![Latest release](https://img.shields.io/github/v/release/Politohh/h1-cod4-demo?label=download)](https://github.com/Politohh/h1-cod4-demo/releases/latest) [![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D4)](#installation) [![License](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)

An **H1-Mod client with an integrated demo system and a COD4-to-H1 demo converter**. Bring original Call of Duty 4 recordings into MWR, replay them with remastered models and effects, and compose shots using native Theater controls.

**[Download](https://github.com/Politohh/h1-cod4-demo/releases/latest)** · **[Documentation](docs/README.md)** · **[Changelog](CHANGELOG.md)** · **[Report an issue](https://github.com/Politohh/h1-cod4-demo/issues/new/choose)**

## Made for replay and cinematic work

| Feature | What you can do |
| --- | --- |
| **Demo library** | Browse, import, convert and play recordings from MWR's native Theater menu. |
| **Playback controls** | Pause, seek, change speed and toggle the HUD independently of the timeline. |
| **Camera tools** | Switch between first person, third person, free camera and dolly paths. |
| **Shot editing** | Adjust camera, lighting, fog, depth of field and visual settings in the native editor. |
| **Model Swapper** | Select a replay player and replace their head or body with native MWR appearances, including models from other factions. Save selections for that demo. |
| **COD4 conversion** | Convert `.dm_1` recordings to `.dm_h1`, including tested Promod and COD4x protocol 19/21 recordings. |
| **Remastered presentation** | Map recorded players, weapons and events to native MWR assets, with recorded HUD data. |
| **Classic sniper playback** | Use original COD4 sniper animations on remastered models, with late ADS zoom and flat scopes. Requires separately prepared inputs. |
| **Video capture** | Export footage through an optional FFmpeg executable. |

This fork builds on [Aurora H1-Mod](https://github.com/auroramod/h1-mod). Conversion coverage varies by recording, map and mod; supported features and remaining limits are listed below.

## Installation

You need a supported installation of **Call of Duty: Modern Warfare Remastered**. Game assets and demo recordings are not included.

1. Download the ZIP ending in **`win64.zip`** from [Releases](https://github.com/Politohh/h1-cod4-demo/releases/latest).
2. Close H1 and back up your existing `h1-mod.exe` and `h1-mod` folder.
3. Extract into the game directory, next to `h1_mp64_ship.exe`. Include the bundled **`h1-mod` folder**; its scripts are required.
4. Launch through your **H1-Mod shortcut**.

The symbols ZIP is optional crash-debugging data. Published packages include SHA-256 checksums.

The latest packaged release is **2026.10.08**, adding COD4-style FOV, Theater Model Swapper and Demo Splitter, restoring character personalization and local Depot support, and fixing conversion of non-player obituary events. See the [changelog](CHANGELOG.md#20261008) for details and validation limits.

## Your first COD4 replay

1. Open **Play Online** and let initialization finish.
2. Open **THEATER**, directly below **Private Match**.
3. Choose **Convert COD4 Demo** and select a `.dm_1` recording.
4. When conversion finishes, choose **Refresh → H1 Demos**, select the result and **Play Demo**.

Use **Import Demos** to bring in several recordings at once. Original files are preserved. Converted demos are saved under the game's `demos/` directory, with a `.conversion.txt` report beside each result.

Console alternative:

```text
cod4convert "C:\Demos\example.dm_1"
```

Keep your originals. **Converter changes require reconversion; playback, UI and sniper animation fixes can use existing converted demos.** See the [library guide](docs/theater-library.md).

## Playback at your fingertips

| Key | Action |
| --- | --- |
| **Space** | Pause / resume |
| **← / →** | Seek backward / forward five seconds |
| **↑ / ↓** | Change playback speed |
| **F1** | Toggle game HUD |
| **F2** | Cycle camera modes |
| **F3** | Open / close the native editor |
| **F4** | Show / hide the timeline |
| **F9** | Switch between cursor controls and camera input |
| **F10** | Open the advanced camera / capture editor |

Free camera uses **WASD + mouse**, **E/Q** for height and **Shift** for faster flight. **Shift + scroll** adjusts zoom; scroll adjusts roll. For dolly paths, **K** adds a marker, **J** starts/stops playback and **L** clears markers while stopped.

For native H1 recordings, `demostart` enables automatic recording and `demostop` disables it. Browse recordings through **Theater → H1 Demos**. See the [playback and editor guide](docs/native-theater.md) for console commands and editor pages.

### Classic COD4 snipers

With the required local inputs installed, enable the mode during converted-demo playback or direct COD4 preview:

```text
cod4_snipers 1
```

Supports **M40A3, R700, Barrett, M21 and Dragunov** with standard scopes. Original action clips drive remastered weapons and hands; scene zoom begins at **95% ADS**, and the scope cuts away without an opacity fade. The mode is off by default and currently applies to replay, including preview. ACOG and regular live/devmap play keep their native behavior.

**[Setup, scope customization and compatibility](docs/cod4-snipers.md)**

Use `cod4_fov 1` for [COD4-style FOV](docs/cod4-fov.md): `cg_fov` changes hip weapon framing, while full ADS stays independent of that setting. `cg_fovScale` scales both hip and aimed views. This toggle also works in regular multiplayer and devmap.

## Compatibility and remaining work

- Custom mod assets and unknown mappings may be unsupported. Inspect the conversion report for omitted data.
- Converted recordings support up to **42 character identities** across native player and agent slots. Source player indices above 41 are unsupported; expanded slot coverage still needs broader playback validation.
- Helicopter combat events, damage stages and ownership/compass markers remain incomplete.
- General recorded sound-event playback and killstreak inventory HUD remain incomplete.
- Hooks and asset mappings target the supported MWR executable. Compatibility with other client forks or game builds is not guaranteed.
- Offline checks cover decoding, mappings and playback math. Exact visual parity still needs confirmation for each recording and native rig.

## Build and contribute

Use **Windows, Visual Studio 2022, the C++ desktop workload and a Windows SDK**. Clone with submodules, run `generate.bat`, then build **Release / x64**.

**[Build instructions and contribution guide](CONTRIBUTING.md)** · [Implementation](src/client/component/demo/) · [Regression tests](tests/)

## Credits and license

- [Aurora H1-Mod](https://github.com/auroramod/h1-mod) — base client.
- [s1-mod](https://git.alterware.dev/alterware/s1-mod) and [h2-mod](https://github.com/alicealys/h2-mod) — upstream codebase and research.
- [momo5502](https://github.com/momo5502) — upstream Arxan and Steam research.
- Dependency authors — see their bundled license notices.

Licensed under [GPL-3.0](LICENSE). Call of Duty and Modern Warfare Remastered belong to their respective owners.
