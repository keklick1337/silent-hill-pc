#!/usr/bin/env bash
# Assemble a release package from a finished build: engine binaries with every
# library they need, the repository's own base data and the license texts. Never
# anything imported from a disc image: the game imports the user's disc at first
# run.
#
# Libraries: the ones the build deployed beside the exe go in (SDL3, OpenAL Soft,
# the FFmpeg set the game opens at run time), and then the import tables of every
# binary in the package are followed (tools/ci/check_package.py): a library that
# is imported but is neither in the package nor part of the operating system is
# looked up in the dependency prefix and in the compiler's runtime directories
# and copied; if it is nowhere, the packaging fails. No list of DLL names is kept
# here.
#
# gamedata/ is taken from the files git tracks, not from <build>/gamedata, so a
# local build folder that has already run the import cannot leak raw/, convert/,
# tables/ or imported maps into the archive. check_package.py refuses the archive
# if anything import-shaped is in it anyway.
#
# Usage: tools/ci/package.sh <build-dir> <linux|windows> <version> [<out-dir>]
#   writes <out-dir>/SilentHillPC-<version>-<platform>-x86_64.{tar.gz,zip}
#   SH_PACKAGE_RUN=1 also starts the packaged game with --version and --help
#   (Windows: under wine) from the unpacked archive.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="$(cd "${1:?build dir}" && pwd)"
PLATFORM="${2:?linux|windows}"
VERSION="${3:?version}"
OUT="${4:-$ROOT/.tmp/ci-dist}"
CHECK="$ROOT/tools/ci/check_package.py"

# A release is named by its tag, and the tag must say the engine version the
# binaries carry (the VERSION file): mods compare "min_engine_version" with it.
# "v1.2.0" and a pre-release "v1.2.0-rc1" fit VERSION 1.2.0; any other name
# (a git describe of a branch build, "dev") is not a release and is not checked.
"$ROOT/tools/ci/check_release_version.sh" "$VERSION"

NAME="SilentHillPC-$VERSION-$PLATFORM-x86_64"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
STAGE="$OUT/$NAME"

case "$PLATFORM" in
    linux)   EXE=""     ;;
    windows) EXE=".exe" ;;
    *) echo "package: unknown platform $PLATFORM" >&2; exit 2 ;;
esac

need() { [[ -e "$1" ]] || { echo "package: missing $1" >&2; exit 1; }; }
need "$BUILD/SilentHillPC$EXE"
need "$BUILD/tools/sh-import/sh-import$EXE"
need "$BUILD/assets"

# A value of the build's CMake cache (empty when it is not there).
cache() { sed -n "s/^$1:[A-Z]*=//p" "$BUILD/CMakeCache.txt" | head -n 1; }
# The build's C++ compiler. A toolchain file sets it as a plain variable, which the cache does not hold; CMake's
# record of the compiler does.
build_cxx() {
    local cxx
    cxx="$(cache CMAKE_CXX_COMPILER)"
    [[ -n "$cxx" ]] || cxx="$(sed -n 's/^set(CMAKE_CXX_COMPILER "\(.*\)")$/\1/p' \
        "$BUILD"/CMakeFiles/*/CMakeCXXCompiler.cmake 2>/dev/null | head -n 1)"
    echo "$cxx"
}

rm -rf "$STAGE"
mkdir -p "$STAGE/licenses"

cp "$BUILD/SilentHillPC$EXE" "$STAGE/"
cp "$BUILD/tools/sh-import/sh-import$EXE" "$STAGE/"
# The AI texture upscaler and its model (tools/ncnn/build.sh, run by the dependency builds); its licenses come with
# the build's licenses/ below.
need "$BUILD/sh-upscale$EXE"
need "$BUILD/upscale-models"
cp "$BUILD/sh-upscale$EXE" "$STAGE/"
cp -r "$BUILD/upscale-models" "$STAGE/"
[[ "$PLATFORM" == linux ]] && strip --strip-unneeded "$STAGE/sh-upscale"

# Where a library the binaries import may be found when the build did not put it beside the exe.
SEARCH=("$BUILD")
shopt -s nullglob
if [[ "$PLATFORM" == windows ]]; then
    # Everything the build deployed: SDL3.dll, OpenAL32.dll and the FFmpeg DLLs, which the exe does not import
    # but opens at the first mp4 movie of a mod.
    libs=("$BUILD"/*.dll)
    (( ${#libs[@]} )) || { echo "package: no DLLs beside the exe" >&2; exit 1; }
    cp "${libs[@]}" "$STAGE/"
    # Every DLL of the dependency prefix the build was configured with must have come along: a build made before
    # tools/win-deps/build.sh finished (no FFmpeg yet) is not a release.
    DEPS="$(cache SH_WIN_DEPS_PREFIX)"
    [[ -n "$DEPS" ]] || DEPS="$ROOT/third_party/deps/x86_64-w64-mingw32"
    for dll in "$DEPS"/bin/*.dll; do
        [[ -f "$STAGE/$(basename "$dll")" ]] \
            || { echo "package: $(basename "$dll") of $DEPS/bin is not beside the exe: rebuild" >&2; exit 1; }
    done
    if grep -qa 'SH_FMV_FFMPEG:BOOL=ON' "$BUILD/CMakeCache.txt"; then
        ff=("$STAGE"/avcodec-*.dll)
        (( ${#ff[@]} )) || { echo "package: the build has no FFmpeg DLLs (tools/win-deps/build.sh)" >&2; exit 1; }
    fi
    # Mesa llvmpipe, loaded only without an OpenGL 3.3 driver (tools/win-deps/software_gl.sh).
    need "$BUILD/software_gl/opengl32.dll"
    cp -a "$BUILD/software_gl" "$STAGE/"
    CXX="$(build_cxx)"
    SEARCH+=("$DEPS/bin")
else
    strip --strip-unneeded "$STAGE/SilentHillPC" "$STAGE/sh-import"
    # The project's FFmpeg (mod mp4 movies), opened from the exe's directory, and its OpenAL Soft when the build
    # used tools/linux-deps/build.sh.
    libs=("$BUILD"/lib*.so.*)
    (( ${#libs[@]} )) && cp "${libs[@]}" "$STAGE/"
    CXX="$(build_cxx)"
    # The Linux menu entry: the template, and the script that installs it for the user who asks.
    need "$BUILD/silent-hill-pc.desktop.in"
    need "$BUILD/install-desktop.sh"
    cp "$BUILD/silent-hill-pc.desktop.in" "$STAGE/"
    install -m 755 "$BUILD/install-desktop.sh" "$STAGE/install-desktop.sh"
fi
shopt -u nullglob

# Follow the imports until nothing is missing. A library that has to be taken from the compiler (a GCC runtime
# library the binaries were not linked statically against) is copied and named in licenses/.
compiler_file() {
    local found
    [[ -n "${CXX:-}" ]] || return 1
    found="$("$CXX" -print-file-name="$1" 2>/dev/null)" || return 1
    [[ "$found" == /* && -f "$found" ]] && { echo "$found"; return 0; }
    if [[ "$PLATFORM" == windows ]]; then
        # The DLLs of a mingw toolchain sit in <sysroot>/bin or <sysroot>/<triple>/bin, which -print-file-name
        # does not search.
        local sysroot dir
        sysroot="$("$CXX" -print-sysroot 2>/dev/null)" || return 1
        for dir in "$sysroot/bin" "$sysroot/mingw/bin" "$sysroot/lib" "$sysroot/x86_64-w64-mingw32/bin" \
                   "$sysroot/x86_64-w64-mingw32/lib"; do
            [[ -f "$dir/$1" ]] && { echo "$dir/$1"; return 0; }
        done
    fi
    return 1
}
runtime_copied=()
for _ in 1 2 3 4 5 6 7 8; do
    missing="$(python3 -I "$CHECK" missing "$STAGE" "$PLATFORM" | cut -f1 | sort -u)"
    [[ -n "$missing" ]] || break
    progressed=0
    while IFS= read -r lib; do
        src=""
        for dir in "${SEARCH[@]}"; do
            [[ -f "$dir/$lib" ]] && { src="$dir/$lib"; break; }
        done
        if [[ -z "$src" ]] && src="$(compiler_file "$lib")"; then
            runtime_copied+=("$lib")
        fi
        if [[ -z "$src" ]]; then
            if [[ "$PLATFORM" == linux && "${SH_PACKAGE_ALLOW_SYSTEM_LIBS:-0}" == 1 ]]; then
                continue
            fi
            echo "package: $lib is imported but is not in the build, the dependency prefix or the compiler's" >&2
            echo "package: runtime, and is not a system library:" >&2
            python3 -I "$CHECK" missing "$STAGE" "$PLATFORM" | grep -F "$lib" >&2 || true
            [[ "$PLATFORM" == linux ]] && echo "package: build the dependencies with tools/linux-deps/build.sh, or" \
                "set SH_PACKAGE_ALLOW_SYSTEM_LIBS=1 for a package of your own that uses this system's libraries" >&2
            exit 1
        fi
        cp -L "$src" "$STAGE/$lib"
        echo "package: added $lib (from $(dirname "$src"))"
        progressed=1
    done <<<"$missing"
    (( progressed )) || break
done

# Runtime assets as cmake/copy_assets.cmake deploys them (assets/runtime -> assets/,
# gamecontrollerdb.txt, licenses/, the console font) and the application icon (assets/icon/).
cp -a "$BUILD/assets" "$STAGE/assets"
need "$BUILD/gamecontrollerdb.txt"
cp "$BUILD/gamecontrollerdb.txt" "$STAGE/"
[[ -d "$BUILD/fonts" ]] && cp -a "$BUILD/fonts" "$STAGE/fonts"
cp "$ROOT/assets/config.cfg" "$STAGE/config.cfg"

# Base game data: exactly the tracked files of gamedata/.
git -C "$ROOT" ls-files -z -- gamedata | (cd "$ROOT" && xargs -0 cp --parents -t "$STAGE")

# Licenses: the project, the bundled assets, the dependency libraries.
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
cp "$ROOT/COPYRIGHT.md" "$STAGE/COPYRIGHT.md"
cp "$ROOT/README.md" "$STAGE/README.md"
cp -a "$ROOT/licenses/." "$STAGE/licenses/"
[[ -d "$BUILD/licenses" ]] && cp -a "$BUILD/licenses/." "$STAGE/licenses/"
[[ -f "$ROOT/third_party/fonts/jetbrains-mono/OFL.txt" ]] && cp "$ROOT/third_party/fonts/jetbrains-mono/OFL.txt" "$STAGE/licenses/LICENSE-JetBrainsMono-OFL.txt"
# The compiler's runtime is part of every binary (statically, cmake/toolchains/mingw-w64-x86_64.cmake) or was
# copied above: its notice, and the distribution's own copyright files when the build machine has them.
if [[ "$PLATFORM" == windows ]]; then
    cp "$ROOT/tools/ci/notices/mingw-w64-runtime.txt" "$STAGE/licenses/NOTICE-mingw-w64-runtime.txt"
    for doc in /usr/share/doc/mingw-w64-x86-64-dev/copyright /usr/share/doc/gcc-mingw-w64-x86-64-posix/copyright \
               /usr/share/doc/gcc-mingw-w64-x86-64-posix-runtime/copyright; do
        [[ -f "$doc" ]] && cp "$doc" "$STAGE/licenses/copyright-$(basename "$(dirname "$doc")").txt"
    done
else
    cp "$ROOT/tools/ci/notices/gcc-runtime.txt" "$STAGE/licenses/NOTICE-gcc-runtime.txt"
fi
if (( ${#runtime_copied[@]} )); then
    echo "package: compiler runtime libraries in the package: ${runtime_copied[*]}"
fi

# The single-header libraries carry their notice inside the source: copy the
# comment block that holds it (from the Nth line matching the pattern to the
# end of that comment), which the MIT/BSD terms require beside the binary.
notice() {
    local out="$1" file="$2" pattern="$3" nth="${4:-1}"
    [[ -f "$ROOT/$file" ]] || return 0
    awk -v pat="$pattern" -v nth="$nth" '
        !on && $0 ~ pat && ++seen == nth { on = 1 }
        on { print; if ($0 ~ /\*\//) exit }' "$ROOT/$file" >"$STAGE/licenses/$out"
    [[ -s "$STAGE/licenses/$out" ]] || { echo "package: no notice found in $file" >&2; exit 1; }
}
notice LICENSE-lua.txt      third_party/lua/src/lua.h          'Copyright [(]C[)] 1994-' 2
notice LICENSE-miniz.txt    third_party/miniz/src/miniz.c      'Copyright 2013-2014 RAD'
notice LICENSE-stb.txt      third_party/stb/include/stb_image.h '^This software is available under 2 licenses'
notice LICENSE-cgltf.txt    third_party/cgltf/include/cgltf.h  'Copyright [(]c[)] 2018-2021 Johannes'
notice LICENSE-bcdec.txt    third_party/bcdec/include/bcdec.h  'Copyright [(]c[)] 2022 Sergii'
# The native Vulkan backend (src/render/rhi/rhi_vulkan.cpp) compiles these in.
cp "$ROOT/third_party/volk/LICENSE.md" "$STAGE/licenses/LICENSE-volk.txt"
cp "$ROOT/third_party/vma/LICENSE.txt" "$STAGE/licenses/LICENSE-VulkanMemoryAllocator.txt"
cp "$ROOT/third_party/vulkan-headers/LICENSES/Apache-2.0.txt" "$STAGE/licenses/LICENSE-Vulkan-Headers-Apache-2.0.txt"

cd "$OUT"
if [[ "$PLATFORM" == windows ]]; then
    ARCHIVE="$NAME.zip"
    rm -f "$ARCHIVE"
    zip -qr9 "$ARCHIVE" "$NAME"
else
    ARCHIVE="$NAME.tar.gz"
    rm -f "$ARCHIVE"
    tar -czf "$ARCHIVE" --owner=0 --group=0 "$NAME"
fi

# The archive as a user gets it: required files, nothing disc-derived, every imported library present.
run=()
[[ "${SH_PACKAGE_RUN:-0}" == 1 ]] && run=(--run)
if [[ "$PLATFORM" == linux && "${SH_PACKAGE_ALLOW_SYSTEM_LIBS:-0}" == 1 ]]; then
    echo "package: SH_PACKAGE_ALLOW_SYSTEM_LIBS=1, the archive is not checked: it needs this system's libraries" >&2
else
    python3 -I "$CHECK" check "$OUT/$ARCHIVE" "$PLATFORM" "${run[@]}"
fi
echo "package: $OUT/$ARCHIVE"
