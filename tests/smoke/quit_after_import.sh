#!/usr/bin/env bash
# Checks --quit-after-import: the game imports the disc image when gamedata needs it and exits without starting.
#
# Usage: quit_after_import.sh <binary> <image.cue|.bin> <workdir>
#
# The run uses <workdir>/quit-after-import/: a copy of the executable and of its config.cfg, links to the rest of
# its directory, and its own gamedata directory (-data), so the build's gamedata is not touched.
# Exit codes: 0 ok; 1 a check failed; 2 error.

set -u

fail() {
    echo "quit_after_import: $*" >&2
    exit 2
}

[ $# -eq 3 ] || fail "usage: quit_after_import.sh <binary> <image.cue|.bin> <workdir>"
game=$(realpath -e -- "$1" 2>/dev/null) && [ -x "$game" ] || fail "not an executable: $1"
image=$(realpath -e -- "$2" 2>/dev/null) && [ -f "$image" ] || fail "no such image: $2"
workdir=$(realpath -e -- "$3" 2>/dev/null) && [ -d "$workdir" ] || fail "no such directory: $3"

run="$workdir/quit-after-import"
rm -rf -- "$run"
mkdir -p -- "$run" || fail "cannot create $run"
for entry in "$(dirname -- "$game")"/*; do
    name=$(basename -- "$entry")
    case $name in
        # data/ is where the game looks for an image by itself: the no-image check needs it absent.
        gamedata | gamedata.legacy | data | config.cfg | "$(basename -- "$game")") ;;
        *) ln -s -- "$entry" "$run/$name" ;;
    esac
done
cp -- "$game" "$run/" || fail "cannot copy the executable"
cp -- "$(dirname -- "$game")/config.cfg" "$run/config.cfg" 2>/dev/null
exe="$run/$(basename -- "$game")"

export ALSOFT_DRIVERS=null SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen
status=0
check() { # <name> <expected code> <args...>
    local name=$1 want=$2
    shift 2
    (cd "$run" && timeout 600 "$exe" --dialogs-to-console --quit-after-import "$@" >"$run/$name.log" 2>&1)
    local got=$?
    if [ "$got" -eq "$want" ]; then
        echo "ok   $name (exit $got)"
    else
        echo "FAIL $name: exit $got, expected $want (log: $run/$name.log)"
        status=1
    fi
}

# No image anywhere: the import cannot start.
check no-image 1 -data "$run/empty" --image "$run/no-such-image.cue"
# A fresh import, then nothing left to do; neither run starts the game.
check import 0 -data "$run/gamedata" --image "$image"
[ -f "$run/gamedata/version.cfg" ] || {
    echo "FAIL import: no version.cfg in $run/gamedata"
    status=1
}
check already-imported 0 -data "$run/gamedata"
if grep -q "ends here" "$run/import.log" && grep -q "ends here" "$run/already-imported.log"; then
    echo "ok   the game did not start"
else
    echo "FAIL the runs did not report the early exit"
    status=1
fi

rm -rf -- "$run/gamedata" "$run/empty"
exit $status
