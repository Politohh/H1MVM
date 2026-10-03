# Changelog

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
