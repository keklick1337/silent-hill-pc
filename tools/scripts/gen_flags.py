#!/usr/bin/env python3
"""Check and normalise the event flag declarations: gamedata/flags/*.json and the original save layout
(the "event_flags" list of gamedata/original_order.json).

A flag is identified by its name only. gamedata/flags/<area>.json declare the flags of an area (game, map0..map7,
paper_map_marks, unused) with a description and their former names:
  "aliases"         former names that still name the flag everywhere (old saves, scripts, mods);
  "old_save_names"  former names that name another flag now: read only from saves written before the rename.
The engine gives every flag its save bit itself. The base flags the original game has keep the bit of the original
save layout (reading PS1 memory cards needs it): the n-th name of "event_flags" is save
bit n, from 1. Nothing else depends on the order of the names.

Checks (--check, the default for the gate) and, without it, rewrites the declaration files in their canonical form
(each file sorted by save bit, then the flags the layout does not place, in file order; one flag per line):
  - every layout name is declared in exactly one file, every declared name is unique;
  - names are PascalCase words joined by "_" (M1S03_ShotgunShells0, MapMark_OldTown_SchoolCircle), never Flag<N>;
  - an alias is no current name, no alias is listed twice;
  - a placeholder alias Flag<N> belongs to the flag at save bit N (the name old saves and scripts wrote for it);
  - an "old_save_names" entry is the current name of another flag;
  - the descriptions contain no Flag<N> (a description names flags by their names).
--upstream <event_flags.h> also lists the e_EventFlag names of the decompilation that differ from ours
(information only: a name is adopted by hand, with the former name as an alias). The header is
include/bodyprog/events/event_flags.h of a checkout of the decompilation, which is not part of this repository.

Usage: python3 -I tools/scripts/gen_flags.py [--check] [--upstream <event_flags.h>]
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FLAGS_DIR = ROOT / "gamedata/flags"
ORDER = ROOT / "gamedata/original_order.json"
NAME = re.compile(r"[A-Z][A-Za-z0-9]*(?:_[A-Z0-9][A-Za-z0-9]*)*")
PLACEHOLDER = re.compile(r"Flag\d+")
ENTRY_RE = re.compile(r"^\s*Ev(?:ent|net)Flag_(\w+)\s*=\s*(\d+)\s*,?")
KEYS = ("description", "aliases", "old_save_names")


def load():
    layout = json.loads(ORDER.read_text(encoding="utf-8")).get("event_flags", [])
    files = {}
    for path in sorted(FLAGS_DIR.glob("*.json")):
        files[path] = json.loads(path.read_text(encoding="utf-8"))
    return layout, files


def check(layout, files):
    errors = []
    declared = {}
    for path, doc in files.items():
        for name, entry in doc.get("flags", {}).items():
            if name in declared:
                errors.append(f"{name} is declared in {declared[name].name} and {path.name}")
            declared[name] = path
            if not NAME.fullmatch(name) or PLACEHOLDER.fullmatch(name):
                errors.append(f"{path.name}: {name} is not a readable PascalCase name")
            unknown = set(entry) - set(KEYS)
            if unknown:
                errors.append(f"{path.name}: {name} has unknown keys {sorted(unknown)}")
            if PLACEHOLDER.search(entry.get("description", "")):
                errors.append(f"{path.name}: {name}: the description names a flag by its placeholder")
    bit_of = {}
    for bit, name in enumerate(layout, 1):
        if name is None:
            continue
        if name in bit_of:
            errors.append(f"event_flags lists {name} at bits {bit_of[name]} and {bit}")
        bit_of[name] = bit
        if name not in declared:
            errors.append(f"event_flags: {name} (bit {bit}) is declared in no gamedata/flags file")
    aliases = {}
    for path, doc in files.items():
        for name, entry in doc.get("flags", {}).items():
            for a in entry.get("aliases", []):
                if a in declared:
                    errors.append(f"{path.name}: alias {a} of {name} is a current name")
                if a in aliases:
                    errors.append(f"{path.name}: alias {a} is listed for {aliases[a]} and {name}")
                aliases[a] = name
            for a in entry.get("old_save_names", []):
                if a not in declared or a == name:
                    errors.append(f"{path.name}: old_save_names {a} of {name} is no other flag's current name")
    # A placeholder alias names the flag of its own bit, nothing else: old saves and scripts wrote it for that bit.
    for alias, name in aliases.items():
        m = PLACEHOLDER.fullmatch(alias)
        if m and bit_of.get(name) != int(alias[4:]):
            errors.append(f"alias {alias} names {name}, which is save bit {bit_of.get(name)}")
    return errors


def render(doc, order_of):
    flags = doc.get("flags", {})
    names = sorted(flags, key=lambda n: (order_of.get(n, len(order_of) + 1), list(flags).index(n)))
    lines = ["{"]
    for key, value in doc.items():
        if key == "flags":
            continue
        lines.append(f"  {json.dumps(key)}: {json.dumps(value, ensure_ascii=False)},")
    lines.append('  "flags": {')
    for i, name in enumerate(names):
        entry = {k: flags[name][k] for k in KEYS if k in flags[name]}
        comma = "," if i + 1 < len(names) else ""
        lines.append(f"    {json.dumps(name)}: {json.dumps(entry, ensure_ascii=False)}{comma}")
    lines += ["  }", "}"]
    return "\n".join(lines) + "\n"


def upstream_report(path, layout):
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        m = ENTRY_RE.match(line)
        if not m or m.group(1).isdigit() or m.group(1) in ("None", "Count", "Hack"):
            continue
        bit = int(m.group(2))
        if 0 < bit <= len(layout) and layout[bit - 1] != m.group(1):
            print(f"upstream: bit {bit} is {m.group(1)}, ours {layout[bit - 1]}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true", help="check only, do not rewrite")
    ap.add_argument("--upstream", help="include/bodyprog/events/event_flags.h of a checkout of the decompilation: "
                                        "list the names that differ")
    args = ap.parse_args()
    layout, files = load()
    errors = check(layout, files)
    for e in errors:
        print("flags:", e)
    order_of = {name: bit for bit, name in enumerate(layout, 1) if name is not None}
    stale = [p for p, doc in files.items() if p.read_text(encoding="utf-8") != render(doc, order_of)]
    if args.check:
        for p in stale:
            print(f"flags: {p.relative_to(ROOT)} is not in its canonical form (run without --check)")
    elif not errors:
        for p in stale:
            p.write_text(render(files[p], order_of), encoding="utf-8")
    if args.upstream:
        upstream_report(args.upstream, layout)
    total = sum(len(doc.get("flags", {})) for doc in files.values())
    print(f"flags: {total} declared in {len(files)} files, {len(layout)} bits in the original layout")
    return 1 if errors or (args.check and stale) else 0


if __name__ == "__main__":
    sys.exit(main())
