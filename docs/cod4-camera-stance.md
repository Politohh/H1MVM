# Recorded crouch/prone camera height

The converter resamples COD4 position and angles at camera-archive timestamps.
It previously held `viewHeightCurrent` from the latest player snapshot instead.
In `cod4_stutter_crouch.dm_1` there are 11,630 archives and 927 player snapshots.
A stance-height change recorded across about 50 ms therefore reached native H1
in the last archive interval, often only 4 ms. Native interpolation could not
recover the missing intermediate heights.

`cod4::interpolate_view_height` interpolates the recorded height on the same
command-time axis as those archives. It preserves source sample values and
does not restart when crouch/prone flags or height targets reverse. Viewpoint
changes, movement-mode changes, teleport flags, large position jumps and gaps
over 250 ms break interpolation. It is stateless for pause and backward seek.
Preview and exported native player states use the same helper.

H1 PC `CG_InterpolatePlayerState` at `0x32BCC0` already interpolates player-state
height at `+0x13C` (`0x32BE74`); first-person view at `0x10BEE0` adds that height
to camera Z (`0x10BF9E..0x10BFB7`). No late render-camera correction or additional
stance easing is applied. Local prediction remains disabled for converted
playback, so H1 does not replay movement commands over the recorded stance.

Reference: [COD4 player-state interpolation](https://github.com/SwagSoftware/KisakCOD/blob/master/src/cgame/cg_predict.cpp).

`tests/run-cod4-view-height.cmd` covers rapid reversal, prone, track endpoints,
pause/rewind and discontinuities, plus the supplied fixture. Its largest
adjacent archive-height jump is reduced from 9.87879 to 1.22353 units; all
recorded player-sample heights remain exact. This is numerical evidence,
not visual acceptance. Existing `.dm_h1` files require reconversion.
