# Classic COD4 sniper playback

`cod4_snipers 1` uses original COD4 action animations on MWR weapons and hands during converted-demo playback and direct COD4 preview. It supports **M40A3, R700, Barrett, M21 and Dragunov** with standard scopes.

The mode is off by default. Regular live/devmap play and ACOG optics retain native behavior. Original input assets are prepared separately and are not bundled with the repository or release packages.

## Prepare the inputs

Use release **2026.10.07 or newer**, or a source build containing the sniper changes. Install Python and Pillow, then run from the repository root with the `main` directory of your COD4 installation:

```powershell
python -m pip install Pillow
python tools/prepare-cod4-sniper-source.py --full --cod4-main "C:\Games\Call of Duty 4\main" --output "C:\Assets\cod4-snipers"
```

The tool reads the original weapon definitions and scope from your installation and downloads animation/rest-rig inputs from a pinned [COD4 Mod Tools revision](https://github.com/cod4mw/CoD4-Mod-Tools/tree/95c76d6a470bea43fdd5ab591afe8475fac7699d). It verifies source hashes and creates an `animations.json` descriptor and `manifest.json`.

With H1 closed, copy the generated runtime inputs into **`<MWR game directory>\cod4_snipers\`**. Keep the `.weapon` definitions and manifest with your preparation output as provenance records. Runtime inputs are the `viewmodel_*` clips, `.rig` files, `animations.json` and `scope.png`.

If you use `tools/install-native-theater.ps1`, supply your preparation folder with `-SniperSourceDirectory`; it validates the manifest, backs up replaced files and verifies installed SHA-256 parity.

## Enable and customize

```text
cod4_snipers 1
cod4_snipers_status
```

Disable with `cod4_snipers 0`. To use only flat sniper scopes, disable the full mode and set `cod4_2dScopes 1`.

For COD4-style hip weapon framing, also enable [`cod4_fov 1`](cod4-fov.md). It couples weapon framing to `cg_fov` while preserving the late sniper zoom and flat-scope switch.

To customize the overlay, place a square RGBA PNG named **`scope_upscale.png`** in the preparation folder before using the installer, or pass `-ScopeImage "C:\Assets\my-scope.png"`. The installer copies the image verbatim as `cod4_snipers/scope.png`; transparency defines the visible lens area. Restart H1 after replacing inputs or the executable.

Existing converted demos work with these runtime changes. Reconversion is needed only when a converter update changes the recorded data.

## Playback behavior

- Original idle, firing, bolt, reload, draw, sprint and melee clips drive remastered weapon and hand rigs.
- Scene zoom occupies the final **5% of ADS**. Weapon projection retains its configured hip FOV during the transition.
- The flat scope replaces the 3D viewmodel just after **95% ADS** and cuts away immediately when leaving full ADS, without an opacity fade.
- Native optic anchors center the full-ADS endpoint. Receiver alignment preserves magazine contact; remastered bolt caps follow the original bolt control.
- The R700/M40A3 cartridge stays at its chambered idle pose during pull-out. Reload and bolt-cycle cartridge motion remain authored.
- Original hip/ADS recoil parameters are applied during the native frame. COD4 preview preserves the calculated camera rotation. The Barrett's original ADS firing slot is idle and relies on camera kick.
- Sprint loops begin after sprint-in, and recorded clocks/action timers drive playback, including pause, slow motion and seeks.
- Cancelling an unfinished weapon-lowering action back to forced idle blends the outgoing pose away over half a second of replay time, following COD4's return transition. Pause freezes the blend; seeks, unsupported actions and weapon changes clear it.

Native notifications and audio remain in use. Unsupported/offhand actions retain native animation. This mode does not promise exact visual parity across every remastered rig.

## Verification

`tests/run-cod4-snipers.cmd "<prepared input directory>"` covers original clips, action mappings, retargeting, mechanical pivots, magazine contact, cartridge draw continuity, recoil parameters, optical centering, sprint clocks and ADS/scope timing. Non-facial controls remain isolated from the facial layer.

Captured native M40A3 and Barrett rigs have also been checked offline. Existing R700, M21 and Dragunov captures were unloaded placeholders, so their complete native assemblies still need separate validation. The latest fixes improved playback in user testing; individual visual issues remain recording- and rig-specific.

[Back to documentation](README.md) · [Playback controls](native-theater.md)
