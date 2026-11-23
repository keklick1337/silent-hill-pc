#!/usr/bin/env bash
# Mesa llvmpipe for Windows x64: the OpenGL 3.3+ renderer on the CPU that the
# game loads from <exe>/software_gl/ when the system has no OpenGL 3.3 driver
# (docs/RENDER.md, section 2.2c). Installs opengl32.dll and
# libgallium_wgl.dll into $PREFIX/software_gl/ and the license texts into
# $PREFIX/share/sh-licenses/ (the game build copies both beside the exe).
#
# The binaries are the prebuilt mingw (UCRT) release of mesa-dist-win: building
# llvmpipe needs a mingw build of LLVM, hours of CPU for one DLL. Every download
# is pinned by version and SHA-256; a changed file stops the script.
#
#   Mesa 26.2.6 (MIT; LLVM 23.1.4 Apache-2.0 WITH LLVM-exception, zlib, zstd BSD-3-Clause linked in)
#   mesa-dist-win 26.2.6 packaging (MIT)
#
# Usage: tools/win-deps/software_gl.sh [--force]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEPS_DIR="$ROOT/third_party/deps"
PREFIX="${SH_WIN_DEPS_PREFIX:-$DEPS_DIR/x86_64-w64-mingw32}"
CACHE="${SH_WIN_DEPS_SRC:-$DEPS_DIR/src}/mesa-dist-win"
FORCE=0
[[ "${1:-}" == "--force" ]] && FORCE=1

VERSION=26.2.6
ARCHIVE="mesa3d-$VERSION-release-mingw.7z"
ARCHIVE_URL="https://github.com/pal1000/mesa-dist-win/releases/download/$VERSION/$ARCHIVE"
ARCHIVE_SHA256=da0f71d9181b672c1918f3c342f11118c687f6059a46b16502845160733cc388
MESA_RAW="https://gitlab.freedesktop.org/mesa/mesa/-/raw/mesa-$VERSION"

# output name | url | sha256
LICENSES=(
    "LICENSE-Mesa.rst|$MESA_RAW/docs/license.rst|0d1a0472ecc81830e75c20d59b0ea02841e3db21255e0ebad97ab682c54d6615"
    "LICENSE-Mesa-MIT.txt|$MESA_RAW/licenses/MIT|323c587d0ccf10e376f8bf9a7f31fb4ca6078105194b42e0b1e0ee2bc9bde71f"
    "LICENSE-Mesa-Apache-2.0.txt|$MESA_RAW/licenses/Apache-2.0|7e4d3fba250d15e9e0afa380d4b611828fa2560cc8fdebe95bcf2f01a487de12"
    "LICENSE-Mesa-BSL-1.0.txt|$MESA_RAW/licenses/BSL-1.0|9b9fea7520a816107fcac2598b7c0155222f16bc430e989706b22f4367f1df7d"
    "LICENSE-Mesa-SGI-B-2.0.txt|$MESA_RAW/licenses/SGI-B-2.0|686bf035a1fd22076416fd3b90370ac67771e884bf57f55693d51f8ce7c710a7"
    "LICENSE-mesa-dist-win.txt|https://raw.githubusercontent.com/pal1000/mesa-dist-win/$VERSION/LICENSE|9cd0121dc070f0ac65c1fc266bee74bdbbc22cea4210ae72ae0c1f076448d4cc"
    "LICENSE-LLVM.txt|https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-23.1.4/llvm/LICENSE.TXT|8d85c1057d742e597985c7d4e6320b015a9139385cff4cbae06ffc0ebe89afee"
    "LICENSE-zstd.txt|https://raw.githubusercontent.com/facebook/zstd/v1.5.7/LICENSE|7055266497633c9025b777c78eb7235af13922117480ed5c674677adc381c9d8"
    "LICENSE-zlib.txt|https://raw.githubusercontent.com/madler/zlib/v1.3.2/LICENSE|e32ff4e00d9d94930537635291da39e7e612703334bf6fde8c7f1686fe8a45a2"
)

# Downloads $2 into $1 unless a file with the SHA-256 $3 is already there.
fetch() {
    local dest="$1" url="$2" sha="$3"
    if [[ $FORCE -eq 0 && -f "$dest" ]] && echo "$sha  $dest" | sha256sum -c --status; then
        return
    fi
    curl -fsSL --retry 3 -o "$dest.tmp" "$url"
    if ! echo "$sha  $dest.tmp" | sha256sum -c --status; then
        echo "software_gl: $url: SHA-256 $(sha256sum "$dest.tmp" | cut -d' ' -f1), expected $sha" >&2
        rm -f "$dest.tmp"
        exit 1
    fi
    mv "$dest.tmp" "$dest"
}

command -v 7z >/dev/null || { echo "software_gl: 7z (p7zip) is needed to unpack $ARCHIVE" >&2; exit 1; }
mkdir -p "$CACHE"
echo "software_gl: Mesa $VERSION (mesa-dist-win, mingw)"
fetch "$CACHE/$ARCHIVE" "$ARCHIVE_URL" "$ARCHIVE_SHA256"

OUT="$PREFIX/software_gl"
rm -rf "$OUT" "$CACHE/unpacked"
7z x -y -bso0 -bsp0 -o"$CACHE/unpacked" "$CACHE/$ARCHIVE" x64/opengl32.dll x64/libgallium_wgl.dll
mkdir -p "$OUT"
mv "$CACHE/unpacked/x64/opengl32.dll" "$CACHE/unpacked/x64/libgallium_wgl.dll" "$OUT/"
rm -rf "$CACHE/unpacked"

LIC="$PREFIX/share/sh-licenses"
mkdir -p "$LIC"
for entry in "${LICENSES[@]}"; do
    IFS='|' read -r name url sha <<<"$entry"
    fetch "$CACHE/$name" "$url" "$sha"
    cp "$CACHE/$name" "$LIC/$name"
done
echo "software_gl: installed into $OUT"
