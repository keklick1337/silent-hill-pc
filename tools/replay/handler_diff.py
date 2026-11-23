#!/usr/bin/env python3
"""Regression of the map scenarios: every map event handler and the scenario files, against stored references.

Every scenario runs on one build and its state dump (sh_replay.py) is reduced to a
digest that must equal the reference in tests/replay/handlers/<map>.json, recorded from an earlier tree
with update-ref. The log of every run must be clean (no E line, no Lua error, no dialog) and the handler
trace must show no Lua error.

Scenarios:
  <map>/auto     New Game straight onto the map (skip_intros = 2), Cross every 30 ticks:
                 whatever the map starts by itself (opening cutscenes, auto events).
  <map>/slot<N>  the same boot with the complete flags of the map's auto-start events set and the
                 required flags of its repeating auto events cleared (SETFLAG before New Game), then the
                 console command EVENT <idx> AT: the first event-table entry that calls handler slot N
                 is started the way Event_Update starts it, with Harry on the event's map point.
                 One per slot of handlers.json whose name events.lua defines.
  <map>/<name>   the scenarios of tools/replay/*_scenarios.json: console commands, inputs, settings.

A reference holds, per scenario: the number of ticks, a digest of the whole dump, a digest per block of
50 ticks (where the first difference is) and a digest per dump field over the whole run (which fields
differ), and the handler slots that ran. The full dumps stay in <out>/<map>/<scenario>/ of a run;
`compare A B` prints the first divergence of every scenario between two such runs (a run of the old
tree and one of the new) tick by tick and field by field.

Commands:
  run         run the scenarios and compare them with the references; --repeat runs each twice and
              also requires both runs to be equal
  update-ref  run each scenario twice; when the runs agree and the logs are clean, write the references
  compare     first divergence of each scenario between two run directories (--out of two runs)

Exit codes: 0 every scenario equal and clean; 1 differences, missing references or errors; 2 harness
error; 77 skipped (no disc image). Run with python3 -I; standard library only.
"""

import argparse
import concurrent.futures
import glob
import hashlib
import importlib.util
import json
import os
import re
import shutil
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location('sh_replay', os.path.join(HERE, 'sh_replay.py'))
R = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(R)

ROOT = R.ROOT
REFERENCE_DIR = os.path.join(ROOT, 'tests', 'replay', 'handlers')
SCENARIO_FILES = sorted(glob.glob(os.path.join(HERE, '*_scenarios.json')))
DISPLAYS = range(250, 260)
SYS_STATE_EVENT_CALLBACK = 10
TRIGGER_NONE = 0
PENDING_FLAGS_MAX = 24  # the console's remembered SETFLAG list (src/engine/debug/commands/remembered_flags.h)
BLOCK = 50              # ticks per block digest
REF_FORMAT = 'sh-handler-ref'

ALL_MAPS = ('map0_s00 map0_s01 map0_s02 '
            'map1_s00 map1_s01 map1_s02 map1_s03 map1_s04 map1_s05 map1_s06 '
            'map2_s00 map2_s01 map2_s02 map2_s03 map2_s04 '
            'map3_s00 map3_s01 map3_s02 map3_s03 map3_s04 map3_s05 map3_s06 '
            'map4_s00 map4_s01 map4_s02 map4_s03 map4_s04 map4_s05 map4_s06 '
            'map5_s00 map5_s01 map5_s02 map5_s03 '
            'map6_s00 map6_s01 map6_s02 map6_s03 map6_s04 map6_s05 '
            'map7_s00 map7_s01 map7_s02 map7_s03').split()


# ---------------------------------------------------------------------------- map data

def lua_symbols(path):
    """Global functions and assignments at the top level of an events.lua."""
    if not os.path.isfile(path):
        return set()
    with open(path, 'r', encoding='utf-8') as f:
        src = f.read()
    return (set(re.findall(r'^function\s+([A-Za-z_]\w*)\s*\(', src, re.M)) |
            set(re.findall(r'^([A-Za-z_]\w*)\s*=', src, re.M)))


def flag_bits():
    """{flag name: save bit}: events.json names flags; the console's SETFLAG takes the save bit. The bits are the
    original layout (the "event_flags" list of gamedata/original_order.json, from 1); the former names of
    gamedata/flags/*.json ("aliases") resolve too."""
    with open(os.path.join(ROOT, 'gamedata', 'original_order.json'), 'r', encoding='utf-8') as f:
        bits = {name: place + 1 for place, name in enumerate(json.load(f)['event_flags']) if name}
    flags_dir = os.path.join(ROOT, 'gamedata', 'flags')
    for file in sorted(os.listdir(flags_dir)):
        with open(os.path.join(flags_dir, file), 'r', encoding='utf-8') as f:
            for name, entry in json.load(f)['flags'].items():
                for alias in entry.get('aliases', []):
                    if name in bits:
                        bits.setdefault(alias, bits[name])
    return bits


def map_info(build, name):
    """The handler slots events.lua defines a function for, and the event table of one map."""
    base = os.path.join(build, 'gamedata', 'maps', name)
    with open(os.path.join(base, 'handlers.json'), 'r', encoding='utf-8') as f:
        handlers = json.load(f)['handlers']
    events_json = os.path.join(base, 'events.json')
    if not os.path.isfile(events_json):
        raise R.HarnessError('%s: no events.json (imported gamedata needed)' % events_json)
    with open(events_json, 'r', encoding='utf-8') as f:
        events = json.load(f)['events']
    bits = None
    for e in events:
        for key in ('required_flag', 'complete_flag'):
            if isinstance(e.get(key), str):
                bits = bits or flag_bits()
                e[key] = bits[e[key].split(':')[-1]]
    defined = lua_symbols(os.path.join(base, 'events.lua'))
    slots = {}
    for h in handlers:
        # The dispatcher calls the Lua function named by the slot's name (or its symbol).
        if h.get('symbol') and (h['symbol'] in defined or h.get('name') in defined):
            slots[h['index']] = h.get('name') or h['symbol']
    return slots, events


# ---------------------------------------------------------------------------- scenarios

def slot_scenarios(build, maps, ticks, auto_ticks):
    out = []
    for m in maps:
        slots, events = map_info(build, m)
        auto_flags, auto_required = [], []
        for e in events:
            if e['trigger_type'] == TRIGGER_NONE and e['complete_flag'] > 0:
                auto_flags.append((e['complete_flag'], e))
            elif e['trigger_type'] == TRIGGER_NONE and e['required_flag'] > 0:
                # Repeating auto events (no complete flag) run while their required flag
                # is set, which New Game on some maps does (map1_s00 slot 15): clear it.
                auto_required.append((e['required_flag'], e))
        out.append({'map': m, 'name': 'auto', 'flags': [], 'clear_flags': [], 'event': None,
                    'ticks': auto_ticks})
        for slot, label in sorted(slots.items()):
            entry = next((e for e in events if e['sys_state'] == SYS_STATE_EVENT_CALLBACK
                          and e['param'] == slot), None)
            if entry is None:
                continue  # nothing in the event table starts the slot: EVENT cannot reach it
            # The map's own opening events would keep the game out of gameplay (and the
            # event queued); their complete flags skip them, except the tested event's.
            flags = sorted({f for f, e in auto_flags
                            if not (e['sys_state'] == SYS_STATE_EVENT_CALLBACK and e['param'] == slot)
                            and f != entry['complete_flag']})
            clear = sorted({f for f, e in auto_required
                            if not (e['sys_state'] == SYS_STATE_EVENT_CALLBACK and e['param'] == slot)})
            # A complete flag that is also such a required flag would start the repeating
            # event (map1_s00: 78 completes the manhole event 21 and starts slot 15).
            flags = [f for f in flags if f not in clear]
            out.append({'map': m, 'name': 'slot%d' % slot, 'handler': label, 'event': entry['index'],
                        'flags': flags[:PENDING_FLAGS_MAX],
                        'clear_flags': clear[:PENDING_FLAGS_MAX - len(flags[:PENDING_FLAGS_MAX])],
                        'ticks': ticks})
    return out


def file_scenarios(paths, maps):
    """Scenarios of JSON files: a list of {"map", "name", "commands": console lines ("@<seconds> CMD" waits
    on the game's virtual clock), "ticks", optional "flags" (SETFLAG before New Game), "inputs":
    [[tick, "Buttons"|"-"], ...] (default: Cross every 30 ticks), "config": {key: value}}."""
    out = []
    for path in paths:
        with open(path, 'r', encoding='utf-8') as f:
            items = json.load(f)
        for it in items:
            if maps and it['map'] not in maps:
                continue
            out.append({'map': it['map'], 'name': it['name'], 'event': None,
                        'flags': it.get('flags', []), 'clear_flags': [], 'commands': it.get('commands', []),
                        'inputs': it.get('inputs'), 'config': it.get('config', {}),
                        'ticks': it.get('ticks', 3000), 'source': os.path.basename(path)})
    return out


def write_route(sc, path):
    cmds = ['SETFLAG %d 1' % f for f in sc['flags']] + ['SETFLAG %d 0' % f for f in sc['clear_flags']]
    if sc['event'] is not None:
        cmds.append('EVENT %d AT' % sc['event'])
    cmds += sc.get('commands', [])
    lines = ['# handler_diff scenario %s/%s' % (sc['map'], sc['name']),
             '#@config skip_intros = 2', '#@config map = %s' % sc['map']] + \
            ['#@config %s = %s' % kv for kv in sorted(sc.get('config', {}).items())] + \
            ['#@console %s' % c for c in cmds] + \
            ['#@timeout %d' % max(300, sc['ticks'] // 6), '1 -']
    if sc.get('inputs') is not None:
        lines += ['%d %s' % (t, b) for t, b in sc['inputs']]
    else:
        # Cross pages messages, answers selections with the default entry and is the action
        # button; 4 ticks held, 26 released.
        for t in range(200, sc['ticks'] - 10, 30):
            lines += ['%d Cross' % t, '%d -' % (t + 4)]
    lines.append('end %d' % sc['ticks'])
    with open(path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')


def read_trace(path):
    """[(tick, map, slot, how, symbol)] of SH_SCRIPT_HANDLER_TRACE."""
    rows = []
    if os.path.isfile(path):
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            for line in f:
                p = line.split()
                if len(p) == 5 and p[0].isdigit():
                    rows.append((int(p[0]), p[1], int(p[2]), p[3], p[4]))
    return rows


# ---------------------------------------------------------------------------- digests

def _h(data, size=4):
    return hashlib.blake2b(data, digest_size=size).hexdigest()


def digest(lines):
    """The reference form of a dump: whole, per block of BLOCK ticks, per field over the run."""
    fields = {}
    for i, line in enumerate(lines):
        for tok in line.split(b' '):
            key, sep, value = tok.partition(b'=')
            if sep:
                fields.setdefault(key.decode('ascii', 'replace'), []).append(b'%d:%s' % (i, value))
    return {
        'ticks': len(lines),
        'dump': _h(b'\n'.join(lines), 16),
        'blocks': [_h(b'\n'.join(lines[i:i + BLOCK])) for i in range(0, len(lines), BLOCK)],
        'fields': {k: _h(b'\n'.join(v)) for k, v in sorted(fields.items())},
    }


def digest_diff(ref, now):
    """None when equal, else a short report: the first differing block and the differing fields."""
    if ref['dump'] == now['dump'] and ref['ticks'] == now['ticks']:
        return None
    first = next((i for i, (a, b) in enumerate(zip(ref['blocks'], now['blocks'])) if a != b), None)
    if first is None:
        span = 'the dumps agree for %d ticks, then one ends (%d / %d ticks)' % (
            min(ref['ticks'], now['ticks']), ref['ticks'], now['ticks'])
    else:
        span = 'first difference in ticks %d-%d' % (first * BLOCK + 1, (first + 1) * BLOCK)
    keys = sorted(set(ref['fields']) | set(now['fields']))
    changed = [k for k in keys if ref['fields'].get(k) != now['fields'].get(k)]
    return '%s; fields: %s' % (span, ' '.join(changed) or '-')


# ---------------------------------------------------------------------------- runs

def run_once(build, sc, d, video, tag):
    """One run: (dump lines, digest, slots that ran, error lines)."""
    route_path = os.path.join(d, '%s_%s.replay' % (sc['map'], sc['name']))
    write_route(sc, route_path)
    route = R.Route(route_path)
    trace = os.path.join(d, 'trace.%s.txt' % tag)
    dump = os.path.join(d, '%s.dump' % tag)
    if os.path.exists(trace):
        os.remove(trace)
    R.run_route(build, route, dump, d, video, extra_env={'SH_SCRIPT_HANDLER_TRACE': trace})
    lines = R.read_dump(dump)
    rows = read_trace(trace)
    ran = sorted({'%s:%d' % (r[1], r[2]) for r in rows if r[3] in ('lua', 'lua-error')})
    errors = R.log_errors(dump + '.log')
    errors += ['handler %s:%d raised a Lua error at tick %d' % (r[1], r[2], r[0]) for r in rows if r[3] == 'lua-error']
    return lines, digest(lines), ran, errors


def run_scenario(build, sc, work, video, runs):
    d = os.path.join(work, sc['map'], sc['name'])
    if os.path.exists(d):
        shutil.rmtree(d)
    os.makedirs(d)
    res = {'map': sc['map'], 'name': sc['name'], 'ticks': sc['ticks']}
    for key in ('handler', 'event', 'source'):
        if sc.get(key) is not None:
            res[key] = sc[key]
    started = time.time()
    try:
        first = None
        errors = []
        for i in range(runs):
            lines, dg, ran, errs = run_once(build, sc, d, video, 'run%d' % (i + 1))
            errors += errs
            if first is None:
                first = (lines, dg, ran)
            elif lines != first[0]:
                res['self_equal'] = False
                res['self_diff'] = digest_diff(first[1], dg)
        if runs > 1 and 'self_equal' not in res:
            res['self_equal'] = True
        res['digest'], res['ran'] = first[1], first[2]
        res['errors'] = errors[:20]
        res['error_count'] = len(errors)
    except R.HarnessError as e:
        res['harness_error'] = str(e)
    res['seconds'] = round(time.time() - started, 1)
    return res


def run_all(build, scenarios, work, jobs, runs, on_result):
    """Runs the scenarios on `jobs` parallel games; on_result(result) under a lock as each ends."""
    lock = threading.Lock()
    videos = []
    candidates = R.display_range(DISPLAYS)
    try:
        for _ in range(jobs):
            v = R.Video(candidates=[n for n in candidates if n not in {x.display for x in videos}])
            try:
                v.__enter__()
            except R.HarnessError:
                if videos:
                    break
                raise
            videos.append(v)
        free = list(videos)

        def job(sc):
            with lock:
                v = free.pop()
            try:
                return run_scenario(build, sc, work, v, runs)
            finally:
                with lock:
                    free.append(v)

        pool = concurrent.futures.ThreadPoolExecutor(len(videos))
        try:
            futs = [pool.submit(job, sc) for sc in scenarios]
            for fut in concurrent.futures.as_completed(futs):
                r = fut.result()
                with lock:
                    on_result(r)
        except BaseException:
            # Ctrl+C or SIGTERM: queued scenarios are dropped and running games killed.
            pool.shutdown(wait=False, cancel_futures=True)
            R.stop_all_games()
            raise
        pool.shutdown(wait=True)
    finally:
        for v in videos:
            v.__exit__(None, None, None)


# ---------------------------------------------------------------------------- references

def reference_path(map_name):
    return os.path.join(REFERENCE_DIR, '%s.json' % map_name)


def load_reference(map_name):
    path = reference_path(map_name)
    if not os.path.isfile(path):
        return {}
    with open(path, 'r', encoding='utf-8') as f:
        doc = json.load(f)
    if doc.get('format') != REF_FORMAT or doc.get('block') != BLOCK:
        raise R.HarnessError('%s: not a %s file with %d-tick blocks' % (path, REF_FORMAT, BLOCK))
    return doc['scenarios']


def write_reference(map_name, scenarios):
    os.makedirs(REFERENCE_DIR, exist_ok=True)
    doc = {'format': REF_FORMAT, 'version': 1, 'map': map_name, 'block': BLOCK,
           'description': 'Digests of the state dumps of the map scenarios of tools/replay/handler_diff.py. '
                          'Written by handler_diff.py update-ref; do not edit.',
           'scenarios': {k: scenarios[k] for k in sorted(scenarios, key=scenario_order)}}
    with open(reference_path(map_name), 'w', encoding='utf-8') as f:
        json.dump(doc, f, indent=1, sort_keys=False)
        f.write('\n')


def scenario_order(name):
    m = re.fullmatch(r'slot(\d+)', name)
    return (0, 0, '') if name == 'auto' else ((1, int(m.group(1)), '') if m else (2, 0, name))


def ref_entry(r):
    return {'ticks': r['digest']['ticks'], 'dump': r['digest']['dump'], 'ran': r['ran'],
            'blocks': r['digest']['blocks'], 'fields': r['digest']['fields']}


# ---------------------------------------------------------------------------- commands

def scenarios_for(args, build):
    maps = args.maps or ALL_MAPS
    for m in maps:
        if m not in ALL_MAPS:
            raise R.HarnessError('unknown map %s' % m)
    scenarios = slot_scenarios(build, maps, args.ticks, args.auto_ticks)
    if not args.no_files:
        scenarios += file_scenarios(SCENARIO_FILES, set(args.maps))
    if args.only:
        wanted = set(args.only)
        scenarios = [s for s in scenarios if '%s/%s' % (s['map'], s['name']) in wanted]
    seen = set()
    for s in scenarios:
        key = (s['map'], s['name'])
        if key in seen:
            raise R.HarnessError('two scenarios are called %s/%s' % key)
        seen.add(key)
    return scenarios


def describe(r):
    if 'harness_error' in r:
        return 'ERROR %s' % r['harness_error']
    parts = [r['verdict']]
    if r.get('self_equal') is False:
        parts.append('NOT REPEATABLE (%s)' % r['self_diff'])
    if r.get('error_count'):
        parts.append('%d error line(s): %s' % (r['error_count'], r['errors'][0]))
    return ', '.join(parts)


def cmd_run(args):
    build = os.path.abspath(args.build)
    R.check_build(build)
    scenarios = scenarios_for(args, build)
    work = os.path.abspath(args.out)
    os.makedirs(work, exist_ok=True)
    refs = {m: load_reference(m) for m in sorted({s['map'] for s in scenarios})}
    runs = 2 if args.repeat else 1
    print('%d scenarios, %d run(s) each, %d jobs' % (len(scenarios), runs, args.jobs), flush=True)
    results = {}
    done = [0]

    def on_result(r):
        ref = refs.get(r['map'], {}).get(r['name'])
        if 'digest' in r:
            if ref is None:
                r['verdict'] = 'NO REFERENCE'
            else:
                diff = digest_diff(ref, r['digest'])
                r['verdict'] = 'equal' if diff is None else 'DIFFERS: ' + diff
                if diff is None and ref.get('ran') != r['ran']:
                    r['verdict'] = 'DIFFERS: slots ran %s, reference %s' % (r['ran'], ref.get('ran'))
        results['%s/%s' % (r['map'], r['name'])] = r
        done[0] += 1
        print('[%d/%d] %s/%s: %s, %.0f s' % (done[0], len(scenarios), r['map'], r['name'], describe(r),
                                             r['seconds']), flush=True)

    run_all(build, scenarios, work, args.jobs, runs, on_result)
    run_keys = {(s['map'], s['name']) for s in scenarios}
    stale = []
    if not args.only:
        for m, ref in refs.items():
            for name in ref:
                if (m, name) not in run_keys and not (args.no_files and not re.fullmatch(r'auto|slot\d+', name)):
                    stale.append('%s/%s' % (m, name))
    with open(os.path.join(work, 'results.json'), 'w', encoding='utf-8') as f:
        json.dump({'build': build, 'scenarios': [results[k] for k in sorted(results)], 'stale': stale}, f, indent=1)
    bad = [r for r in results.values() if 'harness_error' in r or r.get('verdict') != 'equal'
           or r.get('self_equal') is False or r.get('error_count')]
    equal = sum(1 for r in results.values() if r.get('verdict') == 'equal')
    errors = sum(r.get('error_count', 0) for r in results.values())
    slots = len({s for r in results.values() for s in r.get('ran', [])})
    for name in stale:
        print('stale reference: %s (no such scenario any more)' % name)
    print('summary: %d scenarios, %d equal, %d not equal or failed, %d error line(s), %d stale reference(s), '
          '%d handler slots ran; results %s' % (len(results), equal, len(bad), errors, len(stale), slots,
                                               os.path.join(work, 'results.json')))
    return 1 if bad or stale else 0


def cmd_update_ref(args):
    build = os.path.abspath(args.build)
    R.check_build(build)
    scenarios = scenarios_for(args, build)
    work = os.path.abspath(args.out)
    os.makedirs(work, exist_ok=True)
    print('%d scenarios, 2 runs each, %d jobs' % (len(scenarios), args.jobs), flush=True)
    results = []
    done = [0]

    def on_result(r):
        results.append(r)
        done[0] += 1
        ok = 'harness_error' not in r and r.get('self_equal') and not r.get('error_count')
        r['verdict'] = 'recorded' if ok else 'NOT RECORDED'
        print('[%d/%d] %s/%s: %s, %.0f s' % (done[0], len(scenarios), r['map'], r['name'], describe(r),
                                             r['seconds']), flush=True)

    run_all(build, scenarios, work, args.jobs, 2, on_result)
    failed = [r for r in results if r['verdict'] != 'recorded']
    if failed:
        print('%d scenario(s) not repeatable or not clean: no reference written' % len(failed))
        return 1
    by_map = {}
    for r in results:
        by_map.setdefault(r['map'], {})['%s' % r['name']] = ref_entry(r)
    for m, entries in sorted(by_map.items()):
        old = load_reference(m)
        if not args.only and not args.no_files:
            merged = entries  # a full run of the map replaces its file: dropped scenarios go
        else:
            merged = dict(old)
            merged.update(entries)
        changed = sorted(k for k in merged if old.get(k, {}).get('dump') != merged[k]['dump'])
        write_reference(m, merged)
        print('%s: %d scenarios, %d new or changed%s' % (m, len(merged), len(changed),
                                                         (': ' + ' '.join(changed[:12])) if changed else ''))
    return 0


def cmd_compare(args):
    """First divergence of every scenario present in both run directories."""
    differ = 0
    a_dir, b_dir = os.path.abspath(args.a), os.path.abspath(args.b)
    for path in sorted(glob.glob(os.path.join(a_dir, '*', '*', 'run1.dump'))):
        rel = os.path.relpath(path, a_dir)
        other = os.path.join(b_dir, rel)
        if not os.path.isfile(other):
            continue
        la, lb = R.read_dump(path), R.read_dump(other)
        name = os.path.dirname(rel)
        if la == lb:
            continue
        differ += 1
        print('== %s' % name)
        R.compare_dumps(la, lb, 'a', 'b')
    print('%d scenario(s) differ' % differ)
    return 1 if differ else 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)

    def common(sp):
        sp.add_argument('--build', required=True)
        sp.add_argument('--out', default=os.path.join(ROOT, '.tmp', 'handler-diff'),
                        help='run directories, dumps, logs and results.json')
        sp.add_argument('-j', '--jobs', type=int, default=4,
                        help='parallel games (offscreen; on the Xvfb fallback each on its own display in '
                             ':250-:259 or $SH_XVFB_DISPLAYS)')
        sp.add_argument('--ticks', type=int, default=1500, help='length of a slot scenario')
        sp.add_argument('--auto-ticks', type=int, default=3000, help='length of a map auto scenario')
        sp.add_argument('--only', action='append', help='map/scenario to run (repeatable; default all)')
        sp.add_argument('--no-files', action='store_true', help='skip the scenarios of tools/replay/*_scenarios.json')
        sp.add_argument('maps', nargs='*', help='maps (default: all 43)')

    sp = sub.add_parser('run', help='run the scenarios and compare them with the references')
    common(sp)
    sp.add_argument('--repeat', action='store_true', help='run every scenario twice; the runs must be equal')
    sp.set_defaults(func=cmd_run)
    sp = sub.add_parser('update-ref', help='record the references (two equal, clean runs per scenario)')
    common(sp)
    sp.set_defaults(func=cmd_update_ref)
    sp = sub.add_parser('compare', help='first divergence per scenario between two run directories')
    sp.add_argument('a')
    sp.add_argument('b')
    sp.set_defaults(func=cmd_compare)
    args = p.parse_args()
    R.install_term_handler()
    try:
        return args.func(args)
    except KeyboardInterrupt:
        R.stop_all_games()
        raise
    except R.Skip as e:
        print('skipped: %s' % e)
        return R.EXIT_SKIP
    except R.HarnessError as e:
        print('error: %s' % e)
        return 2


if __name__ == '__main__':
    sys.exit(main())
