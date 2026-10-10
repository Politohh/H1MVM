# Theater DOF resource safety

Changing F-stop or focus previously enabled `r_dof_tweak = On` without checking
whether graphics DOF was enabled. H1's forced override bypasses its normal
resource-availability checks, producing `Tried to use 'dofTile0Sampler' when it
isn't valid` if DOF targets were omitted during video initialization.

The editor now requires both a positive `r_dof_limit` and active renderer flags
without the no-DOF bit before enabling the override. Parameters can still be
adjusted while unavailable, but do not activate rendering. Explicit override
changes and preset loading use the same guard. An unsafe existing override is
returned to Off during theater updates and after reset. The DOF page explains
that Depth of Field must be enabled in Graphics options and video settings applied.
Graphics preferences are not changed automatically.

Verified against H1 MP's unpacked renderer (addresses relative to image base):

- `0x687660` sets resource flag `0x20` when `r_dof_limit` is zero.
- `0x6AACE0` skips DOF target allocation when that flag is set.
- `0x6A9AF0` creates the DOF targets, including tile 0 and tile 1.
- `0x1C2B40` checks `r_dof_limit` and active flags at `0xEA74DE8` in normal
  mode, but bypasses those checks when `r_dof_tweak` is On.
- The native override enum is Off / On / Force Disable.

`tests/theater_dof.cpp` covers disabled quality, pending resource recreation,
available resources, override modes, and which controls enable the override.
`tests/theater_lui.lua` checks visible guidance and its removal when resources
become available. Runtime acceptance still requires repeating the original
F-stop/focus change in H1 with graphics DOF disabled and enabled.
