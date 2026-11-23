#!/bin/sh
# Add Silent Hill PC to the applications menu of the current user, or remove it again. Nothing is written outside
# the user's own data directory ($XDG_DATA_HOME, by default ~/.local/share):
#   applications/silent-hill-pc.desktop                    with the absolute path of the game beside this script
#   icons/hicolor/<N>x<N>/apps/silent-hill-pc.png          N = 16 24 32 48 64 128 256
# "SilentHillPC --install-desktop" / "--uninstall-desktop" do the same without this script.
#
# Usage: ./install-desktop.sh [--uninstall]
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
ENTRY="$DATA/applications/silent-hill-pc.desktop"
SIZES="16 24 32 48 64 128 256"

if [ "${1:-}" = "--uninstall" ]; then
    rm -f "$ENTRY"
    for size in $SIZES; do
        rm -f "$DATA/icons/hicolor/${size}x${size}/apps/silent-hill-pc.png"
    done
    echo "removed $ENTRY and the icons"
    exit 0
fi
if [ -n "${1:-}" ]; then
    echo "usage: $0 [--uninstall]" >&2
    exit 2
fi

EXE="$HERE/SilentHillPC"
TEMPLATE="$HERE/silent-hill-pc.desktop.in"
[ -x "$EXE" ] || { echo "install-desktop: no SilentHillPC beside this script" >&2; exit 1; }
[ -f "$TEMPLATE" ] || { echo "install-desktop: no silent-hill-pc.desktop.in beside this script" >&2; exit 1; }

# A path as a .desktop string value: "\" is written "\\". Exec is a quoted argument on top of that: inside the
# quotes ", `, $ and \ take a backslash first, and a literal % is %%. for_sed guards what sed's replacement reads.
value() { printf '%s' "$1" | sed -e 's/\\/\\\\/g'; }
exec_value() {
    printf '"%s"' "$(printf '%s' "$1" | sed -e 's/[\\"`$]/\\&/g' -e 's/\\/\\\\/g' -e 's/%/%%/g')"
}
for_sed() { printf '%s' "$1" | sed -e 's/[\\&|]/\\&/g'; }

mkdir -p "$DATA/applications"
sed -e "s|@EXEC@|$(for_sed "$(exec_value "$EXE")")|" \
    -e "s|@TRYEXEC@|$(for_sed "$(value "$EXE")")|" \
    -e "s|@PATH@|$(for_sed "$(value "$HERE")")|" "$TEMPLATE" >"$ENTRY"

for size in $SIZES; do
    icon="$HERE/assets/icon/app_$size.png"
    [ -f "$icon" ] || continue
    mkdir -p "$DATA/icons/hicolor/${size}x${size}/apps"
    cp "$icon" "$DATA/icons/hicolor/${size}x${size}/apps/silent-hill-pc.png"
done
# Icon caches look at the time of the theme directory.
if [ -d "$DATA/icons/hicolor" ]; then
    touch "$DATA/icons/hicolor"
fi

echo "installed $ENTRY"
