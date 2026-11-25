#!/usr/bin/env bash
# Build the Android APK (docs/BUILDING.md section 5): the game as libmain.so for each ABI with the NDK, the base data
# of the repository, and the Gradle project in android/ that packages them with SDL's Java glue.
#
#   1. tools/android-deps/build.sh for an ABI whose dependencies are missing (SDL3, libjpeg-turbo, opus)
#   2. the game for each ABI in build-android/<abi>/ (CMake, Ninja, the NDK's toolchain file)
#   3. android/app/build/sh/: jniLibs/<abi>/ (libmain.so, libSDL3.so, libc++_shared.so), assets/base.zip (exactly
#      the repository's files of gamedata/, the runtime assets, config.cfg, the console font and the license texts; the
#      activity unpacks it into the app's storage) and res/ (the launcher icon from assets/icon/)
#   4. gradle assembleDebug (or assembleRelease with --release: unsigned)
#
# Nothing imported from a disc image goes into the APK: the game imports the user's disc at the first start.
#
# Environment: ANDROID_HOME (the SDK), ANDROID_NDK_HOME (default: $ANDROID_HOME/ndk/28.2.13676358), JOBS, GRADLE
# (default: gradle), SH_ANDROID_BUILD_TYPE (CMake build type, default Release), SH_ANDROID_STEP: all (default), libs (only
# steps 1-2 for the given ABIs, staged in android/app/build/sh/: CI builds each ABI on its own runner) or package (only
# steps 3-4, from the libraries already staged there).
#
# Usage: tools/android/build.sh [--release] [arm64-v8a|armeabi-v7a|x86_64|x86 ...]   (default: all four)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export ANDROID_HOME="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
NDK_VERSION="${SH_ANDROID_NDK_VERSION:-28.2.13676358}"
export ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-$ANDROID_HOME/ndk/$NDK_VERSION}"
API="${SH_ANDROID_API:-29}"
JOBS="${JOBS:-$(nproc)}"
GRADLE="${GRADLE:-gradle}"
BUILD_TYPE="${SH_ANDROID_BUILD_TYPE:-Release}"
STEP="${SH_ANDROID_STEP:-all}"
[[ "$STEP" == all || "$STEP" == libs || "$STEP" == package ]] || { echo "android: SH_ANDROID_STEP is all, libs or package" >&2; exit 2; }
TOOLCHAIN="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
[[ "$STEP" == package || -f "$TOOLCHAIN" ]] || { echo "android: no NDK at $ANDROID_NDK_HOME (set ANDROID_NDK_HOME)" >&2; exit 2; }

VARIANT=Debug
[[ "${1:-}" == "--release" ]] && { VARIANT=Release; shift; }
ABIS=("$@")
(( ${#ABIS[@]} )) || ABIS=(arm64-v8a armeabi-v7a x86_64 x86)

STAGE="$ROOT/android/app/build/sh"
SYSROOT_LIB="$(echo "$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/*/sysroot/usr/lib)"
STRIP="$(echo "$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/*/bin/llvm-strip)"
# The SPIR-V compiler of the shaders: the build machine's, else the NDK's.
GLSLC_HINT="$(echo "$ANDROID_NDK_HOME"/shader-tools/*)"

triple() {
    case "$1" in
        arm64-v8a) echo aarch64-linux-android ;;
        armeabi-v7a) echo arm-linux-androideabi ;;
        x86_64) echo x86_64-linux-android ;;
        x86) echo i686-linux-android ;;
        *) echo "android: unknown ABI $1" >&2; exit 2 ;;
    esac
}

# SDL's Java glue comes from the same pinned source as its library.
"$ROOT/tools/win-deps/fetch.sh" SDL3

[[ "$STEP" == all ]] && rm -rf "$STAGE/jniLibs" "$STAGE/deps-licenses"
for abi in "${ABIS[@]}"; do
    [[ "$STEP" == package ]] && break
    triple "$abi" >/dev/null
    deps="$ROOT/third_party/deps/android-$abi"
    if [[ ! -f "$deps/lib/libSDL3.so" || ! -f "$deps/lib/libjpeg.a" || ! -f "$deps/lib/libopus.a" ]]; then
        SH_ANDROID_API="$API" JOBS="$JOBS" "$ROOT/tools/android-deps/build.sh" "$abi"
    fi
    build="$ROOT/build-android/$abi"
    echo "android: the game for $abi"
    cmake -G Ninja -S "$ROOT" -B "$build" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DANDROID_ABI="$abi" -DANDROID_PLATFORM="android-$API" -DANDROID_STL=c++_shared \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_PROGRAM_PATH="$GLSLC_HINT" >/dev/null
    cmake --build "$build" --target SilentHillPC --parallel "$JOBS"

    libs="$STAGE/jniLibs/$abi"
    mkdir -p "$libs"
    cp "$build/libmain.so" "$deps/lib/libSDL3.so" "$libs/"
    cp "$SYSROOT_LIB/$(triple "$abi")/libc++_shared.so" "$libs/"
    # Stripped here, where the NDK is: Gradle strips only when it finds one, and the APK job of CI has none.
    "$STRIP" --strip-unneeded "$libs/"*.so
    rm -rf "$STAGE/deps-licenses/$abi"
    mkdir -p "$STAGE/deps-licenses/$abi"
    cp -a "$deps/share/sh-licenses/." "$STAGE/deps-licenses/$abi/"
done
[[ "$STEP" == libs ]] && exit 0
# Packaging takes every ABI staged, whichever runner built it.
mapfile -t ABIS < <(cd "$STAGE/jniLibs" && ls)
(( ${#ABIS[@]} )) || { echo "android: no libraries staged in $STAGE/jniLibs" >&2; exit 2; }

echo "android: base data"
root="$STAGE/root"
rm -rf "$root" "$STAGE/assets" "$STAGE/res"
mkdir -p "$root/assets" "$root/licenses" "$root/fonts" "$STAGE/assets"
# The repository's files only, a new one not yet committed included: a gamedata/ that has run an import must not
# leak into the APK (.gitignore keeps the imported files out of the listing).
git -C "$ROOT" ls-files -z --cached --others --exclude-standard -- gamedata |
    (cd "$ROOT" && xargs -0 cp --parents -t "$root")
# As cmake/copy_assets.cmake deploys assets/ beside the exe, and the window icon.
cp -a "$ROOT/assets/runtime/." "$root/assets/"
mkdir -p "$root/assets/icon"
cp "$ROOT"/assets/icon/app_*.png "$root/assets/icon/"
cp "$ROOT/assets/gamecontrollerdb.txt" "$ROOT/assets/config.cfg" "$root/"
cp "$ROOT/third_party/fonts/jetbrains-mono/JetBrainsMonoNL-Regular.ttf" \
    "$ROOT/third_party/fonts/jetbrains-mono/OFL.txt" "$root/fonts/"

# Licenses: the project, the bundled assets, the libraries in the APK.
cp "$ROOT/LICENSE" "$root/LICENSE.txt"
cp "$ROOT/COPYRIGHT.md" "$ROOT/README.md" "$root/"
cp -a "$ROOT/licenses/." "$root/licenses/"
cp -a "$ROOT/assets/licenses/." "$root/licenses/"
for abi in "${ABIS[@]}"; do
    cp -a "$STAGE/deps-licenses/$abi/." "$root/licenses/"
done
cp "$ROOT/third_party/fonts/jetbrains-mono/OFL.txt" "$root/licenses/LICENSE-JetBrainsMono-OFL.txt"
cp "$ROOT/third_party/volk/LICENSE.md" "$root/licenses/LICENSE-volk.txt"
cp "$ROOT/third_party/vma/LICENSE.txt" "$root/licenses/LICENSE-VulkanMemoryAllocator.txt"
cp "$ROOT/third_party/vulkan-headers/LICENSES/Apache-2.0.txt" "$root/licenses/LICENSE-Vulkan-Headers-Apache-2.0.txt"
cp "$ANDROID_NDK_HOME/NOTICE" "$root/licenses/NOTICE-android-ndk-libc++.txt" 2>/dev/null || true
# The single-header libraries carry their notice in the source: the comment block that holds it, as package.sh does.
notice() {
    local out="$1" file="$2" pattern="$3" nth="${4:-1}"
    awk -v pat="$pattern" -v nth="$nth" '
        !on && $0 ~ pat && ++seen == nth { on = 1 }
        on { print; if ($0 ~ /\*\//) exit }' "$ROOT/$file" >"$root/licenses/$out"
    [[ -s "$root/licenses/$out" ]] || { echo "android: no notice found in $file" >&2; exit 1; }
}
notice LICENSE-lua.txt      third_party/lua/src/lua.h           'Copyright [(]C[)] 1994-' 2
notice LICENSE-miniz.txt    third_party/miniz/src/miniz.c       'Copyright 2013-2014 RAD'
notice LICENSE-stb.txt      third_party/stb/include/stb_image.h '^This software is available under 2 licenses'
notice LICENSE-cgltf.txt    third_party/cgltf/include/cgltf.h   'Copyright [(]c[)] 2018-2021 Johannes'
notice LICENSE-bcdec.txt    third_party/bcdec/include/bcdec.h   'Copyright [(]c[)] 2022 Sergii'

# A fixed time stamp and order: the same files give the same archive, and its hash names the build of the data.
find "$root" -exec touch -h -d '2000-01-01 00:00:00' {} +
(cd "$root" && find . -mindepth 1 | LC_ALL=C sort | zip -q -X -D -@ "$STAGE/assets/base.zip")
sha256sum "$STAGE/assets/base.zip" | cut -d' ' -f1 >"$STAGE/assets/base.stamp"

# The launcher icon, scaled by Android from the nearest size.
for pair in mdpi:48 hdpi:64 xhdpi:128 xxxhdpi:256; do
    mkdir -p "$STAGE/res/mipmap-${pair%%:*}"
    cp "$ROOT/assets/icon/app_${pair##*:}.png" "$STAGE/res/mipmap-${pair%%:*}/ic_launcher.png"
done

echo "android: gradle assemble$VARIANT"
(cd "$ROOT/android" && "$GRADLE" --no-daemon --console=plain "assemble$VARIANT")
variant="$(echo "$VARIANT" | tr '[:upper:]' '[:lower:]')"
ls -l "$ROOT/android/app/build/outputs/apk/$variant/"*.apk
