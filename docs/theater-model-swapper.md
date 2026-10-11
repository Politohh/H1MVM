# Theater Model Swapper

Open a demo and choose **Editor > Model Swapper**.

1. Use **Player** at the top to choose the character. Selection frames the character when present at the current playhead.
2. Browse **Looks**: each named headgear collection appears once. Click a head, choose its **Team**, then choose its **Class** to start the preview. Head and team choices only navigate; they do not change the player. Back returns one step and remembers the head-list page. The uniform is the base body for that exact faction and class; these are composed looks, not additional authored full-body skins.
3. Choose **Keep changes**, or **Cancel** to restore the appearance from before previewing. You can cancel while assets load. Changing player, leaving the tab, closing the editor or ending playback also cancels an unkept preview.

**Customize** exposes head-only and body-only browsing. **Search** accepts appearance names, factions and classes. **All collections** opens collection choices. Eight cards fit per page. Hover never changes the character.

**Manage swaps** contains Save, Load and Restore Player. Save and Load require you to keep or cancel a preview first. Saved changes use `<recording>.dm_h1.models.json`; the original recording and multiplayer profile are unchanged. Editor Reset All restores every player.

The catalog and models come from the installed game's character assets. Available native portraits are shown; missing portraits do not display checkerboards. The live scene supplies the actual appearance preview. No external portrait pack or local development asset folder is required by the release EXE.

This changes replay third-person heads and bodies, not weapons, POV hands or multiplayer unlocks. The paired parts load together before appearing. Native asset-budget checks run before loading; acquired zones stay held until playback ends because earlier DObjs and corpses can still reference them.

## Implementation checks

`tests/test-native-theater.cmd` checks head/body composition and equipment/tag preservation, plus the Lua interaction harness's player identities, grouping, uniform pairing, search, paging, preview guards, save/restore, layout and return navigation. It also checks deferred mouse callbacks, reused card targets and bounded widget creation across repeated filters.

Developer console diagnostics use `demo_model dump <label>` to export native DObj models, attachment tags and rebuild counts under `demos/theater`. The explicit `-modelcheck` launch flag exercises selection, head/body loading, playback and seeking; ordinary launches do not run these checks.

The October 8 development build was checked in native H1 playback using a separate copy of the Warz recording: paused SAS head/body Apply, Restore, Save/Load, forward/backward seeks, alternate heads and extended playback. The paused model rebuilt once per changed selection and stayed unchanged during a 20-second hold. Native script-string allocation remained stable through that sequence. Build and interaction checks passed; final visual confirmation of the menu and authored head/body combinations remains with the user.

The browser filters its catalog in native memory and transfers only eight visible cards into HKS. Filter and paging actions refresh existing widgets on the next Theater poll, preserving the active button through mouse release and preventing repeated clicks from exhausting the UI pool.

For a native browser regression check, start H1 with `-luicheck`, open and pause a replay, then run `lui_testfile <absolute path to tests/theater_models_lui_runtime.lua>` in the console. This opt-in test cycles 400 production filter/paging callbacks, verifies selection changes and a bounded widget tree, forces garbage collection, and observes a 15-second hold. It stops on the first failure and never applies or saves an appearance. Ordinary launches do not expose this command.
