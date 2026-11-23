#!/usr/bin/env python3
"""Generates gamedata/maps/<map>/tables.json, the per-map data tables of the map overlays.

tools/scripts/map_tables_manifest.json lists, per map, the symbols whose data lives in the PSX
overlay (name, va, type, count, bytes, and the relocation layout of pointer tables). For every map this
writes gamedata/maps/<map>/tables.json (format sh-map-tables/1), which sh::maps::MapTables reads to fill
the arrays from gamedata/raw/<bin> at va - base. No disc data is written into the repository: the
values come from the imported disc.

Modes:
  gen_map_tables.py                    write the tables.json files
  gen_map_tables.py --check            fail when a file differs from what would be written (the readable
                                       data map_tables_data.py adds is not compared), or the
                                       manifest names a type or layout the engine does not know
  gen_map_tables.py --check-exe-owned DIR  the "exe_owned" symbols (the executable keeps and clears
                                       them at each map load) are zero in DIR/raw/<bin> of every map
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "tools/scripts/map_tables_manifest.json"


def load_manifest(with_exe_owned=False):
    """The manifest; symbols named in "exe_owned" are left out unless asked for: the executable keeps
    the only copy of them and clears it at every map load (their disc bytes are zero in every map,
    --check-exe-owned), so no overlay defines them and no registry lists them."""
    data = json.loads(MANIFEST.read_text())
    if with_exe_owned:
        return data["base"], data["maps"]
    owned = set(data.get("exe_owned", {}).get("names", []))
    maps = {}
    for map_name, entry in data["maps"].items():
        entry = dict(entry)
        entry["symbols"] = [s for s in entry["symbols"] if s["name"] not in owned]
        maps[map_name] = entry
    return data["base"], maps


def check_exe_owned(gamedata):
    """The exe-owned words start at zero on every map load: true only while the disc bytes are zero."""
    data = json.loads(MANIFEST.read_text())
    owned = set(data.get("exe_owned", {}).get("names", []))
    base = int(data["base"], 16)
    total = bad = 0
    for map_name, entry in data["maps"].items():
        raw = (pathlib.Path(gamedata) / "raw" / entry["bin"]).read_bytes()
        for sym in entry["symbols"]:
            if sym["name"] not in owned:
                continue
            total += 1
            off = int(sym["va"], 16) - base
            disc = raw[off:off + sym["bytes"]]
            if len(disc) != sym["bytes"] or any(disc):
                print(f"{map_name}.{sym['name']}: disc bytes are not zero", file=sys.stderr)
                bad += 1
    print(f"map tables --check-exe-owned: {total - bad}/{total} exe-owned symbols are zero on the disc")
    return bad == 0


ANIM_INFO_FIELDS = [(0, "playbackFunc", "ptr"), (4, "status", 1), (5, "hasVariableDuration", 1),
                    (6, "linkStatus", 1), (8, "duration", "ptr_or_value"), (12, "startKeyframeIdx", 2),
                    (14, "endKeyframeIdx", 2)]


def _beam_end(i, psx):
    return [(psx, f"field_4[{i}].vec_0", "ptr"), (psx + 4, f"field_4[{i}].field_4", 4),
            (psx + 8, f"field_4[{i}].vec_8", "ptr"), (psx + 12, f"field_4[{i}].field_C", 4),
            (psx + 16, f"field_4[{i}].pos_10", 16)]


RELOC_TYPES = {
    "s_AnimInfo": (16, ANIM_INFO_FIELDS),
    # map7_s03's boss light beam steps
    "BossFxStep": (68, [(0, "field_0", 4)] + _beam_end(0, 4) + _beam_end(1, 36)),
}
# Game functions and live game variables a data-only map's tables may point at (the same file), by the
# manifest's external expression.
GAME_FUNCTIONS = {"Anim_PlaybackOnce", "Anim_PlaybackLoop", "Anim_BlendLinear", "Player_VariableAnimDurationGet"}
GAME_VARIABLES = {"&g_SysWork.npcs[2].position": "boss_slot_position", "&D_800F4B30": "boss_bone_position",
                  "&g_BossBonePosition": "boss_bone_position"}
FLAT_TYPES = {"u8", "s8", "u16", "s16", "u32", "s32", "VECTOR3", "s_Pose", "s_WorldObjectPose", "s_UnkStruct3_Mo"}
# Types of the arrays that have no place on the disc ("port_word"): layouts of the running game, with pointers.
PORT_TYPES = {"BoneCoord", "BossFxPattern"}


def tables_json_path(map_name):
    return ROOT / "gamedata/maps" / map_name / "tables.json"


def map_place(map_name):
    """Where the map is in the game, from the map list comment of assets/config.cfg ("#   map1_s00  School - ...")."""
    text = (ROOT / "assets/config.cfg").read_text(encoding="utf-8")
    m = re.search(r"^#\s+" + re.escape(map_name) + r"\s+(.+?)\s*$", text, re.M)
    return m.group(1) if m else map_name


def tables_description(map_name, entry, symbols, has_variables):
    """The "description" of a data-only map's tables.json: written for modders, specific to the map."""
    readable = [s["symbol"] for s in symbols if re.fullmatch(r"[a-z][a-z0-9_]*", s["symbol"])]
    shown = ", ".join(readable[:12]) + (f" and {len(readable) - 12} more" if len(readable) > 12 else "")
    ported = [s["symbol"] for s in symbols if s.get("port_word")]
    text = (f"Disc arrays of {map_name} ({map_place(map_name)}): the data tables of this map's PS1 overlay "
            f"{entry['bin']} (imported from the disc into gamedata/raw/) that the engine, the map's Lua and its "
            f"map_vars.json and world_objects.json read. When the map loads, the engine creates every array of "
            f"\"symbols\" ({len(symbols)} here) and fills it from the overlay at offset va - base: \"va\" is the "
            f"array's PS1 address and \"base\" the overlay's load address (hex strings), \"type\" the element type "
            f"(u8/s16/s32..., or a record such as VECTOR3 or s_Pose whose positions are Q12 metres and angles Q12 "
            f"turns), \"count\" the number of elements and \"bytes\" how many bytes are copied (a record array "
            f"without it is translated field by field). \"symbol\" is the name everything else finds the array by")
    text += f"; the readable ones on this map: {shown}." if readable else "."
    if ported:
        text += (" \"port_word\": true marks a word the engine keeps itself (" + ", ".join(ported) +
                 "): it has no place on the disc and starts at zero on every visit of the map.")
    text += (" \"functions\"" + (" and \"variables\"" if has_variables else "") + " name the game functions"
             + (" and variables" if has_variables else "") + " whose PS1 addresses the arrays hold, so those "
             "pointers are translated when the arrays are filled. Generated by tools/scripts/gen_map_tables.py "
             "from tools/scripts/map_tables_manifest.json: change the manifest and regenerate instead of editing "
             "this file; a mod patches single arrays by \"symbol\".")
    return text


def generate_tables_json(map_name, entry, base):
    """gamedata/maps/<map>/tables.json of a data-only map (no overlay library): the registry that the
    generated <map>_extracted_data.cpp of a library map carries, as data."""
    symbols = []
    for sym in entry["symbols"]:
        if "alias" in sym:
            raise SystemExit(f"{map_name}.{sym['name']}: an alias cannot be a data-only map's table")
        if sym.get("port_word"):
            # A word of the engine's own (no place on the disc): zero storage for each visit of the map.
            if sym["type"] not in FLAT_TYPES and sym["type"] not in PORT_TYPES:
                raise SystemExit(f"{map_name}.{sym['name']}: type {sym['type']} is not known to the executable")
            symbols.append({"symbol": sym["name"], "type": sym["type"], "count": sym["count"], "port_word": True})
            continue
        out = {"symbol": sym["name"], "va": "0x%08X" % int(sym["va"], 16), "type": sym["type"],
               "count": sym["count"]}
        if "reloc" in sym:
            r = sym["reloc"]
            got = [(f["psx_off"], f["member"],
                    "ptr" if f.get("is_ptr") else "ptr_or_value" if f.get("ptr_or_value") else f.get("size", 4))
                   for f in r["fields"]]
            known = RELOC_TYPES.get(r["typedef"])
            if known is None or sym["type"] != r["typedef"] or (r["psx_elem"], got) != known:
                raise SystemExit(f"{map_name}.{sym['name']}: the layout {r['typedef']} is not known to the executable "
                                 f"({', '.join(sorted(RELOC_TYPES))})")
        else:
            if sym["type"] not in FLAT_TYPES:
                raise SystemExit(f"{map_name}.{sym['name']}: type {sym['type']} is not known to the executable")
            out["bytes"] = sym["bytes"]
        symbols.append(out)
    functions = []
    variables = []
    for e in entry.get("externals", []):
        name = e["expr"].lstrip("&")
        if name in GAME_FUNCTIONS:
            functions.append({"va": "0x%08X" % int(e["va"], 16), "symbol": name})
        elif e["expr"] in GAME_VARIABLES:
            variables.append({"va": "0x%08X" % int(e["va"], 16), "symbol": GAME_VARIABLES[e["expr"]]})
        else:
            raise SystemExit(f"{map_name}: external {e['expr']} is not a game function or variable the executable knows")
    doc = {
        "description": tables_description(map_name, entry, symbols, bool(variables)),
        "format": "sh-map-tables/1",
        "bin": entry["bin"],
        "base": base,
        "symbols": symbols,
    }
    if functions:
        doc["functions"] = functions
    if variables:
        doc["variables"] = variables
    return json.dumps(doc, indent=2, ensure_ascii=False) + "\n"


# Keys and the description note that tools/scripts/map_tables_data.py adds after this generator; the check
# compares without them, since this generator does not write them.
DATA_KEYS = {"items", "clips", "reaction_rows", "particle_emitter", "smoke_column", "water_stream", "glass_panes",
             "drips"}
DATA_NOTE_START = " Arrays with a data key ("


def without_data(doc):
    doc = dict(doc)
    doc["description"] = doc.get("description", "").split(DATA_NOTE_START)[0]
    doc["symbols"] = [{k: v for k, v in s.items() if k not in DATA_KEYS} for s in doc.get("symbols", [])]
    return doc


def main():
    args = sys.argv[1:]
    unknown = [a for a in args if a not in ("--check", "--check-exe-owned") and
               not ("--check-exe-owned" in args and args.index(a) == args.index("--check-exe-owned") + 1)]
    if unknown:
        print(__doc__)
        sys.exit(0 if unknown in (["-h"], ["--help"]) else 2)
    if "--check-exe-owned" in args:
        gamedata = args[args.index("--check-exe-owned") + 1]
        sys.exit(0 if check_exe_owned(gamedata) else 1)

    base, maps = load_manifest()
    check = "--check" in args
    wrote = up_to_date = 0
    for map_name, entry in maps.items():
        if not entry.get("data_only"):
            print(f"{map_name}: every map is data-only (tables.json); the manifest entry is not", file=sys.stderr)
            sys.exit(1)
        text = generate_tables_json(map_name, entry, base)
        out = tables_json_path(map_name)
        if check:
            if not out.exists() or without_data(json.loads(out.read_text())) != json.loads(text):
                print(f"{out.relative_to(ROOT)} is out of date: regenerate it", file=sys.stderr)
                sys.exit(1)
            up_to_date += 1
            continue
        out.write_text(text)
        wrote += 1
    if check:
        print(f"map tables: {up_to_date} files up to date")
    else:
        print(f"map tables: wrote {wrote} files (gamedata/maps/*/tables.json)")


if __name__ == "__main__":
    main()
