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
Ordinary native gameplay passes through unchanged. Recordings requiring
more than 18 distinct player slots are rejected before conversion starts;
reusing native slots across disconnected source identities is not implemented.

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
