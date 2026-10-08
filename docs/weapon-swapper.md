# Theater Weapon Swapper

Open **Editor > Weapon Swapper** during a replay. The left column identifies the recorded gun and its class. Use the gun arrows to choose another firearm in that class, pick a kit, and select **Apply Weapon**. For example, select Neanderthal on the R700 or choose the S-Tac Aggressor from the sniper rifles.

The selection replaces that recorded weapon whenever it appears in first-person playback. Other recorded weapons, third-person world guns, ammunition, shots, and gameplay timing retain their original data. A kit for the same gun preserves its recorded attachment; a different gun uses its standard native setup and animations. Changing the gun does not simulate the replacement's fire rate or ballistics. Classic COD4 sniper animation overrides continue on same-gun kits; different guns use native animation rather than retargeting the source rifle's rig.

**Restore This Gun** removes the current gun's override; **Restore All Guns** clears this replay's selections. **Save for This Demo** writes `<recording>.weapons.json`. **Load Saved Swaps** validates weapon classes and installed catalog entries before accepting the sidecar, and loads each replacement when its recorded gun appears. The recording itself is never rewritten. Editor Reset All also clears weapon swaps.

The catalog reads `mp/furniturekits.csv` and its native per-kit model tables. Only installed firearm models in the recorded class are offered. Grenades, launchers, equipment and cross-class replacements are excluded. Models load asynchronously through native transient requests and share the extra-model budget with Model Swapper. Requests remain held until playback exits so an existing DObj cannot reference an unloaded model. Unavailable icons use a labeled tile instead of a checkerboard.

The render override authenticates `BG_GetViewmodelWeaponIndex` at H1 MP RVA `0x2E1270`, called by `CG_UpdateWeaponViewmodels` at `0x1285AB`. It substitutes the result only for the local render playerstate during armed playback. Recorded playerstates and inventory are not rewritten. Session changes discard packed-weapon caches; a seek restart of the same recording retains the selections by native weapon name.

Run `tests/test-native-theater.cmd` and `tests/run-cod4-snipers.cmd <prepared sniper source>` for policy, Lua navigation/reuse and sniper regressions. `demo_weapon_status` writes the current native DObj model names to `demos/theater/weapon-swapper-status.json`. Console equivalents are `demo_weapon apply <native weapon> <kit>`, `restore`, `reset`, `save`, and `load`.

For an explicit developer smoke test, copy a converted R700 recording to `demos/theater_weapon_check.dm_h1` with no preexisting weapon sidecar, then launch with `-weaponswapcheck`. The test pauses, applies R700 Neanderthal and S-Tac base, saves/restores/loads, seeks forward/backward, resumes and exits. Diagnostics are separate from visual acceptance of animation and framing.

## Validation on 2026-10-08

Release/x64 and the Theater and original-sniper regression suites passed. On a copy of `6oPuC_3k_r700_backlot.dm_h1`, H1 discovered 502 native weapon/kit choices and built the viewmodel DObj with `wpn_h1_sni_r700_ndl_vm_camo` and `attach_h1_scope_r700_ndl_vm_camo` for Neanderthal. Switching to S-Tac produced `wpn_h1_sni_tac330_vm_camo`, its scope and reticle. Save/load, backward seek and resumed playback retained the S-Tac; the forward seek landed on the recorded Desert Eagle and correctly displayed that pistol. Restore and Reset returned the original R700 rifle and scope. The copied recording still matched the original SHA-256 after the test. User visual acceptance remains pending.
