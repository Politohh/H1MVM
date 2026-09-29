# DebilzuuR Promod 5v5 stress test

Tested September 29, 2026. **The corpse framing crash is fixed. Native death
animations now play, confirmed by native clip clocks and the user.**
The initial failures below are retained as historical evidence. Blood impact
FX are now translated; native runtime validation is recorded in
[the blood-impact report](promod-blood-fx.md).

## Current blood build

Rebuilt output: `dEbilzuuR_2k_deag_r700_crash.0006.dm_h1`, 4,958 snapshots,
9,967,293 bytes. Container, camera and source time-range checks pass.
It adds 21 recorded flesh impacts and four native incoming-hit events.
The final installed build completes a native 60-second replay. All 25 impacts
dispatch exactly once; 14 native MWR blood bursts allocate at their recorded
positions/clocks, and seven general impacts are outside native PVS visibility.
No visible impact is missing or duplicated. Blood visual acceptance is pending.
Rewind reconstructs a native blood burst at its original clock, and 11,784
paused rendered frames produce no repeat impact/FX calls.

Built/installed EXE SHA-256:
`3F13EF4253A499916ED31CC6C705ECCC916BAE8FEE3A8C68FF81D33C0A6F5E82`.
Rollback: `build/install-backups/20260929-165754-before-promod-stress`.
All 54 preexisting recording/report files were backed up and preserved.
The accepted corpse/death checkpoint below remains intact. Changes are unpushed.

## Accepted corpse and death checkpoint

The archived entity writer omitted the native corpse-origin override bit,
misaligning every later entity. The fix enables that extension with a false
override, retaining recorded corpse placement. Actual native writer/reader
traces pass for 72 snapshots, 4,436 entities and 48 corpse flags. See
[the framing and death-animation analysis](promod-corpse-framing-fix.md).

COD4's death-playback flag `0x80000` now maps to MWR's `0x100000`. The previous
mapping used COD4's voice-icon flag, causing native death clips to finish
immediately. Clip names, restart bits and the recorded ragdoll handoff are
preserved. City and Promod initial/settled corpse checks pass.

Read-only samples of that checkpoint's installed build contain 276 native clock
observations: five corpses play advancing MWR death clips, three reach their
final pose during sampling, and 980 paused pairs keep identical animation
time. The user confirms that the death animations visibly play.

Rebuilt output: `dEbilzuuR_2k_deag_r700_crash.0001.dm_h1`, 4,958 snapshots,
8,789,668 bytes, SHA-256
`717B6B9C723164525B80391BF821CE1B438069F8E18E5F682092F15200F5F3E1`.
The container, camera and timing validator passes. Existing converted files
are preserved; select the rebuilt output or reconvert the original source.

Release x64 build completed at 16:00:03 local time. Built/installed EXE:
`A92D806D460A42DAE1F44FB4F239E6619089F026585F43038BA7F18E17419D87`.
Rollback: `build/install-backups/20260929-160031-before-promod-stress`, with
42 existing recording/report files preserved. The core fix completed a native
60-second replay in the preceding `73DE2B6A...` build; the final build adds
only read-only `demoactorstate` diagnostics to that playback code. Its observed
playback and user animation acceptance are preserved separately. No FPS
measurement or general visual parity claim is made.

Current evidence: `build/analysis/promod-corpse-fix-final`. The initial failed
stress pack below remains intact. Source changes have not been pushed.

Source: `C:\Games\cod4_game_files\Mods\promodlive204\demos\dEbilzuuR_2k_deag_r700_crash.dm_1`.
The copies in `main\demos` and `players\demos\IWXTMP` are byte-identical.
Size: 432,920 bytes. SHA-256:
`AC07AC825F6EF8523D764925F93D254CEF6F1E3E1BF43F018634292F111EBB24`.
The short-path native test input is `F:\Dev\h1-cod4-demo\build\debilzuur.dm_1`, with the same hash.

COD4 was launched with `+set fs_game mods/promodlive204 +demo dEbilzuuR_2k_deag_r700_crash`.
Its log confirms Crash and two discarded opening delta packets. Its live BG
animation table contains 318 names, all matching the stock catalog used by the
converter. Capture: `build/analysis/cod4-promod-animations-live.json`.

## Automated fixture results

`tests/run-cod4-promod-stress.cmd` passes using an independently captured native
MWR animation codec table. This is a conversion/data stress test; its elapsed
time is not a gameplay FPS measurement.

| Recorded data | Result |
| --- | ---: |
| Map and mode | Crash, Promod 2.04 Search and Destroy |
| Duration | 59.973 seconds |
| Players | 10, split 5v5; local client 6 |
| Remote players | 9, all simultaneously present in at least one snapshot |
| World/player snapshots | 1,136 |
| Camera archives | 4,958 |
| Peak recorded entities | 39 |
| Player states | 7,594 |
| Corpse states / entity slots | 2,758 / 8 |
| Pickup states / distinct entities | 2,281 / 13 |
| Projectile states | 1,255 |
| Source/native clips exercised | 100 / 100 |
| Packed actor animation combinations | 608 |
| Remote/local gun actions | 333 / 33 |
| Grenade detonations | 21, including smoke |
| Car explosion FX | 2 |
| Interpolated midpoint checks | 7,591 |
| Actor translations across forward and 20 reverse passes | 946,449 |

All reverse passes reproduce the forward native actor bytes. The checks also
verify player identity, corpse slot collisions, packed animation channels and
restart bits, pickup weapon identity, projectile translation, event payloads,
and identical world/player baseline recovery. City, local gun FX, grenade FX,
world/projectile/body, and car animation/debris regressions pass.

## Problems found and corrected

- The opening snapshot deltas reference two absent frames. The reader now
  matches COD4's packet-discard behavior and resumes from reconstructible
  snapshots, without inventing a baseline. The conversion report counts these.
- Movement, pistol ADS firing, weapon-switch, and death animation mappings
  missing from the smaller City test are resolved to native MWR clips and
  checked against their valid leg/torso channels. Two legacy death reactions
  use native authored spin/quick-death replacements; exact old clip parity
  is not claimed.
- Pickup index 648/776 encodes AK weapon 8 plus world-model variant 5/6.
  The converter now extracts the weapon identity instead of treating the full
  packed value as a weapon index. Native appearance is used and reported.
- Smoke uses `EV_CUSTOM_EXPLODE`, now accepted with the native smoke weapon FX.
- BSP script movers are separated from XModel props in car detection, asset
  resolution, map-dressing duplicate checks, and serialization.
- Remote players and pickups request world weapon models. Only the recorded
  player's inventory requests first-person models and scopes. Previously the
  converter requested every player's view kit and exceeded H1's six transient
  view-weapon slots, producing allocator error 29. The native transient queue
  is serviced during client-only conversion and converted playback.
- Crash's destroyed red sedan now has its verified 41-bone source profile.
  The pinned original model and xmodelparts blob hashes were checked; the
  intact 48-bone car skeleton is not substituted. Stress checks now reject
  masked source models without a verified profile. All prior 26 car/debris
  fixture identities and visibility checks still pass.
- Twenty predicted camera archives contain bob values 256..275. COD4's
  native `BG_GetBobCycle` reads only the low byte; conversion now does the
  same before scaling to MWR's 16-bit phase. The complete 4,958-sample fixture
  and all prior camera fixtures pass the bob, view-motion and damage checks.

## Remaining coverage limits

- Source brush entities 121 (brush 11) and 365 (brush 33) are omitted and
  reported. Corresponding native BSP geometry indices have not been verified.
- `prop_suitcase_bomb` and `vehicle_80s_sedan1_red_wheel_lf` have no available
  mapped native model and are reported as omitted props.
- Pickup camo/world-model variants 5/6 use the resolved native base appearance.
- This recording's two car FX events do not resolve to one of the currently
  supported native destruction-animation model transitions.
- Blood impacts use native MWR assets. User visual acceptance is pending;
  source event 42/43 retains incoming-hit sound/trail behavior rather than
  inventing an additional world blood burst.
- Complete physics/visual parity and gameplay performance acceptance remain
  unmeasured. No verified screenshot or FPS measurement was captured.

## Historical conversion and failed playback evidence

Two native conversions saved successfully under the MWR installation's
`demos/client/auto`: `debilzuur.dm_h1` and `debilzuur.0001.dm_h1`, each with
a conversion report. Both files contain 4,958 native snapshots and are
8,782,585 bytes. Their identical SHA-256 is
`A843D31C5596D054727EE80D791704B76339F5D3485AF529895ED4DE50A119F6`.
The independent container validator passes native compression, gamestate,
snapshot time, camera position/velocity/angles, bob phase and file termination.
The nine remote players, eight corpse slots, thirteen pickups and 333 remote
gun actions are represented in the conversion report and fixture checks.

Both recordings reach native `CA_ACTIVE` (10), with cgame initialized and
ordinary native gun/grenade events processed. Playback then raises
`0xC0000005` at `0x1404EC675` and recovers to the frontend. The faulting native
packed-animation decoder dereferences a null result from an animation-class
lookup. At that time the invalid decoder input was unresolved; the subsequent
native traces establish the corpse framing cause described above.
The first failure occurs near source time 52,429,350, about 11.55 seconds after
the recording begins at 52,417,800. The second run's last gun-event timestamp
is 52,429,301 before cgame drops to zero.

Native entity emission now stages entities by ascending destination ID and
rejects duplicate IDs. This recording already has ascending translated IDs
(`NATIVE_ENTITY_REORDER snapshots=0`); the regenerated file is byte-identical
and reproduces the crash. Entity ordering therefore does not explain this
fixture's failure. The nearby first frag event is a timing correlation, not
an established cause. The debugger experiment did not obtain a useful
per-entity trace.

The two native crash ZIPs are
`h1-mod-crash-2-2026-09-29-14-42-42.zip` and
`h1-mod-crash-2-2026-09-29-14-53-56.zip`. The first dump's exception context,
native field data and stack are retained, along with both lifecycle logs.

## Historical installed build and preserved failure evidence

Release x64 build completed successfully at 14:48:13 local time. The built
and installed `h1-mod.exe` SHA-256 is
`404FF863F7E22C1A728B9BBA0AD5E39C71C56FD48708E6D68BBC515A68047495`.
The installation is
`C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Modern Warfare Remastered`.
Rollback backup and install manifest:
`build/install-backups/20260929-145015-before-promod-stress`.
All 30 recordings/reports that existed before this stress test remain
byte-identical. The final backup also preserves the first new conversion and
its report. The installed build includes the conversion fixes above; its
native playback failure is still open.

The bounded evidence pack is `build/analysis/promod-stress-final`, with the
build executable, demo source snapshot, fixture, converted outputs/reports,
test logs, build/install manifests, live animation references,
conversion/playback logs and both crash ZIPs. Its manifest records hashes,
preservation checks and the failed runtime result. H1, COD4 and the debugger
launched for this test were closed. No source changes have been pushed.

Historical reproduction: `demoplay debilzuur.0001` uses the malformed old
recording. It is retained for diagnosis. Use the rebuilt `.0006` output named
in the current-result section for the corrected playback.
