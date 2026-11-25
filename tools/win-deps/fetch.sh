#!/usr/bin/env bash
# Fetch the Windows build dependencies at pinned versions into
# third_party/deps/src/<name>/ (ignored by git, see docs/BUILDING.md).
#
# Each entry is pinned by tag and by the commit the tag must resolve to: a
# moved or re-tagged upstream release fails here instead of silently building
# something else. The .git directory is removed after the check.
#
# Usage: tools/win-deps/fetch.sh [--force] [name...]
#   With names, only those entries are fetched (tools/ffmpeg/build.sh asks for
#   FFmpeg alone on Linux).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC_DIR="${SH_WIN_DEPS_SRC:-$ROOT/third_party/deps/src}"
FORCE=0
[[ "${1:-}" == "--force" ]] && { FORCE=1; shift; }
ONLY=("$@")

# name | repository | tag | commit | license [| submodules to fetch, space separated]
# Versions match the Linux build of the project (pkg-config sdl3, pkg-config
# openal, pkg-config libjpeg).
DEPS=(
    "SDL3|https://github.com/libsdl-org/SDL.git|release-3.4.18|829a65d769d935c4852f8159e964312c0957260a|Zlib"
    "openal-soft|https://github.com/kcat/openal-soft.git|1.24.3|dc7d7054a5b4f3bec1dc23a42fd616a0847af948|LGPL-2.1-or-later"
    "libjpeg-turbo|https://github.com/libjpeg-turbo/libjpeg-turbo.git|3.1.4.1|9217719d3a58633923b096af4c1d50d304768a64|IJG AND BSD-3-Clause AND Zlib"
    # Mod FMV replacements in mp4; built LGPL-only by tools/ffmpeg/build.sh.
    "FFmpeg|https://github.com/FFmpeg/FFmpeg.git|n8.1.3|1041abdc962f4cc4f394aa8de9dc5236c0c3b9e7|LGPL-2.1-or-later"
    # AI texture upscaling (tools/sh-upscale, docs/RENDER.md section 12), built by tools/ncnn/build.sh.
    # Its Vulkan shaders are compiled by the glslang it carries as a submodule.
    "ncnn|https://github.com/Tencent/ncnn.git|20250916|c4193aadbbb56582aa87b1850dd3d98fb8fd936d|BSD-3-Clause|glslang"
    # Opus audio in mods and the opus music export (Ogg Opus; the Ogg layer is the engine's own, formats/ogg_opus).
    "opus|https://github.com/xiph/opus.git|v1.6.1|22244de5a79bd1d6d623c32e72bf1954b56235be|BSD-3-Clause"
)

mkdir -p "$SRC_DIR"
for entry in "${DEPS[@]}"; do
    IFS='|' read -r name repo tag commit license submodules <<<"$entry"
    if [[ ${#ONLY[@]} -gt 0 && ! " ${ONLY[*]} " == *" $name "* ]]; then
        continue
    fi
    dest="$SRC_DIR/$name"
    stamp="$dest/.sh-fetched"
    if [[ -f "$stamp" && "$(cat "$stamp")" == "$tag $commit" && $FORCE -eq 0 ]]; then
        echo "fetch: $name $tag already present"
        continue
    fi
    rm -rf "$dest" "$dest.tmp"
    echo "fetch: $name $tag ($license)"
    git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$tag" "$repo" "$dest.tmp"
    got="$(git -C "$dest.tmp" rev-parse HEAD)"
    if [[ "$got" != "$commit" ]]; then
        echo "fetch: $name: tag $tag is $got, expected $commit" >&2
        rm -rf "$dest.tmp"
        exit 1
    fi
    for sub in ${submodules:-}; do
        git -C "$dest.tmp" submodule update --quiet --init --depth 1 "$sub"
        rm -rf "$dest.tmp/$sub/.git"
    done
    rm -rf "$dest.tmp/.git"
    mv "$dest.tmp" "$dest"
    echo "$tag $commit" >"$stamp"
done
echo "fetch: sources in $SRC_DIR"
