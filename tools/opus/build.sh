#!/usr/bin/env bash
# Build libopus (BSD-3-Clause, static) from the pinned source of tools/win-deps/fetch.sh into a dependency prefix,
# with its license text in <prefix>/share/sh-licenses/. The dependency scripts of every platform call it
# (tools/linux-deps, tools/win-deps, tools/macos-deps, tools/android-deps); nothing is installed into the system.
#
# Only the codec: no programs, tests or neural-network extensions (DRED, OSCE and deep PLC need model files that the
# source tree does not carry and that a mod sound does not need). The Ogg container is the engine's own
# (src/formats/convert/audio/ogg_opus.cpp), so neither libogg nor opusfile is built.
#
# Usage: tools/opus/build.sh <prefix> <build-dir> [extra CMake arguments: toolchain, ABI ...]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC_DIR="${SH_WIN_DEPS_SRC:-$ROOT/third_party/deps/src}"
JOBS="${JOBS:-4}"
[[ $# -ge 2 ]] || { echo "usage: $0 <prefix> <build-dir> [cmake args...]" >&2; exit 2; }
PREFIX="$1"
BUILD_DIR="$2"
shift 2

"$ROOT/tools/win-deps/fetch.sh" opus
SRC="$SRC_DIR/opus"
# The fetched tree has no .git, from which Opus's CMake takes its version; its package_version file is the
# fallback the release archives use.
tag="$(cut -d' ' -f1 "$SRC/.sh-fetched")"
echo "PACKAGE_VERSION=\"${tag#v}\"" >"$SRC/package_version"

echo "build: opus $tag"
mkdir -p "$BUILD_DIR"
cmake -G Ninja -S "$SRC" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=OFF -DOPUS_BUILD_SHARED_LIBRARY=OFF \
    -DOPUS_BUILD_TESTING=OFF -DOPUS_BUILD_PROGRAMS=OFF \
    -DOPUS_DRED=OFF -DOPUS_OSCE=OFF \
    -DOPUS_INSTALL_PKG_CONFIG_MODULE=ON -DOPUS_INSTALL_CMAKE_CONFIG_MODULE=ON \
    "$@" >"$BUILD_DIR.configure.log" 2>&1 \
    || { tail -n 40 "$BUILD_DIR.configure.log"; exit 1; }
cmake --build "$BUILD_DIR" --parallel "$JOBS" >"$BUILD_DIR.build.log" 2>&1 \
    || { tail -n 40 "$BUILD_DIR.build.log"; exit 1; }
cmake --install "$BUILD_DIR" >"$BUILD_DIR.install.log" 2>&1 \
    || { tail -n 40 "$BUILD_DIR.install.log"; exit 1; }

mkdir -p "$PREFIX/share/sh-licenses"
cp "$SRC/COPYING" "$PREFIX/share/sh-licenses/LICENSE-opus.txt"
