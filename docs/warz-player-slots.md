# Warz sparse player slots

The original `warz_Ban_demo0871.1.dm_1` contains 17 player identities in
slots 0, 1, 2, 3, 5, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18 and 20.
The recorder occupies slot 17. Previously conversion only swapped the
recorder with slot zero, leaving actors 18 and 20 outside H1's player range.

Native H1 evidence from the installed multiplayer executable:

- `0x14031C890` creates player animation trees. Its loop count is the literal
  `0x12` at `0x14031C8AB`; it assigns trees at `cg + 0x1621D8`, stride `0x740`.
- The eight corpse trees are allocated separately at `cgs + 0x2B80`.
- The original failure at `0x1402B4A8E` read the absent actor tree during
  sequence selection. The earlier zero-phase workaround did not initialize
  that tree, so playback subsequently failed at `0x1405CDD0B`.
- The second dump and playback trace identify client 18 and animation 766.

Conversion now builds a stable, collision-free player-slot map. It keeps
the recorder at zero, preserves existing valid slots, and assigns sparse
identities to remaining slots below 18. In Warz, source 18 becomes 4 and
source 20 becomes 6; source 0 remains 17. Player entities, client rows,
corpse client identities, bullet events, obituaries, ground and projectile
references use the same map. Source world and none references retain their
native meanings. The conversion report lists each assignment.

The ineffective sequence-selection workaround was removed. Converted
playback checks player identities before `BG_PlayerAnimation` and asks for
reconversion if an older file still refers to an unsupported slot.
Ordinary native gameplay passes through unchanged. Smaller recordings retain
these exact mappings. Larger recordings now use the additional 24 native agent
slots; reusing native slots across disconnected source identities is not implemented.

## Larger recordings

The native snapshot layout has 18 clientState rows at snapshot `+0x45488`,
followed by the agent count at `+0x46370` and 24 agentState rows of 128 bytes.
The clientInfo name array also has 18 entries; `CG_SetNextSnap` keeps an
18-entry stack presence array. Increasing the client count would overrun these
structures even though characterInfo has 42 entries.

Overflow recorded players therefore use `ET_AGENT` (18) and
`MSG_WriteDeltaAgent` (`0x1404F3330`). Live Ambush inspection verified the
69-field agent entity table retains the native player animation, motion,
weapon and event-ring fields. Agent state carries model at `+2`, attachments
at `+16`, team at `+76` and name token at `+124`.

Recorded name tokens live in the converted file's `@agentname` metadata.
`CL_GetAgentName` (`0x1403439D0`) resolves them through the current parsed
agent ring, preserving name changes and feeding the existing native HUD and
killfeed. It never indexes the 18-entry clientInfo array with a larger identity.

`CG_CreatePlayerAnimTrees` additionally creates the agent slots' script trees
with its existing allocator (`0x140322A30`) and native animation catalog.
`CG_FreePlayerAnimTrees` already visits those slots through `0x14017F570`.
That wrapper normally supplies a heap deallocator to `XAnimFreeSmallTree`;
for tracked replay hunk allocations the hook clears the animation nodes with
the same null deallocator used for native player trees. Other trees retain
their original deallocator. Cleanup follows the allocated tree's ownership,
even after playback stops or changes maps.

The capacity check now protects 42 identities, with source player indices
limited to 0..41. Corpse entities remain 42..49. No player snapshot client row
is emitted for an agent slot. The protocol-19 Ambush fixture maps all 39
identities to 18 players and 21 agents; decoding, names, teams, kill identities
and capacity boundaries pass offline checks. Native conversion/playback
acceptance is still pending.

## Validation

`tests/run-cod4-client-slots.cmd` accepts original `.dm_1` fixtures. Its
synthetic cases cover sparse slots, a high recorder slot, exactly 18
identities, capacity overflow, unrecorded references and native sentinels.
Recorded tests passed for:

| Fixture | Players | Snapshots | Bullet impacts | Obituaries |
| --- | ---: | ---: | ---: | ---: |
| Warz | 17 | 2,002 | 22 | 15 |
| Rav | 10 | 815 | 14 | 6 |
| Diablo | 10 | 16,896 | 213 | 86 |
| Crossfire | 10 | 980 | 15 | 5 |

The earlier popup timing and minimap changes were visually accepted by the
user. Warz requires a new conversion after installing this build. Successful
compilation and offline data checks do not establish successful playback;
the fresh Warz replay remains to be tested.
