# Changelog

## Unreleased

## 2026.10.08.1

[Release](https://github.com/Politohh/h1-cod4-demo/releases/tag/demo-2026.10.08.1)

- Fixed Model Swapper appearance names drawing underneath HEAD/BODY tiles. Titles and faction/class details now use a separate clipped text column, with text fitting recalculated when cards are reused. Long names keep their beginnings and shorten at the end.
- Fit the selected appearance's title and details to the preview panel. Preserve widget reuse and deferred navigation for safe paging and filtering.
- Show a faction/class fallback when a portrait material is unavailable during playback, instead of the checkerboard. Available portraits still display and model swapping remains enabled. This does not restore frontend-only portrait assets; complete portrait loading remains unresolved.

The regression harness covers card layout, focus-state resets, long-name reuse, and missing/available portrait transitions. Final visual confirmation across resolutions remains pending. The reported screen-fixed blurred specks are still under investigation; this release does not include an overlay fix.

## 2026.10.08

[Release](https://github.com/Politohh/h1-cod4-demo/releases/tag/demo-2026.10.08)

- Added the saved `cod4_fov` toggle for live multiplayer, devmaps and replays. Hip weapon framing follows `cg_fov`, fully aimed FOV stays fixed, and `cg_fovScale` affects both. It also preserves the late zoom transition used by `cod4_snipers`.
- Stabilized Model Swapper navigation by deferring category changes until button dispatch finishes, reusing widgets and fetching only four visible appearance cards from the native catalog. Filters and paging no longer rebuild the entire menu or copy the full catalog into Lua on every refresh.

Release/x64 compilation and the FOV, sniper and Theater regression checks passed. Native checks verified hip FOV 65/80, unchanged fully aimed FOV 15, and scaled ADS FOV 16.875. Model Swapper's 400-click native stress check passed on the preceding test build; the final build passed its offline interaction regressions.

- Fixed conversion of demos with non-client obituary attackers, including `City_AKu_1-1__4man.dm_1`. Legal source entity references now retain the death as a world-attributed notification instead of aborting conversion or indexing a player row with an entity number.
- Added **Model Swapper** to Theater Editor: select a replay player, browse head/body appearances by faction, class and collection, then Apply. The full-width browser includes native portraits when available, default appearance cards, scene camera focus, per-player restore and demo-specific save/load.
- Acquire native character transient fastfiles on demand, preserve weapon/equipment attachments and restore the recorded appearance without modifying the demo or profile. Swaps follow recorded player identities across snapshots and seeks. Weapons and POV hands are outside this first version.
- Corrected shared-skeleton head attachment tags, forwarded the native agent rebuild mask, and avoid marking unchanged replacements dirty every snapshot.

- Restored native Character, body Camo, Armory and Collections selectors. The inherited Depot override replaced every tab list with Collections/Armory when its obsolete agreement flag was false, leaving those menus empty.
- Added profile-specific local Depot persistence with validated balances, atomic replacement and backups. Crafting resolves purchase codes to item GUIDs, rejects insufficient funds, rolls back failed saves and delivers native completion events. Collection/season rewards and inventory checks share the same ownership data; the existing Unlock All Loot option also updates collection counts.
- Repaired local supply-drop completion, bounded reward selection, duplicate-event protection and bundle/crate accounting. Ranked-match earnings persist; replay playback does not award currency. This is local Depot support, not an official marketplace or real-money store.

- Added **Split Demo** to Theater for converted H1 replays and original COD4 inputs. Start/end controls save a separate `.dm_h1` clip under `demos/clips`, preserving originals and choosing unused names. Converted files are cut directly on a worker with progress feedback; COD4 sources use the existing converter to emit only the selected range.
- Preserve original snapshot/camera payloads and asset metadata for direct cuts, renumber packets/archives, and rebuild duration and range-filtered kill markers. Back restores the library selection/filter. Native H1 match recordings with dependent snapshots do not yet support direct splitting.

Release/x64 compilation, splitter checks on the 27-minute Crash source/conversion, and Theater menu/library checks passed. A 10:00–10:05 direct cut retained 418 frames with a 4,998 ms span. In-game splitter rendering and clip playback still need confirmation.

## 2026.10.07.3

[Release](https://github.com/Politohh/h1-cod4-demo/releases/tag/demo-2026.10.07.3)

- Added indexed backward seeking for converted COD4 replays. Left Arrow, the five-second Back button and backward timeline scrubs jump to a nearby full snapshot instead of restarting at the beginning. The index builds during playback; existing conversions do not require reconversion.
- Restore the selected frame's recorded camera data and reset native snapshot/prediction history before catch-up. Paused state and playback speed are preserved. Native H1 recordings and unsupported/discontinuous ranges retain the restart fallback.

Release/x64 compilation and offline seek checks passed, including the reported 27-minute Crash conversion: 137,738 independent snapshots, 1,652 anchors and a maximum 2,203 ms of catch-up across all indexed destinations. This is demo time, not a measured wall-clock latency. In-game seek behavior still needs confirmation.

## 2026.10.07.2

[Release](https://github.com/Politohh/h1-cod4-demo/releases/tag/demo-2026.10.07.2)

- Fixed Theater dropping a queued Play request back to the online menu when the client is disconnected and its frontend lobby is unloaded. Theater now remains open until playback starts and reports a deferred launch.
- Read duration/timeline metadata from the bounded native footer, avoiding a packet-by-packet scan of large recordings at library refresh and playback startup.
- Reduced converter compression work using fast, compatible zlib packets while preserving every camera sample and native packet payload.
- Fixed long COD4 conversions exhausting the reserved event entity range and aborting with `Invalid H1 obituary client or entity number`. The converter now allocates additional unique event IDs from slots unused throughout the recording, preserving actor/prop ownership and seek metadata.

Release/x64 build and offline metadata, compression, Theater UI and event allocation checks passed. The reported Crash recording covers 1,042 unique events with zero actor/prop collisions. Conversion previously completed locally with 137,738 snapshots; the latest playback-launch changes and end-to-end conversion speed still need in-game confirmation. Retry failed conversions from the original `.dm_1`; existing completed recordings can use the playback and library fixes without reconversion.

## 2026.10.07.1

[Release](https://github.com/Politohh/h1-cod4-demo/releases/tag/demo-2026.10.07.1)

- Fixed conversion and direct COD4 preview aborting on `destructible_car` kill notifications. Car-explosion deaths preserve recorded attribution and timing using a native generic death notification; no unrelated weapon is substituted.

Release/x64 build and offline checks passed against the reported Crash recording: 236 kill notifications, including 12 headshots and one car-explosion death, with zero unmapped required weapons. Synthetic checks cover weapon-table order, duplicate notifications, seek timing and malformed indices. Retry failed conversion from the original `.dm_1` after updating; visual playback of this fix remains unverified.

## 2026.10.07

[Release](https://github.com/Politohh/h1-cod4-demo/releases/tag/demo-2026.10.07)

- Added optional classic COD4 sniper playback on remastered M40A3, R700, Barrett, M21 and Dragunov models, including original actions, recoil parameters and flat scopes.
- Moved sniper scene zoom to the final 5% of ADS and removed the scope opacity fade on release.
- Corrected native optic centering, magazine contact, bolt-cap movement, sprint-loop phase and R700/M40A3 pull-out cartridge placement.
- Restored demo HUD attachment and recorded match mode/team scores while preserving native-match behavior.
- Added a demo pause menu and improved Theater conversion/playback routing, focus and return-to-library behavior.
- Reorganized the README, setup guides and documentation index around installation, playback and cinematic use.

Release/x64 builds and offline sniper, match-data, facial and Theater UI checks passed. User playback feedback confirmed substantial improvement; complete visual parity for every native rig remains unverified. Runtime sniper/UI corrections do not require demo reconversion. Converter data changes still require reconverting the original recording.

## 2026.10.06

[Release](https://github.com/Politohh/h1-cod4-demo/releases/tag/demo-2026.10.06)

- Added the native **Play Online → Theater** library for H1 and COD4 recordings, with map previews, recording titles, paging, import, conversion and playback actions.
- Restricted library scanning to the game's `demos` folder and saved newly converted recordings directly there.
- Added the native timeline and Theater editor, HUD and control toggles, camera tools, graphics controls, color pickers, sky selection and reset actions.
- Added COD4 facial playback using native facial head models, with tuned expressions and smoother release to idle.
- Fixed demos that use more distinct player slots than MWR supports, preserved corpse identities and increased the demo animation bone budget.
- Smoothed recorded crouch, prone and airborne camera-height transitions.

Release/x64 build and offline Theater, metadata, facial, player-slot and camera regressions passed. Native Theater rendering was checked in H1; the crouch and bone-budget fixes were confirmed in playback. Import, conversion and playback actions from the new library still need a complete interactive check. Reconvert original COD4 recordings to use converter changes.

## 2026.10.03

[Release](https://github.com/Politohh/h1-cod4-demo/releases/tag/demo-2026.10.03) · [Source revision](https://github.com/Politohh/h1-cod4-demo/tree/7242c58d70af0a4bb59cb5f1305c99640fe5c56e)

- Added CoD4x protocol 21 recording support and expanded actor animation mappings.
- Added native MWR helicopter flight and airstrike conversion, including attached jet effects and bomb trajectories.
- Corrected jet trajectory serialization and the integer team selection used by the native faction HUD.
- Improved recorded team, grenade inventory, killfeed and score data for MWR's HUD.
- Added replay client-slot, weapon-budget and conversion safety corrections.

Built from a clean checkout with pinned dependencies. Seven offline regression checks passed: HUD, airstrike, helicopter, actor catalog, CoD4x protocol, transient weapon budget and grenade events. Package hashes and the embedded TLS helper were verified.

Latest jet-motion and team-emblem corrections await visual playback confirmation. Helicopter combat events, general sound-event playback and killstreak inventory HUD remain incomplete. Reconvert original CoD4 demos to use conversion changes.

## 2026.10.01

[Release](https://github.com/Politohh/h1-cod4-demo/releases/tag/demo-2026.10.01) · [Source revision](https://github.com/Politohh/h1-cod4-demo/tree/99d191f119c107c7a84931ab3c5998ee69cf8f37)

- Packaged the H1 demo client with its required runtime scripts and separate crash symbols.
- Included recording/playback, F9 controls, pause/seek/speed, free camera and camera paths.
- Included CoD4 conversion, native actor animations and effects, and native MWR score-popup and killfeed integration.

Release/x64 build and HUD, gun-event and grenade-event fixture checks passed. No new interactive playback test was performed for the package.
