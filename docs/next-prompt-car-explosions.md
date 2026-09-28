# Next prompt: native MWR car explosion events

Continue in `F:\Dev\h1-cod4-demo` on `main`, private repository
`https://github.com/Politohh/h1-cod4-demo`. Read Git status and
`docs/cod4-mapping-coverage.md`; preserve unrelated untracked research files.
Follow the workspace's AGENTS.md instructions.

The next requested feature is recorded car explosion events using native MWR
effects. Do not start broader firing, bullet impact or smoke grenade work.
Keep the accepted native grenade playback intact. Do not infer an explosion
from a disappearing or changed car mesh alone, import COD4 FX assets, invent
replacement effects or simulate new damage.

The accepted grenade implementation maps COD4 events 45/48/49 to native H1
98/103/104. Frag uses the native impact table, while flash/concussion use their
weapon effects. A null optional frag `projExplosionEffect` is valid. Source
sequence wrap, entity reuse, direction and surface translation are covered by
`tests/cod4_grenade_events.cpp`. Stable event identities and recorded clocks
are embedded in the standalone `.dm_h1`; native playback resets them on seek.

Installed accepted executable SHA-256:
`F8EF68ECB76F7C2A0664E25B60D5459A04B5FAC7DADCF6AD45397BC1111A3929`.
The user confirmed "ok it works good" after the corrected ordinary F9 Crash
conversion saved `cod4_to_H1.0004.dm_h1` with 2,241 snapshots and three grenade
events. Evidence is in `build/analysis/grenade-fx-accepted`, with rollback in
`build/install-backups/20260929-002226-before-grenade-fx`. City flash visual
acceptance and detailed seek/freecam acceptance remain separately undocumented.

Start with an inventory of real car destruction and FX records from
`C:\Games\cod4_game_files\main\demos\cod4_to_H1.dm_1`, Backlot and City. Inspect
entity event rings, temporary events, FX configstrings, scripted FX messages
and model/hide-part transitions as applicable. Verify the recorded mechanism
and source identity before selecting MWR's corresponding native event/assets.
The existing source world reader may need a bounded extension if the relevant
record is currently omitted; do not assume grenade event packing applies.

Relevant implementation is under `src/client/component/demo`:

- `cod4_camera.cpp`: native asset resolution, snapshot writer, conversion report.
- `cod4_world_reader.hpp`, `cod4_world_fields.hpp`: source entities/events.
- `cod4_grenade_events.hpp`: verified grenade inventory and native payloads.
- `cod4_model_translation.hpp`, `cod4_world_translation.hpp`: existing car models
  and hidden-part translation; preserve native bone mapping and glass state.
- `cod4_model_metadata.hpp`, `demo_playback.cpp`: standalone metadata, event clock,
  seek/reset and native playback.

Use original COD4 sources in `work/cod4x-reference`,
`work/cod4-engine-reference` and `work/cod4-mod-tools-reference`, plus verified
installed H1 assets and native code. `work/fx-native-analysis.py` and existing
`build/analysis` references support offline analysis. Do not attach a live
debugger; read-only process inspection is available. Respect full native ABI
argument widths, including entity IDs and local-client arguments.

Keep Play Online initialization followed by F9 Convert, Refresh and Play.
Preserve configured FOV, camera controls, pause/speed/seek/freecam/end hold,
and ordinary H1 recordings. Native effects must use recorded time and position,
avoid duplicate dispatch at camera archive frequency, clear stale seek FX and
reconstruct effects at the correct age. Persistent burning/smoke and car model
state should follow actual recorded evidence, with explicit unsupported-case
diagnostics.

Build Release x64 `client` in `build\h1-mod.sln`. Preserve meaningful existing
mapping, model, projectile/body and view-motion regressions; add focused car
event checks and verify actual native rendering. Install only with MWR closed,
back up the executable and existing recordings/reports, and verify parity.
The game is at
`C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Modern Warfare Remastered`;
outputs belong in its existing `demos\client\auto` folder, using numbered names.

Use the Computer Use skill for UI interaction. If physical Escape stops it,
stop UI input and continue source/build/log work. Report exact converted files,
event times, native FX identities and remaining acceptance limits. Update the
coverage document. Do not push the car changes until the user asks.
