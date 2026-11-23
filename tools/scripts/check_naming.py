#!/usr/bin/env python3
"""Readable-name check (AGENTS.md: no address or field_XX names in data and the Lua API,
comments in English).

Looks for:
  json-key      keys in gamedata/**/*.json named field_XX / unk_XX / D_800XXXXX / func_800XXXXX /
                sharedFunc_* / sharedData_* (gamedata/text/ is localization data, skipped)
  handler-name  handlers.json entries whose `name` is missing or an address name, or whose
                `symbol` is an address name
  lua-ident     the same names as identifiers in gamedata Lua code (outside comments and strings)
  lua-api       registered Lua API names (sh.<name>, sh.<group>.<name>) with such names
  cyrillic      Cyrillic words in comments of src/, tools/, tests/, cmake/, gamedata Lua and
                CMakeLists.txt, outside string literals; quoted text inside a comment and runs of
                one or two letters (a glyph being described) are allowed
  numeric-id    a number where the Lua API takes a name (an event flag, a message key, a sound, a
                game file, a character, an item: NUMERIC_ARGS), local Flag/Sfx/Msg tables of
                numbers, "id"/"enum" in the sound registry, flag numbers in world_objects.json
                and sound numbers in the effect JSON (a "sound"/"sfx" field, an "id" of a
                "sounds" list) where sounds.json has a name
  unknown-id    a literal name in those arguments that the base data does not define (flags/*.json,
                sounds.json, items/, chara/, the map's message keys, the disc's file table)
  opaque-name   numbered placeholder names: gamedata JSON keys and Lua identifiers (code outside
                comments and strings: locals, parameters, functions, table fields) named unkN,
                varN, valN, fieldN, dataN, tempN/tmpN, argN, paramN (also as one part of a longer
                name: field28_get), after a MIPS register of the decompiled code (v0-v1, a0-a3,
                t0-t9, s0-s7), after the decompiler's temporaries (temp_s5, var_v0_2, sp10) or after a
                struct offset (f11c); JSON keys of one letter or one letter and a digit ("w", "x0").
                Lua names of one letter are not checked: the scripts use
                them by convention (c the character, i/j/k loop indices, x/y/z, A/S module aliases).
                OPAQUE_ALLOWED lists the legitimate exceptions with the reason for each.
  flag-placeholder  event flag names of the form Flag<N> (a flag nobody has named yet): as a
                declared name in gamedata/flags/*.json or in the event_flags layout of
                original_order.json, as a string in JSON and Lua, and Lua that builds a flag name from
                a number ("Flag" .. n). A Flag<N> in "aliases" / "old_save_names" is allowed: it is the
                former name old saves, scripts and mods still use

Prints a summary per category and file; --list prints every finding, --only CATEGORY[,...]
limits the check, --strict exits 1 when anything is found (for a future gate).

Usage: python3 -I tools/scripts/check_naming.py [--list] [--strict] [--only cyrillic,json-key]
"""

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAMEDATA = ROOT / "gamedata"

ADDRESS = r"(?:field_[0-9A-Fa-f]+\w*|unk_?[0-9A-Fa-f]+\w*|D_800[0-9A-Fa-f]{5}\w*|func_800[0-9A-Fa-f]{5}\w*|sharedFunc_\w+|sharedData_\w+)"
ADDRESS_FULL = re.compile(ADDRESS + r"\Z")
ADDRESS_IDENT = re.compile(r"(?<![\w.])" + ADDRESS + r"\b")
CYRILLIC_WORD = re.compile(r"[\u0400-\u04ff]{3,}")

COMMENT_DIRS = ["src", "tools", "tests", "cmake"]
C_LIKE = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".inc", ".cs", ".js", ".glsl", ".vert", ".frag"}
HASH_LIKE = {".py", ".sh", ".cmake", ".txt"}  # .txt: CMakeLists.txt only
SKIP_DIRS = {"third_party", "__pycache__", ".git"}


# ---------------------------------------------------------------- comment extraction

def c_comments(text: str):
    """(line, comment text) for // and /* */ comments; string, char and raw literals skipped."""
    out = []
    i, n, line = 0, len(text), 1
    while i < n:
        ch = text[i]
        if ch == "\n":
            line += 1
            i += 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append((line, text[i + 2:j]))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j
            body = text[i + 2:j]
            for k, part in enumerate(body.split("\n")):
                out.append((line + k, part))
            line += body.count("\n")
            i = j + 2
        elif ch == "R" and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            m = re.match(r'R"([^(\s]*)\(', text[i:])
            if not m:
                i += 1
                continue
            end = text.find(")" + m.group(1) + '"', i)
            end = n if end < 0 else end + len(m.group(1)) + 2
            line += text.count("\n", i, end)
            i = end
        elif ch in "\"'":
            j = i + 1
            while j < n and text[j] != ch and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            i = j + 1
        else:
            i += 1
    return out


def hash_comments(text: str, python: bool):
    """(line, comment) for # comments; quoted strings (incl. Python triple quotes) skipped."""
    out = []
    i, n, line = 0, len(text), 1
    while i < n:
        ch = text[i]
        if ch == "\n":
            line += 1
            i += 1
        elif ch == "#":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append((line, text[i + 1:j]))
            i = j
        elif python and text.startswith(('"""', "'''"), i):
            q = text[i:i + 3]
            end = text.find(q, i + 3)
            end = n if end < 0 else end + 3
            line += text.count("\n", i, end)
            i = end
        elif ch in "\"'":
            j = i + 1
            while j < n and text[j] != ch and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            i = j + 1
        else:
            i += 1
    return out


def lua_split(text: str):
    """(comments, code) of Lua source: [(line, comment)], [(line, code without strings)]."""
    comments, code = [], []
    i, n, line = 0, len(text), 1
    buf = []

    def flush_code():
        if buf:
            code.append((line, "".join(buf)))
            buf.clear()

    while i < n:
        ch = text[i]
        if ch == "\n":
            flush_code()
            line += 1
            i += 1
        elif text.startswith("--", i):
            m = re.match(r"--\[(=*)\[", text[i:])
            if m:
                close = "]" + m.group(1) + "]"
                end = text.find(close, i)
                end = n if end < 0 else end
                body = text[i + len(m.group(0)):end]
                for k, part in enumerate(body.split("\n")):
                    comments.append((line + k, part))
                line += body.count("\n")
                i = end + len(close)
            else:
                j = text.find("\n", i)
                j = n if j < 0 else j
                comments.append((line, text[i + 2:j]))
                i = j
        elif re.match(r"\[(=*)\[", text[i:]):
            m = re.match(r"\[(=*)\[", text[i:])
            close = "]" + m.group(1) + "]"
            end = text.find(close, i)
            end = n if end < 0 else end + len(close)
            line += text.count("\n", i, end)
            i = end
        elif ch in "\"'":
            j = i + 1
            while j < n and text[j] != ch and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            buf.append('""')
            i = j + 1
        else:
            buf.append(ch)
            i += 1
    flush_code()
    return comments, code


def strip_quoted(comment: str) -> str:
    """Quoted text inside a comment names data (in double or guillemet quotes): not prose to translate."""
    comment = re.sub(r'"[^"\n]*"', "", comment)
    comment = re.sub(r"«[^»\n]*»", "", comment)
    return re.sub(r"'[^'\n]*'", "", comment)


# ---------------------------------------------------------------- checks

def iter_files(base: Path):
    for path in sorted(base.rglob("*")):
        if path.is_file() and not (SKIP_DIRS & set(path.relative_to(ROOT).parts)):
            yield path


def check_cyrillic(findings):
    files = []
    for d in COMMENT_DIRS:
        files += list(iter_files(ROOT / d))
    files.append(ROOT / "CMakeLists.txt")
    files += sorted((GAMEDATA).rglob("*.lua"))
    for path in files:
        suffix = path.suffix.lower()
        if suffix == ".txt" and path.name != "CMakeLists.txt":
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        if not re.search(r"[\u0400-\u04ff]", text):
            continue
        if suffix in C_LIKE:
            comments = c_comments(text)
        elif suffix in HASH_LIKE:
            comments = hash_comments(text, python=suffix == ".py")
        elif suffix == ".lua":
            comments = lua_split(text)[0]
        else:
            continue
        for line, comment in comments:
            if CYRILLIC_WORD.search(strip_quoted(comment)):
                findings["cyrillic"].append((path, line, comment.strip()[:100]))


def walk_keys(value, path, out):
    if isinstance(value, dict):
        for key, child in value.items():
            if ADDRESS_FULL.match(key):
                out.append(f"{path}.{key}" if path else key)
            walk_keys(child, f"{path}.{key}" if path else key, out)
    elif isinstance(value, list):
        for i, child in enumerate(value):
            walk_keys(child, f"{path}[{i}]", out)


def check_json(findings):
    for path in sorted(GAMEDATA.rglob("*.json")):
        rel = path.relative_to(GAMEDATA)
        if rel.parts[0] == "text":
            continue
        try:
            doc = json.loads(path.read_text(encoding="utf-8"))
        except (ValueError, OSError) as e:
            findings["json-key"].append((path, 0, f"cannot parse: {e}"))
            continue
        keys = []
        walk_keys(doc, "", keys)
        for key in keys:
            findings["json-key"].append((path, 0, key))
        if path.name == "handlers.json":
            for h in doc.get("handlers", []):
                if not h.get("symbol"):
                    continue
                name = h.get("name")
                if not name or ADDRESS_FULL.match(name) or ADDRESS_FULL.match(h["symbol"]):
                    findings["handler-name"].append((path, 0, f"index {h.get('index')}: {h['symbol']}"))


def check_lua(findings):
    for path in sorted(GAMEDATA.rglob("*.lua")):
        _, code = lua_split(path.read_text(encoding="utf-8"))
        for line, text in code:
            for m in ADDRESS_IDENT.finditer(text):
                findings["lua-ident"].append((path, line, m.group(0)))


def registered_api():
    """[(file, name)] of the names the script host puts into the `sh` table."""
    names = []
    host = ROOT / "src/engine/script/script_host.cpp"
    text = host.read_text(encoding="utf-8")
    m = re.search(r"kShFuncs\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if m:
        names += [(host, n) for n in re.findall(r'\{\s*"(\w+)"\s*,', m.group(1))]
    m = re.search(r"kEvbFuncs\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if m:
        names += [(host, n) for n in re.findall(r'"(\w+)"', m.group(1))]
    for path in sorted((ROOT / "src").rglob("pc_script_api*.cpp")):
        src = path.read_text(encoding="utf-8")
        for n in re.findall(r'\{\s*(?:"\*"\s*,\s*)?"([a-z_]+\.[A-Za-z0-9_]+)"', src):
            names.append((path, n))
        for lst in re.findall(r"for \(const char\* \w+ : \{(.*?)\}\)", src, re.S):
            names += [(path, n) for n in re.findall(r'"([a-z_]+\.[A-Za-z0-9_]+)"', lst)]
    return sorted(set(names))


def check_api(findings):
    for path, name in registered_api():
        leaf = name.split(".")[-1]
        if ADDRESS_FULL.match(leaf) or ADDRESS_IDENT.search(leaf):
            findings["lua-api"].append((path, 0, "sh." + name))


# ---------------------------------------------------------------- numeric identifiers

# 0-based arguments of the Lua API that take an event flag, a message, a sound or an item: a
# name there ("M1S03_ShotgunShells0", "map1_s03.nothing_special", "door_locked", "HealthDrink");
# a number is the old form.
NUMERIC_ARGS = {
    "flag": {
        "sh.flag_get": [0], "sh.flag_set": [0], "sh.flag_set_alt": [0], "sh.event.flag": [0],
        "sh.event.flag_set": [0], "sh.event.flag_clear": [0], "sh.event.branch_on_flag": [0],
        "sh.event_sys_state_branch_on_flag": [0], "sh.event_item_take": [2], "sh.event_common_item_take": [1],
        "sh.event_paper_map_take": [1], "sh.map_event_set": [3], "load_map": [2], "fmv": [1],
        "sh.map_screen_marking_draw": [2],
    },
    "message": {
        "sh.msg_draw": [0], "say": [0], "sh.event_msg": [1], "sh.event_selection": [1],
        "sh.event_msg_with_sfx": [0], "sh.event_msg_with_bg": [3], "sh.event_item_take": [3],
        "sh.event_paper_map_take": [2], "sh.event_msg_with_texture": [3, 4], "sh.event_msg_with_audio": [0],
        "sh.event_msg_audio": [0], "sh.map_msg_audio": [0], "sh.event_msg_dimmed_bg": [3, 4], "sh.msg.show": [1],
        "sh.msg.show_with_audio": [0], "sh.msg.show_dimmed": [3, 4], "sh.msg.show_with_sfx": [0],
        "sh.msg.play_with_audio": [0], "sh.msg.selection": [1], "sh.msg.draw": [0],
    },
    "sound": dict({n: [0] for n in (
        "sh.sfx", "sh.sfx_stop", "sh.sfx_3d", "sh.sfx_3d_at", "sh.sfx_flags_play", "sh.sfx_flags_pitch_play",
        "sh.sfx_pitch_play", "sh.sfx_play_at_pos", "sh.sfx_falloff_pitch_play", "sh.sfx_attributes_update",
        "sh.sd_play_sfx", "sh.sd_call", "sh.sound.sd_call", "sh.sound.stop", "sh.sound.attributes",
        "sh.sound.play", "sh.sound.play_with_flags", "sh.sound.play_falloff", "sh.sound.play_3d",
        "sh.xa_preload", "sh.sound.xa_preload")},
        **{"sh.event_msg_with_sfx": [1], "sh.msg.show_with_sfx": [1], "sh.chara_sfx": [2]}),
    "file": {  # a game file by its disc path, "TIM/LOCKER1.TIM"
        "sh.event_bg_texture_cmd": [1], "sh.event_msg_with_bg": [0], "sh.event_msg_with_texture": [0],
        "sh.event_msg_dimmed_bg": [0], "sh.overlay_screen_set": [1], "sh.overlay_screen_show": [0],
        "sh.fs_read": [0], "sh.fs_seek": [0], "sh.fs_read_dms": [0], "sh.event_bg_texture_fade_in": [0],
        "sh.event.bg_texture": [1], "sh.event.bg_texture_load_fade_in": [0], "sh.load.file": [0],
        "sh.load.tim": [0],
    },
    "chara": {
        "sh.chara_spawn": [0], "sh.chara_spawn_flags_set": [0], "sh.chara_load": [1], "sh.npc.spawn": [0],
        "sh.npc.load": [1], "sh.npc.set_chara_id": [1], "sh.chara_sfx": [0], "sh.chara_param": [0],
        "sh.chara_anim": [0],
    },
    "item": {
        "sh.give_item": [0], "sh.item_count": [0], "sh.has_item": [0], "sh.take_item": [0], "sh.item_remove": [0],
        "sh.event_item_take": [0], "sh.event_common_item_take": [0], "sh.event_inv_item_cmd": [1],
        "sh.event.inv_item": [1], "sh.player.item_remove": [0], "sh.pickup_animate": [0], "sh.screen.pickup_animate": [0],
    },
    "paper_map": {  # a page of the navigation map screen ("FogSchool1F", gamedata/paper_maps.json)
        "sh.event_paper_map_take": [0], "sh.paper_map_state": [1], "sh.paper_map_cmd": [1],
    },
    "attack": {  # a row of gamedata/config/attacks.json by its "name"
        "sh.entity.attack_number": [0],
    },
    "state": {  # a cutscene state of the slot's character: Harry "pickup_crouch" (character.json
        # "states"), an NPC's control state ("controls"); the slot argument comes first
        "sh.event_chara_anim": [2], "sh.chara_anim_play_to_end": [1], "sh.event_waypoint_player": [0],
        "sh.npc.anim_cmd": [2], "sh.npc.anim_play_to_end": [1],
    },
}
# A sound command of SD_Call is a sound id only inside the effect range; the rest (19 stop all,
# 0x1000+ music and voice commands) are commands, not identifiers.
SOUND_IDS = range(1281, 1792)
XA_IDS = range(0x1000, 0x1700)  # Sfx_XaAudio*: streamed voice lines, sd_call/xa_preload play them too
LUA_CALL = re.compile(r"(?<![\w.:])(sh\.[a-z_0-9.]+|say|load_map|fmv)\s*\(")
INT_LITERAL = re.compile(r"\s*(0x[0-9A-Fa-f]+|\d+)\s*\Z")
NUMERIC_TABLE = re.compile(r"^local (Flag\d*|Sfx|Msg) = \{", re.M)


def lua_masked(text: str) -> str:
    """The source with comments blanked and string contents replaced (offsets kept)."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        if text.startswith("--", i):
            m = re.match(r"--\[(=*)\[", text[i:])
            j = text.find("]" + m.group(1) + "]", i) + len(m.group(1)) + 2 if m else text.find("\n", i)
            j = n if j < (i if m else 0) or j < 0 else j
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        elif text[i] in "\"'":
            q, j = text[i], i + 1
            while j < n and text[j] != q and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            for k in range(i + 1, min(j, n)):
                out[k] = "_"
            i = j + 1
        else:
            i += 1
    return "".join(out)


def lua_call_args(text: str, masked: str):
    """(name, line, [argument text]) of every call of LUA_CALL."""
    for m in LUA_CALL.finditer(masked):
        depth, start, args = 0, m.end(), []
        j = m.end() - 1
        while j < len(masked):
            ch = masked[j]
            if ch in "([{":
                depth += 1
            elif ch in ")]}":
                depth -= 1
                if depth == 0:
                    if masked[start:j].strip():
                        args.append(text[start:j])
                    break
            elif ch == "," and depth == 1:
                args.append(text[start:j])
                start = j + 1
            j += 1
        yield m.group(1), text.count("\n", 0, m.start()) + 1, args


def check_numeric(findings):
    for path in sorted(GAMEDATA.rglob("*.lua")):
        text = path.read_text(encoding="utf-8")
        masked = lua_masked(text)
        for m in NUMERIC_TABLE.finditer(masked):
            findings["numeric-id"].append((path, text.count("\n", 0, m.start()) + 1, f"table of numbers: local {m.group(1)}"))
        for name, line, args in lua_call_args(text, masked):
            for kind, spec in NUMERIC_ARGS.items():
                for pos in spec.get(name, []):
                    lit = INT_LITERAL.match(args[pos]) if pos < len(args) else None
                    if not lit:
                        continue
                    value = int(lit.group(1), 0)
                    if value <= 0 or (kind == "sound" and value not in SOUND_IDS and value not in XA_IDS):
                        continue  # 0 = none; not a sound id: a sound command
                    findings["numeric-id"].append((path, line, f"{name}(...) {kind} {value}"))
    sounds = GAMEDATA / "sounds" / "sounds.json"
    if sounds.is_file():
        for name, info in json.loads(sounds.read_text(encoding="utf-8")).get("sounds", {}).items():
            if isinstance(info, dict) and ("id" in info or "enum" in info):
                findings["numeric-id"].append((sounds, 0, f"sound {name} carries \"id\"/\"enum\""))
    for path in sorted((GAMEDATA / "maps").glob("*/world_objects.json")):
        for obj in json.loads(path.read_text(encoding="utf-8")).get("objects", []):
            flag = (obj.get("condition") or {}).get("event_flag_clear")
            if isinstance(flag, int):
                findings["numeric-id"].append((path, 0, f"{obj.get('model')}: event_flag_clear {flag}"))
    for path, doc in gamedata_json():
        for where, value in json_sound_numbers(doc, ""):
            findings["numeric-id"].append((path, 0, f"{where}: sound {value}"))


def json_sound_numbers(value, path, in_sounds=False):
    """(key path, number) of sound effect numbers in a JSON document: a "sound"/"sfx" field, or an
    "id" of an entry of a "sounds" list (sounds.json names them)."""
    if isinstance(value, dict):
        for key, child in value.items():
            where = f"{path}.{key}" if path else key
            number = isinstance(child, int) and not isinstance(child, bool)
            if number and (key in ("sound", "sfx") or (in_sounds and key == "id")) and \
                    (child in SOUND_IDS or child in XA_IDS):
                yield where, child
            yield from json_sound_numbers(child, where, key == "sounds")
    elif isinstance(value, list):
        for i, child in enumerate(value):
            yield from json_sound_numbers(child, f"{path}[{i}]", in_sounds)


def gamedata_json():
    """(path, document) of every gamedata JSON file except the localization data."""
    for path in sorted(GAMEDATA.rglob("*.json")):
        if path.relative_to(GAMEDATA).parts[0] == "text":
            continue
        try:
            yield path, json.loads(path.read_text(encoding="utf-8"))
        except (ValueError, OSError):
            continue  # json-key reports files that do not parse


# ---------------------------------------------------------------- opaque names

# Numbered placeholders of the decompiled code (unk_4, var2, arg0, temp1) and its MIPS register
# temporaries (v0, a1, t3, s2): the name says nothing about the value. A placeholder is a whole
# name or one "_"-separated part of it (field14C_0, trigger_param0, field28_get); the number may
# be hexadecimal (an offset: field14C).
OPAQUE_PART = re.compile(r"(?:^|_)(unk|var|val|field|data|temp|tmp|arg|param)_?\d[0-9A-Fa-f]*(?=_|$)")
OPAQUE_REGISTER = r"(?:v[01]|a[0-3]|t\d|s[0-7])"
OPAQUE_OFFSET = r"[fo]\d[0-9A-Fa-f]+"  # a struct offset as the name: f11c, o30, f28
# The decompiler's own temporaries: a register (temp_s5, var_v0_20) or a stack slot (sp10).
OPAQUE_M2C = r"(?:temp|var)_(?:[avst]\d|fp|ra)(?:_\d+)?|sp\d[0-9A-Fa-f]*|new_var\d*"
LUA_NAME = re.compile(r"(?<![\w])[A-Za-z_]\w*")
FLAG_PLACEHOLDER = re.compile(r"Flag\d+\Z")

# Legitimate names the opaque-name check would report: (kind "json"/"lua", name regex, path glob
# relative to gamedata/, reason). Every entry carries its reason; add one only with a real one.
OPAQUE_ALLOWED = [
    ("json", r"[xyz]", "**", "coordinate components of a position or a vector (metres or Q12)"),
    ("json", r"[uv]", "**", "texture coordinates in VRAM texels, the PS1 GPU's own names (u across, v down)"),
    ("json", r"[rgb]", "**", "colour components"),
]


def opaque_allowed(kind: str, name: str, path: Path) -> bool:
    rel = path.relative_to(GAMEDATA)
    return any(k == kind and re.fullmatch(pattern, name) and (glob == "**" or rel.match(glob))
               for k, pattern, glob, _ in OPAQUE_ALLOWED)


def opaque_kind(name: str, json_key: bool):
    """The placeholder pattern a name falls under ("param", "register", "letter"), None if none."""
    m = OPAQUE_PART.search(name)
    if m:
        return m.group(1)
    if re.fullmatch(OPAQUE_OFFSET, name):
        return "offset"
    if re.fullmatch(OPAQUE_M2C, name):
        return "decompiler"
    if not json_key and re.fullmatch(OPAQUE_REGISTER, name):
        return "register"
    if json_key and re.fullmatch(r"[A-Za-z]\d?", name):
        return "letter"
    return None


def check_opaque(findings):
    for path, doc in gamedata_json():
        if path == GAMEDATA / "flags.json" or path.parent == GAMEDATA / "flags":
            # Declarations: a placeholder may only be a former name ("aliases", "old_save_names"), so old saves,
            # scripts and mods still resolve it; as a flag's own name it is a finding.
            for name in (doc.get("flags") or {}):
                if FLAG_PLACEHOLDER.match(name):
                    findings["flag-placeholder"].append((path, 0, f"defined: {name}"))
            continue
        if path == GAMEDATA / "original_order.json":
            for i, name in enumerate(doc.get("event_flags") or []):
                if isinstance(name, str) and FLAG_PLACEHOLDER.match(name):
                    findings["flag-placeholder"].append((path, 0, f"event_flags[{i}]: {name}"))
        for where, key in all_keys(doc, ""):
            kind = opaque_kind(key, json_key=True)
            if kind and not opaque_allowed("json", key, path):
                findings["opaque-name"].append((path, 0, f"[{kind}] key {where}"))
        for where, text in json_strings(doc, ""):
            if FLAG_PLACEHOLDER.match(text):
                findings["flag-placeholder"].append((path, 0, f"{where}: \"{text}\""))
    for path in sorted(GAMEDATA.rglob("*.lua")):
        text = path.read_text(encoding="utf-8")
        _, code = lua_split(text)
        for line, chunk in code:
            for m in LUA_NAME.finditer(chunk):
                kind = opaque_kind(m.group(0), json_key=False)
                if kind and not opaque_allowed("lua", m.group(0), path):
                    findings["opaque-name"].append((path, line, f"[{kind}] {m.group(0)}"))
        masked = lua_masked(text)  # a quote kept at the match: a string of code, not of a comment
        for m in re.finditer(r"([\"'])(Flag\d+)\1", text):
            if masked[m.start()] == m.group(1):
                line = text.count("\n", 0, m.start()) + 1
                findings["flag-placeholder"].append((path, line, m.group(2)))
        # A flag name built from a save bit ("Flag" .. (500 + i)): the bit order is no identity of a flag.
        for m in re.finditer(r"([\"'])Flag\1\s*\.\.", text):
            if masked[m.start()] == m.group(1):
                line = text.count("\n", 0, m.start()) + 1
                findings["flag-placeholder"].append((path, line, "a flag name built from a number"))
    for path, name in registered_api():  # the names the scripts call (sh.field28_get was one)
        for part in name.split("."):
            kind = opaque_kind(part, json_key=False)
            if kind:
                findings["opaque-name"].append((path, 0, f"[{kind}] Lua API sh.{name}"))


def all_keys(value, path):
    """(key path, key) of every object key of a JSON document."""
    if isinstance(value, dict):
        for key, child in value.items():
            where = f"{path}.{key}" if path else key
            yield where, key
            yield from all_keys(child, where)
    elif isinstance(value, list):
        for i, child in enumerate(value):
            yield from all_keys(child, f"{path}[{i}]")


def json_strings(value, path):
    if isinstance(value, dict):
        for key, child in value.items():
            yield from json_strings(child, f"{path}.{key}" if path else key)
    elif isinstance(value, list):
        for i, child in enumerate(value):
            yield from json_strings(child, f"{path}[{i}]")
    elif isinstance(value, str):
        yield path, value


def known_names():
    """{kind: set of names} the base data defines (short names; a mod's are namespaced)."""
    flags = set()
    for path in sorted((GAMEDATA / "flags").glob("*.json")):
        for name, info in json.loads(path.read_text(encoding="utf-8")).get("flags", {}).items():
            flags.add(name)
            flags.update((info or {}).get("aliases", []))
    sounds = set()
    path = GAMEDATA / "sounds" / "sounds.json"
    if path.is_file():
        for name, info in json.loads(path.read_text(encoding="utf-8")).get("sounds", {}).items():
            sounds.add(name)
            sounds.update((info or {}).get("aliases", []) if isinstance(info, dict) else [])
    paper_maps = set()
    path = GAMEDATA / "paper_maps.json"
    if path.is_file():
        paper_maps = set(json.loads(path.read_text(encoding="utf-8")).get("paper_maps", {}))
    states = set()  # every "states"/"controls" name of every character (the char of a call's
    for path in sorted(GAMEDATA.glob("chara/*/character.json")):  # slot is not static)
        data = json.loads(path.read_text(encoding="utf-8"))
        states.update(data.get("states", {}))
        states.update(data.get("controls", {}))
    items = {p.stem for p in (GAMEDATA / "items").glob("*.json")}
    charas = {p.name for p in (GAMEDATA / "chara").iterdir() if p.is_dir()}
    files = None  # the disc's file list (the game's file table); unknown without the sources
    listing = ROOT / "tools/data/disc_files.USA.txt"
    if listing.is_file():
        files = set(re.findall(r"^\d+ (\S+)$", listing.read_text(encoding="utf-8"), re.M))
    # SD bus command names (sd_call accepts them next to sound names): read from the engine's
    # table so the check cannot drift from it (sd_command_number, script_steppers.cpp).
    sd_api = ROOT / "src/engine/scripting/script_steppers.cpp"
    if sd_api.is_file():
        block = re.search(r"kCommands\[\] = \{(.*?)\};", sd_api.read_text(encoding="utf-8"), re.S)
        if block:
            sounds.update(re.findall(r'\{"(\w+)", \d+\}', block.group(1)))
    attacks = set()
    path = GAMEDATA / "config" / "attacks.json"
    if path.is_file():
        attacks = {r["name"] for r in json.loads(path.read_text(encoding="utf-8")).get("rows", []) if "name" in r}
    return {"attack": attacks, "flag": flags, "sound": sounds, "item": items, "chara": charas, "file": files, "paper_map": paper_maps, "state": states}


def message_keys(map_name):
    keys = set()
    for path in (GAMEDATA / "text/en/common.json", GAMEDATA / f"text/en/maps/{map_name}.json"):
        if path.is_file():
            for idx in json.loads(path.read_text(encoding="utf-8")).get("index", {}).values():
                keys.update(k for k in idx.get("keys", []) if k)
    return keys


def check_unknown(findings):
    names = known_names()
    for path in sorted(GAMEDATA.rglob("*.lua")):
        text = path.read_text(encoding="utf-8")
        rel = path.relative_to(GAMEDATA).parts
        msgs = message_keys(rel[1]) if rel[0] == "maps" else None
        for name, line, args in lua_call_args(text, lua_masked(text)):
            for kind, spec in NUMERIC_ARGS.items():
                for pos in spec.get(name, []):
                    m = re.fullmatch(r'\s*"([^"]*)"\s*', args[pos]) if pos < len(args) else None
                    if not m or ":" in m.group(1) and not m.group(1).startswith("silenthill:"):
                        continue  # not a literal name, or a mod's
                    value = m.group(1).removeprefix("silenthill:")
                    known = msgs if kind == "message" else names[kind]
                    if known is not None and kind == "message" and value.split(".")[0] not in (rel[1], "common"):
                        continue  # another map's or a mod's key: not checkable here
                    if known is not None and value not in known:
                        findings["unknown-id"].append((path, line, f"{name}(...) {kind} \"{value}\""))


CHECKS = {
    "json-key": check_json,
    "handler-name": check_json,
    "lua-ident": check_lua,
    "lua-api": check_api,
    "cyrillic": check_cyrillic,
    "numeric-id": check_numeric,
    "unknown-id": check_unknown,
    "opaque-name": check_opaque,
    "flag-placeholder": check_opaque,
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--list", action="store_true", help="print every finding")
    ap.add_argument("--strict", action="store_true", help="exit 1 when anything is found")
    ap.add_argument("--only", default="", help="comma-separated categories: " + ", ".join(CHECKS))
    args = ap.parse_args()

    wanted = [c for c in args.only.split(",") if c] or list(CHECKS)
    unknown = [c for c in wanted if c not in CHECKS]
    if unknown:
        ap.error("unknown category: " + ", ".join(unknown))

    findings = defaultdict(list)
    for fn in dict.fromkeys(CHECKS[c] for c in wanted):
        fn(findings)

    total = 0
    for category in wanted:
        items = findings.get(category, [])
        total += len(items)
        per_file = Counter(str(p.relative_to(ROOT)) for p, _, _ in items)
        print(f"{category}: {len(items)} in {len(per_file)} files")
        if category == "opaque-name" and items:
            per_pattern = Counter(re.match(r"\[(\w+)\]", what).group(1) for _, _, what in items)
            print("  by pattern: " + ", ".join(f"{k} {v}" for k, v in sorted(per_pattern.items())))
        for name, count in per_file.most_common(None if args.list else 8):
            print(f"  {count:5d}  {name}")
        if args.list:
            for p, line, what in items:
                where = f"{p.relative_to(ROOT)}:{line}" if line else str(p.relative_to(ROOT))
                print(f"    {where}: {what}")
    print(f"total: {total}")
    return 1 if args.strict and total else 0


if __name__ == "__main__":
    sys.exit(main())
