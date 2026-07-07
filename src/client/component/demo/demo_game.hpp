// =============================================================================
//  component/demo/demo_game.hpp
//
//  h1 (MWR / h1_mp64_ship) game-layer bridge for the iw6-mod demo_system port.
//
//  Adds ONLY what the demo needs on top of h1-mod's native game namespace
//  (src/client/game/game.hpp already provides msg_t, dvar_t, errorParm,
//  environment, symbol<T>, and ~22 dvar/draw/util symbols the demo reuses).
//
//  Offset provenance: DEMO_SYSTEM_PORT.md §B (all HIGH-confidence hunts).
//  h1 imagebase = 0x0 -> symbol<T>{sp, mp} : sp=0 (MWR is MP-only), mp = h1 RVA.
//
//  Struct fidelity:
//   * VERIFIED (h1 static RE): clientActive_t snap.valid/snap.serverTime/
//     newSnapshots/clientArchive(stride 36)/clientArchiveIndex; msg_t reused
//     from h1-mod; ClientArchiveData = 36B (no vehicle, MWR reality).
//   * PORTED-FROM-iw6 + TODO(h1): cg_t vision/refdef/weaponSelect fields, clc,
//     cls, cgs, serverStatic tail fields. Marked below; refine as needed.
// =============================================================================
#pragma once

#include <span>
#include <vector>
#include <string>
#include "utils/compression.hpp" // h1-mod string-based zlib compress/decompress
#include "utils/string.hpp"      // h1-mod utils::string::strip

// iw6-signature strip overload the demo uses: strip(view, bool) -> string.
namespace utils::string
{
	inline std::string strip(std::string_view string, bool /*strip_color*/)
	{
		return strip(std::string(string));
	}
}

// iw6-signature zlib overloads the demo relies on: (span<const u8>, size_hint) ->
// vector<u8>, implemented over h1-mod's string-based zlib. Added to the open
// namespace so the demo's utils::compression::zlib::compress/decompress calls resolve.
namespace utils::compression::zlib
{
	inline std::vector<std::uint8_t> compress(std::span<const std::uint8_t> input, std::size_t /*exp_output_size*/)
	{
		const auto out = compress(std::string(reinterpret_cast<const char*>(input.data()), input.size()));
		return std::vector<std::uint8_t>(out.begin(), out.end());
	}
	inline std::vector<std::uint8_t> decompress(std::span<const std::uint8_t> input, std::size_t /*exp_output_size*/)
	{
		const auto out = decompress(std::string(reinterpret_cast<const char*>(input.data()), input.size()));
		return std::vector<std::uint8_t>(out.begin(), out.end());
	}
}

#include "game/game.hpp" // h1-mod native game namespace (msg_t, dvar_t, symbol<T>, environment, Dvar_*, R_*, Sys_Milliseconds, ScrPlace_*, UI_Get*Display*, SV_Loaded, CG_GetLocalClientGlobals, connectionState...)
#include "game/dvars.hpp" // dvars::insert_dvar_info -- see demo_compat below

#ifndef WEAK
#define WEAK __declspec(selectany)
#endif

namespace game
{
	// -------------------------------------------------------------------------
	// Leaf types the demo passes around (font handle is opaque -> reuse Font_s).
	// -------------------------------------------------------------------------
	using Font_t = Font_s;
	using vec3_t = float[3];
	struct PlayerVehicleState; // fwd (full def in STRUCTS section)

	// =========================================================================
	//  FUNCTION SYMBOLS  (found this session; NOT already in h1-mod symbols.hpp)
	// =========================================================================
	// ---- Client message / packet -------------------------------------------
	WEAK symbol<void(int localClientNum, msg_t* msg)> CL_ParseServerMessage{0x0, 0x3425B0};       // HIGH
	WEAK symbol<void(int localClientNum)> CL_WritePacket{0x0, 0x13D490};                            // HIGH (h1-mod party.cpp)
	WEAK symbol<void(int localClientNum)> CL_SetCGameTime{0x0, 0x33C950};                           // HIGH (self-string)
	WEAK symbol<int(msg_t* msg, int bits)> MSG_ReadBits{0x0, 0x4EB320};                             // HIGH
	WEAK symbol<void(msg_t* msg, int value, int bits)> MSG_WriteBits{0x0, 0x4EBD40};                // HIGH
	// real standalone MSG_ReadLong (does its own bounds-check + -1 overflow return; NOT identical
	// to MSG_ReadBits(msg,32) — verified via CL_ParseGamestate call site 0x3427DD).
	WEAK symbol<int(msg_t* msg)> MSG_ReadLong{0x0, 0x4EB7D0};                                       // HIGH

	// ---- Predicted data (record capture) -----------------------------------
	namespace mp { struct clientActive_t; struct playerState_t; }
	WEAK symbol<bool(const mp::clientActive_t* cl, int serverTime, mp::playerState_t* to)>
		CL_GetPredictedPlayerInformationForServerTime{0x0, 0x3409C0};                               // HIGH (exact ring-loop twin)
	WEAK symbol<void(mp::clientActive_t* cl, int serverTime)>
		CL_SavePredictedPlayerInformationForServerTime{0x0, 0x342D80};                              // HIGH (record writer)
	// MWR has no vehicle prediction (ClientArchiveData is 36B, no PlayerVehicleState).
	// Stub returns false so the (dead-on-MWR) vehicle playback path is inert.
	inline bool CL_GetPredictedVehicleForServerTime(const mp::clientActive_t*, int, PlayerVehicleState*) { return false; }

	// ---- CG / server commands / omnvars ------------------------------------
	// NOTE(h1): 0x11A410 (sub_11A410) is NOT this function -- it is the PER-COMMAND inner
	// executor (takes a single absolute command index, unconditionally sets
	// clc->lastExecutedServerCommand = that index, no bounds-safe looping). The real
	// "CG_ExecuteNewServerCommands" wrapper is 0xFC3D0 (sub_FC3D0): it loops from ITS OWN
	// persistent counter at cgs+0x18 (cgs->serverCommandSequence) up to the target, calling
	// sub_11A410 once per index -- this is what keeps cgs's counter and clc's counter in
	// sync and never skips an intermediate command. Confirmed via decompile: 0x11A410's only
	// real caller is 0xFC3D0; 0xFC3D0's callers are the per-snapshot cgame handlers (e.g.
	// sub_330200, called once per applied snapshot with target = snapshot->serverCommandSequence).
	// Calling 0x11A410 directly (as this code did previously) advances clc->lastExecutedServerCommand
	// WITHOUT advancing cgs->serverCommandSequence, silently skipping every intermediate command
	// and, once native per-snapshot processing (sub_330200->sub_FC3D0) eventually runs its own
	// catch-up loop from the now-stale cgs counter, that loop requests long-cycled-out indices
	// -> "Lost reliable commands" (the a2 <= serverCommandSequence-256 check in sub_11A410).
	// Bound to the real wrapper here so process_server_commands() executes every intermediate
	// command in order and never desyncs the two counters.
	WEAK symbol<void(int localClientNum, int latestSequence)> CG_ExecuteNewServerCommands{0x0, 0xFC3D0}; // HIGH (only caller of the per-command executor 0x11A410; matches native per-snapshot usage)
	namespace mp { struct snapshot_t; }
	WEAK symbol<void(int localClientNum, const mp::snapshot_t* oldSnap, const mp::snapshot_t* newSnap)>
		CG_UpdateOmnvars{0x0, 0x331E90};                                                            // HIGH
	WEAK symbol<void*(int index)> Omnvar_GetDef{0x0, 0x5A4460};                                     // HIGH (&table[8*n])

	// ---- UI / misc ----------------------------------------------------------
	WEAK symbol<bool(int localClientNum, void* luaVM)> LUI_IntermissionBegan{0x0, 0x26B550};        // HIGH
	WEAK symbol<bool(int localClientNum, int clientNum, char* buffer, int length)>
		CL_GetClientNameColorize{0x0, 0x343EA0};                                                    // HIGH
	WEAK symbol<int(int msec)> Com_TimeScaleMsec{0x0, 0x17EAC0};                                    // HIGH (entry; note h1-mod already hooks timescale here)

	// =========================================================================
	//  NAME ADAPTERS  (h1-mod exposes these under different names/arities)
	// =========================================================================
	inline const char* UI_LocalizeMapname(const char* mapName)   { return UI_GetMapDisplayName(mapName); }
	inline const char* UI_LocalizeGametype(const char* gametype) { return UI_GetGameTypeDisplayName(gametype); }
	inline void Dvar_SetString(const dvar_t* dvar, const char* string)
	{
		Dvar_SetFromStringFromSource(const_cast<dvar_t*>(dvar), string, DvarSetSource::DVAR_SOURCE_INTERNAL);
	}
	// Version label for demo headers (iw6 used LiveStorage_FetchFFotD); constant is safe.
	inline const char* LiveStorage_FetchFFotD() { return "1.0"; }

	// Dvar_Register* adapters: h1-mod's symbols take a leading int hash and no desc;
	// the demo uses the iw6 (name,...,desc) signature. Bridge via generateHashValue.
	//
	// NOTE(h1): also register into dvars::dvar_map (h1-mod's own hash->name/description
	// side table, game/dvars.cpp) the same way dvars::register_bool/float/enum do. MWR's
	// real Dvar_Register* calls don't expose a queryable name back to the engine's own
	// console -- h1-mod's in-game console autocomplete (game_console.cpp find_matches)
	// and the `dvar <name>` info command both resolve names via this side table, NOT the
	// engine directly. Without this, a dvar registered via the bare Dvar_RegisterBool call
	// still WORKS (Dvar_FindVar("name") hashes with the same generateHashValue and finds
	// it, so `set demoautorecord 1` typed blind does work), but it never appears while
	// typing/autocompleting in the console -- easy to conclude it "doesn't exist".
	namespace demo_compat
	{
		inline dvar_t* register_bool(const char* name, bool value, unsigned int flags, const char* desc)
		{
			const auto hash = generateHashValue(name);
			dvars::insert_dvar_info(hash, name, desc);
			return Dvar_RegisterBool(hash, name, value, flags);
		}
		inline dvar_t* register_float(const char* name, float value, float mn, float mx, unsigned int flags, const char* desc)
		{
			const auto hash = generateHashValue(name);
			dvars::insert_dvar_info(hash, name, desc);
			return Dvar_RegisterFloat(hash, name, value, mn, mx, flags);
		}
		inline dvar_t* register_enum(const char* name, const char** valueList, int defaultIndex, unsigned int flags, const char* desc)
		{
			const auto hash = generateHashValue(name);
			dvars::insert_dvar_info(hash, name, desc);
			return Dvar_RegisterEnum(hash, name, valueList, defaultIndex, flags);
		}
	}

	// Ghosts-only helpers with no MWR twin (MWR has no drivable vehicles/turrets).
	// Safe stubs so the demo compiles; the vehicle path is dead weight on MWR.
	inline void* CG_GetVehicleDef(const mp::playerState_t* /*ps*/) { return nullptr; }
	inline void CG_HandleTurretFire(int, const void*, unsigned int, unsigned int, bool) {}
	inline bool GetRemoteEyeValues(int /*localClientNum*/) { return false; }
	inline void VehicleCam_UpdatePlayerControlCam(int, void*, vec3_t*, vec3_t*) {}

	// CG_SelectWeapon: MWR — no real fn resolved (low value). Write cg->weaponSelect
	// directly so the playback weapon-mismatch clears (see mp::cg accessor below).
	// (defined after cg_t, at bottom.)

	// enum bridges — DVAR_FLAG_NONE, connstate_t (CA_*), TC_NONE, ScreenPlacement,
	// and the mp:: server structs (client_t/gentity_s/...) all come from h1-mod's
	// game namespace (included above). Only the demo-specific ones are declared here.
	enum { CS_VISIONSET_COUNT = 6 };

	// =========================================================================
	//  STRUCTS  (demo-specific; added to game::mp — distinct names from h1-mod)
	// =========================================================================
	// PlayerVehicleState kept for signature/type compatibility; MWR never fills it.
	struct PlayerVehicleState
	{
		int entity;            // +0x00 (compared to 2047)
		int flags;
		vec3_t origin;
		vec3_t angles;
		vec3_t velocity;
		vec3_t angVelocity;
		unsigned int splineId;
		int targetEntity;
		vec3_t gunAngles;
	};

	// MWR ClientArchiveData = 36 bytes / 9 dwords (VERIFIED via CL_SavePredicted 0x342D80):
	// [0]serverTime [1..3]origin [4..6]velocity [7]bobCycle [8]movementDir. NO vehicle.
	struct ClientArchiveData
	{
		int serverTime;        // +0x00
		vec3_t origin;         // +0x04
		vec3_t velocity;       // +0x10
		int bobCycle;          // +0x1C
		int movementDir;       // +0x20
	};                         // = 36 bytes (matches MWR clientArchive stride)

	// REAL MWR config-string store (RE-VERIFIED 2026-07-07 via fresh decompile of
	// CL_GetConfigString 0x33B820 + CL_ParseConfigStrings_Internal 0x340D80 +
	// sub_342F90, the per-string writer). stringOffsets and stringData ARE
	// contiguous: stringOffsets @ 0x2F52FA8 (+0x0000), stringData @ 0x2F5876C
	// (+0x57C4). BUT the append cursor ("dataCount") is at 0x2F7876C -- a
	// SEPARATE, NON-CONTIGUOUS global (dword_2F7876C), NOT at gameState+0x77C4 as
	// previously (wrongly) assumed -- that offset was pure padding that happened
	// to always read 0 (confirmed live: 1186/1186 samples during an active match
	// read exactly 0), which is why write_gamestate_data_ always silently failed.
	// The real cursor is exposed separately below via cs_data_count. Also fixed:
	// stringData's real size is 0x20000 (131072), not 0x2000 -- confirmed by
	// sub_342F90's own overflow guard (`v4 + dword_2F7876C + 1 > 0x20000`).
	struct gameState_t
	{
		int stringOffsets[0x15F1]; // +0x0000  (5617 entries -> 0x57C4 bytes)
		char stringData[0x20000];  // +0x57C4  (dataCount bytes used; real size, was wrongly 0x2000)
	};
	static_assert(offsetof(gameState_t, stringData) == 0x57C4, "gs.stringData");

	namespace mp
	{
		enum { DEMO_TYPE_NONE = 0, DEMO_TYPE_CLIENT = 1, DEMO_TYPE_SERVER = 2 };

		// -- playerState_t : VERIFIED MWR offsets (h1-mod playerState_s in structs.hpp +
		// CL_GetPredictedPlayerInformationForServerTime 0x3409C0 write offsets a3[..]):
		//   pm_type@0x02, otherFlags@0x30, eFlags@0x58, bobCycle@0x74 (a3[29]), origin@0x78
		//   (a3[30]), velocity@0x84 (a3[33]), movementDir@0xC8 (a3[50]), delta_angles@0x12C.
		//   NOTE: playerState has NO absolute viewangles (the view = usercmd.angles +
		//   delta_angles), so demo view-angle restore is a separate TODO.
		struct playerState_t
		{
			char _pad_00[0x02];
			char pm_type;             // +0x02  VERIFIED (h1)
			char _pad_03[0x30 - 0x03];
			int otherFlags;           // +0x30  VERIFIED (h1)
			char _pad_34[0x58 - 0x34];
			int eFlags;               // +0x58  VERIFIED (h1)
			char _pad_5C[0x74 - 0x5C];
			int bobCycle;             // +0x74  VERIFIED (CL_GetPredicted a3[29])
			vec3_t origin;            // +0x78  VERIFIED (a3[30] / h1)  <- local camera position
			vec3_t velocity;          // +0x84  VERIFIED (a3[33] / h1)
			char _pad_90[0xC8 - 0x90];
			int movementDir;          // +0xC8  VERIFIED (a3[50])
			char _pad_CC[0x12C - 0xCC];
			vec3_t delta_angles;      // +0x12C VERIFIED (h1)
			// ---- below: no verified MWR offset; present only so the demo compiles. MWR has no
			//      vehicles (vehicleState inert), commandTime is read only by the disabled
			//      predicted-capture recording path, viewangles restore is unsolved (TODO).
			int commandTime;          // +0x138 TODO(h1)
			int remoteControlEnt;     // +0x13C TODO(h1)
			int viewlocked_entNum;    // +0x140 TODO(h1)
			int weapon;               // +0x144 TODO(h1)
			vec3_t viewangles;        // +0x148 TODO(h1)
			PlayerVehicleState vehicleState; // +0x154 TODO(h1)
			char _pad_end[0x100];
		};
		static_assert(offsetof(playerState_t, pm_type) == 0x02, "ps.pm_type");
		static_assert(offsetof(playerState_t, otherFlags) == 0x30, "ps.otherFlags");
		static_assert(offsetof(playerState_t, eFlags) == 0x58, "ps.eFlags");
		static_assert(offsetof(playerState_t, bobCycle) == 0x74, "ps.bobCycle");
		static_assert(offsetof(playerState_t, origin) == 0x78, "ps.origin");
		static_assert(offsetof(playerState_t, velocity) == 0x84, "ps.velocity");
		static_assert(offsetof(playerState_t, movementDir) == 0xC8, "ps.movementDir");
		static_assert(offsetof(playerState_t, delta_angles) == 0x12C, "ps.delta_angles");

		struct usercmd_t { int serverTime; char _pad[0x3C]; }; // demo reads .serverTime

		// -- clSnapshot_t : type of cl->snap. Occupies cl+0..~19076 (playerState +
		// entities up front, metadata in the tail). VERIFIED tail offsets (cl-absolute,
		// and snap starts at cl+0 so they're snap-relative too):
		//   valid @ 18988, serverTime @ 19012. deltaNum placed after (TODO confirm).
		struct clSnapshot_t
		{
			playerState_t ps;         // +0x0000  snap.ps (playerState at snapshot front)
			char _pad_after_ps[18988 - sizeof(playerState_t)]; // -> valid
			int valid;                // +18988 (0x4A2C)  VERIFIED
			char _pad_a30[19012 - 18992];
			int serverTime;           // +19012 (0x4A44)  VERIFIED
			int messageNum;           // +19016
			int deltaNum;             // +19020           TODO(h1) confirm (== -1 test)
			char _pad_end[19076 - 19024]; // snap ends right before cl->serverTime
		};
		static_assert(offsetof(clSnapshot_t, valid) == 18988, "snap.valid");
		static_assert(offsetof(clSnapshot_t, serverTime) == 19012, "snap.serverTime");
		static_assert(sizeof(clSnapshot_t) == 19076, "clSnapshot_t size");

		// -- clientActive_t : VERIFIED pinned offsets (h1 static RE) --------------
		// cl = *(clientActive_t**)0x2EC84F0 (pointer deref).
		struct clientActive_t
		{
			clSnapshot_t snap;        // +0x0000  (ends at 19076)  cl->snap.valid/serverTime/deltaNum/ps
			int serverTime;           // +19076 (0x4A84)  cl->serverTime   TODO(h1) confirm (v3[4769])
			char _pad_A88[19100 - 19080];
			int newSnapshots;         // +19100 (0x4A9C)  cl->newSnapshots VERIFIED
			char _pad_A9C[27520 - 19104];
			ClientArchiveData clientArchive[256]; // +27520 (0x6B80) stride 36 VERIFIED (ends 36736)
			int clientArchiveIndex;   // +36736 (0x8F80)  VERIFIED
			// Record-path fields (usercmd ring + predicted-time). TODO(h1): real MWR
			// offsets — placed after the verified region; the predicted-throttle path
			// that reads cmds[] is gated, so exact offsets are not load-critical.
			int oldSnapServerTime;
			int cmdNumber;
			int cgamePredictedDataServerTime;
			usercmd_t cmds[128];
			char _pad_end[0x400];
		};
		static_assert(offsetof(clientActive_t, serverTime) == 19076, "cl serverTime");
		static_assert(offsetof(clientActive_t, newSnapshots) == 19100, "cl newSnapshots");
		static_assert(offsetof(clientActive_t, clientArchive) == 27520, "cl clientArchive");
		static_assert(offsetof(clientActive_t, clientArchiveIndex) == 36736, "cl clientArchiveIndex");

		// -- cg_t : monolithic (h1 == iw6 engine, NOT split like WWII). ------------
		// TODO(h1): field offsets ported from iw6; verify demoType/cvsData/refdef/weaponSelect vs MWR cg_s.
		struct weaponSelect_t { std::uint64_t data; };
		struct ClientVisionSetLerpData { int style; };
		struct ClientVisionSetData
		{
			ClientVisionSetLerpData visionSetLerpData[CS_VISIONSET_COUNT];
			float postApplyLerpDest;
			char visionName[6][64];
			char visionSetCurrent[0x2A8];
		};
		struct bgs_t { char _pad[0x10]; };
		struct cg_t
		{
			playerState_t ps;                        // +0x00
			char _pad_after_ps[0x5978 - sizeof(playerState_t)];
			int clientNum;                           // +0x5978 TODO(h1)
			char _pad_597C[0x598C - 0x597C];
			int demoType;                            // +0x598C TODO(h1)
			char _pad_5990[0x6418 - 0x5990];
			weaponSelect_t weaponSelect;             // +0x6418 TODO(h1)
			int inKillCam;
			bgs_t bgs;
			char _pad_to_refdef[0x1E6904 - 0x6428];
			vec3_t refdefViewAngles;                 // +0x1E6904 TODO(h1)
			char _pad_to_cvs[0x4378F8 - 0x1E6910];
			ClientVisionSetData cvsData;             // +0x4378F8 TODO(h1)
		};

		// -- clientConnection_t (clc) : REAL h1 offsets, VERIFIED via IDA decompile of
		// CL_WritePacket 0x13D490 + CG_ExecuteNewServerCommands 0x11A410 + CL_AddReliableCommand
		// 0x12B810 (all deref clc = *(clientConnection_t**)0x2EC8510):
		//   reliableSequence @0x12C, reliableAcknowledge @0x130, reliableCommands @0x134 (x128,
		//   stride 0x400), serverMessageSequence @0x40134 (v1[65613], the snapshot delta base
		//   compared to cl->snap.messageNum), serverCommandSequence @0x40138, lastExecuted
		//   ServerCommand @0x4013C, serverCommands @0x40140 (x128, stride 0x400).
		struct clientConnection_t
		{
			char _pad_00[0x12C];
			int reliableSequence;                    // +0x12C
			int reliableAcknowledge;                 // +0x130
			char reliableCommands[128][1024];        // +0x134  (ends 0x20134)
			char _pad_20134[0x40134 - 0x20134];      // +0x20134  gap (outgoing/netchan buffers)
			unsigned int serverMessageSequence;      // +0x40134
			int serverCommandSequence;               // +0x40138
			int lastExecutedServerCommand;           // +0x4013C
			char serverCommands[128][1024];          // +0x40140 (ends 0x60140)
		};
		static_assert(offsetof(clientConnection_t, reliableSequence) == 0x12C, "clc reliableSequence");
		static_assert(offsetof(clientConnection_t, reliableAcknowledge) == 0x130, "clc reliableAcknowledge");
		static_assert(offsetof(clientConnection_t, serverMessageSequence) == 0x40134, "clc serverMessageSequence");
		static_assert(offsetof(clientConnection_t, serverCommandSequence) == 0x40138, "clc serverCommandSequence");
		static_assert(offsetof(clientConnection_t, lastExecutedServerCommand) == 0x4013C, "clc lastExecutedServerCommand");
		static_assert(offsetof(clientConnection_t, serverCommands) == 0x40140, "clc serverCommands");

		struct cgs_t
		{
			char _pad_00[0x18];
			int serverCommandSequence;               // +0x18 TODO(h1)
			char _pad_1C[0x44 - 0x1C];
			char szHostName[256];                    // +0x44 TODO(h1)
		};

		struct clientStatic_t
		{
			gameState_t gameState;
			int realtime;
		};

		struct omnvar_t { int value; };
		// snapshot_t : CG_UpdateOmnvars param. Demo reads .ps and .omnvars[103/194].
		// TODO(h1): omnvar index base / ps offset must match real MWR snapshot layout.
		struct snapshot_t
		{
			playerState_t ps;
			omnvar_t omnvars[256];
		};

		// Server structs (client_t / gentity_s / serverStatic_t) come from h1-mod's
		// game namespace; the sv-recording component (demo_sv_recording) is excluded
		// from this build (dedi-only, needs SV_Netchan_Transmit which is unresolved).

		struct centity_s { char _pad[0x10]; }; // only &centities[n] is taken (turret stubbed)

		// =====================================================================
		//  GLOBAL ACCESSORS
		// =====================================================================
		// cl : *(clientActive_t**)0x2EC84F0  (VERIFIED pointer deref)
		class cl_accessor
		{
		public:
			clientActive_t* get() const { return *reinterpret_cast<clientActive_t**>(game::base_address + 0x2EC84F0); }
			clientActive_t* operator->() const { return get(); }
			operator clientActive_t*() const { return get(); }
		};
		WEAK cl_accessor cl{};

		// cg : CG_GetLocalClientGlobals() (h1-mod symbol, returns cg_s*; reinterpret to demo cg_t)
		class cg_accessor
		{
		public:
			cg_t* get() const { return reinterpret_cast<cg_t*>(game::CG_GetLocalClientGlobals()); }
			cg_t* operator->() const { return get(); }
			operator cg_t*() const { return get(); }
		};
		WEAK cg_accessor cg{};

		// connstate : (int)*(0x2EC82C8)  (VERIFIED)
		class connstate_accessor
		{
		public:
			connstate_t* get() const { return reinterpret_cast<connstate_t*>(game::base_address + 0x2EC82C8); }
			connstate_t& operator*() const { return *get(); }
			operator connstate_t() const { return *get(); }
			connstate_accessor& operator=(const connstate_t v) { *get() = v; return *this; }
		};
		WEAK connstate_accessor connstate{};

		// clc : REAL global — clc = *(clientConnection_t**)0x2EC8510 (VERIFIED pointer deref;
		// h1-mod symbols.hpp:504 also exposes it as connect_state{0x0,0x2EC8510}). This is what
		// CL_ParseServerMessage/CL_ParseSnapshot read for the snapshot delta base + server
		// commands, so process_network_data MUST write serverMessageSequence here (not a stub)
		// or fed snapshots never apply (snap.valid stays 0).
		class clc_accessor
		{
		public:
			clientConnection_t* get() const { return *reinterpret_cast<clientConnection_t**>(game::base_address + 0x2EC8510); }
			clientConnection_t* operator->() const { return get(); }
			operator clientConnection_t*() const { return get(); }
		};
		WEAK clc_accessor clc{};

		// cgs : TODO(h1) — still placeholder port-local storage (not on the core snapshot path;
		// only used for server-command-sequence bookkeeping + host name).
		inline cgs_t& cgs_storage() { static cgs_t s{}; return s; }
		template <typename T, T& (*S)()> class stub_ptr
		{
		public:
			T* get() const { return &S(); }
			T* operator->() const { return &S(); }
			operator T*() const { return &S(); }
		};
		WEAK stub_ptr<cgs_t, cgs_storage> cgs{};                // TODO(h1) real cgs

		// cls : REAL overlay — clientStatic_t.gameState is the config-string store @
		// 0x2F52FA8 (VERIFIED). gameState is member offset 0 so cls-> lands correctly.
		// NOTE: cls->realtime is NOT contiguous on MWR (lands on unrelated memory); it is
		// only used by playback fast-forward, not recording. TODO(h1): find real realtime.
		class cls_accessor
		{
		public:
			clientStatic_t* get() const { return reinterpret_cast<clientStatic_t*>(game::base_address + 0x2F52FA8); }
			clientStatic_t* operator->() const { return get(); }
			operator clientStatic_t*() const { return get(); }
		};
		WEAK cls_accessor cls{};

		// cs_data_count : the REAL, separately-addressed config-string append cursor
		// (dword_2F7876C, VERIFIED via sub_342F90 decompile 2026-07-07 -- NOT contiguous
		// with cls->gameState, see gameState_t comment above). This is what write_
		// gamestate_data_ must read (not cls->gameState.dataCount, which no longer exists
		// as a member) and what update_gamestate_config_strings must write on restore.
		class cs_data_count_accessor
		{
		public:
			int* get() const { return reinterpret_cast<int*>(game::base_address + 0x2F7876C); }
			int& operator*() const { return *get(); }
			operator int() const { return *get(); }
			cs_data_count_accessor& operator=(const int v) { *get() = v; return *this; }
		};
		WEAK cs_data_count_accessor cs_data_count{};

		// centities : demo only takes &centities[n] (turret stubbed) -> port-local.
		class centities_accessor
		{
		public:
			centity_s& operator[](int) const { static centity_s dummy{}; return dummy; }
		};
		WEAK centities_accessor centities{};
	} // namespace mp

	// CG_SelectWeapon: write cg->weaponSelect.data directly (WWII-port approach).
	inline bool CG_SelectWeapon(int /*localClientNum*/, int weapon, int /*useAltMode*/)
	{
		mp::cg->weaponSelect.data = static_cast<std::uint64_t>(weapon);
		return true;
	}
} // namespace game
