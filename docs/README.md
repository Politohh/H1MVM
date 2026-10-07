# Documentation

Start with the [project README](../README.md) for installation and a first replay. The [changelog](../CHANGELOG.md) separates published releases from changes on `main`.

## Using the client

| Guide | Contents |
| --- | --- |
| [Theater library](theater-library.md) | Import recordings, convert COD4 demos, browse and play results. |
| [Playback, cameras and editor](native-theater.md) | Timeline shortcuts, free camera, dolly paths, visual settings and capture controls. |
| [Classic COD4 snipers](cod4-snipers.md) | Original animations on remastered models, local input preparation and scope customization. |
| [Build and contribute](../CONTRIBUTING.md) | Source setup, regression checks and useful bug reports. |

## Conversion and implementation

- [COD4x protocols 19 and 21](cod4x-protocol21.md)
- [Actor animation audit and replacement coverage](cod4-actor-animation-audit.md)
- [Facial animation implementation and validation](cod4-facial-prototype.md)
- [Recorded crouch/prone camera height](cod4-camera-stance.md)
- [Promod animation mapping](strike-promod-animation-mapping.md)
- [Corpse animation and framing](promod-corpse-framing-fix.md)
- [Replay player slots and larger recordings](warz-player-slots.md)
- [Startup trampoline allocation](startup-trampoline-allocation.md)

## HUD and effects

- [Native teams, factions and grenade inventory](cod4-hud-teams-equipment.md)
- [Minimap and hitmarkers](minimap-hitmarkers.md)
- [Blood effects](promod-blood-fx.md)
- [Airstrike jets, bombs and attached effects](cod4-airstrike.md)
- [Helicopter flight and first-spawn crash correction](cod4-helicopter.md)

## Validation and research archive

These notes preserve the evidence, limitations and earlier approaches behind the implementation. Dated results apply to the revisions they describe; they do not establish current visual acceptance for every recording.

- [Promod stress-test evidence](promod-stress-test.md)
- [Mapping coverage and investigation history](cod4-mapping-coverage.md)
- [Native weapon and grenade FX research](native-fx-research.md)
- [Vehicle animation and debris research](vehicle-animation-research.md)
- [COD4 demo architecture research](cod4-demo-architecture.md)
- [Transcoding reconnaissance](cod4-transcode-recon.md)
- [Transcoding plan](cod4-transcode-plan.md)
- [Early camera preview](cod4-camera-preview.md)
- [Historical serverless loader status](cod4play-serverless-status.md)
- [Early MWR mapping notes](demo_mwr_mapping.md)

Active implementation lives in [`src/client/component/demo/`](../src/client/component/demo/). `demo_prior_skeleton/` is historical reference material. Original recordings, game assets and locally generated diagnostics are not bundled.
