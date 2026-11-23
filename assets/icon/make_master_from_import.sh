#!/usr/bin/env bash
# Make master.png from a picture of your own imported game data instead of the project's drawing: the health
# portrait of the inventory screen, <gamedata>/convert/textures/ITEM/HEROPIC2.png (104x152, from ITEM/HEROPIC2.TIM).
# The square 90x90 at (9,9) holds the head; it is laid over a dark vignette, denoised (bilateral blur: the picture
# is a 256-colour dithered render), enlarged to 512x512 and cut to a rounded square. The sizes and app.ico are
# then rebuilt from it.
#
# The result is game art: it is for a build of your own. Whether it may be committed or released is the
# repository owner's decision (COPYRIGHT.md, section 4).
# Needs ImageMagick 7 (magick).
#
# Usage: assets/icon/make_master_from_import.sh <gamedata>/convert/textures/ITEM/HEROPIC2.png
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${1:?path of convert/textures/ITEM/HEROPIC2.png}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

magick -size 90x90 radial-gradient:'#3c4852-#101418' "$TMP/back.png"
magick "$SRC" -crop 90x90+9+9 +repage "$TMP/head.png"
magick "$TMP/back.png" "$TMP/head.png" -composite \
    -bilateral-blur 5x5 -filter Lanczos -resize 512x512 -unsharp 0x1.2+0.6+0.02 "$TMP/flat.png"
magick -size 512x512 xc:none -fill white -draw 'roundrectangle 0,0 511,511 96,96' "$TMP/mask.png"
magick "$TMP/flat.png" "$TMP/mask.png" -alpha off -compose CopyOpacity -composite \
    -strip -define png:exclude-chunks=date,time "$HERE/master.png"
echo "icon: $HERE/master.png"
"$HERE/generate.sh" --keep-master
