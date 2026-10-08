# Single-file Theater client

Put `h1-mod.exe` in your existing Modern Warfare Remastered game directory and launch it.
The original MWR game installation is still required. No separate client support pack,
COD4 installation, Python, or FFmpeg installation is required by this release.

The EXE contains the Theater UI (including Model Swapper and Weapon Swapper), all
catalogued COD4 facial source clips/rest rigs, the five classic sniper animation sets
and scope, 45 sky presets, and FFmpeg for movie capture. Game weapon/kit models and
portraits continue to come from the installed MWR game assets; this package does not
create portraits that are absent from those assets.

On first launch the EXE verifies and extracts its support files into
`%LOCALAPPDATA%/h1-mod/bundles/<content hash>/`. Allow roughly 1 GB for the cache in
addition to the EXE. Subsequent launches verify the cache and repair missing or altered
packaged files from the EXE. Older caches are retained for rollback. Demos, saved swaps,
capture output and user settings remain outside the bundle and are not overwritten.

Bundled UI packages take precedence over obsolete copies from older client installs.
Unrelated user UI packages still load. Built-in source assets always use the verified
cache. An existing valid custom FFmpeg path is respected; otherwise capture uses the
bundled encoder. Custom sky files in `demos/theater/skies` can extend the preset list.

For a stand-alone integrity check, run `h1-mod.exe -bundle-check` in any writable
directory. It writes `bundle-check.json` and exits without loading MWR. Developers can
set `H1_RUNTIME_CACHE` to an absolute disposable directory to test a fresh cache.

## Building

Prepare the pinned facial and sniper sources with the existing tools in this repository.
Provide an inputs JSON containing `facial` (directory with `raw/` and
`facial-source-provenance.json`), `snipers`, `scope`, `scope_sha256`, `skies`,
`sky_manifest`, `ffmpeg` (static Windows binary), and `ffmpeg_sha256`.
The FFmpeg distribution must include its `LICENSE` and `README.txt` beside `bin/`.
Run `python tools/pack-runtime-assets.py --configure inputs.json`, then generate and
build the Visual Studio solution normally. Machine-specific paths stay in ignored
`build/runtime-assets-inputs.json`. Every build validates every required input against
its provenance; missing inputs cannot silently become an incomplete release.

The binary payload is deterministic: a versioned manifest and individually compressed
files embedded as a Windows resource. Its manifest records every path, length and
SHA-256. Source provenance and third-party notices are also inside the payload under
`provenance/` and `licenses/`. FFmpeg's included README identifies the build, license,
configuration and upstream source revision. The client source is published in this
repository at the corresponding release tag.
