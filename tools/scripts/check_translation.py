#!/usr/bin/env python3
"""Checks a translation of the game's own text, gamedata/text/<lang>/ (docs/modding/translation-handbook.md).

  check_translation.py <lang> [--gamedata DIR] [--root gamedata/text] [-q]

The game's English is gamedata/text/en of the repository. The metrics of the game font come from an imported game
(--gamedata DIR, the gamedata directory next to the game after its first start; default: $SH_GAMEDATA, then
.tmp/gate/build/gamedata of the repository).

Checked for common.json, maps/*.json and items.json of the language:
  keys      every key gamedata/text/en has English for is present, in the file the base keeps it in; no other keys
  markup    the control codes ({wait 2.5}, {color 2}, {/color}, {end}, {choice 4}, {pos 4}, {center},
            {hires}, {instant}, {page}) are the English ones, same kind, same arguments, same order;
            the codes that open or close the English message open or close the translation
  encoding  every string encodes to engine bytes (tools/scripts/sh_text.py)
  limits    the renderer does not wrap: a message line is at most 300 px of the 12x16 font, a page has
            at most max(3, English lines) lines (9 in the grid box {pos 4}, never more than 9), a {instant} page at most 400
            glyphs; an item name is one line of at most 202 px, a description at most 3 lines of 270 px
  english   no English left: a string equal to its English, or (for a non-Latin script) Latin words that
            the language's glossary does not list under "keep_latin"
  font      every character has a glyph in the game font, or is drawn outside it:
            - listed under "fallback_glyphs" in the language's glossary (system font; measured as its
              base letter);
            - East Asian wide (Hangul, kana, CJK and their punctuation): system-font fallback, 12 px;
            - a script the game font does not hold (Arabic): fallback font, shaped and ordered at run
              time; the width is estimated (7 px a letter, 5 px a space, 0 for a combining mark);
  script    text of a shaped script is stored in logical order, unshaped: no presentation forms, no
            tatweel, no directional control characters
  kinsoku   in a line with East Asian wide characters: after a line break no closing punctuation, small
            kana or long-vowel mark starts the line (an ellipsis may); no line ends with opening punctuation
  spacing   no line starts or ends with a space, no double spaces (unless the English does the same)

Exit code 1 when there are problems.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import unicodedata
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import en_text  # noqa: E402
import sh_text  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
TEXT = ROOT / "gamedata" / "text"
GLOSSARY = ROOT / "tools" / "translation"

MSG_LINE_PX = 300       # lines are centred on a 320 px screen; the size fit keeps 304 px (fit_half_width 152)
MSG_LINES_MIN_CAP = 3   # a bottom box rises by a line per extra line; 3 lines is what the dialogue uses
MSG_LINES_HARD = 9      # MessageSpec::line_cap
INSTANT_GLYPHS = 400    # text_style.json message.display_all_length
NAME_PX = 202           # widest English item name
DESC_PX = 270           # widest English description line
DESC_LINES = 3
SPACE_PX = 6            # FontAtlas::space_advance
# Bytes below 0x80 keep the meaning of the disc's font.
ASCII_ALIAS = {"*": "=", "=": "—", "@": "★", "[": "“", "]": "”", "\\": "!", "^": "&",
               "`": "’"}
# Scripts outside the game font: the engine shapes them and draws them with a fallback font.
FALLBACK_SCRIPTS = ((0x0600, 0x06FF), (0x0750, 0x077F), (0x08A0, 0x08FF))   # Arabic
FALLBACK_LETTER_PX = 7  # average advance of a joined letter at the size of the 12x16 font
FALLBACK_SPACE_PX = 5
# Characters a shaping engine produces or a hand-made layout needs; the stored text has none of them.
SHAPED = ((0xFB50, 0xFDFF), (0xFE70, 0xFEFF),   # Arabic presentation forms A and B
          (0x0640, 0x0640),                     # tatweel
          (0x200E, 0x200F), (0x202A, 0x202E), (0x2066, 0x2069), (0x061C, 0x061C))  # directional controls
WIDE_PX = 12            # a full-width character fills the 12 px cell, in a kanji font and in the fallback alike
# Kinsoku. Wide punctuation is recognised by its Unicode category (opens, closes); these are the rest:
# marks that belong to the character before them, and the narrow punctuation used among wide characters.
NO_LINE_START = set("゛゜ヽヾゝゞ々ー" "ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ" "”’,.!?)")
NO_LINE_END = set("“‘(")

TAG_RE = re.compile(r"\{([^{}]*)\}")


def find_gamedata(arg: str | None) -> Path | None:
    for cand in (arg, os.environ.get("SH_GAMEDATA"), ROOT / ".tmp" / "gate" / "build" / "gamedata"):
        if cand and (Path(cand) / "convert" / "fonts" / "game.font.json").is_file():
            return Path(cand)
    return None


def load_font(gamedata: Path) -> dict[str, int]:
    path = gamedata / "convert" / "fonts" / "game.font.json"
    if not path.is_file():
        raise SystemExit(f"{path}: no game font in this import (importer version 4 or newer writes it)")
    doc = json.loads(path.read_text(encoding="utf-8"))
    return {g["char"]: int(g["advance"]) for g in doc["glyphs"]}


def add_fallback_glyphs(font: dict[str, int], lang: str) -> list[str]:
    """Characters the game font lacks but the language's glossary lists under "fallback_glyphs" (the engine
    draws them from the system font): each gets the advance of its base letter, so that lines can be measured."""
    glossary = GLOSSARY / f"glossary_{lang}.json"
    if not glossary.is_file():
        return []
    added = []
    for ch in json.loads(glossary.read_text(encoding="utf-8")).get("fallback_glyphs", []):
        if ch not in font:
            base = unicodedata.normalize("NFD", ch)[0]
            font[ch] = font.get(base, 12)
            added.append(ch)
    return added


def lang_meta(root: Path, lang: str) -> dict:
    info = root / lang / "lang.json"
    return json.loads(info.read_text(encoding="utf-8")) if info.is_file() else {}








def strip_tags(text: str) -> str:
    return TAG_RE.sub("", text.replace("{{", "\x00").replace("}}", "\x01")).replace("\x00", "{").replace("\x01", "}")


def tags_of(text: str) -> list[str]:
    return ["{" + m.group(1) + "}" for m in TAG_RE.finditer(text.replace("{{", "").replace("}}", ""))]


def edge_tags(text: str) -> tuple[list[str], list[str]]:
    """The codes before the first and after the last drawn character."""
    lead = re.match(r"(?:\{[^{}]*\})*", text).group(0)
    rest = text[len(lead):]
    trail = re.search(r"(?:\{[^{}]*\})*$", rest).group(0)
    return tags_of(lead), tags_of(trail)


def in_ranges(ch: str, ranges) -> bool:
    return any(a <= ord(ch) <= b for a, b in ranges)


def is_fallback(ch: str) -> bool:
    return in_ranges(ch, FALLBACK_SCRIPTS)


def space_px(line: str) -> int:
    """A line of a fallback script takes its spaces from the fallback font."""
    return FALLBACK_SPACE_PX if any(is_fallback(ch) for ch in line) else SPACE_PX


def is_wide(ch: str) -> bool:
    """East Asian wide and full-width characters (Hangul, kana, CJK ideographs and their punctuation):
    outside the game font, drawn by the engine's fallback fonts, one 12 px cell each. With them the marks the
    engine counts as wide though Unicode calls their width ambiguous (is_wide in src/i18n/core/unicode.cpp):
    the dash U+2015, the leader U+2025, the reference mark U+203B, the geometric marks U+25A0..U+25EF."""
    return unicodedata.east_asian_width(ch) in ("W", "F") or ch in "\u2015\u2025\u203b" or "\u25a0" <= ch <= "\u25ef"


def opens(ch: str) -> bool:
    """Kinsoku: a character that may not end a line."""
    return ch in NO_LINE_END or (is_wide(ch) and unicodedata.category(ch) in ("Ps", "Pi"))


def closes(ch: str) -> bool:
    """Kinsoku: a character that may not start a line."""
    return ch in NO_LINE_START or (is_wide(ch) and unicodedata.category(ch) in ("Pe", "Pf", "Po"))


def line_px(line: str, font: dict[str, int]) -> int:
    width = 0
    line = strip_tags(line)
    space = space_px(line)
    for ch in line:
        if ch == " ":
            width += space
        elif is_fallback(ch) and ch not in font:
            width += 0 if unicodedata.combining(ch) or unicodedata.category(ch) == "Mn" else FALLBACK_LETTER_PX
        else:
            ch = ASCII_ALIAS.get(ch, ch) if ord(ch) < 0x80 else ch
            width += font.get(ch, WIDE_PX)
    return width


def pages_of(text: str) -> list[list[str]]:
    return [page.split("\n") for page in text.split("{page}")]


def message_lines_limit(en: str, en_lines: int) -> int:
    """Lines a page of a message may have. The grid box ({pos 4}) is centred vertically for up to 9 lines;
    the other boxes keep what the English uses, with room for the 3 lines of the dialogue box."""
    if "{pos 4}" in en:
        return MSG_LINES_HARD
    return min(MSG_LINES_HARD, max(MSG_LINES_MIN_CAP, en_lines))


def has_letters(text: str) -> bool:
    return any(ch.isalpha() for ch in strip_tags(text))


def load_lang_files(root: Path, lang: str) -> dict[Path, dict[str, str]]:
    out: dict[Path, dict[str, str]] = {}
    base = root / lang
    for path in sorted(base.rglob("*.json")):
        if path.name == "lang.json":
            continue
        doc = json.loads(path.read_text(encoding="utf-8"))
        out[path.relative_to(base)] = doc.get("strings", {})
    return out


def is_game_file(rel: Path) -> bool:
    return rel.parts[0] == "maps" or rel.name in ("common.json", "items.json")


def check(lang: str, gamedata: Path, root: Path = TEXT, out=print) -> tuple[int, dict]:
    english = en_text.english()
    game_glyphs = load_font(gamedata)
    fallback = add_fallback_glyphs(game_glyphs, lang)
    files = load_lang_files(root, lang)
    font = game_glyphs

    def drawn_outside_game_font(ch: str) -> bool:
        """The engine's fallback fonts take the wide characters and the fallback scripts."""
        return is_fallback(ch) or is_wide(ch)

    # A language whose own letters are not Latin: Cyrillic of the game font, or a script drawn outside it.
    non_latin = any(
        is_fallback(ch) or is_wide(ch) or 0x0400 <= ord(ch) <= 0x04FF
        for rel, strings in files.items() if is_game_file(rel) for t in strings.values() for ch in t)
    keep_latin: set[str] = set()
    glossary = GLOSSARY / f"glossary_{lang}.json"
    if glossary.is_file():
        keep_latin = set(json.loads(glossary.read_text(encoding="utf-8")).get("keep_latin", []))

    problems = 0

    def bad(where: str, what: str) -> None:
        nonlocal problems
        problems += 1
        out(f"{where}: {what}")

    have: dict[str, tuple[Path, str]] = {}
    for rel, strings in files.items():
        if not is_game_file(rel):
            continue
        for key, text in strings.items():
            if key in have:
                bad(f"{rel} {key}", f"the key is also in {have[key][0]}")
            have[key] = (rel, text)

    for key, (rel, _text, _ws) in english.items():
        if key not in have:
            bad(f"{rel} {key}", "missing (the English has this key)")
        elif have[key][0] != rel:
            bad(f"{have[key][0]} {key}", f"belongs in {rel}")
    for key, (rel, _text) in have.items():
        if key not in english:
            bad(f"{rel} {key}", "the English has no such key")

    stats = {"messages": 0, "item_names": 0, "item_descs": 0, "copied": 0, "max_line_px": 0, "max_lines": 0}
    missing_glyphs: dict[str, str] = {}
    for key, (rel, text) in have.items():
        if key not in english:
            continue
        en = english[key][1]
        where = f"{rel} {key}"
        dialect = sh_text.dialect_for_key(key)
        is_item = key.startswith("item.")
        stats["item_names" if key.endswith(".name") and is_item else "item_descs" if is_item else "messages"] += 1

        try:
            sh_text.encode(text, "", dialect)
        except sh_text.MarkupError as err:
            bad(where, f"does not encode: {err}")
            continue

        if tags_of(text) != tags_of(en):
            bad(where, f"control codes {' '.join(tags_of(text)) or '-'} differ from the English "
                       f"{' '.join(tags_of(en)) or '-'}")
        elif edge_tags(text) != edge_tags(en) and has_letters(en):
            lead, trail = edge_tags(en)
            # A colour may open or close anywhere in the sentence; the box-level codes may not move.
            fixed = [t for t in lead + trail if not t.startswith(("{color", "{/color"))]
            mine = [t for t in sum(edge_tags(text), []) if not t.startswith(("{color", "{/color"))]
            if fixed != mine:
                bad(where, "the codes that open or close the English message must open or close the translation")

        if not has_letters(en):
            stats["copied"] += 1
            if text != en:
                bad(where, f"the English has no words here; keep it as it is: {en!r}")
            continue

        # Limits.
        en_pages = pages_of(en)
        for p, lines in enumerate(pages_of(text)):
            if is_item:
                limit_px = NAME_PX if key.endswith(".name") else DESC_PX
                limit_lines = 1 if key.endswith(".name") else DESC_LINES
            else:
                limit_px = MSG_LINE_PX
                en_lines = len(en_pages[p]) if p < len(en_pages) else 1
                limit_lines = message_lines_limit(en, en_lines)
            if len(lines) > limit_lines:
                bad(where, f"{len(lines)} lines on a page, at most {limit_lines}")
            stats["max_lines"] = max(stats["max_lines"], len(lines))
            for line in lines:
                px = line_px(line, font)
                if not is_item:
                    stats["max_line_px"] = max(stats["max_line_px"], px)
                if px > limit_px:
                    bad(where, f"line of {px} px, at most {limit_px}: {strip_tags(line)!r}")
            if not is_item and "{instant}" in text:
                glyphs = sum(len(strip_tags(line)) for line in lines)
                if glyphs > INSTANT_GLYPHS:
                    bad(where, f"{glyphs} glyphs on a {{instant}} page, at most {INSTANT_GLYPHS}")

        # Spacing.
        plain_lines = [strip_tags(line) for line in text.split("\n")]
        en_plain = [strip_tags(line) for line in en.split("\n")]
        if any(line != line.strip(" ") for line in plain_lines) and not any(l != l.strip(" ") for l in en_plain):
            bad(where, "a line starts or ends with a space")
        if any("  " in line.strip(" ") for line in plain_lines) and not any("  " in l.strip(" ") for l in en_plain):
            bad(where, "double space")

        # Kinsoku. The first line is free to start with a mark: no line break put it there. An ellipsis
        # ("…", "...") may open a sentence, so it may start a line.
        for n, line in enumerate(plain_lines):
            if not any(is_wide(ch) for ch in line):
                continue
            if n and closes(line[0]) and not line.startswith(".."):
                bad(where, f"kinsoku: a line starts with {line[0]!r}: {line!r}")
            if opens(line[-1]):
                bad(where, f"kinsoku: a line ends with {line[-1]!r}: {line!r}")

        # English left behind.
        plain = strip_tags(text)
        if plain.strip() == strip_tags(en).strip():
            if non_latin or len(re.findall(r"[A-Za-z]{2,}", plain)) >= 3:
                bad(where, "equal to the English")
        elif non_latin:
            words = [w for w in re.findall(r"[A-Za-z][A-Za-z']*", plain) if w not in keep_latin]
            if words:
                bad(where, f"Latin text {' '.join(words)!r} (translate it or list it under keep_latin in "
                           f"tools/translation/glossary_{lang}.json)")

        # Font.
        for ch in plain:
            if ch in " \n":
                continue
            glyph = ASCII_ALIAS.get(ch, ch) if ord(ch) < 0x80 else ch
            if in_ranges(ch, SHAPED):
                bad(where, f"U+{ord(ch):04X}: a presentation form, a tatweel or a directional control; "
                           "store the text in logical order, unshaped")
            elif glyph not in game_glyphs and ch not in missing_glyphs and not drawn_outside_game_font(ch):
                missing_glyphs[ch] = key
    for ch, key in sorted(missing_glyphs.items()):
        bad(f"font U+{ord(ch):04X} {ch!r}", f"no glyph in the game font (first in {key}); it needs a fallback glyph")
    stats["keys_english"] = len(english)
    stats["fallback_glyphs"] = "".join(fallback)
    return problems, stats


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("lang")
    ap.add_argument("--gamedata", help="an imported gamedata directory: the metrics of the game font")
    ap.add_argument("--root", default=str(TEXT), help="directory of the language directories (default gamedata/text)")
    ap.add_argument("-q", "--quiet", action="store_true", help="only the summary line")
    args = ap.parse_args()
    gamedata = find_gamedata(args.gamedata)
    if gamedata is None:
        print("check_translation needs the game font: pass --gamedata <imported gamedata directory>",
              file=sys.stderr)
        return 2
    problems, stats = check(args.lang, gamedata, Path(args.root), (lambda *_: None) if args.quiet else print)
    print(f"check_translation {args.lang}: {stats['messages']} messages, {stats['item_names']} item names, "
          f"{stats['item_descs']} item descriptions ({stats['copied']} without words) of {stats['keys_english']} "
          f"English keys; widest message line {stats['max_line_px']} px, most lines {stats['max_lines']}; "
          f"{problems} problems")
    if stats["fallback_glyphs"]:
        print(f"  not in the game font, left to the system-font fallback: {' '.join(stats['fallback_glyphs'])}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
