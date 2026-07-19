# cod4play — Server-less CoD4 `.dm_1` → MWR Direct Loader: STATUS / RESUME (2026-07-18)

Goal (user): point at a CoD4 `.dm_1`, parse it, transcode CoD4→MWR, and **load it directly with
NO server, NO borrowed scaffold, NO loopback, NO 600M clock hack**. This documents exactly how far
the server-less loader got, the walls hit + fixed, the current blocker, and how to resume.

## Where it stands (paused by user: "stop, save state")

`cod4play` (new command, `src/client/component/demo/cod4_demo.cpp`) is a self-contained server-less
pump. Run at the MAIN MENU. It:
1. Parses `cod4native.0000.dm_h1` into gamestate + snapshot chunks (still uses the pre-built .dm_h1
   for now — Stage-2 was to transcode the cod4 gamestate in-memory; not reached).
2. `party::connect_serverless_demo(localhost, mp_backlot, war)` → **`CL_ConnectFromParty` (sub_12DFF0)**
   loads the map **CLIENT-SIDE, `SV_Loaded=0` (NO server)** — this WORKS (fastfiles load, `cl` allocated).
3. Phase machine (`sless_tick`, scheduler::main loop): waits for `cl`, ~4s settle, forces
   `connstate=CA_CONNECTED`, feeds the gamestate → **reaches `connstate=9 (CA_PRIMED)` + cgame init**.
4. Feeds per-frame snapshots (`sless_pump_next`, 20Hz) owning `cl->serverTime`.

**PROVEN:** server-less map load + gamestate feed reaches PRIMED + cgame init with no server. Big.

## Walls hit and fixed

1. **Xbox Live disconnect (`XBOXLIVE_NETCONNECTION`) — FIXED (mechanism found via IDA).**
   `CL_WritePacket` = **`sub_13D490`** transmits the client packet to the host each frame (netchan
   `sub_340460`); server-less there is no host, the send fails, and at `0x13d8d8` it calls
   `sub_12EC50(clientNum, "XBOXLIVE_NETCONNECTION", 0)` (disconnect) — gated by `if (!send && dword_2E6F4E0 <= 0)`.
   FIX applied: `demo_playback.cpp` `write_packet` hook now also swallows `CL_WritePacket` while
   `cod4_demo::cod4play_active()` → no send → no disconnect. (Also added a belt-and-suspenders swallow
   of the `XBOXLIVE_NETCONNECTION` Com_Error in `logger.cpp` `com_error_stub`.)
   - **SIDE EFFECT (the new problem):** blocking `CL_WritePacket` also blocks `CL_ConnectFromParty`'s
     own connect/challenge handshake → its Live/party layer never stabilises and **reconnect-loops**
     (`connstate` cycles `0→4→7→5` = DISCONNECTED→CHALLENGING→SYNC→CONNECTED). The party-connect path
     WANTS a handshake we can't give server-less; swallowing the send trades the disconnect for instability.

## CURRENT BLOCKER — fed snapshots never validate

Every fed snapshot reads `snap.valid=0, newSnaps=0` (per-frame diagnostic confirms). Then
**`CL_SetCGameTime` (sub_33C950)** errors `!cl->snap.valid` and the server shuts down → drop to menu.
The **loopback path parses these EXACT bytes fine (renders)**, so it is a *context* difference, not the
snapshot encoding (cod4_build writes valid keyframes: `deltaNum=0`, `snapFlags=12`/bit3, delta-present bit=0).

Decompiled chain (H1-Mod IDB, PC RVAs, module base 0x140000000):
- `CL_SetCGameTime` (`sub_33C950`): at `connstate==9 && cl->newSnapshots`, clears newSnapshots, calls
  `CL_FirstSnapshot`, then `if (connstate==10 && !cl->snap.valid) Com_Error`.
- `CL_FirstSnapshot` (`sub_33B6B0`): sets `connstate=10 (ACTIVE)` **unconditionally** (0x33b71d) — does
  NOT check snap.valid. So a set `newSnapshots` with an invalid snap = guaranteed error.
- `CL_ParseSnapshot` (`sub_342770`): keyframe path (deltaNum=0 → newSnap.deltaNum=-1; snapFlags bit3 set
  → `v15=0` null baseline) → `LABEL_13`: `MSG_ReadBit` (**`sub_73F90`**) reads the delta-present bit; if
  it returns >0 with `v15==0` → `valid=0, abort`. cod4_build writes that bit = 0, so it *should* pass.
  Commit (end) sets `newSnapshots=1` AND copies `valid=1` to `cl.snap` together — so a committed snap
  can't have newSnaps without valid. Since runtime shows valid=0/newSnaps=0 for ALL snaps, **every
  snapshot is ABORTING inside CL_ParseSnapshot** (LABEL_16 or the `*a2` msg-overflow tail).

**Leading hypothesis:** `CL_ConnectFromParty` does NOT initialise the client's snapshot system
(ring / entity baselines / areamask) the way `SV_SpawnServer` (loopback) or a real server connection
does, so the demo's snapshots fail to parse/validate. The loopback works because SV establishes that state.

## Key addresses & offsets (H1-Mod PC IDB, base 0x140000000)
- `Com_Error` 0x159860 (hooked by logger). `CL_WritePacket` `sub_13D490` (disconnect call @0x13d8d8 = `sub_12EC50`).
- `CL_SetCGameTime` 0x33C950 (hooked by demo_playback `handle_demo_reading`). `CL_FirstSnapshot` `sub_33B6B0`.
- `CL_ParseSnapshot` `sub_342770`. `MSG_ReadBit` `sub_73F90`. `CL_ParseServerMessage` 0x3425B0.
- `CL_ConnectFromParty` `sub_12DFF0` → **cl/clc alloc `sub_12BCD0`**, **client map load `sub_138ED0`** (+`sub_138E40`).
  These two are the pieces to call DIRECTLY for a Live-free bootstrap (skip the connstate=3 + address setup that triggers the handshake).
- `Cbuf_AddText(localClient,controller,text)` 0x1CF480. `"XBOXLIVE_NETCONNECTION"` string @ 0x8dfb00 (data; localized, key-hash-referenced).
- connstate = int @ `base+0x2EC82C8` (0 DISCONN,3 CONNECTING,4 CHALLENGING,5 CONNECTED,6 SENDINGSTATS,7 SYNC,8 LOADING,9 PRIMED,10 ACTIVE).
- `cl` = `*(base+0x2EC84F0)`; `clc` = `*(base+0x2EC8510)`. cl.snap.valid @ cl+18988; cl.newSnapshots @ cl+19100; cl.serverTime @ cl+19076; snap.serverTime @ cl+19012.

## NEXT STEPS if resuming server-less
1. **Stabilise the connect without blocking the handshake:** instead of swallowing `CL_WritePacket`,
   set `dword_2E6F4E0 > 0` during cod4play (that gate alone suppresses the send-fail disconnect) so the
   handshake can run and connstate stops cycling. (Verify what `dword_2E6F4E0` is first.)
2. **Fix snapshot validity:** RE what `SV_SpawnServer`/the real connect sets up for the CLIENT snapshot
   system that `CL_ConnectFromParty` doesn't (snapshot ring, entity baselines, areamask). Consider calling
   `sub_138ED0`/`sub_12BCD0` directly (bypass the party/Live handshake entirely). Get a CLEAN per-frame
   trace first: make `cod4play` STOP the pump on server-shutdown so the aftermath isn't reconnect noise.
3. Then Stage 2 (transcode the cod4 gamestate in-memory, drop the borrowed .dm_h1) + Stage 3 (own clock, remove 600M).

## FALLBACK (pragmatic, was Option B — proven to RENDER)
Offline loopback for map-load + client-init ONLY (SV establishes valid snapshots), suppress its own
snapshots (existing `capture_data` dropper), mod owns the clock (no 600M), load the `.dm_1` directly with
a transcoded gamestate (no borrowing). A server runs under the hood but delivers a rendering demo + every
concrete user ask. This sidesteps the entire online-wall/client-init problem.

## Build / deploy notes
- VS 2026 MSBuild (`...\18\Insiders\...`), `-t:client -p:Configuration=Release -p:Platform=x64 -m:2`
  (**`-m:2` avoids the PCH `C1076`/`C3859` out-of-memory** on this box). Kill `mspdbsrv.exe` + `h1-mod.exe`
  before linking or **LNK1201** (PDB lock). Deploy = copy `build/bin/x64/Release/h1-mod.exe` → game dir.
- Current deployed build = the diagnostic one (cod4play + CL_WritePacket swallow + Com_Error swallow +
  per-snapshot/per-frame STATE logging, all gated on `cod4play_active()`; harmless outside cod4play).
- CE live reads only work while the game is RUNNING; the module base is 0x140000000 (no ASLR relocation).
