# Promod corpse snapshot framing

September 29, 2026. The user reproduced the same `0xC0000005` at
`0x1404EC675` as the initial automated stress run.

The native animation-class lookup fault was a consequence of an earlier
snapshot boundary error. Instrumented `MSG_WriteDeltaEntity` and
`MSG_ReadDeltaEntity` agree through the first corpse's fields. The caller
`CL_ParsePacketEntity` reads an additional bit for corpses in archived frames
at `0x1403408C9`; this selects whether to replace their recorded origin with
the local player's origin. The conversion writer omitted that bit.

At time 52,429,357, corpse 42 ends at bit 4,651 in both field codecs. The
caller consumes bit 4,651, which actually belongs to the next entity's index.
The subsequent script mover 77 is decoded as entity 529. At time 52,429,406,
corpse 42 ends at bit 4,178; consuming the following adjacent-index bit causes
corpse 43 to be decoded as entity 1,858. Subsequent malformed entity fields
eventually request a nonexistent animation class and crash.

The fix enables the native writer's archived corpse extension (`context +28`)
only for entity serialization. Its origin override remains false
(`context +35`), so recorded corpse placement is retained. The native writer
at `0x1404F6772` emits the bit, including when several corpse slots occur in
one snapshot. Playerstate and client-state writer contexts remain unchanged.

`tests/validate_native_entity_trace.py` compares actual native writer/reader
traces for identical entity IDs and types, completed reads, and exact bit
boundaries including the caller's corpse flag. This check supplements the
offline actor mappings and the independent demo-container validator.

Native validation and final installed-build provenance are recorded in
[the Promod stress report](promod-stress-test.md). Old `.dm_h1` files retain
the malformed snapshot format; reconvert the source demo with the fixed build.

## Death animation playback

After the framing correction, the user confirmed playback but reported that
corpses immediately snapped to their final death pose. Clip selection already
resolved to native MWR animations; the animation playback flag was wrong.

COD4's stock `BG_RunLerpFrameRate` extracts bit 19 (`0x80000`) from the
entity flags before its death-animation branch. If absent, it sets the clip
time to 1.0. If present, it starts the authored death clip. This is visible in
the [pinned stock disassembly](https://github.com/callofduty4x/CoD4x_Server/blob/ddd65fce1a1093017bad2dbb6840d75d11c0d56f/tools/cod4_dasm/src/_bg_animation_mp.asm#L307).
MWR's corresponding `BG_SetLerpFrameAnimation` at `0x1402BA5E5` tests bit 20
(`0x100000`); its absent-flag branch calls `XAnimSetTime` with 1.0 at
`0x1402BA6E6`. The same flag permits the player-to-corpse tree handoff at
`0x14032FE2F`.

The converter now maps COD4 bit 19 to MWR bit 20. Previously it mapped
COD4's unrelated voice-icon bit (`0x200000`). Recorded death-phase changes
remain intact: initial death states play the native clip, while settled
corpse states keep the native ragdoll trajectory. Existing clip selection and
recorded animation restart bits are preserved.

City and Promod tests verify the initial and settled phases using real
corpse states, including a corpse with the old voice-icon bit set.
`demoactorstate` provides read-only native corpse clip clocks during active
converted playback. It does not change animation time or weight.
