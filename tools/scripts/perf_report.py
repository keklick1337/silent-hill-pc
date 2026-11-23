#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Summary of performance recordings (console `perfrec` / `perfbench`).

  perf_report.py summary <run.csv> [...]   stability, hitches and what caused them, GPU passes, worst places
  perf_report.py compare <a.csv> <b.csv>   two recordings pass by pass (B against A)
  perf_report.py bench <perfrec/bench-...> the variants of a perfbench against its baseline, most expensive first
  perf_report.py selftest                  the analysis on synthetic rows (no game)

A file name alone means `summary`. Each <run>.csv has <run>.frames.csv (one line per frame) and <run>.json (build,
GPU, renderer, graphics keys, summary) beside it; both are optional here. Standard library only: python3 -I.
"""
import csv
import io
import json
import math
import os
import re
import sys

BUCKET_M = 8.0  # worst places are compared in squares of this many metres
CAUSE_UPLOAD_KB = 512.0


def num(v):
    if v is None or v == '':
        return None
    try:
        return float(v)
    except ValueError:
        return None


def pct(values, q):
    if not values:
        return None
    s = sorted(values)
    i = min(len(s) - 1, max(0, int(math.ceil(q * len(s))) - 1))
    return s[i]


def mean(values):
    values = [v for v in values if v is not None]
    return sum(values) / len(values) if values else None


def median(values):
    return pct([v for v in values if v is not None], 0.5)


def fmt(v, digits=2):
    return '-' if v is None else ('%.*f' % (digits, v))


class Run:
    def __init__(self, name, rows, frames=None, header=None):
        self.name = name
        self.rows = rows
        self.frames = frames or []
        self.header = header or {}
        self.windows = [r for r in rows if r.get('kind') == 'window']
        self.hitches = [r for r in rows if r.get('kind') == 'hitch']
        self.passes = []
        for r in rows:
            for k in r:
                if k and k.startswith('gpu:') and k[4:] not in self.passes:
                    self.passes.append(k[4:])

    def value(self, row, key):
        return num(row.get(key))


def read_rows(text):
    return [dict(r) for r in csv.DictReader(io.StringIO(text))]


def load(path):
    with open(path, encoding='utf-8') as f:
        rows = read_rows(f.read())
    base = path[:-4] if path.endswith('.csv') else path
    frames, header = [], {}
    if os.path.exists(base + '.frames.csv'):
        with open(base + '.frames.csv', encoding='utf-8') as f:
            frames = read_rows(f.read())
    if os.path.exists(base + '.json'):
        with open(base + '.json', encoding='utf-8') as f:
            try:
                header = json.load(f)
            except ValueError:
                header = {}
    return Run(os.path.basename(path), rows, frames, header)


# ---- analysis ----------------------------------------------------------------------------------------------------

def stability(run):
    """Frame-time figures; from the per-frame file when there is one, else from the rows."""
    times = [num(f.get('frame_ms')) for f in run.frames]
    times = [t for t in times if t is not None]
    out = {'source': 'frames' if times else 'rows'}
    if times:
        n = len(times)
        avg = sum(times) / n
        med = pct(times, 0.5)
        out.update(frames=n, seconds=sum(times) / 1000.0, fps=1000.0 / avg if avg else 0.0, avg=avg, p50=med,
                   p99=pct(times, 0.99), p999=pct(times, 0.999), max=max(times),
                   stdev=math.sqrt(max(0.0, sum(t * t for t in times) / n - avg * avg)),
                   jitter=sum(abs(times[i] - times[i - 1]) for i in range(1, n)) / (n - 1) if n > 1 else 0.0,
                   over_1_5x=sum(1 for t in times if t > 1.5 * med) / n * 100.0,
                   over_2x=sum(1 for t in times if t > 2.0 * med) / n * 100.0)
    else:
        w = run.windows
        frames = sum(num(r.get('frames')) or 0 for r in w)
        secs = sum((num(r.get('frames')) or 0) * (num(r.get('frame_ms_avg')) or 0) for r in w) / 1000.0
        out.update(frames=int(frames), seconds=secs, fps=frames / secs if secs else 0.0,
                   avg=secs * 1000.0 / frames if frames else None, p50=median([num(r.get('frame_ms_avg')) for r in w]),
                   p99=pct([num(r.get('frame_ms_max')) for r in w if num(r.get('frame_ms_max')) is not None], 0.99),
                   p999=None, max=max([num(r.get('frame_ms_max')) or 0 for r in w] or [0]),
                   stdev=mean([num(r.get('frame_ms_stdev')) for r in w]), jitter=mean([num(r.get('jitter_ms')) for r in w]),
                   over_1_5x=None, over_2x=None)
    out['hitches'] = len(run.hitches)
    return out


def typical(run):
    keys = ['cpu_update_ms', 'cpu_render_ms', 'wait_ms', 'gpu_ms']
    t = {k: median([num(r.get(k)) for r in run.windows]) for k in keys}
    for p in run.passes:
        t['gpu:' + p] = median([num(r.get('gpu:' + p)) for r in run.windows])
    return t


def hitch_causes(row, base):
    """What stood out in a hitch row, biggest first."""
    causes = []
    built = num(row.get('pipelines_built')) or 0
    if built > 0:
        causes.append((num(row.get('pipeline_build_ms')) or built, 'pipeline compile x%d (%.1f ms)' % (
            built, num(row.get('pipeline_build_ms')) or 0)))
    upload = num(row.get('texture_upload_kb')) or 0
    if upload >= CAUSE_UPLOAD_KB:
        causes.append((upload / 1024.0, 'texture upload %.0f KB' % upload))
    swaps = num(row.get('upscale_swaps')) or 0
    if swaps > 0:
        causes.append((swaps, 'upscale swap x%d' % swaps))
    cells = num(row.get('cells_loaded')) or 0
    if cells > 0:
        causes.append((cells * 2.0, 'map cell ready x%d' % cells))
    for key, label in (('cpu_update_ms', 'game update'), ('cpu_render_ms', 'render CPU'), ('wait_ms', 'wait/present')):
        v, b = num(row.get(key)), base.get(key)
        if v is not None and v > 2.0 * (b or 0) + 2.0:
            causes.append((v - (b or 0), '%s %.1f ms (usual %.1f)' % (label, v, b or 0)))
    g, gb = num(row.get('gpu_ms')), base.get('gpu_ms')
    if g is not None and gb is not None and g > 1.5 * gb + 1.0:
        worst, extra = None, 0.0
        for k, b in base.items():
            if k.startswith('gpu:'):
                v = num(row.get(k))
                if v is not None and v - (b or 0) > extra:
                    worst, extra = k[4:], v - (b or 0)
        causes.append((g - gb, 'GPU %.1f ms (usual %.1f)%s' % (g, gb, ', most in %s +%.1f ms' % (worst, extra) if worst else '')))
    causes.sort(key=lambda c: -c[0])
    return [c[1] for c in causes] or ['nothing counted stood out (driver, OS or another program)']


def pass_table(run):
    out = []
    total = mean([num(r.get('gpu_ms')) for r in run.windows if (num(r.get('pass_timing')) or 0) > 0.99])
    for p in run.passes:
        vals = [num(r.get('gpu:' + p)) for r in run.windows]
        vals = [v for v in vals if v is not None]
        if not vals:
            continue
        avg = sum(vals) / len(vals)
        out.append((p, avg, pct(vals, 0.95), avg / total * 100.0 if total else None))
    out.sort(key=lambda x: -x[1])
    return out, total


def bucket_of(row):
    x, z = num(row.get('player_x')), num(row.get('player_z'))
    if x is None or z is None:
        return None
    return (row.get('map') or '?', int(math.floor(x / BUCKET_M)), int(math.floor(z / BUCKET_M)))


def worst_places(run, top=10):
    groups = {}
    for r in run.rows:
        b = bucket_of(r)
        if b is not None:
            groups.setdefault(b, []).append(r)
    out = []
    for b, rows in groups.items():
        w = [r for r in rows if r.get('kind') == 'window']
        if not w:
            continue
        frame = mean([num(r.get('frame_ms_avg')) for r in w])
        passes = {}
        for p in run.passes:
            v = mean([num(r.get('gpu:' + p)) for r in w])
            if v is not None:
                passes[p] = v
        dominant = max(passes.items(), key=lambda kv: kv[1]) if passes else None
        out.append({'map': b[0], 'x': (b[1] + 0.5) * BUCKET_M, 'z': (b[2] + 0.5) * BUCKET_M, 'windows': len(w),
                    'frame_ms': frame, 'low': min([num(r.get('fps_1pct_low')) or 0 for r in w]),
                    'jitter': mean([num(r.get('jitter_ms')) for r in w]),
                    'hitches': sum(1 for r in rows if r.get('kind') == 'hitch'),
                    'gpu': mean([num(r.get('gpu_ms')) for r in w]), 'dominant': dominant,
                    'cutscene': any(r.get('cutscene') == '1' for r in w)})
    out.sort(key=lambda g: -(g['frame_ms'] or 0))
    return out[:top]


def slow_growth(run, share=0.1):
    """Which pass grows most in the slowest windows against all windows."""
    w = [r for r in run.windows if num(r.get('frame_ms_avg')) is not None]
    if len(w) < 10:
        return []
    slow = sorted(w, key=lambda r: -num(r.get('frame_ms_avg')))[:max(1, int(len(w) * share))]
    out = []
    for p in run.passes:
        a, s = mean([num(r.get('gpu:' + p)) for r in w]), mean([num(r.get('gpu:' + p)) for r in slow])
        if a is not None and s is not None:
            out.append((p, a, s, s - a))
    out.sort(key=lambda x: -x[3])
    return out


def timestamp_cost(run):
    on = [r for r in run.windows if num(r.get('pass_timing')) == 1 and (num(r.get('hitches')) or 0) == 0]
    off = [r for r in run.windows if num(r.get('pass_timing')) == 0 and (num(r.get('hitches')) or 0) == 0]
    if not on or not off:
        return None
    return {'with': len(on), 'without': len(off),
            'gpu': mean([num(r.get('gpu_ms')) for r in on]) - (mean([num(r.get('gpu_ms')) for r in off]) or 0),
            'frame': mean([num(r.get('frame_ms_avg')) for r in on]) - mean([num(r.get('frame_ms_avg')) for r in off])}


# ---- printing ----------------------------------------------------------------------------------------------------

def print_summary(run, out=sys.stdout):
    h = run.header
    p = lambda *a: print(*a, file=out)
    p('== %s' % run.name)
    if h:
        p('   %s on %s (%s), preset %s%s, render %s x%s, revision %s' % (
            h.get('renderer', '?'), h.get('gpu', '?'), h.get('driver', '?'), h.get('preset', '?'),
            '' if h.get('preset_matches', True) else ' (changed)', h.get('render_size', '?'), h.get('render_scale', '?'),
            h.get('revision', '?') or '?'))
    s = stability(run)
    p('-- frame-time stability (from %s)' % s['source'])
    p('   %d frames, %.1f s, %.1f fps; frame ms avg %s p50 %s p99 %s p99.9 %s max %s' % (
        s['frames'], s['seconds'], s['fps'], fmt(s['avg']), fmt(s['p50']), fmt(s['p99']), fmt(s['p999']), fmt(s['max'])))
    p('   stdev %s ms, jitter %s ms (mean change frame to frame); frames over 1.5x median %s%%, over 2x %s%%; '
      '%d hitches' % (fmt(s['stdev']), fmt(s['jitter']), fmt(s['over_1_5x'], 1), fmt(s['over_2x'], 1), s['hitches']))
    base = typical(run)
    if run.hitches:
        tally = {}
        listed = sorted(run.hitches, key=lambda r: -(num(r.get('frame_ms_max')) or 0))
        p('-- hitches (longest first; GPU of the frame and the two after it)')
        for r in listed:
            causes = hitch_causes(r, base)
            key = re.split(r' [\d(x+]', causes[0])[0] if causes else '?'
            tally[key] = tally.get(key, 0) + 1
        for r in listed[:20]:
            p('   %7.2f s  %-9s %7.1f,%7.1f  %6.1f ms  GPU %s  %s' % (
                num(r.get('time_s')) or 0, r.get('map') or '-', num(r.get('player_x')) or 0, num(r.get('player_z')) or 0,
                num(r.get('frame_ms_max')) or 0, fmt(num(r.get('gpu_ms')), 1), '; '.join(hitch_causes(r, base)[:3])))
        p('   first cause, all hitches: ' + ', '.join('%s %d' % kv for kv in sorted(tally.items(), key=lambda kv: -kv[1])))
    table, total = pass_table(run)
    if table:
        p('-- GPU passes (per frame, windows with pass times; GPU total %s ms)' % fmt(total, 3))
        for name, avg, p95, share in table:
            p('   %-24s avg %7.3f  p95 %7.3f  %5s%%' % (name, avg, p95, fmt(share, 1)))
    places = worst_places(run)
    if places:
        p('-- worst places (%g m squares, by mean frame time)' % BUCKET_M)
        for g in places:
            dom = '%s %.2f ms' % g['dominant'] if g['dominant'] else '-'
            p('   %-9s %7.1f,%7.1f  %3d win  %6s ms  1%%low %5.0f fps  jitter %s  hitches %d  GPU %s  top pass %s%s' % (
                g['map'], g['x'], g['z'], g['windows'], fmt(g['frame_ms']), g['low'], fmt(g['jitter']), g['hitches'],
                fmt(g['gpu']), dom, '  (cutscene)' if g['cutscene'] else ''))
    growth = slow_growth(run)
    if growth:
        p('-- in the slowest 10% of windows the passes grow most: ' + ', '.join(
            '%s %.2f -> %.2f ms' % (n, a, s) for n, a, s, d in growth[:4] if d > 0))
    cost = timestamp_cost(run)
    if cost:
        p('-- per-pass timestamps cost about %+.3f ms GPU, %+.3f ms frame (%d windows with, %d without)' % (
            cost['gpu'], cost['frame'], cost['with'], cost['without']))


def print_compare(a, b, out=sys.stdout):
    p = lambda *x: print(*x, file=out)
    sa, sb = stability(a), stability(b)
    p('== B %s against A %s' % (b.name, a.name))
    for key, label in (('fps', 'fps'), ('avg', 'frame ms avg'), ('p99', 'frame ms p99'), ('p999', 'frame ms p99.9'),
                       ('max', 'frame ms max'), ('stdev', 'stdev ms'), ('jitter', 'jitter ms'), ('hitches', 'hitches')):
        va, vb = sa.get(key), sb.get(key)
        d = (vb - va) if va is not None and vb is not None else None
        p('   %-16s %10s %10s %+10s' % (label, fmt(va), fmt(vb), fmt(d)))
    ta, total_a = pass_table(a)
    tb, total_b = pass_table(b)
    da, db = {n: v for n, v, _, _ in ta}, {n: v for n, v, _, _ in tb}
    p('   %-16s %10s %10s %10s' % ('GPU total', fmt(total_a, 3), fmt(total_b, 3),
                                    fmt(total_b - total_a, 3) if total_a is not None and total_b is not None else '-'))
    names = sorted(set(da) | set(db), key=lambda n: -abs(db.get(n, 0) - da.get(n, 0)))
    for n in names:
        p('   %-16s %10s %10s %+10.3f' % ('gpu:' + n, fmt(da.get(n), 3), fmt(db.get(n), 3), db.get(n, 0) - da.get(n, 0)))


def bench_rows(folder):
    runs = []
    for f in sorted(os.listdir(folder)):
        if f.endswith('.csv') and not f.endswith('.frames.csv'):
            runs.append(load(os.path.join(folder, f)))
    return runs


def print_bench(folder, out=sys.stdout):
    p = lambda *x: print(*x, file=out)
    runs = bench_rows(folder)
    if not runs:
        p('no recordings in %s' % folder)
        return
    def gpu_of(run):
        _, g = pass_table(run)
        return g if g is not None else mean([num(r.get('gpu_ms')) for r in run.windows])

    # The baseline is recorded first and last: their mean is the reference, their difference the drift.
    bases = [r for r in runs if (r.header.get('note') or '').startswith('baseline')] or runs[:1]
    stats = [stability(r) for r in bases]
    sb = {k: mean([s[k] for s in stats]) for k in ('fps', 'avg', 'p99')}
    gb = mean([gpu_of(r) for r in bases])
    p('== bench %s, baseline (%d recordings): %.1f fps, frame %s ms, p99 %s, GPU %s ms' % (
        folder, len(bases), sb['fps'], fmt(sb['avg']), fmt(sb['p99']), fmt(gb, 3)))
    if len(bases) > 1:
        gs = [gpu_of(r) for r in bases]
        if None not in gs:
            p('   drift between the baselines: GPU %+.3f ms, frame %+.3f ms' % (
                gs[-1] - gs[0], (stats[-1]['avg'] or 0) - (stats[0]['avg'] or 0)))
    lines = []
    for r in runs:
        if r in bases:
            continue
        s = stability(r)
        table, g = pass_table(r)
        g = g if g is not None else mean([num(x.get('gpu_ms')) for x in r.windows])
        note = r.header.get('note') or r.name
        dg = (g - gb) if g is not None and gb is not None else None
        df = (s['avg'] - sb['avg']) if s['avg'] is not None and sb['avg'] is not None else None
        lines.append((dg if dg is not None else 0.0, note, s, g, dg, df))
    # The variant that saves most GPU time names the most expensive feature of the baseline.
    lines.sort(key=lambda x: x[0])
    p('   %-36s %7s %8s %8s %8s %9s %9s' % ('variant', 'fps', 'ms', 'p99', 'GPU', 'dGPU', 'dframe'))
    for _, note, s, g, dg, df in lines:
        p('   %-36s %7.1f %8s %8s %8s %+9s %+9s' % (note[:36], s['fps'], fmt(s['avg']), fmt(s['p99']), fmt(g, 3),
                                                   fmt(dg, 3), fmt(df, 3)))
    p('   (dGPU < 0: the variant is cheaper than the baseline; when it turns a feature off, that is the feature\'s cost)')


# ---- self-test ---------------------------------------------------------------------------------------------------

HEADER = ('time_s,kind,frames,map,room,player_x,player_y,player_z,player_yaw,camera_x,camera_y,camera_z,camera_yaw,'
          'cutscene,fps_avg,fps_1pct_low,frame_ms_avg,frame_ms_max,frame_ms_stdev,jitter_ms,hitches,cpu_main_ms,'
          'cpu_update_ms,cpu_render_ms,wait_ms,gpu_ms,gpu_busy_pct,pass_timing,draws,triangles,patch_triangles,lights,'
          'rt,vram_mb,uploads_pending,pipelines_built,pipeline_build_ms,texture_upload_kb,upscale_swaps,cells_loaded')


def synthetic(slow_x=40.0, scene_extra=0.0):
    lines = [HEADER + ',gpu:scene,gpu:post']
    t = 0.0
    for i in range(80):
        t += 0.25
        x = float(i)
        slow = abs(x - slow_x) < 4
        frame = 10.0 + (6.0 if slow else 0.0)
        scene = 5.0 + (5.0 if slow else 0.0) + scene_extra
        timing = 0 if i % 8 >= 6 else 1
        cells = [t, 'window', 25, 'map0_s01', 3, x, -1, 2.0, 90, x, 1, -2, 90, 0, 1000.0 / frame, 900.0 / frame, frame,
                 frame + 1, 0.5, 0.3, 0, 4, 1, 2, 6, scene + 1.0, '', timing, 300, 50000, '', 4, 1, 900, 0, 0, 0, 10, 0, 0]
        row = ','.join(str(c) for c in cells) + (',%s,1' % scene if timing else ',,')
        lines.append(row)
        if i == 20:
            h = [t, 'hitch', 1, 'map0_s01', 3, x, -1, 2.0, 90, x, 1, -2, 90, 0, 20, 20, 50, 50, 0, 40, 1, '', 1, 30, 6,
                 7, '', 1, 300, 50000, '', 4, 1, 900, 0, 3, 28.5, 10, 0, 0]
            lines.append(','.join(str(c) for c in h) + ',6,1')
    return '\n'.join(lines) + '\n'


def selftest():
    failures = []

    def check(cond, what):
        if not cond:
            failures.append(what)

    run = Run('a.csv', read_rows(synthetic()))
    check(run.passes == ['scene', 'post'], 'pass columns %r' % run.passes)
    check(len(run.hitches) == 1 and len(run.windows) == 80, 'row kinds')
    s = stability(run)
    check(s['hitches'] == 1 and s['source'] == 'rows', 'stability from rows')
    causes = hitch_causes(run.hitches[0], typical(run))
    check(causes[0].startswith('render CPU') or causes[0].startswith('pipeline'), 'hitch cause %r' % causes)
    check(any(c.startswith('pipeline compile x3') for c in causes), 'pipeline cause %r' % causes)
    table, total = pass_table(run)
    check(table[0][0] == 'scene' and abs(total - (table[0][1] + 1.0)) < 1e-6, 'pass table %r %r' % (table, total))
    places = worst_places(run)
    check(places and abs(places[0]['x'] - 44.0) < 8.1 and places[0]['dominant'][0] == 'scene', 'worst place %r' % places[:1])
    growth = slow_growth(run)
    check(growth and growth[0][0] == 'scene', 'slow growth %r' % growth[:1])
    cost = timestamp_cost(run)
    check(cost is not None and cost['with'] > 0 and cost['without'] > 0, 'timestamp cost %r' % cost)
    frames = read_rows('time_s,frame_ms\n' + ''.join('%d,%s\n' % (i, 10 if i % 100 else 40) for i in range(1, 1001)))
    s2 = stability(Run('b.csv', read_rows(synthetic()), frames))
    check(s2['source'] == 'frames' and s2['p50'] == 10 and s2['max'] == 40 and abs(s2['over_2x'] - 1.0) < 1e-9,
          'stability from frames %r' % s2)
    buf = io.StringIO()
    print_compare(run, Run('c.csv', read_rows(synthetic(scene_extra=2.0))), buf)
    check('gpu:scene' in buf.getvalue() and '+2.000' in buf.getvalue(), 'compare output')
    buf = io.StringIO()
    print_summary(run, buf)
    check('worst places' in buf.getvalue() and 'hitches' in buf.getvalue(), 'summary output')
    for f in failures:
        print('FAIL', f)
    print('selftest', 'passed' if not failures else 'FAILED')
    return 0 if not failures else 1


def main(argv):
    if not argv or argv[0] in ('-h', '--help'):
        print(__doc__)
        return 0
    cmd = argv[0]
    if cmd == 'selftest':
        return selftest()
    if cmd == 'compare' and len(argv) == 3:
        print_compare(load(argv[1]), load(argv[2]))
        return 0
    if cmd == 'bench' and len(argv) == 2:
        print_bench(argv[1])
        return 0
    files = argv[1:] if cmd == 'summary' else argv
    if not files:
        print(__doc__)
        return 2
    for f in files:
        print_summary(load(f))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
