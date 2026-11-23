#!/usr/bin/env bash
# Cross-build the Windows dependencies with mingw-w64 x86_64 and install them
# into third_party/deps/x86_64-w64-mingw32 (ignored by git). Sources come from
# tools/win-deps/fetch.sh, which this script runs first.
#
#   SDL3           shared  SDL3.dll        (Zlib)
#   openal-soft    shared  OpenAL32.dll    (LGPL: must stay a replaceable DLL)
#   libjpeg-turbo  static  libjpeg.a       (IJG/BSD)
#   FFmpeg         shared  av*-N.dll, sw*-N.dll (LGPL, tools/ffmpeg/build.sh)
#   Mesa llvmpipe  prebuilt software_gl/opengl32.dll (MIT, tools/win-deps/software_gl.sh)
#   ncnn           static  for sh-upscale, with the Real-ESRGAN model (BSD-3-Clause, tools/ncnn/build.sh)
#
# Usage: tools/win-deps/build.sh [--clean]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TRIPLE=x86_64-w64-mingw32
DEPS_DIR="$ROOT/third_party/deps"
SRC_DIR="${SH_WIN_DEPS_SRC:-$DEPS_DIR/src}"
BUILD_DIR="$DEPS_DIR/build/$TRIPLE"
PREFIX="${SH_WIN_DEPS_PREFIX:-$DEPS_DIR/$TRIPLE}"
TOOLCHAIN="$ROOT/cmake/toolchains/mingw-w64-x86_64.cmake"
JOBS="${JOBS:-$(nproc)}"

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf "$BUILD_DIR" "$PREFIX"
fi

"$ROOT/tools/win-deps/fetch.sh"

build() {
    local name="$1"; shift
    echo "build: $name"
    cmake -G Ninja -S "$SRC_DIR/$name" -B "$BUILD_DIR/$name" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DSH_WIN_DEPS_PREFIX="$PREFIX" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        "$@" >"$BUILD_DIR/$name.configure.log" 2>&1 \
        || { tail -n 40 "$BUILD_DIR/$name.configure.log"; exit 1; }
    cmake --build "$BUILD_DIR/$name" --parallel "$JOBS" >"$BUILD_DIR/$name.build.log" 2>&1 \
        || { tail -n 40 "$BUILD_DIR/$name.build.log"; exit 1; }
    cmake --install "$BUILD_DIR/$name" >"$BUILD_DIR/$name.install.log" 2>&1 \
        || { tail -n 40 "$BUILD_DIR/$name.install.log"; exit 1; }
}

mkdir -p "$BUILD_DIR" "$PREFIX"

# SDL3 has no SDLmain library (SDL_main.h is header-only and the game does not
# use it: the exe enters at mainCRTStartup and calls SDL_SetMainReady). The GPU
# API (D3D12, Vulkan) is part of SDL3.dll.
build SDL3 \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF

build openal-soft \
    -DLIBTYPE=SHARED \
    -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF \
    -DALSOFT_INSTALL_CONFIG=OFF -DALSOFT_INSTALL_HRTF_DATA=OFF \
    -DALSOFT_INSTALL_AMBDEC_PRESETS=OFF -DALSOFT_INSTALL_EXAMPLES=OFF \
    -DALSOFT_INSTALL_UTILS=OFF -DALSOFT_UPDATE_BUILD_VERSION=OFF \
    -DALSOFT_STATIC_LIBGCC=ON -DALSOFT_STATIC_STDCXX=ON -DALSOFT_STATIC_WINPTHREAD=ON

build libjpeg-turbo \
    -DENABLE_SHARED=OFF -DENABLE_STATIC=ON \
    -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF \
    -DCMAKE_ASM_NASM_COMPILER=nasm

# The licenses travel with the binaries (LGPL, Zlib and IJG require the
# notice); the game build copies them into <build>/licenses/.
LIC="$PREFIX/share/sh-licenses"
rm -rf "$LIC"; mkdir -p "$LIC"

# FFmpeg (LGPL, shared: avformat-62.dll ... swresample-6.dll) for mod mp4 FMVs;
# it writes its own license files into $LIC.
echo "build: FFmpeg"
SH_WIN_DEPS_PREFIX="$PREFIX" JOBS="$JOBS" "$ROOT/tools/ffmpeg/build.sh" --mingw
cp "$SRC_DIR/SDL3/LICENSE.txt"         "$LIC/LICENSE-SDL3.txt"
cp "$SRC_DIR/openal-soft/COPYING"      "$LIC/LICENSE-openal-soft.txt"
cp "$SRC_DIR/libjpeg-turbo/LICENSE.md" "$LIC/LICENSE-libjpeg-turbo.md"
cp "$SRC_DIR/libjpeg-turbo/README.ijg" "$LIC/LICENSE-libjpeg-turbo-ijg.txt"

# Rendering on the CPU without an OpenGL 3.3 driver; writes its license texts into $LIC.
SH_WIN_DEPS_PREFIX="$PREFIX" "$ROOT/tools/win-deps/software_gl.sh"

# The texture upscaler's ncnn and model; writes its license texts into $LIC.
SH_WIN_DEPS_PREFIX="$PREFIX" JOBS="$JOBS" "$ROOT/tools/ncnn/build.sh" --mingw

echo "build: installed into $PREFIX"
ls "$PREFIX/bin"
