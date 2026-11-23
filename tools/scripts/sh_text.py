#!/usr/bin/env python3
"""Readable game text <-> engine message format.

The engine format is the original renderer's: '_' draws a space, ' ', '\\t' and
'\\n' are layout the renderer skips, and ~X codes carry one argument byte
(~N newline, ~E end, ~C<n> colour, ~S<n> choice, ~J<m>(<sec>) timed line, ...).
The readable format is what people edit: real spaces, real newlines and
{markup} instead of ~codes. The skipped layout whitespace does not survive in
the readable text; the base English files keep it in a per-string "ws" record
so encode(decode(s)) == s byte for byte. Without a record (packs, mods, edited
text) the encoder writes canonical layout, which the renderer draws the same.

The C++ twin is src/formats/convert/text/text_markup.cpp; both must produce the
same bytes (tests/convert_text/text_markup_test.cpp, tools/scripts/gen_text.py
--check).
"""

from __future__ import annotations

import re

# Codes whose argument byte is not read: the byte after the letter is
# swallowed, so it belongs to the layout run that follows.
NOARG = {"N": "\n", "E": "end", "P": "page", "D": "instant", "H": "hires", "M": "center", "T": "inset"}
NOARG_TAG = {v: k for k, v in NOARG.items() if k != "N"}
DIGIT = {"C": "color", "S": "choice", "L": "pos"}
DIGIT_TAG = {v: k for k, v in DIGIT.items()}
LAYOUT = " \t\n"
DIGITS = "0123456789"
UI_LAYOUT = " \t"  # Gfx_StringDraw: '\n' is a line break there, ' ' and '\t' draw nothing
BYTE_TAG = {"\v": "wide", "\x01": "kern"}
TAG_BYTE = {v: k for k, v in BYTE_TAG.items()}


class Unit:
    """One readable element: a glyph, a visible space, or a code."""

    __slots__ = ("kind", "code", "arg", "text")

    def __init__(self, kind: str, code: str = "", arg: str = "", text: str = ""):
        self.kind = kind  # "g" glyph, "s" space, "c" code, "j" timed line, "r" raw
        self.code = code  # code letter for "c", mode digit for "j"
        self.arg = arg    # digit for C/S/L, seconds for "j"
        self.text = text  # glyph character / raw bytes


def _engine_units(s: str, dialect: str = "msg") -> tuple[list[Unit], list[str]]:
    """Splits an engine string into units and the layout runs around them:
    runs[k] precedes units[k], runs[len(units)] trails."""
    units: list[Unit] = []
    runs: list[str] = [""]
    layout = LAYOUT if dialect == "msg" else UI_LAYOUT
    i, n = 0, len(s)
    while i < n:
        c = s[i]
        if c in layout:
            runs[-1] += c
            i += 1
            continue
        u = None
        if dialect == "ui":
            if c == "\n":
                u = Unit("n")
            elif c == "_":
                u = Unit("s")
            elif ord(c) < 0x20:
                u = Unit("b", text=c)
            else:
                u = Unit("g", text=c)
            i += 1
            units.append(u)
            runs.append("")
            continue
        if c == "~" and i + 1 < n:
            t = s[i + 1]
            if t == "J" and i + 2 < n and s[i + 2] in DIGITS and i + 3 < n and s[i + 3] == "(":
                close = s.find(")", i + 4)
                body = s[i + 4:close] if close > 0 else ""
                if close > 0 and body and all(ch in DIGITS or ch == "." for ch in body):
                    u = Unit("j", code=s[i + 2], arg=body)
                    i = close + 1
            elif t in DIGIT and i + 2 < n and s[i + 2] in DIGITS:
                u = Unit("c", code=t, arg=s[i + 2])
                i += 3
            elif t in NOARG and (i + 2 >= n or s[i + 2] in LAYOUT):
                # The swallowed argument byte (layout or the terminator) opens the next run.
                u = Unit("c", code=t)
                i += 2
        if u is None:
            if c == "~":
                # A code this format does not model: keep its three bytes raw.
                u = Unit("r", text=s[i:i + 3])
                i += len(u.text)
            elif c == "_":
                u = Unit("s")
                i += 1
            elif ord(c) < 0x20:
                u = Unit("b", text=c)
                i += 1
            else:
                u = Unit("g", text=c)
                i += 1
        units.append(u)
        runs.append("")
    return units, runs


def _has_wait(units: list[Unit]) -> bool:
    return any(u.kind == "j" for u in units)


def canonical_run(units: list[Unit], k: int, dialect: str = "msg") -> str:
    """The layout run the encoder writes before units[k] (k == len: trailing)
    when the text has no ws record. Matches the retail script's dominant style;
    it must keep every code's argument byte and the ~J terminator."""
    if dialect == "ui":
        return ""
    prev = units[k - 1] if k > 0 else None
    nxt = units[k] if k < len(units) else None
    if prev is None:
        if nxt is None or nxt.kind in ("j", "c", "r"):
            return ""
        return "\t"
    if prev.kind == "j":
        return "\t"
    if prev.kind == "c" and prev.code == "N":
        return "\n\t\t\t" if _has_wait(units) else "\n\t"
    if prev.kind == "c" and prev.code in NOARG:
        return " "
    if prev.kind == "c":
        return " "
    # prev is a glyph, space or raw code
    if nxt is None:
        return " "
    if nxt.kind == "c":
        return " "
    return ""


def _run_ok(units: list[Unit], k: int, run: str) -> bool:
    """A run may not merge a code's argument byte with the next unit."""
    prev = units[k - 1] if k > 0 else None
    if prev is None:
        return True
    if prev.kind == "c" and prev.code in NOARG and k < len(units) and run == "":
        return False
    if prev.kind == "j" and " " not in run and "\t" not in run:
        return False
    return True


def fnv1a(text: str) -> str:
    h = 0x811C9DC5
    for b in text.encode("utf-8"):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return f"{h:08x}"


_RUN_ENC = {" ": "s", "\t": "t", "\n": "n"}
_RUN_DEC = {v: k for k, v in _RUN_ENC.items()}


def _readable_units(units: list[Unit]) -> str:
    out = []
    color_open = False
    for u in units:
        if u.kind == "g":
            out.append("{{" if u.text == "{" else "}}" if u.text == "}" else u.text)
        elif u.kind == "s":
            out.append(" ")
        elif u.kind == "n":
            out.append("\n")
        elif u.kind == "b":
            out.append("{" + BYTE_TAG[u.text] + "}" if u.text in BYTE_TAG else "{byte %02x}" % ord(u.text))
        elif u.kind == "r":
            out.append("{raw " + u.text + "}")
        elif u.kind == "j":
            out.append("{wait " + u.arg + "}" if u.code == "0" else "{wait" + u.code + " " + u.arg + "}")
        elif u.code == "N":
            out.append("\n")
        elif u.code in NOARG:
            out.append("{" + NOARG[u.code] + "}")
        elif u.code == "C" and u.arg == "7" and color_open:
            out.append("{/color}")
            color_open = False
        else:
            out.append("{" + DIGIT[u.code] + " " + u.arg + "}")
            if u.code == "C":
                color_open = u.arg != "7"
    return "".join(out)


def dialect_for_key(key: str) -> str:
    """"ui.*" is PC interface text (plain), "item.*"/"menu.*" Gfx_StringDraw text, the rest messages."""
    if key.startswith("ui."):
        return "plain"
    return "ui" if key.startswith(("item.", "menu.")) else "msg"


def decode(engine: str, dialect: str = "msg") -> tuple[str, str]:
    """Engine string -> (readable text, ws record or "").
    dialect "msg": map messages (Gfx_MapMsg_*); "ui": item and menu strings (Gfx_StringDraw);
    "plain": PC interface text, no markup, '_' is a space."""
    if dialect == "plain":
        return engine.replace("_", " "), ""
    units, runs = _engine_units(engine, dialect)
    text = _readable_units(units)
    dev = []
    for k, run in enumerate(runs):
        if run != canonical_run(units, k, dialect):
            dev.append(f"{k}=" + ("".join(_RUN_ENC[c] for c in run) or "-"))
    ws = (fnv1a(text) + ":" + ",".join(dev)) if dev else ""
    return text, ws


class MarkupError(ValueError):
    pass


def _parse_readable(text: str, dialect: str = "msg") -> list[Unit]:
    units: list[Unit] = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "{":
            if text.startswith("{{", i):
                units.append(Unit("g", text="{"))
                i += 2
                continue
            close = text.find("}", i)
            if close < 0:
                raise MarkupError("unclosed { at " + str(i))
            tag = text[i + 1:close]
            i = close + 1
            if tag.startswith("raw "):
                units.append(Unit("r", text=tag[4:]))
                continue
            if tag in TAG_BYTE:
                units.append(Unit("b", text=TAG_BYTE[tag]))
                continue
            if tag.startswith("byte ") and re.fullmatch(r"[0-9a-f]{2}", tag[5:]):
                units.append(Unit("b", text=chr(int(tag[5:], 16))))
                continue
            parts = tag.split(" ")
            name = parts[0]
            if name == "/color" and len(parts) == 1:
                units.append(Unit("c", code="C", arg="7"))
            elif name in NOARG_TAG and len(parts) == 1:
                units.append(Unit("c", code=NOARG_TAG[name]))
            elif name in DIGIT_TAG and len(parts) == 2 and len(parts[1]) == 1 and parts[1] in DIGITS:
                units.append(Unit("c", code=DIGIT_TAG[name], arg=parts[1]))
            elif name.startswith("wait") and len(parts) == 2 and (name == "wait" or (len(name) == 5 and name[4] in DIGITS)) \
                    and parts[1] and all(ch in DIGITS or ch == "." for ch in parts[1]):
                units.append(Unit("j", code=name[4] if len(name) == 5 else "0", arg=parts[1]))
            else:
                raise MarkupError("unknown markup {" + tag + "}")
            continue
        if c == "}":
            if text.startswith("}}", i):
                i += 2
            else:
                i += 1
            units.append(Unit("g", text="}"))
            continue
        if c == "\n":
            units.append(Unit("c", code="N") if dialect == "msg" else Unit("n"))
        elif c == " ":
            units.append(Unit("s"))
        elif c in "\t\r":
            pass  # layout is the encoder's business
        else:
            units.append(Unit("g", text=c))
        i += 1
    return units


def _parse_ws(ws: str, text: str, count: int) -> dict[int, str] | None:
    if not ws:
        return None
    head, _, body = ws.partition(":")
    if head != fnv1a(text):
        return None  # the text was edited since the record was made
    out: dict[int, str] = {}
    for item in body.split(","):
        if not item:
            continue
        k, _, run = item.partition("=")
        if not k or any(ch not in DIGITS for ch in k) or int(k) > count:
            return None
        if run != "-" and any(c not in _RUN_DEC for c in run):
            return None
        out[int(k)] = "" if run == "-" else "".join(_RUN_DEC[c] for c in run)
    return out


def encode(text: str, ws: str = "", dialect: str = "msg") -> str:
    """Readable text (+ optional ws record) -> engine string."""
    if dialect == "plain":
        return text.replace(" ", "_")
    units = _parse_readable(text, dialect)
    exact = _parse_ws(ws, text, len(units))
    out = []
    for k in range(len(units) + 1):
        run = canonical_run(units, k, dialect)
        if exact is not None and k in exact and _run_ok(units, k, exact[k]):
            run = exact[k]
        out.append(run)
        if k == len(units):
            break
        u = units[k]
        if u.kind == "g":
            out.append(u.text)
        elif u.kind == "s":
            out.append("_")
        elif u.kind == "n":
            out.append("\n")
        elif u.kind == "b":
            out.append(u.text)
        elif u.kind == "r":
            out.append(u.text)
        elif u.kind == "j":
            out.append("~J" + u.code + "(" + u.arg + ")")
        else:
            out.append("~" + u.code + u.arg)
    return "".join(out)
