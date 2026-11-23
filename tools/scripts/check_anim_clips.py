#!/usr/bin/env python3
"""Checks src/formats/convert/anim/anim_clips.json (the named clips of every ANM file) against an import.

anim_clips.json is the source of the clip names (the model converter embeds it to name the glTF
animations; edit it by hand). A clip is one playback entry (kind PlaybackOnce / PlaybackLoop, odd status)
of an animation table: keyframes [start, end] at `fps` keyframes per second. This check rebuilds the
clips of every ANM from:
  - the animation tables of the disc import: tables/anim/*.json (default disc-unpack/tables/anim, or the
    path given as the only argument; at runtime the game reads the same files from gamedata/tables/anim)
  - gamedata/chara/Harry/base_clips.json         Harry's base clip table (HB_BASE.ANM)
  - gamedata/chara/<Name>/character.json "animations": which ANM file a character uses
and requires, per ANM file, the same tables and the same set of clip ranges (start, end, fps, loop,
variable_speed, reverse) as the committed file, and a readable name for every committed clip, unique
within its ANM.

It also requires a clip table of every character whose behaviour steps its animation: a folder
gamedata/chara/<Name>/ with a Lua file that calls anim_step needs "clip_tables" in its character.json,
and every name there is an imported table (tables/anim/<name>.json) or a file under gamedata/chara/.

    python3 -I tools/scripts/check_anim_clips.py [tables-dir]

Exit codes: 0 consistent, 1 differences (listed).
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "src/formats/convert/anim/anim_clips.json"

# Tables whose name is not the snake_case of the character enum, and tables of no character entry.
TABLE_ANM_OVERRIDES = {
    "unkkown_23": ["ANIM/MAR.ANM"],
    "harry_m6s04_extra": ["ANIM/HB_M6S04.ANM"],
}

# Tables whose statuses belong to no enum of their own: numeric clip names (harry_m6s04_extra copies
# its entries into Harry's map table and its rows always used numbers).
TABLES_WITHOUT_ENUM = {
    "harry_m6s04_extra",
}

ENUM_MEMBER = re.compile(r"(\w+?)_(\w+)\s*=\s*(\d+)\s*,?")


def snake(name):
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name).lower()


def camel(name):
    return "".join(w.capitalize() for w in name.split("_"))


def enum_names(table):
    """The clip names live in anim_clips.json only; the rebuilt clips are compared without names."""
    return {}


def clip_name(names, anim):
    name = names.get(anim)
    if name is None:
        return "Anim" + str(anim)
    return name


def parse_table_entries(table, data):
    """The playback clips of an imported anim-table json."""
    names = enum_names(table)
    clips = []
    for e in data["entries"]:
        kind = e["kind"]
        active = bool(e["status"] & 1)  # the status field is the packed (anim << 1) | playing
        if not active or kind not in ("playback_once", "playback_loop"):
            continue
        variable = bool(e.get("variable_duration", False))
        speed = 30.0 if variable else e.get("speed", e.get("speed_q12", 0) / 4096.0)
        start = e["start_kf"]
        end = e["end_kf"]
        if start < 0:
            start = end
        clips.append({
            "name": clip_name(names, e["status"] >> 1),
            "start": min(start, end),
            "end": max(start, end),
            "fps": abs(speed),
            "loop": kind == "playback_loop",
            "variable_speed": variable,
            "reverse": speed < 0 or start > end,
        })
    return clips


# The glTF names of Harry's clips keep the enum names the seed table used; two of them had no name then.
HARRY_LEGACY_NAMES = {"handgun_aim_start": "Unk29", "weapon_attack_2": "Unk30"}


def parse_base_clips(doc):
    """The playback clips of Harry's base clip table (gamedata/chara/Harry/base_clips.json)."""
    clips = []
    for c in doc["clips"]:
        kind = c["kind"]
        if not c["active"] or kind not in ("playback_once", "playback_loop"):
            continue
        variable = c["variable"] or "speed_q12" not in c
        # The imported clips were named with 12 fps for every constant speed (the seed-table parser read the digits
        # of the Q12 macro); kept so the import does not change. The game plays them at speed_q12.
        speed = 30.0 if variable else 12.0
        start = c["start_kf"]
        end = c["end_kf"]
        if start < 0:
            start = end
        anim = c["anim"]
        name = HARRY_LEGACY_NAMES.get(anim) or "".join(part.capitalize() for part in anim.split("_"))
        clips.append({
            "name": name,
            "start": min(start, end),
            "end": max(start, end),
            "fps": abs(speed),
            "loop": kind == "playback_loop",
            "variable_speed": variable,
            "reverse": speed < 0 or start > end,
        })
    return clips


def main():
    if "-h" in sys.argv[1:] or "--help" in sys.argv[1:]:
        print(__doc__.strip())
        return 0
    args = [a for a in sys.argv[1:] if a != "--check"]
    tables_dir = ROOT / (args[0] if args else "disc-unpack/tables/anim")
    if not tables_dir.is_dir():
        print(f"no animation tables in {tables_dir}: run the disc import (sh-import) first", file=sys.stderr)
        sys.exit(1)

    # One folder per character (gamedata/chara/<Name>/character.json); "animations" names the
    # converted clip file, anim/<DIR>/<FILE>.anim.json, which is the disc's <DIR>/<FILE>.ANM.
    anm_by_table = {}
    for path in sorted((ROOT / "gamedata/chara").glob("*/character.json")):
        anim = json.loads(path.read_text()).get("animations") or ""
        if anim.startswith("anim/") and anim.endswith(".anim.json"):
            anm = anim[len("anim/"):-len(".anim.json")] + ".ANM"
            anm_by_table.setdefault(snake(path.parent.name), []).append(anm)

    by_anm = {}

    def add(anm, table, clips):
        entry = by_anm.setdefault(anm, {"tables": [], "clips": []})
        entry["tables"].append(table)
        for clip in clips:
            same = [c for c in entry["clips"] if c["name"] == clip["name"]]
            if any(c["start"] == clip["start"] and c["end"] == clip["end"] for c in same):
                continue
            if same:
                clip = dict(clip, name=clip["name"] + "@" + table)
            entry["clips"].append(clip)

    for path in sorted(tables_dir.glob("*.json")):
        if "@" in path.stem:
            continue  # an overlay variant of the same table, not another set of clips
        table = path.stem
        anms = TABLE_ANM_OVERRIDES.get(table) or sorted(set(anm_by_table.get(table, [])))
        if not anms:
            print("no ANM for table", table, file=sys.stderr)
            continue
        clips = parse_table_entries(path.stem, json.loads(path.read_text()))
        for anm in anms:
            add(anm, table, clips)

    harry = json.loads((ROOT / "gamedata/chara/Harry/base_clips.json").read_text())
    add("ANIM/HB_BASE.ANM", "harry_base", parse_base_clips(harry))

    committed = json.loads(OUT.read_text())["anm"]
    problems = []
    for anm in sorted(set(committed) | set(by_anm)):
        have, want = committed.get(anm), by_anm.get(anm)
        if have is None or want is None:
            problems.append(f"{anm}: {'not in' if have is None else 'only in'} {OUT.relative_to(ROOT)}")
            continue
        if sorted(have["tables"]) != sorted(want["tables"]):
            problems.append(f"{anm}: tables {sorted(have['tables'])}, the import gives {sorted(want['tables'])}")
        ranges_have = {clip_range(c) for c in have["clips"]}
        ranges_want = {clip_range(c) for c in want["clips"]}
        for r in sorted(ranges_want - ranges_have):
            problems.append(f"{anm}: the import has clip {r}, the file does not")
        for r in sorted(ranges_have - ranges_want):
            problems.append(f"{anm}: the file has clip {r}, the import does not")
        names = [c["name"] for c in have["clips"]]
        for name in sorted({n for n in names if names.count(n) > 1}):
            problems.append(f"{anm}: clip name {name!r} is used twice")
        for name in names:
            if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*(@[a-z0-9_]+)?", name):
                problems.append(f"{anm}: clip name {name!r} is not readable")
    problems += clip_table_problems(tables_dir)
    for p in problems:
        print(p, file=sys.stderr)
    print(f"{OUT.relative_to(ROOT)}: {len(committed)} ANM files, "
          f"{sum(len(v['clips']) for v in committed.values())} clips, {len(problems)} problems")
    sys.exit(1 if problems else 0)


def clip_table_problems(tables_dir):
    """A behaviour that calls anim_step fails every tick ("the character has no clip table") without a table."""
    problems = []
    chara = ROOT / "gamedata/chara"
    # Tables the engine registers itself (filled from the disc at import) exist whether or not an import is at hand.
    registry = ROOT / "src/engine/tables/anim_table_registry.cpp"
    registered = set(re.findall(r'^\s*"([a-z0-9_]+)",\s*"[A-Z0-9_]+"', registry.read_text(), re.M)) if registry.is_file() else set()
    for path in sorted(chara.glob("*/character.json")):
        tables = json.loads(path.read_text()).get("clip_tables") or []
        rel = path.relative_to(ROOT)
        for name in tables:
            known = (chara / name).is_file() if name.endswith(".json") else (
                name in registered or (tables_dir / (name + ".json")).is_file())
            if not known:
                problems.append(f"{rel}: clip table {name!r} does not exist")
        steps = [lua.name for lua in sorted(path.parent.glob("*.lua")) if "anim_step" in lua.read_text()]
        if steps and not tables:
            problems.append(f"{rel}: {', '.join(steps)} calls anim_step, the character has no \"clip_tables\"")
    return problems


def clip_range(c):
    return (c["start"], c["end"], c["fps"], c["loop"], c["variable_speed"], c["reverse"])


if __name__ == "__main__":
    main()
