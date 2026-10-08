# COD4 FOV behavior

Enable COD4-style weapon framing with:

```text
cod4_fov 1
cg_fov 80
cg_fovScale 1
```

`cg_fov` changes both the scene and weapon framing at hip fire. Increasing it makes the weapon appear farther away, as in COD4. At full ADS the weapon's zoom FOV takes over, so changing `cg_fov` between 65 and 80 leaves the aimed view consistent. `cg_fovScale` scales both hip and ADS FOV, as it does in COD4.

The toggle is saved, off by default, and applies to first-person weapons in multiplayer, devmap, and Theater playback. It needs no external assets or reconversion. Use `cod4_fov 0` to restore MWR's native weapon projection. Freecam/dolly editor FOV remains controlled by Theater.

With `cod4_snipers 1`, the existing late scene zoom and flat-scope cut remain in use. The visible 3D sniper model uses your configured hip FOV until the scope replaces it. `cod4_fov` changes projection; animations, bobbing, recoil and remastered model proportions remain controlled by their existing systems.

## Implementation and verification

COD4's [`CG_GetViewFov` and `CG_CalcViewValues`](https://github.com/callofduty4x/CoD4x_Server/blob/master/tools/cod4_dasm/src/_cg_view_mp.asm) blend `cg_fov` toward the weapon's `adsZoomFov`, then apply `cg_fovScale`. The projection uses a 4:3 reference FOV and expands horizontally with the display aspect ratio.

H1 already calculates that scene/ADS projection. Its additional `CG_GetViewmodelFov` compensation at RVA `0x1046F0` multiplies the scene FOV by `(baseFov / cg_fov) * (1 - zoom) + zoom`, keeping the hip weapon projection independent of `cg_fov`. The toggle bypasses this extra compensation through the existing, signature-checked sniper projection hook. It preserves native scene calculation, ADS timing, aspect handling and weapon offsets.

`cod4_fov_status` reports supported/active state, sample count, ADS fraction, scene FOV and weapon FOV from the native projection boundary. These values are reference FOV degrees, before widescreen expansion. `tests/run-cod4-fov.cmd` checks hip framing, constant full-ADS endpoints, scale, aspect ratios, flat-scope compatibility and fallback. Visual parity still requires comparison in-game; matching projection does not make different meshes identical.

The 2026-10-08 native check measured hip scene/weapon FOV of 65/65 and 80/80 in `devmap mp_backlot`. A paused converted R700 replay retained 15/15 at full ADS with either hip setting, and 16.875/16.875 with scale 1.125. With classic sniper mode enabled, its flat overlay replaced the gun while the 3D projection retained the configured 80-degree hip FOV. The Release build, projection tests and existing sniper/Theater regressions passed; user visual confirmation remains pending.

[Back to documentation](README.md) · [Classic snipers](cod4-snipers.md)
