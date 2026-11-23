#!/usr/bin/env python3
"""The game's text layer gamedata/text/<lang>/: checks the files.

Every committed gamedata/text/<lang>/ file is a source, edited in the repository (en is the base). The English
of the game itself is in en/common.json, en/items.json and en/maps/*.json: the original's text, with the index
tables that tie the message numbers to keys.

Strings are written in the readable markup of tools/scripts/sh_text.py with a per-string "ws"
record, so the engine bytes are reproduced exactly.

  gen_text.py --check   every file is sh-text v1, every string round-trips through the engine
                        bytes, the other languages name only keys en has, every ui.* key the
                        code names exists in an en file, and every language directory has a
                        descriptor (lang.json: name, direction, fonts)
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sh_text  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL_ORDER = ROOT / "gamedata" / "original_order.json"
OUT = ROOT / "gamedata" / "text"
UI_JSON = OUT / "en" / "ui.json"
COMMON_COUNT = 15  # e_MapMsgIdx entries: the block every map's messages open with
ITEM_FIRST = 32    # e_InvItemId of ITEM_NAME.0 / ITEM_DESC.0
ITEM_SLOTS = 195

COMMON_KEYS = [
    "yes", "no", "notes_useful", "no_map", "too_dark_for_map",
    "take_first_aid_kit", "take_health_drink", "take_ampoule", "take_handgun_bullets",
    "take_rifle_shells", "take_shotgun_shells", "lock_jammed", "locked", "unlocked",
    "now_making",
]

_ESC = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", '"': '"', "'": "'", "\\": "\\", "?": "?",
        "v": "\v", "b": "\b"}


# --- C literals --------------------------------------------------------------------

def c_literals(body: str) -> list[str]:
    """The comma-separated string literals of an initializer body, C-exact:
    adjacent literals concatenate, escapes decode to the bytes they compile to."""
    out: list[str] = []
    cur: list[str] | None = None
    i, n = 0, len(body)
    while i < n:
        c = body[i]
        if c == "#":
            line_end = body.find("\n", i)
            raise SystemExit("unexpected directive: " + body[i:n if line_end < 0 else line_end])
        if c == '"':
            j = i + 1
            buf = []
            while body[j] != '"':
                if body[j] == "\\":
                    e = body[j + 1]
                    if e == "x":
                        k = j + 2
                        while k < n and body[k] in "0123456789abcdefABCDEF":
                            k += 1
                        buf.append(chr(int(body[j + 2:k], 16)))
                        j = k
                        continue
                    if e not in _ESC:
                        raise SystemExit("unhandled escape \\" + e)
                    buf.append(_ESC[e])
                    j += 2
                else:
                    buf.append(body[j])
                    j += 1
            cur = (cur or []) + buf
            i = j + 1
            continue
        if c == ",":
            if cur is not None:
                out.append("".join(cur))
            cur = None
        i += 1
    if cur is not None:
        out.append("".join(cur))
    return out


def dump(doc: dict) -> str:
    return json.dumps(doc, ensure_ascii=False, indent=2) + "\n"


def committed() -> dict[Path, dict]:
    files: dict[Path, dict] = {}
    for path in sorted(OUT.rglob("*.json")):
        files[path] = json.loads(path.read_text(encoding="utf-8"))
    return files


DESCRIPTOR_KEYS = {"format", "version", "code", "name", "english_name", "direction", "fonts", "glyph_cell_height", "menu_order", "description"}


def check_descriptor(rel: Path, code: str, doc: dict) -> int:
    """A language's lang.json (sh-text-lang v1): the directory is the code, the name is in the language itself."""
    problems = []
    if doc.get("format") != "sh-text-lang" or doc.get("version") != 1:
        problems.append("expected sh-text-lang v1")
    if doc.get("code", code) != code:
        problems.append(f"\"code\" {doc.get('code')!r} is not the directory name")
    if not isinstance(doc.get("name"), str) or not doc.get("name"):
        problems.append("\"name\" (the language's name in the language itself) is missing")
    if doc.get("direction", "ltr") not in ("ltr", "rtl"):
        problems.append("\"direction\" is ltr or rtl")
    fonts = doc.get("fonts", [])
    if not isinstance(fonts, list) or any(not isinstance(f, str) or not f for f in fonts):
        problems.append("\"fonts\" is a list of font family names")
    cell = doc.get("glyph_cell_height", 16)
    if not isinstance(cell, int) or isinstance(cell, bool) or not 16 <= cell <= 32:
        problems.append("\"glyph_cell_height\" is a whole number from 16 to 32")
    order = doc.get("menu_order", 0)
    if not isinstance(order, int) or isinstance(order, bool) or not -1000 <= order <= 1000:
        problems.append("\"menu_order\" is a whole number from -1000 to 1000")
    for key in sorted(set(doc) - DESCRIPTOR_KEYS):
        problems.append(f"\"{key}\" is not a key of lang.json")
    for p in problems:
        print(f"{rel}: {p}")
    return len(problems)


def check() -> int:
    """Shape and round trip of every committed file, the converted packs, and the code's keys."""
    bad = 0
    files = committed()
    en_keys = {k for p, d in files.items() if p.parent.name == "en" or p.parent.parent.name == "en"
               for k in d.get("strings", {})}
    n = 0
    for path, doc in files.items():
        rel = path.relative_to(ROOT)
        if path.name == "lang.json":
            bad += check_descriptor(rel, path.parent.name, doc)
            continue
        lang = path.parent.parent.name if path.parent.name == "maps" else path.parent.name
        if doc.get("format") != "sh-text" or doc.get("version") != 1 or doc.get("lang") != lang:
            print(f"{rel}: expected sh-text v1 of lang {lang!r}")
            bad += 1
        strings, ws = doc.get("strings"), doc.get("ws", {})
        if not isinstance(strings, dict) or not isinstance(ws, dict):
            print(f"{rel}: \"strings\" (and \"ws\") must be objects")
            bad += 1
            continue
        for key in sorted(set(ws) - set(strings)):
            print(f"{rel} {key}: a ws record without a string")
            bad += 1
        for key in doc.get("index", {}).get("map", {}).get("keys", []):
            if key not in strings:
                print(f"{rel}: the index names {key}, which the file does not have")
                bad += 1
        for key, text in strings.items():
            n += 1
            dialect = sh_text.dialect_for_key(key)
            try:
                engine = sh_text.encode(text, ws.get(key, ""), dialect)
            except (ValueError, KeyError, SystemExit) as e:
                print(f"{rel} {key}: does not encode: {e}")
                bad += 1
                continue
            back, rec = sh_text.decode(engine, dialect)
            if sh_text.encode(back, rec, dialect) != engine:
                print(f"{rel} {key}: the engine bytes do not round-trip")
                bad += 1
            if lang != "en" and key not in en_keys:
                print(f"{rel} {key}: en has no such key")
                bad += 1
    for lang_dir in sorted(p for p in OUT.iterdir() if p.is_dir()):
        if not (lang_dir / "lang.json").is_file():
            print(f"{lang_dir.relative_to(ROOT)}: no lang.json, so the game does not list the language")
            bad += 1
    bad += check_code(files)
    print(f"gen_text --check: {len(files)} files, {n} strings, {bad} problems")
    return 1 if bad else 0


def _code_literals() -> tuple[set[str], set[str]]:
    """Every C string literal of src/ (decoded), and the ui.* keys among them."""
    lits: set[str] = set()
    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix not in (".cpp", ".h", ".inc") or not path.is_file():
            continue
        text = path.read_text(errors="ignore")
        for m in re.finditer(r'"(?:[^"\\\n]|\\.)*"(?:\s*"(?:[^"\\\n]|\\.)*")*', text):
            try:
                lits.update(c_literals(m.group(0)))
            except SystemExit:
                pass
    return lits, {s for s in lits if re.fullmatch(r"ui\.[a-z0-9_]+(\.[a-z0-9_]+)+", s)}


def check_code(files: dict[Path, dict]) -> int:
    """Every ui.* key the code names exists in the en files (the menus themselves are data now)."""
    _, ui_keys = _code_literals()
    en = {k for p, d in files.items() if p.parent.name == "en" for k in d.get("strings", {})}
    bad = 0
    for key in sorted(ui_keys - en):
        print(f"src names {key}, which no gamedata/text/en file has")
        bad += 1
    for key in files[UI_JSON]["strings"]:
        if not key.startswith("ui."):
            print(f"en/ui.json {key}: ui.json holds ui.* keys only")
            bad += 1
    return bad


def main() -> int:
    if sys.argv[1:] != ["--check"]:
        print(__doc__)
        return 2
    return check()


if __name__ == "__main__":
    sys.exit(main())
