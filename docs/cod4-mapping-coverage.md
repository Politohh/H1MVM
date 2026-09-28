# COD4 to H1 mapping coverage

Updated 2026-09-28. Conversion produces a native `.dm_h1` recording using the
installed MWR models, attachments and animation clips. Use **F9 → Convert COD4
demo → Refresh → Play**. Play Online remains the ordinary multiplayer entry.

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

The converter's broader limitations remain: remote player/NPC entities and all
faction/class body identities are not fully mapped. The currently verified local
body identity was USMC sniper; the subsequent City update also maps the OpFor
desert SMG body/head. Developer-only weapons and custom mod weapons with
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
