# CoD4 facial animation prototype

The prototype connects original CoD4 facial curves to native H1 lobby
head rigs. Facial heads are enabled by default for new CoD4 conversions and
`cod4cam`; use `cod4_facialHeads 0` to disable them. It remains experimental.
Runtime expression
quality is still under validation; source/build tests do not prove visual parity.

## Use

`cod4_facialHeads` defaults to 1. Its choice is captured once per conversion.
Existing converted files without facial data need to be
reconverted to acquire facial curves; substituting a head alone is insufficient.
Use `cod4_facial_status` to inspect the active session and applied/rejected bones.

Conversion inputs belong in `cod4_facial/xanim` and `cod4_facial/xmodelparts`
beneath the game directory. `tools/install-cod4-facial-source.ps1 -RawDirectory
<Mod-Tools-raw-directory>` copies supplied local assets with backup and hash
checks. Authored assets are not included in this repository. To prepare the full
stock input set, run `python tools/prepare-cod4-facial-source.py --output
F:/Dev/h1-facial-source`, then install its `raw` directory. The preparation tool
checks every original Git blob against Mod Tools commit `95c76d6` and writes a
SHA-256 provenance manifest. It supplies all 244 unique compiled clips referenced
by the stock 318-entry animation table and 42 exact first-LOD skeleton assets for
49 head/body model identities. The local install also retains the earlier ADS
pistol fixture, for 245 XAnim files in total.

Saved facial conversions use the v4 marker and bounded facial-data records.
They contain only facial curves, source rest poses and recorded actor intervals;
playback does not need the source demo or the Mod Tools directory. Ordinary
conversions retain v3 and their existing behavior.
Existing v4 facial conversions also work with the bind-offset and regional corrections below;
they do not need to be converted again for those math corrections. Reconvert
after expanding the source input set: absent curves cannot be recovered from
an older saved conversion.

## Native layer

Stock head substitution uses column 6 of H1's `mp/costumemodeltable.csv`,
preserving patched identities. Bodies, hands and weapons retain existing native
mappings. The local animation boundary at MP RVA `0x5CFBA0` is authenticated
against this executable before installing its detour. A different prologue
safely disables the option rather than interrupting startup.

The layer runs after native local animation evaluation and before hierarchy
composition. It writes requested, newly evaluated facial controls only, skips
native control/world-control bones, and preserves body, weapon and ragdoll
processing. Native translations are animation offsets, calibrated against the
model's rest translations and explicit translation scale. It never writes
absolute head positions or the rendered world skeleton.

CoD4 translation keys are offsets from the source head's bind translations.
The sampler adds that bind translation before composing the source hierarchy.
Replacing bind translations with the raw keys collapsed facial controls toward
their parents and produced severe mouth/cheek deformation in the first runtime
test. This correction preserves the native mesh's existing skin weights.

Worker threads read an immutable published session. Each worker owns its rest
calibration cache, scoped to that session. Model/pose pointers are not retained
across sessions. Stop/disconnect clears the published facial data. Exact native
character/corpse handles select the recorded source-head rest pose.

Jaw, eyes, eyelids, brows, cheeks, levators, lips, mouth corners and chin have
explicit anatomical correspondences. Their visual weighting still requires
close-up acceptance. A source head's absent controls are omitted. In particular,
the masked Nomex source has 14 facial controls rather than the other heads' 20.
The exact XModel references now cover all 30 stock character definitions:
19 separate head identities plus nine complete-body identities with facial
controls (28 source model bindings with faces). SAS CT and selected USMC models
embed the face skeleton in the body rather than a separate head; their actual
recorded body rig supplies the rest transforms. Masked source rigs drive only
controls they possess. Patched native lobby head names are accepted too.
Missing original compiled animation inputs now stop conversion with the asset
name, instead of silently substituting neutral. Root/unused, engine SAW aim
poses and clips with no authored face tracks contribute no invented curves.

## Expression calibration

Lower lip/chin motion is sampled relative to the animated source jaw, then
inherited through the destination jaw. This uses H1's actual pivot and lip
distances; source jaw motion is not copied into the lips a second time.
Translation proportions are measured separately for brows, cheeks, lips and
mouth corners. Jaw/eye/lid translations retain the eye-spacing scale.

The inspected H1 rigs expose centre lip and supporting mouth, brow and cheek
controls. The prototype now drives 48 native controls for an unmasked source,
or 24 for Nomex. Symmetric lip centres average the two source sides. Lower lip
centres inherit the destination jaw; teeth and tongue retain their existing
local poses and follow it naturally. Eyelid child controls also inherit once.

The following live float dvars range from 0 to 1. They attenuate each region's
own rotation and translation. Zero lips still allows inherited jaw motion.
These defaults are an initial artistic calibration, not measured visual parity.

| Dvar | Default |
| --- | ---: |
| `cod4_facialJaw` | 0.25 |
| `cod4_facialEyes` | 1.00 |
| `cod4_facialEyelids` | 0.55 |
| `cod4_facialBrows` | 0.35 |
| `cod4_facialCheeks` | 0.25 |
| `cod4_facialLips` | 0.20 |

Workers read an immutable strength snapshot published from the main thread.
Changing strengths needs neither reconversion nor a new calibration cache.
`cod4_facial_status` also prints the current six strengths.
The first regional preset (jaw 0.70, lips 0.55) was viewed on the assault head
at 00:13 of the firing sample and remained too strong. The lower defaults above
were selected from a live neutral/reduced comparison at the same paused frame.
The gentler result was observed on that assault head; acceptance across other
characters and death poses is still pending.

## Timing

The original legs/torso clip identity and restart bit select facial clips.
Death uses the recorded legs channel; upper-body expressions use the torso
channel. When that channel has no facial tracks, the recorded full-body/idle
legs clip supplies its authored face, rather than replacing it with neutral.
CoD4 omits the recorder from packet entities, so its animation comes from player
state. Player/corpse intervals retain source identity and reset across removal,
restart and slot reuse. A corpse can inherit an already observed player death
phase. A death already underway at the recording's first frame has no recoverable
pre-recording start time; this prototype begins its curve at that first observation.

Sampling uses native demo server time, with sparse-key interpolation, loop/end
clamps and quaternion hemisphere handling. It is stateless through pause,
rewind, forward seeks and speed changes. Animation transitions are reconstructed
at recorded snapshot precision; exact CoD4 blend weights are not stored.

Stopping a verified shooting expression now freezes its last recorded pose and
blends into idle with a smooth release over `cod4_facialBlendTime` seconds
(default 0.20, range 0 to 1). This is an authored playback transition, not a
reconstruction of CoD4's original blend weights. Setting it to 0 restores an
immediate switch. A new shooting/death clip takes priority immediately.
The fade uses demo server time, so it pauses, seeks and scales with playback.

New conversions preserve neutral intervals where a visible actor has no supplied
facial clip. These store presence and source identity, not invented idle curves.
Older facial conversions can use a bounded gap release when the recorded identity
ahead matches. Reconvert for explicit idle-presence boundaries. Terminal removal
and a different client's reuse never receive that gap tail.

## Evidence and checks

Before the expression layer, both supplied recordings loaded on Backlot with
lobby heads. Five native heads have 89 bones each. Eighteen sampled player/corpse
objects stayed below the 254-bone limit; the highest observed count was 210.
The assault head showed no obvious neck gap in the observed view. These checks
do not establish every weapon combination, LOD or material.

The decoder consumes actual Mod Tools XAnim v17, discarding body/root curves.
Its format was cross-checked against the pinned OpenAssetTools loader. The full
244-clip stock set decodes, including legitimate zero-length single-frame
poses. Standing, crouching and prone automatic-fire clips retain 20 facial
tracks, with 20, 18 and 20 changing tracks respectively. The rest reader consumes
all 42 prepared v25 XModelParts. Native bind reconstruction agrees with H1's captured
bind positions within 0.000002 units on all five heads. Jaw/eye/lid fixture
retarget checks cover the full locally supplied clip set, the actual runtime
mappings, finite transforms and neutral preservation across all five captured
native rigs. All 28 source facial model bindings also produce non-neutral
automatic-fire, idle and death poses on those five destination rig captures.
Regression checks also cover keyed zero offsets and the actual `pt_stand_shoot`
jaw curve on each source head, ensuring sampled local positions retain their
bind translations. Visual acceptance of the corrected expressions is pending.
Regional regression cases check that lower lips preserve the destination jaw's
lever arm, jaw attenuation is inherited only once, symmetric centre lips do not
drift sideways, disabled strengths preserve the bind pose, and repeated paused
evaluations produce identical output.

The saved-session suite checks bounded input, truncated/trailing data, actor
removal, restart/reuse and deterministic seeks. Actual recording results:

- `cod4_facemovements.dm_1`: one source rig, six clips, one actor and 41
  intervals; two release boundaries, 87,375 bytes of facial data.
- `explosion_deagle.1.dm_1`: five source rigs, 86 clips, eighteen actors and
  782 intervals, including twenty death and 82 firing intervals; eighteen
  release boundaries, 795,523 bytes of facial data. Arab LMG client 3 and USMC
  LMG client 9 retain thirteen and fourteen standing automatic-fire intervals.

The previous 17-clip install omitted `pt_stand_shoot_auto` and its crouched/prone
variants, causing LMG firing states to become neutral even with a lobby head.
Regression checks now assert both LMG clients' saved curves, all 30 stock
character definitions, embedded-body faces, patched head recognition and
missing-input rejection. These are source/conversion/math checks; runtime
visual acceptance across every H1 class and map remains pending.

```text
tests\run-cod4-facial-models.cmd
tests\run-cod4-facial-animation.cmd <raw-xanim-files...>
tests\run-cod4-facial-retarget.cmd <raw-xmodelparts-files...>
tests\run-cod4-facial-native.cmd <capture.json> <raw-xmodelparts-directory> <raw-xanim-directory>
tests\run-cod4-facial-session.cmd <source.dm_1> <raw-directory>
```

Runtime acceptance still needs close-up firing/death comparisons, pause/seek,
speed and camera changes, corpse persistence and slot reuse. Do not describe
all characters or all expressions as verified until those checks pass.
