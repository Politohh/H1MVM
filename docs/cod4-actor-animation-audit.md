# Complete recorded actor animation audit

The previous regression suite covered the animations in selected demo files.
It did not cover every named entry in CoD4's multiplayer animation table.
`demo0011.dm_1` reached an untested `pb_hold_idle` entry after its asset-loading
failure was fixed. A full-table audit found 41 missing source-name mappings.

## Source and native coverage

The reference is the CoD4 Mod Tools at commit
`95c76d6a470bea43fdd5ab591afe8475fac7699d`: `raw/mp/playeranim.script`,
`raw/animtrees/multiplayer.atr`, and the complete `raw/xanim` asset listing.
The cached source scripts are checked against their pinned SHA-256 hashes.

- 5,258 xanim assets are inventoried in [the asset report](cod4-modtools-xanim-inventory.tsv).
- 441 of those assets are referenced by the active multiplayer animation tree.
- The verified network animation table has 318 entries: root, 70 unused slots,
  244 named clips and three named SAW aiming blend nodes. All 247 named entries
  are present in the Mod Tools tree and resolve to native MWR animations.
- The remaining tree assets include blend children and clips not assigned a
  recorded actor ID. The inventory labels assets outside the recorded actor
  table explicitly; it does not claim to implement SP cinematics or arbitrary
  custom animation trees. First-person weapon animation selection continues
  through native MWR weapon definitions.

[The mapping report](cod4-actor-animation-coverage.tsv) lists every recorded
name across eight weapon families, its native target, exact/replacement status,
and native leg/torso channel. Runtime targets resolve by name from the loaded
H1 table. Exact matches retain priority. Unknown IDs and unused slots still
fail explicitly instead of silently becoming idle poses.

## Changes

Added standing/crouched hold idles and directional runs, RPG sprint/prone/crouch
and directional locomotion, crouch-to-prone transitions, directional crouch
turns, standing bomb stance, high mantle, crouched automatic ADS fire, unarmed
locomotion and stance-specific pain reactions. `pb_hold_idle` now resolves to
`mp_hold_idle`; `pb_crouch_hold_idle` resolves to `mp_crouch_hold_idle`.

Some replacements are approximations using native authored clips: unarmed
walk uses hold locomotion, extended pain uses stance/weapon-specific flinches,
high mantle uses the native 57-height clip, and the absent crouched automatic
ADS firing layer uses the native automatic firing layer. These are not claims
of identical timing or motion. Existing crouched bomb and legacy death/flinch
approximations remain documented by their explicit native targets.

Where a CoD4 full-body reaction maps to an MWR upper-body clip, the converter
now preserves prone/crouched stance and weapon family in the lower body. It
preserves recorded restarts and does not overwrite an explicit recorded torso
animation. Runtime validation audits all distinct recorded actor states and
logs all failures together if a different native table is missing targets.

## Validation

- `tests/run-cod4-actor-catalog.cmd`: all 247 named entries, eight weapon
  families, 3,952 channel/restart checks; 173 source entries use a replacement
  for at least one family. Tests include root/unused/invalid IDs, exact-match
  priority, prone/crouch preservation and explicit torso precedence.
- `python tests/audit_cod4_modtools_anims.py`: full Mod Tools inventory and
  source-script/hash/tree membership checks against the generated mapping TSV.
- All actor states resolve in `demo0011` (42,512), Diablo (151,648), Warz
  (27,943), Strike TDM (11,175), Promod Crash (10,352) and City (1,783).
- Release build and installation are separate from visual acceptance. Native
  conversion/playback of this build remains pending user verification.
