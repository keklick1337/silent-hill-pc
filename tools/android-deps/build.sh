#!/usr/bin/env bash
# Build the Android dependencies of the game for each ABI with the NDK, from the same pinned sources as the Linux,
# Windows and macOS builds (tools/win-deps/fetch.sh), into third_party/deps/android-<abi>/ (ignored by git). Nothing
# is installed into the system.
#
#   SDL3           shared  libSDL3.so    (Zlib; its Java glue, android-project/app/src/main/java of the same source
#                                         tree, is compiled into the APK by android/app/build.gradle)
#   libjpeg-turbo  static  libjpeg.a     (IJG/BSD)
#
# The game's sound goes through SDL3 (AAudio or OpenSL ES), so OpenAL Soft, which the desktop builds link, is not
# needed. FFmpeg (mod mp4 movies) and ncnn (sh-upscale) are not built for Android: the disc's movies have their own
# decoder, and the texture upscaler is a desktop tool.
#
# Every library is built against the shared C++ runtime (c++_shared), which the APK carries once.
#
# Environment: ANDROID_NDK_HOME (default: $ANDROID_HOME/ndk/<SH_ANDROID_NDK_VERSION>), SH_ANDROID_API (the minimum
# API level, default 29), JOBS.
#
# Usage: tools/android-deps/build.sh [--clean] [arm64-v8a|armeabi-v7a|x86_64|x86 ...]   (default: all four)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEPS_DIR="$ROOT/third_party/deps"
SRC_DIR="${SH_WIN_DEPS_SRC:-$DEPS_DIR/src}"
JOBS="${JOBS:-$(nproc)}"
API="${SH_ANDROID_API:-29}"
NDK_VERSION="${SH_ANDROID_NDK_VERSION:-28.2.13676358}"
NDK="${ANDROID_NDK_HOME:-${ANDROID_HOME:-$HOME/Android/Sdk}/ndk/$NDK_VERSION}"
TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"
[[ -f "$TOOLCHAIN" ]] || { echo "android-deps: no NDK at $NDK (set ANDROID_NDK_HOME)" >&2; exit 2; }

CLEAN=0
[[ "${1:-}" == "--clean" ]] && { CLEAN=1; shift; }
ABIS=("$@")
(( ${#ABIS[@]} )) || ABIS=(arm64-v8a armeabi-v7a x86_64 x86)

"$ROOT/tools/win-deps/fetch.sh" SDL3 libjpeg-turbo

build() {
    local abi="$1" name="$2"; shift 2
    local build_dir="$DEPS_DIR/build/android-$abi/$name" prefix="$DEPS_DIR/android-$abi"
    echo "build: $name ($abi)"
    mkdir -p "$build_dir"
    cmake -G Ninja -S "$SRC_DIR/$name" -B "$build_dir" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DANDROID_ABI="$abi" -DANDROID_PLATFORM="android-$API" -DANDROID_STL=c++_shared \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        "$@" >"$build_dir.configure.log" 2>&1 \
        || { tail -n 40 "$build_dir.configure.log"; exit 1; }
    cmake --build "$build_dir" --parallel "$JOBS" >"$build_dir.build.log" 2>&1 \
        || { tail -n 40 "$build_dir.build.log"; exit 1; }
    cmake --install "$build_dir" >"$build_dir.install.log" 2>&1 \
        || { tail -n 40 "$build_dir.install.log"; exit 1; }
}

for abi in "${ABIS[@]}"; do
    case "$abi" in
        arm64-v8a|armeabi-v7a|x86_64|x86) ;;
        *) echo "android-deps: unknown ABI $abi" >&2; exit 2 ;;
    esac
    prefix="$DEPS_DIR/android-$abi"
    if (( CLEAN )); then
        rm -rf "$DEPS_DIR/build/android-$abi" "$prefix"
    fi

    build "$abi" SDL3 \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF

    # x86 and x86_64 SIMD is assembled with nasm; without it the plain C code is built.
    simd=ON
    if [[ "$abi" == x86* ]] && ! command -v nasm >/dev/null && ! command -v yasm >/dev/null; then
        simd=OFF
    fi
    build "$abi" libjpeg-turbo \
        -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_SIMD="$simd" \
        -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF

    # The licenses travel with the binaries; tools/android/build.sh puts share/sh-licenses/* into the APK.
    lic="$prefix/share/sh-licenses"
    mkdir -p "$lic"
    cp "$SRC_DIR/SDL3/LICENSE.txt"           "$lic/LICENSE-SDL3.txt"
    cp "$SRC_DIR/libjpeg-turbo/LICENSE.md"   "$lic/LICENSE-libjpeg-turbo.md"
    cp "$SRC_DIR/libjpeg-turbo/README.ijg"   "$lic/LICENSE-libjpeg-turbo-ijg.txt"
    echo "build: $abi installed into $prefix"
done
