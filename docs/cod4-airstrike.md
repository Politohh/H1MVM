# Recorded airstrike playback

The converter now retains COD4 `ET_PLANE` entities and plays the recorded
cluster-bomb burst using native MWR assets. Reconvert the original `.dm_1`;
existing `.dm_h1` files do not contain the previously omitted strike records.

## Evidence and implementation

- `demo0011.dm_1` (SHA-256
  `C3B0922795AC5B2D6C62CD9A5EF34C760F315CC1CC205751179F784B3C42182E`)
  contains three MiG passes, 411 plane states, 60 falling bomb states, and
  three cluster launches at source clocks 105986700, 105988700 and 105990450.
- COD4 `CG_AddPacketEntity` sends `ET_PLANE` through `CG_ScriptMover`.
  Translation uses MWR's script-mover renderer and preserves the recorded
  linear-stop trajectory. `vehicle_mig29_desert` maps to `h1_vehicle_mig29`.
- Full snapshot entity deltas use the empty baseline actually present in the
  converted gamestate. `MSG_InitEntityState` sets position type 3 and angular
  type 1; using that as a writer baseline omitted the jets' equal movement type,
  while the client decoded from type 0. Live inspection reproduced stationary
  native jet states despite the recorded linear-stop duration and velocity.
  The writer now starts from zero and protects its zero origin before encoding.
- `projectile_cbu97_clusterbomb` is present in MWR. Its recorded gravity path
  is sampled into native linear motion for each archived snapshot, preserving
  COD4 gravity rather than resimulating with MWR gravity.
- COD4 event 56 resolves a two-digit effect ID and tag from configstrings
  beginning at 1698. The stationary `tag_origin` carrier launches
  `explosions/clusterbomb`; MWR `_hardpoints` uses
  `fx/explosions/clusterbomb_mp` on the same carrier. We serialize native
  event 111 plus its source clock using the existing FX reconstruction path.
- Effect/tag registrations in recorded `d` server commands are applied in
  timestamp order before each snapshot. Airstrike tags may be registered when
  the strike is called rather than in the initial gamestate. Future registrations
  cannot repair an earlier event, and conflicting model/tag bindings are rejected.
- Engine and wingtip effects remain attached to the moving jet using native
  event 112. Bone indices are resolved by tag name from the actual MWR model.
  Native handler `0x14031CBD0` consumes a 9-bit FX index and 8-bit bone index.
  The playback hook temporarily binds the verified named FX for that call
  and restores the previous table entry immediately afterward.
- Reserved conversion FX indices: 508 contrails, 509 engine, 510 cluster
  burst; existing car FX remains at 511. `@scriptfx` timing metadata shares
  the existing event-age/seek machinery. Each native cluster effect supplies
  its authored child explosions; we do not invent impacts from kill counts.
- FX carrier event history survives visibility gaps. Changed carrier poses
  and event sequence wrap remain supported.

Native asset names and `_hardpoints` were inspected read-only in the running
H1 process (PID 21188) on 2026-10-02. Local evidence is under
`build/killstreak-native-assets.json` and
`build/analysis/native-killstreak-assets/42904.gsc`. Those game-derived assets
and decompiled scripts are not included in the repository deliverable.

## Validation and remaining scope

`tests/run-cod4-airstrike.cmd <demo paths...>` checks real recordings, plane
trajectory preservation, native event payloads, bomb motion, duplicate
snapshot/visibility handling, sequence wrap, metadata and malformed input.
The 22 readable recordings under the local COD4 `main/demos` directory pass:
2,357 plane states, 347 gravity bomb states and 19 cluster bursts. Separate
Diablo and Warz fixtures also pass. One pre-existing unreadable unlock demo
is excluded. The native Release x64 build is checked separately.

Visual playback, pause/seek attachment cleanup and jet sound parity still
need in-game verification. This change restores strike visuals; it does not
implement the general recorded sound-event stream, killstreak inventory HUD,
or helicopter weapon behavior. No helicopter entities were found in those
22 fixtures. Helicopter flight is covered separately in
[Recorded helicopter flight](cod4-helicopter.md), using the later Overgrown
sample; the report distinguishes translated helicopter entities from their
untranslated combat event states.

For `demo0011`, watch roughly 89–97 seconds after the first camera sample:
three planes, bomb drops and the three native cluster sequences should play.
