#!/usr/bin/env bash
# Checks that the game can be quit while the title screen's attract demo plays: it must end with exit code 0.
# A read that runs past the end of its buffer while the demo loads damages the heap, and such damage shows only
# when the heap is torn down at exit (abort, exit code 134): the quit is what proves there was none.
#
# Usage: quit_in_demo.sh <binary> <workdir> [seconds]
#
# The game needs imported game data beside <binary> (gamedata/version.cfg). The run uses <workdir>/quit-in-demo/:
# a copy of the executable and of its config.cfg (skip_intros = 1: straight to the title), links to the rest of
# its directory, its own log. The title is left alone until the demo starts; `quit` runs [seconds] after the start
# (default 45).
# Exit codes: 0 ok; 1 a check failed; 2 error.

set -u

fail() {
    echo "quit_in_demo: $*" >&2
    exit 2
}

[ $# -eq 2 ] || [ $# -eq 3 ] || fail "usage: quit_in_demo.sh <binary> <workdir> [seconds]"
game=$(realpath -e -- "$1" 2>/dev/null) && [ -x "$game" ] || fail "not an executable: $1"
workdir=$(realpath -e -- "$2" 2>/dev/null) && [ -d "$workdir" ] || fail "no such directory: $2"
seconds=${3:-45}
build=$(dirname -- "$game")
[ -f "$build/gamedata/version.cfg" ] || fail "no imported game data in $build/gamedata"

run="$workdir/quit-in-demo"
rm -rf -- "$run"
mkdir -p -- "$run" || fail "cannot create $run"
for entry in "$build"/*; do
    name=$(basename -- "$entry")
    case $name in
        config.cfg | logs | .savegames | mods_config.cfg | "$(basename -- "$game")") ;;
        *) ln -s -- "$entry" "$run/$name" ;;
    esac
done
cp -- "$game" "$run/" || fail "cannot copy the executable"
exe="$run/$(basename -- "$game")"
{
    grep -vE '^(skip_intros|log_file|log_categories|show_console|fullscreen)[[:space:]]*=' "$build/config.cfg" 2>/dev/null
    # fs=debug: the log then names every file read, the demo's recording among them.
    printf 'skip_intros = 1\nlog_file = 1\nlog_categories = fs=debug\nshow_console = 0\nfullscreen = 0\n'
} >"$run/config.cfg"
printf '@%s quit\n' "$seconds" >"$run/commands.txt"

export ALSOFT_DRIVERS=null SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen
(cd "$run" && timeout $((seconds + 120)) "$exe" --dialogs-to-console -consolecmd "$run/commands.txt" \
    >"$run/stdout.txt" 2>&1)
code=$?
log=$(ls -t "$run"/logs/*.log 2>/dev/null | head -n 1)

status=0
check() { # <description> <command...>
    local what=$1
    shift
    if "$@"; then
        echo "ok   $what"
    else
        echo "FAIL $what (exit code $code, log: ${log:-none}, output: $run/stdout.txt)"
        status=1
    fi
}
[ -n "$log" ] || fail "the game wrote no log in $run/logs (exit code $code)"
check "the demo's recording was read before the quit" grep -aq 'read MISC/PLAY[0-9A-F]*\.DAT' "$log"
check "the quit command ran" grep -aq '> quit' "$log"
check "no read ran past its buffer" sh -c '! grep -aq "does not fit its work buffer" "$1"' sh "$log"
check "no crash report" sh -c '! grep -aqs "\[CRASH\]" "$@"' sh "$log" "$run/stdout.txt" "$run/logs/crash.log"
check "exit code 0" [ "$code" -eq 0 ]
exit $status
