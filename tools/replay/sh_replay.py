#!/usr/bin/env python3
"""Replay harness driver: runs routes, compares state dumps, keeps the references.

Commands:
  run         one route -> one dump
  check       each route twice: the dumps must be byte-identical and match the reference
  update-ref  regenerate the stored reference of each route
  compare     first divergence between two dumps (.gz accepted)
  selftest    the comparison on synthetic dumps (no game)

Video: SDL_VIDEODRIVER=offscreen by default (the real GPU, no window, no X
input), a private Xvfb on :240+ only as a fallback (--video xvfb, $SH_VIDEO=xvfb, or offscreen
failing to start SDL). Screenshots are the engine's own `shot` of the composed frame.

Exit codes: 0 ok, 1 dumps differ, 2 error, 77 skipped (no disc image).
Run with python3 -I; standard library only.
"""

import argparse
import gzip
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROUTES_DIR = os.path.join(ROOT, 'tests', 'replay', 'routes')
REFERENCE_DIR = os.path.join(ROOT, 'tests', 'replay', 'reference')
BASE_CONFIG = os.path.join(ROOT, 'assets', 'config.cfg')
EXE_NAME = 'SilentHillPC.exe' if os.name == 'nt' else 'SilentHillPC'
DISPLAYS = range(240, 250)
DEFAULT_RESOLUTION = '1920x1080'
VIDEO_MODES = ('auto', 'offscreen', 'xvfb')
# What the game logs when SDL cannot start its video driver: auto mode then retries on Xvfb.
VIDEO_FAILURES = (b'Failed to initialise SDL',)
DISPLAYS_ENV = 'SH_XVFB_DISPLAYS'
EXIT_SKIP = 77
CONSOLE_FILE = 'console.txt'

# Settings every replay runs with, on top of assets/config.cfg. They pin what the
# config may change about logic or timing. The render size comes from
# --resolution (width/height, written by write_config): it changes no dump field.
PINNED_CONFIG = [
    ('fullscreen', '0'), ('vsync', '0'),
    ('refresh_rate', '0'), ('fps_cap', '30'), ('menu_fps_unlock', '0'),
    ('immersive_fps_head_tracking', '0'), ('enable_debug_log', '0'),
    # 1: stdout and stderr reach the run log instead of the null device.
    ('show_console', '1'),
    ('allow_debug_controls', '0'),
    ('control_style', 'classic'), ('movement_original', '1'),
    ('spu_renderer', 'authentic'), ('language', 'auto'),
    ('skip_intros', '0'), ('map', 'map0_s00'),
    # The quick setup waits for a player; a replay is answered by its route alone.
    ('quick_setup', '0'),
    # No mods: the run directory gets no mods/ (prepare_run_dir links only maps/, assets/, licenses/), and an
    # extra mods folder from the config is switched off, so a check never depends on what is installed.
    ('mods_dir', ''),
    # A private Xvfb renders on llvmpipe: the graphics stay those of the config, as on a GPU.
    ('software_gl', 'off'),
    # Stamped current: the default migrations (v9 moves fps_cap 30 -> 60) must not rewrite the pinned values.
    ('config_version', '9'),
]

# Sub-field names of the composite dump fields, for readable divergence reports.
CHARA_FIELDS = ['charaId', 'controlState', 'stateStep', 'anim.status', 'anim.keyframeIdx',
                'anim.time', 'anim.alpha', 'pos.x', 'pos.y', 'pos.z', 'rot.x', 'rot.y', 'rot.z',
                'moveSpeed', 'fallSpeed', 'headingAngle', 'health', 'flags', 'attackReceived',
                'deathTimer']
SUBFIELDS = {
    'gss': ['0', '1', '2'], 'sss': ['0', '1', '2'], 'c1c': ['0', '1', '2'],
    'cgf': ['0', '1', '2', '3'],
    'pl': CHARA_FIELDS,
    'plx': ['upper.anim.status', 'upper.anim.keyframeIdx', 'upper.anim.time',
            'upper.anim.alpha', 'state', 'upperBodyState', 'lowerBodyState', 'lastUsedItem'],
    'cam': ['pos.x', 'pos.y', 'pos.z', 'ang.x', 'ang.y', 'ang.z'],
}


# Lines of a game log the gates count as errors (docs/DEVELOPMENT.md section 4): an E-level log line, a Lua or
# behaviour error, a dialog the game would have shown the player (SH_DIALOGS_TO_CONSOLE).
LOG_ERROR = re.compile(rb'^\S+ f\d+\s+E\s|\[DIALOG\]|update error|[Ll]ua[^\n]*error|behavior[^\n]*error')


def log_errors(log_path):
    """The error lines of one run log (LOG_ERROR); a missing log has none."""
    try:
        with open(log_path, 'rb') as f:
            data = f.read()
    except OSError:
        return []
    return [line.decode('utf-8', 'replace') for line in data.split(b'\n') if LOG_ERROR.search(line)]


class HarnessError(Exception):
    pass


class Skip(Exception):
    pass


# ---------------------------------------------------------------------------- dumps

def read_dump(path):
    """Dump lines without the header comment, as bytes (gzip by extension)."""
    opener = gzip.open if path.endswith('.gz') else open
    with opener(path, 'rb') as f:
        data = f.read()
    return [line for line in data.split(b'\n') if line and not line.startswith(b'#')]


def parse_line(line):
    fields = {}
    for tok in line.decode('ascii', 'replace').split(' '):
        key, sep, value = tok.partition('=')
        if sep:
            fields[key] = value
    return fields


def subfield_diffs(key, old, new):
    """[(name, old, new)] for one differing field, split into named parts where known."""
    if key == 'ef':
        def words(v):
            return {} if v == '-' else dict(w.split(':', 1) for w in v.split(','))
        a, b = words(old), words(new)
        return [('ef[%s]' % w, a.get(w, '0'), b.get(w, '0'))
                for w in sorted(set(a) | set(b), key=int) if a.get(w) != b.get(w)]
    names = SUBFIELDS.get(key)
    if names is None and key.startswith('n') and key[1:].isdigit():
        names = CHARA_FIELDS
    a, b = old.split(','), new.split(',')
    if names is None or len(a) != len(b):
        return [(key, old, new)]
    return [('%s.%s' % (key, names[i] if i < len(names) else str(i)), a[i], b[i])
            for i in range(len(a)) if a[i] != b[i]]


def tick_key(t):
    return int(t) if t.lstrip('-').isdigit() else 1 << 62


def compare_dumps(lines_a, lines_b, label_a='was', label_b='now', out=sys.stdout, limit=12):
    """Prints the first divergence. Returns True when the dumps are identical."""
    if lines_a == lines_b:
        out.write('identical: %d ticks\n' % len(lines_a))
        return True

    n = min(len(lines_a), len(lines_b))
    first = next((i for i in range(n) if lines_a[i] != lines_b[i]), None)
    if first is None:
        last_a = parse_line(lines_a[-1]).get('t', '?') if lines_a else '-'
        last_b = parse_line(lines_b[-1]).get('t', '?') if lines_b else '-'
        out.write('dumps agree on %d ticks, then one ends: %s has %d ticks (last t=%s), '
                  '%s has %d (last t=%s)\n' % (n, label_a, len(lines_a), last_a,
                                               label_b, len(lines_b), last_b))
        return False

    fa, fb = parse_line(lines_a[first]), parse_line(lines_b[first])
    out.write('first divergence: tick %s\n' % fa.get('t', fb.get('t', '?')))
    diffs = []
    for key in list(fa) + [k for k in fb if k not in fa]:
        va, vb = fa.get(key, '(absent)'), fb.get(key, '(absent)')
        if va != vb:
            if '(absent)' in (va, vb):
                diffs.append((key, va, vb))
            else:
                diffs.extend(subfield_diffs(key, va, vb))
    for name, va, vb in diffs[:limit]:
        out.write('  %-26s %s %-14s %s %s\n' % (name, label_a, va, label_b, vb))
    if len(diffs) > limit:
        out.write('  ... %d more differing fields on this tick\n' % (len(diffs) - limit))

    # Which fields go on to differ, and where each one starts: the earliest is the lead.
    starts, counts = {}, {}
    for i in range(first, n):
        if lines_a[i] == lines_b[i]:
            continue
        a, b = parse_line(lines_a[i]), parse_line(lines_b[i])
        for key in set(a) | set(b):
            if a.get(key) != b.get(key):
                counts[key] = counts.get(key, 0) + 1
                starts.setdefault(key, a.get('t', b.get('t', '?')))
    summary = ', '.join('%s (from t=%s, %d ticks)' % (k, starts[k], counts[k])
                        for k in sorted(counts, key=lambda k: (tick_key(starts[k]), -counts[k], k))[:12])
    out.write('ticks differing: %d of %d compared; fields: %s\n'
              % (sum(1 for i in range(first, n) if lines_a[i] != lines_b[i]), n, summary))
    if len(lines_a) != len(lines_b):
        out.write('lengths differ: %s %d ticks, %s %d ticks\n'
                  % (label_a, len(lines_a), label_b, len(lines_b)))
    return False


# ---------------------------------------------------------------------------- routes

class Route:
    def __init__(self, path):
        self.path = os.path.abspath(path)
        self.name = os.path.splitext(os.path.basename(path))[0]
        self.config = []
        self.args = []
        self.console = []
        self.timeout = 300
        self.end = None
        with open(self.path, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if line.startswith('#@'):
                    word, _, rest = line[2:].partition(' ')
                    rest = rest.strip()
                    if word == 'config':
                        key, sep, value = rest.partition('=')
                        if not sep:
                            raise HarnessError('%s: bad #@config line: %s' % (path, line))
                        self.config.append((key.strip(), value.strip()))
                    elif word == 'args':
                        self.args = rest.split()
                    elif word == 'console':
                        if not rest:
                            raise HarnessError('%s: empty #@console line' % path)
                        self.console.append(rest)
                    elif word == 'timeout':
                        self.timeout = int(rest)
                    else:
                        raise HarnessError('%s: unknown directive: %s' % (path, line))
                elif line.startswith('end'):
                    self.end = int(line.split()[1])
        if self.end is None:
            raise HarnessError('%s: no "end <tick>" line' % path)


def find_routes(names):
    if not names:
        paths = sorted(os.path.join(ROUTES_DIR, n) for n in os.listdir(ROUTES_DIR)
                       if n.endswith('.replay'))
    else:
        paths = []
        for n in names:
            p = n if os.path.exists(n) else os.path.join(ROUTES_DIR, n + '.replay')
            if not os.path.exists(p):
                raise HarnessError('no such route: %s' % n)
            paths.append(p)
    return [Route(p) for p in paths]


# ---------------------------------------------------------------------------- game runs

def find_image(build):
    explicit = os.environ.get('SH_DISC_IMAGE')
    if explicit:
        return explicit if os.path.isfile(explicit) else None
    for d in (os.path.join(build, 'data'), build, os.path.join(ROOT, 'data')):
        if os.path.isdir(d):
            for n in sorted(os.listdir(d)):
                if n.lower().endswith('.bin') and os.path.isfile(os.path.join(d, n)):
                    return os.path.join(d, n)
    return None


def check_build(build):
    exe = os.path.join(build, EXE_NAME)
    if not os.path.isfile(exe):
        raise HarnessError('no %s in %s' % (EXE_NAME, build))
    if not os.path.isfile(os.path.join(build, 'gamedata', 'version.cfg')) and find_image(build) is None:
        raise Skip('no imported %s/gamedata and no disc image: link it as %s/data/, put it in '
                   'data/ or set SH_DISC_IMAGE' % (build, build))
    return exe


def game_env(video):
    """The environment of one game start: no SH_REPLAY_* of the caller, the video driver of
    `video` (a Video, or a bare X display name as older callers pass), no audio device, and
    dialog texts in the log instead of a modal window (engine/app/dialog.h)."""
    env = {k: v for k, v in os.environ.items() if not k.startswith('SH_REPLAY_')}
    if isinstance(video, str):
        env.pop('WAYLAND_DISPLAY', None)
        env.update({'DISPLAY': video, 'SDL_VIDEODRIVER': 'x11'})
    else:
        video.apply(env)
    env.update({'SDL_AUDIODRIVER': 'dummy', 'ALSOFT_DRIVERS': 'null', 'SH_DIALOGS_TO_CONSOLE': '1'})
    return env


def video_failed(log_path):
    try:
        with open(log_path, 'rb') as f:
            text = f.read()
    except OSError:
        return False
    return any(m in text for m in VIDEO_FAILURES)


def ensure_import(build, video, work_dir):
    """The game reads only the imported <exe>/gamedata. Without it, one start of the
    build's own exe imports the image there, as the first start of a player would."""
    if os.path.isfile(os.path.join(build, 'gamedata', 'version.cfg')):
        return
    image = find_image(build)
    route = os.path.join(work_dir, 'import.replay')
    with open(route, 'w', encoding='utf-8') as f:
        f.write('1 -\nend 1\n')
    log_path = os.path.join(work_dir, 'import.log')
    print('importing %s into %s/gamedata' % (image, build))
    for attempt in (0, 1):
        env = game_env(video)
        env['SH_REPLAY_INPUT'] = route
        with open(log_path, 'wb') as log:
            rc = subprocess.call([os.path.join(build, EXE_NAME), '--image', os.path.abspath(image)],
                                 cwd=work_dir, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=1800)
        if rc == 0 or attempt or isinstance(video, str) or not video.fall_back(log_path):
            break
    if rc != 0 or not os.path.isfile(os.path.join(build, 'gamedata', 'version.cfg')):
        raise HarnessError('import failed (exit %d), see %s' % (rc, log_path))


def parse_resolution(text):
    w, sep, h = (text or '').lower().partition('x')
    if not (sep and w.isdigit() and h.isdigit() and int(w) >= 320 and int(h) >= 240):
        raise HarnessError('bad resolution %r, expected WIDTHxHEIGHT (at least 320x240)' % text)
    return int(w), int(h)


def write_config(path, route, resolution=DEFAULT_RESOLUTION):
    with open(BASE_CONFIG, 'r', encoding='utf-8') as f:
        lines = f.read().split('\n')
    width, height = parse_resolution(resolution)
    wanted = dict([('width', str(width)), ('height', str(height))] + PINNED_CONFIG)
    wanted.update(dict(route.config))
    seen = set()
    for i, line in enumerate(lines):
        key, sep, _ = line.partition('=')
        key = key.strip()
        if sep and not line.lstrip().startswith('#') and key in wanted:
            lines[i] = '%s = %s' % (key, wanted[key])
            seen.add(key)
    lines += ['%s = %s' % (k, v) for k, v in wanted.items() if k not in seen]
    # The game appends config_version to the file: without a final newline it lands on
    # the last pinned key and corrupts it (spu_renderer = authenticconfig_version = ...).
    with open(path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines).rstrip('\n') + '\n')


def prepare_run_dir(run_dir, build, route, resolution=DEFAULT_RESOLUTION):
    """A private copy of the exe directory. The game takes config.cfg, maps/, assets/ and
    gamedata/ from the exe's directory and saves from <exe dir>/.savegames,
    so the exe is hard-linked into the run directory with links to the rest: each run
    gets the pinned config, the shared imported data and no saves."""
    if os.path.exists(run_dir):
        shutil.rmtree(run_dir)
    os.makedirs(os.path.join(run_dir, 'gamedata'))
    exe = os.path.abspath(os.path.join(run_dir, EXE_NAME))
    try:
        os.link(os.path.join(build, EXE_NAME), exe)
    except OSError:
        shutil.copy2(os.path.join(build, EXE_NAME), exe)
    for name in os.listdir(build):
        # mods/: a test build may carry a mods folder; the shipped builds have none.
        if name in ('maps', 'assets', 'licenses', 'mods') or name.endswith('.txt'):
            os.symlink(os.path.join(build, name), os.path.join(run_dir, name))
    src = os.path.join(build, 'gamedata')
    for name in os.listdir(src):
        if name != 'save':
            os.symlink(os.path.join(src, name), os.path.join(run_dir, 'gamedata', name))
    write_config(os.path.join(run_dir, 'config.cfg'), route, resolution)
    if route.console:
        with open(os.path.join(run_dir, CONSOLE_FILE), 'w', encoding='utf-8') as f:
            f.write('\n'.join(route.console) + '\n')
    return exe


def display_range(default):
    """Xvfb display numbers to try: $SH_XVFB_DISPLAYS ("120-129", "120,121,125" or
    "120 121"), else `default`. Parallel agents give each harness its own range."""
    spec = os.environ.get(DISPLAYS_ENV, '').strip()
    if not spec:
        return list(default)
    out = []
    for part in spec.replace(',', ' ').split():
        a, sep, b = part.partition('-')
        try:
            out.extend(range(int(a), int(b) + 1) if sep else [int(a)])
        except ValueError:
            raise HarnessError('%s: bad display list %r' % (DISPLAYS_ENV, spec))
    return out


def display_busy(n):
    return os.path.exists('/tmp/.X%d-lock' % n) or os.path.exists('/tmp/.X11-unix/X%d' % n)


def xvfb_owns(n, pid):
    """Xvfb writes its pid into /tmp/.X<n>-lock, created atomically. Only that file proves
    the server on :n is ours: the socket alone may belong to a server another harness
    started on the same number between our check and our start."""
    try:
        with open('/tmp/.X%d-lock' % n, 'r') as f:
            return int(f.read().strip() or '0') == pid
    except (OSError, ValueError):
        return False


class Xvfb:
    """A private X server on the first free display in :240-:249 (or $SH_XVFB_DISPLAYS).
    With use_desktop, the session's own DISPLAY instead, so a person can watch or play."""

    def __init__(self, display=None, use_desktop=False, candidates=None, resolution=DEFAULT_RESOLUTION):
        self.proc = None
        self.display = display
        self.name = None
        self.use_desktop = use_desktop
        self.candidates = candidates
        self.resolution = resolution

    def __enter__(self):
        if self.use_desktop and self.display is None and os.environ.get('DISPLAY'):
            self.name = os.environ['DISPLAY']
            return self
        if self.display is not None:
            candidates = [self.display]
        else:
            candidates = list(self.candidates) if self.candidates is not None else display_range(DISPLAYS)
        for n in candidates:
            if display_busy(n):
                continue
            self.log = open(os.devnull, 'w')
            if shutil.which('Xvfb') is None:
                raise Skip('offscreen video failed and Xvfb is not installed')
            w, h = parse_resolution(self.resolution)
            self.proc = subprocess.Popen(['Xvfb', ':%d' % n, '-screen', '0', '%dx%dx24' % (w, h),
                                          '-nolisten', 'tcp'], stdout=self.log, stderr=self.log)
            for _ in range(100):
                if (os.path.exists('/tmp/.X11-unix/X%d' % n) and xvfb_owns(n, self.proc.pid)) \
                        or self.proc.poll() is not None:
                    break
                time.sleep(0.05)
            if self.proc.poll() is None and os.path.exists('/tmp/.X11-unix/X%d' % n) \
                    and xvfb_owns(n, self.proc.pid):
                self.display = n
                self.name = ':%d' % n
                return self
            self.proc.kill()
            self.proc.wait()
            self.log.close()
            self.proc = None
        raise HarnessError('no free display in %s' % ','.join(':%d' % n for n in candidates))

    def __exit__(self, *exc):
        if self.proc is not None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
            self.log.close()


def default_video_mode():
    mode = os.environ.get('SH_VIDEO', '').strip().lower() or 'auto'
    if mode not in VIDEO_MODES:
        raise HarnessError('SH_VIDEO=%s: expected one of %s' % (mode, ', '.join(VIDEO_MODES)))
    return mode


class Video:
    """Where the game renders. 'offscreen' (and 'auto') is SDL's offscreen driver: the real GPU,
    no window and no X input, so input comes only from SH_REPLAY_INPUT. 'xvfb' is a private Xvfb
    (Mesa llvmpipe), never the session's DISPLAY. 'auto' moves to Xvfb for good once a game
    reports that SDL could not start (fall_back). use_desktop is for a person watching or
    playing (record, paced): the session's own display."""

    def __init__(self, mode=None, resolution=DEFAULT_RESOLUTION, display=None, use_desktop=False,
                 candidates=None):
        self.mode = mode or default_video_mode()
        if self.mode not in VIDEO_MODES:
            raise HarnessError('bad video mode %r' % self.mode)
        parse_resolution(resolution)
        self.resolution = resolution
        self.want_display = display
        self.use_desktop = use_desktop and bool(os.environ.get('DISPLAY'))
        self.candidates = candidates
        self.xvfb = None
        self.lock = threading.Lock()

    @property
    def kind(self):
        if self.use_desktop:
            return 'desktop'
        return 'xvfb' if self.xvfb is not None else 'offscreen'

    @property
    def display(self):
        return self.xvfb.display if self.xvfb is not None else None

    @property
    def name(self):
        if self.use_desktop:
            return os.environ['DISPLAY']
        return self.xvfb.name if self.xvfb is not None else 'offscreen'

    def _start_xvfb(self):
        x = Xvfb(self.want_display, candidates=self.candidates, resolution=self.resolution)
        x.__enter__()
        self.xvfb = x

    def __enter__(self):
        # An explicit --display asks for an X server.
        if not self.use_desktop and (self.mode == 'xvfb' or self.want_display is not None):
            self._start_xvfb()
        return self

    def __exit__(self, *exc):
        if self.xvfb is not None:
            self.xvfb.__exit__(*exc)
            self.xvfb = None

    def apply(self, env):
        if self.use_desktop:
            env.pop('SDL_VIDEODRIVER', None)
            return
        # Nothing may reach the session's display or compositor.
        env.pop('DISPLAY', None)
        env.pop('WAYLAND_DISPLAY', None)
        if self.xvfb is not None:
            env.update({'DISPLAY': self.xvfb.name, 'SDL_VIDEODRIVER': 'x11'})
        else:
            env['SDL_VIDEODRIVER'] = 'offscreen'

    def fall_back(self, log_path):
        """After a failed start: True when the run should be repeated on a fresh Xvfb."""
        with self.lock:
            if self.mode != 'auto' or self.use_desktop or not video_failed(log_path):
                return False
            if self.xvfb is None:
                print('offscreen video failed (see %s), falling back to Xvfb' % log_path)
                self._start_xvfb()
            return True


class CpuLoad:
    """Busy processes for the stress run, pinned to the same core as the game."""

    def __init__(self, count, cpu):
        self.count, self.cpu, self.procs = count, cpu, []

    def __enter__(self):
        cmd = [sys.executable, '-I', '-c', 'while True: pass']
        if self.cpu is not None and shutil.which('taskset'):
            cmd = ['taskset', '-c', str(self.cpu)] + cmd
        self.procs = [subprocess.Popen(cmd) for _ in range(self.count)]
        return self

    def __exit__(self, *exc):
        for p in self.procs:
            p.kill()
        for p in self.procs:
            p.wait()


# Games this harness started and has not reaped yet; each leads its own process group
# (start_new_session), so stopping one never touches anything this harness did not start.
_LIVE_GAMES = set()
_LIVE_LOCK = threading.Lock()
_STOPPING = threading.Event()


def start_game(cmd, **kw):
    with _LIVE_LOCK:
        if _STOPPING.is_set():
            raise HarnessError('harness is stopping')
        proc = subprocess.Popen(cmd, start_new_session=True, **kw)
        _LIVE_GAMES.add(proc)
    return proc


def stop_game(proc):
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass
    proc.wait()


def forget_game(proc):
    with _LIVE_LOCK:
        _LIVE_GAMES.discard(proc)


def stop_all_games():
    with _LIVE_LOCK:
        _STOPPING.set()
        live = list(_LIVE_GAMES)
    for proc in live:
        if proc.poll() is None:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                pass


def install_term_handler():
    """SIGTERM/SIGHUP (a harness stopped by its caller) behave like Ctrl+C: the games are
    killed at once, so worker threads blocked on them return, and the `finally` blocks
    stop the Xvfb servers. Without it the games and servers were orphaned and a game left
    on a dead display could spin for hours."""
    def handler(signum, frame):
        stop_all_games()
        raise KeyboardInterrupt('signal %d' % signum)
    for sig in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, handler)


def run_route(build, route, dump_path, work_dir, video, stress=False, paced=False,
              record=None, shot=None, extra_env=None, hold=False):
    """One game run. `video` is a Video (or, from older callers, an X display name). With
    `shot`, the engine saves its last composed frame there as an opaque PNG (SH_REPLAY_SHOT);
    with `hold`, the game keeps the last frame on screen until it is stopped (a person watching)."""
    check_build(build)
    ensure_import(build, video, work_dir)
    run_dir = os.path.join(work_dir, 'run-%s' % route.name)
    exe = prepare_run_dir(run_dir, build, route, getattr(video, 'resolution', DEFAULT_RESOLUTION))
    if shot:
        shot = os.path.abspath(shot)
        if os.path.exists(shot):
            os.remove(shot)
    for attempt in (0, 1):
        rc, elapsed, log_path = start_route(exe, run_dir, route, dump_path, work_dir, video, stress,
                                            paced, record, shot, extra_env, hold)
        if rc == 0 or attempt or isinstance(video, str) or not video.fall_back(log_path):
            break
    if rc != 0:
        raise HarnessError('%s: game exited with %d after %.1f s, see %s'
                           % (route.name, rc, elapsed, log_path))
    if not record:
        lines = read_dump(dump_path)
        last = parse_line(lines[-1]).get('t') if lines else None
        if last != str(route.end):
            raise HarnessError('%s: dump ends at t=%s, expected %d, see %s'
                               % (route.name, last, route.end, log_path))
    if shot and not os.path.isfile(shot):
        raise HarnessError('%s: no screenshot %s, see %s' % (route.name, shot, log_path))
    return elapsed


def start_route(exe, run_dir, route, dump_path, work_dir, video, stress, paced, record, shot,
                extra_env, hold):
    env = game_env(video)
    # handler_diff.py: SH_SCRIPT_HANDLER_TRACE.
    env.update(extra_env or {})
    if record:
        # The route gives the settings and, with its end line, the length of the session.
        env['SH_REPLAY_RECORD'] = os.path.abspath(record)
        env['SH_REPLAY_END'] = str(route.end)
    else:
        env['SH_REPLAY_INPUT'] = route.path
    env['SH_REPLAY_DUMP'] = os.path.abspath(dump_path)
    if paced:
        env['SH_REPLAY_PACED'] = '1'
    if shot:
        env['SH_REPLAY_SHOT'] = shot
    if hold:
        env['SH_REPLAY_HOLD'] = '1'
    cmd = [exe] + route.args
    if route.console:
        # The console commands of the route (#@console): one per tick, "@<seconds>" waits count the
        # game's virtual clock in replay, so they land on the same tick every run.
        cmd += ['-consolecmd', os.path.join(run_dir, CONSOLE_FILE)]
    if stress and shutil.which('taskset'):
        cmd = ['taskset', '-c', '0'] + cmd
    log_path = os.path.join(work_dir, '%s.log' % os.path.basename(dump_path))
    started = time.time()
    with open(log_path, 'wb') as log:
        proc = start_game(cmd, cwd=run_dir, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            rc = proc.wait(timeout=None if (record or hold) else route.timeout)
        except subprocess.TimeoutExpired:
            stop_game(proc)
            raise HarnessError('%s: no end after %d s, see %s' % (route.name, route.timeout, log_path))
        except BaseException:
            # Interrupted harness (Ctrl+C, SIGTERM): the game must not outlive it.
            stop_game(proc)
            raise
        finally:
            forget_game(proc)
    return rc, time.time() - started, log_path


def reference_path(route):
    return os.path.join(REFERENCE_DIR, route.name + '.dump.gz')


def write_reference(dump_path, route):
    os.makedirs(REFERENCE_DIR, exist_ok=True)
    with open(dump_path, 'rb') as f:
        data = f.read()
    # mtime 0 and no file name: the same dump gives the same .gz bytes.
    with open(reference_path(route), 'wb') as raw:
        with gzip.GzipFile(filename='', mode='wb', fileobj=raw, compresslevel=9, mtime=0) as gz:
            gz.write(data)
    return os.path.getsize(reference_path(route))


# ---------------------------------------------------------------------------- commands

def work_dir_for(args):
    if args.work_dir:
        os.makedirs(args.work_dir, exist_ok=True)
        return os.path.abspath(args.work_dir)
    return tempfile.mkdtemp(prefix='sh-replay-')


def cmd_run(args):
    route = find_routes([args.route])[0]
    work = work_dir_for(args)
    if args.hold and not os.environ.get('DISPLAY'):
        raise HarnessError('--hold shows the last frame on the desktop: no DISPLAY')
    with Video(args.video, args.resolution, args.display,
               use_desktop=bool(args.record) or args.paced or args.hold) as x:
        if args.record:
            secs = run_route(args.build, route, args.dump, work, x, paced=True,
                             record=args.record)
            # The settings of the session go with its input, or a replay would start elsewhere.
            with open(route.path, 'r', encoding='utf-8') as f:
                directives = [l for l in f if l.startswith('#@')]
            with open(args.record, 'r', encoding='utf-8') as f:
                recorded = f.read()
            with open(args.record, 'w', encoding='utf-8') as f:
                f.write(''.join(directives) + recorded)
        else:
            extra = {}
            if args.shots:
                os.makedirs(args.shots, exist_ok=True)
                extra = {'SH_REPLAY_SHOTS': os.path.abspath(args.shots),
                         'SH_REPLAY_SHOTS_FROM': str(args.shots_from)}
            secs = run_route(args.build, route, args.dump, work, x, stress=args.stress,
                             paced=args.paced, shot=args.shot, hold=args.hold, extra_env=extra)
    print('%s: %.1f s, dump %s, video %s %s' % (route.name, secs, args.dump, x.kind, args.resolution))
    if args.shot:
        print('screenshot %s' % os.path.abspath(args.shot))
    return 0


def cmd_check(args):
    routes = find_routes(args.routes)
    work = work_dir_for(args)
    failed = 0
    with Video(args.video, args.resolution, args.display) as x:
        print('video %s %s' % (x.kind, args.resolution))
        for route in routes:
            dumps, times, errors = [], [], []
            for i in range(args.runs):
                stress = args.stress and i % 2 == 1
                path = os.path.join(work, '%s.%d.dump' % (route.name, i))
                if stress:
                    with CpuLoad(args.load_procs, 0):
                        times.append(run_route(args.build, route, path, work, x, stress=True))
                else:
                    times.append(run_route(args.build, route, path, work, x))
                dumps.append(path)
                errors += log_errors(path + '.log')
            first = read_dump(dumps[0])
            status = 'ok'
            if errors:
                print('%s: FAIL, %d error line(s) in the logs, first: %s' % (route.name, len(errors), errors[0]))
                status = 'FAIL'
            for path in dumps[1:]:
                if read_dump(path) != first:
                    print('%s: FAIL, two runs of the same input differ (%s, %s):'
                          % (route.name, dumps[0], path))
                    compare_dumps(first, read_dump(path), 'run1', 'run2')
                    status = 'FAIL'
                    break
            ref = reference_path(route)
            ref_note = 'no reference'
            if status == 'ok' and os.path.exists(ref):
                if read_dump(ref) == first:
                    ref_note = 'matches reference'
                else:
                    print('%s: FAIL, dump differs from the reference %s:' % (route.name, ref))
                    compare_dumps(read_dump(ref), first, 'was', 'now')
                    status = 'FAIL'
            if status != 'ok':
                failed += 1
                continue
            print('%s: ok, %d runs identical (%d ticks, %s), %s'
                  % (route.name, len(dumps), len(first),
                     ', '.join('%.1f s' % t for t in times), ref_note))
    if not args.work_dir and not failed:
        shutil.rmtree(work, ignore_errors=True)
    elif failed:
        print('dumps and logs kept in %s' % work)
    return 1 if failed else 0


def cmd_update_ref(args):
    routes = find_routes(args.routes)
    work = work_dir_for(args)
    with Video(args.video, args.resolution, args.display) as x:
        for route in routes:
            a = os.path.join(work, '%s.a.dump' % route.name)
            b = os.path.join(work, '%s.b.dump' % route.name)
            run_route(args.build, route, a, work, x)
            run_route(args.build, route, b, work, x)
            if read_dump(a) != read_dump(b):
                print('%s: two runs differ, reference not written:' % route.name)
                compare_dumps(read_dump(a), read_dump(b), 'run1', 'run2')
                return 1
            old = reference_path(route)
            if os.path.exists(old) and read_dump(old) != read_dump(a):
                print('%s: reference changes:' % route.name)
                compare_dumps(read_dump(old), read_dump(a), 'was', 'now')
            size = write_reference(a, route)
            print('%s: reference %s (%d bytes)' % (route.name, reference_path(route), size))
    if not args.work_dir:
        shutil.rmtree(work, ignore_errors=True)
    return 0


def cmd_selftest(args):
    """The comparison on synthetic dumps: what replay_check relies on to report a divergence."""
    import io
    base = [b't=%d gs=11 rng=%08x pl=1,0,1,5,17,%d,0,-24912,0,%d,0,2048,0,5724,0,2048,409600,136,-1,0 ef=0:0000000e'
            % (t, t * 7, 1000 + t, 630000 - t) for t in range(1, 101)]
    changed = list(base)
    for t in range(40, 101):
        changed[t - 1] = changed[t - 1].replace(b',-24912,', b',-24900,').replace(b'ef=0:0000000e', b'ef=0:0000000e,3:00000001')
    failures = []

    out = io.StringIO()
    if not compare_dumps(base, list(base), out=out) or 'identical: 100 ticks' not in out.getvalue():
        failures.append('identical dumps: %r' % out.getvalue())
    out = io.StringIO()
    if compare_dumps(base, changed, out=out):
        failures.append('a changed dump compared equal')
    text = out.getvalue()
    for want in ('first divergence: tick 40', 'pl.pos.x', '-24912', '-24900', 'ef[3]', 'ticks differing: 61'):
        if want not in text:
            failures.append('report lacks %r:\n%s' % (want, text))
    out = io.StringIO()
    compare_dumps(base, base[:70], out=out)
    if 'then one ends' not in out.getvalue():
        failures.append('a truncated dump: %r' % out.getvalue())

    for f in failures:
        print('FAIL: %s' % f)
    print('selftest: %s' % ('ok' if not failures else '%d failures' % len(failures)))
    return 1 if failures else 0


def cmd_compare(args):
    return 0 if compare_dumps(read_dump(args.a), read_dump(args.b), 'was', 'now') else 1


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)

    def common(sp):
        sp.add_argument('--build', required=True, help='build directory with SilentHillPC and maps/')
        sp.add_argument('--work-dir', help='keep run directories, dumps and logs here')
        sp.add_argument('--video', choices=VIDEO_MODES, default=None,
                        help='offscreen: SDL offscreen driver on the real GPU; xvfb: a private Xvfb (llvmpipe); '
                             'auto (default, or $SH_VIDEO): offscreen, Xvfb when SDL cannot start')
        sp.add_argument('--resolution', default=DEFAULT_RESOLUTION, metavar='WxH',
                        help='render size (default %(default)s); the dumps do not depend on it')
        sp.add_argument('--display', type=int, help='run on a private Xvfb with this display number '
                        '(default: offscreen; Xvfb takes the first free in :240-:249 or $SH_XVFB_DISPLAYS)')

    sp = sub.add_parser('run', help='run one route')
    common(sp)
    sp.add_argument('route', help='route name in tests/replay/routes or a path')
    sp.add_argument('--dump', required=True)
    sp.add_argument('--stress', action='store_true', help='run the game under taskset -c 0')
    sp.add_argument('--paced', action='store_true', help='wall-clock pace (watch with a real display)')
    sp.add_argument('--record', help='record live input to this file instead of replaying')
    sp.add_argument('--shot', help="the engine's screenshot of the last tick (opaque PNG at --resolution), "
                    'for writing routes')
    sp.add_argument('--shots', help='a shot of every tick into this directory (t<tick>.png), for checking motion')
    sp.add_argument('--shots-from', type=int, default=0, help='first tick of --shots')
    sp.add_argument('--hold', action='store_true',
                    help='keep the last frame on the desktop until the game is closed (a person watching)')
    sp.set_defaults(func=cmd_run)

    sp = sub.add_parser('check', help='run routes twice, compare with each other and the reference')
    common(sp)
    sp.add_argument('routes', nargs='*', help='route names (default: all)')
    sp.add_argument('--runs', type=int, default=2)
    sp.add_argument('--stress', action='store_true',
                    help='every second run under taskset -c 0 with busy processes on CPU 0')
    sp.add_argument('--load-procs', type=int, default=4)
    sp.set_defaults(func=cmd_check)

    sp = sub.add_parser('update-ref', help='rewrite the stored references')
    common(sp)
    sp.add_argument('routes', nargs='*')
    sp.set_defaults(func=cmd_update_ref)

    sp = sub.add_parser('selftest', help='check the dump comparison on synthetic dumps')
    sp.set_defaults(func=cmd_selftest)

    sp = sub.add_parser('compare', help='first divergence between two dumps')
    sp.add_argument('a')
    sp.add_argument('b')
    sp.set_defaults(func=cmd_compare)

    args = p.parse_args()
    if getattr(args, 'build', None):
        args.build = os.path.abspath(args.build)
    install_term_handler()
    try:
        return args.func(args)
    except Skip as e:
        print('skipped: %s' % e)
        return EXIT_SKIP
    except HarnessError as e:
        print('error: %s' % e)
        return 2


if __name__ == '__main__':
    sys.exit(main())
