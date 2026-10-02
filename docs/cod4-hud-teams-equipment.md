# Recorded teams and equipment in the native HUD

Converted demos require reconversion for these changes. Existing snapshot bytes
and initial gamestates do not gain the missing fields by updating the executable.

## Team membership

H1's player-client update follows `0x140330839` -> `0x14032F1B0` ->
`0x14032E880` and reads team from clientState `+8`. It writes current team to
characterInfo `+12`, preserving the previous team at `+16`. Native obituary
processing reads the latter and compares it against the POV player's current
team. The original `+96` mapping and the subsequently attempted `+76` mapping
both left the actual player team zero. The latter mistakenly followed the
adjacent AI-agent update (`0x14032E770`) rather than the player-client path.
Team values now come
from the recorded client state on every snapshot, including team changes.
The native engine remains responsible for name colours.

## Grenade slots

H1 playerState has independent offHand (`+904`), lethalWeapon (`+908`) and
tacticalWeapon (`+912`) handles. Previously only offHand was populated. The HUD
slots now select owned source grenades and resolve their native weapon handles.
Their counts use the same recorded keyed ammo records as the rest of inventory.
No grenade is granted merely to fill a HUD slot, and empty owned grenades retain
their icons with zero ammo.

Live inspection of the reconverted Crash replay found the native integer-watch
cache already holding 1 lethal and 3 tactical grenades despite blank counters.
MWR's `weaponinfohud.lua` defers those updates while `Game.GetPlayerHealth()` is
zero and retries its cached event on `playerstate_client_changed`. Converted
playback now defers the native notification (`0x14026E690`) until the render
frame has finished updating the displayed playerState, and requests one initial
refresh after bootstrap. Ordinary game notifications retain their native timing.
This is an event-order correction; no equipment widget is replaced. Its visual
effect still requires post-install playback verification.

COD4's weapon definitions in `iw_11.iwd` classify frag/short-frag/brick as frag,
smoke/concussion as smoke, and flash as flash. Its `PM_Weapon_CheckForOffHand`
maps playerState.offhandSecondary zero to smoke and nonzero to flash. That
selection is distinct from the offhandClass enum.

## Faction emblem

The client-only replay does not execute native `_teams::initscoreboard`.
Conversion resolves the recorded character's faction against the loaded native
`mp/factiontable.csv`, using its name, icon and colour columns (2, 5, 14–16).
Only existing nondefault MWR materials are selected. These dvars are stored in
systeminfo, configstring 1, which native `CL_SystemInfoChanged` (`0x140342ED0`)
applies on replay initialization. No replacement HUD widget is used.

The native `teamscoreshud.lua` selects its emblem from `ui_team_selected`, not
directly from characterInfo. That player omnvar is an integer with minimum -1
and maximum 3. Its wire value is team minus minimum: raw 1 selects team 0,
raw 2 selects Axis and raw 3 selects Allies. Each converted snapshot now stores
the recorded team using the loaded definition's minimum. Playback preserves it
instead of overwriting it with boolean 1. Live inspection after the player-team
fix confirmed character team 1 alongside the incorrect raw omnvar value 1.

## Validation

- Read-only live inspection found valid character models with all team fields
  zero before this correction.
- Native machine code establishes the client-state and character-info offsets.
- Native `_teams` script and faction table establish the emblem lookup.
- HUD test covers all four team values, inventory/secondary selection, the four
  factions, and the existing Strike score-popup and obituary fixtures.
- Release/x64 build and helicopter client-slot regression pass.
- Post-install reconversion and visual playback remain pending.
