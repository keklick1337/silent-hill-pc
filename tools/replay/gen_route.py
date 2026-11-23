#!/usr/bin/env python3
"""Writes a pseudo-random replay route (input format v1).

The same seed always gives the same file: random.Random(seed), no other input.

  python3 -I tools/replay/gen_route.py --seed 1500 --start 300 --end 4500 out.replay
"""

import argparse
import random

# Gameplay inputs only: Start, Select and Triangle would leave the walk in the pause,
# item and map screens.
STATES = ['-', 'Up', 'Up', 'Up', 'Up Left', 'Up Right', 'Down', 'Left', 'Right', 'Cross',
          'Circle', 'Up Cross', 'Up Square', 'Up Square', 'Square', 'R1', 'L1', 'L2', 'R2',
          'Up Circle', 'Down Square']


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--seed', type=int, required=True)
    p.add_argument('--start', type=int, default=300, help='first tick with input')
    p.add_argument('--end', type=int, required=True, help='last tick')
    p.add_argument('--min-gap', type=int, default=10)
    p.add_argument('--max-gap', type=int, default=40)
    p.add_argument('--header', action='append', default=[], help='line to put first (#@config ...)')
    p.add_argument('out')
    a = p.parse_args()

    rng = random.Random(a.seed)
    lines = list(a.header)
    lines.append('# generated: gen_route.py --seed %d --start %d --end %d --min-gap %d --max-gap %d'
                 % (a.seed, a.start, a.end, a.min_gap, a.max_gap))
    lines.append('1 -')
    t = a.start
    while t < a.end:
        lines.append('%d %s' % (t, rng.choice(STATES)))
        t += rng.randint(a.min_gap, a.max_gap)
    lines.append('end %d' % a.end)
    with open(a.out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
