#!/usr/bin/env bash
# Runs the game for a while, takes the engine's own screenshot (`shot`, the composed frame,
# opaque) near the end and, if a reference is given, compares it with it (RGB only).
#
# Usage: smoke.sh [-r WxH] [-v auto|offscreen|xvfb] [-a game-arg]...
#                 <binary> <workdir> <seconds> <out.png> [reference.png] [threshold]
#
#   -r  render size (default 1920x1080). The game reads config.cfg next to its executable, so
#       the run uses <workdir>/smoke-run/: a hard link (or copy) of the executable, links to
#       the rest of its directory and a copy of its config.cfg with width/height/fullscreen
#       replaced. A <binary> that is a wrapper script runs as it is, at its config's size.
#   -v  video: offscreen = SDL_VIDEODRIVER=offscreen, the
#       real GPU with no window; xvfb = a private Xvfb (Mesa llvmpipe) on the first free
#       display in :290-:299 or $SH_XVFB_DISPLAYS; auto (default, or $SH_VIDEO) = offscreen,
#       and again on Xvfb when SDL cannot start.
#   -a  extra argument for the game (repeatable), e.g. -a -skiptogame
#
# Exit codes: 0 ok; 1 screenshot differs from the reference by more than threshold;
# 2 error; 3 the game exited before the time limit; 77 skipped (offscreen failed, no Xvfb).

set -u

fail() {
    echo "smoke: $*" >&2
    exit 2
}

USAGE="usage: smoke.sh [-r WxH] [-v auto|offscreen|xvfb] [-a game-arg]... <binary> <workdir> <seconds> <out.png> [reference.png] [threshold]"
resolution=1920x1080
video=${SH_VIDEO:-auto}
game_args=()
while getopts "r:v:a:" opt; do
    case $opt in
        r) resolution=$OPTARG ;;
        v) video=$OPTARG ;;
        a) game_args+=("$OPTARG") ;;
        *) fail "$USAGE" ;;
    esac
done
shift $((OPTIND - 1))
if [ $# -lt 4 ] || [ $# -gt 6 ]; then
    fail "$USAGE"
fi
[[ $resolution =~ ^[0-9]+x[0-9]+$ ]] || fail "bad -r: $resolution (WIDTHxHEIGHT)"
case $video in auto | offscreen | xvfb) ;; *) fail "bad -v: $video" ;; esac

game=$(realpath -e -- "$1" 2>/dev/null) && [ -x "$game" ] || fail "not an executable: $1"
workdir=$(realpath -e -- "$2" 2>/dev/null) && [ -d "$workdir" ] || fail "no such directory: $2"
case $3 in
    '' | *[!0-9]*) fail "seconds must be a positive integer: $3" ;;
esac
secs=$((10#$3))
# timeout treats 0 as "no limit", so zero must not get through.
[ "$secs" -ge 1 ] || fail "seconds must be a positive integer: $3"
out=$(realpath -m -- "$4") || fail "bad output path: $4"
mkdir -p -- "$(dirname -- "$out")" || fail "cannot create $(dirname -- "$out")"
ref=
if [ $# -ge 5 ]; then
    ref=$(realpath -e -- "$5" 2>/dev/null) && [ -f "$ref" ] || fail "no reference image: $5"
fi
threshold=0
if [ $# -ge 6 ]; then
    case $6 in
        '' | *[!0-9]*) fail "threshold must be a non-negative integer: $6" ;;
    esac
    threshold=$6
fi

command -v timeout >/dev/null || fail "timeout is not installed"
if [ -n "$ref" ]; then
    for tool in compare identify; do
        command -v "$tool" >/dev/null || fail "$tool is not installed"
    done
fi
if [ "$video" = xvfb ] && ! command -v Xvfb >/dev/null; then
    echo "smoke: skipped, Xvfb is not installed" >&2
    exit 77
fi

# shellcheck source=xvfb.sh
. "$(dirname -- "$(realpath -e -- "${BASH_SOURCE[0]}")")/xvfb.sh"
game_pid=
xvfb_pid=
cleanup() {
    if [ -n "$game_pid" ]; then
        kill "$game_pid" 2>/dev/null
    fi
    if [ -n "$xvfb_pid" ]; then
        kill "$xvfb_pid" 2>/dev/null
        wait "$xvfb_pid" 2>/dev/null
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

xvfb_log=${out%.png}.xvfb.log
game_log=${out%.png}.game.log
cmd_file=${out%.png}.cmds

# The render size lives in config.cfg next to the executable: a private run directory.
exe=$game
run=
if [ "$(head -c 4 -- "$game" | od -An -c | tr -d ' ')" = '177ELF' ]; then
    gamedir=$(dirname -- "$game")
    run="$workdir/smoke-run"
    rm -rf -- "$run"
    mkdir -p -- "$run" || fail "cannot create $run"
    exe="$run/$(basename -- "$game")"
    ln -- "$game" "$exe" 2>/dev/null || cp -- "$game" "$exe" || fail "cannot copy $game"
    for entry in "$gamedir"/* "$gamedir"/.savegames; do
        [ -e "$entry" ] || continue
        case ${entry##*/} in
            "$(basename -- "$game")" | config.cfg | logs) ;;
            *) ln -s -- "$entry" "$run/${entry##*/}" || fail "cannot link $entry" ;;
        esac
    done
    if [ -f "$gamedir/config.cfg" ]; then
        sed -e "s/^width = .*/width = ${resolution%x*}/" -e "s/^height = .*/height = ${resolution#*x}/" \
            -e 's/^fullscreen = .*/fullscreen = 0/' "$gamedir/config.cfg" >"$run/config.cfg"
    else
        printf 'width = %s\nheight = %s\nfullscreen = 0\n' "${resolution%x*}" "${resolution#*x}" >"$run/config.cfg"
    fi
    # Xvfb renders on llvmpipe: the reference pictures are made with the config's graphics, not the CPU preset.
    printf 'software_gl = off\n' >>"$run/config.cfg"
else
    echo "smoke: $game is not an executable file of the game, the render size comes from its config" >&2
fi

# The engine writes SHOT relative to ITS cwd (the workdir); give it an absolute
# path so the PNG lands next to the requested output whatever the caller did.
# One line ("@N SHOT ..."): the movie player runs only a due SHOT, not the rest of the file.
printf '@%d SHOT %s\n' "$((secs > 1 ? secs - 1 : 1))" "$out" >"$cmd_file"

# One start of the game on video $1; sets rc.
launch() {
    local mode=$1
    local -a venv=(-u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen)
    if [ "$mode" = xvfb ]; then
        # Displays: $SH_XVFB_DISPLAYS, else :290-:299 (tests/smoke/xvfb.sh); never the session's.
        xvfb_start 290-299 "${resolution}x24" "$xvfb_log" \
            || fail "no free display in ${SH_XVFB_DISPLAYS:-290-299}, see $xvfb_log"
        xvfb_pid=$XVFB_PID
        venv=(-u WAYLAND_DISPLAY DISPLAY=":$XVFB_DISPLAY" SDL_VIDEODRIVER=x11)
    fi
    rm -f -- "$out"
    (cd -- "$workdir" && exec env "${venv[@]}" SDL_AUDIODRIVER=dummy ALSOFT_DRIVERS=null \
        SH_DIALOGS_TO_CONSOLE=1 timeout -k 5 "$secs" "$exe" -consolecmd "$cmd_file" "${game_args[@]}") \
        >"$game_log" 2>&1 &
    game_pid=$!
    wait "$game_pid"
    rc=$?
    game_pid=
    if [ -n "$xvfb_pid" ]; then
        kill "$xvfb_pid" 2>/dev/null
        wait "$xvfb_pid" 2>/dev/null
        xvfb_pid=
    fi
}

mode=$video
[ "$mode" = auto ] && mode=offscreen
launch "$mode"
if [ "$video" = auto ] && grep -aqs 'Failed to initialise SDL' "$game_log" "$run"/logs/*.log; then
    if ! command -v Xvfb >/dev/null; then
        echo "smoke: skipped, offscreen video failed and Xvfb is not installed, see $game_log" >&2
        exit 77
    fi
    echo "smoke: offscreen video failed, again on Xvfb" >&2
    launch xvfb
fi

# 124: timeout stopped the game at the limit.
# 137: the game ignored SIGTERM and timeout -k killed it.
case $rc in
    124 | 137) ;;
    *)
        echo "smoke: the game exited before the ${secs}s limit with status $rc, see $game_log" >&2
        exit 3
        ;;
esac
[ -s "$out" ] || fail "no screenshot $out (the engine's SHOT did not run), see $game_log"

if [ -z "$ref" ]; then
    echo "smoke: ok, screenshot $out"
    exit 0
fi

size_out=$(identify -format '%wx%h' "$out" 2>/dev/null)
size_ref=$(identify -format '%wx%h' "$ref" 2>/dev/null)
[ -n "$size_ref" ] || fail "cannot read reference image: $ref"
# compare -metric AE does not report a size mismatch; it compares only the overlap.
[ "$size_out" = "$size_ref" ] \
    || fail "reference is $size_ref, screenshot is $size_out: use the same -r"

# RGB only: an alpha channel in either file (some references) is no part of the picture.
result=$(compare -alpha off -metric AE "$out" "$ref" null: 2>&1)
crc=$?
# compare exits 1 when the images differ; the count is printed either way.
{ [ "$crc" -le 1 ] && [[ $result =~ ^[0-9]+ ]]; } || fail "compare failed: $result"
differing=${result%% *}
if [ "$differing" -gt "$threshold" ]; then
    echo "smoke: screenshot differs from $ref: $differing pixels, threshold $threshold" >&2
    exit 1
fi
echo "smoke: ok, $differing pixels differ from the reference (threshold $threshold)"
exit 0
