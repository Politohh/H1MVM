# MWR/H1 Demo System — Function Mapping Report

Generated from IDA MCP analysis of:
- **MWR PS4 ELF** (`default_mp.elf`, imagebase 0x0) — port 13338, fully named via leaked PDB
- **H1 PC exe** (`h1_mp64_ship_dump.exe`, imagebase 0x0) — port 13341, unanalysed dump

IW6 reference: <https://git.alterware.dev/anomaly/iw6-mod>

Confidence key:
| Level | Meaning |
|-------|---------|
| **Confirmed** | Address found in h1-mod `symbols.hpp` OR verified in PC exe IDB |
| **PS4 ELF** | Found by name in PS4 ELF; PC exe address needs IDA xref walk |
| **Weak candidate** | Nearest match by xref / string heuristic; not verified |
| **Not found** | Not located in either IDB — may be inlined or renamed |

---

## Client Playback Hooks

| IW6 Function | IW6 addr (ref) | PS4 ELF addr | H1 PC exe addr | Confidence | Notes |
|---|---|---|---|---|---|
| `CL_SetCGameTime` | IW6: `0x1402B9D80` | `0x233b80` | `0x33C950` (MP) | **Confirmed** | Structural match to IW6 (connstate checks, I_stricmp("localhost"), serverTime/cgameTime updates); hook LIVE |
| `CL_WritePacket` | — | `0x2386b0` | `0x13D490` (MP) | **Confirmed** | From h1-mod party.cpp `invoke<void>(0x13D490_b, 0)` comment; hook LIVE |
| `CL_GetPredictedPlayerInformationForServerTime` | — | `0x249800` | `0x3409C0` (MP) | **Confirmed** | Structural match (snapshot ring buffer, % 256 loop, field copies) |
| `CL_GetPredictedVehicleForServerTime` | — | `0x2498b0` | N/A | **Not in H1** | MWR has no vehicles; function absent or inlined as no-op |
| `VehicleCam_UpdatePlayerControlCam` | — | `0x5d6bc0` (stub→0x5d5d60=`VehicleCam_UpdateDriveCam`) | TODO | **PS4 ELF** | Suppress vehicle cam updates; PS4 stub tail-calls DriveCam |
| `GetRemoteEyeValues` | XB360: `0x82245c90` | Not found in PS4 | TODO | **Not found** | XB360 has it; search H1 via `remoteEyesEnt` pattern |
| `LUI_IntermissionBegan` | — | `0xfd180` | `0x26B550` (MP) | **Confirmed** | Via string "intermission_began" xref |
| `CG_UpdateOmnvars` | XB360: `0x82234078` | Not found in PS4 | `0x331E90` (MP) | **Confirmed** | Via callee chain: calls CG_GetLocalClientGlobals + LUI_NotifyOmnvarChanged |
| `CG_VisionSetStartLerp_To` | — | `0x213690` | TODO | **PS4 ELF** | Called from CG_UpdateCinematicCamera (H1: sub_13AC80); exact callee TBD |
| `Omnvar_GetDef` | — | `0x58a690` | `0x5A4460` (MP) | **Confirmed** | Tiny fn: `return &array + 8 * index`; structural match |
| `CG_DrawActiveFrame` | — | — | `0xF5D40` (MP) | **Confirmed** | In h1-mod `symbols.hpp`; used for timescale hook |
| `CL_IsCgameInitialized` | — | — | `0x33C640` (MP) | **Confirmed** | In h1-mod `symbols.hpp` |
| `CL_FirstSnapshot` | IW6: sub near `0x1402B8D00` | — | `0x33B6B0` (MP) | **Confirmed** | Called at CA_PRIMED→CA_ACTIVE in CL_SetCGameTime decompile |
| `CL_AddReliableCommand` | — | — | `0x12B810` (MP) | **Confirmed** | From h1-mod party.cpp invoke comment |
| `CL_Disconnect` | — | — | `0x12F080` (MP) | **Confirmed** | From h1-mod party.cpp invoke comment |
| `MSG_ReadDeltaPlayerstate` | — | `0x4895c0` | `0x4EE63E` (MP) | **Confirmed** | Calls Omnvar_PerPlayerstateCount + CL_GetPredictedPlayerInfo |
| `LUI_NotifyOmnvarChanged` | — | — | `0x26E430` (MP) | **Confirmed** | Via strings "omnvar_update"/"omnvar" xref |

---

## Gamestate / Config String Path

| IW6 Function / patch site | IW6 addr | PS4 ELF addr | H1 PC exe addr | Confidence | Notes |
|---|---|---|---|---|---|
| Gamestate update call patch | `0x1402CCD56` | — | TODO | **Not found** | IW6 `demo_playback.cpp` line ~420; need xref walk in H1 |
| NOP patch site 1 | `0x14029981C` (nop 6) | — | TODO | **Not found** | IW6 suppresses something in connstate path |
| NOP/patch site 2 | `0x14028844B` | — | TODO | **Not found** | |
| `CL_GetConfigString` | — | — | `0x33B820` (MP) | **Confirmed** | In `symbols.hpp` |
| `SV_GetConfigstring` | — | — | `0x552E90` | **Confirmed** | In `symbols.hpp` |
| `SV_SetConfigstring` | — | — | `0x553E60` | **Confirmed** | In `symbols.hpp` |

---

## Server-Side Recording

| IW6 Function | XB360 IW6 addr | PS4 ELF addr | H1 PC exe addr | Confidence | Notes |
|---|---|---|---|---|---|
| `SV_WriteSnapshotToClient` | `0x824497D0` | `0x50c7e0` | TODO | **Not found** | No unique strings; need callee-chain trace from SV_SendSnapshots |
| `SV_BuildClientSnapshot` | `0x82445938` | `0x508ce0` | `0x460510` | **Weak candidate** | Sole caller of SV_GetCachedSnapshot wrapper; H1 size 0x492 vs XB360 0x1570 — PopulatePlayerstate/Omnvars likely split out |
| `SV_GetCachedSnapshot` | `0x824472E8` | `0x509e70` | `0x55F040` | **High confidence** | Sole caller of SV_GetCachedSnapshotInternal |
| `SV_GetCachedSnapshotInternal` | `0x82446EA8` | — | `0x55E1F0` | **Confirmed** | Unique string "** Too many entities %d > %d to cache snapshot data. ***\n" present in both XB360 and H1 |
| `SV_SendSnapshots` | `0x8244A528` | `0x50f6b0` | TODO | **Not found** | Calls SV_WriteSnapshotToClient; PS4 also calls SV_BeginClientSnapshot, SV_EndClientSnapshot, SV_SendMessageToClient |
| `SV_SendClientSnapshot` | Not found | Not found | TODO | **Not found** | May not exist by this name in MWR |
| `SV_SendServerCommand` | — | — | `0x1CC040` | **Confirmed** | In `symbols.hpp` |

---

## Message / Network

XB360 IW6 MSG reference addresses (imagebase 0x82000000) confirmed from named binary:

| IW6 Function | XB360 IW6 addr | PS4 ELF addr | H1 PC exe addr | Confidence | Notes |
|---|---|---|---|---|---|
| `MSG_Init` | `0x823E0548` | `0x4857a0` | TODO | **Not found** | Cluster near MSG_ReadStringLine (0x4EB9D0) |
| `MSG_BeginReading` | `0x823E05E8` | `0x485830` | TODO | **Not found** | |
| `MSG_WriteByte` | `0x823E09A0` | `0x485c00` | TODO | **Not found** | |
| `MSG_WriteShort` | `0x823E1168` | — | TODO | **Not found** | |
| `MSG_WriteLong` | `0x823E1198` | `0x485ae0` | TODO | **Not found** | |
| `MSG_WriteData` | `0x823E09D8` | `0x485c20` | TODO | **Not found** | |
| `MSG_WriteBigString` | `0x823E0AA0` | — | TODO | **Not found** | |
| `MSG_ReadByte` | `0x823E0C58` | `0x486130` | TODO | **Not found** | |
| `MSG_ReadShort` | `0x823E1458` | `0x486170` | TODO | **Not found** | |
| `MSG_ReadLong` | `0x823E14F8` | `0x486060` | TODO | **Not found** | |
| `MSG_ReadData` | `0x823E1090` | `0x486740` | TODO | **Not found** | |
| `MSG_ReadBigString` | `0x823E0F08` | `0x4864c0` | TODO | **Not found** | |
| `MSG_ReadStringLine` | `0x823E0FC8` | — | `0x4EB9D0` | **Confirmed** | In `symbols.hpp` |
| `MSG_ReadDeltaPlayerstate` | `0x823E4C28` | `0x4895c0` | `0x4EE63E` | **Confirmed** | Calls Omnvar count fns + CL_GetPredictedPlayer |
| `MSG_WriteDeltaPlayerstate` | `0x823EBD10` | `0x492cc0` | TODO | **Not found** | Called from SV_WriteSnapshotToClient |
| `MSG_WriteDeltaOmnvars` | `0x823EC7A8` | `0x495be0` | TODO | **Not found** | |
| `MSG_WriteDeltaScoreboard` | `0x823E9400` | `0x4955b0` | TODO | **Not found** | |
| `MSG_WriteEntityIndex` | `0x823E8978` | `0x491630` | TODO | **Not found** | |
| `MSG_ClearLastReferencedEntity` | `0x823E2878` | `0x488810` | TODO | **Not found** | |
| `Netchan_Transmit` | — | — | `0x4F24A0` | **Confirmed** | Via string "Netchan_Transmit: length = %i" xref |

---

## Client State Globals

| IW6 Symbol | H1 equivalent | H1 PC exe addr | Confidence | Notes |
|---|---|---|---|---|
| `game::mp::connstate` | `game::connectionState` | `0x2EC82C8` (MP) | **Confirmed** | In `symbols.hpp`; `connstate_t` enum in `structs.hpp` |
| `game::mp::cl` | `game::mp::client_state` (likely) | `0x2EC84F0` (MP) | **Weak candidate** | Pointer at this addr; struct type unknown |
| `game::mp::clc` | `game::mp::connect_state` (likely) | `0x2EC8510` (MP) | **Weak candidate** | Pointer at this addr; struct type unknown |
| `game::mp::cls` | Unknown | TODO | **Not found** | |
| `game::mp::cg` | via `CG_GetLocalClientGlobals()` | `0x5CB80` (MP) | **Confirmed** | Getter in `symbols.hpp`; returns `mp::cg_s*` |
| `game::mp::cgs` | Unknown | TODO | **Not found** | |

---

## Selectors / Utility

| IW6 Function | PS4 ELF addr | H1 PC exe addr | Confidence | Notes |
|---|---|---|---|---|
| `CG_SelectWeapon` | `0x220150` | TODO | **PS4 ELF** | Used in `handle_demo_patches` |
| `Dvar_FindVar` | — | `0x183EB0` (MP) | **Confirmed** | In `symbols.hpp` |
| `Dvar_RegisterEnum` | — | `0x182700` (MP) | **Confirmed** | In `symbols.hpp` |
| `Dvar_RegisterFloat` | — | `0x1827F0` (MP) | **Confirmed** | In `symbols.hpp` |
| `Dvar_RegisterBool` | — | `0x182340` (MP) | **Confirmed** | In `symbols.hpp` |
| `LUI_OpenMenu` | — | `0x1E1210` (MP) | **Confirmed** | In `symbols.hpp` |
| `Scr_NotifyNum` | — | `0x4b1100` (PS4 ref) | **PS4 ELF** | |

---

## Next Steps

1. **Walk CL_SetCGameTime in PC exe**: use `xrefs_to(CL_IsCgameInitialized=0x33C640)` — the caller
   that also checks `connectionState` and dispatches to cgame is likely `CL_SetCGameTime`.
2. **Find MSG_* in PC exe**: use `search_text` for string "MSG_Init" or byte-pattern matching
   against the PS4 ELF's function prologues.
3. **Identify client state struct types**: decompile `0x2EC84F0` dereferences to infer layout.
4. **Locate gamestate patch site**: cross-reference from `CL_GetConfigString` callgraph in PC exe.
5. **Implement CL_SetCGameTime playback tick**: once address found, enable the hook in
   `demo_playback.cpp`'s `post_unpack()`.
6. **Implement `parse_demo()`**: deserialize the chunk-based file format in `demo_utils.cpp`.
