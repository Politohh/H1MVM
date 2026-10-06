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

The frontend no longer opens the old conversion overlay with F9. During playback,
F9 opens the native timeline; F10 retains the advanced camera/export controls.
F1, F3 and F4 keep their existing HUD, editor and controls functions.

Metadata is read on a background worker and cached by path and modification time.
H1 chunk headers are scanned without decompressing network packets; COD4 source
metadata uses the existing bounded decoder. Invalid/incomplete recordings remain
visible with an error and cannot be played or converted from the library.
Thumbnails use resident native map materials; unavailable thumbnails keep a
plain background and recording title. Existing recordings are never overwritten.

Verification: `tests/test-theater-library.cmd` checks metadata and malformed-file
handling; `tests/test-native-theater.cmd` checks selection, paging, filtering,
Convert/Play routing and existing editor/timeline behavior. Native rendering and
navigation also require an in-game check.
