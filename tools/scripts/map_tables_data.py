#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes the readable data of gamedata/maps/*/tables.json arrays from an imported disc.

The arrays it converts - Harry's map clips (s_AnimInfo), his reaction rows, the loadable items and the map
effect parameter blocks - get a data key with named records (engine/maps/map_table_data.h); the engine then
fills them from the data instead of the disc. Every converted array is encoded back and compared with the
disc's bytes: a mismatch stops the script without writing.

    python3 -I tools/scripts/map_tables_data.py --gamedata <imported gamedata> [--check]

--check only verifies that the data in the repository encodes to the disc's bytes.
"""

import argparse
import json
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASE = 0x800C9578

PLAYBACK = {"Anim_BlendLinear": "blend", "Anim_PlaybackOnce": "once", "Anim_PlaybackLoop": "loop"}
SPEED_FUNCTION = "Player_VariableAnimDurationGet"

# name, offset, width, signed - the disc layouts of engine/maps/map_table_data.cpp.
EMITTER = [("flags", 0, 1, 0), ("mode", 1, 1, 0), ("level_speed", 2, 1, 0), ("spark_spread", 3, 1, 0),
           ("fade_phase", 4, 1, 0), ("fade_amp", 5, 1, 0), ("spark_size", 6, 1, 0), ("wobble", 8, 2, 0),
           ("swirl", 10, 2, 1), ("count", 12, 2, 0), ("speed", 14, 2, 1), ("level", 16, 2, 1), ("top", 18, 2, 1),
           ("bottom", 20, 2, 1), ("height", 22, 2, 1), ("half_x", 24, 2, 1), ("half_z", 26, 2, 1),
           ("radius", 28, 2, 0), ("ember_x", 32, 4, 1), ("ember_z", 36, 4, 1)]
SMOKE = [("shape", 0, 1, 0), ("size", 1, 1, 0), ("keep", 2, 1, 0), ("puffs", 4, 2, 1), ("top", 6, 2, 1),
         ("base", 8, 2, 1), ("radius", 10, 2, 1), ("rise", 12, 2, 1), ("fade_rate", 14, 2, 1), ("fade", 16, 2, 1),
         ("depth_bias", 18, 2, 1), ("offset_x", 20, 4, 1), ("offset_z", 24, 4, 1), ("fade_delay", 28, 4, 1)]
GLASS = [("x", 0, 4, 1), ("z", 4, 4, 1), ("size", 8, 2, 1)]
DRIP = [("x", 0, 4, 1), ("z", 4, 4, 1), ("y", 8, 2, 1), ("angle", 10, 2, 1), ("spread", 12, 2, 1),
        ("size", 14, 2, 1), ("period", 16, 2, 1), ("count", 18, 2, 1), ("pattern", 20, 1, 0)]
WATER_SEGMENT = [("x", 0, 4, 1), ("y", 16, 2, 1), ("z", 24, 4, 1), ("length", 40, 2, 1), ("mode", 48, 1, 0),
                 ("angle", 52, 2, 1), ("width", 60, 2, 1), ("step", 68, 2, 1), ("flow", 76, 2, 1),
                 ("phase", 84, 2, 0), ("spread", 92, 2, 1), ("ramp", 100, 2, 1), ("jitter", 108, 2, 1)]
WATER = [("drops", 116, 2, 1), ("count", 120, 1, 0)]
SIZES = {"particle_emitter": 40, "smoke_column": 32, "glass_panes": 12, "drips": 24, "water_stream": 124}

# The arrays that become data: symbol -> data key (Harry's clip arrays by type).
EFFECTS = {"particle_emitter": "particle_emitter", "smoke_emitter": "smoke_column", "glass_shards": "glass_panes",
           "water_stream": "water_stream", "water_drips": "drips"}
FMT = {(1, 0): "<B", (1, 1): "<b", (2, 0): "<H", (2, 1): "<h", (4, 0): "<I", (4, 1): "<i"}


def data_key(sym):
    if sym["symbol"] == "LOADABLE_INVENTORY_ITEMS":
        return "items"
    if sym["type"] == "s_AnimInfo" and sym["symbol"].startswith("HARRY_"):
        return "clips"
    if sym["symbol"] == "g_MapHeaderTable_38":
        return "reaction_rows"
    return EFFECTS.get(sym["symbol"])


def room(sym, key):
    if "bytes" in sym:
        return sym["bytes"]
    return sym["count"] * {"clips": 16, "reaction_rows": 8}.get(key, 1)


def item_names():
    order = json.loads((ROOT / "gamedata/original_order.json").read_text())
    by_number = {}
    for g, (_, names) in enumerate(order["items"].items()):
        for i, n in enumerate(names):
            if n is not None:
                by_number[32 * g + i] = n
    return by_number


def reaction_names():
    clips = json.loads((ROOT / "gamedata/chara/Harry/reactions.json").read_text())["clips"]
    by_id = {}
    for name in sorted(clips):  # the alphabetically first name, as HarryReactions::name_of
        c = clips[name]
        by_id.setdefault(c["anim"] * 2 + (1 if c["active"] else 0), name)
    return by_id


def read(b, off, width, signed):
    return struct.unpack_from(FMT[(width, signed)], b, off)[0]


def put(out, off, width, signed, value):
    struct.pack_into(FMT[(width, signed)], out, off, value)


def decode_record(b, off, layout, index=0):
    return {name: read(b, off + o + index * w, w, s) for name, o, w, s in layout}


def encode_record(out, off, rec, layout, index=0):
    for name, o, w, s in layout:
        put(out, off + o + index * w, w, s, rec[name])


def functions_of(doc):
    return {int(f["va"], 16): f["symbol"] for f in doc.get("functions", [])}


def decode(doc, sym, key, b, items, reactions):
    """Readable records of the disc bytes `b` (the array's room); trailing zero elements are the terminator."""
    funcs = functions_of(doc)
    if key == "items":
        out = []
        for v in b:
            if v == 0:
                break
            out.append(items[v])
        return out
    if key == "clips":
        out = []
        for e in range(0, len(b) - 15, 16):
            fn, status, variable, link, speed, start, end = struct.unpack_from("<IBBBxihh", b, e)
            if fn == 0:
                break
            c = {"playback": PLAYBACK[funcs[fn]], "status": status, "link": link}
            if variable:
                if funcs.get(speed & 0xFFFFFFFF) != SPEED_FUNCTION:
                    raise SystemExit(f"{sym['symbol']}: variable clip with speed word {speed:#x}")
                c["speed_function"] = "variable_duration"
            elif (speed * 1000) % 4096 == 0:
                c["speed"] = speed / 4096 if speed % 4096 else speed // 4096
            else:
                c["speed_q12"] = speed
            c["start_keyframe"] = start
            c["end_keyframe"] = end
            out.append(c)
        return out
    if key == "reaction_rows":
        out = []
        for e in range(0, len(b) - 7, 8):
            status, nxt, rid, start, last = struct.unpack_from("<BBhhh", b, e)
            if status == 0 and nxt == 0 and rid == 0:
                break
            out.append({"reaction": reactions.get(rid, rid), "status": status, "next_status": nxt,
                        "start_keyframe": start, "last_keyframe": last})
        return out
    if key in ("particle_emitter", "smoke_column"):
        return decode_record(b, 0, EMITTER if key == "particle_emitter" else SMOKE)
    if key == "water_stream":
        rec = decode_record(b, 0, WATER)
        return {"segments": [decode_record(b, 0, WATER_SEGMENT, i) for i in range(4)], **rec}
    layout = GLASS if key == "glass_panes" else DRIP
    size = SIZES[key]
    out = []
    for e in range(0, len(b) - size + 1, size):
        if not any(b[e:e + size]):
            break
        out.append(decode_record(b, e, layout))
    return out


def encode(doc, sym, key, data, items, reactions):
    funcs = {v: k for k, v in functions_of(doc).items()}
    out = bytearray(room(sym, key))
    if key == "items":
        by_name = {v: k for k, v in items.items()}
        for i, n in enumerate(data):
            out[i] = by_name[n]
    elif key == "clips":
        names = {v: k for k, v in PLAYBACK.items()}
        for i, c in enumerate(data):
            if "speed_function" in c:
                variable, speed = 1, funcs[SPEED_FUNCTION] - (1 << 32)
            else:
                variable, speed = 0, c["speed_q12"] if "speed_q12" in c else int(c["speed"] * 4096)
            struct.pack_into("<IBBBxihh", out, i * 16, funcs[names[c["playback"]]], c["status"], variable,
                             c["link"], speed, c["start_keyframe"], c["end_keyframe"])
    elif key == "reaction_rows":
        by_name = {v: k for k, v in reactions.items()}
        for i, r in enumerate(data):
            rid = by_name[r["reaction"]] if isinstance(r["reaction"], str) else r["reaction"]
            struct.pack_into("<BBhhh", out, i * 8, r["status"], r["next_status"], rid, r["start_keyframe"],
                             r["last_keyframe"])
    elif key in ("particle_emitter", "smoke_column"):
        encode_record(out, 0, data, EMITTER if key == "particle_emitter" else SMOKE)
    elif key == "water_stream":
        encode_record(out, 0, data, WATER)
        for i, seg in enumerate(data["segments"]):
            encode_record(out, 0, seg, WATER_SEGMENT, i)
    else:
        for i, rec in enumerate(data):
            encode_record(out, i * SIZES[key], rec, GLASS if key == "glass_panes" else DRIP)
    return bytes(out)


def contained_overlap(doc, sym):
    """Ranges of other data arrays that lie inside this array's room (they keep their own bytes)."""
    lo = int(sym["va"], 16)
    hi = lo + room(sym, data_key(sym))
    out = []
    for other in doc["symbols"]:
        k = data_key(other)
        if other is sym or k is None:
            continue
        o = int(other["va"], 16)
        if lo <= o < hi and room(other, k) < room(sym, data_key(sym)):
            out.append((o - lo, o - lo + room(other, k)))
    return out


def masked(b, ranges):
    b = bytearray(b)
    for a, z in ranges:
        b[a:z] = bytes(z - a)
    return bytes(b)


def compact(value):
    return json.dumps(value, separators=(", ", ": "))


def dump(doc):
    """tables.json as the generator writes it, data records one per line."""
    lines = ["{"]
    keys = list(doc)
    for ki, k in enumerate(keys):
        comma = "," if ki + 1 < len(keys) else ""
        if k != "symbols":
            body = json.dumps(doc[k], indent=2).replace("\n", "\n  ")
            lines.append(f"  {json.dumps(k)}: {body}{comma}")
            continue
        lines.append('  "symbols": [')
        for si, sym in enumerate(doc["symbols"]):
            lines.append("    {")
            fields = list(sym)
            for fi, f in enumerate(fields):
                fc = "," if fi + 1 < len(fields) else ""
                v = sym[f]
                if f in ("clips", "reaction_rows", "glass_panes", "drips") and not v:
                    lines.append(f'      "{f}": []{fc}')
                elif f in ("clips", "reaction_rows", "glass_panes", "drips"):
                    rows = [f"        {compact(r)}" for r in v]
                    lines.append(f'      "{f}": [')
                    lines.append(",\n".join(rows))
                    lines.append(f"      ]{fc}")
                elif f == "water_stream":
                    lines.append(f'      "{f}": {{')
                    lines.append('        "segments": [')
                    lines.append(",\n".join(f"          {compact(s)}" for s in v["segments"]))
                    rest = [x for x in v if x != "segments"]
                    lines.append("        ]" + ("," if rest else ""))
                    for ri, x in enumerate(rest):
                        lines.append(f"        {json.dumps(x)}: {json.dumps(v[x])}" + ("," if ri + 1 < len(rest) else ""))
                    lines.append(f"      }}{fc}")
                elif f in ("particle_emitter", "smoke_column", "items"):
                    lines.append(f'      "{f}": {compact(v)}{fc}')
                else:
                    lines.append(f"      {json.dumps(f)}: {json.dumps(v)}{fc}")
            lines.append("    }" + ("," if si + 1 < len(doc["symbols"]) else ""))
        lines.append(f"  ]{comma}")
    lines.append("}")
    return "\n".join(lines) + "\n"


DATA_NOTE = (" Arrays with a data key (items, clips, reaction_rows, particle_emitter, smoke_column, water_stream, "
             "glass_panes, drips) are filled from that readable data instead of the disc; "
             "tools/scripts/map_tables_data.py wrote it from the disc and checks it (rerun it after regenerating with "
             "gen_map_tables.py, which does not keep the data).")


DATA_PREFIX = DATA_NOTE[:40]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gamedata", required=True, help="an imported gamedata (its raw/ holds the overlays)")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    items, reactions = item_names(), reaction_names()
    raw = pathlib.Path(args.gamedata) / "raw"
    bad = 0
    converted = 0
    for path in sorted((ROOT / "gamedata/maps").glob("*/tables.json")):
        doc = json.loads(path.read_text())
        disc = (raw / doc["bin"]).read_bytes()
        base = int(doc.get("base", hex(BASE)), 16)
        for sym in doc["symbols"]:
            key = data_key(sym)
            if key is None:
                continue
            off = int(sym["va"], 16) - base
            want = disc[off:off + room(sym, key)]
            if len(want) < room(sym, key):
                want = want + bytes(room(sym, key) - len(want))
            if not args.check:
                for k in list(sym):
                    if k in ("items", "clips", "reaction_rows") or k in SIZES:
                        del sym[k]
                sym[key] = decode(doc, sym, key, want, items, reactions)
            got = encode(doc, sym, key, sym.get(key), items, reactions) if key in sym else None
            overlap = contained_overlap(doc, sym)
            if got is None or masked(got, overlap) != masked(want, overlap):
                print(f"{path.parent.name}: {sym['symbol']}: the data does not encode to the disc's bytes")
                bad += 1
            converted += 1
        if not args.check and not bad:
            doc["description"] = doc["description"].split(DATA_PREFIX)[0] + DATA_NOTE
            path.write_text(dump(doc))
            if json.loads(path.read_text()) != doc:
                raise SystemExit(f"{path}: the written file does not read back")
    print(f"{converted} arrays, {bad} mismatches")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
