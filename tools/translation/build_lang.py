#!/usr/bin/env python3
"""Builds gamedata/text/<lang>/{common.json,items.json,maps/*.json} from a translator's draft
(docs/modding/translation-handbook.md).

  build_lang.py <lang> DRAFT.txt [DRAFT2.txt ...] --gamedata DIR [--out gamedata/text] [--template]

  --template   write DRAFT.txt as a template instead: every key with its English text in the draft form
               (a work file: keep it under .tmp/)

The draft is plain text, one string per line:

  ### maps/map0_s01.json          section: the file; keys below are written without the file's prefix
  was_i_dreaming<TAB>Мне это приснилось?
  there_is_a_kitchen_knife<TAB>Здесь <кухонный нож>.|Взять?
  # a comment

Only the words are written. The tool takes the control codes from the English string of the key:
the codes that open it ({wait 2.0}, {pos 4}, {center}, {color 3}, {hires}, {instant}) and the codes
that close it ({end}, {choice 4}, {/color}) are copied; a colour span inside the sentence is written
as <...> and gets the English {color N}...{/color}. A key whose English has no words (a pause, an
empty slot) needs no draft line: it is copied. A draft line that itself contains "{" is taken as the
complete string, codes and all.

Line breaks: "|" is a line break the translator chose and is kept. A string without "|" is wrapped
by the tool into the fewest lines that fit the box, as even as possible (the renderer does not wrap).
The limits are those of tools/scripts/check_translation.py.

A language written without spaces (NO_SPACE: ja, zh) has no words to wrap by. Its lines break after
a punctuation mark, before an opening bracket or a colour span, and inside a run of text where the
language allows it: Japanese where kana is followed by kanji or katakana (a phrase starts there),
Chinese between any two characters; never against kinsoku (check_translation.py), never inside a
run of Latin letters or digits. A space in such a draft is a break point the translator offers and
is not written, except between two Latin letters or digits; a real blank is the ideographic space
U+3000. Korean has spaces and breaks only at them.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "scripts"))
import check_translation as ct  # noqa: E402
import en_text  # noqa: E402

# A line should not end on one of these (a word that belongs to the next one).
CLINGS = {}
CLINGS["ru"] = {"а", "в", "и", "к", "о", "с", "у", "я", "во", "до", "за", "из", "мы", "на", "не", "ни", "но",
                "об", "он", "от", "по", "со", "ты", "для", "как", "над", "под", "при", "что"}
CLINGS["pt"] = {"a", "e", "o", "à", "é", "ao", "as", "da", "de", "do", "em", "me", "na", "no", "os", "ou", "se",
                "te", "um", "às", "aos", "com", "das", "dos", "mas", "meu", "nas", "nos", "num", "não", "por",
                "que", "sem", "seu", "sob", "sua", "teu", "tua", "uma", "numa", "para", "pela", "pelo", "minha"}
CLINGS["pl"] = {"a", "i", "o", "u", "w", "z", "bo", "by", "co", "do", "ku", "na", "od", "po", "to", "we", "za",
                "ze", "że", "aby", "ale", "ani", "bez", "czy", "dla", "gdy", "jak", "lub", "nad", "nie", "pod",
                "przy", "żeby", "przez"}
CLINGS["es"] = {"a", "e", "o", "u", "y", "al", "de", "el", "en", "es", "la", "le", "lo", "me", "mi", "ni", "no",
                "se", "si", "su", "te", "tu", "un", "con", "del", "las", "los", "mis", "muy", "más", "por",
                "que", "sin", "sus", "tan", "tus", "una", "como", "para", "pero", "unas", "unos"}
CLINGS["fr"] = {"y", "à", "au", "ce", "de", "du", "en", "et", "il", "je", "la", "le", "ma", "ne", "on", "ou",
                "sa", "si", "ta", "tu", "un", "aux", "ces", "cet", "des", "les", "mes", "mon", "par", "que",
                "qui", "ses", "son", "sur", "tes", "ton", "une", "dans", "pour", "sans", "sous", "cette"}
CLINGS["ar"] = {"و", "أن", "أو", "أي", "إن", "بل", "ثم", "عن", "في", "قد", "كل", "كي", "لا", "لم", "لن", "لو",
                "ما", "مع", "من", "هل", "يا", "إذا", "إلا", "إلى", "بين", "تلك", "حتى", "دون", "ذلك", "سوى",
                "على", "عند", "غير", "كان", "لقد", "لكن", "ليس", "منذ", "هذا", "هذه", "التي", "الذي"}
CLINGS["it"] = {"a", "e", "i", "o", "è", "ai", "al", "ci", "da", "di", "ed", "ha", "ho", "il", "in", "la", "le",
                "lo", "ma", "mi", "se", "si", "su", "ti", "un", "che", "con", "dal", "dei", "del", "fra", "gli",
                "mia", "mio", "nei", "nel", "non", "per", "più", "sul", "tra", "tua", "tuo", "una", "uno",
                "agli", "alla", "alle", "allo", "dalla", "degli", "della", "delle", "dello", "nella", "nelle",
                "nello", "sulla"}
CLINGS["tr"] = {"o", "bu", "en", "ne", "ve", "ya", "şu", "bir", "her", "hiç", "pek", "çok", "daha"}
CLINGS["ko"] = {"그", "꼭", "내", "네", "더", "두", "못", "및", "세", "안", "이", "잘", "저", "제", "한", "그런", "모든", "무슨",
                "어떤", "이런", "저런"}
CLINGS["de"] = {"am", "an", "im", "in", "ob", "so", "um", "zu", "als", "auf", "aus", "bei", "das", "dem", "den",
                "der", "des", "die", "ein", "für", "ihr", "mit", "nur", "und", "vom", "von", "vor", "wie",
                "zum", "zur", "aber", "dass", "dein", "eine", "ihre", "kein", "mein", "nach", "noch", "oder",
                "sehr", "sein", "wenn", "über", "deine", "durch", "einem", "einen", "einer", "eines", "keine",
                "meine", "seine", "unter"}

# A line should not start with one of these (a particle that belongs to the previous word).
TRAILERS = {}
TRAILERS["ru"] = {"б", "ж", "—", "бы", "же", "ли", "ль"}
TRAILERS["pl"] = {"—", "się", "też"}
TRAILERS["fr"] = {"—"}
TRAILERS["it"] = {"—"}
TRAILERS["tr"] = {"—", "da", "de", "ki", "mi", "mu", "mü", "mı", "bile", "gibi", "için", "değil", "kadar",
                  "misin", "miydi", "miyim", "musun", "muydu", "muyum", "müsün", "müydü", "müyüm", "mısın",
                  "mıydı", "mıyım"}
TRAILERS["ko"] = {"—", "거", "걸", "것", "게", "듯", "때", "뿐", "수", "줄", "채", "같군", "같다", "같아", "않다", "않아", "없다",
                  "없어", "있다", "있어"}
TRAILERS["de"] = {"—"}

# Languages written without spaces between words, and where a run of their text may break:
#   "phrase"     where the script shows that a phrase starts: kana followed by kanji or katakana
#   "character"  between any two characters: the script does not mark its words
NO_SPACE = {"ja": "phrase", "zh": "character"}
_CLAUSE_END = "、。，．！？…‥」』）”’—"
_OPENERS = "「『（<"
_HONORIFIC = "おご"     # kana prefixes that belong to the kanji after them


def _is_latin(ch: str) -> bool:
    return ch.isascii() and ch.isalnum()


def _is_kana(ch: str) -> bool:
    return "ぁ" <= ch <= "ゟ"


def _starts_phrase(ch: str) -> bool:
    """Kanji or katakana: after kana, a new phrase usually starts here."""
    return "一" <= ch <= "鿿" or "ァ" <= ch <= "ヺ"


def may_break(a: str, b: str, lang: str) -> bool:
    """Whether a line of a language without spaces may break between the characters a and b."""
    # Kinsoku; the colour span marks "<" and ">" stay with their word; an ellipsis and a row of dashes
    # stay with the text before them.
    if ct.opens(a) or a == "<" or ct.closes(b) or b in ">…‥—":
        return False
    if a in _CLAUSE_END or b in _OPENERS:
        return True
    if NO_SPACE[lang] == "phrase":
        return _is_kana(a) and a not in _HONORIFIC and _starts_phrase(b)
    return ct.is_wide(a) or ct.is_wide(b)


def break_units(draft: str, lang: str) -> list[tuple[str, str]]:
    """(unit, what joins it to the unit before it) for every piece of a draft a line may start with."""
    words = draft.split(" ")
    if lang not in NO_SPACE:
        return [(word, " " if n else "") for n, word in enumerate(glue(words))]
    out: list[tuple[str, str]] = []
    for word in filter(None, words):
        # The draft's space is a break point; only between Latin letters or digits it is a space of the text.
        joiner = " " if out and _is_latin(out[-1][0][-1]) and _is_latin(word[0]) else ""
        start = 0
        for i in range(1, len(word)):
            if may_break(word[i - 1], word[i], lang):
                out.append((word[start:i], joiner))
                joiner, start = "", i
        out.append((word[start:], joiner))
    return out


def join_units(units: list[tuple[str, str]]) -> str:
    return "".join(joiner + unit for unit, joiner in units)


def glue(words: list[str]) -> list[str]:
    """Punctuation written apart from its word (French "Quoi ?", "« mot »") stays on the word's line:
    a word made only of closing punctuation joins the word before it, an opening quote the word after it."""
    out: list[str] = []
    opening = False
    for word in words:
        if out and (opening or (word and all(ch in "?!:;»>" for ch in word) and word != ">")):
            out[-1] += " " + word
        else:
            out.append(word)
        opening = word in ("«", "<«")
    return out


def split_codes(en: str) -> tuple[list[str], list[str], list[str]]:
    """(opening codes, codes inside the sentence, closing codes) of an English string."""
    lead = re.match(r"(?:\{[^{}]*\})*", en).group(0)
    rest = en[len(lead):]
    trail = re.search(r"(?:\{[^{}]*\})*$", rest).group(0)
    mid = ct.tags_of(rest[:len(rest) - len(trail)] if trail else rest)
    lead_t, trail_t = ct.tags_of(lead), ct.tags_of(trail)
    # A colour that opens the string and closes inside it (or the reverse) is a span of the sentence.
    if "{/color}" in mid and not any(t.startswith("{color") for t in mid):
        mid.insert(0, lead_t.pop())
    if any(t.startswith("{color") for t in mid) and "{/color}" not in mid and trail_t[:1] == ["{/color}"]:
        mid.append(trail_t.pop(0))
    return lead_t, mid, trail_t


def to_draft(en: str) -> str:
    lead, mid, trail = split_codes(en)
    body = en[len("".join(lead)):len(en) - len("".join(trail))] if trail else en[len("".join(lead)):]
    if mid:
        body = body.replace(mid[0], "<", 1).replace("{/color}", ">", 1)
    return body.replace("\n", "|")


def wrap(units: list[tuple[str, str]], limit: int, font: dict[str, int], lang: str) -> list[str] | None:
    """The fewest lines within the limit; among those, the most even ones."""
    words = [unit for unit, _ in units]
    # The colour span marks "<" and ">" are not drawn. Only a text without spaces is measured without
    # them: the texts of the other languages were wrapped with the marks counted, and stay reproducible.
    widths = [ct.line_px(w.replace("<", "").replace(">", "") if lang in NO_SPACE else w, font) for w in words]
    if any(w > limit for w in widths):
        return None
    n = len(words)

    space = ct.space_px(join_units(units))
    gaps = [space * len(joiner) for _, joiner in units]

    def span(i: int, j: int) -> int:
        return sum(widths[i:j]) + sum(gaps[i + 1:j])

    lines, i = 0, 0
    while i < n:
        j = i + 1
        while j < n and span(i, j + 1) <= limit:
            j += 1
        lines, i = lines + 1, j
    clings = CLINGS.get(lang, set())
    widest = max(span(0, n) / lines, max(widths))
    inf = float("inf")
    best = [[inf] * (lines + 1) for _ in range(n + 1)]
    back = [[0] * (lines + 1) for _ in range(n + 1)]
    best[0][0] = 0.0
    for j in range(1, n + 1):
        for k in range(1, lines + 1):
            for i in range(k - 1, j):
                w = span(i, j)
                if w > limit or best[i][k - 1] == inf:
                    continue
                cost = best[i][k - 1] + (w - widest) ** 2
                if j < n:
                    last = re.sub(r"[<>«„(]", "", words[j - 1]).lower()
                    end = last.rstrip("”’»）」』")[-1:]
                    if last in clings or (words[j - 1].startswith(("«", "„")) and len(last) <= 3):
                        cost += 5000
                    elif end in ".!?…؟。！？":
                        cost -= 3000  # a break between sentences reads best
                    elif end in ",:;،؛，：；、":
                        cost -= 700
                    if re.sub(r"[<>»”.,:;!?…]", "", words[j]).lower() in TRAILERS.get(lang, set()):
                        cost += 5000
                if cost < best[j][k]:
                    best[j][k], back[j][k] = cost, i
    if best[n][lines] == inf:
        return None
    out, j = [], n
    for k in range(lines, 0, -1):
        i = back[j][k]
        out.append(join_units([(words[i], "")] + units[i + 1:j]))
        j = i
    return out[::-1]


def compose(key: str, en: str, draft: str, font: dict[str, int], lang: str,
            notes: list[str]) -> tuple[str, list[str]]:
    errors: list[str] = []
    is_item = key.startswith("item.")
    if "{" in draft:
        return draft.replace("|", "\n"), errors
    lead, mid, trail = split_codes(en)
    if is_item and key.endswith(".name"):
        limit, max_lines = ct.NAME_PX, 1
    elif is_item:
        limit, max_lines = ct.DESC_PX, ct.DESC_LINES
    else:
        limit = ct.MSG_LINE_PX
        max_lines = ct.message_lines_limit(en, en.count("\n") + 1)
    if "|" in draft:
        lines = draft.split("|")
    else:
        units = break_units(draft, lang)
        lines = wrap(units, limit, font, lang) or [join_units(units)]
    clings = CLINGS.get(lang, set())
    for line in lines[:-1]:
        tail = line.split(" ")[-1]
        last = re.sub(r"[<>«„(]", "", tail).lower()
        if last in clings or (tail.startswith(("«", "„")) and len(last) <= 3):
            notes.append(f"a line ends on {tail!r}: {' / '.join(lines)}")
    if len(lines) > max_lines:
        errors.append(f"{len(lines)} lines, at most {max_lines}: {' / '.join(lines)}")
    body = "\n".join(lines)
    if mid:
        if body.count("<") != 1 or body.count(">") != 1 or body.index("<") > body.index(">"):
            errors.append(f"the English has a colour span {mid[0]}...{{/color}}: mark it with <...>")
        body = body.replace("<", mid[0], 1).replace(">", "{/color}", 1)
    elif "<" in body or ">" in body:
        errors.append("<...> but the English has no colour span inside the sentence")
    return "".join(lead) + body + "".join(trail), errors


def read_draft(paths: list[Path]) -> dict[str, str]:
    out: dict[str, str] = {}
    for path in paths:
        prefix = ""
        for n, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
            if line.startswith("###"):
                rel = Path(line[3:].strip())
                prefix = {"common.json": "common.", "items.json": "item."}.get(rel.name, rel.stem + ".")
                continue
            if not line.strip() or line.startswith("#"):
                continue
            if "\t" not in line:
                raise SystemExit(f"{path}:{n}: expected key<TAB>text")
            key, text = line.split("\t", 1)
            key = prefix + key.strip()
            if key in out:
                raise SystemExit(f"{path}:{n}: {key} is given twice")
            out[key] = text.rstrip("\r")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("lang")
    ap.add_argument("drafts", nargs="+")
    ap.add_argument("--gamedata")
    ap.add_argument("--out", default=str(ct.TEXT))
    ap.add_argument("--template", action="store_true")
    args = ap.parse_args()
    gamedata = ct.find_gamedata(args.gamedata)
    if gamedata is None:
        print("build_lang needs the game font: pass --gamedata <imported gamedata directory>", file=sys.stderr)
        return 2
    english = en_text.english()
    per_file: dict[Path, list[str]] = {}
    for key, (rel, _text, _ws) in english.items():
        per_file.setdefault(rel, []).append(key)

    if args.template:
        chunks = []
        for rel, keys in per_file.items():
            prefix = {"common.json": "common.", "items.json": "item."}.get(rel.name, rel.stem + ".")
            chunks.append(f"### {rel.as_posix()}")
            chunks += [f"{key[len(prefix):]}\t{to_draft(english[key][1])}" for key in keys if ct.has_letters(english[key][1])]
        Path(args.drafts[0]).write_text("\n".join(chunks) + "\n", encoding="utf-8")
        print(f"wrote the template {args.drafts[0]}")
        return 0

    font = ct.load_font(gamedata)
    ct.add_fallback_glyphs(font, args.lang)
    draft = read_draft([Path(p) for p in args.drafts])
    bad = 0
    for key in sorted(set(draft) - set(english)):
        print(f"{key}: the English has no such key")
        bad += 1
    out_dir = Path(args.out) / args.lang
    written = 0
    for rel, keys in per_file.items():
        strings: dict[str, str] = {}
        for key in keys:
            en = english[key][1]
            if not ct.has_letters(en):
                strings[key] = en
                continue
            if key not in draft:
                print(f"{rel} {key}: no translation in the draft")
                bad += 1
                continue
            notes: list[str] = []
            strings[key], errors = compose(key, en, draft[key], font, args.lang, notes)
            for note in notes:
                print(f"note: {rel} {key}: {note}")
            for err in errors:
                print(f"{rel} {key}: {err}")
                bad += 1
        doc: dict = {"format": "sh-text", "version": 1, "lang": args.lang}
        if rel.parts[0] == "maps":
            doc["map"] = rel.stem
        doc["strings"] = strings
        path = out_dir / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        written += len(strings)
    print(f"build_lang {args.lang}: {written} strings in {len(per_file)} files under {out_dir}, {bad} problems")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
