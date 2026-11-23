#!/usr/bin/env python3
"""Finds which music layers the game had open in a recorded mix: renders the song
with every combination of layers (sh-music --base --channel ...) and keeps the one whose envelope matches the
game's best, then prints the comparison of that render (compare_music.py).

  tests/audio/fit_layers.py SH_MUSIC KDT VAB GAME.wav --layers 0,2,2,2,0,4,... [--game-start S] [--seconds N]
"""
import argparse
import itertools
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('sh_music')
    ap.add_argument('kdt')
    ap.add_argument('vab')
    ap.add_argument('game')
    ap.add_argument('--layers', required=True, help='layer of each MIDI channel 0..15, comma separated')
    ap.add_argument('--game-start', type=float, default=0.0)
    ap.add_argument('--seconds', type=float, default=30.0)
    ap.add_argument('--keep', help='write the best render here')
    args = ap.parse_args()

    layers = [int(x) for x in args.layers.split(',')]
    used = sorted(set(l for l in layers if l))
    work = tempfile.mkdtemp(prefix='sh-fit-')
    out = os.path.join(work, 'r.wav')
    best = None
    for bits in itertools.product([0, 1], repeat=len(used)):
        if not any(bits):
            continue
        on = {l for l, b in zip(used, bits) if b}
        cmd = [args.sh_music, args.kdt, args.vab, out, '--base']
        for ch, l in enumerate(layers):
            if l:
                cmd += ['--channel', '%d=%d' % (ch, 127 if l in on else 0)]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        r = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'compare_music.py'), args.game, out,
                            '--game-start', str(args.game_start), '--seconds', str(args.seconds)],
                           check=True, capture_output=True, text=True)
        res = json.loads(r.stdout)
        res['layers_on'] = sorted(on)
        if best is None or res['envelope_corr'] > best['envelope_corr']:
            best = res
            if args.keep:
                shutil.copyfile(out, args.keep)
    print(json.dumps(best))


if __name__ == '__main__':
    main()
