# Recorded helicopter flight

The converter retains COD4 `ET_HELICOPTER` entities and uses MWR's native
helicopter renderer, rotor animation and vehicle controllers. Reconvert the
source `.dm_1`; previously converted files omitted these entities.

## Source recording

`helicopter_to_h1.dm_1`, SHA-256
`4D29C18C5080DDBBD9C48371EC9EE4E962D443FE1DD057B07188DA86402FED3F`,
records Overgrown. Cobra entity 330 first appears at clock 80450, about
7.2 seconds after the first camera sample (73243). It appears in 754 world
states spanning about 38 seconds. Its model is
`vehicle_cobra_helicopter_fly`, its stage is 3, and its event ring is empty.
It provides flight coverage, not helicopter shooting or destruction coverage.

## Native bindings

- Source model index is at entity offset 216. H1's corresponding model is
  at offset 208, consumed by `CG_Vehicle` (`0x1405FCEE0`). Cobra keeps its
  native model name; COD4 Hind maps to `h1_vehicle_mi24_hind`.
- Definitions `cobra_mp` and `hind_mp` and both models were inspected in
  the loaded H1 process. Each conversion resolves the definition index by
  name through native configstrings starting at 2232 and validates its type.
  It also registers that definition in the client vehicle table through H1's
  own configstring-update handler (`0x1405FD1F0`). Converted playback checks
  the binding immediately before native helicopter rendering and restores
  it after a map bootstrap or restart if necessary. Ordinary gameplay keeps
  the original renderer path.
- `CG_ResetVehicle` (`0x1405FD2C0`) takes the low three bits of state +168
  as the runtime vehicle slot and bits 3..8 as the definition index.
  The converter allocates up to eight simultaneous slots, preserves each
  visible vehicle's slot and reuses slots after disappearance. Over-capacity
  recordings fail explicitly rather than indexing outside H1's vehicle array.
- `CG_GetDObj` (`0x140117810`) selects H1's shared helicopter animation tree
  for entity type 12. Native pose code (`0x1405FD3D0`) drives the rotor
  controllers and reads the recorded `time2` input at native state +24.
  COD4's helicopter-stage integer is not copied into H1's vehicle flags.
- Position and angle samples are interpolated at archived camera times,
  using shortest-angle interpolation. Absence, model changes, teleports and
  long snapshot gaps stop interpolation. H1 still performs final render-frame
  interpolation and native vehicle movement/audio updates.
- The recording registers `mp_cobra_helicopter` dynamically with command
  `d 1352 mp_cobra_helicopter` at 80400. The converter applies these sound
  config updates in recorded order, resolves the exact native looping alias
  through configstrings 2328+index, and writes it to the native 9-bit loop
  field at +8. No COD4 sound ordinal is copied into H1 directly.
- Hidden part bits use the existing bone-name translation. Unmapped hidden
  bones and unavailable required models fail explicitly.

## Verification and limits

Run `tests/run-cod4-helicopter.cmd <demo paths...>`. Checks cover all 754
source helicopter states and camera samples, native field layout, slot
capacity/reuse, model identity, dynamic sound registration, angle wrap and
interpolation boundaries. The existing airstrike test also covers demo0011's
411 plane states, 60 gravity bomb states and three cluster launches.

This implements helicopter flight and its recorded loop sound. The source
sample has no firing or destruction events. Helicopter combat events, damage
stages, ownership/compass markers and their effects are not translated yet;
the conversion report exposes event-bearing helicopter states. Hind's asset
mapping is verified, but no recorded Hind flight fixture was supplied.

Visual playback, rotor appearance, sound mixing and pause/seek behavior need
in-game verification. The foreground game has not been controlled during
this work. No game assets or decompiled engine code are distributed.

## First-spawn crash correction

The 2026-10-02 14:08:11 crash dump identified a null vehicle definition at
`0x1405FD617` (`rcx=0`, attempted read at `0x570`). The conversion log selected
`cobra_mp` at definition index 34, and the pose code retained index 34 in
`rax`. The former asset-existence check did not establish that this slot in
the client vehicle table had been initialized for converted playback.

Registration now uses H1's native per-definition update before that table
can be dereferenced. Model/definition identity and index bounds are checked,
and a failed registration produces a controlled replay error. This applies
to existing helicopter conversions as well as newly converted files.
Regression coverage includes an empty slot 34, a stale default definition,
a cleared registry after restart, failed registration and an invalid index.
