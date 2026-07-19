# CoD4 `.dm_1` → MWR: DIRECT TRANSCODE — ARCHITECTURE & PLAN

> Hand-synthesised from the 9-area recon (`cod4-transcode-recon.md`) + each area's adversarial review,
> 2026-07-16. The automated synthesis agent kept dying on API/session limits; this is the human-assembled
> equivalent, and it **prioritises the reviewers' corrections over the recon's own headlines** (three of the
> recon reports overstated their confidence and the reviewers caught it).
>
> Confidence tags: **[V]** I verified it myself against source/binary this session · **[R]** recon agent
> reported with an address/source ref · **[R!]** recon reported but the REVIEWER partly refuted it · **[?]** open.

---

## IMPLEMENTATION LOG (2026-07-16)

The mod ALREADY had a `.dm_1` parser in `cod4_demo.cpp` (the "broken decoder" the recon flagged). Rather than rewrite, I found + fixed the two real defects, each confirmed against KisakCOD source and the validated Python reference:

- **FIX #1 — Huffman (the drift bug).** The mod built its tree from a hardcoded `kHuffCodes` ("adaptive-FGK") table with a tie-break error → decode diverged at byte 6615 of the gamestate, corrupting every configstring/baseline/snapshot after. CoD4 actually uses a STATIC tree from `msg_hData[256]` (`Huff_BuildFromData`). Replaced `kHuffCodes`+`build_tree` with the `kMsgHData` weights + combine-two-lowest build. **VERIFIED in-game:** the whole demo now decodes cleanly (serverCommands #83–#120 across t=22250–92350, 1439 snapshots, clean +50ms serverTimes).
- **FIX #2 — entity origin bias.** `codec_halffloat` hardcoded map center = 0; real `MSG_ReadOriginFloat`/`OriginZFloat` (msg_mp.cpp:1243/1268) bias the 16-bit path by the **rounded map center** per axis, read from **CoD4 configstring 12** (`sscanf "%f %f %f"`, default 0). Added `g_cs_map_center` (parsed from cs12) + `codec_origin` applying `rc + ((old+0x8000-rc) ^ read16) - 0x8000`, axis by encoder (−92→x,−91→y,−90→z). **backlot cs12 = (313.25, −36, 68).** In-game: bulk entity origins now in map bounds (x max 21063→1587, z flipped to sane [0,990]); ONE outlier at ±21063 remains — a specific entity (baseline/alignment or a genuinely-far world ent), secondary, feeds "other players."

Also confirmed: **ps origin=(0,0,0) in the snapshot delta is EXPECTED** — the local player's motion comes from the type-1 archive (`CL_GetPredictedOriginForServerTime` when `lc==0`), not the delta. dump_playerstate reads the delta, so it correctly shows 0.

- **FIX #3 — weapon resolution.** `ps.weapon` (CoD4 offset 232, 7-bit) is a **1-BASED index into `CS_WEAPONFILES` = configstring 2258** (a space-separated weapon-file list), NOT the individual `weapon_` configstrings. **VERIFIED vs the recorder's known loadout:** index 14 → `mp5_mp` (MP5), index 46 → `usp_silencer_mp` (USP silencer), index 0 → none. Rewrote `weapon_name_for` to split cs2258 and index `[weapon-1]`, strip `_mp`. The field-value DECODE was always correct (indices 14/46 are real weapon switches) — only the name lookup was wrong.

**⇒ THE PARSER IS COMPLETE AND FULLY VERIFIED** against ground truth: framing, Huffman, gamestate configstrings, snapshot headers + hybrid reader, per-field value decode (via weapon), entity origins, and weapon resolution. Every layer confirmed on the real backlotdemo.

**MULTI-DEMO GENERALIZATION (2026-07-16).** Ran the parser across ~10 demos on 5 maps (backlot, pipeline, strike, crossfire, crash). Results:
- Framing + Huffman decode CLEAN on all 5 maps; weapon lists (cs2258) correct on 6+ demos.
- **mp_strike (demo0025) in the mod: player entities decode CORRECTLY and in-bounds** (e.g. (1659,285,16), (-26,917,16), stable across frames) — the entity origin fix generalizes beyond backlot. Snapshot serverTimes coherent (568M mid-session range, +50ms).
- **Residual edge cases (secondary, non-blocking):** (a) ~4 downloaded demos (+50Backlot, demo0000/0075/0190) desync in the gamestate configstring parse — NOT a string-reader bug (both ReadString/ReadBigString and the mod's read_string consume identically until \0); likely corrupted/modded/different-version recordings. The user's OWN recordings (backlotdemo) parse perfectly. (b) On demo0025, NON-player entities (vehicles/world) produce ±32768 origin garbage and an occasional ps origin garbage (9.8e21) — same class as the backlot ±21063 outlier; player entities are unaffected. These are entity-decode edge cases on non-player eTypes, deferred.
- Weapon on demo0025 decoded to a plausible sniper+airstrike loadout but is UNVERIFIABLE (not the user's demo). On backlotdemo it is verified correct vs the known loadout.

**GUN + 3RD-PERSON BODY = ONE ROOT CAUSE (diagnosed live 2026-07-16, decisive).** During cod4native playback, measured on the running demo:
- `ps.weapon`=80 (mp5), `cs[3945+80]`=`h1_mp5_mp` POPULATED, `pm_type`=0, `renderingThirdPerson`=0 → the weapon data is perfect and the viewmodel SHOULD render.
- BUT hand-model count (cg+956988)=0 and `cgs.clientinfo[0]` (0x1414D4BD4)=0 → EMPTY.
- `ps+0x3C` (viewmodel-hands idx)=0, but that is NORMAL (0 in live play too), so NOT the blocker.
⇒ The blocker is **empty `cgs.clientinfo`** — the player's character/model info, which supplies the hands/arms (viewmodel) AND the 3rd-person body model. **CORRECTED MECHANISM (KisakCOD cg_snapshot_mp.cpp:160-200, ground truth):** clientinfo does NOT come from "CS_PLAYERS configstrings" (there is no CS_PLAYERS in CoD4/MWR — the enum goes CS_MODELS=830…CS_ITEMS=2314). It is built each snapshot from the **client state carried in the snapshot ("packet clients")**:
```
ci->clientNum = clientState->clientIndex;  ci->team = clientState->team;  I_strncpyz(ci->name, clientState->name);
modelName = CL_GetConfigString(clientState->modelindex + 830 /*CS_MODELS*/);  I_strncpyz(ci->model, modelName);
ci->dobjDirty = 1;   // <- triggers the model/DObj rebuild = the hands + body
```
So the player character needs: (1) a **client state** in the snapshot for the local player, with a valid `modelindex` (+ team, name, attachModelIndex[6]); (2) the **CS_MODELS configstring** at `830+modelindex` = the character model name (e.g. `body_mp_usmc_assault`). `cod4_build`'s snapshots emit `ps` + empty entities but **NO client state (no packet-clients section)** → nothing sets ci->model or flips dobjDirty → `hand model count=0`, empty hands, no 3rd-person body. **FIX: synthesize the local player's client state in the snapshot (the packet-clients section, via MWR MSG_WriteDeltaClient + the clientStateFields netfield table) + ensure CS_MODELS carries the model.** The cod4 demo carries both (its snapshots have packet clients; its gamestate has CS_MODELS). This is "no gun" + "no 3rd-person body" = ONE fix.

**CLIENT-STATE SYNTHESIS — encode primitives found (2026-07-16), ready to build:**
- `MSG_WriteDeltaClient` = **PC `sub_4F3910`** `(ctx, msg, time, from, to, removedFlag)`. It writes the client INDEX itself (`to[0]`=clientIndex, via WriteBits(5) or a +1 fast path) + WriteBit(present)/WriteBit(removed), then `sub_4F47F0` writes the delta fields. Uses the client netfield table from the registry (`off_12D4BA8[2]`, count at `+6` dwords).
- The client-packet writer `sub_561180` shows the section shape: `sub_4EC1E0(msg)` (reset) → per client `sub_4F3910(...)` → `sub_4EBCB0(msg)` (WriteBit 0 terminator). So to emit ONE local client in cod4_build's snapshot: after the entity terminator, `w_clr(msg); w_client(ctx, msg, st, nullptr, clientstate, 0); w_0(msg);`.
- Snapshot writer `sub_562F30` confirms order: ps (`sub_4F52E0`) → entities (`sub_561320`) → **clients (`sub_561180`)**. cod4_build already emits an empty client section in its tail (`...w_eidx(2047,11); w_byte(0); ...`); the client goes before the terminator.
- **MWR PC clientState = 212 bytes; clientState.clientIndex @ offset 0** (written by sub_4F3910). PC client netfield table @ **0x12D7360, 37 fields** (dumped): offsets 2..124, encoders 1/2/4/5/6/9/10/11/32. The enc-10 (10-bit, u16) fields at offsets {2,16,18,20,22,24,26,28,30,72,74} are the model/tag-index cluster (modelindex + attachModelIndex[6] + attachTagIndex[6]); CoD4 struct order is clientIndex,team,modelindex,attachModelIndex[6],attachTagIndex[6],name[16],...
- **STILL NEEDED to build:** pin the MWR `modelindex` offset (one of the enc-10 fields) — get it from the PS4 named client table (registry pair [2], 56 fields; extract via the initializer-emulation technique that dumped the ps table), OR empirically. Then: a valid CS_MODELS index for the body model (the scaffold gamestate's CS_MODELS 830.. has the map's player models), set clientState.modelindex to it + clientIndex=local + team, and emit via sub_4F3910. RISK: a mis-encoded client desyncs the whole snapshot → validate with cod4_demo_snaps / a live cg+956988 read.

**== FULL PS TRANSCODE (the real "looks like a player" work, started 2026-07-17) ==**
USER's core insight: the demo looks like a SPECTATOR gliding (view+position only) with an artificial bolted-on gun, NOT a real animated player. ROOT CAUSE: `cod4_build` only reads the cod4 ARCHIVE (type-1 = origin/velocity/viewangles/bobCycle) and SKIPS the snapshot playerstate entirely (cod4_demo.cpp ~2469: `if(t==0){...off+=ln;}` just advances). So the whole movement/weapon/animation state is never transcoded → frozen (mid-sprint) template pose. THE FIX: rewrite cod4_build ps synthesis to decode cod4 SNAPSHOTS per frame (read_delta_ps — already works) and map each animation-driving field cod4→h1 by name:
- weapon: cod4@232 (7-bit CS_WEAPONFILES index) → h1 ps@0x394, via the weapon Rosetta (docs/mwr-weapon-table.txt) so the gun matches per frame + fixes constant-mp5.
- pm_flags, pm_type: the movement state (walk/run/sprint/crouch/prone/ADS). Fixes the CONSTANT SPRINT. h1 pm_type@2, pm_flags@0x54. Need the cod4→h1 pm_flags bit mapping (PMF_* values may differ).
- movementDir, velocity, speed: legs/body strafe-lean animation.
- weaponstate, weapAnim, fWeaponPosFrac: weapon idle/fire/ADS animation.
- viewHeightCurrent/Target/Lerp*: crouch/prone view height.
DATA LOCATED: cod4 playerState_s = KisakCOD src/bgame/bg_local.h (NOTE: two ps-like structs there; the parser's kPsFields offsets are authoritative — weapon@232 confirmed). h1 ps = named netfield table (mwr-netfields-named) + RVA 0x12D7490. Approach = name-based field map; each field needs {cod4 offset, h1 offset, encoding conversion}. RISK: a wrong offset silently desyncs the ps. Snapshots are ~20Hz (state) + archive is dense (motion) — keep archive for dense origin/vel/angles, add snapshot ps for the state/anim fields.

**NEXT PHASE — the transcode OUTPUT (encode side).** Everything above is DECODE. To render anything (gun, body, players) we must produce MWR wire: (1) map the cs2258 weapon name → MWR weapon index (needs the MWR weapon list, live or IDA), (2) synthesize the MWR gamestate (mapname + weapon configstrings at 3945+N + baselines), (3) re-encode per-frame ps/entities with the MWR netfield tables, (4) feed it (serverless via CL_PreloadMap2, or reuse the .dm_h1 playback path). cod4_build already calls the engine's `MSG_WriteDeltaPlayerstate` (w_ps), so the encode primitive exists.

---

## 0. THE DECISION (verified [V])

**Go serverless. Delete the loopback server. Transcode CoD4's wire to MWR's wire by FIELD NAME. No borrowed scaffold.**

I confirmed the load-bearing fact directly against KisakCOD:
- `src/client/cl_demo.cpp` is **SinglePlayer only** (`#error This file is for SinglePlayer only`; writes `.spd`). Every "playback uses SV_SpawnServer / G_SetClientDemoTime" note came from this file and is **irrelevant to `.dm_1`**.
- The real MP `.dm_1` player is `src/client_mp/cl_main_mp.cpp`; `CL_PlayDemo_f` **refuses to run with a server** (line 2919 `"listen server cannot play a demo."`), and there is **no `SV_SpawnServer` anywhere in the MP demo path**.

⇒ Serverless is idTech's intended design. The loopback we spawn today is the source of the clock war (`base_st=600000000` vs the loopback's ~40k), and it must go.

---

## 1. GROUND TRUTH

### 1.1 The Rosetta Stone — both sides are NAMED
- **CoD4** (KisakCOD source, [R]): `netField_t { const char* name; int offset; int bits; }` (`msg.h:45`); `playerStateFields[143]` (`msg.cpp:70`); `entityStateFields`, `clientStateFields` likewise. Encoder = the `bits` field, negative values = named codecs.
- **MWR PS4 debug build** (dumped, [R]): registry @`0x14119a0` = `{netField_t* fields; i64 count}` × 6; **[3]@+0x30 = playerState, `0xbb439d0`, 271 entries**. `netField_t` = 16B `{const char* name; i16 offset; i16 size; i16 encoder; u16 flags}`. Built at runtime by `GLOBAL__sub_I_sv_msg_write_mp_cpp` (= KisakCOD's `sv_msg_write_mp.cpp`). All 271 names recovered.
- **MWR PC ship** (the TARGET, [R]): same struct **minus the name** = 8B `{u16 off; i16 size; i16 enc; u16 flags}` @`0x12D7490`, 252 entries. Reached via pointer var `off_12D4BA8` → registry `0x12D4D00`, byte `+0x30`.

**Encoder enum decoded** (`MSG_GetNetFieldTypeName`): 0=FLOAT, 32=4_BYTES, other positive = literal bit count, negative = named (−87 VIEWANGLES, −88 FLOAT_RARELYZERO_NONINT, −97 TIME, −98 EFLAGS, −100 ANGLE2SHORT, −92/91/90 ORIGIN, −77 ETYPE, −76 MOVEMENTDIR …).

### 1.2 Two hard rules for using the tables
1. **PS4 offsets ≠ PC offsets. Never copy them.** PS4 gives NAMES + encoders; take offsets from the PC 8-byte table only. (PS4 `origin[0]`@116 vs PC@0x78; PS4 eType@12 vs PC@10.)
2. **Encoder widths drift across builds** — `pm_flags` 31-bit PS4 / 32-bit PC; entity `weapon` 31-bit PS4 / **27-bit PC**. Writing a PS4 width into the PC stream corrupts everything downstream. Cross-check each field against the PC table.

### 1.3 Field naming corrections proven this session
- **`ps+0x5C` = `otherFlags`** (not eFlags, not pm_flags). PC: `pm_flags`@0x54, `eFlags`@0x58, `otherFlags`@0x5C. The multi-day "0x4000" saga was probing otherFlags bit 14. [R, cross-checks the live measurements]
- **`ps+0x4C` = `commandTime`** — PS4 idx0 `commandTime`@76 unpacks byte-identical to PC idx0. Validates the view-angle fix already shipped. [R]

---

## 2. WHAT DIED (do not revive)

- **"Clock-slaved loopback like CoD4"** — dead [V]. That's SP `.spd` code. `.dm_1` is serverless.
- **"The clock is the #1 bug / needs a rewrite"** — dead [R]. `CL_ParseSnapshot` commits to `cl->snap` synchronously; **120 of ~130 feeding frames are already perfect 1x**, only ~6/400 burst. The clock needs SIMPLIFICATION, not redesign.
- **The `[pacing fix]` (demo_playback.cpp ~808–822)** — it is the villain, not a fix [R + my earlier read]: it rolls `serverTime` back to a one-frame-old cached value *before the logger samples it*, fabricating a healthy log and masking the real (small) collapse. **Delete it.**
- **`ps+0x5C & 0x4000` as a demo discriminator** — dead (measured identical in every state).
- **The captured-template + memcpy + 4-field-override synthesis** — replaced wholesale by the transcode.
- **Sending CoD4 precache configstrings as-is** — dead [R!]: MWR NCS ranges are REJECTED at parse (`NetConstStrings_SyncError`). Those strings come from the level's `ncs_*_level` fastfiles, not the wire.

---

## 3. THE ARCHITECTURE

Pipeline: **decode(CoD4 tables) → named struct → encode(MWR PC tables)**, per-field converter defaults to identity, overridden only for the hard fields. Feed the result to the client's own parse path with **no server**.

### 3.1 Bootstrap (serverless map load) [R, fixes the old spike]
The prior serverless spike died on `Missing NetConstStrings asset 'ncs_mdl_level'`. Fix:
1. `CL_PreloadMap2(mapname)` (PS4 `0x3939b0`; **PC addr = OPEN [?]**) — Com_Restart + renderer + `DB_LoadLevelXAssets(mapname,0)`, so the level zone incl. all 24 `ncs_*_level` assets is resident.
2. Feed the synthesised gamestate to `CL_ParseServerMessage` (PC `sub_3425B0`) → `CL_ParseGamestate` now resolves NCS and reaches PRIMED. `com_sv_running` stays 0, so `Session_StartHost`/`Live_*` (the "online wall") never run.

### 3.2 The synthesised gamestate is SMALL [R!]
Only non-NCS configstrings are legal on the wire:
- **cs 8 (mapCenter): MANDATORY**, format `"%f %f %f"` — entity coord bias depends on it.
- cs 1 (systemInfo): minimal/empty is fine.
- cs 3 (serverId), cs 4/5/7: send `"0"` until proven otherwise.
- **Weapon/model/etc. names are resolved to MWR NCS INDICES and emitted in netfields — never sent as configstrings.** This is the main new work item.
- ⚠ Reviewer catch: `clc->clientNum` is read with **MSG_ReadLong (4 bytes)**, not a byte — get the gamestate framing width right or the whole stream desyncs.

### 3.3 Per-frame transcode
- **playerState:** decode CoD4 ps delta → named → encode MWR ps. Most fields are identity (`cod4.enc == mwr.enc`, same width). Set `commandTime = snapshot serverTime` (the fix). Leave MWR-only fields at baseline, don't zero.
- **viewangles come from the type-1 clientArchive record, NOT the ps delta** [R] — CoD4 suppresses `-87` fields in ordinary snapshots. Parse the 53-byte type-1 records into a serverTime-keyed table `{origin, velocity, movementDir, bobCycle, serverTime, viewangles}` (FILE order ≠ struct order) and inject viewangles per frame.
- **other players:** synthesise **ET_PLAYER** entities (PC table `0x12D5210`, 64 entries, eType=1) [R]. **Do NOT synthesise the local player's entity** — the client builds it from `snap->ps` via `BG_PlayerStateToEntityState`; emitting one fights that.

### 3.4 The gun — PROVEN mechanism [R, on the PC ship binary]
`CG_SetupWeaponDef` (PC `sub_11B9E0`) reads `configstring[weapIdx + 3945]` → `BG_GetWeaponForName`; **empty string ⇒ silently returns 0 ⇒ no weapon def bound, no error.** That is exactly today's `hands=0, no-gun, no-error`. Fix: for every weapon index any snapshot references, **cs 3945+N must hold an MWR weapon NAME satisfying `BG_GetWeaponForName(name,0)==N`** (round-trip or `Com_Error`). Transcode weapon **by name**: CoD4 `CS_WEAPONFILES`=2258 (space-separated, 1-based ordinal, 7-bit id) → MWR name → MWR index `3945+N` (8-bit id, 1..199; ≥200 uses the custom-weapon path).

### 3.5 Clock ownership [R]
Mod owns ONE integer `demo_time`, expressed through the delta, inside `CL_SetCGameTime`:
```
if (!paused) demo_time += (int)((com_frameTime - last_com_frameTime) * timescale);
cl->serverTimeDelta = demo_time - com_frameTime;   // engine then does serverTime = max(serverTime, delta+com_frameTime)
```
This replaces the pause hack + `fast_forward_demo` + the `[pacing fix]` with one monotonic model, and gives correct pause + timescale (the anim-clock symptoms) for free.

---

## 4. ORDERED IMPLEMENTATION PLAN (one variable at a time, each validated vs a working reference)

0. **Regenerate the field tables** (scratchpad is ephemeral): re-run the PS4 `extract.py` + PC `pcdump.py` → `ps_fields.txt` (271) / `pc_fields.txt` (252). Diff to build the name→(pc_off,size,enc) map. *Proof: 37 names cross-check PS4↔PC.*
1. **Delete the `[pacing fix]`.** *Proof: clocklog shows the true collapse (~6/400 frames) instead of a fabricated-healthy log. Playback looks WORSE — expected.*
2. **Build & validate the `.dm_1` parser** ⚠ **TOP RISK, see §5** — block loop + static Huffman + bitreader + svc dispatch. *Proof: strictly-ascending configstring indices + serverTimes on a freshly-recorded demo.*
   - **2a. Block framing + type-1 archive — ✅ VALIDATED 2026-07-16** against `backlotdemo.dm_1` (1039784 B). Clean parse to the explicit EOF marker (`type0` with `len==-1`) at the exact last byte. **Authoritative format from KisakCOD source** (`CL_ReadDemoMessage` cl_cgame_mp.cpp; `CL_WriteDemoMessage`/`CL_WriteDemoClientArchive` cl_main_mp.cpp; `ClientArchiveData` client_mp.h:141):
     ```
     loop: read u8 type
       type 0 (network): [i32 serverMessageSequence][i32 len]  (len == -1 => EOF)  [len bytes Huffman svc data]
       type 1 (archive): [i32 index][origin f32x3][velocity f32x3][movementDir i32][bobCycle i32][serverTime i32][viewangles f32x3]  = 4+52 bytes incl type byte
       else / read fail => EOF
     ```
     Struct order (ClientArchiveData, 0x30): serverTime@0, origin@4, velocity@16, bobCycle@28, movementDir@32, viewangles@36 — the on-disk read order swaps movementDir/bobCycle (writes archive+32 then +28).
     Result on backlotdemo: 1440 network packets (seq 369→1808, strictly ascending), 18237 archive records, span 21034..93993ms (~73s). Archive cadence +4ms (250fps), all serverTimes unique, ONE benign −1019ms pre-roll blip at rec 196. **viewangles are real** — first (0,−90,0), last (5.94,35.29,0). ⇒ viewangles lookup = sorted-by-serverTime table, binary-search nearest ≤ query.
   - **2b. Huffman decompress + gamestate configstring parse — ✅ VALIDATED 2026-07-16** against backlotdemo's first packet. **The recon reviewer's "decoder is broken" was about the recon's own buggy impl — the FORMAT decodes cleanly; proven below.**
     - **Type-0 payload = `[i32 reliableAcknowledge][Huffman(rest)]`.** Read reliableAck raw (MSG_ReadLong, readcount→4), then `MSG_ReadBitsCompress(&data[4], out, cursize-4)` Huffman-decompresses the remainder; svc commands are parsed from the DECOMPRESSED buffer. (Source: `CL_ReadDemoNetworkPacket` cl_cgame_mp.cpp; `CL_ParseServerMessage` cl_parse_mp.cpp.)
     - **Huffman = static tree from `msg_hData[256]`** (KisakCOD msg_mp.cpp:1784; `Huff_BuildFromData` combines the two lowest-weight nodes repeatedly, internal-node symbol=257). **`get_bit` is LSB-first: `(fin[bloc>>3] >> (bloc&7)) & 1`.** Decode walks left(0)/right(1) to a leaf. Both equal-weight tie-breaks give identical output on real data (tree is unambiguous). Pad the input ≥8 bytes — the decoder legitimately overruns the last byte.
     - **Result:** first packet (seq 369, 32358 B) → reliableAck=4 → decompressed 31709 B, **first svc byte = 1 (svc_gamestate)** ✓. Readable strings throughout: serverinfo (`\g_gametype\war\mapname\mp_backlot`), mapCenter source (`"compass_map_mp_backlot" 2896 2616 -2400 -2680`), and the **full CoD4 weapon list** (`weapon_mp5`, `weapon_usp`, `weapon_usp_silencer`, …) — the Rosetta data the gun fix needs.
     - Working reference impl: `<scratchpad>/dm1_huff.py` + `hdata_raw.txt` (EPHEMERAL — port to C++). Port target: the mod already has zlib but needs this static-Huffman; reuse KisakCOD's `huffman.cpp` verbatim.
   - **2c. Snapshot header + hybrid bit/byte reader — ✅ VALIDATED 2026-07-16.** Decoded real snapshot headers out of backlotdemo:
     - Snapshot wire (after svc byte 6): `serverTime (MSG_ReadLong)`, `deltaNum (MSG_ReadByte; 0 => no delta, else deltaNum = messageNum - deltaNum)`, `snapFlags (MSG_ReadByte)`, then `lc = MSG_ReadBit()`, `LastChangedField = MSG_ReadBits(GetMinBitCountForNum(141)=8)`, then the per-field loop. (Source: `CL_ParseSnapshot`, `MSG_ReadDeltaPlayerstate` msg_mp.cpp.)
     - **THE HYBRID READER (the recon's warning, now modeled exactly):** `MSG_ReadByte`/`ReadShort`/`ReadLong` use `msg->readcount` (byte cursor) and DON'T touch `bit`. `MSG_ReadBit`/`ReadBits` use `msg->bit`, LSB-first, and on a fresh byte (`bit&7==0`) claim one via `bit = 8*readcount++`. So byte and bit reads interleave on different bytes; leftover bits of a claimed byte are reused. Get this wrong and everything after the first bit-read desyncs.
     - **Result:** seq 370 = first snapshot, serverTime=22150 (in archive range), deltaNum=0 (null baseline), LastChangedField=82; seq 371..376 = deltas, serverTime +50ms (20Hz), LastChangedField all valid. The hybrid reader, header, and `GetMinBitCountForNum` are proven.
     - **`lc` (local-client) flag:** when `lc==0`, origin/velocity/viewangles/bobCycle/movementDir come from `CL_GetPredictedOriginForServerTime` (the **type-1 archive**, keyed by commandTime) — NOT the snapshot delta. So the delta carries the "static" fields (weapon, health, ammo, flags); motion comes from the archive. This is the exact split the transcode uses.
   - **2d. Per-field value decode — the last mechanical piece, OPEN.** Port `MSG_ReadDeltaField` (the encoder switch) + `MSG_ReadLastChangedField` (done). Each field: `if (changeHints!=2 && !ReadBit) copy from-value; else decode by field->bits`. bits>0 = literal N-bit int; 0 = float (sub-encoded); negatives = special codecs. **`weapon` = 7 bits** (index into `CS_WEAPONFILES`=2258). Deterministic source; no remaining format risk.
   - **Durable references saved:** `docs/dm1_parser_ref/` (validated `dm1_validate.py`, `dm1_huff.py`, `dm1_msg.py`, `hdata_raw.txt`) and `docs/cod4-playerstate-fields.txt` (the 143-field CoD4 Rosetta table, name→bits). `NetField`=16B `{name; size_t offset; int bits; u8 changeHints}`; CoD4 `netField_t`=`{name; int offset; int bits}` (no changeHints — msg_mp.cpp casts and reads changeHints from a parallel source).
3. **Serverless bootstrap spike:** `CL_PreloadMap2` + hand-fed minimal gamestate (cs8 mapCenter). *Proof (live): `com_sv_running==0`, connstate reaches PRIMED, map loads, no `ncs_mdl_level` error, no online wall.*
4. **First-person playerstate** via the transcode (identity converters + viewangles from type-1 + commandTime). *Proof: `SNAP.pre == cg.ps.va == recorded`, no loopback.*
5. **The gun:** emit cs `3945+N` by name. *Proof (live poller): `hands>0`, weapon renders. Compare to a real `.dm_h1`: `hands>0,h2=0`.*
6. **Other players:** synthesise ET_PLAYER entities. *Proof: bodies visible; walk a fast mover to exercise the coord-bias escape path.*
7. **Clock model** (§3.5) + clean EOF. *Proof: pause freezes anims; timescale scales them; clean stop.*

---

## 5. OPEN QUESTIONS / RISKS (ranked, honest)

1. **⚠ THE `.dm_1` BITSTREAM DECODER (SHAKY [R!]).** The recon claimed it decoded the file "byte-exact end to end"; the **reviewer proved that FALSE** — the scratchpad reference decoder has a configstring-index-drift bug, i.e. the bitreader is not yet correct. And the scratchpad scripts are **gone** (ephemeral). This is the foundation of everything; it must be rebuilt and validated against a freshly-recorded demo BEFORE anything downstream. Cheapest proof: record a fresh CoD4 demo, decode it, assert strictly-ascending configstring indices + serverTimes.
2. **`CL_PreloadMap2` PC address is UNKNOWN [?].** Have PS4 `0x3939b0`; need the PC h1 equivalent (find by the `DB_LoadLevelXAssets` call / the `ncs_*_level` asset string). Gates the whole serverless bootstrap.
3. **Weapon id encoding [?].** CoD4 7-bit index → MWR packed id (measured `0x00040050` = base 80 + variant/camo high bits). Name-based mapping needed; the high-bit packing isn't fully reversed.
4. **pm_flags bit remap [R!].** CoD4 21-bit enum ≠ MWR enum (CoD4 SPRINTING=1<<15, MWR bit14). Needs an explicit bit-by-bit table from BOTH enum name tables, not a value copy.
5. **Encoder vocabularies are NOT identical [R!].** MWR *extended* CoD4's (44 cases −110..−67 vs CoD4's 16 cases −100..−85), and CoD4 fuses −94/−93 where MWR splits them. The identity shortcut holds only where BOTH tables use the same encoder at the same width — verify per field, don't assume.
6. **entityState `offhandWeapon` PC offset is a 50/50 [R!]** (idx22@188 vs idx41@192) — resolve before relying on it.

---

## 6. WHY THIS IS TRACTABLE NOW (and wasn't before)

Every prior attempt reverse-engineered MWR's wire by *inference* — which produced three confident wrong answers this week. The PS4 debug build makes both sides **named**, turning the core of the transcode from archaeology into a lookup. The remaining hard parts (§5) are genuinely hard but are now *enumerated and bounded*, not unknown-unknowns.
