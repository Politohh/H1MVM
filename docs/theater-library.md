# Theater library

Open **Play Online → Theater**. The frontend shows nine recordings per page,
with the selected map preview, mode, file name, date and duration on the right.
Cards show the recording's filename without its demo extension in a smaller,
readable heading. Long titles are shortened with an ellipsis; the selected
recording's complete filename remains visible in the preview.

- **All Demos** includes native H1 recordings, converted H1 files and imported COD4 sources.
- **H1 Demos** lists native and converted `.dm_h1` recordings inside the game's `demos` directory. Select a recording and choose **Play Demo**.
- **COD4 Demos** lists `.dm_1` sources. Select a recording and choose **Convert Demo**. Conversion uses the existing map/bootstrap pipeline; its resulting `.dm_h1` appears in the H1 library after **Refresh**.
- **Import Demos** opens a Windows file chooser with multiple selection and copies recordings into `demos`, preserving the original files and using an unused name if one already exists.
- **Convert COD4 Demo** is always visible. Pick a `.dm_1` file; it is imported into `demos` and conversion starts. Converted files are saved directly in `demos`.

Only the game's `demos` folder and its subfolders are scanned. External folders,
the old import registry and symlink targets outside this folder are excluded.

Double-click a recording to play it or begin conversion, according to its type.
Theater stays open until playback starts. A blocked launch is reported in the
library status instead of silently returning to the online menu.
The frontend no longer opens the old conversion overlay with F9. During playback,
F9 switches between cursor controls and camera input; F10 retains the advanced camera/export controls.
F1, F3 and F4 keep their existing HUD, editor and controls functions.

During a demo, **Escape** opens the demo pause menu with playback actions,
recording details and native options. Returning to Theater reopens the library
after the frontend is ready.

Keep original COD4 recordings. Reconvert after updates that change converter
data; playback, UI and sniper animation corrections work with existing
converted demos. The adjacent `.conversion.txt` report lists omitted or
unsupported data.

## Split a recording

Select a converted `.dm_h1` replay or an original `.dm_1` recording and choose
**Split Demo** in its preview. Set **Start** and **End** with the sliders or
arrows, then choose **Save Clip** (converted replay) or **Convert and Save Clip**
(COD4 source). Arrows adjust by one second; the readouts show minutes, seconds
and milliseconds. The selected range is relative to the recording's beginning
and snaps inward to its recorded frames. It must contain at least two frames.

Clips are new `.dm_h1` files in `demos/clips`, with automatic unused names.
The original remains unchanged. Back returns to the selected recording and
filter; Refresh shows the saved clip in the library. You can split a clip again.
Splitting is unavailable while playback, conversion or another library job runs.

Converted replays are split on the library worker with progress feedback. They
keep the starting native gamestate, required assets and recorded metadata, while
only the selected independent snapshots and matching camera records are copied.
Packet/archive indices, duration and kill markers are rebuilt. Original server
timestamps are retained for animation, corpses, facial data, HUD and effects.
No reconversion is required for this path. Some shared metadata covers the full
recording, so output size does not shrink exactly in proportion to duration.

For original COD4 inputs, the existing converter loads the remastered map and
reconstructs the complete source history, then emits the selected frames as an
H1 clip. It returns to Theater after conversion. It does not write a COD4-playable
`.dm_1` cut. Native H1 match recordings with dependent snapshots are not currently
offered the Split button; the direct file splitter requires converted COD4 data.

`tests/run-demo-splitter.cmd "<converted .dm_h1>" "<original .dm_1>"` checks
range validation, malformed/dependent snapshots, cancellation, repeated cuts,
packet numbering, duration/kill markers and byte preservation. The reported
27-minute Crash replay's 10:00–10:05 cut retains 418 frames and spans 4,998 ms.
Original-source range selection is also checked against its real camera track.
Release compilation and these offline checks do not establish interactive menu
rendering or in-game acceptance of a saved clip.

Metadata is read on a background worker and cached by path and modification time.
Current H1 recordings use the metadata prefix and a bounded footer read, so
duration lookup does not walk every packet of a large recording. Older layouts
fall back to scanning chunk headers without decompressing network packets. COD4 source
metadata uses the existing bounded decoder. Invalid/incomplete recordings remain
visible with an error and cannot be played or converted from the library.
Thumbnails use resident native map materials; unavailable thumbnails keep a
plain background and recording title. Existing recordings are never overwritten.

Verification: `tests/test-theater-library.cmd` checks metadata and malformed-file
handling; `tests/test-native-theater.cmd` checks selection, paging, filtering,
Convert/Play routing and existing editor/timeline behavior. Native rendering and
navigation also require an in-game check.

[Back to documentation](README.md) · [Playback controls](native-theater.md) · [Classic snipers](cod4-snipers.md)
