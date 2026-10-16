# SP Bots — experimental Private Match prototype

**Working WIP, development paused at Preview 19 (14 October 2026).**
This is an opt-in experiment for one human playing against bots in a local
Modern Warfare Remastered private match, then recording the action for cinematics.
It is not a finished replacement for MP bots, a complete campaign AI port, or an
online multiplayer feature. The user has played the prototypes successfully,
but animation quality, tactical decisions and stability still need work.

The source is on `main` so the experiment can be inspected and continued.
Normal client builds do **not** include this overlay. Build it explicitly using
the instructions below. The standard H1MVM v1.0 release does not include SP Bots. This source
checkpoint remains available for separate experimental builds.

## What this experiment actually does

It creates native MP agents using MP character bodies and heads, adapts selected
animations read from the installed campaign `common.ff`, and drives the agents
with original GSC behavior scripts. It does **not** run the entire SP executable,
campaign missions, SP navigation system or original campaign scripts inside MP.
Campaign-style behavior is being implemented around the MP engine's available
navigation, collision, damage, animation, weapons and snapshot systems.

The intended workflow is **Private Match → Game Setup → SP BOTS ON**, select
friendly/enemy counts and difficulty, start a match, play and record it. Joining
ongoing online games, co-op and public servers are outside the supported scope.

## Trying it

Requirements:

- Windows x64 and the MWR executable supported by this repository.
- Installed MP assets and the supported campaign `common.ff` in the game working
  directory. The reader checks its size and indexed compressed-block hashes;
  other asset revisions are not automatically supported.
- A separately built SP Bots preview EXE. Standard releases are not interchangeable.

1. Close H1. Keep your ordinary EXE and earlier preview for rollback.
2. From the preview output folder run:

   ```bat
   Launch-SP-Bots.cmd "C:\Games\Call of Duty Modern Warfare Remastered"
   ```

   Substitute your actual game directory. The launcher leaves the EXE in the
   preview folder and uses the installed game directory as its working directory.
3. Open **Private Match → Game Setup**. Start with **Team Deathmatch / Backlot**.
4. Set **Friendly Bots**, **Enemy Bots**, their difficulties and **SP BOTS: ON**.
   A useful first test is one friendly and three enemies.
5. Start the game and choose a team and class normally.
6. Use the existing demo recording and Theater workflow to inspect the result.

Equivalent launch flags are `-multiplayer -spai-probe -bundled-only +set sv_lanOnly 1`.
The legacy flag name `spai-probe` is still required for native registration.
`spai_bots 1` is the console toggle, but it does not replace the native lobby
start sequence and agent-pool reservation. Do not use old `devmap` prototype
instructions as the normal setup workflow.

**TDM is the current supported/tested menu path.** The menu intentionally omits
the toggle outside its supported host/mode conditions. Backlot is the main test
map; broad map and restart coverage is unfinished. The friendly/enemy counts
share a maximum of 17 agents. This is not a claim that every count combination
has been validated on every map.

## Current implementation

| Area | Present behavior | Important limit |
| --- | --- | --- |
| Private Match | Native menu toggle, team counts, difficulty settings, pre-map slot reservation and cleanup | Local TDM only; final map/mode/lifecycle matrix incomplete |
| Bodies | Native MP faction/class body and head selection with adapted SP poses | Broader body/weapon combinations and rendered rig quality need review |
| Movement | Walk, run, sprint, crouched and directional movement; native navigation; bounded traversal | Route quality, feet, speed and transitions can still look wrong |
| Perception | Visible-target acquisition, limited last-seen memory and gunshot awareness | Not campaign-grade intelligence; decisions can look slow or repetitive |
| Combat | Aim, fire, reload, damage, scoring, death and respawn | Current rifle path centers on the M16; this is not a complete weapon/loadout catalog |
| Cover | Cover selection, approach, hidden/peek behavior and cancellation | Consistently convincing cover use is not accepted |
| Grenades | Nearby-danger response, escape selection, fuse priority and animated throwback when a checked return is available | Throw arc checks primarily predict first impact; bounce/friendly safety and natural combined-build interactions need more validation |
| Reactions | Standing, running, crouched and left/right cover reactions; native stumble travel until obstruction | Natural distribution and rendered transitions need user review |
| Deaths | Five ordinary standing choices, plus headshot, crouched and two running contexts; corpse then ragdoll | Contexts deliberately restrict choices; not the whole SP death library |
| Presentation | Weapon model/kit setup, native shot effects, receiving-client rigs and score rows | Grip, muzzle appearance and exact flash/event parity remain open visual checks |
| Theater | Versioned recording, actor/corpse animations, aim, scores, weapon effects and rewind | Native checks passed; exact per-frame rendered live/replay parity is unproven |

## Preview 19 changes

Earlier builds selected one stumble for nearly every damage reaction. If the
whole predicted path was obstructed, they disabled all translation, producing a
stationary stumble. Preview 19 adds seven contextual reaction clips and checks
upcoming root-motion segments, allowing clear travel before an obstacle stops it.
Standing/running reaction pairs avoid immediate repeats. Physics and collision
still own movement; production behavior does not teleport the human or use noclip.

Ordinary standing deaths previously selected only two clips. Three additional
clips bring that pool to five. Headshot, crouched and running deaths retain their
own contexts; immediately repeating a standing/running choice is avoided.

The current animation schema is **8**, with **34 states**, score wire **9** and
capability **10**. State indices from older recordings are preserved; schemas 6/7
still prepare 24 states. New recordings contain `H1SPAI:8` and require a compatible
newer preview. Older supported schemas are parsed explicitly rather than guessed.

## What has actually been checked

These are bounded automated observations, **not full visual or gameplay approval**.
The compact [validation record](validation.json) includes hashes and exact scope.

- Ten appended clips loaded and advanced on three actors with two MP faction
  rigs. Native animation time and finite server bone samples were checked.
- Contextual reaction selection covered standing/running nonrepeat, crouch and
  both cover sides. A controlled stumble moved 273.3 game units in open space
  and 161.0 before obstruction, with collision safety and recovery checks.
- One hundred standing death selections reached all five choices without an
  immediate repeat. Three added deaths advanced on native corpses, entered
  ragdoll and produced exact kill/death/respawn increments.
- A 60-second ordinary local combat section was recorded, followed by controlled
  reaction and death fixtures. The demo ended cleanly and existing recordings
  were preserved. This was a mixed recording, not 109 seconds of natural combat.
- Fresh schema8 Theater playback, rewind to five seconds and replay to the end
  retained the new stumble/deaths, recorded score updates and muzzle events.
- An older schema7 demo retained its24-state trees, aim controllers, scores and
  muzzle events through forward playback and rewind on the new native build.
- Historical focused checks cover grenade release/cancellation, short fuses,
  grenade priority, native score accounting and other lifecycle cases. They are
  not a substitute for repeating interactions on the final combined build.

The source checkpoint is derived from the frozen Preview19 manual source. The
repository port changes diagnostic output paths and build/package plumbing;
it does not change gameplay logic. Copied C++ trailing spaces were also removed. Rebuilding is not byte-identical to the
historical EXE because of build metadata, paths and these diagnostic changes.
See [provenance.json](provenance.json). Historical runtime evidence applies to
the identified preview binaries, not automatically to every future rebuild.

## Known problems and unfinished work

- Waist, rifle grip, feet and some poses have looked wrong during user testing.
  Several fixes landed, but there is no final visual sign-off for all actions.
- User reports included bots feeling dumb/slow, repetitive walking and deaths,
  stationary stumbles and missing-looking muzzle flashes. Later previews added
  gaits, pacing, effects and reactions; those changes are not blanket acceptance.
- Cover/combat decisions still need substantial tuning in real matches.
- The full SP animation set is **not** ported. Thirty-four runtime states are
  a selected set, not every campaign behavior, gesture or weapon animation.
- Natural grenade/damage interactions, bounce outcomes, action interruption and
  all difficulty/count/map/restart combinations are not fully validated.
- A crash in an older preview was traced to native cover-node ownership. The
  unsafe native claim call was removed in favor of scripted spatial claims;
  subsequent bounded cover tests passed. This does not establish crash-free play.
- Earlier prototypes had a bad test teleport and a broken private-match loading
  transition. Those were repaired; do not reuse old fixtures or launchers.
- A nonfatal Theater UI warning and an earlier extra muzzle-event observation
  remain unresolved. Rendered playback quality still needs review.
- SP asset format/executable offsets are version-specific. Unsupported assets
  must fail validation rather than be silently treated as compatible.

Development is paused at the user's request. The feature is **working WIP**,
not complete, production-ready or declared flawless.

## Build a separate single-EXE preview

1. Follow [CONTRIBUTING](../../CONTRIBUTING.md), initialize submodules, generate
   the Visual Studio solution and build normal **Release/x64** dependencies.
2. Configure the complete embedded runtime bundle as described in
   [Single-file Theater client](../../docs/single-file-release.md). Those pinned
   inputs are required by the base client; the SP Bots source does not ship retail
   campaign animations or game assets.
3. Ensure `AURORAH1_GAME_PATH` is unset when generating/building so no post-build
   auto-install can replace your normal client.
4. From the repository root, with Python 3 and Visual Studio 2022 C++ tools installed:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File experiments/sp-bots/build.ps1
   ```

5. Output is `build/sp-bots-preview/h1-mod.exe`, a launcher and package manifest.
   Existing packages are not overwritten. Use `-PackageDirectory build/sp-bots-preview-2`
   for another build. Native intermediates stay in `build/sp-bots/`.

The build imports [spai.props](spai.props) to replace selected native components
for this experimental EXE only. It joins the three production GSC parts and
embeds them, the menu and scoreboard into a copy of the base resource bundle.
Packaging reads back and hashes every entry and preserves unrelated entries.
No research test sequence, forced team/class selection or automated host movement
is included in the manual package.

Runtime needs the EXE plus the installed game. It does not need Python, source
files, a development checkout or loose exported SP assets. The standard verified
bundle cache, demos, settings and optional diagnostics are output files, not
external development dependencies. The launcher creates `spai-diagnostics/` in
the game working directory for logs; these can grow during long sessions.

The generated campaign index is checked in and contains block locations/hashes,
not animation payload. Extending it requires a separate asset-format investigation
against a supported local game installation. Never commit the game's `common.ff`,
extracted animation bytes, crash dumps or other retail payloads.

## Code map and continuation notes

| Files | Responsibility |
| --- | --- |
| `spai_entry.gsc` | Opt-in entry and replay/live separation |
| `spai_match.gsc` | Characters, native agent allocation, slots, spawning and lifecycle |
| `spai_behavior.gsc` | Perception, paths, combat, cover, damage, death and grenades |
| `spai_menu.lua`, `spai_scoreboard.lua` | Native private-lobby controls and score presentation |
| `spai_component.cpp` | Version-specific native hooks, GSC bindings and diagnostics |
| `campaign_reader.hpp`, `campaign_index.hpp` | Bounded installed campaign animation decoding |
| `mp_rig_adapter.hpp`, `spai_rig_variants.hpp`, `spai_character_assets.hpp` | MP skeleton adaptation and character assets |
| `animation_schema.hpp`, `spai_remote.hpp`, `spai_recording.hpp`, `spai_replay.hpp` | Stable state IDs, local snapshot transport and recorded playback |
| `spai_aim.hpp`, `aim_controller_probe.hpp` | Observed-target aim and optional measurement |
| `spai_scores.hpp`, `spai_score_transport.hpp`, `spai_score_client.hpp` | Score accounting and validated receiving-client data |
| `*_research.cpp` | Experimental replacements for base client components, pinned to this checkpoint |

Do not blindly replace these copied components with newer base versions. Review
their differences when the main client changes. Never renumber historical state
indices; add a recording schema for incompatible animation changes. Keep live
population disabled during replay, preserve native collision, reset ownership
and temporary state on death/cancellation, and validate both real receiving-client
animation and scoring rather than accepting server-only counters.

`animation_schema_test.cpp` is a standalone C++20 assertion test; compile it
with assertions enabled from a Visual Studio developer prompt. It tests schemas,
wire acceptance and capability identity. It does not test game rendering.

Next work should start with a human Preview19 reaction/death review, then tackle
convincing combat/cover and wider body/weapon coverage, followed by combined
grenade/action tests and a final supported-map lifecycle matrix. Keep evidence
tiers explicit: compilation, native runtime, recording/replay and rendered
gameplay acceptance are different things.

## Reporting a problem

Include the exact preview/build, map and mode, friendly/enemy counts, difficulty,
whether this is live play or Theater, the action before the issue, and a short
video or screenshots of the pose/behavior. For crashes, keep the timestamp and
exception address plus local diagnostics; do not upload game assets. State
whether it reproduces with SP BOTS OFF. Keep the original demo when reporting
a replay problem, and note its schema/build and timestamp.

To stop using the experiment, close it and launch your ordinary client. No
installed EXE is replaced by the build or supplied launcher. Preserve newer SP
Bots demos for a compatible preview; an older client may not understand them.
