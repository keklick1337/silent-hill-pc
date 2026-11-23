#!/usr/bin/env python3
"""Translator tool for gamedata/text/<lang>/.

The game's text is keyed JSON (sh-text v1). Translators work in gettext PO files instead:
one entry per key, msgctxt = the key, msgid = the English, msgstr = the translation. Any PO
editor (Poedit, Lokalize, Weblate, a text editor) handles them.

  text_po.py export <lang> [-o FILE.po]      English + the language's current text (template:
                                             lang "pot" writes an empty-msgstr template)
  text_po.py import FILE.po <lang> [--out DIR] [--name N] [--english-name E] [--direction ltr|rtl]
                                   [--fonts "Family A, Family B"]
                                             write DIR/<lang>/ (default gamedata/text) from it; the
                                             options fill the language's descriptor, lang.json
  text_po.py stats [lang ...]                translated keys per language and area, and
                                             markup that differs from the English
  text_po.py check <lang>                    stats for one language; exit 1 on markup errors

The English of the game itself (messages, item names) is gamedata/text/en of the repository, like the
interface text.

Markup in msgid/msgstr is the readable form of gamedata/text ({wait 2.0}, {color 2}..{/color},
{end}, {choice 4}, ...; ui.* keys have none, their {n}/{key} are placeholders to keep).
Import checks every msgstr encodes; a bad one is reported with its key and not written.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sh_text  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
TEXT = ROOT / "gamedata" / "text"
AREAS = ("common", "maps", "items", "menu", "ui")


# --- gamedata/text -----------------------------------------------------------------

def area_of(rel: Path) -> str:
    return rel.parts[0] if rel.parts[0] == "maps" else rel.stem


def load_lang(root: Path, lang: str) -> dict[str, tuple[Path, str]]:
    """key -> (path relative to the language dir, readable text)."""
    out: dict[str, tuple[Path, str]] = {}
    base = root / lang
    for path in sorted(base.rglob("*.json")):
        if path.name == "lang.json":
            continue
        doc = json.loads(path.read_text(encoding="utf-8"))
        for key, text in doc.get("strings", {}).items():
            out[key] = (path.relative_to(base), text)
    return out


def load_english() -> dict[str, tuple[Path, str | None]]:
    """key -> (file, English): the interface text and the game's own text of gamedata/text/en."""
    return load_lang(TEXT, "en")


def markup_tags(text: str, dialect: str) -> list[str]:
    """The codes a line carries, in order; a translation should keep the same ones."""
    if dialect == "plain":
        return sorted(re.findall(r"\{[a-z]+\}", text))
    tags = []
    for m in re.finditer(r"\{([^{}]*)\}", text.replace("{{", "").replace("}}", "")):
        name = m.group(1).split(" ")[0]
        if name in ("end", "choice", "page") or name.startswith("wait"):
            tags.append(name)
    return tags


# --- PO ------------------------------------------------------------------------------

def po_quote(s: str) -> str:
    s = s.replace("\\", "\\\\").replace('"', '\\"').replace("\t", "\\t")
    if "\n" not in s:
        return '"' + s + '"'
    parts = s.split("\n")
    lines = ['""'] + ['"' + p + '\\n"' for p in parts[:-1]] + (['"' + parts[-1] + '"'] if parts[-1] else [])
    return "\n".join(lines)


def po_unquote(lines: list[str]) -> str:
    out = []
    for line in lines:
        line = line.strip()
        if not (line.startswith('"') and line.endswith('"')):
            raise ValueError("bad PO string line: " + line)
        body = line[1:-1]
        i = 0
        while i < len(body):
            c = body[i]
            if c == "\\" and i + 1 < len(body):
                e = body[i + 1]
                out.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\"}.get(e, "\\" + e))
                i += 2
                continue
            out.append(c)
            i += 1
    return "".join(out)


def read_po(path: Path) -> list[dict]:
    entries: list[dict] = []
    cur: dict = {}
    field = None
    for raw in path.read_text(encoding="utf-8").split("\n") + [""]:
        line = raw.rstrip("\r")
        if not line.strip():
            if cur:
                entries.append(cur)
            cur, field = {}, None
            continue
        if line.startswith("#"):
            cur.setdefault("comments", []).append(line)
            continue
        m = re.match(r"(msgctxt|msgid|msgstr)\s+(.*)$", line)
        if m:
            field = m.group(1)
            cur[field] = [m.group(2)]
            continue
        if field is None:
            raise ValueError(f"{path}: stray line: {line}")
        cur[field].append(line)
    out = []
    for e in entries:
        if "msgid" not in e:
            continue
        out.append({k: po_unquote(e[k]) if k in ("msgctxt", "msgid", "msgstr") else e[k]
                    for k in e})
    return out


def write_po(path: Path | None, lang: str, en: dict, tr: dict) -> int:
    chunks = ['msgid ""\nmsgstr ""\n'
              '"Content-Type: text/plain; charset=UTF-8\\n"\n'
              f'"Language: {"" if lang == "pot" else lang}\\n"\n'
              '"X-Generator: tools/scripts/text_po.py\\n"\n']
    n = 0
    order = {k: i for i, k in enumerate(en)}  # file order: the story reads top to bottom
    for key in sorted(en, key=lambda k: (AREAS.index(area_of(en[k][0])) if area_of(en[k][0]) in AREAS else 9,
                                         str(en[k][0]), order[k])):
        rel, text = en[key]
        dialect = sh_text.dialect_for_key(key)
        note = {"msg": "map message", "ui": "game-font menu/item text", "plain": "PC interface text"}[dialect]
        lines = [f"#: {rel.as_posix()}", f"#. {note}"]
        if dialect == "plain" and re.search(r"\{[a-z]+\}", text):
            lines.append("#. keep the {placeholders}: the game fills them in")
        lines.append("msgctxt " + po_quote(key))
        lines.append("msgid " + po_quote(text))
        lines.append("msgstr " + po_quote(tr[key][1] if key in tr else ""))
        chunks.append("\n".join(lines) + "\n")
        n += 1
    body = "\n".join(chunks)
    if path is None:
        sys.stdout.write(body)
    else:
        path.write_text(body, encoding="utf-8")
    return n


# --- commands ------------------------------------------------------------------------

def cmd_export(args) -> int:
    en = load_english()
    tr = {} if args.lang == "pot" else load_lang(TEXT, args.lang)
    n = write_po(Path(args.o) if args.o else None, args.lang, en, tr)
    if args.o:
        print(f"wrote {n} entries to {args.o}", file=sys.stderr)
    return 0


def cmd_import(args) -> int:
    en = load_english()
    out_root = Path(args.out) if args.out else TEXT
    lang_dir = out_root / args.lang
    existing = load_lang(out_root, args.lang) if lang_dir.is_dir() else {}
    per_file: dict[Path, dict[str, str]] = {}
    for key, (rel, text) in existing.items():
        per_file.setdefault(rel, {})[key] = text
    bad = done = 0
    for e in read_po(Path(args.po)):
        key, msgstr = e.get("msgctxt", ""), e.get("msgstr", "")
        if not key or not msgstr:
            continue
        rel = en[key][0] if key in en else existing.get(key, (Path("mod.json"), ""))[0]
        try:
            sh_text.encode(msgstr, "", sh_text.dialect_for_key(key))
        except sh_text.MarkupError as err:
            print(f"{key}: {err}; not imported", file=sys.stderr)
            bad += 1
            continue
        per_file.setdefault(rel, {})[key] = msgstr
        done += 1
    order = {k: i for i, k in enumerate(en)}
    for rel, strings in per_file.items():
        doc: dict = {"format": "sh-text", "version": 1, "lang": args.lang}
        if rel.parts[0] == "maps":
            doc["map"] = rel.stem
        doc["strings"] = dict(sorted(strings.items(), key=lambda kv: order.get(kv[0], 1 << 30)))
        path = lang_dir / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    info = lang_dir / "lang.json"
    if not info.is_file() or args.name or args.english_name or args.direction or args.fonts:
        # The descriptor that makes the directory a language: its name in the language itself, the
        # English name, the writing direction and the font families to draw missing characters with.
        meta = json.loads(info.read_text(encoding="utf-8")) if info.is_file() else {}
        meta.update({"format": "sh-text-lang", "version": 1, "code": args.lang})
        if args.name:
            meta["name"] = args.name
        if args.english_name:
            meta["english_name"] = args.english_name
        if args.direction:
            meta["direction"] = args.direction
        if args.fonts:
            meta["fonts"] = [f.strip() for f in args.fonts.split(",") if f.strip()]
        meta.setdefault("name", args.lang)
        meta.setdefault("english_name", meta["name"])
        meta.setdefault("direction", "ltr")
        info.parent.mkdir(parents=True, exist_ok=True)
        info.write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"imported {done} strings into {lang_dir} ({bad} rejected)", file=sys.stderr)
    return 1 if bad else 0


def lang_stats(lang: str, en: dict, verbose: bool) -> tuple[str, int]:
    tr = load_lang(TEXT, lang)
    per: dict[str, list[int]] = {a: [0, 0] for a in AREAS}
    diff = 0
    for key, (rel, text) in en.items():
        area = area_of(rel)
        per.setdefault(area, [0, 0])[1] += 1
        if key not in tr:
            continue
        per[area][0] += 1
        if text is None:
            continue  # no import at hand: nothing to compare the markup with
        dialect = sh_text.dialect_for_key(key)
        if markup_tags(text, dialect) != markup_tags(tr[key][1], dialect):
            diff += 1
            if verbose:
                print(f"  {lang} {key}: markup {markup_tags(tr[key][1], dialect)} vs English "
                      f"{markup_tags(text, dialect)}")
    done = sum(v[0] for v in per.values())
    total = sum(v[1] for v in per.values())
    cols = "  ".join(f"{a} {v[0]}/{v[1]}" for a, v in per.items() if v[1])
    extra = len(set(tr) - set(en))
    line = f"{lang:7} {done:5}/{total} ({100.0 * done / max(total, 1):5.1f}%)  {cols}"
    if extra:
        line += f"  +{extra} keys English lacks"
    if diff:
        line += f"  {diff} markup differences"
    return line, diff


def cmd_stats(args) -> int:
    en = load_english()
    langs = args.langs or sorted(p.name for p in TEXT.iterdir() if p.is_dir() and p.name != "en")
    print(f"{'en':7} {len(en):5} keys (base)")
    worst = 0
    for lang in langs:
        line, diff = lang_stats(lang, en, args.verbose)
        print(line)
        worst = max(worst, diff)
    return 0


def cmd_check(args) -> int:
    en = load_english()
    line, diff = lang_stats(args.lang, en, True)
    print(line)
    bad = 0
    for key, (rel, text) in load_lang(TEXT, args.lang).items():
        try:
            sh_text.encode(text, "", sh_text.dialect_for_key(key))
        except sh_text.MarkupError as err:
            print(f"  {args.lang} {key}: {err}")
            bad += 1
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("export")
    p.add_argument("lang")
    p.add_argument("-o")
    p.set_defaults(fn=cmd_export)
    p = sub.add_parser("import")
    p.add_argument("po")
    p.add_argument("lang")
    p.add_argument("--out")
    p.add_argument("--name", help="the language's name in the language itself (shown in Options)")
    p.add_argument("--english-name")
    p.add_argument("--direction", choices=("ltr", "rtl"))
    p.add_argument("--fonts", help="font families for characters the game font lacks, comma separated")
    p.set_defaults(fn=cmd_import)
    p = sub.add_parser("stats")
    p.add_argument("langs", nargs="*")
    p.add_argument("-v", "--verbose", action="store_true")
    p.set_defaults(fn=cmd_stats)
    p = sub.add_parser("check")
    p.add_argument("lang")
    p.set_defaults(fn=cmd_check)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
