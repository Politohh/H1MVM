# Theater capture-only FFmpeg

The bundled static Windows x64 FFmpeg contains the four encoders used by
`demo_avidemo.cpp`: libsvtav1, libx265, libx264 and native prores. Theater sends RGB24
frames through stdin. The build retains rawvideo input, scaling/pixel conversion,
vertical flip, and MP4, MOV and Matroska output. Audio, networking, players,
probes and unrelated codecs/formats are excluded. A valid user-selected external
FFmpeg still works as before.

H.264/H.265 are built for Theater's 8-bit YUV420 output; ProRes retains 10-bit
YUV444 input. Runtime SIMD dispatch remains enabled, without targeting the build
machine's CPU. All codec libraries and compiler runtimes are linked statically.

## Pinned sources and build

Create an isolated build directory with `src/` containing:

| Directory | Upstream | Revision |
|---|---|---|
| ffmpeg-9.0.2 | https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz | 9.0.2 |
| x264 | https://code.videolan.org/videolan/x264.git | b35605ace3ddf7c1a5d67a2eb553f034aef41d55 |
| x265 | https://bitbucket.org/multicoreware/x265_git.git | 1d117bed4747758b51bd2c124d738527e30392cb (4.1) |
| svt-av1 | https://gitlab.com/AOMediaCodec/SVT-AV1.git | 6dbe8506261ce72c8886c82ffed5bf1608ab12aa (v4.0.0) |

Use w64devkit 2.10.0 (GCC 16.2.0), NASM 2.16.03, CMake 3.31.10 and Git for
Windows Bash. Put CMake, w64devkit and NASM on PATH, in that order. CMake 4.4
cannot configure x265 4.1's old policies. From the isolated build directory run
`sh <repository>/tools/build-capture-ffmpeg.sh`. `JOBS` defaults to 8.
The optional stage argument is `x264`, `x265`, `svt` or `ffmpeg`.

The resulting encoder is `dist/bin/ffmpeg.exe`. Put FFmpeg's `COPYING.GPLv2` at
`dist/LICENSE`, distribution/build details at `dist/README.txt`, and dependency
licenses, patent notices, source hashes and the build script in `dist/licenses/`.
The runtime packer embeds those notices. Retain the complete corresponding source
and build recipe separately for release distribution; they are development
materials, not runtime dependencies.

`tools/package-capture-ffmpeg.py <build-root> <ffmpeg-source-archive>
<toolchain-notices-directory>` creates that distribution, checks the source pins,
and records hashes. The initial build used CMake 3.31.10 for x265 and 4.4.3 for
SVT-AV1; CMake 3.31.10 can be used for both in subsequent builds.

Configure the bundle's `ffmpeg` and `ffmpeg_sha256` to that verified binary. Run
`tests/check-capture-ffmpeg.py <encoder> <full-ffmpeg-bin> <output>` to test all four
encoders, every supported container, flip on/off, 320x180/30fps and 1080p/60fps.
It uses Theater's stdin command, a clean system-only PATH, independent full decode,
frame counts and orientation checks. Keep its JSON evidence with the test build.

## Scope PNG

Optimize a copy of the source with oxipng 10.2.1, without `--alpha` or other lossy
options. Compare full decoded RGBA including RGB under transparent pixels. Compile
`tests/scope-pixel-parity.cpp` with `deps/stb` on the include path to verify the
same decoder used by the client. Keep the original source. Compare the PNG's
**zlib-9 packed size** as well as disk size: a smaller PNG can produce a larger
runtime bundle. Only select the copy if it reduces the final packed size.
`tools/select-lossless-scope.py <original> <output> [candidates...]` verifies the
pixels and chooses by packed size. It also tests joining the original IDAT chunks,
which preserves the existing compressed pixel stream and all metadata.
