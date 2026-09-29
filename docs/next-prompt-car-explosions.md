# Continue native MWR car animation and debris validation

Work in `F:\Dev\h1-cod4-demo` on `main`, private origin
`https://github.com/Politohh/h1-cod4-demo`. Read Git status and
`docs/cod4-mapping-coverage.md`. Preserve unrelated untracked research. This
is a historical car-animation handoff; the current installed build and remaining
HUD work are summarized in the mapping-coverage document.

Primary source: `C:\Games\cod4_game_files\main\demos\car_explo_to_h1.dm_1`,
SHA-256 `E220C6065C5E39495547C4DF3DEDE3569F32828ACE96DA4D4B4694A85538A0CB`.
The destroyed-model fix now converts successfully (7,990 snapshots, exact source
camera validation), and the user confirmed the cars explode with destroyed
models visible. It expands the source bone catalog to 60 verified profiles and
ignores retained mask tails outside the current source skeleton; active source
bones still require a native name match. Four unavailable debris identities
remain omitted: brown wagon front/back bumpers and silver/yellow sedan wheels.

Native car FX is implemented and installed. The source has four actual
`EV_PLAY_FX` (55) records for `explosions/small_vehicle_explosion`, source effect
14, entities 95/149/250/342 at 106850/117800/124800/124800 ms. No explosion is
inferred from a model change. MWR's matching resident asset is
`fx/explosions/small_vehicle_explosion`, verified by read-only inspection of live
Backlot both in playback and after the user freshly loaded the normal map. It is
not in that map's scripted FX table. Standalone `@fx:511:<name>` metadata binds
it only during native EV_PLAY_FX (111) dispatch, restoring the table slot and
client clock immediately; `@carfx:<entity>:<time>` metadata shares the existing
seek/reset/age handling with grenade events. H1 CG_PlayFX is at 0x114360 and the
512-entry native FX table is at 0x143909D00. Native eventParm is at state +36;
position +108, angular base +144, eType 131. Scope the handler to converted,
tracked event identities; ordinary gameplay and native demos must pass through.

The user reconverted `car_explo_to_h1.0001.dm_h1` and accepted the explosion FX.
They reported laggy lift/drop animation and missing detached doors/wheels.
Read-only inspection of a normal Backlot match verified native ScriptableDef
death states for the yellow/silver sedan and brown wagon. All three use
`vehicle_80s_sedan1_destroy` (60 frames, 30 fps). See
`build/analysis/native-car-defs.json` and `native-car-defs-summary.txt`.

`cod4_car_motion.hpp` associates four explicit recorded car explosions with
their destroyed bodies. The converter removes the COD4 body pop, writes native
H1 TR_ANIMATED 11, original placement, clip start and native duration, and binds
`@anim:254:vehicle_80s_sedan1_destroy`. The existing configstring hook supplies
native animation name at CS 4302 (0x10CE)+index; slot 254 binds CS 4556.
Native animation controller 0x2F28E0
uses entityState +184 index/+188 start and owns its simple XAnim tree.

The old script-model translator copied incompatible trajectory numbers.
Native evaluator 0x2C2750 and PhysicsLaunchClient 0x453FB0 verify source
acceleration 6 -> H1 7, deceleration 7 -> H1 8 and client physics 8 -> H1 9.
Source position delta is a launch point; angular delta is force, both preserved.
H1 11 is animated root motion, not client physics. `tests/run-cod4-car-motion.cmd`
passes four body clips, 31 recorded launches / 7,591 physics states and 1,111
body animation states. Source launch counts include unavailable parts. Sedan
wheels and wagon bumpers remain unresolved; the native wagon wheel has different
authored geometry and is not a verified sedan substitute. Do not claim these
four omitted identities fixed.

The first animation build failed the user's `.0002` playback with "Unknown
attachment vehicle_80s_sedan1_destroy for custom weapon". The hook's wrong
base 4310 plus slot 255 yielded CS 4565 in custom weapons. Native weapon base
is 4557 (0x11CD). Corrected animation binding uses 4302+254=4556, limits the
lookup and metadata indices to 1..254, and includes a production-helper
regression check that animation names never reach weapon slots. The old
experimental `@anim:255` file now requires reconversion. Its 7,990 snapshots,
camera archives and 32.2 s footer independently validate; playback still failed.

Subsequent gun firing work is now installed on top of this correction:
`2A7574330343AAAB4F4A4B5B71ADCE327F954E6672AFEC4D81D44C53B546A3EA`.
Its rollback is `build/install-backups/20260929-024312-before-native-gun-fx`
with 28 files preserved. See `docs/next-prompt-fx.md` and `gun-fx-installed`
for pending Backlot native gun FX validation. Car runtime acceptance is still
pending; the facts below describe the corrected car-only build.

Corrected car-only EXE SHA-256:
`C3761FF381C96E6DB53691D86FDCF8F68DB1FFDC4FA742AC0172229C3A004BEA`.
Immediate rollback to the failed animation build:
`build/install-backups/20260929-021515-before-car-animation-binding-fix`,
including all 24 current demo/report files. The user-accepted FX executable
remains in `build/install-backups/20260929-015828-before-native-car-motion`.
Installed EXE, backup and preserved-file parity pass; zero source manifest
mismatches. Corrected evidence:
`build/analysis/car-animation-binding-fix-installed`. Original failed animation
build evidence remains in `build/analysis/car-motion-installed`.
Release x64 build, car motion/FX tests, fresh world/projectile/body, native grenade
regressions and destroyed-model checks pass. The car collector passes ten
readable demos with 21 explicit car FX events; eight known reader failures remain
separate. Focused runner: `tests/run-cod4-car-fx.cmd`.

At this handoff the corrected executable is installed with MWR closed. The
next runtime check is to relaunch, F9 reconvert the source,
Refresh and Play the newest numbered car recording, checking smooth body motion
and detached doors/hood. Runtime animation/debris acceptance is pending. Inspect
ordinary conversion and playback logs, native animation state and client physics
actors before claiming success. Native physics reconstruction after a seek is
not established by offline checks.
The game is at `C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Modern
Warfare Remastered`; converted files are in `demos\client\auto`. Existing converted
files retain their old payload. Preserve successful conversion outputs, reports,
logs and source-camera validation evidence separately from build-only evidence.

Separate EV_PLAY_FX_ON_TAG damage/burning records are currently not translated.
Do not invent perpetual burning or expand into firing/bullet/smoke work without
user direction. Native grenade frag/flash/concussion mappings and native model,
glass, projectile, body, camera, pause/seek/freecam/end-hold behavior must remain
intact. Use recorded FX identities and native MWR assets only.

Do not attach a live debugger. Read-only process inspection is available. Use the
Computer Use skill for UI interaction; physical Escape means stop UI input. Build
Release x64 `client` in `build\h1-mod.sln`. Install only while MWR is closed,
back up the executable and all recordings/reports, and verify parity. Update
coverage with observed runtime evidence; do not equate a build/test pass with
visual acceptance.
