# CoD4x protocol 19 and 21 recordings

`demo0011.dm_1` begins with block 2: protocol 21, a legacy `-1` end marker,
and eight reserved zero bytes. The original decoder accepted only packet and
camera archive blocks and rejected this header.

The protocol header now selects the CoD4x format in all three readers. Its
gamestate uses 32-bit configstring counts and indices with two 4096-entry banks.
Origin fields (-90, -91, -92) carry raw floats instead of legacy quantized deltas.
The gamestate footer adds a config-data sequence before the recorder identity.
Separate client identity messages supply the recorded names for native H1
client rows and the killfeed. Existing headerless recordings retain their
original decoding path.

The format was checked against the CoD4x client's
[protocol/gamestate reader and writer](https://github.com/callofduty4x/CoD4x_Client_pub/blob/master/src/cl_main.c)
and [origin encoding](https://github.com/callofduty4x/CoD4x_Client_pub/blob/master/src/msg.c),
then verified against the complete supplied file. Unknown protocol versions,
misplaced headers, corrupt markers and truncated records still fail explicitly.
Reliable-message block 3 and protocols other than 19 and 21 remain unsupported.

## Protocol 19

Protocol 19 is now accepted by the shared protocol-header reader used by camera,
player and world decoding. Its extended configstring bank, separate client-name
messages and raw-float origin codec are already handled by these readers.
This was checked against the official CoD4x 19.3 source at
[`410a4184128631486258a888784b0f449c8206ec`](https://github.com/callofduty4x/CoD4x_Client_pub/tree/410a4184128631486258a888784b0f449c8206ec),
specifically `CL_ParseGamestateX`, `CL_Record_WriteGameState`, and the
`MSG_ReadOriginFloat` / `MSG_ReadOriginZFloat` implementations. This does not
enable arbitrary version numbers or treat all CoD4x releases as interchangeable.

`tests/run-cod4-protocol19.cmd <path-to-60_AMBUSH_ACOG_C4S.dm_1>` checks the full
Ambush recording (source map `mp_convoy`), SHA-256
`06e863883b5e02039f0109d93e29b9b7c3429d89beca4ffe66eabab9aeb85a9e`:

- 1,424 retained camera frames from 1,667 archive records and 317 packets.
- 316 synchronized player/world snapshots and 82 entity baselines.
- Recorder 27, `dutchchedda`; 47 obituary events and 15 gun events.
- Fractional actor positions, all three raw-origin codecs, and malformed,
  misplaced, truncated and unsupported protocol-header rejection.

This recording has 39 active player identities simultaneously. The replay now
maps them to 18 native players and 21 native agents, preserving names and teams
without overflowing H1's fixed player snapshot rows. Native agent script trees
use the player tree's hunk allocation and matching cleanup. The character limit
is 42, before the eight corpse slots. The full fixture passes decoding and
identity mapping checks; visual conversion/playback of this file
has not passed.

## Validation

Run `tests/run-cod4-protocol21.cmd <path-to-demo0011.dm_1>`.

The fixture is Crash, recorder 5 (`Polito`): 2,922 retained camera frames,
2,036 player/world snapshots, 62 entity baselines, 16 active character identities,
69 kill events and 81 gun events. Camera time spans 105893896–106005066;
player/world snapshots span 105903200–106005150. Tests also check native name
translation, fractional origin precision and malformed input rejection.

Legacy Warz/Diablo player-slot tests, Crash/Strike minimap/hitmarker tests and
Crossfire/Diablo popup timing tests cover regression compatibility. Successful
offline decoding and compilation do not establish visual playback acceptance.

## Planted explosive follow-up

The next in-game conversion reached weapon resolution and rejected source
weapon 39, `claymore_mp`, with event 45. That event is shared by grenades and
planted explosives. Live H1 definitions verified `h1_claymore_mp` and
`h1_c4_mp` both use impact type 9 and have their own projectile models.
Both now pass the event-45 weapon check and retain those native identities.
The existing native impact table supplies their explosion effects; neither
the event nor the weapon is replaced with a synthetic frag.

The grenade test covers all 25 detonations in `demo0011.dm_1`, including its
claymores at 105977100 and 105996600, with direction and weapon identity
preserved. Existing Crash, Backlot and City grenade fixtures also pass.

## Transient weapon allocation follow-up

The next conversion loaded seven first-person weapon fastfiles and failed with
allocator error 29. The 2026-10-02 05:06:27 minidump retains the call chain from
transient allocation (`0x1400744A0`) through page commitment (`0x140074020`,
error-29 branch at `0x1400740CD`). The reported access violation at
`0x1405A2814` occurred in allocator cleanup during the subsequent error exit.
The dialog's generic disk-space advice does not identify this fixed-pool limit.

H1 reserves six view-weapon slots at `0x14007445D`. Normal matches keep six.
Converted playback and conversion now request up to 64 through the same native
reservation function, before any assets load. The count is capped by the free
entries in H1's 294-slot shared table; even the native split-screen reservation
of 162 preceding entries fits. Virtual space is reserved up front; the native
allocator still commits pages as fastfiles load and owns teardown. Existing
live pools are never resized, and remote characters still request world models
only. The reservation is logged with the native category name and actual count.

Model requests are collected and checked against the session's category
capacities before acquisition. Shared-zone parts count once. Excess requests
report the required and available slots instead of overrunning the allocator.
Conversion releases only the model references it actually acquired. Saved
converted files receive the same budget check before their asset requests.

`tests/run-cod4-transient-budget.cmd <path-to-demo0011.dm_1>` checks normal-match
behavior, replay capacity, shared-table boundaries and invalid reservations.
The real fixture has 11 native weapon identities across its recorded inventory,
including seven firearms/launchers that need first-person fastfiles. Decoder
and grenade regressions pass; in-game conversion/playback of this build remains
pending user verification.
