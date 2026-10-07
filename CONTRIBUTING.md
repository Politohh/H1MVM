# Contributing

This fork focuses on H1 demo playback, COD4 conversion and cinematic tools. Keep changes focused, describe the recording or workflow they affect, and include the checks you ran.

## Build from source

Requirements: Windows, Git, Visual Studio 2022 with **Desktop development with C++**, and a Windows SDK.

```powershell
git clone --recurse-submodules https://github.com/Politohh/h1-cod4-demo.git
cd h1-cod4-demo
.\generate.bat
```

Open `build/h1-mod.sln` and build **Release / x64**, or run this from a Visual Studio developer shell:

```text
msbuild build\h1-mod.sln /m /p:Configuration=Release /p:Platform=x64
```

Output: `build/bin/x64/Release/h1-mod.exe`. With H1 closed, back up the installed client, replace the executable, and copy the contents of `data/cdata/` into the game's `h1-mod/` folder. The TLS helper is embedded in the executable.

For an existing checkout, run `git submodule update --init --recursive` before generating. To reproduce a published release, check out its tag first. GitHub's source ZIP does not include the dependency submodules.

## Source map

| Location | Responsibility |
| --- | --- |
| `src/client/component/demo/` | Demo recording, playback, conversion, cameras and native Theater integration |
| `data/cdata/ui_scripts/theater/` | Native library, timeline, editor, match HUD and pause menu |
| `tools/` | Project generation, local asset preparation and installation utilities |
| `tests/` | Offline decoding, mapping, animation and UI regression checks |
| `docs/` | Feature guides, implementation evidence and historical research |

## Validation

Run the checks relevant to your change from the repository root. Examples:

```text
tests\test-theater-library.cmd
tests\test-native-theater.cmd
tests\run-cod4-match.cmd
tests\run-cod4-snipers.cmd "C:\Assets\cod4-snipers"
```

The sniper check requires [prepared original inputs](docs/cod4-snipers.md). Other fixture-based runners may need recordings or captured game data that are not distributed. See [test fixture notes](tests/fixtures/README.md).

Report build, offline fixture and in-game results separately. A passing decoder or animation test does not establish that a native model looks correct in playback. Preserve supported native behavior outside converted demos and avoid substituting unrelated assets for unknown mappings.

## Reporting a problem

Include the release or commit, game/map, native H1 versus converted COD4 recording, reproduction steps and the exact demo timestamp. For conversion failures, include the adjacent `.conversion.txt` report. For visual problems, a screenshot or short clip is useful; for sniper problems, include `cod4_snipers_status` output and the active toggle values.

Do not upload game assets or a recording you do not have permission to share. A small reproducible description and relevant logs are often enough to start an investigation.
