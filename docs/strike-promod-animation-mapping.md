# Strike Promod actor animation conversion

The CoD4x protocol 21 recording `TDM_PROMOD_STRIKE_5k_r700_SICK_NOSC_SKY.dm_1`
(`mods/up-pml221`, `mp_strike`) failed at source animation
`pt_flinch_grenade_left`. The original recording has SHA-256
`B5DA2BD35F9F6B4D1A1C1539564182725AE28885CD40DCD7419E75BD21609214`.

The live CoD4 client loaded the recording and exposed 318 animation indices;
every entry matched `cod4_animation_catalog.hpp`. The linked
[Promod player animation script](https://github.com/promod/promod4/blob/master/mp/playeranim.script)
has empty flinch actions, but this recording identifies a separate custom
`up-pml221` server mod. Its exact animation script is unavailable locally.
Consequently, the source networked flinch IDs are treated by their verified
CoD4 index names; the converter does not infer a different source index table
from the generic Promod script.

`cod4_actor_animation.hpp` maps the recorded flinch states to native MWR
upper-body flinch clips for grenade, pistol, rifle, and launcher poses.
Directional CoD4 names have no direct directional MWR counterpart, so the
native `a` variant is used. The same fix maps recorded melee, crouched grenade
throw, rifle ADS fire, shotgun fire, mantle, bomb-stance, and crouched grenade
run states to native clips. The existing restart bits and native animation
codec are retained.

Validation on 2026-09-29:

- The Strike fixture has 595 world snapshots, 11,175 recorded actor states,
  including 1,393 flinch states. All resolve with the live MWR 1,709-entry
  animation catalog. Synthetic checks cover 12 source flinch names and both
  restart states.
- The City actor regression passed (814 actors, 969 corpse states); the
  earlier 5v5 Promod stress regression passed (10,352 actor states in its
  fixture, no mapping failure).
- Release x64 build SHA-256:
  `564C4036A4AECF312A0641280F5303053842265651C75E7A6E96147AE9BC6CF3`.
  The installed executable matches. Rollback copy:
  `build/install-backups/20260929-184857-before-strike-animation/h1-mod.exe`.
- Native H1 conversion saved 3,770 snapshots over 31.9 seconds. The converted
  demo passed `validate_converted_demo.py` and native playback reached
  “Demo finished. Holding the last frame” without a fatal error.
- The source fixture is byte-identical to the original recording. The validated
  converted bytes were also copied to
  `demos/client/auto/TDM_PROMOD_STRIKE_5k_r700_SICK_NOSC_SKY.dm_h1`
  (SHA-256 `776ACC8BFA7335412BB966178D6CA5B22D2E512C57192D4C2A73D5E127868344`)
  so it appears under the original clip name in H1's demo list.

Visual accuracy of the flinch choice still needs user review because the exact
`up-pml221` source script is unavailable; runtime validation confirms loading
and completion, not a frame-by-frame comparison with the original mod.
