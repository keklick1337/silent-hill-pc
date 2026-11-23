#!/usr/bin/env bash
# Build a minimal LGPL FFmpeg (shared libraries) inside the project for mod FMV
# replacements in mp4. Sources come from
# tools/win-deps/fetch.sh (FFmpeg, pinned tag + commit) into
# third_party/deps/src/FFmpeg; nothing is installed into the system.
#
#   native (default)  third_party/deps/<os>-<cpu>/  (e.g. linux-x86_64): lib*.so.N
#   --mingw           third_party/deps/x86_64-w64-mingw32/ (or SH_WIN_DEPS_PREFIX): *-N.dll
#
# Only what a mod movie needs is enabled: the mov/mp4 demuxer, H.264/HEVC/AAC
# decoders and parsers, the file protocol, swscale and swresample. No GPL or
# nonfree parts, no programs, network, devices, filters, hardware or external
# libraries. The libraries stay shared (LGPL: the user can replace them); the
# game loads them at run time from the exe's directory (src/media/fmv/ffmpeg_library.cpp).
#
# Usage: tools/ffmpeg/build.sh [--mingw] [--clean]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEPS_DIR="$ROOT/third_party/deps"
SRC_DIR="${SH_WIN_DEPS_SRC:-$DEPS_DIR/src}"
JOBS="${JOBS:-4}"
MINGW=0
CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --mingw) MINGW=1 ;;
        --clean) CLEAN=1 ;;
        *) echo "usage: $0 [--mingw] [--clean]" >&2; exit 2 ;;
    esac
done

if [[ $MINGW -eq 1 ]]; then
    TRIPLE=x86_64-w64-mingw32
    PREFIX="${SH_WIN_DEPS_PREFIX:-$DEPS_DIR/$TRIPLE}"
    BUILD_DIR="$DEPS_DIR/build/$TRIPLE/FFmpeg"
    TARGET_FLAGS=(--enable-cross-compile --target-os=mingw32 --arch=x86_64
                  "--cross-prefix=$TRIPLE-" --extra-ldflags=-static-libgcc)
else
    HOST="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
    PREFIX="${SH_FFMPEG_PREFIX:-$DEPS_DIR/$HOST}"
    BUILD_DIR="$DEPS_DIR/build/$HOST/FFmpeg"
    # The libraries need each other (avformat asks for avcodec and avutil): the run path $ORIGIN makes each find
    # the others beside itself, wherever the game's folder is. The option reaches the linker in a response file:
    # a dollar sign does not survive configure, make and the shell on the command line.
    ORIGIN_FLAGS="$BUILD_DIR/origin.ldflags"
    TARGET_FLAGS=("--extra-ldsoflags=-Wl,@$ORIGIN_FLAGS")
fi

[[ $CLEAN -eq 1 ]] && rm -rf "$BUILD_DIR"

"$ROOT/tools/win-deps/fetch.sh" FFmpeg

# --disable-everything turns off every component; the libraries themselves stay
# on except the ones disabled by name.
CONFIG_FLAGS=(
    "--prefix=$PREFIX"
    --enable-shared --disable-static --enable-pic
    --disable-gpl --disable-nonfree --disable-version3
    --disable-programs --disable-doc --disable-network --disable-autodetect
    --disable-avdevice --disable-avfilter --disable-everything --disable-debug
    --enable-avformat --enable-avcodec --enable-swscale --enable-swresample
    --enable-protocol=file
    --enable-demuxer=mov
    --enable-decoder=h264,hevc,aac
    --enable-parser=h264,hevc,aac
)

mkdir -p "$BUILD_DIR" "$PREFIX"
[[ $MINGW -eq 1 ]] || printf '%s\n' '-rpath=$ORIGIN' >"$ORIGIN_FLAGS"
echo "ffmpeg: configure ($BUILD_DIR)"
(cd "$BUILD_DIR" && "$SRC_DIR/FFmpeg/configure" "${CONFIG_FLAGS[@]}" "${TARGET_FLAGS[@]}") \
    >"$BUILD_DIR/configure.log" 2>&1 || { tail -n 40 "$BUILD_DIR/configure.log"; exit 1; }
echo "ffmpeg: build (-j$JOBS)"
make -C "$BUILD_DIR" -j"$JOBS" >"$BUILD_DIR/build.log" 2>&1 \
    || { tail -n 40 "$BUILD_DIR/build.log"; exit 1; }
make -C "$BUILD_DIR" install >"$BUILD_DIR/install.log" 2>&1 \
    || { tail -n 40 "$BUILD_DIR/install.log"; exit 1; }

# The license travels with the libraries; the game build copies
# share/sh-licenses/* into <build>/licenses/.
mkdir -p "$PREFIX/share/sh-licenses"
cp "$SRC_DIR/FFmpeg/COPYING.LGPLv2.1" "$PREFIX/share/sh-licenses/LICENSE-FFmpeg.txt"
cp "$SRC_DIR/FFmpeg/LICENSE.md"       "$PREFIX/share/sh-licenses/LICENSE-FFmpeg.md"

echo "ffmpeg: installed into $PREFIX"
