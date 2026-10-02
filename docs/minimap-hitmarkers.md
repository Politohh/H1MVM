# Converted minimaps and hitmarkers

The converter resolves the recorded compass material against loaded native MWR
assets. It preserves the recorded bounds and north yaw. Both the original name
and MWR's `_dlc` variant are supported without a map allowlist. Default/missing
materials are rejected during conversion instead of saving a checkerboard.
The configstring reader applies the same resolution to existing converted demos;
ordinary native gameplay is unaffected.

Crash was inspected live: its native configstring 1228 contains
`compass_map_mp_crash_dlc`, and that material is resident. COD4's Crash recording
names `compass_map_mp_crash`. North yaw is configstring 1227. No replacement
texture or custom minimap widget is created.

Recorded COD4 HUD material elements named `damage_feedback` or
`damage_feedback_j` supply hit confirmations. Their fade starts on the hit,
unlike the score popup whose fade starts later. Initial history, unchanged
snapshots, duplicate HUD banks, and stale timestamps restored by edited demos
do not produce additional hits.

The loaded MWR `damagefeedbackhud.lua` listens to the `damage_feedback` player
omnvar. Conversion resolves this omnvar and the `standard` / `hitjuggernaut`
LUI net-constant strings by name, writes recorded hit times into player state,
and playback sends the native omnvar notification. The native UI owns the
material, layout and animation. Hitmarkers require reconverting old demos.

Demo loading also sets `developer` to `1`, for both native client demos and
direct COD4 conversion/playback.

## Validation

`tests/run-cod4-minimap-hitmarkers.cmd` checks both material naming conventions
over 22 map names, preservation of bounds, rejection of missing/default assets,
and hitmarker timing/deduplication. Recorded fixtures contain four Warz hits,
five Promod Crash hits and five Strike hits at their exact source timestamps.
These naming tests do not constitute visual acceptance on all 22 maps.

`tests/run-cod4-hud-timing.cmd` independently preserves the already accepted
score timing: 26 kill awards and two recorded assists across Crossfire/Diablo.
The new build's minimap and hitmarker rendering still require playback testing.
