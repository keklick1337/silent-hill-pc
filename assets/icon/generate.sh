#!/usr/bin/env bash
# Rebuild every size of the application icon. Everything about the icon is in this directory:
#   master.svg   the drawing (the project's own); rendered to master.png when rsvg-convert is there
#   master.png   the one source of the sizes: a square picture, 512x512 or larger
#   app_<N>.png  N = 16 24 32 48 64 128 256 - the window icon at run time (app_128.png) and the hicolor icons the
#                desktop entry installs (--install-desktop, install-desktop.sh)
#   app.ico      the Windows resource of the exe (assets/res/app.rc)
# To change the icon, replace master.png and run this script with --keep-master (or remove master.svg); commit
# the results. Needs ImageMagick 7 (magick).
#
# Usage: assets/icon/generate.sh [--keep-master]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SIZES=(16 24 32 48 64 128 256)

if [[ "${1:-}" != --keep-master && -f "$HERE/master.svg" ]] && command -v rsvg-convert >/dev/null; then
    rsvg-convert -w 512 -h 512 "$HERE/master.svg" -o "$HERE/master.png"
fi
[[ -f "$HERE/master.png" ]] || { echo "icon: no master.png in $HERE" >&2; exit 1; }

for size in "${SIZES[@]}"; do
    # A little sharpening keeps the picture readable in the small sizes.
    magick "$HERE/master.png" -filter Lanczos -resize "${size}x${size}" -unsharp 0x0.6+0.5+0 \
        -strip -define png:exclude-chunks=date,time "$HERE/app_$size.png"
done
magick "$HERE/app_16.png" "$HERE/app_24.png" "$HERE/app_32.png" "$HERE/app_48.png" "$HERE/app_64.png" \
    "$HERE/app_256.png" "$HERE/app.ico"
echo "icon: ${SIZES[*]} and app.ico in $HERE"
