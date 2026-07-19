> **SOURCE:** 14-agent clean-slate RE workflow (9 subsystem maps + 4 adversarial verifiers + max-effort synthesis),
> run 2026-07-15 against h1_mp64_ship + iw3mp.exe + KisakCOD. ~1.8M subagent tokens, 440 tool calls, 52 min.
> The MWR-PS4 named PDB was UNREACHABLE to all agents - nothing here is PDB-sourced.
>
> ## !! READ THIS FIRST - THE HEADLINE CLAIM IS FALSIFIED !!
> Section 3's root cause ("ps+0x5C & 0x4000 is clear; real demos set it because a demo viewer IS a
> follower") is **WRONG**, disproved 2026-07-15 by live + on-disk measurement:
>   * live private match  cg->ps+0x5C = 0x00008000  (CheatEngine, backlot, connstate 10)
>   * REAL backlot demo   cg->ps+0x5C = 0x00008000  (cod4_ps_template.bin, captured during `demoplay backlot`)
> ps+0x5C is BYTE-IDENTICAL between live play and real demo playback - it does not even appear in the
> 33-byte diff. So 0x4000 is NOT the discriminator and Step 1 must NOT be implemented as written.
> The MECHANISM (writer suppression in sub_4F2FE0 + reader clobber in sub_32B120, both gated on that bit)
> may still be real - what is false is that real demos set it. Open question: how do real demos avoid the
> clobber? Candidates from the 33-byte diff: pm_flags bit 0x0800 (ps+0x55, set ONLY in the demo),
> cg[1536081] gate, or h1-mod's demo component writing angles itself.
>
> Everything else below (netfield table 0x12D7490/252, ps+0x12C = viewangles float degrees, the ctx layout,
> the client coord bias at 0x2ED20A8, no-native-demo-player, architecture A) is independently corroborated.

---

I have enough to adjudicate every major contradiction. Writing the document.

---

# COD4 `.dm_1` → MWR (h1) — DEFINITIVE ARCHITECTURE

**Verification status:** all H1 addresses are RVAs (runtime base `0x140000000`). Items marked **[V]** I verified myself this session against `h1_mp64_ship` via IDA. **[R]** = confirmed by a subsystem report with an address I did not re-derive. **[I]** = inferred. **The MWR-PS4 named-PDB server was unreachable for all 13 agents** — no finding anywhere in this packet is PDB-sourced, and every non-`sub_XXXXXX` name is logic-derived. Re-audit any memory note claiming PS4 provenance.

---

## 1. GROUND TRUTH TABLE

### 1.1 The netfield system

| Fact | Value | Status |
|---|---|---|
| `off_12D4BA8` is a **pointer variable**, not a struct | `*(void**)0x12D4BA8` = **`0x12D4D00`** | **[V]** |
| Registry at `0x12D4D00` = `{void* table; i64 count}[]` | `[0]`=0x12D6E40/71, `[1]`=0x12D7080/92, `[2]`=0x12D7360/37, **`[3]`=0x12D7490/252**, `[4]`=0x12D7C70/7, `[5]`=0x12D7CB0/44, `[6]`=0x12D4E10/63 | **[V]** |
| **playerState netfield table** | **`0x12D7490`, 252 entries** (Hex-Rays `off_12D4BA8[6]` = `*(0x12D4D00+0x30)`; count `*((DWORD*)off_12D4BA8+14)` = `*(0x12D4D00+0x38)` = 252) | **[V]** |
| Loader disasm | `0x4F5502 mov rax, cs:off_12D4BA8` / `0x4F551A mov r14d,[rax+38h]` / `0x4F551E mov rax,[rax+30h]` | **[V]** |
| Max offset in ps table | `0x488C` (= 18572, consistent with `sizeof(playerState)`=18576) | **[V]** |
| `netField_t` = **8 bytes** | `{u16 offset; i16 size; i16 encoder; u16 flags}`; `size` sign = signedness, magnitude = byte width | **[V]/[R]** |
| entityState eType dispatch | `*(void**)0x12D4BA0` = **`0x12D4BB0`**, 21 × 16B, index = `min(eType,20)` (`sub_4F2C30`) | **[R]** |
| **ET_PLAYER table** | **`0x12D5210`, 64 entries** (0x12D5210 + 64×8 = 0x12D5410 = next table) | **[V]/[R]** |
| `sizeof(entityState_s)` | **256** (`dword_10B2D44`) | **[R]** |

### 1.2 The playerState fields that matter (all **[V]**, dumped from `0x12D7490`)

| ps off | idx | size | encoder | flags | Meaning |
|---|---|---|---|---|---|
| **0x12C** | **34** | 4 | **-87 (0xFFA9)** | 0x210 | **viewangles[0]** (pitch) |
| **0x130** | **32** | 4 | **-87** | 0x000 | **viewangles[1]** (yaw) |
| **0x134** | **39** | 4 | **-87** | 0x010 | **viewangles[2]** (roll) |
| **0x5C** | **56** | -4 | 31 (plain) | 0 | **flags dword carrying bit `0x4000`** |
| **0x3C** | **58** | 2 (u16) | 10 bits | 0 | **viewmodel-hands index → configstring `1240+n`** |
| 0x02 | 91 | 1 | 4 bits | 0 | `pm_type` |
| 0x20 | 63 | -2 | 11 bits | 0x80 | linked-entity num (implicit default 2047) |
| 0x58 | 30 | -4 | -98 (0xFF9E) | 0 | `eFlags` (29-bit, one-bit-change fast path) |
| 0x74 | 15 | **-4 (int32)** | 16 bits | **0x14** | `bobCycle` (**16-bit cyclic**, not float, not u8) |
| 0x78/7C/80 | 12/14/13 | 4 | -88 (0xFFA8) | **0x4** | `origin` (XOR-delta, 32-bit raw) |
| 0x84/88/8C | 17/16/19 | 4 | -88 | **0x14** | `velocity` |
| 0xC8 | 18 | -4 | 8 bits | **0x4** | `movementDir` |
| 0x5EC | 10 | -4 | **-70 (ALWAYS SEND)** | **0x4** | — |
| 0x13C | 29 | 4 | -88 | 0 | `viewHeightCurrent` (float) |
| 0x394 | 47 | 4 | 27 bits | 0 | `weapon` (**only low byte indexes `off_389EB40`**) |
| **0x5E0/5E4/5E8** | 227/228/229 | 4 | **0xFFA4/A5/A6** | 0 | **bias-dependent vec3 — see §1.5** |

**The complete `flags&4` ("predicted") set is exactly 9 fields [V]:** `0x5EC, 0x78, 0x7C, 0x80, 0x74, 0x84, 0x88, 0x8C, 0xC8`. Nothing else.

**`-87` (0xFFA9) occurs exactly 3× in all 252 entries [V]** — only 0x12C/0x130/0x134. There is no separate transmitted `delta_angles` vec3 in MWR's ps.

### 1.3 The wire envelope — `sub_562F30` = the REAL snapshot writer **[V]**

Verified at instruction level (`0x56316D`–`0x56320B`):

```
0x56316D  mov edx,6 / lea r8d,[rdx-3] / call sub_4EBD40   ; MSG_WriteBits(msg, 6, 3)  <- svc_snapshot, 3 BITS
0x56317E  mov edx, dword_B7F9B88     / call sub_4EBFF0    ; MSG_WriteLong(serverTime) <- 4 RAW BYTES at byte cursor
0x56318F                               call sub_4EBF30    ; MSG_WriteByte(deltaNum)
0x5631C4  or edx,2 / cmovnz edx,r13d / call sub_4EBF30    ; MSG_WriteByte(snapFlags)  ; bit1(0x2)=NOT_ACTIVE
0x5631D7  test byte ptr [rdi+4A50h],8                     ; frame+19024 & 8 -> force keyframe
0x5631E3  call sub_4EBCF0   (WriteBit 1) + r9=rdi         ; hasDeltaPS=1, from=baseline
0x5631F0  call sub_4EBCB0   (WriteBit 0) + xor r9d,r9d    ; hasDeltaPS=0, from=NULL
0x5631FF  lea rcx,[rbp+0F0h+var_C0]                       ; a1 = ctx  (ctx base)
0x56320B  call sub_4F52E0                                 ; MSG_WriteDeltaPlayerstate(ctx,msg,time,from,to)
```

Reader mirror `sub_342770` (CL_ParseSnapshot): `ReadLong(serverTime)` @0x3427E2 → `ReadByte(deltaNum)` @0x3427FC → `ReadByte(snapFlags)` @0x34282B → **`MSG_ReadBit` (`sub_73F90`) @0x3428BF selects `from`** → `sub_4EE4E0(...)` @0x3428F8. **[R]**, and the writer side is **[V]** — the `hasDeltaPS` bit is real and is emitted by the *envelope*, not by `MSG_WriteDeltaPlayerstate`.

**The MSG is a hybrid bit/byte stream [R].** `sub_4EBFF0` (WriteLong) writes 4 raw bytes at `cursize` and *does not advance the bit cursor at msg+40*; bit writers lazily claim bytes from `cursize`. After `WriteBit; WriteLong;` later bits back-fill the byte *before* the long. **Never model this as a flat bitstream. Use `sub_4EB1D0` + the engine primitives.**

### 1.4 `ctx` — layout and the `ctx[29]` question **[V]**

`sub_562F30`'s ctx base = `rbp+0xF0`; frame slots `var_A3`@0x10D and `var_A2`@0x10E ⇒ **ctx+0x1D (=ctx[29]) and ctx+0x1E (=ctx[30])** exactly where `alternative-designs`/`v-ctx` placed them. **[V]**

| ctx | `sub_562F30` (live client) | `sub_51DB60` (archive) | Read by |
|---|---|---|---|
| `[0]` byte | `clientNum` @0x562FEA | **uninitialized** | `4F52E0`@0x4F5B08 (emits a bit), `4F28D0`@0x4F29EC |
| `+4/+8/+12` **float** | `dword_B7F9C08/0C/10` (`movss` **[V]**) | same | `sub_4F7040`@0x4F70C9, `sub_4F7100`@0x4F716A |
| `+0x10` ptr | `client_t*` @0x562FF0 | uninitialized | `4F52E0`@0x4F53EF (**null-deref if ctx29=ctx31=0**) |
| `[29]` | **0** @0x562FFA | **1** @0x51DBD2 | `0x4F53E5`, `4F2FE0`@0x4F2FE4, `0x4F562E` |
| `[30]` | `(from != 0)` @0x563055 | — | entity/event path |
| `[32]`,`[34]` | 0 / computed | — | `[34]` = `ctx29 \|\| v82` |

**Allocate ctx as ≥120 zeroed bytes** (`sub_562F30` does `memset(base+48, 0, 120)` @0x562FA0 **[R]**) — callees index to `ctx+0x65`.

**What `ctx[29]=1` provably does [V]** — disasm `0x4F54DF`–`0x4F54FD`:
```
loc_4F54DF:                       ; taken when ctx[29] || ctx[31]
  mov [rsp+var_4967], 0           ; v83 = 0
  mov [rsp+var_4966], 1           ; v84 = 1
  mov [rsp+var_4968], 1           ; v82 = 1
  xor edx,edx / call sub_4EBEB0   ; WriteBit(msg, 0)   <- BIT #1 = 0
  mov dl,1    / call sub_4EBEB0   ; WriteBit(msg, 1)   <- BIT #2 = 1
```
⇒ `v83=0, v82=1, ctx[34]=1`: no field is suppressed by the `flags&0x1F0` path or the `flags&4`+`v83` elision, and **both `ctx+0x10` derefs are short-circuited away** (0x4F53EF, 0x4F5B2F) — which is why a serverless client needs it.

### 1.5 The coordinate bias — the finding no report made **[V]**

Encoders `0xFFA4/A5/AD/AE` → `sub_4F7040`; `0xFFA6/AF` → `sub_4F7100`. Both take `a3 = ctx+4` and bias the 16-bit escape path:

```c
// WRITER sub_4F7040:  v10 = floor(to) - floor(from)
//   |v10| < 64  -> WriteBit0; WriteBits(v10+64, 7)
//   else        -> WriteBit1; WriteBits(v10 - floor(bias) + 0x8000, 16)
//   axis select: v14 = ((enc+106) in {0,14,23}) ? 0 : 4   [8404993 = 0x804001]

// READER sub_4F08D0 (0x4F08D0):
//   bias source = (float*)&qword_2ED20A8  [+0] or [+1]  <-- CLIENT-SIDE GLOBAL
//   return ReadBits(16) - 0x8000 + floor(bias) + floor(from)
// READER sub_4F09B0 (0x4F09B0): movss xmm6, cs:dword_2ED20B0   <-- axis 2
```

**The client's bias vec3 is `0x2ED20A8 / 0x2ED20AC / 0x2ED20B0`, and it is written by `sub_3411A0` = CL_ParseGamestate** (`0x3411F1 mov cs:qword_2ED20A8, rax` — floats [0],[1]; `0x3411F8 mov cs:dword_2ED20B0, eax` — float [2]). **[V]**

The server's `dword_B7F9C08/0C/10` is only ever touched by `sub_51DB60` (read), `sub_562560` (write, svs→svsHeader shadow), `sub_562F30` (read) **[V]** — i.e. it is the *server-side shadow of the same quantity*.

**Consequence:** a client-side writer must set `ctx+4/+8/+12 = *(float*)0x2ED20A8/AC/B0`, **not** `B7F9C08`. Affected: **ET_PLAYER `pos.trBase[0/1/2]` = `0xFF96/97/98`** (idx 1/2/4, off 0x6C/0x70/0x74) **[V]** — i.e. **every entity's world position** — plus ps `0x5E0/5E4/5E8`. Writer and reader are exactly symmetric (`floor(to)-floor(from)-floor(bias)+0x8000` ↔ `+0x8000+floor(bias)+floor(from)`), so an incorrect bias silently teleports entities by a constant only when `|Δ| ≥ 64` — i.e. it works in testing and breaks on the first fast mover. **This single fact would have burned weeks of entity work.**

### 1.6 The `from == NULL` baseline — `sub_4EC4C0` **[V]**

`sub_4F52E0` @0x4F53C9/0x4F53D6: `if (!from) { memset(buf,0,18576); sub_4EC4C0(buf); }`. Reader does the **identical** thing @0x4EE558/0x4EE562 **[R]**. Full contents of `sub_4EC4C0` **[V]** — writes only offsets:

`14`=15, `26/30/36/42`=0x7FF7FFF, `34`=(u16)2046, `40`=(u16)2047, `50`=(u16)2047, `52`=(u16)800, `104/168/172/176/184/204`=0, `460/476/480`=1.0f, `940`=(q)0, `948`=1.0f, `7708`=1.0f, `7728`=6.0f, `7732`=1.7461f, `7744`=0, `7748`=30.0f, `7752`=90.0f, `7756`=2.0f, `7760`=4.0f, `7764`=30.0f, `7768/7772/7776`=90.0f, and `*(u16*)(ps+7784+36i)=2047` for i in 0..35.

> **It touches NONE of the nine `flags&4` fields, and none of `0x02`, `0x20`, `0x3C`, `0x5C`, `0x12C/130/134`.** All are **0** in the baseline. **[V]**

### 1.7 The view/render chain **[V]/[R]**

| Addr | Name (logic-derived) | Fact |
|---|---|---|
| `0xF5D40` | `CG_DrawActiveFrame` | sole real caller `0x34334D` in `sub_3432F0`; **`xor r8d,r8d` @0x343340 ⇒ a3 = `cg+18640` = 0 ALWAYS** **[V]** |
| `0x32B8E0` | snapshot→cg copy/lerp | called @0xF5FAA **[V]**; `0x32BA3D: if (cg+18640 \|\| (nextSnap->ps[92] & 0x4000)) return sub_32BCC0(a1,1)` **[V]** |
| `0x32BCC0` | `CG_InterpolatePlayerState(cn, grabAngles)` | copies `nextSnap->ps`→`cg+0` (18576), then **LerpAngles `cg+300/304/308` OUTSIDE the `if(grabAngles)` block** — unconditional **[R]×3** |
| **`0x32B120`** | **`CG_PredictPlayerState` (the real one)** | called @0xF6242 via `sub_32ABF0` **[V]**; `pmove_t` @ `unk_39101B0 + 392*cn`, `pmove.ps = cg+0`; `sub_33B8B0`=GetCurrentCmdNumber, `sub_33C060`=GetUserCmd, **`sub_2D18C0`=Pmove**, 127-cmd replay loop **[V]** |
| `0x1D5950` | `CG_CalcRenderingThirdPerson` | reads **`cg->snap->ps` (cg+18680)**, not the predicted ps; result → `cg+601092` **[V]** |
| `0x13DF80` | `CG_CalcViewValues` | `0x13E334: cg+912912..920 = cg+300..308`; `0x13E4D7: sub_59C7C0` (AnglesToAxis) → `cg+601164` **[R]** |
| `0x10BEE0` | `CG_OffsetFirstPersonView` | **runs in normal play**; *adds* bob to `cg+912912/916/920`; driven by `ps.bobCycle` **[R]** |
| `0x2ED20A8` | client coord bias vec3 | written by `sub_3411A0` = CL_ParseGamestate **[V]** |
| `0x73F90` | `MSG_ReadBit` | **[V]** (used as bias-escape selector in `sub_4F08D0`) |

---

## 2. CONTRADICTIONS — READ THIS SECTION FIRST

### ⛔ C1. "`ps+0x12C` is provably not on the wire" — **REFUTED [V]**
`snapshot-write` staked its entire report on this, calling the working diagnosis "a misdiagnosis… chasing a mechanism that does not exist." **It read raw bytes at `0x12D4BA8` and indexed there, instead of dereferencing the pointer variable.** That lands on `0x12D6240`/63 — which is the **eType-3 *entityState* table** (per `entities`' verified dispatch map). Its "max offset 0xF4, therefore ps+0x12C unreachable" is an observation about an entity table.

My dump: **table `0x12D7490`, 252 entries, max offset `0x488C`, and `ps+0x12C/0x130/0x134` are present as the only three `-87` fields.** Every "IMPLICATION" in that report that flows from the 63-entry table is void. (Its `sub_4EC4C0` dump, MSG hybrid-stream hazard, and `sub_4F6C10` statefulness findings are good and survive.)

### ⛔ C2. "`ps+0x12C` is `delta_angles`" — **REFUTED, unanimously**
It is **`viewangles`, float degrees.** Five independent proofs: only 3× `-87` in the table **[V]**; `CG_CalcViewValues` feeds it raw to `AnglesToAxis` with no `SHORT2ANGLE` **[R]**; `sub_32BCC0` LerpAngle-interpolates it (`×1/360`→`floor`→`×360`) **[R]**; live read was float `90.0` (as `delta_angles` int it would be `16384`) **[R]**; IW3 ground truth has `viewangles` @264 and `delta_angles` @100 as *different* fields, and `viewheight` float at h1 `0x13C` exactly fits CoD4's `float viewangles[3]; int viewHeightTarget; float viewHeightCurrent;` **[R]**.

**The code writing `f.va` into `ps[0x12C]` was always correct. The label was wrong, and the label sent three agents hunting for plumbing that doesn't exist.**

### ⛔ C3. "The archive ring eats origin/velocity — the transcode's core mechanism is invalidated" — **REFUTED [V]**
`demo-native` claimed `sub_3409C0`'s gate is "a hard-coded literal `1`" and therefore origin/velocity/bobCycle/movementDir "are never sourced from the network stream," declaring the design dead.

**`v155` is a *wire bit*, not a parameter.** `v-ctx` correctly identified the real prologue (`sub_11A0365E`; `sub_4EE63E` and `sub_361D25` are obfuscator-split chunks): `v155 = MSG_ReadBit()` @0x4EE5D8, `v152 = MSG_ReadBit()` @0x4EE5EB. And **[V]** the writer with `ctx[29]=1` emits **BIT#1 = 0** (`0x4F54E1`/`0x4F54F3`). So `v155 = 0`, the `if (v155) { sub_3409C0(...) }` gate @0x4EE821 **never fires**, and origin/velocity/bobCycle/movementDir are decoded from the wire. `demo-native`'s `mov [rsp+68h+var_40],1` @0x3428EB is the 6th argument — a *different* thing, mis-recovered inside a chunk-level decompile.

This also retroactively explains `snapshot-read`'s correct inference ("origin works ⟹ v155 is FALSE") and the original brief's "a hook on `sub_3409C0` never fires." All three are consistent. **Do not hook `sub_3409C0`. Do not emit a `predicted_data` chunk.**

### ⛔ C4. "The pinned view is an encoder bug" — **HALF THE STORY [V]**
Every report except `v-angles` looked only at `sub_32B8E0` (which has no Pmove) and concluded "demo playback runs no pmove; writing ps.viewangles is sufficient." **`view-chain` states this as its headline "ANSWER TO THE CRUX." It is wrong.**

`CG_DrawActiveFrame` calls **both** `sub_32B8E0` (@0xF5FAA) *and* `sub_32ABF0`→**`sub_32B120`** (@0xF6242) **[V]**. `sub_32B120` is the real `CG_PredictPlayerState`, and it clobbers the angles by **two** routes:

```c
if ( cg[18640] || (cg[92] & 0x4000) ) return 0;          // 0x32B15B  <- the ONLY escape
...
if ( ps.commandTime(cg+76) >= latestCmd.serverTime ) {
    if ( !cg[1536081] ) {
        cg[300] = cg[1536148];  cg[304] = cg[1536152];   // <<< ps.viewangles := LOCAL MOUSE ANGLES
        cg[308] = cg[1536156];
    }
} else { ... 127-cmd replay loop ... sub_2D18C0(pmove); }  // PM_UpdateViewAngles clobbers too
```

**Even a byte-perfect encoder renders `(0,0,0)`.** The writer suppression and the reader clobber are gated by *the same bit*, and the current design has it wrong on both sides at once.

### ⛔ C5. "`sub_51DB60` is the engine's snapshot writer" — **REFUTED**
`sub_51DB60` **never calls `sub_4F52E0`** (xrefs: `sub_51DF60`@0x51E0DC, `sub_51E610`@0x51E739/784/811, **`sub_562F30`@0x56320B**). It is the **killcam/theater archive recorder** — `sub_562B00` (gated `dword_2DCFE00==2 && sub_55DE70()`) → `sub_560790` → a 4 MB ring at `unk_29C0D80` with a 1200-entry index at `unk_2DC0D88`. The real writer is **`sub_562F30`**, which sets **`ctx[29]=0`** and `ctx+0x10 = svs.clients[i]`.

**Nuance that matters: `ctx[29]=1` is still the right choice for us — for a reason nobody stated.** It is not "write more fields" (with `ctx29=0, v82=1` you get identical field gating). Its real value is that it forces `v82=1`/`v83=0`, which **short-circuits away both `svs.clients` dereferences** (`0x4F53EF` via `ctx+0x10`, `0x4F5B2F` via `qword_B7F9B80 + 1011960*clientNum`). On a client with no server those are guaranteed crashes. **Right answer, wrong reason — and the wrong reason would have led us to copy `sub_51DB60`'s uninitialized `ctx[0]` and `ctx+4/8/12`.**

### ⛔ C6. "`from=NULL` risks a flag-4 writer/reader baseline desync" — **REFUTED [V]**
`v-ctx` flagged (honestly, as UNCERTAIN) that with `ctx29=1` the writer uses `FROM` as the flag-4 baseline while the reader computes `ZERO`. **This is provably inert when `from == NULL`:** `sub_4EC4C0` touches none of the nine flag-4 offsets **[V]**, so the synthetic baseline is `0` there and `FROM ≡ ZERO`. The two hypotheses `v-ctx` could not separate **do not need separating on the keyframe path.** This is a decisive argument for keyframe-only.

### ⛔ C7. "No gun is a viewmodel/DObj problem" — **REFUTED**
`sub_128360` **draws nothing** — it is an anim update (`sub_5C8640(dobj, dt, 1, 0)` = `dobj[56] += dt` + XAnim tree advance). A non-NULL `qword_390B130` proves only that `sub_129EA0` allocated a DObj and its clock ticks. The scene-submission gate is `sub_119350` @0x119372: `pm_type <= 4 && !cg[601092] && !sub_2C3760(cg)`, where **`cg+601092` = `renderingThirdPerson`** from `sub_1D5950`. Stop investigating `off_389EB40` (provably non-NULL — `sub_E9950` would have null-derefed).

### ⛔ C8. "`sub_5CB80(clientNum)` is a per-client getter" — **REFUTED**
It takes **no arguments**. It is an obfuscated PEB-`ROR12`-dispatched 16-case XOR/ROL decryptor of the single global `qword_3910108`, **with an anti-hook return-address check** (`retaddr[-5]==0xE8 || retaddr[-7]==0x9A || retaddr[-2/-3/-6/-7]==0xFF`). Reached other than by a real `call`, it XORs a different constant and **silently returns a corrupted `cg`**. There is exactly one `cg`. **Any mod code obtaining `cg` from a detour or code cave via this function is getting garbage.** Use `cg = *(void**)0x14390B580 - 0x92C20` (`qword_390B580 = cg + 601120` @0xF5E0A = `&cg->refdef`).

### ⛔ C9. MWR has **no native demo player** — CONFIRMED, and the "removed commands" are a red herring
`record`/`demo`/`stoprecord` appear **only** in `sub_13AC80` = `CL_ShutdownOnceForAllClients`'s `Cmd_RemoveCommand` list — a legacy blind list. Zero registrations, zero data-table entries (contrast `disconnect` @0x12D8B78 = `{"disconnect", 0x54D130, 1}` with 11 xrefs). The LUI demo API is compiled-in **stubs returning hard-coded 0** — `isdemoplaying` (expr id 223) → block `0x58D974`: `xor edi,edi; mov [rsp+38h],edi; mov [rsp+30h],edi; jmp 59028Fh`. Control: CoD4 `iw3mp.exe` has the full marker set (`demos/%s.dm_%d` @0x6CF388, `CL_ReadDemoMessage:` @0x6CF411, `cl_freezeDemo` @0x6D03B8); h1 has **none**, while retaining 66,580 strings including CL diagnostics. **The `cl_demo_play` / native-UI-repoint line of work (commit 42b48a1) is dead. Close it permanently.**
Two live hazards: (a) `CL_ShutdownOnceForAllClients` will **silently remove a mod-registered `demo`/`record`/`stoprecord` command** on client shutdown; (b) LUI expr block `0x58D974` is **shared by ~29 unrelated ids** — never patch the block, retarget the jumptable entry at `0x58CAEF`.

### ⛔ C10. `cg.demoType` is permanently 0 — **[V]**
`cg+18640` = `a3` of `CG_DrawActiveFrame`; sole caller `0x34334D` does `xor r8d, r8d` @0x343340. Any plan to "tell cgame it's in a demo" via that field is a no-op. **You don't need it** — every path is snapshot-driven once `0x4000` is set.

### ⛔ C11. The engine never sends you your own `ET_PLAYER` **[R]**
`sub_561320` @0x561686: `if (v27 == v64) goto LABEL_49;` where `v64` = the receiving client's number. **Adding a self-entity to the snapshot is the wrong fix.** The local body comes from the ps + clientInfo + configstrings.

### ⛔ C12. CoD4 archive field order — the current decode is silently wrong **[R]**
`CL_WriteDemoClientArchive` (`cl_main_mp.cpp:1779`) writes fields **out of struct order**: `+0` index, `+4` origin, `+16` velocity, **`+28` movementDir**, **`+32` bobCycle**, **`+36` serverTime** (not "cmd_time"), `+40` viewangles. Any code assuming `+28=bobCycle` or `+36=cmd_time` is corrupting both today. Also: **CoD4 is Huffman end-to-end**, gamestate included (`MSG_WriteBitsCompress`, 4 raw bytes + compressed remainder). The "zlib for gamestate" note is wrong.

---

## 3. ROOT CAUSES

### Symptom 1 — Pinned view angles → **MECHANICALLY PROVEN. One bit: `ps+0x5C & 0x4000`.**

Three independent mechanisms, all gated by the same bit:

1. **Writer suppression** — `sub_4F2FE0` has a special case for `encoder == -87` (viewangles and nothing else). With `ctx[29]==0`, an unlinked (`ps+0x20 == 2047`), alive (`pm_type ∉ {1,6}`), non-following (`!(ps+0x48 & 4)`) player **`return 0` @0x4F309C — field declared unchanged, never written.** This is *by design*: MWR does not send you your own view angles, because a live client already knows them.
2. **Reader clobber A** — `sub_32B8E0` @0x32BA3D falls through to a **raw memcpy** of `nextSnap->ps` (no lerp).
3. **Reader clobber B** — `sub_32B120` @0x32B15B does **not** early-out ⇒ either `ps.viewangles := cg[1536148..]` (local mouse angles ≈ 0) or the Pmove loop runs `PM_UpdateViewAngles`. **Either way → `(0,0,0)`.**

Setting **`ps+0x5C |= 0x4000`** flips all three at once:
- `sub_4F2FE0`: the `else if (!v6)` suppression block is skipped → falls to the generic compare → **field emitted** (baseline is 0, our value differs) **[V]**
- `sub_32B8E0` → `sub_32BCC0(cn, 1)` → **viewangles LerpAngle'd from the snapshots** **[V]**
- `sub_32B120` → **`return 0` — no Pmove, no clobber** **[V]**

This is exactly why the real backlot demo works: its recorded ps carries `0x4000` set, because a demo viewer *is* a follower.

### Symptom 2 — No gun → **NOT conclusively proven by anyone. Three ranked candidates + a 3-read diagnostic.**

I will not pretend this one is closed. What *is* proven: it is **not** the weapon asset (`off_389EB40[80]` non-NULL or `sub_E9950` would have null-derefed), **not** the DObj (non-NULL), and **not** `sub_128360` (draws nothing). The real gate is `sub_119350` @0x119372 → `cg+601092` (`renderingThirdPerson`) ← `sub_1D5950`, which reads **`cg->snap->ps`** — i.e. *your transmitted bytes, verbatim*.

Ranked:
1. **`ps+0x3C` (viewmodel-hands index) arrives as 0, and/or configstring `1240+n` doesn't resolve.** `sub_128540` @0x1286E2: `v18 = *(u16*)(cg+60); if (v18) v46 = sub_694F30(sub_33B820(v18 + 1240)); else v46 = 0;`. Fallback = `cgs.clientinfo[clientNum]` (`unk_14D4BD4`, stride 1008, 4× u16 head) — **also empty** if the scaffold's `CS_PLAYERS` describe the original recorder's slot. Both empty ⇒ `cg+956988 == 0` ⇒ `sub_128360` @0x12844A **suppresses the error** — which is exactly the observed *"no gun AND no error"*.
2. **`*(BYTE*)(cg+2)` (`pm_type`) ≥ 7** ⇒ `sub_128360` @0x1283A6 early-returns into `sub_1C8740`, the entire normal viewmodel path never executes. Netfield idx 91, 4 bits, **baseline 0** **[V]** — so a template captured while dead/spectating reproduces both symptoms 2 and 3 exactly.
3. `renderingThirdPerson == 1` from `sub_1D5950`'s failure paths: `pm_type ≥ 7`, `snap->ps[92] & 2`, `snap->ps[48] & 4`, `(v4>>27)&1`, or `cg[979676] && cg[979696]`.

**Note the weapon id reads back correct (`cg+0x394` = `0x00040050`).** That is strong evidence the ps bitstream is **byte-coherent** — i.e. this is *not* a 1-bit envelope desync, and `snapshot-read`'s otherwise-excellent "missing `hasDeltaPS` bit" theory is **not** the live cause (it would have destroyed the weapon id too). It points squarely at the **template + scaffold-configstring mismatch**, which the recommended architecture deletes wholesale.

**Good news [V]:** setting `0x4000` does **not** break the gun. `sub_1D5950` with `0x4000` set, bits 27/28/29 clear, `cg[979676]==0`:
```c
if ((v4 & 0x4000) == 0 || cg[979676])  return (v4 >> 27) & 1;   // skipped
if (v4 & 0x20000000)                   return 0;                // skipped
if ((v4 & 0x10000000) == 0)            return cg[601088];       // <-- cg_thirdPerson dvar = 0 = FIRST PERSON
```
It routes `renderingThirdPerson` to the **dvar** instead of `ps` bit 27 — which is strictly better: it gives a free working `cg_thirdPerson 1` toggle during playback. `v-angles`' warning here is discharged.

### Symptom 3 — No 3rd-person body → **PROVEN, and it is NOT an entity bug.**
`sub_561320` @0x561686 proves the engine **never transmits your own `ET_PLAYER` to you [R]**. The body requires `sub_4E98E0(ps->clientNum) != NULL` — `word_AC55FC0[entnum]` → a DObj (`&unk_AAA3FC0 + 272*handle`) — required by `sub_11BAA0` and the body block @0xF632B–0xF63DB. That table is populated from **clientInfo → model → DObj**, i.e. from **`CS_PLAYERS` configstrings**, not from a snapshot entity. Plus `renderingThirdPerson` must be **1** to see it. **Adding a self-entity would be actively wrong.**

### Symptom 4 — Hitches (52 micro) + 3s skip → **PROVEN mechanism, needs data.**
Three independent, mutually reinforcing gates:
- `sub_32BCC0` skips **all** interpolation unless `nextSnap->serverTime > snap->serverTime` → ps snaps to `nextSnap` raw for that frame.
- `sub_32B8E0` path C is gated `(u32)(cg.time - cg[601076]) < 0x3E8` — exceed 1000 ms and `predictedPlayerState` goes **entirely stale**. That is the shape of the 3-second skip.
- `sub_32F750` (CG_ProcessSnapshots) spins until `cg.time` brackets `[snap, nextSnap)`.
- Plus: `CL_ParseSnapshot` @0x342A2B — **any** `msg->overflowed` (`*(DWORD*)msg`) silently sets `valid = 0` and drops the whole snapshot. Silent frame drops look like pacing bugs, not parse errors.

⇒ **strictly-increasing, no-duplicate `serverTime`, and instrument `msg->overflowed` per frame.** Also: **use the CoD4 archive's `+36` (`serverTime`) as the clock**, not the misidentified "cmd_time" (C12) — 52 micro-hitches smell exactly like a clock derived from the wrong field.

### Symptom 5 — EOF crash → **NOT PROVEN. Ranked candidates.**
1. **`snapFlags = 12` = `8|4`. Bit 4 is `SNAPFLAG_SERVERCOUNT`.** `sub_32F750` @0x32F862: `((next->snapFlags ^ snap->snapFlags) & 4)` → `sub_330200`+`sub_3303D0`+`sub_331BA0` = map-restart/teleport. A scaffold snapshot with a different bit-4 fires a **spurious restart**. **Use `snapFlags = 8`.** (Bit `0x2` = NOT_ACTIVE must stay clear — `sub_562F30` @0x5631C4 `or edx,2`; `sub_32F750` @0x32F7DA gates on it.)
2. Uninitialized `ctx` (`sub_51DB60` leaves `ctx[0]`, `ctx+0x10`, `ctx[31]` as **stack garbage**, and `ctx[0]` is *still read* @0x4F5B08 emitting a bit from it).
3. Running past the last snapshot with no clean teardown.

---

## 4. RECOMMENDED ARCHITECTURE

> ## **A — Client-side wire→wire transcode. Keyframe-only snapshots. Engine encoders. Mod owns the clock. No `.dm_h1` container.**

**Not B (loopback puppeteering).** `alternative-designs` argued for B, and its reasoning was sound *given what it knew*. Every premise has since failed:

| B's argument | Status |
|---|---|
| "A must hand-reimplement `sub_562F30` bit-for-bit forever" | The envelope is **6 calls** **[V]** (§1.3). Not a burden. |
| "A cloned the wrong ctx; `sub_562F30` sets `ctx[29]=0`, `ctx+0x10=client_t*`" | True — and **fatal to B, not A.** B must *fabricate a `client_t`* at `qword_B7F9B80 + 1011960*i` with valid `+196`/`+256`/`+1624..1636`, because `ctx[29]=0` makes `sub_4F52E0` deref it. **`ctx[29]=1` is the only serverless-safe mode.** |
| "Entities come free via `sub_561320`" | `sub_561320` needs `svs.snapshotEntities`, `svs.clients`, and skips your own entity anyway (C11). You must populate `g_entities[]` — the same work, plus a server. |
| "Pacing comes free — one clock" | The pacing gates are **all cgame-side** (`sub_32BCC0`/`sub_32B8E0`/`sub_32F750`) and are driven by `serverTime` *in the snapshot*. A owns that directly. |
| "Bring-up is B's main risk" | **`find_regex "testclient|AddTestClient"` → 0 hits.** Getting a client to `state==5` with no connection is **unproven**, and it is B's foundation. |

And the decisive one: **A's four alleged blockers are all now refuted [V]** — the ps table (C1), the archive ring (C3), the flag-4 desync (C6), and the coordinate bias (§1.5, solved by `0x2ED20A8`). A has no known blocker left. B has an unproven foundation and reintroduces `SV_SpawnServer` — the exact fragility your own restart plan identified.

**The shape (per `cod4-source-data`, and it is right):** the `.dm_1` **already contains everything** — block #1 is a client-synthesized gamestate with up to 2442 configstrings + 1024 entity baselines; every subsequent type-0 block is a real captured server packet carrying `ps` + **packet entities** + **packet clients**. Reading only the 52-byte type-1 archive discards ~95% of the source and *guarantees* no gun, no bodies, no world.

So: **decode CoD4's wire with CoD4's tables → named fields → re-encode with h1's tables.** Tables are fully in hand: IW3 `playerStateFields` `off_6BA060`/141, `entityStateFields` `0x6B6AA0`/59, `clientStateFields` `0x6B9EE0`/24 (all with **names**, `netField_t` = 16 bytes); h1 ps `0x12D7490`/252, ET_PLAYER `0x12D5210`/64. **Map by NAME, re-encode through h1's own writer. Never copy raw bits** — the codecs differ per-field (IW3 `-87` viewangles / `-88` origin / `-90/-91/-92` per-axis entity trBase vs h1 `-87`/`-88`/`0xFF96/97/98`).

**Delete the `.dm_h1` container.** There is no native player (C9) — the mod is already the demo player. A file container + zlib chunk writer + scaffold-chunk trick is pure indirection between our parser and `CL_ParseServerMessage`. Build the h1 snapshot message in memory and feed it in.

### What survives
- The `.dm_1` parser: framing (`[type:u8][payload]`; type 0 = `[seq:i32][len:i32][data]`; type 1 = 52 B; EOF = `[type][-1][-1]`), **Huffman** decode with the 4-byte raw prefix.
- The type-1 archive decode — **with C12's field order fixed** (`+28`=movementDir, `+32`=bobCycle, `+36`=serverTime, `+40`=viewangles).
- ps field mapping: `origin +0x78`, `velocity +0x84`, `bobCycle +0x74`, `weapon +0x394`, `viewangles +0x12C`.
- `ctx[29] = 1` — right answer, **wrong reason** (§C5). Keep it; document why.
- `from = NULL` — engine-blessed and, on this path, provably desync-free (§C6).
- Writing `f.va` (float degrees) into `ps[0x12C]` — **already correct**, just mislabelled. No `SHORT2ANGLE`. CoD4 archive `+40` maps 1:1.
- Using the engine's `msg_t` primitives (`sub_4EB1D0`, `4EBCB0/4EBCF0/4EBEB0/4EBD40/4EBFF0/4EBF30`).

### What dies
- **The scaffold-gamestate + memcpy-a-template + override-4-fields approach.** It is the root of symptoms 2 and 3 and it can never produce entities. Replace with a real transcoded gamestate + real transcoded snapshots.
- **The `.dm_h1` container and the zlib chunk writer** — entirely.
- **Writing only `WriteEntityIndex(2047,11)`** — and note the terminator is *stateful* on `msg+44`; `sub_4EC1E0(msg)` must precede each entity section.
- **Any hook on `sub_3409C0`** — never fires (§C3).
- **Any `cl_demo_play` / native-UI-repoint work** (commit 42b48a1) — §C9.
- **The freeze-clock guard** (your memory: "FIX BACKFIRED") — superseded; the mod owns a monotonic clock.
- The label `delta_angles` everywhere.
- Any code reading the netfield table via "`off_12D4BA8[6]`, count at `+14` dwords" as a raw address (§C1).
- Any code calling `sub_5CB80` from a detour/cave (§C8).

**⚠ Re-audit, do not assume dead:** the deployed **FOV fix** (`CG_CalcFov` = `sub_104330`). `view-chain` places `sub_EF460`→`sub_104330` on the `CG_CalcViewValues` path; `v-viewmodel` says `sub_EF460` is only reached via `sub_106290` (CG_Init tail), **not** from `CG_DrawActiveFrame`. These cannot both be true, and a shipped fix depends on it. One `xrefs_to 0xEF460` settles it.

---

## 5. IMPLEMENTATION PLAN

**Golden rule throughout: every step is validated against a REAL `.dm_h1` demo (backlot), which plays perfectly.** Hook the same function, dump the same field, diff ours vs theirs. Do not validate against theory.

### Step 0 — Instrumentation harness (do this first; it pays for itself immediately)
Hook `sub_4F52E0` (entry) and `sub_342770` (CL_ParseSnapshot). During **`demoplay backlot`**, dump per snapshot:
- `ctx[0]`, `ctx[29]`, `ctx[31]`, `ctx+4/8/12` (as **floats**), `ctx+0x10`
- `ps+0x5C` (expect **`& 0x4000` SET**), `ps+0x02` (pm_type), `ps+0x20`, `ps+0x3C`, `ps+0x12C..0x134`
- `snapFlags`, `deltaNum`, `serverTime`, `*(DWORD*)msg` (overflowed)
- `*(float*)0x2ED20A8/AC/B0` (the client coord bias)
- `cg+601092` (renderingThirdPerson), `cg+956988` (hand-model count), `cg+2`, `cg+60`

**This single session answers more open questions than any further static work** — it settles §6.1, §6.2, §6.4 and gives you the exact template you should be emitting. *(Caveat: a real `.dm_h1` plays through `CL_ParseServerMessage`, so `sub_4F52E0` may not fire on the read path — in that case hook `sub_4EE4E0`/`sub_342770` for the reader-side values and get the writer-side ctx from a listen-server match instead.)*

### Step 1 — `ps+0x5C |= 0x4000` (the one-bit fix)
In the synthesized ps, set `*(uint32_t*)(ps + 0x5C) |= 0x4000`, and ensure bits `0x2`, `0x8000000` (27), `0x10000000` (28), `0x20000000` (29) are **clear**; `ps+0x02 (pm_type) < 7`; `ps+0x48 & 4` clear.
**Verify:** `cg+300/304/308` tracks the recorded yaw; `cg+601092 == 0`; the gun appears; `cg_thirdPerson 1` now shows a body (if clientInfo is populated). Compare `ps+0x5C` against the real backlot demo's value from Step 0 — **it should match**.

### Step 2 — Correct ctx construction
```c
uint8_t ctx[128] = {0};                                  // >=120, ZEROED (sub_562F30: memset(base+48,0,120) @0x562FA0)
ctx[0]                    = demoClientNum;               // read at 0x4F5B08 - emits a bit; sub_51DB60 leaves it GARBAGE
*(float*)(ctx+4)          = *(float*)0x142ED20A8;        // CLIENT bias, NOT B7F9C08   <-- §1.5
*(float*)(ctx+8)          = *(float*)0x142ED20AC;
*(float*)(ctx+12)         = *(float*)0x142ED20B0;
*(void**)(ctx+0x10)       = NULL;                        // safe ONLY because ctx[29]=1
ctx[29]                   = 1;                           // -> WriteBit(0),WriteBit(1); skips both svs.clients derefs
ctx[31]                   = 0;                           // NOT interchangeable with [29] - see §C5
ctx[32]                   = 0;
```
`ctx[0]` must equal what the reader reconstructs from **`*(BYTE*)(qword_2EC8510 + 4)`** (`0x4EE52E`). Verify with CheatEngine — do not assume 0.
**Verify:** encoder emits `ps+0x12C` (breakpoint `sub_4F2FE0`, confirm the `-87` case reaches `LABEL_22`, not `return 0` @0x4F309C).

### Step 3 — Correct envelope (mirror `sub_562F30` exactly)
```c
sub_4EB1D0(&msg, buf, size);            // MSG_Init
sub_4EBD40(&msg, 6, 3);                 // svc_snapshot, 3 BITS      (NOT a byte)
sub_4EBFF0(&msg, serverTime);           // 4 RAW BYTES at byte cursor
sub_4EBF30(&msg, 0);                    // deltaNum = 0
sub_4EBF30(&msg, 8);                    // snapFlags = 8  (NOT 12 - bit4 = SNAPFLAG_SERVERCOUNT)
sub_4EBCB0(&msg);                       // hasDeltaPS = 0   <<-- THE BIT. from = NULL.
sub_4F52E0(ctx, &msg, serverTime, NULL, ps);
```
`serverTime` passed to `sub_4F52E0` **must be identical** to the one in the header — time-typed encoders (`-93/-94/-97/-98`, `0xFF9F/B6/B8/BA` via `sub_4F6340`) encode relative to it.
**⚠ `deltaNum = 0` costs an extra wire byte** — `sub_341960`'s call site @0x3429A1 re-reads a byte when `deltaByte == 0`. Emit it or the entity/client sections desync.
**Verify:** `*(DWORD*)msg == 0` (not overflowed); `cl.snap.valid == 1` @cl+18988; `ps+0x394` still reads back `0x00040050`.

### Step 4 — Real gamestate (kills symptoms 2 and 3)
Transcode CoD4 block #1 → h1 gamestate: **all** configstrings (esp. `CS_MODELS` base **1240** for `ps+0x3C`, and the `CS_PLAYERS` range for `cgs.clientinfo`), and the 1024 entity baselines. Make our `clientNum` **match** the source demo's recorder slot, or remap `CS_PLAYERS`.
**Verify:** `*(u16*)(cg+60) != 0`; `sub_33B820(cg[60]+1240)` resolves; `cg+956988 > 0`; `unk_14D4BD4 + 1008*clientNum` head u16s non-zero; **`*(float*)0x2ED20A8` is now populated** (CL_ParseGamestate ran).

### Step 5 — Entities (other players)
Per entity, in **ascending number order**, driving `sub_4F6C10`'s `msg+44` state machine (call `sub_4EC1E0(msg)` first):
```
WriteEntityIndex(num, 11); WriteBit(0)/*not removed*/; WriteBit(1)/*changed*/;
WriteBit(ctx[32]); WriteBits(lastChangedIdx, 7);
per changed field: sub_4F2A70(idx - prevIdx, base 3); <codec value>
... terminate: sub_4F6C10(msg, 2047, 11)
```
**ET_PLAYER new-entity baseline** (server `sub_4EC490` and client **inlines** the identical thing @0x4ECDE5 — `xrefs_to(0x4EC490)` shows only the server, do **not** conclude they diverge):
```c
memset(ent,0,256); *(i16*)(ent+0x02)=2047; *(i16*)(ent+0x00)=entnum;
*(u8*)(ent+0x0B)=7; *(i16*)(ent+0x04)=2046; *(u8*)(ent+0x0C)=entnum;
*(i32*)(ent+0x60)=3 /*pos.trType*/; *(i32*)(ent+0x84)=1 /*apos.trType*/;
```
**⚠ Two different bases in one call:** the *changed-check* runs against the patched baseline, but because `from->eType (0) != to->eType (1)`, `sub_4F3B40` forces the **value codec's** base to **ZERO** (`if (LOBYTE(a9)) v21 = from+off; else v21 = &zero;`, @0x4F6942 / @0x4ECEB6). Get this wrong and the whole bitstream desyncs.
Minimum visible/moving body: idx 0 `eType=1`; 40 `clientNum` (free if `== entityNum`); 32/39 trType (free if 3/1); **1/2/4 `pos.trBase` — encoders `0xFF96/FF97/FF98` are per-axis and NOT interchangeable, and are bias-dependent (§1.5)**; 7/5/31 apos.trBase; 51/58 trTime; 50/59 trDuration; 48/49/52 + 54/55/56 trDelta; 14 eFlags; 9–12 anims (`0xFFA2`, 8-byte, keyed `4*clientNum + ((off-44)>>3)`).
**Verify:** entity count in `cl+19024`; bodies render; **walk a fast-moving player past — that's what exercises the `|Δ|≥64` escape path and proves the bias.**

### Step 6 — Clock and EOF
Drive `serverTime` from the CoD4 archive `+36`. **Assert strictly increasing, no duplicates.** Assert `cg.time - cg[601076] < 1000` never trips. On EOF, tear down deliberately.
**Verify:** zero hitches; no `msg->overflowed`; clean exit.

---

## 6. OPEN QUESTIONS / RISKS

| # | Question | Why it matters | Cheapest proof |
|---|---|---|---|
| **6.1** | **`sub_4F2FE0` arg order: is `a4` = `to` or `from`?** `ps-netfields` + `v-angles` say `a4=TO` (reversed from `sub_4F52E0`'s own `a4=from`); `view-chain` says `a4=from`. | Changes which ps the `-87` gate reads. **Practically moot** — with `0x4000` on our ps and `0` in the baseline, the field is emitted under *either* reading. Matters only if we ever chain deltas. | Breakpoint `0x4F2FE0`, compare `rcx/r9/[rsp+0x20]` against the known `from`/`to` pointers. 2 min. |
| **6.2** | **Root cause of "no gun"** — 3 candidates (§3.2), none proven. | Only symptom without a proven mechanism. | **Step 0 harness.** Read `cg+2`, `cg+60`, `cg+601092`, `cg+956988` in a real demo vs ours. **One session decides it and could invalidate half of Step 4.** |
| **6.3** | Does **`0x2ED20A8` survive** into the state our writer runs in, and is it per-map? | If it's stale/zero, entity positions break on the escape path only — i.e. it passes testing and fails in the field. | CheatEngine-read `0x142ED20A8` during real backlot playback; compare to `0x14B7F9C08` in a listen match. Should be equal. |
| **6.4** | `v-ctx`'s `a6` term in the `0x4EE63E` chunk — real 6th param or mis-recovered register? | **Moot on the keyframe path** (§C6, proven **[V]**). Becomes live the instant `from != NULL`. | Only if delta-chaining is ever added. |
| **6.5** | **Is the deployed FOV fix on a live path?** `view-chain` vs `v-viewmodel` disagree on whether `sub_EF460`→`sub_104330` is reached from `CG_DrawActiveFrame`. | A shipped fix may be dead code or may be the thing keeping FOV sane. | `xrefs_to 0xEF460`. 30 s. |
| **6.6** | `sub_EA4B0(cn, cg+912912, cg)` @0x10C16D — **un-inspected angle modifier that runs live** in `CG_OffsetFirstPersonView`. | The only unaudited node on the angle path. | `decompile 0xEA4B0`. |
| **6.7** | `sub_8295A0`'s lost argument in `sub_32BCC0`'s LerpAngle (`+0.5` or not). | Decides shortest-path vs always-positive yaw wrap at the 0/360 seam. | `disasm 0x32BFDB`. 5 min. |
| **6.8** | **Bring-up: how do we reach `CA_ACTIVE` with the map loaded, serverless?** `sub_3432F0` gates `CG_DrawActiveFrame` on `dword_2EC82C8 != 10`. | **The one genuinely unsolved design question.** Architecture A says nothing about it. | Keep the loopback **for bring-up only** (map load + `CA_ACTIVE`), then stop letting it drive — the mod owns the snapshot stream. The bugs were never in the loopback; they were in encoder usage. Revisit serverless only after Steps 1–6 land. |
| **6.9** | `pm_type` asymmetry: the send gate tests `!= 6 && != 1`, the predict gate tests `== 1 \|\| == 8`. **6 vs 8.** | If `pm_type` is ever driven to 6 or 8, writer and reader disagree about who owns the angles. | Avoid 6/8; keep `pm_type = 0`. |
| **6.10** | `ps+0x5E0/5E4/5E8` (bias-dependent vec3) in the template. | If non-zero and the bias is wrong, they emit garbage on the escape path. | Zero them in the synthesized ps until §6.3 is closed. |

### Standing hazards
- **`CL_ShutdownOnceForAllClients` (`sub_13AC80`) silently removes** a mod-registered `demo`/`record`/`stoprecord` command on client shutdown (§C9). Re-register on re-init.
- **LUI expr block `0x58D974` is shared by ~29 ids** — never patch it; retarget the jumptable at `0x58CAEF` (§C9).
- **`sub_5CB80` anti-hook** returns a corrupted `cg` when not reached by a real `call` (§C8).
- **`sub_4F7480` trap:** `sub_4F63B0` rebases `ent+24/100/136/168` by a per-frame key before delta-encoding. Call `sub_4F6B30`, not `sub_4F63B0`, or supply consistent keys.
- **`msg->overflowed`** anywhere ⇒ silent whole-snapshot drop (`0x342A2B`). Instrument it permanently.

---

### The one-paragraph version

`ps+0x12C` is **viewangles**, it **is** on the wire (netfield idx 34, encoder `-87`, table `0x12D7490`/252 — the "not on the wire" report indexed a pointer variable as an array and analyzed an *entity* table), and writing it was always correct. The view is pinned because **`ps+0x5C & 0x4000` is clear**, which simultaneously makes `sub_4F2FE0` suppress the field on write (`return 0` @0x4F309C) *and* makes `sub_32B120` run Pmove and overwrite it on read (@0x32B15B) — a mechanism only one of thirteen agents found, because the other twelve looked at `sub_32B8E0` and never noticed `CG_DrawActiveFrame` calls a *second* predictor at `0xF6242`. Setting that one bit fixes the writer and both reader paths and routes `renderingThirdPerson` to the `cg_thirdPerson` dvar for free. The archive ring never fires (`ctx[29]=1` → `WriteBit(0)` @0x4F54F3 → `v155=0`), the flag-4 baseline desync is provably inert when `from==NULL` (`sub_4EC4C0` zeroes all nine), and entity coordinates are biased by a **client-side** vec3 at `0x2ED20A8` that `CL_ParseGamestate` computes for us — so hand-rolled entity encoding is viable and the loopback-puppeteering design is unnecessary. Missing gun and missing body are the **scaffold-gamestate + memcpy-a-template** design failing to deliver `ps+0x3C` and `CS_PLAYERS`, not viewmodel bugs — and the fix is to stop synthesizing state and start **transcoding CoD4's wire to h1's wire by field name**, which the `.dm_1` already contains in full.