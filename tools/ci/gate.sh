#!/usr/bin/env bash
# The local verification of docs/DEVELOPMENT.md, section 4, in one run: build, unit tests, static checks and, with
# a disc image, the import, the replay check, the map sweep, the map scenarios and a start-and-quit of the game.
#
# Usage: tools/ci/gate.sh [-b <build dir>] [-i <image.cue|.bin>] [-j <jobs>] [-g <games>] [-w <work dir>] [-s]
#
#   -b  build directory, already configured with CMake (default: build)
#   -i  the disc image; without it only the steps that need no game run (build, unit tests, static checks)
#   -j  build jobs (default 4)
#   -g  games run at once by the sweep and the scenarios, 1..10 (default 8)
#   -w  work directory for logs and run directories (default: .tmp/gate); it is created and reused
#   -s  static only: skip the build and the unit tests as well
#
# The game runs without a window and without sound (SDL_VIDEODRIVER=offscreen on the real GPU). Every step prints
# one line, "ok" or "FAIL" with the log to read. Exit code: 0 all steps passed, 1 a step failed, 2 usage.

set -u

build=build
image=
jobs=4
games=8
work=.tmp/gate
static_only=0
while getopts "b:i:j:g:w:sh" opt; do
    case "$opt" in
        b) build=$OPTARG ;;
        i) image=$OPTARG ;;
        j) jobs=$OPTARG ;;
        g) games=$OPTARG ;;
        w) work=$OPTARG ;;
        s) static_only=1 ;;
        h) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) sed -n '5p' "$0" | sed 's/^# //' >&2; exit 2 ;;
    esac
done

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd) || exit 2
cd -- "$root" || exit 2
mkdir -p -- "$work" || exit 2
work=$(cd -- "$work" && pwd)
case "$build" in /*) ;; *) build=$root/$build ;; esac
if [ -n "$image" ]; then
    [ -f "$image" ] || { echo "gate: no disc image at $image" >&2; exit 2; }
    image=$(cd -- "$(dirname -- "$image")" && pwd)/$(basename -- "$image")
fi

export SDL_VIDEODRIVER=offscreen ALSOFT_DRIVERS=null SDL_AUDIODRIVER=dummy
failed=0

# step <name> <command...>: runs the command with its output in <work>/<name>.log.
step() {
    local name=$1
    shift
    if "$@" >"$work/$name.log" 2>&1; then
        echo "ok    $name"
    else
        echo "FAIL  $name  (see $work/$name.log)"
        failed=1
    fi
}

py() { python3 -I "$@"; }

# 1 and 2: build and unit tests.
if [ "$static_only" -eq 0 ]; then
    [ -f "$build/build.ninja" ] || [ -f "$build/Makefile" ] || {
        echo "gate: $build is not a configured build directory (cmake -S . -B $build -G Ninja)" >&2
        exit 2
    }
    step build nice -n 19 cmake --build "$build" -j "$jobs"
    [ "$failed" -eq 0 ] || exit 1
    step unit-tests "$build/tests/sh_tests"
fi

# 3: static checks.
step naming py tools/scripts/check_naming.py --strict
step map-handlers py tools/scripts/check_map_handlers.py
step text py tools/scripts/gen_text.py --check
step map-tables py tools/scripts/gen_map_tables.py --check
step flags py tools/scripts/gen_flags.py --check
step paper-maps py tools/scripts/gen_paper_maps.py --check
step weapon-rigs py tools/scripts/gen_weapon_rig.py --check
step input-glyphs py tools/scripts/build_input_glyphs.py --check
step unicode-tables py tools/scripts/gen_unicode_tables.py --check
step lua-api-docs py tools/scripts/lua_api_docs.py markdown --check
step docs py tools/scripts/check_docs.py

if [ -z "$image" ] || [ "$static_only" -ne 0 ]; then
    [ -n "$image" ] || echo "gate: no disc image (-i): the steps that run the game are skipped"
    exit "$failed"
fi

# The map sweep reads the .bin; a .cue names it by the same file name.
bin=$image
case "$image" in *.cue | *.CUE) bin=${image%.*}.bin ;; esac
[ -f "$bin" ] || { echo "gate: no $bin beside the cue sheet" >&2; exit 2; }

# 4: import once into the build directory.
import_once() {
    (cd -- "$build" && ./SilentHillPC --dialogs-to-console --image "$image" --quit-after-import)
}
step import import_once
[ "$failed" -eq 0 ] || exit 1
step anim-clips py tools/scripts/check_anim_clips.py "$build/gamedata/tables/anim"

# 5: replay.
step replay py tools/replay/sh_replay.py check --build "$build" --work-dir "$work/replay"

# 6: map sweep, on a copy of the game data: the sweep does not import into a build directory that has one.
sweep() {
    rm -rf -- "$work/sweep" "$work/gamedata" || return 1
    cp -a -- "$build/gamedata" "$work/gamedata" || return 1
    tests/smoke/map_sweep.sh -j "$games" -d "$work/gamedata" "$build" "$bin" "$work/sweep" || return 1
    # A map that stayed alive may still have logged an error.
    local errors
    errors=$(grep -rhE '^\S+ +f\S+ +[EF] |FATAL|\[DIALOG\]|stack traceback|corrupted' "$work/sweep" \
        --include=game.log | wc -l)
    echo "error lines in the maps' logs: $errors"
    [ "$errors" -eq 0 ]
}
step map-sweep sweep
rm -rf -- "$work/gamedata"

# 7: map scenarios.
step map-scenarios py tools/replay/handler_diff.py run --build "$build" --out "$work/handler-diff" -j "$games" --repeat

# The game starts on its own configuration and leaves by the console's quit without an error line.
start_and_quit() {
    printf '@3 quit\n' >"$work/quit.cmd"
    (cd -- "$build" && timeout -k 5 60 ./SilentHillPC --dialogs-to-console -consolecmd "$work/quit.cmd") || return 1
}
quit_clean() {
    start_and_quit >"$work/quit.out" 2>&1 || { cat "$work/quit.out"; return 1; }
    cat "$work/quit.out"
    ! grep -qE '^\S+ +f\S+ +[EF] |FATAL|\[DIALOG\]|corrupted' "$work/quit.out"
}
step start-and-quit quit_clean

exit "$failed"
