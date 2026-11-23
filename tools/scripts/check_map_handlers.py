#!/usr/bin/env python3
"""Check gamedata/maps/<map>/handlers.json of every map against what the engine's readers accept.

handlers.json is the map's event handler table itself: there is no other
source to regenerate it from. A slot's "shape" tells MapRuntime that the slot has a handler
(src/engine/maps/map_description.cpp, apply_event_handlers); the script dispatcher then calls the
events.lua function named by "name", falling back to "symbol" (src/engine/script/sh_script.cpp).

Rules, per map:
  - top level: "map" equals the folder name, "table" is a string, "handlers" a list; no other keys;
  - every entry has exactly "index", "symbol", "name", "shape"; indices run 0, 1, 2, ... in order and
    stay below the engine's table size (kEventHandlers);
  - an empty slot (shape null) has no symbol and no name;
  - a used slot has shape item, cutscene or script and a readable symbol and name (no
    func_800XXXXX / sharedFunc_* / D_800XXXXX address names; a mod's handler is named the same
    way), the name a global function events.lua defines;
  - one name per slot: two slots of a map do not share a name.

  python3 -I tools/scripts/check_map_handlers.py [--gamedata DIR]

Exit codes: 0 every map is clean, 1 problems (listed). Standard library only.
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAP_NAME = re.compile(r"map\d+_s\d+")
ADDRESS_NAME = re.compile(r"(?:func|sharedFunc)_800[0-9A-Fa-f]{5}\w*|D_800[0-9A-Fa-f]{5}\w*")
SHAPES = ("item", "cutscene", "script")
ENTRY_KEYS = {"index", "symbol", "name", "shape"}
TOP_KEYS = {"map", "table", "handlers"}
MAP_COUNT = 43


def event_handler_limit():
    """kEventHandlers of src/engine/maps/map_runtime.h: the loader rejects an index at or above it."""
    text = (ROOT / "src/engine/maps/map_runtime.h").read_text(encoding="utf-8")
    m = re.search(r"kEventHandlers\s*=\s*(\d+)", text)
    if not m:
        raise SystemExit("src/engine/maps/map_runtime.h: no kEventHandlers")
    return int(m.group(1))


def lua_functions(path: Path):
    """Global functions events.lua defines (`function Name(`, `Name = function`, `Alias = Name`)."""
    if not path.is_file():
        return set()
    text = path.read_text(encoding="utf-8", errors="replace")
    defined = set(re.findall(r"^[^\S\n]*function\s+([A-Za-z_]\w*)\s*\(", text, re.M)) | set(
        re.findall(r"^[^\S\n]*([A-Za-z_]\w*)\s*=\s*function\b", text, re.M))
    for alias, target in re.findall(r"^([A-Za-z_]\w*)\s*=\s*([A-Za-z_]\w*)\s*$", text, re.M):
        if target in defined:
            defined.add(alias)
    return defined


def check_map(map_dir: Path, limit: int, problems: list):
    name = map_dir.name
    path = map_dir / "handlers.json"
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        problems.append(f"{name}: handlers.json: {e}")
        return 0
    if not isinstance(doc, dict):
        problems.append(f"{name}: handlers.json is not an object")
        return 0
    for key in sorted(set(doc) - TOP_KEYS):
        problems.append(f"{name}: unknown key {key!r}")
    if doc.get("map") != name:
        problems.append(f"{name}: \"map\" is {doc.get('map')!r}")
    if not isinstance(doc.get("table"), str):
        problems.append(f"{name}: \"table\" must be a string")
    handlers = doc.get("handlers")
    if not isinstance(handlers, list):
        problems.append(f"{name}: no \"handlers\" list")
        return 0
    lua = lua_functions(map_dir / "events.lua")
    seen = {}
    used = 0
    for pos, h in enumerate(handlers):
        where = f"{name}[{pos}]"
        if not isinstance(h, dict):
            problems.append(f"{where}: not an object")
            continue
        if set(h) != ENTRY_KEYS:
            problems.append(f"{where}: keys {sorted(h)}, expected {sorted(ENTRY_KEYS)}")
        index = h.get("index")
        if not isinstance(index, int) or isinstance(index, bool) or index != pos:
            problems.append(f"{where}: \"index\" {index!r}: slots run 0, 1, 2, ... in order")
        if pos >= limit:
            problems.append(f"{where}: beyond the engine's {limit} handler slots")
        shape, symbol, label = h.get("shape"), h.get("symbol"), h.get("name")
        if shape is None:
            if symbol is not None or label is not None:
                problems.append(f"{where}: an empty slot (no shape) names {label or symbol!r}")
            continue
        used += 1
        if shape not in SHAPES:
            problems.append(f"{where}: shape {shape!r} is not one of {', '.join(SHAPES)}")
        if not isinstance(symbol, str) or not symbol or ADDRESS_NAME.fullmatch(symbol):
            problems.append(f"{where}: a used slot needs a readable \"symbol\" (got {symbol!r})")
        if not isinstance(label, str) or not label or ADDRESS_NAME.fullmatch(label):
            problems.append(f"{where}: a used slot needs a readable \"name\" (got {label!r})")
            continue
        if label not in lua and symbol not in lua:
            problems.append(f"{where}: {label!r} is not a function of {name}/events.lua")
        if label in seen:
            problems.append(f"{where}: name {label!r} is also slot {seen[label]}")
        seen[label] = pos
    return used


def main() -> int:
    args = sys.argv[1:]
    gamedata = ROOT / "gamedata"
    if "--gamedata" in args:
        gamedata = Path(args[args.index("--gamedata") + 1]).resolve()
    maps_dir = gamedata / "maps"
    maps = sorted(d for d in maps_dir.iterdir() if d.is_dir() and MAP_NAME.fullmatch(d.name))
    problems = []
    if len(maps) != MAP_COUNT:
        problems.append(f"expected {MAP_COUNT} maps in {maps_dir}, found {len(maps)}")
    limit = event_handler_limit()
    slots = sum(check_map(d, limit, problems) for d in maps)
    for p in problems:
        print(f"error: {p}", file=sys.stderr)
    print(f"handlers.json: {len(maps)} maps, {slots} used slots, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
