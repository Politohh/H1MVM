# Personalization and local Depot

Open **Personalization → Character** for heads and **Personalization → Camo**
for body camouflage. These use MWR's native selectors, preview and equip flow.
Armory and Collections retain their native category/season tabs.

Local ownership is available in both the frontend and active matches. The
existing **Stats → Unlock All Loot** option unlocks catalog loot and updates
collection counts without writing every item into your owned inventory.
With that option off, locked items remain craftable using local Parts; zero
completed collections does not mean the selectors should be empty.

Crafting checks the price and balance, saves the item GUID and completes the
native waiting popup. Completed collections and seasons can claim their reward
once. Local supply drops and bundles use the installed game catalog, with
duplicate salvage and protection against repeated transaction events. They do
not reproduce official marketplace odds or provide a real-money store.

Ranked-match results award local Parts and Credits. Replays do not. Native
profile stats continue to store equipped head/camo selections.

Inventory, currency and Depot preferences are stored in the game's
`players2/user/depot.json`, or the active mod's stats subfolder. Saves replace
the file atomically and retain the preceding valid version as `depot.json.bak`.
Malformed existing files are preserved separately on recovery. A failed save
cancels the inventory transaction and retains the previous balance.

After updating, restart H1 so it loads the new Lua scripts. The executable and
`custom_depot` scripts must be from the same update. Startup and save diagnostics
appear in `h1-lui-errors.log`, prefixed with `[Depot]`.

Developer verification: `tests/test-depot.cmd` covers persistence/recovery,
frontend and match ownership, native selector preservation, crafting, collection
claims, deferred completion events, local drops and replay earnings exclusion.
These checks do not confirm the rendered preview, outfit fit or live-match visuals.
