#!/bin/sh
# Windows x64 w64devkit + NASM. Sources and revisions are documented in
# docs/capture-ffmpeg.md. Run from the isolated build directory, never in-game.
set -eu
ROOT=$(pwd)
PREFIX="$ROOT/prefix"
JOBS=${JOBS:-8}
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"
mkdir -p "$PREFIX" dist/bin

case "${1:-all}" in
x264|all)
    cd "$ROOT/src/x264"
    "${BASH_EXE:-C:/Program Files/Git/bin/bash.exe}" ./configure --prefix="$PREFIX" --enable-static --disable-cli \
        --disable-opencl --bit-depth=8 --chroma-format=420 \
        --extra-cflags='-O3 -ffunction-sections -fdata-sections'
    make -j "$JOBS"
    make install
    cd "$ROOT"
    ;;
esac
case "${1:-all}" in
x265|all)
    cmake -S src/x265/source -B build/x265 -G 'Unix Makefiles' \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DENABLE_SHARED=OFF \
        -DENABLE_CLI=OFF -DENABLE_LIBNUMA=OFF -DSTATIC_LINK_CRT=ON \
        -DHIGH_BIT_DEPTH=OFF -DNATIVE_BUILD=OFF \
        '-DCMAKE_C_FLAGS=-ffunction-sections -fdata-sections' \
        '-DCMAKE_CXX_FLAGS=-ffunction-sections -fdata-sections'
    cmake --build build/x265 -j "$JOBS"
    cmake --install build/x265
    ;;
esac
case "${1:-all}" in
svt|all)
    cmake -S src/svt-av1 -B build/svt -G 'Unix Makefiles' \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_APPS=OFF -DBUILD_TESTING=OFF \
        -DNATIVE=OFF -DSVT_AV1_LTO=OFF \
        '-DCMAKE_C_FLAGS=-ffunction-sections -fdata-sections'
    cmake --build build/svt -j "$JOBS"
    cmake --install build/svt
    ;;
esac
case "${1:-all}" in
ffmpeg|all)
    cd "$ROOT/src/ffmpeg-9.0.2"
    ./configure --target-os=mingw32 --arch=x86_64 \
        --disable-autodetect --disable-everything --disable-network \
        --disable-doc --disable-debug --disable-ffplay --disable-ffprobe \
        --disable-avdevice --disable-shared --enable-static --enable-ffmpeg \
        --enable-gpl --enable-libx264 --enable-libx265 --enable-libsvtav1 \
        --enable-encoder=libx264,libx265,libsvtav1,prores \
        --enable-decoder=rawvideo --enable-demuxer=rawvideo \
        --enable-muxer=mov,mp4,matroska --enable-protocol=file,pipe,fd \
        --enable-filter=buffer,buffersink,scale,format,null,trim,vflip \
        --pkg-config-flags=--static \
        --extra-cflags="-O3 -I$PREFIX/include -ffunction-sections -fdata-sections" \
        --extra-ldflags="-static -L$PREFIX/lib -Wl,--gc-sections" \
        --extra-libs='-lstdc++'
    make V=1 -j "$JOBS" ffmpeg.exe
    cp ffmpeg.exe "$ROOT/dist/bin/ffmpeg.exe"
    ;;
esac
