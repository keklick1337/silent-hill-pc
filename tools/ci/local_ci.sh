#!/usr/bin/env bash
# The GitHub workflows of .github/workflows/ run locally in Docker, step by step as the yml files have them:
#
#   linux    build-linux.yml    in ubuntu:24.04 as a sudo user "runner", like the ubuntu-24.04 runner
#   windows  build-windows.yml  in debian:trixie as root, the job's own container
#   checks   check.yml          in python:3.14, standing in for actions/setup-python 3.14
#
# Usage: tools/ci/local_ci.sh [-j <cpus>] [--fresh] [linux|windows|checks|all]...   (default: all)
#
#   -j       CPUs the container may use, so that "nproc" inside sees them (default 8)
#   --fresh  drop the dependency caches of the chosen jobs first
#
# The job gets a clone of the repository's HEAD with the working tree laid over it, so uncommitted changes are
# built too (the version then ends in "-dirty"). Nothing is installed on the host: the runner is a Docker image
# (sh-ci-ubuntu-runner), the apt downloads, the workspace and the caches of actions/cache are Docker volumes named
# sh-ci-*; the log of each job goes to .tmp/local-ci/<job>.log. The steps are transcribed from the yml files: a change there belongs here as well.
#
# What the ubuntu-24.04 runner image has preinstalled and build-linux.yml relies on is installed into the local
# runner image: sudo, git, curl, ca-certificates, python3, unzip, xz-utils, file and build-essential (make and the
# host compiler of FFmpeg's configure).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CPUS=8
FRESH=0
JOBS_TO_RUN=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        -j) CPUS="$2"; shift 2 ;;
        --fresh) FRESH=1; shift ;;
        linux|windows|checks) JOBS_TO_RUN+=("$1"); shift ;;
        all) JOBS_TO_RUN+=(checks linux windows); shift ;;
        -h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "local_ci: unknown argument $1" >&2; exit 2 ;;
    esac
done
[[ ${#JOBS_TO_RUN[@]} -gt 0 ]] || JOBS_TO_RUN=(checks linux windows)
command -v docker >/dev/null || { echo "local_ci: docker is needed" >&2; exit 2; }

OUT="$ROOT/.tmp/local-ci"
mkdir -p "$OUT"
CPUSET="0-$((CPUS - 1))"

# The checkout: a bundle of HEAD with its tags (fetch-depth: 0) and a tar of the working tree as git sees it.
prepare_source() {  # job
    rm -f "$OUT/$1.bundle" "$OUT/$1.tar"
    git -C "$ROOT" bundle create --quiet "$OUT/$1.bundle" HEAD --tags 2>/dev/null \
        || git -C "$ROOT" bundle create --quiet "$OUT/$1.bundle" HEAD
    (cd "$ROOT" && git ls-files -z -co --exclude-standard | xargs -0 -r sh -c 'for f; do [ -e "$f" ] && printf "%s\0" "$f"; done' _ \
        | COPYFILE_DISABLE=1 tar --null -cf "$OUT/$1.tar" -T -)  # macOS tar: no ._ files of extended attributes
}

# actions/checkout: the clone into $GITHUB_WORKSPACE.
CHECKOUT='
checkout() {
rm -rf "$GITHUB_WORKSPACE" && mkdir -p "$GITHUB_WORKSPACE" "$RUNNER_TEMP"
git -c init.defaultBranch=main -c advice.detachedHead=false clone --quiet /in/repo.bundle "$GITHUB_WORKSPACE"
cd "$GITHUB_WORKSPACE"
git ls-files -z | xargs -0 -r rm -f
tar -xf /in/tree.tar
}
'

# actions/cache: restore <path> from /cache/<key> before, save it after a successful job when it was a miss.
CACHE_FUNCS='
cache_restore() {  # key path [output variable]
    if [ -d "/cache/$1" ]; then mkdir -p "$(dirname "$2")"; rm -rf "$2"; cp -a "/cache/$1" "$2"; echo "cache: hit $1"; return 0; fi
    echo "cache: miss $1"; echo "$1|$2" >>/tmp/cache-save; return 1
}
cache_save_all() {
    [ -f /tmp/cache-save ] || return 0
    while IFS="|" read -r key path; do
        [ -d "$path" ] || continue
        rm -rf "/cache/$key.tmp"; cp -a "$path" "/cache/$key.tmp"; mv "/cache/$key.tmp" "/cache/$key"; echo "cache: saved $key"
    done </tmp/cache-save
}
hash_files() { (cd "$GITHUB_WORKSPACE" && cat "$@" 2>/dev/null | sha256sum | cut -c1-16); }
step() { echo; echo "::: step: $*"; }
'

run_job() {
    local job="$1" image="$2" user="$3" steps="$4"
    local log="$OUT/$job.log"
    [[ $FRESH -eq 1 ]] && docker volume rm -f "sh-ci-$job-cache" >/dev/null
    echo "local_ci: $job (log: .tmp/local-ci/$job.log)"
    prepare_source "$job"
    local started=$SECONDS
    set +e
    docker run --rm -i --cpuset-cpus "$CPUSET" \
        -v "$OUT/$job.bundle:/in/repo.bundle:ro" -v "$OUT/$job.tar:/in/tree.tar:ro" \
        -v "sh-ci-$job-cache:/cache" -v "sh-ci-$job-apt:/var/cache/apt/archives" \
        -v "sh-ci-$job-work:/__w" \
        "$image" bash -s >"$log" 2>&1 <<EOF
set -e
rm -f /etc/apt/apt.conf.d/docker-clean
mkdir -p /__w
if [ "$user" != root ]; then chown "$user:$user" /__w /cache; fi
cat >/tmp/job.sh <<'JOB'
set -euo pipefail
export GITHUB_WORKSPACE=/__w/silent-hill-pc RUNNER_TEMP=/__w/_temp GITHUB_OUTPUT=/__w/_temp/github_output CI=true
$CACHE_FUNCS
$CHECKOUT
$steps
cache_save_all
echo "::: job passed"
JOB
if [ "$user" = root ]; then nice -n 10 bash /tmp/job.sh; else nice -n 10 runuser -u "$user" -- bash /tmp/job.sh; fi
EOF
    local rc=$?
    set -e
    if [[ $rc -eq 0 ]]; then
        echo "local_ci: $job ok ($((SECONDS - started)) s)"
    else
        echo "local_ci: $job FAIL ($((SECONDS - started)) s), last step: $(grep '^::: step' "$log" | tail -n1 | cut -c11-)"
    fi
    return $rc
}

# The ubuntu-24.04 runner as far as build-linux.yml relies on it (see the head of this file).
runner_image() {
    docker build -q -t sh-ci-ubuntu-runner - >/dev/null <<'EOF'
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        sudo git curl ca-certificates python3 unzip xz-utils file build-essential \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -m -s /bin/bash runner && echo 'runner ALL=(ALL) NOPASSWD:ALL' >/etc/sudoers.d/runner
EOF
}

# --- build-linux.yml ------------------------------------------------------------------------------------------
LINUX_STEPS='
export CC=gcc-14 CXX=g++-14 ALSOFT_DRIVERS=null SDL_AUDIODRIVER=dummy SDL_AUDIO_DRIVER=dummy SDL_VIDEODRIVER=offscreen
step Checkout
checkout
step Install system packages
sudo apt-get update -q
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends \
  g++-14 cmake ninja-build pkg-config zip nasm desktop-file-utils \
  libgl-dev libegl-dev libgles-dev \
  glslang-tools \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
  libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols \
  libdecor-0-dev libdrm-dev libgbm-dev libudev-dev libdbus-1-dev \
  libpulse-dev libasound2-dev libpipewire-0.3-dev
step Cache dependency sources
cache_restore "deps-src-$(hash_files tools/win-deps/fetch.sh)" third_party/deps/src || true
step Fetch dependency sources
tools/win-deps/fetch.sh
ln -sfn deps/src/SDL3 third_party/SDL3
step Cache Linux dependencies
hit=0
cache_restore "linux-deps-ubuntu-24.04-gcc14-$(hash_files tools/win-deps/fetch.sh tools/linux-deps/build.sh tools/ffmpeg/build.sh tools/ncnn/build.sh tools/opus/build.sh)" third_party/deps/linux-x86_64 && hit=1
step Build dependencies
[ "$hit" = 1 ] || tools/linux-deps/build.sh
step Configure
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc"
step Build
cmake --build build --parallel "$(nproc)"
step Unit tests
(cd build/tests && ./sh_tests)
step Desktop entry
(
export HOME="$RUNNER_TEMP/desktop-home" XDG_DATA_HOME=
mkdir -p "$HOME"
build/SilentHillPC --install-desktop
desktop-file-validate "$HOME/.local/share/applications/silent-hill-pc.desktop"
test -f "$HOME/.local/share/icons/hicolor/48x48/apps/silent-hill-pc.png"
build/SilentHillPC --uninstall-desktop
test ! -e "$HOME/.local/share/applications/silent-hill-pc.desktop"
)
step Version
v="$(git describe --tags --always --dirty)"
echo "version=$v" >>"$GITHUB_OUTPUT"
step Package
tools/ci/package.sh build linux "$v" dist
step Check the package
python3 -I tools/ci/check_package.py check dist/*.tar.gz linux --run
'

# --- build-windows.yml ----------------------------------------------------------------------------------------
WINDOWS_STEPS='
export WINEDEBUG=-all
step Install toolchain
dpkg --add-architecture i386
apt-get update -q
DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends \
  ca-certificates git cmake ninja-build make pkg-config nasm zip unzip python3 curl 7zip \
  gcc libc6-dev \
  gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 \
  glslang-tools wine wine64
update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix
update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix
step Checkout
checkout
# On GitHub the runner user (uid 1001) owns $RUNNER_TEMP while the container runs as root.
chown 1001:1001 "$RUNNER_TEMP"
step Trust the checkout
git config --global --add safe.directory "$GITHUB_WORKSPACE"
step Cache Windows dependencies
cache_restore "win-deps-trixie-$(hash_files tools/win-deps/*.sh tools/ffmpeg/build.sh tools/ncnn/build.sh tools/opus/build.sh cmake/toolchains/mingw-w64-x86_64.cmake)" third_party/deps || true
step Build dependencies
tools/win-deps/build.sh
step Configure
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake
step Build
cmake --build build-win --parallel "$(nproc)"
step Unit tests \(wine\)
(
cd build-win/tests
export WINEPREFIX=/tmp/wineprefix
wineboot -i
wine reg add "HKCU\\Environment" /v SDL_AUDIO_DRIVER /d dummy /f
wine reg add "HKCU\\Environment" /v ALSOFT_DRIVERS /d null /f
wineserver -w
wine sh_tests.exe
)
step Version
v="$(git describe --tags --always --dirty)"
echo "version=$v" >>"$GITHUB_OUTPUT"
step Package
tools/ci/package.sh build-win windows "$v" dist
step Check the package
export WINEPREFIX=/tmp/wineprefix
python3 -I tools/ci/check_package.py check dist/*.zip windows --run
'

# --- check.yml ------------------------------------------------------------------------------------------------
CHECKS_STEPS='
step Checkout
checkout
step Pillow
python3 -m pip install -q --root-user-action=ignore pillow==12.0.0
for c in "check_naming.py --strict" check_map_handlers.py "gen_text.py --check" "gen_map_tables.py --check" \
         "gen_flags.py --check" "gen_paper_maps.py --check" "gen_weapon_rig.py --check" \
         "gen_unicode_tables.py --check" "build_input_glyphs.py --check" "lua_api_docs.py markdown --check" \
         check_docs.py; do
    step "$c"
    python3 -I tools/scripts/$c
done
'

status=0
for job in "${JOBS_TO_RUN[@]}"; do
    case "$job" in
        linux) runner_image; run_job linux sh-ci-ubuntu-runner runner "$LINUX_STEPS" || status=1 ;;
        windows) run_job windows debian:trixie root "$WINDOWS_STEPS" || status=1 ;;
        checks) run_job checks python:3.14 root "$CHECKS_STEPS" || status=1 ;;
    esac
done
exit $status
