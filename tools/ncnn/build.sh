#!/usr/bin/env bash
# Build ncnn (static, Vulkan compute) for tools/sh-upscale, the AI texture upscaler (docs/RENDER.md
# section 12), and fetch the Real-ESRGAN model it runs. Sources come from tools/win-deps/fetch.sh (ncnn, pinned tag +
# commit, with its glslang submodule) into third_party/deps/src/ncnn; nothing is installed into the system.
#
#   native (default)  third_party/deps/<os>-<cpu>/        (e.g. linux-x86_64)
#   --mingw           third_party/deps/x86_64-w64-mingw32/ (or SH_WIN_DEPS_PREFIX)
#
# ncnn opens the Vulkan loader at run time (its own minimal loader, NCNN_SIMPLEVK): no Vulkan SDK is needed to
# build, and a machine without Vulkan only loses the upscaler. OpenMP is off: the work runs on the GPU, and the
# MinGW build then needs no libgomp DLL.
#
# The model (realesrgan-x4plus, BSD-3-Clause, COPYRIGHT.md; chosen over the smaller realesr-animevideov3, which
# smears the game's textures, and x4plus-anime, which paints outlines) comes from the Real-ESRGAN release
# archive pinned by its SHA-256 and lands in <prefix>/share/sh-upscale/models/; the root CMakeLists.txt copies
# it beside the tool.
#
# Usage: tools/ncnn/build.sh [--mingw] [--clean]
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

CMAKE_FLAGS=()
if [[ $MINGW -eq 1 ]]; then
    TRIPLE=x86_64-w64-mingw32
    PREFIX="${SH_WIN_DEPS_PREFIX:-$DEPS_DIR/$TRIPLE}"
    BUILD_DIR="$DEPS_DIR/build/$TRIPLE/ncnn"
    CMAKE_FLAGS+=("-DCMAKE_TOOLCHAIN_FILE=$ROOT/cmake/toolchains/mingw-w64-x86_64.cmake")
else
    HOST="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
    PREFIX="${SH_LINUX_DEPS_PREFIX:-$DEPS_DIR/$HOST}"
    BUILD_DIR="$DEPS_DIR/build/$HOST/ncnn"
fi

[[ $CLEAN -eq 1 ]] && rm -rf "$BUILD_DIR"

"$ROOT/tools/win-deps/fetch.sh" ncnn

mkdir -p "$BUILD_DIR" "$PREFIX"
echo "ncnn: configure ($BUILD_DIR)"
# The guard keeps an empty list (a native build) usable under set -u in bash 3.2, the system bash of macOS.
cmake -G Ninja -S "$SRC_DIR/ncnn" -B "$BUILD_DIR" ${CMAKE_FLAGS[@]+"${CMAKE_FLAGS[@]}"} \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DNCNN_VULKAN=ON -DNCNN_SIMPLEVK=ON -DNCNN_SYSTEM_GLSLANG=OFF \
    -DNCNN_SHARED_LIB=OFF -DNCNN_OPENMP=OFF -DNCNN_C_API=OFF \
    -DNCNN_BUILD_TOOLS=OFF -DNCNN_BUILD_EXAMPLES=OFF -DNCNN_BUILD_BENCHMARK=OFF -DNCNN_BUILD_TESTS=OFF \
    -DNCNN_PYTHON=OFF -DNCNN_PIXEL_ROTATE=OFF -DNCNN_PIXEL_AFFINE=OFF -DNCNN_PIXEL_DRAWING=OFF \
    >"$BUILD_DIR/configure.log" 2>&1 || { tail -n 40 "$BUILD_DIR/configure.log"; exit 1; }
echo "ncnn: build (-j$JOBS)"
nice -n 19 cmake --build "$BUILD_DIR" --parallel "$JOBS" >"$BUILD_DIR/build.log" 2>&1 \
    || { tail -n 40 "$BUILD_DIR/build.log"; exit 1; }
cmake --install "$BUILD_DIR" >"$BUILD_DIR/install.log" 2>&1 \
    || { tail -n 40 "$BUILD_DIR/install.log"; exit 1; }
# The license travels with the tool; the game build copies share/sh-licenses/* into <build>/licenses/.
mkdir -p "$PREFIX/share/sh-licenses"
cp "$SRC_DIR/ncnn/LICENSE.txt" "$PREFIX/share/sh-licenses/LICENSE-ncnn.txt"

# The model: its two files out of the release archive, each checked by its own hash as well.
MODEL_URL="https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-ubuntu.zip"
MODEL_ZIP_SHA256="e5aa6eb131234b87c0c51f82b89390f5e3e642b7b70f2b9bbe95b6a285a40c96"
MODEL_FILES=(
    "realesrgan-x4plus.param|35330ececcea33b6c397a72548e788d5d53becee4734c50b7fada36e89f10a86"
    "realesrgan-x4plus.bin|713ee713b0353afaa27976f0563a64a5043bd70b9bd8936c2e26e25ebcdbcddf"
)
MODEL_DIR="$PREFIX/share/sh-upscale/models"
MODEL_CACHE="$SRC_DIR/realesrgan-models"
mkdir -p "$MODEL_DIR" "$MODEL_CACHE"
zip="$MODEL_CACHE/realesrgan-ncnn-vulkan-20220424-ubuntu.zip"
if [[ ! -f "$zip" ]] || ! echo "$MODEL_ZIP_SHA256  $zip" | sha256sum -c --quiet - 2>/dev/null; then
    echo "model: download $MODEL_URL"
    curl -fsSL -o "$zip.tmp" "$MODEL_URL"
    echo "$MODEL_ZIP_SHA256  $zip.tmp" | sha256sum -c --quiet - || { echo "model: archive hash mismatch" >&2; exit 1; }
    mv "$zip.tmp" "$zip"
fi
for entry in "${MODEL_FILES[@]}"; do
    IFS='|' read -r file sum <<<"$entry"
    unzip -o -q -j "$zip" "models/$file" -d "$MODEL_DIR"
    echo "$sum  $MODEL_DIR/$file" | sha256sum -c --quiet - || { echo "model: $file hash mismatch" >&2; exit 1; }
done
# Real-ESRGAN, whose release carries the model, is BSD-3-Clause; its license file at the same tag.
LICENSE_URL="https://raw.githubusercontent.com/xinntao/Real-ESRGAN/v0.2.5.0/LICENSE"
LICENSE_SHA256="4a699ec4863d96a91fc265948a0c90033f7e8735d515524dcf3444736406e0c2"
lic="$PREFIX/share/sh-licenses/LICENSE-Real-ESRGAN.txt"
if [[ ! -f "$lic" ]] || ! echo "$LICENSE_SHA256  $lic" | sha256sum -c --quiet - 2>/dev/null; then
    curl -fsSL -o "$lic.tmp" "$LICENSE_URL"
    echo "$LICENSE_SHA256  $lic.tmp" | sha256sum -c --quiet - || { echo "model: license hash mismatch" >&2; exit 1; }
    mv "$lic.tmp" "$lic"
fi
echo "ncnn: installed into $PREFIX, model in $MODEL_DIR"
