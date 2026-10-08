# Theater Model Swapper

Open a demo, choose **Editor → Model Swapper**, then follow the three columns:

1. **Select Player**: choose the recorded player. The list indicates whether they are in the current scene.
2. **Choose Appearance**: choose **Heads** or **Bodies**, filter by faction/class/set, and select a card. The browser starts with the **Default** set; cycle **Set** to browse additional collections. The browser shows four appearances per page. Use **Clear Filters** to browse all available appearances again.
3. **Apply to Player**: check the selected player and appearance, then **Apply Head** or **Apply Body**. Wait for the appearance to load before applying another selection.

**View Player in Scene** places the free camera near that player's head. Players outside the current scene cannot be focused. **Restore This Player** returns both parts to the recorded appearance. **Back to Editor** returns to the ordinary category list.

**Save for This Demo** stores selections in `<recording>.dm_h1.models.json` beside the recording. **Load Saved Swaps** validates and restores those selections; unavailable assets are rejected. The original recording and multiplayer profile are unchanged. Editor **Reset All** also restores all player appearances.

The catalog comes from this installation's native character tables. Models registered to a character transient fastfile can load on demand, so a player can use an SAS head/body on a map with another faction. Portraits use native materials when available; default models use a faction/class card instead. Native asset-budget checks run before loading. Acquired character zones remain held until playback ends because earlier DObjs and corpses can still use them.

This version changes third-person player heads and bodies during replay playback. It does not change weapon models, POV hands, profile unlocks, or the recorded demo data. Body/head combinations may retain appearance differences authored into the game's models.

## Implementation checks

`tests/test-native-theater.cmd` checks head/body composition and equipment/tag preservation, plus the Lua interaction harness's player identities, filtering, paging, Apply guards, save/restore, layout and return navigation. It also checks deferred mouse callbacks, reused card targets and bounded widget creation across repeated filters.

Developer console diagnostics use `demo_model dump <label>` to export native DObj models, attachment tags and rebuild counts under `demos/theater`. The explicit `-modelcheck` launch flag exercises selection, head/body loading, playback and seeking; ordinary launches do not run these checks.

The October 8 development build was checked in native H1 playback using a separate copy of the Warz recording: paused SAS head/body Apply, Restore, Save/Load, forward/backward seeks, alternate heads and extended playback. The paused model rebuilt once per changed selection and stayed unchanged during a 20-second hold. Native script-string allocation remained stable through that sequence. Build and interaction checks passed; final visual confirmation of the menu and authored head/body combinations remains with the user.

The browser filters its catalog in native memory and transfers only four visible cards into HKS. Filter and paging actions refresh existing widgets on the next Theater poll, preserving the active button through mouse release and preventing repeated clicks from exhausting the UI pool.

For a native browser regression check, start H1 with `-luicheck`, open and pause a replay, then run `lui_testfile <absolute path to tests/theater_models_lui_runtime.lua>` in the console. This opt-in test cycles 400 production filter/paging callbacks, verifies selection changes and a bounded widget tree, forces garbage collection, and observes a 15-second hold. It stops on the first failure and never applies or saves an appearance. Ordinary launches do not expose this command.
