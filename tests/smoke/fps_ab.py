#!/usr/bin/env python3
"""The same fight at several frame rates: results must not depend on fps_cap.

Each run starts New Game in the school (--map), takes the steel pipe, spawns an enemy with a fixed random seed and
health and swings at it on a fixed schedule of game time (`@+N` lines of -consolecmd, the `pad` console command). The
table shows the blows that landed, the enemy's health after each, Harry's health and a hash of both characters'
positions and states and of the game's random seed at every checkpoint. Rows of 60 fps and above must be equal; 30 fps
runs the original 30 Hz step and differs. With --stall a share of the updates sleeps 40-100 ms: the traces must equal
those of a run without it at the same fps_cap (a stalled frame is made up by updates of the usual step, section 2.2).
"stalled frames" counts changes of the step after the seed and should be 0. --style tps or fps plays it with an
alternate camera turned by a scripted right stick: the picture turns on every presented frame, the game only on its
updates.

  fps_ab.py --build <build dir> --gamedata <imported gamedata + repository gamedata> --out <work dir>
            [--enemy Groaner] [--health 200] [--caps 30,60,120,240,0] [--frames] [--stall percent]
            [--style classic|tps|fps] [--map map1_s00]

--frames also saves every presented frame of a turn and of a blow (`shotseq`): <out>/<cap>/walk_NNN.png, hit_NNN.png.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys

SWINGS = 20
SKIP = {'SilentHillPC', 'config.cfg', 'logs', 'gamedata', 'CMakeFiles', 'src', 'tests', 'tools', 'build.ninja',
        'CMakeCache.txt'}


def scenario(enemy, health, frames_prefix, stall, style):
    def capture(name, count, wait):
        return '@+%d shotseq %s_%s %d' % (wait, frames_prefix, name, count) if frames_prefix else '@+%d' % wait

    # The weapon and a far frozen enemy first: their loads stall frames, and a stalled frame takes a longer step.
    # The measured part starts from a fixed seed after them.
    # The map comes with the configuration (New Game starts on it): a `map` command at a time of the wall clock
    # came after New Game on a fast start, and the run played on another map.
    lines = (['@+30 stall %d %d' % (stall, 7)] if stall else ['@+30']) + ['@+30 give SteelPipe', '@+10 equip SteelPipe', '@+10 spawn %s auto 30' % enemy,
             '@+10 select nearest', '@+2 freeze', '@+60 rngseed 4660', '@+2 spawn %s auto 3' % enemy,
             '@+4 select nearest', '@+1 health %s' % health, '@+2 charas', '@+1 rngseed',
             '@+2 pad R2', '@+10 pad R2+Left', capture('walk', 36, 6), '@+24 pad -']
    # The alternate cameras turn with the right stick, read once an update; the frames between updates turn the
    # picture on by it, which must not reach the game (the weather spawns around the view, the body follows it).
    if style != 'classic':
        lines += ['@+2 lookinput stick 90 -30', '@+20 lookinput stick -60 20', '@+20 lookinput off']
    # One blow a cycle: the weapon is lowered and raised again, so Harry turns to the enemy before each.
    for i in range(SWINGS):
        lines += ['@+8 pad R2', '@+24 pad R2+Cross']
        if i == 3:
            lines.append(capture('hit', 60, 1))
        lines += ['@+6 pad R2', '@+40 pad -', '@+2 charas', '@+1 rngseed']
    lines += ['@+30 charas', '@+2 quit']
    return '\n'.join(lines) + '\n'


def start(args, cap):
    run = os.path.join(args.out, str(cap))
    shutil.rmtree(run, ignore_errors=True)
    os.makedirs(run)
    for name in os.listdir(args.build):
        if name not in SKIP:
            os.symlink(os.path.join(args.build, name), os.path.join(run, name))
    shutil.copy(os.path.join(args.build, 'SilentHillPC'), run)
    os.symlink(args.gamedata, os.path.join(run, 'gamedata'))
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    pinned = {'width': '800', 'height': '600', 'fullscreen': '0', 'vsync': '0', 'fps_cap': str(cap),
              'skip_intros': '2', 'map': args.map, 'control_style': args.style}
    with open(os.path.join(root, 'assets', 'config.cfg')) as src, open(os.path.join(run, 'config.cfg'), 'w') as dst:
        for line in src:
            key = line.split('=')[0].strip()
            dst.write('%s = %s\n' % (key, pinned[key]) if key in pinned else line)
        # Stamped: the migration that moves fps_cap 30 to 60 must not rewrite the value under test.
        dst.write('config_version = 9\n')
        # The lightest picture, so that the high caps are reached; no graphics key changes game logic.
        dst.write('graphics_preset = original\n')
    with open(os.path.join(run, 'console.txt'), 'w') as f:
        f.write(scenario(args.enemy, args.health, os.path.join(run, 'frames') if args.frames else None,
                         args.stall, args.style))
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', ALSOFT_DRIVERS='null', SDL_AUDIODRIVER='dummy',
               SH_DIALOGS_TO_CONSOLE='1')
    log = open(os.path.join(run, 'out.log'), 'w')
    return subprocess.Popen(['timeout', '-k', '5', '120', './SilentHillPC', '-consolecmd',
                             os.path.join(run, 'console.txt')], cwd=run, env=env, stdout=log, stderr=log)


def report(args, cap):
    text = open(os.path.join(args.out, str(cap), 'out.log'), errors='replace').read()
    seen = {}
    # The game's random seed at every checkpoint after the fixed one: a draw of a frame between two updates moves it.
    seeds = re.findall(r'rngseed: (0x[0-9a-f]+)', text[text.find('rngseed 4660'):])[1:]
    for line in text.splitlines():
        m = re.search(r'console\s+(player Harry|npc\d+)\s.*?\bhp\s+(-?[\d.]+)', line)
        if m:
            seen.setdefault(m.group(1), []).append((float(m.group(2)), line.split('console', 1)[1].strip()))
    # The measured enemy is the one given the fixed health (the far one keeps the health it rolled before the seed).
    near = [v for k, v in seen.items() if k != 'player Harry' and v[0][0] == float(args.health)]
    enemy = near[0] if near else []
    harry = seen.get('player Harry', [])
    health = [h for h, _ in enemy]
    steps = [h for i, h in enumerate(health) if i == 0 or h != health[i - 1]]
    hits = sum(1 for a, b in zip(health, health[1:]) if b < a)
    trace = hashlib.md5('\n'.join([l for _, l in harry + enemy] + seeds).encode()).hexdigest()[:8]
    errors = len(re.findall(r'update error|lua.*error', text))
    # Changes of the update step after the seed: none when stalled frames are made up by updates of the usual step.
    stalls = len(re.findall(r'\[CONSOLECMD\] step \d+ -> \d+', text[text.find('rngseed 4660'):])) // 2
    return hits, steps, harry[-1][0] if harry else None, trace, errors, stalls


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--build', required=True)
    ap.add_argument('--gamedata', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--enemy', default='Groaner')
    ap.add_argument('--health', default='200')
    ap.add_argument('--caps', default='30,60,120,240,0')
    ap.add_argument('--frames', action='store_true')
    ap.add_argument('--stall', type=int, default=0, help='percent of the updates that sleep 40-100 ms')
    ap.add_argument('--map', default='map1_s00')
    ap.add_argument('--style', default='classic', choices=('classic', 'tps', 'fps'), help='control_style')
    args = ap.parse_args()
    for name in ('build', 'gamedata', 'out'):
        setattr(args, name, os.path.abspath(getattr(args, name)))
    caps = [int(c) for c in args.caps.split(',')]
    for p in [start(args, cap) for cap in caps]:
        p.wait()
    rows = {cap: report(args, cap) for cap in caps}
    print('%-8s %4s  %-28s %6s  %-8s %6s %s' % ('fps_cap', 'hits', 'enemy health', 'Harry', 'trace', 'errors',
                                                   'stalled frames'))
    for cap in caps:
        hits, steps, harry, trace, errors, stalls = rows[cap]
        print('%-8s %4d  %-28s %6s  %-8s %6d %d' % (cap or 'uncapped', hits, ' > '.join('%.1f' % s for s in steps),
                                                     harry, trace, errors, stalls))
    clean = [cap for cap in caps if (cap == 0 or cap >= 60) and rows[cap][5] == 0]
    equal = len({rows[cap][3] for cap in clean}) <= 1 and all(r[4] == 0 and r[2] is not None for r in rows.values())
    stalled = [cap for cap in caps if rows[cap][5] != 0]
    print('60 fps and above, runs without a stalled frame (%d): %s' % (len(clean), 'equal' if equal else 'DIFFERENT'))
    if stalled:
        print('runs whose step changed are not compared: run again')
    return 0 if equal else 1


if __name__ == '__main__':
    sys.exit(main())
