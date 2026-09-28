# Current workflow: convert to native H1 demo

The earlier separate `cod4cam` preview is superseded for the user workflow.
Use F9 -> Convert COD4 demo -> Convert to .dm_h1, then Refresh the regular demo
list, select the generated file and press the regular Play button. Normal H1
demo controls own pause, speed, seek, replay and theater camera controls.
An existing demo is preserved: subsequent conversions use the first unused
numbered filename, such as `cod4_to_H1.0001.dm_h1`. Existing temporary files are
also preserved. The conversion status shows the filename that was saved.
Ordinary .dm_1 launch arguments no longer queue playback on Play Online.
The startup conversion shortcut has been removed. Launch H1 normally and complete
Play Online initialization, then explicitly convert using F9 or `cod4convert`.
Conversion requires completed online initialization, initializes H1's map/assets
as a client, writes the file, then disconnects.
It does not play the source recording or start a local match. The source file is
not required when playing the converted file.

Latest validated fixture output before the glass correction:
`demos/client/auto/cod4_to_H1.dm_h1`, 2426945 bytes,
2241 snapshots, 33585..56385 ms, 22.8 seconds. All container chunks, zlib payloads,
packet sequences, monotonic snapshot times, duration footer and EOF passed
`tests/validate_converted_demo.py`. The native engine ammo getter matched translated
clip counts during conversion, including the full Deagle seven-shot sequence.
Saved-file playback was checked in a fresh H1 process through the normal Play
Online -> F9 -> Play workflow. Cars, the Crash helicopter wreck, recorded player
body/head and world weapon were visible. The user supplied screenshots confirming
the restored map props. Camera controls and holding at EOF work. This remains
partial conversion. Third-person animation and projectile translation were added
in the builds below. The user confirmed grenade rendering and supplied animated
body screenshots; body orientation and freecam LOD corrections follow below.
Other players, bullet effects and event sounds remain incomplete.

The file uses the normal H1 chunk format and native compressed gamestate/snapshots.
The standard descriptive header identifies converted demos and lists required
remastered model names. The installed client uses that metadata for client-only
bootstrap, asset residency and the neutral FOV multiplier; stock unmodified H1
clients have not been verified with these files. Snapshot contents do not embed
the COD4 source demo or depend on its decoder at playback time.

Previous glass/preservation build SHA-256:
`1655EBF8EA9D1E09F95D48CA137148EDE5E1AD30FF045FA40DD2A4DD6260F1B3`.
Release x64 build succeeded with zero warnings/errors; install hash matches.
This build adds recorded car-part visibility. The user reconverted the sample as
`cod4_to_H1.0001.dm_h1` (2512364 bytes) and confirmed the glass fix works.
The preceding demo is preserved in
`build/install-backups/20260928-134322-before-glass/cod4_to_H1.dm_h1` (SHA-256
`78FBEED751F12C2E4A488E5095DD58D1EC23389DFFC5F69088B012C9DF41F88F`).
It was restored to its usual game folder after the user reported its absence;
the restored file's hash matches that backup. Launcher installation now copies
the demo into the rollback backup while leaving the original in place. The
filename-preservation build passed with zero warnings/errors and is installed.

## World models and native replay UI (2026-09-28)

- The COD4 decoder retains entity baselines, delta updates, removals and clients.
  The fixture has 405 source snapshots, 11 recorded destructible cars, and one
  USMC sniper client.
- Recorded model ordinals resolve through the source configstrings. H1 asset
  availability and model residency are checked before exporting. High-index
  model bindings are saved in the converted header for a fresh process.
- Script movers use H1's model-index field at entityState+172. All position
  trajectories are protected with the native trajectory helper before the
  native network writer serializes them. A prior generic-index implementation
  and a prior plaintext-trajectory implementation were rejected after testing.
- H1's authored map entities add 28 permanent remastered props absent from the
  COD4 recording, including both helicopter wreck pieces. Recorded props are
  deduplicated by model and position. Gametype objectives are excluded.
- Recorded body/head identity maps to `body_usmc_desert_sniper_mp_camo` and
  `head_usmc_desert_sniper_mp_patch`. The model is visible; its full-body pose
  is not yet correctly translated. Native world-weapon furniture supplies the
  M40A3 scope. The new animation translation is described below.
- Native replay UI suppression resolves the actual omnvar names and the PC
  snapshot layout (global records +0x4A24, player records +0x4540), updating their
  timestamps when needed. The former fixed-index implementation corrupted
  unrelated state. A full 69-second native H1 demo and backward seek were checked
  without the unwanted loadout/class screen. Its recorded opening countdown
  remains visible.

## Car windows and recorded part visibility (2026-09-28)

The user reported that otherwise intact cars showed shattered windows. The source
demo actually hides ten damaged glass/light variants using COD4 partBits
`0000242A 92480000 00000000 00000000`. The converter had discarded these masks.

Live model inspection found 48 bones in each original red/brown/green sedan and
51 in its remastered counterpart. H1 inserts `tag_driver`, `tag_passenger` and
`tag_trunk_strap`, so copying source bit ordinals would hide the wrong parts.
`cod4_part_translation.hpp` retains the verified source bone order and resolves
destination ordinals by native bone name after residency. Each recorded snapshot
then translates its own mask, including later damage changes. Unknown hidden
parts fail explicitly rather than silently dropping visibility.

H1's entity-update function at RVA 0xEAC70 supplies centity+0x214 to the native
DObj hide-parts setter at 0x5C40B0, which copies eight words. With entityState at
centity+0x13C, the destination snapshot field is +216. For the intact fixture the
translated mask is `00001215 42490000` followed by six zero words.

Tests pass across all 405 fixture snapshots and all recorded cars. Additional
cases exercise a shot through one window, reversed destination bone order,
missing hidden bones, missing mapping and out-of-model bits. A recorded glass
damage/explosion fixture remains pending. The user confirmed intact windows
render correctly in the regenerated demo.

Original damage-state semantics were checked against Infinity Ward's
[COD4 mod tools scripts](https://github.com/cod4mw/CoD4-Mod-Tools/blob/95c76d6a470bea43fdd5ab591afe8475fac7699d/raw/maps/mp/_destructible_types.gsc).
Reference commit: `95c76d6a470bea43fdd5ab591afe8475fac7699d`. Only state/model/bone
relationships are used; COD4 visual assets are not installed into H1. The mod
tools multiplayer animation tree and player-animation script are retained as
research references for the remaining body-animation work.

The game is left closed after installing the glass build. Normal startup remains
unchanged. Reconvert the sample through F9, then refresh and play the resulting
new numbered file; the restored older file cannot acquire the new masks merely
by opening it with the updated launcher. Computer Use was stopped with Escape;
source/build/install work continued without additional game inputs.

## Native MWR projectiles and body animations (2026-09-28)

The converter translates recording data into H1 snapshots. Models and animation
clips come from the installed MWR assets. It does not export COD4 animation data
or require another COD4 executable for conversion or playback.

- The source recording contains three missiles: stun entity 317 at
  46950..48700 ms, frag 319 at 48500..51800 ms, and stun 321 at
  49600..51300 ms. All 138 missile snapshot states are retained.
- Native MWR weapon definitions were inspected in an isolated local H1 dedicated
  reference process. Their projectile model pointers resolve to
  `wpn_h1_grenade_frag_proj` and `wpn_h1_grenade_concussion_proj`. The converter
  requests these models and saves their residency metadata with the demo.
- Missile identity, owner, ground sentinel, native weapon ID, launch clock and
  spin are translated into H1's missile fields. Recorded bounces and removals
  remain driven by source world snapshots. COD4 gravity is 800 and H1's native
  trajectory evaluator uses 900. Each archive sample therefore contains the
  recorded position and instantaneous velocity as an H1 linear trajectory,
  preserving the source flight rather than resimulating collisions in MWR.
- H1's loaded BG animation table has 1709 rows in the inspected reference.
  Animation names are resolved again in the converting H1 client; native indices
  are not hardcoded. Recorded locomotion and weapon states select MWR's idle,
  directional walk/run, sprint, firing, reload, melee and grenade pull/throw
  clips. The fixture resolves 23 native clip names over all 2241 archive poses.
- Movement comes from the local archive because server snapshots omit local
  prediction velocity. Leg and torso fields, recorded restart toggles, timers,
  movement direction and translated sprint/stance flags are written into the
  native player state. Unsupported native clip names fail conversion explicitly.

The projectile/body fixture test checks all three lifetimes, independent
ballistic positions/velocities, bounces, stationary rest, removal, backwards
sampling, native fields, action coverage, missing clips and reordered native
animation indices. Existing world/glass, ammo/ADS and camera tests also pass.
The Release x64 build passed with zero warnings and zero errors. The installed
launcher matches SHA-256
`D76E971B7EFAD0072D4813DD0C3D1075B4E7FD6CA00E7388DA2FE29FB592AC1E`.
Rollback EXE, copies of both existing demos and the install manifest are in
`build/install-backups/20260928-150521-before-projectiles-body`. Both original
demo paths were checked after installation and retain their prior hashes.

The user confirmed projectile rendering on the D76E build. Animated body
screenshots exposed incorrect facing. The new heading/LOD build below still
needs a normal F9 playback check. Explosions, stun effects, bounce sounds and event sounds
are not translated by this change. No visual runtime acceptance is claimed from
the native asset catalog or the offline fixture tests.

## Body heading, theater LOD and r_forceLod (2026-09-28)

- COD4 archives store `movementDir` as signed relative yaw degrees. Native H1
  stores absolute yaw quantized to a byte in player state +0xC8, player entity
  +0xA8 and the prediction archive. The converter now translates both the PS
  and archive values. Camera angles and recorded backward/sideways facing stay
  intact. The sample at source time 37875 has yaw 147.1893 and relative heading
  17: H1 receives 117 (164.53125 degrees), replacing the incorrect raw 17.
- H1's native encoder at 0x2B4930 uses the live constants 0.703125 (half step),
  1/360, 360 and 256. Its player-state-to-entity conversion at 0x2C44A5 copies
  +0xC8 into player entity +0xA8. Evidence is saved under `build/analysis`.
- CG_DrawActive sets dynamic-model LOD parameters at 0xF6052, before calculating
  the render view at 0xF65AD. The old freecam only replaced that later view.
  The native R_SetupLodParams helper (0x1C7380), shared by dynamic/static scene
  setup, now receives the theater origin and FOV during armed freecam/dolly
  playback. Normal first/third-person selection uses the original native view.
- Native `r_forceLod` is an enum: `high`/0, `medium`/1, `low`/2, `lowest`/3,
  `lod4`/4, `lod5`/5 and `none`/6. MWR's XModelSetLodDist at 0x5DC5D0 is empty.
  The MP renderer now applies the chosen enum at native XModelGetLod selection
  (0x5DC430), clamping to the model's available levels. `none` retains native
  distance selection/culling. The visual setting is usable without server cheats.

Release build `build/build-body-facing-lod.log` passed with zero warnings/errors.
The installed EXE matches SHA-256
`05FA7F9531C1BFFF814F2674AF2B2D141FA169EEF2AA543E2D51FC0E0A8135F8`.
Rollback EXE, copies of all three existing demos and the install manifest are in
`build/install-backups/20260928-153318-before-body-facing-lod`. All existing demo
paths and hashes were preserved. Body heading tests cover all 2241 archive
samples plus wrap, negative yaw, backward motion and strafe cases; camera and
projectile/body fixture checks pass. The container validator can compare a new
file to decoded source archives while allowing only the body-heading translation.

Reconvert through F9 to create the next numbered file (currently expected
`cod4_to_H1.0003.dm_h1`) for the body correction. The LOD fixes apply at playback
and do not require reconversion. Computer Use ended with physical Escape;
source/build/install checks continued. Visual acceptance of these fixes remains
pending; the earlier grenade-rendering acceptance does not establish LOD parity.

Open H1 normally, enter Play Online, then use F9's conversion section with
`C:\Games\cod4_game_files\main\demos\cod4_to_H1.dm_1`. The next unused filename
is expected to be `cod4_to_H1.0002.dm_h1`. Refresh and play that new file through
the ordinary demo controls. Existing converted files remain in their usual
folder and keep their original content. Startup and Play Online remain normal.

---

## Historical implementation notes

# COD4 camera preview in H1

The project goal is to replay original COD4 recordings with MWR's remastered maps,
weapons, characters and effects. The original demo supplies state; H1 supplies assets
and rendering. This first milestone implements the recorded position and view angles.

## Development status — 2026-09-27

The first prototype loaded a local match and overrode its camera. The user confirmed
movement on remastered Crash, but rejected the match-based approach. That result
proves camera decoding/render coordinates only; it is not accepted demo playback.

The replacement initializes a replay client, translates source map metadata, and
feeds camera keyframe snapshots through H1's snapshot reader. It suppresses outgoing
gameplay packets and checks that `SV_Loaded()` is false. The installed test build
reached CA_PRIMED (9), accepted its first snapshot at source time 33585, then reached
CA_ACTIVE (10), with `server=0`. Its trace covered the full 22.8-second recording.
The user confirmed "it worked !" after this client-only test. This accepts the
camera milestone, not full COD4 demo support.

Accepted camera-only executable SHA256 (rollback build):
`BFE3035748128DA52839477113F72C230987603DA52306D8CE397D2377822998`.
Source is currently uncommitted on top of `bec18e5`.

### Weapon and FOV work

Playerstate decoding identifies Deagle/M40A3 switches and ADS. Weapon names are
resolved through the COD4 table and translated to H1 names, then checked against
H1's live registry. Initial runtime tests still showed no guns: model inspection
found a one-model DObj containing only `viewhands_h1_usmc_desert`, despite valid
weapon indices, first-person mode and `cg_drawGun=1`.

The current implementation requests each weapon's native gun model and the hands
through H1's transient-model system, balances those requests on stop, and waits
for readiness before advancing playback. Runtime acceptance is pending. It does
not force hidden models to render. Native PC entry points used: model request
`0x75720`, readiness `0x75470`, release `0x76850` (RVAs).

The fixed 65-degree replay FOV and manual replay FOV slider have been removed.
H1's FOV routine also multiplies by a gameplay omnvar that was zero in our
synthetic state. The first native-FOV test therefore rendered 1.33331 degrees
horizontal despite `cg_fov=80`, `cg_fovScale=1.125`. During the native view
calculation only, the replay supplies a neutral value of 1 for this multiplier
and restores it afterward. H1 retains control of both FOV components, client
settings, aspect handling and ADS zoom. Runtime confirmation of this correction
is pending.

The transient requests were verified in the subsequent runtime log: the Deagle
DObj now contains hands plus `wpn_h1_pst_de50_vm_camo`; the M40A3 contains hands,
`wpn_h1_sni_m40a3_vm_camo` and `reticle_h1_sni_m40a3`. The FOV regression prevented
visual acceptance of these models.

## Run

From H1's menu, press F9, expand **COD4 camera import**, paste the full `.dm_1` path,
and click **Load COD4 camera**. Alternatively use the in-game console:

```
cod4cam "C:\Games\cod4_game_files\main\demos\cod4_to_H1.dm_1"
```

The map comes from the COD4 gamestate. H1 uses its installed remastered map assets.
No H1 recording is required. Disconnect from
an online/private match before importing. Passing the `.dm_1` path to `h1-mod.exe`
also schedules import once the usual H1 online-data initialization completes.

Playback holds the last camera frame at the end. F9 exposes pause, restart, seek
and speed controls; those controls have not yet completed regression testing on
the new client replay path. Backward snapshot seeking and clean stop/disconnect
are still development work. Commands: `cod4cam_pause`, `cod4cam_seek <seconds>`,
`cod4cam_speed <0.05..4>`, `cod4cam_stop`.

## Scope

- Decodes COD4 type-0 packet framing, the initial Huffman gamestate/map name, and
  type-1 camera archives. Unsupported block types fail with an explicit error.
- A portable decoder produces a timestamped track. H1 samples it each render frame.
- Interpolates positions and shortest-path angles. Holds across teleports and gaps.
- Later archive records replace overlapping pre-roll samples after a clock reset.
- Uses MWR map geometry/materials. It does not export or import COD4 game assets.
- Eye height uses recorded view height with a manual adjustment. FOV stays under
  H1's native camera calculation, including client FOV settings and recorded ADS.
  Weapon action animations, other players and projectiles remain incomplete.
- The initial local-match prototype has been superseded by validated client-only
  camera playback. The generated playerstate is deliberately minimal; it is not
  a complete translation of source playerstate yet.
- Does not yet produce `.dm_h1` files. Source packet entities, clients and actions
  are not translated by this camera-only prototype.
- Writes sampled source positions/angles and applied render origins to
  `demos/cod4-camera-last.csv` in the game directory for comparison.
- Writes replay initialization and snapshot validation results to
  `demos/cod4-replay-last.log`.

## Exact fixture

`cod4_to_H1.dm_1` is 165,049 bytes, records `mp_crash`, and parses to the explicit EOF.
It contains 406 network packets and 2,281 archive records. Forty initial records
are replaced by the subsequent time reset; the resulting 2,241 samples cover
33,585..56,385 ms (22.8 seconds).

`tests/cod4_camera_reader.cpp` checks framing, exact sample fidelity, pre-roll handling,
interpolation, angle wrapping, endpoint clamping, teleport handling and malformed
input rejection. Build it as a standalone C++20 executable with `src/client` on the
include path, then pass the exact fixture path. The demo is not checked into Git.

## Next milestone

Decode player state alongside the archives: view height, ADS fraction, weapon
identity and action state. Map weapon names to H1 assets, then map the animation
state and recorded events. Weapon/entity indices must not be copied across games.

## Recorded weapon actions (2026-09-27)

The user confirmed remastered weapons/hands are visible after the FOV correction.
Runtime logs verify horizontal FOV 106.26 with client cg_fov=80 and cg_fovScale=1.125.
The accepted build is backed up before installing this animation experiment.

The player decoder now reads reserve/clip delta tails, preserving snapshot baselines.
Standard MP ammo/clip equivalence groups come from the local COD4 iw_11.iwd weapon
metadata; only those relationships are retained, not game assets. Unknown weapon
metadata is rejected. Source mods that change ammo grouping are not supported yet.
For this fixture Deagle resolves to reserve 11 / clip 14 and M40A3 to reserve 2 /
clip 31. Tests confirm the seven-shot Deagle clip countdown and initial 7/5 rounds.

The H1 bridge translates COD4 animation numbers and restart bit 0x200 into native
H1 numbers and restart bit 0x800. Destination meanings were verified in H1's
BG_WeaponAnimToAnimIndex (RVA 2E8E50). Player offsets 548 (animation), 552/556
(timers), 564 (state), 572 (shot count), and sprint fields were checked against
native code and the live PC netfield table. Melee charge currently uses H1's
standard melee swipe; a distinct charge presentation is not implemented.

Ammo uses native key resolvers 2DE400/2DE580 and records at offsets 956 (12-byte
reserve entries) and 1136 (24-byte clip entries). Native getter E9640 checks the
result in the trace. Equipped weapons, offhand selection and USING_OFFHAND are
also supplied. Bullet impacts, muzzle FX, weapon event sounds, other players and
projectile entities remain separate unfinished stages. This build needs visual
validation of the native action playback; successful decoding/building is not
visual acceptance. Test from a fresh playback, as backward seeking is unfinished.

Animation experiment build: SHA-256
`3F55C4109F4A728FA5A2A9E73FE9A29990B11363A7C8FEEE65D9E662BAACB67C`.
Release x64 build: zero warnings/errors; installed EXE hash matches.
Rollback: `build/install-backups/20260927-before-recorded-actions/h1-mod.exe`
(FOV-corrected build `D3E676BDD60DBFB971D423701C7530BE01970399ECC4A057313E0748E5400AD6`).
First startup hit the previously observed engine AV at RVA 11AD2C97 before playback;
retry opened H1 successfully. Runtime action validation is pending user playback.

## Startup flow correction

The `--convert-cod4` ten-second startup callback was removed after the user
reported the network-connection dialog. That callback had initiated map loading
before the normal Play Online initialization. The menu itself was not removed
from source. The client is now launched with no conversion/replay arguments;
conversion only runs on an explicit F9/console action after initialization.
Whether that early load caused the connection dialog has not been proven.

Startup-restored build installed and hash-verified: D9B5A1ACB68FAB6E70543695E82FC4178EF9E5146E1F28107D9C9EC805FCF719. Release build: zero warnings/errors. Relaunched with no arguments; initial Multiplayer / Play Online menu observed after more than 15 seconds. Online initialization and the reported network dialog still require user validation.

### Converted playback network-dialog investigation

- User screenshot showed converted demo controls at 00:08 / 00:22 while the menu displayed the network-connection error; viewport playback was not accepted.
- Found that native demo `CL_WritePacket` suppression began only after replay arming. The accepted preview suppressed packets throughout its offline bootstrap. Converted playback now suppresses packets from conversion-header recognition onward, including synchronous map loading. Normal demos retain their existing handshake behavior.
- Added `demos/converted-playback-last.log` for bootstrap/frame state, local-server/lobby/cgame flags, snapshot clock, draw calls and suppressed packets. This is diagnostic evidence, not a claim that the dialog is fixed.
- Build `build/build-converted-offline.log`: 0 warnings, 0 errors.
- Installed executable SHA256: `F5382CAB249E11807BEBDBC10B0028570A8C1909007C9322F499A39F650DE92F`; source/install hash parity verified. Previous executable retained in `build/install-backups/20260927-before-offline-packet-guard/`.
- Launched with no arguments and observed normal Play Online startup. F9 playback validation is pending the user's next run; no automatic demo launch was added.
- First runtime check of this build: bootstrap ended at CA_PRIMED with server=0, lobby=0, cgame=1 and onlineFlags=0; three outgoing packets were blocked during bootstrap. Subsequent logs show CA_ACTIVE and advancing snapshot/render clocks, over 4,900 draw calls with server=0 throughout. Capture showed Crash rendering. User reports a different problem; details requested. This confirms the map-rendering failure changed, not complete playback acceptance.

### Camera archives and end-of-demo hold

- User reported incorrect camera angles after the offline packet fix. Converted files had snapshots but omitted normal H1 predicted-player archive chunks. Export now writes one standard 50-byte player archive per source camera frame before its snapshot. The prediction getter preserves native failure when no matching recorded camera exists instead of unconditionally claiming success.
- Updated sample keeps all previous chunks byte-for-byte, adding 2,241 source camera archives. Independent fixture validation checks each archive timestamp against its native snapshot, ring indices, and all 48 payload bytes against the portable COD4 decoder output. PASS: 560,790 bytes, 22.8 seconds.
- Clean EOF now pauses on the final frame and keeps the demo, models and controls active. Resume restarts; rewind is a one-shot operation and no longer silently enables permanent looping. Explicit loop mode retains looping; parse errors still stop instead of being treated as completion.
- Build `build/build-camera-end-hold.log`: 0 warnings, 0 errors. Installed executable SHA256 `DBA687AF3F47115823D61A987F0CCFA0E923C1666D562B1B8E1CA216CA30958A`; sample SHA256 `31BEF20D658B161886C9BB5BE03055A2223A5D4B0E0F05AB458B67856FE62BF9`. Installation hashes match source artifacts. Both prior files backed up in `build/install-backups/20260927-before-camera-archives-end-hold`.
- Normal startup launched with no arguments. Camera alignment, final-frame hold, resume and rewind still require runtime confirmation of this build.

### Camera rotation root cause (live verified)

- Paused fixture at source time 38335: source angles `(1.2359619, 137.0544434, 0)` exactly matched H1 predicted player state at cg+0x12C. Final camera cg+0xDEE10 was `(0.331049, 274.120361, 0.841999)`.
- Native `0xEA4B0`, called by `0x10C16D`, reads a uint16 linked-view entity at ps+0x2C. Unless this is 2047, it looks up that entity and multiplies its rotation into the camera. Zero-initialized converted states unintentionally selected entity 0 (the player), doubling the rotation. This was not an archive angle-axis ordering error.
- Bounded live diagnostic writes of 2047 to that field produced `(1.235962, 137.054443, -0.0)` in the final native camera calculation while the recording remained paused. Regular prediction overwrote the diagnostic writes from the old snapshot afterward; no process code was patched.
- Converter now emits ENTITYNUM_NONE=2047 at ps+0x2C. Converted-v1 playback repairs the same field before the native camera calculation to support already-created files. Native demo files and normal game views are untouched. This repairs attachment state rather than rotating the render camera after calculation.
- Additional observation: clientActive snapshot PS begins at cl+8, despite older comments declaring cl+0; cg predicted PS is at cg+0. No unrelated structure changes were made in this fix.
- Installed build `build/build-camera-link-sentinel.log`: 0 warnings/errors. Source and installed executable SHA256 match `79649E8C822A3F7D8EE8DEB831B04EA492C8F9CD089C82DA857E06A1977DB079`. Backup: `build/install-backups/20260927-before-link-entity-fix/h1-mod.exe`. Relaunched normally, no automatic playback. Root-cause correction measured in the prior paused process; full new-build playback still awaits visual confirmation.

### 2026-09-28 scope, ADS and rewind investigation

- Live M40A3 WeaponDef confirmed gunModel `wpn_h1_sni_m40a3_vm_camo`, reticleViewModels `reticle_h1_sni_m40a3` and `reticle_h1_sniper_transparent`; none of its seven optional attachments is automatic. Export now requests the definition's gun and reticle view models instead of only gunModel[0]. Visual scope acceptance still pending.
- Recorded ADS was sampled with a previous-snapshot hold. Export now linearly interpolates fWeaponPosFrac at camera timestamps, preserving exact source samples and refusing interpolation across weapon changes or gaps over 250ms. Portable fixture checks passed all 30 changing intervals and 405 player snapshots.
- Inspected HorizonMW/HorizonMW-Client revision cf81621e85de85187b1d6f49c5aa0f4460fcab07 under work/hmw-reference. No 2D-scope toggle implementation was found in the checked source/data; no code copied. Native H1 scope remains the current target.
- Rewind error `cl->snap.serverTime < cl->oldFrameServerTime`: native CL_SetCGameTime at 0x33C950 compares snap time against cl+0x4A94, omitted by repeat(). Added reset. First runtime test then exposed CG_ProcessSnapshots latestSnapshotNum, confirming clock-only reset was insufficient.
- Native CG_ProcessSnapshots 0x32F750 reads cg+0x48E4 latestSnapshotNum, +0x48E8 latestSnapshotTime, +0x48F0 snap, +0x48F8 nextSnap. CG_ReadNextSnapshot 0x32F950 walks the real cgs+0x1C processedSnapshotNum. Reset these together to force native first-snapshot initialization; also clear recorded prediction history and the real cgs command counter.
- First scope/ADS/clock build installed SHA256 30D374FFCB57AB55FDAC24EC76D3899B4F145A325788467A1DB18A7366152187, zero warnings/errors. Previous exe and sample retained under build/install-backups/20260928-before-scope-ads-rewind.
- Explicit console conversion after normal Play Online initialization regenerated the sample: 558541 bytes, SHA256 AE7434589396507997F7F6098A210C3C813B2549B64C829EB2B8EF490A08E601. Independent container + exact source camera archive validation passed, 2241 snapshots / 22.8s. Output copy updated.
- Conversion completion still displayed the network dialog after its disconnect; dismissed to continue F9 testing. F9 playback rendered Crash with server=0 and held EOF. Rewind on first build failed at the additional CG counter check, as noted above. Do not count this first build as rewind acceptance.
- Revised snapshot-reset build installed with hash parity: 1C4F480522B67EE0005A6C9C9F3A27CDEF20CB9CF03CCA480FB4539C93CB1973. build/build-scope-ads-rewind2.log: zero warnings/errors. Live check pending.
- Second runtime test exposed old snapshot-ring entries after resetting CG counters. CL_GetSnapshot (0x33B940) uses cl+0x8FA0 ring, count cl+0x8F84, stride 0x4A78, valid flag +0x4A24; it does not verify the requested sequence matches each entry. Added invalidation of each ring valid flag on restart. Live allocation had 4 entries/mask 3. Third build: 8E9F27037A4BA797340B4DD167A0802C7F1E8BD9DBBF8E6FFE0F676037FE4EB5, zero warnings/errors and installed hash parity.

### Native M40 scope and live ADS/seek acceptance

- Missing physical scope was a native furniture-kit omission. HMW's custom_weapons.gsc uses `h1_m40a3_mp_a#none_f#base`; the base weapon resolver alone supplies the bare receiver. Native `mp/furniturekits/base.csv` selects the base and scope parts (default=1), including dry/wet view models. Converter now requests these models and registers the native kit with 0x2E2D50. Full handles: Deagle 131132, M40 262218 in this fixture.
- Persisted numeric kit configstrings at 4557+variant allow a fresh process to recreate the same native variants. Initial experiment also serialized base weapon netconstant strings as ordinary configs; H1 rejected that file with XBOXLIVE_CANTJOINSESSION. Removed those prohibited base entries. Rejected file is retained as `.rejected-constant-strings`; it is not the installed sample.
- Build `build/build-native-kit2.log`: zero warnings/errors. Installed/source EXE SHA256 A892A86127A519631CD32985A534B881CF123F2A8B462FA4FE40EE80033FF11F matched. Fresh process, normal Play Online then F9 Play successfully loaded the saved file without conversion/autoplay. Physical M40 scope and native scoped sight/crosshair both observed live.
- Regenerated sample: 567395 bytes, SHA256 F3D312C7D92EF80BD15D914620C245BF067554B85D1F358F56352A635743C13C. Game and user outputs copies match.
- Backward timeline seeks from EOF to ~8s, then ~2s, then EOF to ~5.7s completed without either snapshot-time error. Space resume from ~2s played to EOF and held on the map; runtime trace remained server=0/lobby=0/cgame=1.
- Read-only live predicted-player-state sampling: `build/live-ads-key-resume.csv`, 7707 samples. M40 commandTime 38835..39285 yielded 88 observations / 87 distinct ADS values, progressing smoothly through intermediate values to full ADS. This verifies runtime interpolation rather than only export math; subjective animation smoothness still requires user review. Portable reader fixture passed 405 snapshots / 30 changing ADS intervals.
- Found a separate UI collision: file Play and paused-timeline Play shared an ImGui ID. Timeline toggle now has `##toggle_pause`, preserving its visible label. Keyboard resume already passed; revised button build verification follows.
- Remaining scope: no HMW 2D-toggle implementation copied, no complete animation/FX acceptance claimed. Explicit conversion completion can still show the previously recorded network dialog; fresh saved-demo playback did not.
- Final UI build `build/build-scope-ads-controls.log`: zero warnings/errors; installed/source SHA256 4C71F5F59F0AA33024E79FF463EB01D9A5E0FCA6BB8D27D614B880316E4BCB9F. Previous native-kit executable preserved in `build/install-backups/20260928-before-scope-ads-rewind/h1-mod-native-kit.exe`. Normal startup and Play Online observed again.
- Final live UI check: timeline Play restarted EOF twice; Pause at ~12s changed to Play. Fresh saved-file playback remained client-only with no network/error modal.
- Timeline Play resumed the paused ~12s state and changed back to Pause on the final build. The duplicate-ID fix is live verified.
