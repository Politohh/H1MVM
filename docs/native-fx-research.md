# Native weapon and grenade FX research

Historical implementation notes and validation evidence from September 2026.
For current features and setup, see the [documentation index](README.md).

Status on 2026-09-29: native frag/flash/concussion and car explosion FX are
implemented; grenade and car FX visuals were accepted. The car animation binding
regression was corrected; native car animation/debris visuals still need
confirmation. Gun firing events are now implemented and installed for the
requested `backlot_cod4_to_h1.dm_1` fixture: 19 shots, 14 AK-47 and five Desert
Eagle. Native visuals remain pending a fresh conversion/playback. Current EXE:
`2A7574330343AAAB4F4A4B5B71ADCE327F954E6672AFEC4D81D44C53B546A3EA`.
Backup: `build/install-backups/20260929-024312-before-native-gun-fx` (28 files).
This is a historical FX handoff. The current installed build is listed in
`docs/cod4-mapping-coverage.md`.
Evidence: `build/analysis/gun-fx-installed`; see current mapping coverage.
Native gun PS sequence is +0x104, event/parameter ring +0x10C/+0x110. Use H1
SV_FIRE_WEAPON 51/52 for recorded local shots; normal 39/40 skips this path.
Rechamber/brass map to 42/43. Do not claim bullet impacts, remote actor firing
or aged gun smoke/brass seek reconstruction complete; this Backlot recording
contains no recorded bullet impacts. The broader priorities below are retained
for context, not a statement that all work is complete. Car handoff is at
[vehicle-animation-research.md](vehicle-animation-research.md).

The investigation used the COD4-to-H1 demo converter in `F:\Dev\h1-cod4-demo`, repository
`https://github.com/Politohh/H1MVM`. The verified
character mapping implementation is commit `62c5eefa315fc2cd80ea6eeee2ba75d54e029129`.
Read the current Git status and `docs/cod4-mapping-coverage.md` before changing code.
Preserve unrelated untracked research files. Follow the workspace's AGENTS.md
instructions. Do not create a replacement COD4 executable.

Implement the missing FX when shooting and using grenades in converted demos.
The goal is to replay the recorded COD4 actions inside H1 using **MWR's native
effects, weapon definitions, models, materials and animations**. Do not port
COD4 FX assets or invent unrelated replacement effects. Use original COD4
sources to understand recorded events, and the installed H1 engine/assets to
verify the native destination behavior. Give missing native effects a clear
diagnostic; optional objects with no MWR counterpart may be omitted and reported.

The output must remain a standalone native `.dm_h1` recording, loaded through
the existing F9 demo controls. Keep the normal Play Online initialization screen,
then F9 → Convert → Refresh → Play. Conversion can load the remastered map's
assets, but must not start a live match or autoplay when entering Play Online.
Preserve the client's configured FOV, native camera controls, pause, speed,
forward/backward seek, freecam and end-frame hold.

Work through these priorities:

1. Map firing events to native MWR muzzle flashes, weapon smoke and shell ejection
   where the weapon supports them. Resolve the effective weapon/attachment kit,
   first-person versus world attachment points, and recorded event timing.
2. Translate recorded bullet impacts into the appropriate native impact FX and
   decals, with recorded position, direction and surface information. Use native
   tracers where the recorded event and MWR weapon call for them. Do not fabricate
   hit positions or turn replay into a new gameplay simulation.
3. Complete frag, stun, flash and smoke grenade events: throwing/bouncing where
   applicable, detonation bursts and persistent smoke. Extend the same verified
   event mapping to RPG and underbarrel explosions when present in the samples.
   Projectile meshes and trajectories already work; a disappearing projectile
   alone is not evidence of a detonation. Preserve the existing native shellshock
   profile mapping for recorded flash/concussion effects.
4. Make event playback consistent through pause, playback-speed changes and
   rewind. Repeated camera snapshots must not retrigger one shot or explosion.
   Clear stale FX when seeking and reconstruct effects that should still be
   alive at the target time, with the correct age. Check this in first-person
   and freecam, and keep ordinary native H1 demos working.

Start with an event inventory from the real samples. The source reader already
decodes player `eventSequence`, `events[4]` and `eventParms[4]`, plus entity events
and temporary event entities. However, `cod4_camera.cpp::feed_snapshot` currently
serializes recorded missiles (`eType == 4`), script models (`eType == 6`), authored
map models and client model metadata; it drops the other recorded entity types.
It writes weapon animation/shot count fields but does not translate the source
event stream. `translate_missile` currently maps weapon, owner, launch time and
trajectory, without the full event payload. Investigate both player events and
entity events; do not assume matching COD4 and H1 enum values, offsets or
parameter packing. Native snapshots are emitted at camera-archive frequency,
which is often much higher than the source server-snapshot frequency.

Relevant source:

- `src/client/component/demo/cod4_camera.cpp`: native snapshot writer, weapon and
  transient asset resolution, `.dm_h1` export and conversion report.
- `cod4_player_fields.hpp`, `cod4_playerstate_reader.hpp`, `cod4_world_fields.hpp`,
  `cod4_world_reader.hpp`: recorded event data. These are under the same folder.
- `cod4_world_translation.hpp`, `cod4_action_translation.hpp`,
  `cod4_weapon_catalog.hpp`, `cod4_view_translation.hpp`: current native mappings.
- `demo_playback.cpp`, `demo_utils.cpp` and `theater_camera.cpp`: native playback,
  seek/reset, camera and end-of-demo behavior. Inspect the actual paths before
  modifying event lifetime or clock handling.

Use primary references. The original COD4 Mod Tools are at
`https://github.com/cod4mw/CoD4-Mod-Tools/tree/95c76d6a470bea43fdd5ab591afe8475fac7699d`.
Local research is available in `work/cod4-mod-tools-reference`,
`work/cod4-engine-reference`, `work/cod4x-reference` and `build/analysis`; these
are local evidence, not all tracked repository dependencies. Treat old offsets
and findings as leads to verify. Avoid a live debugger attach to H1: previous
attaches destabilized the client. Read-only process inspection and offline
binary/dump analysis have been used successfully. Match native ABI argument
widths; the earlier City crash was caused by a byte argument to a native function
that reads a full 32-bit index.

The game installation is:
`C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Modern Warfare Remastered`.
Converted recordings belong in its existing `demos\client\auto` folder. Preserve
all recordings and reports, and use the converter's existing numbered output
behavior when reconverting.

Source demos are in `C:\Games\cod4_game_files\main\demos`:

- `cod4_to_H1.dm_1`: Crash; Deagle, M40A3, firing, ADS, sprint and grenade actions.
- `backlot_cod4_to_h1.dm_1`: Backlot weapon/action regression sample.
- `city_cod4_to_h1_test_player_____.dm_1`: City; flash/concussion states and a
  second recorded client, with missing optional props already reported.
- `BLOC_cod4_to_h1_idle.dm_1`: Bloc; ghillie character/hands regression sample.
- Other readable recordings include `D1ablo_cic7_infinite.dm_1` and
  a local COD4 test recording; inspect their actual events before selecting tests.

The previous build passed stock character mappings across nine readable demos,
850 class/map combinations and 95,270 camera samples. Eight other files have
reader failures before mapping; these are documented and were not counted as
successes. Complete remote player/NPC entity playback is still unfinished.
Keep these limitations distinct from the weapon/grenade FX work.

Installed baseline EXE SHA-256:
`3FFF3CCA78CC4D7E8E6CA9D30A9FE1BAA3E70E62C6404F4EDD488ADCDB4CA0F2`.
The user confirmed successful F9 conversion of Bloc: 6,074 validated native
snapshots, with native Spetsnaz woodland body/head/ghillie hand bindings.
Its playback visuals still need acceptance. Provenance and rollback are in
`build/analysis/bloc-character-mappings` and
`build/install-backups/20260928-231658-before-character-mappings`.

Implement, build and install the FX changes with an executable rollback backup
and hash checks, preserving the existing demos. The local VS 2022 solution is
`build\h1-mod.sln`, target `client`, configuration `Release`, platform `x64`.
Existing local helpers are `work/build-model-visibility.ps1`,
`work/test-model-visibility.cmd` and `work/install-model-visibility.ps1`; inspect
and adapt them instead of assuming their old labels or checks describe FX.
Keep the meaningful mapping and snapshot regression checks, and add focused
checks for event counts/timing, wrapping sequences and seek behavior. Validate
actual native effect rendering in representative firing and grenade segments;
compilation or finding an FX name is not visual acceptance. Report the events
implemented, unsupported events observed, produced demo names and exact runtime
results. Update the coverage document. The user subsequently authorized pushing
the converter work to main.

Use the Computer Use skill for UI interaction. If I press physical Escape, stop
UI input and continue source/build/log work. Do not stop the whole task because
I escaped Computer Use. Ask for a concise manual in-game step only when needed.
