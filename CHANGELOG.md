# Changelog

## Unreleased

Changes on `main` after the 2026.10.06 package:

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
