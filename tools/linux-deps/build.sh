#!/usr/bin/env bash
# Build the Linux dependencies a release package carries instead of asking the user's system for them, from the
# same pinned sources as the Windows build (tools/win-deps/fetch.sh), into third_party/deps/<os>-<cpu>/
# (e.g. linux-x86_64, ignored by git). Nothing is installed into the system.
#
#   openal-soft    shared  libopenal.so.1  (LGPL: must stay a replaceable library beside the exe; its audio
#                                           backends - PipeWire, PulseAudio, ALSA - are opened at run time)
#   libjpeg-turbo  static  libjpeg.a       (IJG/BSD; distributions disagree on the soname: libjpeg.so.8 / .62)
#   FFmpeg         shared  libav*.so.N, libsw*.so.N (LGPL, tools/ffmpeg/build.sh)
#
# The root CMakeLists.txt takes this prefix by itself when it is there. A plain development build does not need
# this script: without it the system's OpenAL and libjpeg are linked. SDL3 is not built here: the release build
# compiles it statically from third_party/SDL3 (.github/workflows/build-linux.yml).
#
# Usage: tools/linux-deps/build.sh [--clean]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HOST="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
DEPS_DIR="$ROOT/third_party/deps"
SRC_DIR="${SH_WIN_DEPS_SRC:-$DEPS_DIR/src}"
BUILD_DIR="$DEPS_DIR/build/$HOST"
PREFIX="${SH_LINUX_DEPS_PREFIX:-$DEPS_DIR/$HOST}"
JOBS="${JOBS:-$(nproc)}"

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf "$BUILD_DIR" "$PREFIX"
fi

"$ROOT/tools/win-deps/fetch.sh" openal-soft libjpeg-turbo FFmpeg

build() {
    local name="$1"; shift
    echo "build: $name"
    cmake -G Ninja -S "$SRC_DIR/$name" -B "$BUILD_DIR/$name" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        "$@" >"$BUILD_DIR/$name.configure.log" 2>&1 \
        || { tail -n 40 "$BUILD_DIR/$name.configure.log"; exit 1; }
    cmake --build "$BUILD_DIR/$name" --parallel "$JOBS" >"$BUILD_DIR/$name.build.log" 2>&1 \
        || { tail -n 40 "$BUILD_DIR/$name.build.log"; exit 1; }
    cmake --install "$BUILD_DIR/$name" >"$BUILD_DIR/$name.install.log" 2>&1 \
        || { tail -n 40 "$BUILD_DIR/$name.install.log"; exit 1; }
}

mkdir -p "$BUILD_DIR" "$PREFIX"

# The C++ runtime goes into the library, so it asks the user's system for nothing newer than its C library; the
# run path $ORIGIN lets it find what the package puts beside it (libatomic).
build openal-soft \
    -DLIBTYPE=SHARED \
    -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF \
    -DALSOFT_INSTALL_CONFIG=OFF -DALSOFT_INSTALL_HRTF_DATA=OFF \
    -DALSOFT_INSTALL_AMBDEC_PRESETS=OFF -DALSOFT_INSTALL_EXAMPLES=OFF \
    -DALSOFT_INSTALL_UTILS=OFF -DALSOFT_UPDATE_BUILD_VERSION=OFF \
    -DALSOFT_STATIC_LIBGCC=ON -DALSOFT_STATIC_STDCXX=ON \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON "-DCMAKE_INSTALL_RPATH=\$ORIGIN"

build libjpeg-turbo \
    -DENABLE_SHARED=OFF -DENABLE_STATIC=ON \
    -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF

echo "build: FFmpeg"
SH_FFMPEG_PREFIX="$PREFIX" JOBS="$JOBS" "$ROOT/tools/ffmpeg/build.sh"

# The licenses travel with the binaries; the game build copies share/sh-licenses/* into <build>/licenses/.
LIC="$PREFIX/share/sh-licenses"
mkdir -p "$LIC"
cp "$SRC_DIR/openal-soft/COPYING"      "$LIC/LICENSE-openal-soft.txt"
cp "$SRC_DIR/libjpeg-turbo/LICENSE.md" "$LIC/LICENSE-libjpeg-turbo.md"
cp "$SRC_DIR/libjpeg-turbo/README.ijg" "$LIC/LICENSE-libjpeg-turbo-ijg.txt"

# The texture upscaler's ncnn and model (sh-upscale); writes its license texts into $LIC.
SH_LINUX_DEPS_PREFIX="$PREFIX" JOBS="$JOBS" "$ROOT/tools/ncnn/build.sh"

echo "build: installed into $PREFIX"
ls "$PREFIX/lib"
