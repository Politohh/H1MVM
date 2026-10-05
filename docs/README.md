# Technical documentation

For installation and everyday use, start with the [project README](../README.md). Published versions are listed in the [changelog](../CHANGELOG.md).

These documents describe individual implementation changes and their evidence. Their dated validation sections apply to the revisions they describe; they are not a claim that every feature has passed visual playback in the latest release.

## Conversion and playback

- [CoD4x protocols 19 and 21](cod4x-protocol21.md)
- [Actor animation audit and replacement coverage](cod4-actor-animation-audit.md)
- [Promod stress test](promod-stress-test.md)
- [Promod animation mapping](strike-promod-animation-mapping.md)
- [Corpse animation and framing](promod-corpse-framing-fix.md)
- [Replay player-slot initialization](warz-player-slots.md)
- [Startup trampoline allocation](startup-trampoline-allocation.md)

## HUD and effects

- [Native teams, factions and grenade inventory](cod4-hud-teams-equipment.md)
- [Minimap and hitmarkers](minimap-hitmarkers.md)
- [Blood effects](promod-blood-fx.md)
- [Airstrike jets, bombs and attached effects](cod4-airstrike.md)
- [Helicopter flight and first-spawn crash correction](cod4-helicopter.md)
- [Mapping coverage and accumulated investigation notes](cod4-mapping-coverage.md)

## Historical research

The following documents include earlier prototypes, plans and superseded findings. Use current source and feature notes to establish present behavior.

- [CoD4 demo architecture research](cod4-demo-architecture.md)
- [Transcoding reconnaissance](cod4-transcode-recon.md)
- [Transcoding plan](cod4-transcode-plan.md)
- [Early camera preview](cod4-camera-preview.md)
- [Historical serverless loader status](cod4play-serverless-status.md)
- [Early MWR mapping notes](demo_mwr_mapping.md)

The active implementation is in [`src/client/component/demo/`](../src/client/component/demo/). `demo_prior_skeleton/` is a historical reference. Regression runners live in [`tests/`](../tests/); original recordings and locally captured game assets are not bundled.
