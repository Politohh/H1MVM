# Native MWR blood impacts

September 29, 2026. The Promod source contains 21 general flesh impacts
(COD4 event 41) and four incoming client hits (event 43). These events were
previously omitted. General impacts map to native MWR event 77; incoming
small/large hits map to native events 80/81, retaining their sound/trail
behavior. Blood is driven by recorded surface 7 and impact normals, not by
inventing an effect whenever a player dies.

The translation retains the recorded origin, bullet start, shooter, explicit target,
weapon identity, alternate mode, client identity and low-byte impact flags.
COD4 has one impact per event and no H1 shot-count field; the destination
uses one shot. Temporary event incarnations and event-ring wrap are handled
without repeating an impact on subsequent camera archives. Stable native
identities and `@impact` clocks support paused playback and FX reconstruction
at the original recorded age after a seek.

The first runtime build dispatched impacts but produced no blood bursts.
Read-only native snapshot inspection showed that surface 7 became surface 0.
`MSG_InitEntityState` at `0x1404EC490` initializes its surface byte to flesh
(7). The converter used this as its delta baseline, while its gamestate
contains no entity baselines. The client's empty baseline supplied surface 0.
The native writer therefore omitted flesh as unchanged. Setting the writer's
baseline surface to 0 makes the native codec actually transmit flesh. Its
default target also needs zeroing: otherwise an unchanged world target is
omitted and decodes as player 0. Both fixes are verified against decoded
native snapshot fields, rather than just the translated input. Other
native baseline defaults and the accepted corpse-origin extension are retained.

Stock `Bullet_ImpactEffect` does not assign a victim entity to its temporary
impact; its ground-entity field remains zero. MWR treats target 0 as a player
and performs an extra player-impact pass, duplicating exit bursts and
suppressing some first-person impacts. The absent target maps to native
`ENTITYNUM_WORLD` (2046); an explicit nonzero target is preserved. H1 treats
`ENTITYNUM_NONE` (2047) as an instruction to skip impact-effect selection, as
confirmed by its native handler and the next diagnostic replay. No victim
identity is guessed from nearby bodies.

The world-target diagnostic reached the complete 60-second end with all 25
impacts dispatched once and the decoded target correct, but only seven blood
bursts allocated. H1's world-impact branch backface-culls oriented wall marks;
its player branch skips this angle test for volumetric flesh effects. Converted
flesh impacts with an unrecorded victim now use that player visibility behavior:
the same native point/PVS test at `0x14065B970` / `0x1406602B0`, without the
wall-mark angle test. The hook applies only inside a tracked converted flesh
event with world target 2046. Explicit targets, nonflesh impacts, native demos
and normal matches continue through the original culling routine. No graphics
dvar is changed, and the native handler still selects and allocates the effect.

A dedicated dispatch history also prevents native centity history resets
from replaying a stable impact identity after respawn. It is cleared together
with native FX on seek, restart and demo unload. The surface-only diagnostic
replay reached the end but contained two repeated dispatches, motivating this
native runtime check in addition to the offline duplicate tests.

Native `CG_EntityEvent` at `0x1400F9400` selects the installed weapon's impact
behavior. General impacts reach `CG_GetImpactEffect` (`0x140101D00`), which
chooses native MWR flesh effects. The passive oriented-effect trace records
the engine's selected effect name, allocation result, recorded clock and
position; it does not replace the engine's selection or allocation.

Stock payload reference: [pinned COD4 CG_EntityEvent disassembly](https://github.com/callofduty4x/CoD4x_Server/blob/ddd65fce1a1093017bad2dbb6840d75d11c0d56f/tools/cod4_dasm/src/_cg_event.asm).
Its Git blob was checked against the pinned tree. H1 IDs and offsets were
verified from the installed native dispatch, netfields and runtime assets.

Offline checks pass for all 25 Promod impacts, synthetic small hits,
nonflesh surfaces, byte payloads, event wrap/reuse, metadata collisions,
pause/seek presence and malformed data. The Backlot/City fixtures have no
recorded bullet impacts and still pass; the car FX regression retains its
four explicit car explosions and three grenade detonations.

Current installed EXE: `3F13EF4253A499916ED31CC6C705ECCC916BAE8FEE3A8C68FF81D33C0A6F5E82`.
Rollback: `build/install-backups/20260929-165754-before-promod-stress`.
All 54 recording/report files present at installation were backed up and
preserved. The rebuilt recording is
`dEbilzuuR_2k_deag_r700_crash.0006.dm_h1`, 4,958 snapshots, 9,967,293 bytes,
SHA-256 `BC6729644B092BFDBB84A6B5A7DDA127E548E595D864753570D03531C767206C`.
Its container, camera and time-range checks pass.
The recording was generated by the retained `66C233F2...` diagnostic build;
the final build changes only playback visibility, so that recording is reused.

The final installed build completes the original 60-second replay at native
time 52,477,634. An independent comparison validates all 25 decoded payloads
against the conversion lifecycle, all 25 event dispatches exactly once, and
14 successful native blood allocations at exact recorded clocks/positions.
The other seven general impacts are culled by native PVS visibility; no visible
burst is missing or duplicated. Selected assets are native MWR body nonfatal
and body fatal-exit effects. The final playback creates no new crash dump.

A rewind reconstructs the first blood impact exactly once using its original
birth clock. The settled native clock remains at 52,430,044 across 11,784
additional paused rendered frames, with no repeated impact, visibility or FX
calls. This passes `tests/validate_native_blood_seek.py` independently of the
offline timing checks. User visual acceptance of blood is pending. The previous
death-animation correction has already been visually accepted and its
checkpoint remains preserved.

Evidence and file hashes: `build/analysis/promod-blood-fix-final/manifest.json`.
The accepted death checkpoint and earlier failure packs remain intact. Source
changes are uncommitted and unpushed. H1 is left on the `.0006` recording near
the reconstructed blood impact for user inspection.

One frontend setup attempt, before conversion/playback started, looped on
protected-dvar commands and faulted at `0x1405A2814` when exiting. Its dump is
retained in `build/analysis/promod-blood-startup-failure`. Sequential console
initialization succeeded afterward. This is retained separately from the
completed final native playback rather than being discarded.
