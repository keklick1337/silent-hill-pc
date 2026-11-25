#!/usr/bin/env bash
# Build the macOS dependencies a release package carries instead of asking the user's system (or Homebrew) for them,
# from the same pinned sources as the Linux and Windows builds (tools/win-deps/fetch.sh), into
# third_party/deps/darwin-<cpu>/ (e.g. darwin-arm64, ignored by git). Nothing is installed into the system.
#
#   libjpeg-turbo  static  libjpeg.a
#   opus           static  libopus.a (BSD-3-Clause, tools/opus/build.sh: Ogg Opus sound of mods)
#   FFmpeg         shared  libav*.N.dylib, libsw*.N.dylib (LGPL, tools/ffmpeg/build.sh; install names
#                          @loader_path/..., so they find each other beside the exe)
#   ncnn           static  for sh-upscale, with the Real-ESRGAN model (tools/ncnn/build.sh)
#   MoltenVK       shared  libMoltenVK.dylib, Vulkan over Metal for renderer = vulkan and sh-upscale. Not built: the
#                          Khronos release binary (universal, macOS 12+), pinned by version and SHA-256
#
# OpenAL and OpenGL are the system's frameworks. SDL3 is not built here: the release build compiles it statically
# from third_party/SDL3 (.github/workflows/build-macos.yml). The root CMakeLists.txt takes this prefix by itself
# when it is there. MACOSX_DEPLOYMENT_TARGET (default 13.3) is the oldest macOS the libraries run on; the game must
# be configured with the same CMAKE_OSX_DEPLOYMENT_TARGET.
#
# Usage: tools/macos-deps/build.sh [--clean]
set -euo pipefail

[[ "$(uname -s)" == Darwin ]] || { echo "macos-deps: this script builds on macOS" >&2; exit 2; }

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HOST="darwin-$(uname -m)"
DEPS_DIR="$ROOT/third_party/deps"
SRC_DIR="${SH_WIN_DEPS_SRC:-$DEPS_DIR/src}"
BUILD_DIR="$DEPS_DIR/build/$HOST"
PREFIX="${SH_MACOS_DEPS_PREFIX:-$DEPS_DIR/$HOST}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"
export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-13.3}"

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf "$BUILD_DIR" "$PREFIX"
fi

"$ROOT/tools/win-deps/fetch.sh" libjpeg-turbo FFmpeg

mkdir -p "$BUILD_DIR" "$PREFIX"

echo "build: libjpeg-turbo"
cmake -G Ninja -S "$SRC_DIR/libjpeg-turbo" -B "$BUILD_DIR/libjpeg-turbo" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
    -DENABLE_SHARED=OFF -DENABLE_STATIC=ON \
    -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF \
    >"$BUILD_DIR/libjpeg-turbo.configure.log" 2>&1 \
    || { tail -n 40 "$BUILD_DIR/libjpeg-turbo.configure.log"; exit 1; }
cmake --build "$BUILD_DIR/libjpeg-turbo" --parallel "$JOBS" >"$BUILD_DIR/libjpeg-turbo.build.log" 2>&1 \
    || { tail -n 40 "$BUILD_DIR/libjpeg-turbo.build.log"; exit 1; }
cmake --install "$BUILD_DIR/libjpeg-turbo" >"$BUILD_DIR/libjpeg-turbo.install.log" 2>&1 \
    || { tail -n 40 "$BUILD_DIR/libjpeg-turbo.install.log"; exit 1; }

JOBS="$JOBS" "$ROOT/tools/opus/build.sh" "$PREFIX" "$BUILD_DIR/opus" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET"

echo "build: FFmpeg"
SH_FFMPEG_PREFIX="$PREFIX" JOBS="$JOBS" "$ROOT/tools/ffmpeg/build.sh"

# The licenses travel with the binaries; the game build copies share/sh-licenses/* into <build>/licenses/.
LIC="$PREFIX/share/sh-licenses"
mkdir -p "$LIC"
cp "$SRC_DIR/libjpeg-turbo/LICENSE.md" "$LIC/LICENSE-libjpeg-turbo.md"
cp "$SRC_DIR/libjpeg-turbo/README.ijg" "$LIC/LICENSE-libjpeg-turbo-ijg.txt"

# MoltenVK: the release archive and the library in it, each checked by its hash.
MVK_VERSION=v1.4.2
MVK_URL="https://github.com/KhronosGroup/MoltenVK/releases/download/$MVK_VERSION/MoltenVK-macos.tar"
MVK_TAR_SHA256=f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e
MVK_DYLIB_SHA256=aef00b13bcc808adf15b85bef9ae67393d92be7ed5dfe41cad16fa809e4a4c5f
mvk_tar="$SRC_DIR/MoltenVK-$MVK_VERSION-macos.tar"
sha_ok() { [[ -f "$2" ]] && [[ "$(shasum -a 256 "$2" | cut -d' ' -f1)" == "$1" ]]; }
if ! sha_ok "$MVK_TAR_SHA256" "$mvk_tar"; then
    echo "fetch: MoltenVK $MVK_VERSION (Apache-2.0)"
    curl -fsSL -o "$mvk_tar.tmp" "$MVK_URL"
    sha_ok "$MVK_TAR_SHA256" "$mvk_tar.tmp" || { echo "macos-deps: MoltenVK archive hash mismatch" >&2; exit 1; }
    mv "$mvk_tar.tmp" "$mvk_tar"
fi
mvk_dir="$BUILD_DIR/MoltenVK"
rm -rf "$mvk_dir" && mkdir -p "$mvk_dir"
tar -xf "$mvk_tar" -C "$mvk_dir" MoltenVK/LICENSE MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib
sha_ok "$MVK_DYLIB_SHA256" "$mvk_dir/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib" \
    || { echo "macos-deps: libMoltenVK.dylib hash mismatch" >&2; exit 1; }
cp "$mvk_dir/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib" "$PREFIX/lib/"
cp "$mvk_dir/MoltenVK/LICENSE" "$LIC/LICENSE-MoltenVK.txt"

# The texture upscaler's ncnn and model (sh-upscale); writes its license texts into $LIC.
SH_LINUX_DEPS_PREFIX="$PREFIX" JOBS="$JOBS" "$ROOT/tools/ncnn/build.sh"

echo "build: installed into $PREFIX"
ls "$PREFIX/lib"
