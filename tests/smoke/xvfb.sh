# Sourced by smoke.sh and map_sweep.sh: start a private Xvfb on a free display (their fallback when
# SDL_VIDEODRIVER=offscreen is unavailable; never the session display).
#
# xvfb_start <default-range> <geometry> <log>
#   Tries the displays of $SH_XVFB_DISPLAYS ("120-129", "120,121,125" or "120 121"), else
#   <default-range> ("90-99"). Skips any number with /tmp/.X<n>-lock or /tmp/.X11-unix/X<n>,
#   starts Xvfb and keeps it only if its socket is up AND the lock file holds its own pid:
#   Xvfb creates that file atomically, so it is the only proof the server on :n is ours
#   when another harness picks the same number at the same moment.
#   On success sets XVFB_DISPLAY and XVFB_PID and returns 0; otherwise returns 1.

xvfb_displays() {
    local spec=${SH_XVFB_DISPLAYS:-$1} part a b
    local IFS=$', \t\n'
    for part in $spec; do
        case $part in
            *-*) a=${part%-*} b=${part#*-} ;;
            *) a=$part b=$part ;;
        esac
        case $a$b in '' | *[!0-9]*) return 1 ;; esac
        seq "$a" "$b"
    done
}

xvfb_owns() {
    local n=$1 pid=$2 held
    held=$(tr -d ' \n' <"/tmp/.X$n-lock" 2>/dev/null) || return 1
    [ "$held" = "$pid" ]
}

xvfb_start() {
    local range=$1 geometry=$2 log=$3 n pid i
    XVFB_DISPLAY= XVFB_PID=
    for n in $(xvfb_displays "$range"); do
        [ -e "/tmp/.X$n-lock" ] || [ -e "/tmp/.X11-unix/X$n" ] && continue
        Xvfb ":$n" -screen 0 "$geometry" -nolisten tcp >"$log" 2>&1 &
        pid=$!
        for ((i = 0; i < 50; i++)); do
            [ -S "/tmp/.X11-unix/X$n" ] && xvfb_owns "$n" "$pid" && break
            kill -0 "$pid" 2>/dev/null || break
            sleep 0.1
        done
        if kill -0 "$pid" 2>/dev/null && [ -S "/tmp/.X11-unix/X$n" ] && xvfb_owns "$n" "$pid"; then
            XVFB_DISPLAY=$n XVFB_PID=$pid
            return 0
        fi
        kill "$pid" 2>/dev/null
        wait "$pid" 2>/dev/null
    done
    return 1
}
