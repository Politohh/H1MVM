# COD4 to H1 mapping coverage

Updated 2026-09-29. Conversion produces a native `.dm_h1` recording using the
installed MWR models, attachments and animation clips. Use **Play Online → Theater →
Import Demos → Convert Demo**, then **Refresh → H1 Demos → Play Demo**.

## M16 M203 model requests (2026-10-04)

The Ambush conversion requested `attach_h1_launcher_m203_m16_npc` from the
native `glmwrm16` attachment and rejected it after transient loading. A read-only
inspection of the installed H1 catalog confirmed that the selected
`mp/furniturekits/base.csv` GL row instead supplies the M16 M203 handguard
models, including the native patch/camo and wet variants.

Model acquisition now distinguishes base-weapon furniture from selected
attachment furniture. It requests the kit's geometry instead of obsolete
attachment view/world models when that kit supplies the corresponding view.
Missing kit geometry still uses the attachment definition. Native reticles and
model-valued weapon fields, including `h1_projectile_m203_grenade`, remain requested.

The model-selection tests cover native M16 rows, dry/wet variants, remote world
models, unrelated attachments and absent or partial kit data. They pass, as does
the Backlot firing-event regression and Release/x64 build. The installed EXE/PDB
match the build; rollback and parity evidence are recorded in
`build/m203-install-validation.json`. Reconversion and visual playback are pending
the user's test; H1 was left closed after installation.

## Current installed build and HUD work

Release x64 executable SHA-256:
`9A77B7D6FFE0D6DBA5563B2B7A8E5FE4BD9BA2715CE27E78675D7F76BA59B23C`.
Installed with H1 closed. Rollback executable, 60 preserved demo/report files and
file-parity manifests are in
`build/install-backups/20260929-201055-before-hud-main`.

The converter now retains recorded player health and HUD elements. It writes
health to native H1 player stats and translates recorded `MP_PLUS` numeric
elements into H1's current HUD with a runtime-resolved MWR localized-string
index. The Strike source has 595 HUD frames and six recorded +5 popup updates.
Existing native keyed ammo translation remains in place. All 11 focused CoD4
regressions and the Release x64 build pass. This HUD build has not yet been
reconverted or visually checked in H1; match score/timer and other CoD4 HUD
elements are not mapped to native MWR UI state yet.

## Promod 5v5 stress test

Conversion and automated checks pass for ten players (5v5), all nine remote
players, eight corpse slots and 100 native animation clips. The 11.5-second
crash was caused by a missing archived corpse bit and is fixed. Native death
clips now play instead of snapping to their final pose; the user confirms
the correction. Recorded flesh impacts and incoming hits now use native MWR
events; see [the blood-impact report](promod-blood-fx.md) for runtime evidence.
The complete native replay passes 25 single impact dispatches, 14 native blood
allocations and seven PVS visibility culls. User blood visual acceptance is pending.
See [the stress-test report](promod-stress-test.md) for current validation,
remaining omissions and the preserved historical crash evidence.

Current built/installed EXE SHA-256:
`3F13EF4253A499916ED31CC6C705ECCC916BAE8FEE3A8C68FF81D33C0A6F5E82`.
Rollback: `build/install-backups/20260929-165754-before-promod-stress`.
The rebuilt Promod recording is `dEbilzuuR_2k_deag_r700_crash.0006.dm_h1`.
The 54 files present at installation were backed up and remain intact.

## Other players, corpses and weapon pickups

The City friend demo now serializes remote player entities, player corpses and
weapon pickups with native MWR character and weapon assets. Player positions
and angles interpolate between recorded server snapshots; teleport toggles,
entity removals and long gaps prevent blending unrelated poses. Movement
heading, lean, torso/waist pitch, firing, ADS and death flags use native fields.

The original COD4 1.7 process supplied its actual 318-entry animation table.
The loaded H1 process supplied its animation names, body-channel codecs and
PC player/corpse/item netfields. Remote actors use H1's packed animation field
at +84; its native legs and torso restart bits are retained. MWR's separate
upper-body grenade/stun actions receive a compatible native base pose.
Weapon-family locomotion selects the corresponding MWR clips. Unsupported
source animation mappings produce a named conversion error.

COD4 corpse slots 64..71 map to H1's fixed slots 42..49. Recorded corpse
trajectories select native ragdoll modes 12..14; ordinary initial trajectories
remain ordinary until the recorded physics handoff. Native simulation supplies
the skeleton; COD4 ragdoll joint transforms are not present in this wire data.
Remote fire uses native world events 39/40 instead of local PS events 51/52.
Respawn clears stale firing payloads while preserving the filtered sequence.

`tests/run-cod4-remote-actors.cmd` passes the City demo's 814 player states,
969 corpse states and 662 pickup states across 932 world snapshots. These
represent one friend, two corpses and two pickup entities. It resolves 22
animation IDs including the empty torso/root, checks 13 recorded friend shots,
source/native sequence wrap, respawn history, animation packing/restarts,
native corpse ranges, flag translation, yaw seams and visibility removals.
Local gun, grenade, world/projectile/body and car-motion regressions also pass.

The earlier City remote-actor build and installation passed with EXE SHA-256
`FCFC777AA70A93DB30E88FB209B461384659D49773EA20D871F9D353A43E1DF7`.
All 30 recordings/reports were preserved. That build's install manifest is
`build/install-backups/20260929-133848-before-remote-actors/manifest.json`;
the prior gun build is retained in
`build/install-backups/20260929-133451-before-remote-actors`.
The old City recording needs a fresh F9 conversion. At that stage, native
conversion and visual playback, including corpse ragdoll behavior after
seeking, were pending; the startup command attempt did not produce a new City
recording. The subsequent Promod native results are documented above.

Live reference data and native decoder disassembly are retained under
`build/analysis/cod4-city-animations-live.json`,
`h1-city-actor-reference.json`, `h1-city-actor-codecs.txt` and
`native-animation-codec-build.txt`. The source City demo SHA-256 is
`69BDC2648B390EEF7D5B9F3FCA1C59FBE06D385572DD2BC59913666ABC31C6B5`.
The COD4 animation-table reader later wrote its Promod capture to the former
City JSON path before separate output filenames were added; that file is no
longer independent evidence of the original City capture. The new Promod
capture is stored separately and verifies the same 318 stock names.

## Weapon identities and animations

`cod4_weapon_catalog.hpp` contains 108 explicit source identities, including all
standard player firearms and their original attachment variants:

| Native weapon family | COD4 weapons |
| --- | --- |
| Rifle | AK47, G3, G36C, M4, M14, M16, MP44 |
| SMG | AK74u, MP5, P90, Skorpion, Uzi |
| LMG | M60E4, RPD, SAW |
| Shotgun | M1014, Winchester 1200 |
| Sniper | Barrett, Dragunov, M21, M40A3, Remington 700 |
| Pistol | Beretta, Colt .45, Desert Eagle, golden Desert Eagle, USP |
| Launcher | RPG |
| Equipment / selector | Frag, short-fuse frag, flash, stun, smoke, C4, claymore, bomb/defuse briefcases, airstrike, artillery, helicopter, radar |

ACOG, reflex, suppressor, grip and underbarrel variants select MWR's native
attachment kits where that variant exists in COD4. The six `gl_<rifle>_mp`
identities select the native rifle's alternate grenade-launcher mode; they share
its equipped handle and retain separate native ammo keys. Golden Desert Eagle
uses `h1_deserteagle55_mp`. M14 uses MWR's rifle animation class.

Body movement and action selection covers eight native families: rifle, sniper,
LMG, SMG, shotgun, pistol, RPG and equipment hold. No COD4 models or animation
clips are imported. Empty held-weapon state uses a native hold pose.

The 15-slot native inventory limit applies per snapshot, rather than limiting
the number of different weapons seen over an entire recording.

## Stock multiplayer characters and viewhands

The character catalogue covers all 34 stock `character_mp_*.gsc` scripts in the
pinned COD4 Mod Tools reference: 30 distinct source bodies, including four
unused legacy USMC compatibility identities. It selects 30 native MWR class
sets (body, head and hands) from `mp/costumemodeltable.csv`.

| Native faction/environment | Class roles |
| --- | --- |
| USMC desert | Assault, SMG, LMG, shotgun, sniper |
| Arab desert | Assault, SMG, LMG, shotgun, sniper |
| SAS urban/CT | Assault, SMG, LMG, shotgun, sniper |
| SAS woodland | Assault, SMG, LMG, shotgun, ghillie sniper |
| Spetsnaz urban | Assault, SMG, LMG, shotgun, sniper |
| Spetsnaz woodland | Assault, SMG, LMG, shotgun, ghillie sniper |

All six stock COD4 multiplayer viewhand identities are mapped. In particular,
`viewhands_marine_sniper` selects SAS or Spetsnaz remastered ghillie hands using
the recorded body. Shared source head names also use body/class context. Urban
and woodland Russian classes reuse source bodies, so the original map script's
environment selects MWR's variant. The 25 known environment rows disambiguate
these identities; they do not restrict custom map names.

Each native target was verified resident or registered as a transient in the
installed H1 client. Hands now use the same native binding and transient-loading
path as body models; they no longer need to be present in the menu's model
configstrings. Conversion maps each recorded hands/body combination, preserving
class and faction changes instead of fixing the first snapshot's hands for the
whole demo. Where COD4 has an integrated head, conversion includes MWR's
separate class head. Original weapon attachments are replaced by the existing
native MWR weapon/furniture assembly path.

Reference fixtures are in `tests/fixtures`, with provenance in its README.
Checks passed 850 class/map combinations and all required character/hands
identities in nine readable local demos (95,270 camera samples), including four
hands/body combinations in `D1ablo_cic7_infinite.dm_1`. Eight other files fail
before mapping: six contain an unsupported block type, one lacks a player delta
baseline, and one lacks the recorded map name. Those reader failures are not
counted as mapping successes.

The ordinary F9 Bloc conversion succeeded on the installed build, confirmed by
the user. Its report resolves `body_mp_opforce_sniper` and
`head_mp_opforce_ghillie` to the native Spetsnaz woodland sniper, and
`viewhands_marine_sniper` to `viewhands_h1_spetsnaz_ghillie_mp_camo`.
No prop identities were omitted. Container/camera validation passed 6,074
snapshots, 2,497,698 bytes and a 25.3-second recorded span. Playback visual
acceptance remains separate; physical Escape stopped the automated UI check.

Installed EXE SHA-256:
`3FFF3CCA78CC4D7E8E6CA9D30A9FE1BAA3E70E62C6404F4EDD488ADCDB4CA0F2`.
Source base is `3a9eb4e2713748c0fd5c6bcbdf23e7dfc4fa1d50` plus this character
mapping change. Release x64 MSBuild, stock character/model/visibility/view-motion
checks and the native shock-call ABI check passed. Rollback, source hashes and
seven preserved recording hashes are in
`build/install-backups/20260928-231658-before-character-mappings/manifest.json`.
Bloc output SHA-256:
`A4295F56F8E4E01356C9ACA7F67349BEBF8846209FC8239AF0160BF3F7FA1AE0`.

## Ammo registration

The original ammo/clip registries allocate consecutive IDs for distinct names.
Empty names share index zero, and aliases share an existing ID. Weapon ordinals
are not clip ordinals. The converter reconstructs both registries from the
recorded weapon registration order and translates counts into H1's native keys.

Backlot registers a stationary SAW before the player's weapons. With the
corrected registry, source AK47 weapon 18 uses reserve 6 / clip 17 and starts
with 30 rounds. Source Deagle weapon 12 uses reserve 9 / clip 11 and starts with
7 rounds. Recorded firing and reload count changes agree with the native BG
ammo lookup in the conversion log.

## Destructible vehicles

`cod4_vehicle_bones.hpp` contains 37 original multiplayer vehicle identities:
80s hatchbacks, sedans and wagons; small hatchbacks and wagons; pickup; and four
UAZ variants. The four source bone orders have 48, 54, 46 and 70 bones.

Recorded hidden-part bits are translated by bone name into the corresponding
MWR model's skeleton. This preserves intact and damaged windows when the native
bone order or bone count differs. Backlot exercises silver and yellow sedans
and a brown wagon, extending the earlier Crash sedan coverage.

## Destroyed car conversion fix (2026-09-29)

### Stock wreck coverage update (2026-10-04)

The protocol-19 Ambush recording exposed the missing
`vehicle_80s_wagon1_yel_destroyed` profile. Its pinned Mod Tools model references
`vehicle_80s_wagon_destroyed_lod01`, the same verified 42-bone skeleton as the
brown wagon. The catalog now has 136 explicit model identities: all stock color
variants of the 80s hatch/sedan/wagon and small hatch/wagon wrecks and wheels,
the pickup wreck/wheel and UAZ wrecks, plus all vehicle debris in this recording.
Each model and skeleton was checked against the original Git blob identity;
custom model names still require their own verified profile.

The focused regression verifies all 316 Ambush snapshots, including 632 yellow
wagon wreck states, against reordered native bone names. The Diablo regression
passes 173,246 mapped states, and the Backlot car fixture passes 18,216 states.
The Ambush check can be run as the optional second argument to
`tests\run-cod4-vehicle-variants.cmd`, after the Diablo fixture path. Full H1
conversion and playback remain pending the user's test.

The new `car_explo_to_h1.dm_1` Backlot recording exposed missing destroyed-car
bone profiles. It contains 7,990 camera archives, 625 server world frames and
26 car/debris identities. Destroyed yellow/silver sedans have 41 source bones;
the destroyed brown wagon has 42. Their intact counterparts have 48/54, so
reusing an intact profile would translate visibility into the wrong bone names.

The catalog now includes these three destroyed variants and their 20 recorded
detached parts, retaining all 37 existing profiles (60 total). Bone order comes
from the original COD4 Mod Tools v25 xmodelparts referenced by each model.
Model and skeleton blob hashes were checked against pinned source tree
`95c76d6a470bea43fdd5ab591afe8475fac7699d`; no model data is included in the code.

The recording also retains hide-part mask bits beyond the smaller destroyed
skeletons. Part translation now knows the verified source bone count and ignores
that unused tail, so it cannot hide extra MWR geometry. Hidden bones within the
current source skeleton still require an unambiguous native bone match; unknown
source profiles still fail when visibility data needs translation.

Focused checks pass 18,216 recorded car/debris states, including 1,111 destroyed
states and 2,515 retained out-of-model bits. Tests reorder the destination bones,
add native-only geometry and verify that missing active bones remain errors.
Run `tests\run-cod4-destroyed-vehicle.cmd` with an optional source-demo path.
Intact-car/glass, projectile/body, grenade FX, model/visibility, character and
view-motion regressions pass. Character coverage now includes the new recording:
10 readable demos and 103,260 camera samples, with eight existing reader failures
reported separately.

Release x64 build and installed executable SHA-256:

`13F475D99E898792F621F6EB384B301DBEFE85D6903DCDFAF7412FC8AF7E359A`

Rollback and source/check provenance are in
`build/install-backups/20260929-005043-before-destroyed-vehicle`; all 18 existing
recording/report files remained byte-identical. Evidence is under
`build/analysis/destroyed-vehicle-installed`.

The subsequent ordinary F9 conversion saved `car_explo_to_h1.dm_h1`. Native
container and source-camera validation passed all 7,990 snapshots, 13,206,050
bytes and a 32.2-second recorded span. Output SHA-256:
`BC236A05A57B36B2B3B857CCA86C13B696E7533D68F1DE97D21B56EE2D1A90A0`.
The report retains three native frag detonations and omits four detached prop
identities with no available native counterpart: brown wagon front/back bumpers
and silver/yellow sedan left-front wheel variants. The output, report, lifecycle
log and camera validation evidence are retained in
`build/analysis/destroyed-vehicle-conversion-success`.

The user subsequently confirmed the cars explode and supplied a playback image
showing the destroyed models. The missing car explosion FX is addressed below.

## Native gun firing FX (2026-09-29)

`backlot_cod4_to_h1.dm_1` (SHA-256
`594E0BD8B3215997B7C4658F2C16E8D7D72E81D5EFA5E302D8CB38212ECB962D`)
records 19 shots: 14 AK-47 and five Desert Eagle. Its world event stream has
no recorded bullet-hit/client-hit/bullet events; impact positions and tracers
are not invented or simulated by this change. Bullet-impact translation remains
future work. Firing uses the existing resolved MWR weapon and attachment handle,
with native muzzle/brass behavior and sounds. The converter validates non-null
base WeaponDef FX references and logs their native identities; authored null
references stay optional. Attachment-specific selection stays with native H1.

`cod4_gun_events.hpp` deduplicates the player event ring, handles source sequence
wrap and excludes pre-recording history. It writes the native PS unpredictable
ring: sequence +0x104, interleaved events/parameters +0x10C/+0x110. These offsets
are verified in native CG_CheckPlayerstateEvents (0x328290) and the PC netfields.
COD4 firing 26/last-shot 27 map to native authoritative firing 51/52; ordinary
native firing 39/40 deliberately skips this non-predicting local PS path.
Rechamber 28/brass 29 map to 42/43. The native handler (0xF9400 -> 0xFD690)
owns weapon FX attachment points and brass scheduling. No event enum is copied
without a semantic mapping. Delayed actions with a zero held-weapon identity
are omitted and reported instead of guessing a weapon or rejecting conversion.

`tests/run-cod4-gun-events.cmd` passes all 19 shots across 6,948 camera samples,
repeated paused samples, restart, native PS payloads, source/native sequence
wrap and deterministic seek sampling. Scrubbing suppresses transient gun actions
while catching up; converted gun-only seeks also clear stale native FX. This
initial implementation does not reconstruct aged gun smoke or brass at a seek
position. Native gameplay and ordinary H1 demos keep their handler path.
The collector accepts all ten currently readable fixtures; the eight existing
source-reader failures remain separate. Fresh world/projectile/body, grenade,
car FX and car animation/configstring-isolation checks pass.

Release x64 installed, SHA-256:
`2A7574330343AAAB4F4A4B5B71ADCE327F954E6672AFEC4D81D44C53B546A3EA`.
Rollback to the car animation binding correction and all 28 current demo/report
files: `build/install-backups/20260929-024312-before-native-gun-fx`.
Installed executable, backup and preserved-file parity pass; zero source manifest
mismatches. Evidence: `build/analysis/gun-fx-installed`.

**Native gun visuals remain unverified.** Relaunch H1, F9 convert the Backlot
source, Refresh and Play the new numbered file. Check the AK-47 segment around
5.6-6.8 s and Desert Eagle around 23.8-24.4 s for muzzle flashes, authored smoke,
brass and shot sounds, then pause/rewind and repeat. Inspect the conversion and
native event logs. First-person/freecam attachment behavior, visual recoil parity
and seek playback require runtime confirmation. Existing converted demos keep
their previous event payload until reconverted. These changes remain local.

## Native car explosion FX (2026-09-29)

The dedicated Backlot recording contains four explicit COD4 `EV_PLAY_FX` (55)
events referencing effect 14, `explosions/small_vehicle_explosion`, at source
times 106,850, 117,800 and 124,800 ms (two distinct explosions at the last time).
Their temporary source entities are 95, 149, 250 and 342. The converter uses the
recorded origin and orientation, rather than deriving events from model swaps.

Read-only inspection of live Backlot, including a freshly loaded normal map,
verified native asset `fx/explosions/small_vehicle_explosion` in MWR's FX pool.
It is absent from the map's scripted FX table. Conversion validates its resident
definition and one-shot elements; standalone metadata records its name and the
four event clocks. Native `EV_PLAY_FX` (111, event entity type 131) uses a valid
native table index, 511, bound to that resident asset only during the converted
event handler. The previous slot and client clock are restored immediately.
Ordinary gameplay and H1 demos retain their native table entries.

The existing seek/repeat reset clears stale FX and resets the converted event
identities. Both grenade and car events reconstruct at their recorded age;
repeated archives and paused draws do not create a new burst. The car effect's
own native fire and smoke elements remain native. Separate source
`EV_PLAY_FX_ON_TAG` damage/burning records are not yet translated by this change.

`tests/run-cod4-car-fx.cmd` verifies the four recorded events, sequence wrap,
entity reuse, temporary-event deduplication, native payload/orientation, chain
reaction ordering, pause/seek presence and malformed standalone metadata. The
collector also passes all ten readable local demos, with 21 explicit car FX
events; eight existing reader failures remain separate. The three grenade
detonations in the car fixture remain intact. Grenade and destroyed-car model
regressions pass, and Release x64 builds successfully.

Installed executable SHA-256:
`CE73D840469F17D0105089AF1843DAD41EEB7AE39C0212706128753B2BC74E78`.
Rollback to the accepted model build:
`build/install-backups/20260929-011221-before-car-fx`. All 20 pre-existing
recording/report files were preserved byte-for-byte. Build/check/asset evidence
is retained in `build/analysis/car-fx-installed`. Final installation source/hash provenance is in `build/install-backups/20260929-011704-before-car-fx`.

The user reconverted `car_explo_to_h1.0001.dm_h1` and confirmed the native car
explosion FX work well. They reported choppy body movement and missing flying
parts. Detailed seek/pause acceptance remains separate. The car changes remain
local and unpushed.

## Native car destruction animation and debris physics (2026-09-29)

Read-only inspection of a normal MWR Backlot match verified the native
ScriptableDef death states for the yellow sedan, silver sedan and brown wagon.
All three swap to their respective destroyed model and play
`vehicle_80s_sedan1_destroy` (60 frames at 30 fps). The converter now associates
four recorded explosions with those destroyed bodies and writes H1's native
animated trajectory (11), animation index, original placement and start time.
It removes the old COD4 lift/drop trajectories and derives the duration from
the resident MWR clip. Standalone `@anim:254:<name>` metadata supplies the clip
through the existing scoped configstring binding; H1 owns the native XAnim
tree. Native CG_UpdateEntityScriptedAnimation is at 0x2F28E0.

The first animation build failed playback of `car_explo_to_h1.0002.dm_h1` with
"Unknown attachment vehicle_80s_sedan1_destroy for custom weapon". Its
configstring hook used base 4310, so slot 255 appeared at CS 4565 inside the
custom-weapon range. Native instructions verify animation base 0x10CE (4302)
and custom-weapon base 0x11CD (4557). The corrected binding uses slot 254 / CS
4556, restricts lookups to animation indices 1..254 and rejects index 255 in
metadata. A regression test exercises the production binding helper at the
previously failing custom-weapon slot and neighboring boundaries. The old
experimental `@anim:255` file requires reconversion; prior files without that
metadata remain supported.

The previous script-model translator copied COD4 trajectory ordinals unchanged.
Native BG_EvaluateTrajectory at 0x2C2750 verifies that COD4 acceleration 6 maps
to H1 7, and deceleration 7 maps to H1 8. Native PhysicsLaunchClient at
0x453FB0 verifies COD4 client physics 8 maps to H1 9 for both position and
rotation. The recorded position delta is the launch point; angular delta is
the force vector. Both are preserved for MWR's client physics simulation.
H1 type 11 is animated root motion, not client physics.

`tests/run-cod4-car-motion.cmd` passes four native clip associations, 31 source
debris launches over 7,591 physics states, 1,111 animated body states, native
trajectory/index/clock fields, removal of the old pop, animation/weapon
configstring isolation and malformed metadata.
Fresh world, glass, projectile/body, grenade FX, car FX and destroyed-skeleton
checks pass. These source counts include unavailable props; they do not claim
all 31 launches render. Silver/yellow sedan wheel models and brown wagon
front/back bumper models remain unavailable. A native wagon wheel exists but
uses a different authored mesh; it is not substituted for the sedan wheels.
Part availability remains reported explicitly by conversion.

Corrected Release x64 build installed, SHA-256:
`C3761FF381C96E6DB53691D86FDCF8F68DB1FFDC4FA742AC0172229C3A004BEA`.
Immediate rollback (the failed animation build) and all 24 current demo/report
files are preserved in
`build/install-backups/20260929-021515-before-car-animation-binding-fix`.
The user-accepted car FX executable remains in
`build/install-backups/20260929-015828-before-native-car-motion`.
Installed EXE and both backup/file parity checks passed; the source manifest
has zero mismatches. Corrected evidence:
`build/analysis/car-animation-binding-fix-installed`. Failed-build evidence is
retained in `build/analysis/car-motion-installed`; native asset/definition evidence is in
`build/analysis/car-native-current-assets.txt`, `native-car-defs.json` and
`native-car-defs-summary.txt`.

**Runtime animation/debris acceptance is pending.** The user has been asked to
relaunch, F9 reconvert the source, Refresh and Play the newest numbered recording.
The failed `.0002` container independently passes 7,990 snapshots and exact
source-camera/time validation (32.2 s); that does not establish playback success.
Existing files keep their previous trajectories and metadata. Native physics supplies local
simulation; detailed physics reconstruction after seeking is not established
by these offline checks. Do not claim missing sedan wheels/wagon bumpers fixed
or push these local changes without a user request.

## Native grenade explosion FX (2026-09-28)

The converter now writes MWR's native frag (`EV_GRENADE_EXPLODE`, 98), flash
(`EV_FLASHBANG_EXPLODE`, 103) and recorded concussion (`EV_CUSTOM_EXPLODE`, 104)
event entities. Frag uses the mapped MWR weapon's impact type and native surface
impact FX; flash and concussion use the weapon's own explosion definition.
Sounds also come from MWR. No COD4 FX assets are imported. Missing native FX
produces an explicit conversion error; other observed explosion event kinds are
listed in the conversion report.

Source event rings and temporary event entities are inventoried separately from
missile removal. Direction bytes are verified against both engines' identical
162-vector tables. Surface IDs are translated by the stock surface identities:
MWR inserts metal grate and moves cushion, so source IDs are not copied.

Each explosion has a stable native temporary-event identity, emitted from its
first recorded server snapshot onward. The native previous-event gate prevents
camera archives or paused draws from firing it again. Timing records are embedded
in the same `.dm_h1` header, alongside existing model bindings. Converted playback
supplies the original event time to MWR's handler; seek clears native FX and resets
these event identities so bursts can be recreated at their recorded age. Direct
explosion sounds are suppressed while reconstructing a seek or rewind. Ordinary
H1 recordings and earlier converted recordings use the existing path. The number
of explosion identities is bounded by the native entity slots left after authored
models; overflow fails explicitly.

Offline decoding and focused checks passed these actual fixtures:

| Source demo | Recorded detonations (seconds from first camera archive) |
| --- | --- |
| `cod4_to_H1.dm_1` / Crash | Concussion 15.165 and 17.765; frag 18.265 |
| Backlot | Concussion 13.472; frag 21.272 |
| City | Concussion 14.019, 37.069 and 40.119; flash 30.769; frag 31.719 |

Focused checks cover all 10 events, source sequence wrap, number reuse, temporary
entity deduplication, native event payload/surface mapping, stable snapshot event
presence through pause/seek, metadata parsing and invalid-input rejection. Existing
character, model, visibility, view-motion and Crash projectile/body checks pass.
Run the focused check with `tests\run-cod4-grenade-events.cmd`; optional arguments
replace its three local fixture paths. The accepted converted file also passes
`tests/validate_converted_demo.py`: 2,241 native snapshots and the expected 22.8s
clock range.

The initial Release x64 build passed offline checks and was installed:

`F539F81A8B018C68193E5059AE406FF8311DAA69D6D1A15114FEF4DF65285C48`

Rollback, source hashes and offline check output are in
`build/install-backups/20260928-235957-before-grenade-fx`. All 16 existing files
in `demos/client/auto`, including recordings and conversion reports, were backed
up and remained byte-identical. This initial build was superseded after the
frag validation failure below.

The user's ordinary F9 Crash conversion rejected `h1_fraggrenade_mp` during FX
validation. Read-only inspection of the running MWR weapon definitions confirmed
that frag and short frag deliberately have a null `projExplosionEffect`, impact
type 9 and explosion type 0. Flash and concussion instead own
`vfx/explosion/flashbang_h1` and `vfx/explosion/concussion_grenade_h1`.
The corrected check runs after native client initialization and follows
`CG_GetImpactEffect`: impact type 9 selects impact-table row 13, indexed by the
translated recorded surface. It requires that native frag surface effect,
while retaining the weapon-effect requirement for flash/concussion. Focused
regression checks now cover a valid frag with a null optional weapon effect.

The correction passed Release x64 MSBuild and focused checks and is installed:

`F8EF68ECB76F7C2A0664E25B60D5459A04B5FAC7DADCF6AD45397BC1111A3929`

Rollback and source hashes are in
`build/install-backups/20260929-002226-before-grenade-fx`; all 16 existing
recording/report files remained byte-identical.

**The user accepted the corrected playback on 2026-09-29: "ok it works good".**
The ordinary F9 Crash conversion saved `cod4_to_H1.0004.dm_h1`: 2,241 snapshots,
2,601,796 bytes, SHA-256
`D7F1FC42A7DB44FEF319B5B49DE0BF2F50554B2ABF082995E00FF56BAED5AB82`.
Conversion resolved native frag `vfx/explosion/frag_grenade_concrete` for MWR
surface 17 and `vfx/explosion/concussion_grenade_h1` for the two concussion
events. Playback dispatched entities 1052/1053/1054 as events 104/104/98 at
their recorded times 48750/51350/51850 ms. The installed executable still matches
the corrected build hash above. Runtime logs and the accepted recording are
retained locally under `build/analysis/grenade-fx-accepted`.

This confirms the tested Crash conversion/playback, while City flash rendering,
effect aging during seeks and freecam acceptance have not been independently
documented. Reconvert through F9, Refresh and Play; old `.dm_h1` files retain their
old snapshots. City supplies the flash event near 30.769 seconds. Conversion and
playback diagnostics are in `demos/cod4-replay-last.log` and
`demos/converted-playback-last.log`. Car explosion events are the next work item.

## Native view motion update

The 2026-09-28 motion update supplies MWR's native view and weapon movement
calculations with the recorded bob phase, movement speed, breathing scale,
stance, damage inputs and effect timing. COD4's byte bob phase is converted to
MWR's 16-bit range with `source * 257`, in both snapshots and prediction archives.
The native idle clock follows recorded time, so pause and seek restore its phase.

Stun/flash profile names resolve against H1's native SHOCK registry after
remastered map assets load, including COD4 `flashbang` to MWR `flashbang_mp`.
Their COD4 ordinals are never reused. Net-constant registries remain immutable;
when H1 uses ordinary configstrings, a verified native profile can be registered
in an unused slot. The map load provides the native asset tables used by
conversion; conversion does not start a match or change Play Online's normal
behavior.

Offline checks pass all 256 bob phases, idle pause/rewind, profile remapping and
invalid-state checks, plus 10,560 camera archives from Crash, Backlot and City
and 713 source snapshots carrying effects. City supplies recorded flash and
concussion states, and its conversion is verified below. Visual acceptance of
the native motion and effect rendering remains pending.

Reconvert the source `.dm_1` to obtain the new motion fields, then use the normal
F9 Refresh and Play controls. Previously converted files retain their old data.

## Mapping baseline verification

| Check | Result |
| --- | --- |
| Release x64 MSBuild | Exit 0 |
| Offline weapon/body catalog | 108 source identities; 25,920 poses; 133 distinct native clips resolved |
| Native H1 weapon audit | 108/108 base identities and attachment/alternate resolutions passed |
| Vehicle bone translation | All 37 profiles pass reordered-skeleton checks |
| Backlot source fixture | 6,948 camera samples and 539 server player snapshots decoded |
| Backlot converted file | 6,948 native snapshots; 8,655,324 bytes; 27,961 ms recorded span |
| Camera fidelity | Source samples, snapshot timing, origin, velocity and bob preserved; angle quantization checked |
| Backlot live playback | Normal F9 Play; native AK47, Deagle, hands and third-person body observed; backward seek and end-frame hold passed |
| Earlier Crash regression | Camera, world/glass and projectile/body checks passed |
| Existing recordings | Five earlier recordings remained byte-identical in their original folder |

The native audit verifies definition and kit resolution. It does **not** mean
every variant has been visually exercised. Crash and Backlot supply the current
runtime fixtures; special equipment action trees and the other weapon variants
still need representative recordings for visual acceptance.

The stock faction/class model catalogue is now mapped as described above.
Remote player/NPC entity translation remains incomplete; mapping their client
model metadata does not establish complete remote animation/event playback.
Developer-only weapons and custom mod weapons with
no verified MWR equivalent produce an explicit error rather than substituting a
different asset. Weapon mapping coverage is not a claim that every COD4 demo
feature is implemented.

## City props and conversion cleanup

The City sample records `mil_tntbomb_mp` separately from `com_bomb_objective`.
The failed conversion reported the TNT model as unavailable. There is no
verified replacement for it, and it is not remapped to the objective bomb.

As requested, unavailable recorded script-model props are now omitted. Asset
availability is checked against the **loaded remastered map** and its native
transient catalog. A resident-model export from another map is not a complete
availability list. Available props keep their own identity, trajectories and
translated damage bits. Missing models never become default placeholder models.

Each successful conversion writes `<demo>.dm_h1.conversion.txt` beside the
recording, with the omitted model names, source model indices, entity numbers
and reasons. F9's completion text also shows the number of omitted prop model
identities. The conversion lifecycle log retains these warnings. Characters,
weapon definitions and malformed model names still require valid mappings.

City also contains `body_mp_arab_regular_cqb` and
`head_mp_arab_regular_headwrap`. They map to MWR's
`body_arab_desert_smg_mp_camo` and `head_arab_desert_smg_mp`. The native catalog
contains both targets. Original COD4's
[`_teams.gsc`](https://github.com/cod4mw/CoD4-Mod-Tools/blob/95c76d6a470bea43fdd5ab591afe8475fac7699d/raw/maps/mp/gametypes/_teams.gsc)
assigns the desert CQB character to SMG, and its
[character definition](https://github.com/cod4mw/CoD4-Mod-Tools/blob/95c76d6a470bea43fdd5ab591afe8475fac7699d/raw/character/character_mp_arab_regular_cqb.gsc)
supplies the headwrap. This adds client model metadata; it does not implement
remote player entity animations.

The City conversion crash is now attributed to cleanup. The WER dump
`h1-mod.exe.25220.dmp` shows native disconnect raising
`PLATFORM_DISCONNECTED_FROM_SERVER` through an engine longjmp while a scheduled
callback runs inside the scheduler's noexcept frame destructor. That unwind
reaches `std::terminate` and aborts the client. The triggering conversion error
was the unrecognized recorded `flashbang` profile.

Completion, errors, Stop and load timeout now queue the native `disconnect`
command. The snapshot-clock gate and model bindings remain active until the
client is disconnected; a scheduled check then releases them. On the first
corrected build, the user's ordinary F9 City conversion reported the profile
error, returned to the menu, and logged successful cleanup without a new WER
dump. This validates that error path; successful City export still requires a
separate runtime check. Dump analysis, executable/PDB and the graceful-error
log are retained under `build/analysis/conversion-crash-25220`.

COD4's recorded `flashbang` maps to MWR's `flashbang_mp`. The City runtime
registry places it at slot 4, while `concussion_grenade_mp` retains its name at
slot 1. MWR's separate `stun_grenade_mp` at slot 7 is not substituted for
concussion. Native profile indices are resolved by identity rather than copying
COD4 indices. When H1 uses ordinary configstrings, a verified native profile
may occupy a free slot; net-constant registries remain immutable. All profile
parameters come from the native remastered shock files.

## Model visibility outside the map

`r_keepModels 1` enables an experimental rendering override; `r_keepModels 0`
restores native visibility. It defaults to off and is saved as a client setting.
It can be used with ordinary noclip or the demo freecam. `r_forceLod` remains an
independent detail selector.

Native H1 checks a static instance's `cullDist` before calling `XModelGetLod`.
Consequently forcing detail cannot prevent that instance from disappearing.
It also draws only static models selected by the view's DPVS/Umbra visibility
bits. The new toggle includes all resident static-model instances in that draw
list and bypasses their instance distance rejection. It changes frame visibility
bits without altering model files, map assets or saved cull distances.

For model detail selection, the toggle keeps native automatic LOD selection;
when the native selector would return no drawable LOD because of distance, it
retains the last available LOD. An explicit `r_forceLod` selection still wins.

The visibility-word helper passes allocation-boundary, high-bit-first index,
tail-bit and padded-list tests through 65,536 model instances. The injected
call preserves all general registers and volatile XMM registers. Source hook
sites were checked against the captured native H1 executable.

**Runtime acceptance is pending.** The distant car/grass/vista shots have not
been retested. Dynamic entities have additional visibility gates, and world
brushes, FX, streaming, authored fades and the far plane are not covered by the
static-model override. Drawing more of the scene can increase frame cost.

Test with a normal native demo and a converted demo: enable the toggle, move
far outside the map, turn toward the missing objects, then toggle it off/on at
the same camera position. Check static vegetation/vista models separately from
script-mover cars and projectiles. Confirm normal F9 loading, pause, backward
seek and end-frame hold with the toggle off.

The earlier City/visibility build passed Release x64 MSBuild, the new
model/visibility checks and the existing view-motion checks, but subsequently
failed during conversion cleanup as described above. Its EXE SHA-256 was:

`9A41F5EE6E1E8FF4CBA04B9D85A8C0369B153545B953F4FCAF53E8E1A66E4290`

Rollback and source hashes are retained in
`build/install-backups/20260928-194902-before-model-visibility/manifest.json`.
All six recordings present at that installation stayed byte-identical in
`demos/client/auto`. Distant model visibility still needs runtime verification.
Computer Use was stopped with physical Escape; source/build/install work
continued.

## Conversion cleanup and flash-profile iteration

Source base: `9ff5f6f291b171e162f9ba0bf05ae62a0db5e512` plus the working-tree
mapping, motion, visibility and cleanup changes. The first flash-profile build's
EXE SHA-256 was:

`862B2D3E15F31E9E2B370A5A4D5D890F2F1EB4539C85617F27C93ABA8893533D`

Release x64 MSBuild completed successfully. Offline model/visibility checks
passed for Crash, Backlot and City. View-motion checks covered 10,560 source
archives and 713 affected player snapshots, including the observed native City
shellshock registry and the `flashbang` to `flashbang_mp` mapping. Invalid,
missing, immutable and exhausted profile registries were also exercised.

The installation manifest, source hashes and rollback executable are in
`build/install-backups/20260928-201757-before-conversion-crash-fix`. All six
existing `.dm_h1` files were backed up and remained byte-identical in
`demos/client/auto`. This build subsequently reached profile loading but failed
with access violation `0xC0000005` at `0x1402C579A`. The minidump and matching
executable/PDB are retained in `build/analysis/conversion-flash-crash-201919`.
Its stack identifies `prepare_client_frame` calling the native profile copy
routine with an invalid destination. The compiled call to H1's profile getter
used `mov cl` for the byte-sized `shock_profile::index`, whereas the native
getter consumes all of `ecx`. The invalid destination exactly matches slot
`0xEC8E0004` multiplied by the native profile stride, confirming stale upper
argument bits. The correction promotes the argument to native `int` width.

The preceding cleanup-only build, SHA-256
`590E80F85AF24D119800FC9DB5169E43473B8D30CC6D459AA9B34A1FC92FC117`,
passed the user's City error/cleanup check. The argument-width correction's
City conversion is verified below; playback visuals remain pending acceptance.

The argument-width correction is installed with EXE SHA-256:

`C8FBE9DD57F372A3294CB0815D27C214430478D3E7719E342FF156A737F46B89`

Release x64 MSBuild succeeded. Disassembly of the built executable verifies
`movzx ecx, byte ptr [...]` before the native profile getter, replacing the
crashing build's partial `mov cl`. The check and emitted instructions are
retained in `work/verify-shock-call-width.py` and
`build/analysis/conversion-flash-crash-201919/corrected-call-width.txt`.
Model, visibility and view-motion checks passed again during installation.
Rollback and source/demo hashes are in
`build/install-backups/20260928-202500-before-conversion-crash-fix`; all six
existing recordings remain byte-identical.

The user's subsequent ordinary F9 City conversion succeeded. The lifecycle
log records `1 flashbang_mp -> 4`, `15 concussion_grenade_mp -> 1`, 17 resolved
native body clips, export of 1,371 snapshots, and completion of native
disconnect before releasing model bindings. The user confirmed that the demo
was saved. H1 remained responsive, and no new crash archive was present.

The resulting `demos/client/auto/city_cod4_to_h1_test_player_____.dm_h1` is
2,770,800 bytes, spans 56.917 seconds, and has SHA-256:

`4579C8C0EC232B8C322853A32A21493C1C1221A7C1B878BCEF1A1644F538F9F9`

Native container validation passed with the exact decoded source archives:
all camera positions, velocities and angles match; snapshot times increase;
converted body heading and bob phase retain the recorded conventions. The
conversion report lists 15 omitted prop identities from the actual loaded City
map. Six pre-existing recordings still match their pre-installation hashes.
The output, report, lifecycle log and runtime provenance are retained in
`build/analysis/city-conversion-success`. Native playback visuals, especially
flash/concussion effects and remote characters, require separate acceptance.

## Earlier motion build and local evidence

Source base: `9ff5f6f291b171e162f9ba0bf05ae62a0db5e512` plus the mapping and
motion changes in the working tree. Installed `h1-mod.exe` SHA-256:

`6E5B83EFB66067916DD82CAB20F29C219B9CC42D911813DFB95A86AD9BD60AD9`

The corrected motion build passed Release x64 MSBuild and the transport tests.
Its installation manifest and rollback executable are in
`build/install-backups/20260928-182834-before-view-motion`. The five recordings
present at installation remained byte-identical in their usual folder.

The earlier mapping baseline below was converted and played on this executable:

`710CCDC4C97D4D6D8797E045B8B31899E9E6AC001BB39B94246CB7ED1276D8E4`

Backlot source SHA-256:

`594E0BD8B3215997B7C4658F2C16E8D7D72E81D5EFA5E302D8CB38212ECB962D`

Earlier mapping baseline's converted `backlot_cod4_to_h1.dm_h1` SHA-256:

`ABBDFB008BD93FF56B0612B58BD7A38519F7A7DA3F343422FB1AEC03231C6DCC`

Local build evidence is retained under `build/analysis` and
`build/install-backups/20260928-171534-before-mapping-catalog`. The native audit
was run on the preceding build with the same weapon catalog and kit code; the
final rebuild corrected ammo indexing and was separately converted and played.
The installation preserves an executable rollback copy and recording backups.

`cod4_mapping_audit` can be run from H1's console after loading a demo. It writes
`cod4-mapping-audit.log` in the game directory. The offline catalog test accepts
the native animation-name export and a Backlot source demo as its two arguments.
The container/camera validator accepts the converted file and optional raw
48-byte source camera samples.

## Source provenance

- Standard weapon ammo/clip grouping: original installed `iw_11.iwd` metadata.
- Ammo registration semantics: [CoD4x BG weapon assembly](https://github.com/callofduty4x/CoD4x_Server/blob/master/src/asmsource/_bg_weapons.asm), `BG_ClearWeaponDef`, `BG_GetWeaponIndexForName`, and `BG_FillInAllWeaponItems`.
- Source vehicle bone names/order: [original COD4 Mod Tools](https://github.com/cod4mw/CoD4-Mod-Tools/tree/95c76d6a470bea43fdd5ab591afe8475fac7699d), xmodelparts metadata. Only names/order are used.
- Remastered weapon definitions, attachment/furniture kits and animation names:
  the installed H1/MWR engine's native catalogs and string tables.
