# Mouse autofocus and freecam names

The DOF tab opens in **Basic**: Depth of Field, Mouse Autofocus, Focus Speed,
Focus Distance, Aperture (f-stop), and Bokeh. Numeric values remain visible
below sliders, with an explanation beside each control. **Advanced** holds
engine override modes, film diagonal, viewmodel settings and legacy near/far
blur controls. Both views use the same settings and complete-tab reset.

Autofocus
is continuous in freecam/dolly. Show the Theater cursor with F4 and move it over
the scene; hide the cursor to retain that screen position while moving the camera.
Focus continues behind the Theater editor. Console/chat, unrelated menus and
loss of game focus suspend sampling. The status line reports active focus distance
or why focus is paused. Focus Speed
runs from .001 to 1: higher values focus faster and 1 is instant. The default
is .2, a 20 percent transition per 60 Hz frame, normalized to elapsed time to
keep the same transition duration at other frame rates. Old presets containing
`demo_dof_response` are converted to equivalent speed values on load.
Manual Focus Distance turns autofocus off. Enabling DOF or adjusting the main
optical controls activates physical DOF automatically; disabling DOF also
disables autofocus, so it cannot immediately turn the effect back on.

The aperture sliders cover f/1 to f/22 instead of H1's debugging range of
f/0.125 to f/512. Manual scene focus uses a logarithmic slider for precision
at close distances, with a readout in metres (engine distances remain inches).
Readouts update immediately during slider dragging and arrow clicks.
**Cinematic Defaults** enables autofocus and physical DOF at f/2.8 with a
43.2 mm sensor diagonal, High-quality circular bokeh and the native 0.8
sharpness. It retains focus distance until a valid scene sample arrives.
The same resource guard gates this action; it never changes graphics quality
or recreates the video device automatically.

Graphics DOF must be enabled and its video settings applied. The existing resource
guard still gates the native override. Active autofocus enables physical DOF.
Turning autofocus off leaves the current
focus distance available for manual editing. Sky or invalid depth retains the
last valid distance. The original recording is unchanged.

This uses H1's physical DOF and scene depth. The interaction was informed by
[CinematicDOF's mouse-driven autofocus](https://github.com/FransBouma/OtisFX/blob/master/Shaders/CinematicDOF.fx);
no ReShade shader code or additional runtime is bundled.

Native MP evidence: physical DOF pass `0x92640`; RT 8 at `0x10AA5F00` is
`$floatz`, allocated at `0x6AAE23` as an R32_TYPELESS resource sampled as R32_FLOAT.
World CoC setup `0x934C0`
calls `0x96EB0` with depth scale 63/64, bias zero and direction one. Inverting
the zero-CoC condition gives `focusZ = view[0x148] * (63/64) / depth`.
`view[0x158]` is optical focal length, not a projection-matrix coefficient.
Sampling runs after the pass's command setup call at `0x9267B` has acquired H1's
immediate-context lock; it verifies that the pass owns the same context before
readback. Sampling at pass entry would precede that lock and race render workers.
Only this render hook accesses the three-slot 1-pixel staging queue, once per
native DOF pass, using nonblocking Map and no explicit Flush. Cursor sampling
and focus interpolation run each main frame with a high-resolution clock,
independent of the 50 ms menu refresh. GPU readback still adds a few frames of
latency; this is not the all-GPU ReShade autofocus implementation. Each sample keeps
its frame's depth coefficient and is discarded across session/activation changes.

Entering freecam suppresses `cg_DrawFriendlyNames` and `cg_DrawCrosshairNames`.
Their independently saved values are restored on exit or when playback ends.
The main pipeline handles restoration even if scene rendering has stopped.

Validation: `tests/theater_focus.cpp` checks depth conversion, sky/invalid
samples, response invariance across frame rates, and real D3D11 WARP pixel
readback. The opt-in `-mousefocuscheck` uses a disposable
`demos/theater_mouse_focus_check.dm_h1`, temporarily applies DOF quality,
checks focus while the native editor is open, applies cinematic defaults from
an intentionally excessive f-stop, samples two screen positions, verifies
the usable aperture range and names in/out of freecam, writes
`mouse-focus-check.json`, restores quality/names and quits. Never launch this
flag against an existing interactive H1 session. Visual blur/framing acceptance
remains separate from numerical/runtime checks.
