#!/usr/bin/env bash
# Boots the game straight onto each map (New Game on NORMAL at the config `map`), drives it
# with simple walking input for a fixed time and records whether it survived, the error
# lines of its log and two screenshots (the engine's own `shot` of the composed frame).
#
# Video: SDL_VIDEODRIVER=offscreen, the real GPU with no
# window; a private Xvfb only as a fallback (tests/smoke/xvfb.sh). The walking input is a
# virtual pad (SH_REPLAY_INPUT, paced to the wall clock), so no X input is needed.
#
# Usage: map_sweep.sh [-t seconds] [-j jobs] [-d gamedata] [-k] [-g] [-x key=value]
#                     [-r WxH] [-v auto|offscreen|xvfb] <build_dir> <disc.bin> <out_dir> [map...]
#
#   -t  seconds per map (default 60)
#   -j  parallel games, 1..10 (default 6); on the Xvfb fallback each run takes the first free
#       display in :280-:289, or in $SH_XVFB_DISPLAYS ("300-309") when set
#   -r  render size (default 1920x1080)
#   -v  video: offscreen, xvfb, or auto (default, or $SH_VIDEO): offscreen, and a run whose
#       SDL cannot start is repeated on Xvfb
#   -d  an imported gamedata/ to use; without it the image is imported once with
#       <build_dir>/tools/sh-import/sh-import into <out_dir>/gamedata and removed at the end
#   -k  keep the imported <out_dir>/gamedata
#   -g  run the game under gdb and keep the backtrace of a fatal signal
#   -x  extra config.cfg line for every run (repeatable), e.g. -x cd_speed_ps1=1
#   map...  default: all 43 maps
#
# The game reads config.cfg, gamedata/ and assets/ next to its executable, so every
# map gets a run directory with a hard link (or copy) of the executable, its own config.cfg
# and links to the rest.
#
# Writes <out_dir>/<map>/{run/,game.log,mid.png,end.png,gdb.txt} and <out_dir>/summary.tsv,
# and prints the table. Result: alive (killed at the limit), static (alive, but the two
# screenshots taken 20 s apart are identical: likely a hang), fatal (the overlay did not
# load and the game stopped), dialog (alive, but the log shows a [DIALOG] message box),
# crash:<signal>, exit:<code>.
# Exit codes: 0 every map alive; 1 some map is not alive; 2 error.

set -u

fail() {
    echo "map_sweep: $*" >&2
    exit 2
}

ALL_MAPS="map0_s00 map0_s01 map0_s02
map1_s00 map1_s01 map1_s02 map1_s03 map1_s04 map1_s05 map1_s06
map2_s00 map2_s01 map2_s02 map2_s03 map2_s04
map3_s00 map3_s01 map3_s02 map3_s03 map3_s04 map3_s05 map3_s06
map4_s00 map4_s01 map4_s02 map4_s03 map4_s04 map4_s05 map4_s06
map5_s00 map5_s01 map5_s02 map5_s03
map6_s00 map6_s01 map6_s02 map6_s03 map6_s04 map6_s05
map7_s00 map7_s01 map7_s02 map7_s03"

USAGE="usage: map_sweep.sh [-t seconds] [-j jobs] [-d gamedata] [-k] [-g] [-x key=value] [-r WxH] [-v auto|offscreen|xvfb] <build_dir> <disc.bin> <out_dir> [map...]"
secs=60
resolution=1920x1080
video=${SH_VIDEO:-auto}
jobs=6
gamedata=
keep=0
use_gdb=0
extra=()
while getopts "t:j:d:kgx:r:v:" opt; do
    case $opt in
        t) secs=$OPTARG ;;
        j) jobs=$OPTARG ;;
        d) gamedata=$OPTARG ;;
        k) keep=1 ;;
        g) use_gdb=1 ;;
        x) extra+=("$OPTARG") ;;
        r) resolution=$OPTARG ;;
        v) video=$OPTARG ;;
        *) fail "$USAGE" ;;
    esac
done
shift $((OPTIND - 1))
[ $# -ge 3 ] || fail "$USAGE"
case $secs in '' | *[!0-9]*) fail "bad -t: $secs" ;; esac
case $jobs in '' | *[!0-9]*) fail "bad -j: $jobs" ;; esac
[ "$secs" -ge 45 ] || fail "-t must be at least 45 (boot and map load take ~30 s)"
[ "$jobs" -ge 1 ] && [ "$jobs" -le 10 ] || fail "-j must be 1..10"
[[ $resolution =~ ^[0-9]+x[0-9]+$ ]] || fail "bad -r: $resolution (WIDTHxHEIGHT)"
res_w=${resolution%x*} res_h=${resolution#*x}
case $video in auto | offscreen | xvfb) ;; *) fail "bad -v: $video" ;; esac

build=$(realpath -e -- "$1" 2>/dev/null) && [ -d "$build" ] || fail "no build directory: $1"
image=$(realpath -e -- "$2" 2>/dev/null) && [ -f "$image" ] || fail "no disc image: $2"
out=$(realpath -m -- "$3") || fail "bad output directory: $3"
shift 3
maps=${*:-$ALL_MAPS}
game="$build/SilentHillPC"
[ -x "$game" ] || fail "no executable: $game"
[ -f "$build/config.cfg" ] || fail "no config.cfg in $build"
if [ -n "$gamedata" ]; then
    gamedata=$(realpath -e -- "$gamedata" 2>/dev/null) && [ -f "$gamedata/version.cfg" ] \
        || fail "not an imported gamedata/: $gamedata"
fi

for tool in compare timeout; do
    command -v "$tool" >/dev/null || fail "$tool is not installed"
done
[ "$video" = offscreen ] || [ "$video" = auto ] || command -v Xvfb >/dev/null || fail "Xvfb is not installed"
[ "$use_gdb" = 0 ] || command -v gdb >/dev/null || fail "gdb is not installed"

# shellcheck source=xvfb.sh
. "$(dirname -- "$(realpath -e -- "${BASH_SOURCE[0]}")")/xvfb.sh"
xvfb_range=280-289
if [ "$video" = xvfb ]; then
    free=0
    for n in $(xvfb_displays "$xvfb_range") ; do
        [ -e "/tmp/.X$n-lock" ] || [ -e "/tmp/.X11-unix/X$n" ] || free=$((free + 1))
    done
    [ "$free" -ge 1 ] || fail "no free display in ${SH_XVFB_DISPLAYS:-$xvfb_range}"
    [ "$jobs" -le "$free" ] || jobs=$free
fi
mkdir -p -- "$out" || fail "cannot create $out"

imported=
if [ -z "$gamedata" ]; then
    importer="$build/tools/sh-import/sh-import"
    [ -x "$importer" ] || fail "no $importer; pass -d with an imported gamedata/"
    gamedata="$out/gamedata"
    rm -rf -- "$gamedata"
    "$importer" "$image" --out "$gamedata" >"$out/import.log" 2>&1 \
        || fail "import failed, see $out/import.log"
    imported=$gamedata
fi
# The first run records the image it imported from; the game reads the disc through it.
grep -q '^image_path = ' "$gamedata/version.cfg" \
    || printf 'image_path = %s\n' "$image" >>"$gamedata/version.cfg"

prepare_run() {
    local map=$1 run=$2 entry
    rm -rf -- "$run"
    mkdir -p -- "$run" || return 1
    ln -- "$game" "$run/SilentHillPC" 2>/dev/null || cp -- "$game" "$run/SilentHillPC" || return 1
    for entry in "$build"/*; do
        case ${entry##*/} in
            SilentHillPC | config.cfg | CMakeFiles | src | tests | tools | *.a | *.ninja | *.cmake \
                | CMakeCache.txt | CTestTestfile.cmake | *.h | SilentHill*.log | logs | gamedata*) ;;
            *) ln -s -- "$entry" "$run/${entry##*/}" || return 1 ;;
        esac
    done
    ln -s -- "$gamedata" "$run/gamedata" || return 1
    {
        sed -e "s/^map = .*/map = $map/" \
            -e 's/^skip_intros = .*/skip_intros = 2/' \
            -e "s/^width = .*/width = $res_w/" \
            -e "s/^height = .*/height = $res_h/" \
            -e 's/^fullscreen = .*/fullscreen = 0/' \
            -e 's/^log_file = .*/log_file = 1/' \
            -e 's/^show_console = .*/show_console = 0/' \
            "$build/config.cfg"
        printf 'log_file_level = debug\n'
        local kv
        for kv in "${extra[@]}"; do
            printf '%s = %s\n' "${kv%%=*}" "${kv#*=}"
        done
    } >"$run/config.cfg"
    grep -q "^map = $map\$" "$run/config.cfg" && grep -q '^skip_intros = 2$' "$run/config.cfg"
}

# The walking input as a virtual pad (engine/devtools/replay.cpp): offscreen has no
# X input. Ticks are game frames, about 60 per second at the shipped fps_cap; boot, the
# automatic New Game and the map load end well before the first key. Repeats to the limit:
# forward, turn, forward, turn back, forward, back. The end tick lies past any limit (timeout
# stops the game), so the replay never ends the run.
write_input() {
    local file=$1 t=1200 last=$((secs * 60 + 600))
    {
        echo "# map_sweep walking input"
        echo "1 -"
        while [ "$t" -lt "$last" ]; do
            printf '%d Up\n%d Left\n%d Up\n%d -\n%d Right\n%d Up\n%d Down\n' \
                "$t" $((t + 180)) $((t + 216)) $((t + 336)) $((t + 366)) $((t + 438)) $((t + 558))
            t=$((t + 618))
        done
        printf '%d -\nend 100000000\n' "$t"
    } >"$file"
}

# One start of the game for run_map, on video $1 (offscreen or xvfb). Sets rc.
launch() {
    local mode=$1 dir=$2 run=$3 xvfb_pid= game_pid
    local -a venv=(-u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen)
    if [ "$mode" = xvfb ]; then
        # A display per run, picked when the run starts: one held across runs could be taken
        # by another harness in the gap, and two runs of this sweep never share one.
        if ! xvfb_start "$xvfb_range" "${resolution}x24" "$dir/xvfb.log"; then
            rc=xvfb-failed
            return
        fi
        xvfb_pid=$XVFB_PID
        venv=(-u WAYLAND_DISPLAY DISPLAY=":$XVFB_DISPLAY" SDL_VIDEODRIVER=x11)
    fi
    venv+=(SDL_AUDIODRIVER=dummy ALSOFT_DRIVERS=null SH_DIALOGS_TO_CONSOLE=1
        SH_REPLAY_INPUT="$dir/input.replay" SH_REPLAY_PACED=1)
    if [ "$use_gdb" = 1 ]; then
        # gdb exits on its own after a fatal signal (batch mode); timeout ends a live run.
        (cd -- "$run" && exec env "${venv[@]}" \
            timeout -k 5 "$secs" gdb -q -batch -nx \
            -ex 'set pagination off' -ex 'handle SIGPIPE nostop noprint' \
            -ex 'handle SIG32 SIG33 SIG34 SIG35 nostop noprint' \
            -ex run -ex 'bt 40' -ex 'info registers rip' -ex 'x/6i $pc' \
            --args "$run/SilentHillPC" -consolecmd "$dir/cmds.txt") >"$dir/gdb.txt" 2>&1 &
    else
        (cd -- "$run" && exec env "${venv[@]}" \
            timeout -k 5 "$secs" "$run/SilentHillPC" -consolecmd "$dir/cmds.txt") >"$dir/stdout.txt" 2>&1 &
    fi
    game_pid=$!
    wait "$game_pid"
    rc=$?
    if [ -n "$xvfb_pid" ]; then
        kill "$xvfb_pid" 2>/dev/null
        wait "$xvfb_pid" 2>/dev/null
    fi
}

run_map() {
    local map=$1
    local dir="$out/$map" run="$out/$map/run"
    local rc result mode
    mkdir -p -- "$dir"
    rm -f -- "$dir/mid.png" "$dir/end.png" "$dir/game.log" "$dir/gdb.txt" "$dir/stdout.txt"
    if ! prepare_run "$map" "$run"; then
        printf '%s\tsetup-failed\t-\t-\t-\n' "$map" >"$dir/result.tsv"
        return
    fi
    write_input "$dir/input.replay"
    # mid.png at secs-21 and end.png at secs-4 of game time, from the engine's own frame
    # (absolute paths: SHOT writes relative to the run directory). Identical frames under
    # steady input mean the game is stuck.
    printf '@%d SHOT %s\n@%d SHOT %s\n' \
        $((secs - 21)) "$(realpath -m -- "$dir/mid.png")" \
        $((secs - 4)) "$(realpath -m -- "$dir/end.png")" >"$dir/cmds.txt"

    mode=$video
    [ "$mode" = auto ] && mode=offscreen
    launch "$mode" "$dir" "$run"
    if [ "$video" = auto ] && [ "$mode" = offscreen ] \
        && grep -aqs 'Failed to initialise SDL' "$dir/stdout.txt" "$dir/gdb.txt" "$run"/logs/*.log; then
        echo "map_sweep: $map: offscreen video failed, again on Xvfb" >&2
        command -v Xvfb >/dev/null && launch xvfb "$dir" "$run"
    fi
    if [ "$rc" = xvfb-failed ]; then
        printf '%s\txvfb-failed\t-\t-\t-\n' "$map" >"$dir/result.tsv"
        return
    fi

    local logf
    # The run's own log: <exe>/logs/silenthill-<time>.log (older builds: <exe>/SilentHill*.log).
    logf=$(ls -t "$run"/logs/silenthill-*.log "$run"/SilentHill*.log 2>/dev/null | head -1)
    [ -n "$logf" ] && cp -- "$logf" "$dir/game.log"

    if [ "$use_gdb" = 1 ] && grep -q "received signal" "$dir/gdb.txt" 2>/dev/null; then
        result="crash:$(grep -m1 -o 'received signal [A-Z0-9]*' "$dir/gdb.txt" | awk '{print $3}')"
    else
        case $rc in
            124 | 137) result=alive ;;
            1[2-9][0-9]) result="crash:$(kill -l $((rc - 128)) 2>/dev/null || echo $rc)" ;;
            *) result="exit:$rc" ;;
        esac
    fi
    if [ "$result" = alive ] && [ -f "$dir/mid.png" ] && [ -f "$dir/end.png" ]; then
        local ae
        # RGB only: the alpha channel is no part of the picture.
        ae=$(compare -alpha off -metric AE "$dir/mid.png" "$dir/end.png" null: 2>&1 | awk '{print $1}')
        [ "$ae" = 0 ] && result=static
    fi
    # The map did not load and the game stopped with a message box (engine/loader/map_list.cpp).
    # A message box (SH_DIALOGS_TO_CONSOLE prints it instead of blocking) is a failure too.
    [ "$result" = alive ] && grep -aqs '\[DIALOG\] ' "$dir/game.log" "$dir/stdout.txt" && result=dialog
    grep -aqE ' map [^ ]+ \(number -?[0-9]+\) could not be loaded' "$dir/game.log" 2>/dev/null && result=fatal

    local active="-" errors=0 first="-"
    if [ -f "$dir/game.log" ]; then
        active=$(grep -aoE 'map map[0-9]_s[0-9][0-9] \(.*\) loaded: number' "$dir/game.log" | awk '{print $2}' \
            | paste -sd, -)
        [ -n "$active" ] || active="-"
        # Error-level log lines and hard failures only: info lines such as "0 relocation errors" are not
        # errors.
        local pattern='^[^ ]+ +f[^ ]+ +[EF] |FATAL|stack traceback|\[DIALOG\]|abort|assert'
        errors=$(grep -acE "$pattern" "$dir/game.log")
        first=$(grep -aE -m1 "$pattern" "$dir/game.log" | tr '\t' ' ' | cut -c1-120)
        [ -n "$first" ] || first="-"
    fi
    printf '%s\t%s\t%s\t%s\t%s\n' "$map" "$result" "$active" "$errors" "$first" >"$dir/result.tsv"
}

worker() {
    local slot=$1 idx=0 map
    for map in $maps; do
        if [ $((idx % jobs)) -eq "$slot" ]; then
            run_map "$map"
            echo "map_sweep: $(cut -f1,2 "$out/$map/result.tsv" | tr '\t' ' ')" >&2
        fi
        idx=$((idx + 1))
    done
}

pids=()
cleanup() {
    for p in "${pids[@]}"; do
        pkill -TERM -P "$p" 2>/dev/null
        kill "$p" 2>/dev/null
    done
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

for ((s = 0; s < jobs; s++)); do
    worker "$s" &
    pids+=($!)
done
wait
pids=()
[ -n "$imported" ] && [ "$keep" = 0 ] && rm -rf -- "$imported"

summary="$out/summary.tsv"
printf 'map\tresult\tactive_maps\tlog_errors\tfirst_error\n' >"$summary"
bad=0
for map in $maps; do
    if [ -f "$out/$map/result.tsv" ]; then
        cat -- "$out/$map/result.tsv" >>"$summary"
        [ "$(cut -f2 "$out/$map/result.tsv")" = alive ] || bad=$((bad + 1))
    else
        printf '%s\tnot-run\t-\t-\t-\n' "$map" >>"$summary"
        bad=$((bad + 1))
    fi
done
column -t -s $'\t' "$summary"
total=$(($(wc -l <"$summary") - 1))
echo "map_sweep: $((total - bad))/$total alive" >&2
[ "$bad" -eq 0 ]
