# Single-file Theater client

Put `h1-mod.exe` in your existing Modern Warfare Remastered game directory and launch it.
The original MWR game installation is still required. No separate client support pack,
COD4 installation, Python, or FFmpeg installation is required by this release.

The EXE contains the Theater UI (including Model Swapper and Weapon Swapper), all
catalogued COD4 facial source clips/rest rigs, the five classic sniper animation sets
and scope, a small index of 45 native sky presets, and FFmpeg for movie capture. Game weapon/kit models and
portraits continue to come from the installed MWR game assets; this package does not
create portraits that are absent from those assets.

On first launch the EXE verifies and extracts its support files into
`%LOCALAPPDATA%/h1-mod/bundles/<content hash>/`. Allow roughly 65 MB for this cache in
addition to the EXE. Subsequent launches verify the cache and repair missing or altered
packaged files from the EXE. Older caches are retained for rollback. Demos, saved swaps,
capture output and user settings remain outside the bundle and are not overwritten.

Bundled UI packages take precedence over obsolete copies from older client installs.
Unrelated user UI packages still load. Built-in source assets always use the verified
cache. An existing valid custom FFmpeg path is respected; otherwise capture uses the
bundled encoder. Custom sky files in `demos/theater/skies` can extend the preset list.

The capture-only FFmpeg keeps AV1, H.265, H.264 and ProRes capture with the existing
MP4/MKV/MOV options. It omits unrelated codecs and tools. The 4096x4096 scope PNG
is repacked losslessly; decoded color and alpha pixels are unchanged.

Sky pixels are not shipped in the EXE. Selecting a stock preset reads only its indexed
compressed blocks from the installed MWR `.ff` file on a background worker, verifies
the exact pixel hash, and caches the result in `%LOCALAPPDATA%/h1-mod/sky-cache/`.
The original sky remains visible until the selected sky is ready. The editor shows
preparation or missing-game-file messages. Later selections reuse the verified cache;
corrupted entries are rebuilt from the game. Caching all 45 skies uses up to 692 MB,
but only selected skies are extracted. Missing map files leave the original sky intact.
No Python, network download, full map load, or restart is needed for extraction.

For a stand-alone integrity check, run `h1-mod.exe -bundle-check` in any writable
directory. It writes `bundle-check.json` and exits without loading MWR. Developers can
set `H1_RUNTIME_CACHE` to an absolute disposable directory to test a fresh cache.

## Building

Prepare the pinned facial and sniper sources with the existing tools in this repository.
Provide an inputs JSON containing `facial` (directory with `raw/` and
`facial-source-provenance.json`), `snipers`, `scope`, `scope_sha256`,
`ffmpeg` (static Windows binary), and `ffmpeg_sha256`.
The FFmpeg distribution must include its `LICENSE` and `README.txt` beside `bin/`.
Additional files under its `licenses/` directory are also embedded. See
`docs/capture-ffmpeg.md` for the smaller capture-only build and validation recipe.
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

The committed `data/sky-presets.json` contains block offsets, native image descriptions
and SHA-256 values, not texture pixels. To regenerate it for a supported MWR data build,
run `tools/index-theater-skies.py <game> <verified-sky-manifest> data/sky-presets.json`
using a development Python environment with `lz4`. The generator verifies all extracted
pixels against the original preset manifest before writing an index. An unsupported
archive version or pixel mismatch is rejected at runtime rather than displaying bad data.
