#!/usr/bin/env python3
"""The input prompt sheet of the Controls screen, cut from Kenney's "Input Prompts Pixel 16x" tile map (CC0, in
kenney_input_prompts_pixel/ beside this script).

    python3 -I tools/scripts/build_input_glyphs.py                  write the sheet and the cell table
    python3 -I tools/scripts/build_input_glyphs.py --check          verify the files in the tree are what this writes
    python3 -I tools/scripts/build_input_glyphs.py --preview a.png  a labelled picture of every prompt, nothing else

Writes assets/runtime/icons/input_glyphs.png with the pack's licence beside it, and
src/input/glyphs/glyph_cells.inc (the order of the prompts there is the order of sh::input::Glyph). Needs Pillow.

The sheet is 16 cells wide and 32 tall, 16 px a cell: the left 8 columns hold every prompt in its light form, the
right 8 the same prompts in the dark form, at the same row and 8 columns over. A prompt takes one cell or two.
"""
import io
import os
import sys

from PIL import Image, ImageDraw

SCRIPTS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(SCRIPTS))
PACK = os.path.join(SCRIPTS, "kenney_input_prompts_pixel")
SHEET_PATH = os.path.join(REPO, "assets", "runtime", "icons", "input_glyphs.png")
LICENCE_PATH = os.path.join(REPO, "assets", "runtime", "icons", "License_Kenney.txt")
CELLS_PATH = os.path.join(REPO, "src", "input", "glyphs", "glyph_cells.inc")

CELL = 16
HALF = 8  # columns of one form
ROWS = 32


class Prompt:
    """One prompt: the tiles of the pack laid over each other for the light form and for the dark one."""

    def __init__(self, name, light, dark, cells=1):
        self.name = name
        self.light = light
        self.dark = dark
        self.cells = cells
        self.col = self.row = 0


def prompts():
    out = []

    def one(name, light, dark, cells=1):
        out.append(Prompt(name, light, dark, cells))

    # Keyboard caps: the dark keyboard is the light one 8 rows down.
    def cap(name, col, row, cells=1):
        one("Key" + name, [(col, row)], [(col, row + 8)], cells)

    # Everything else: the dark tile is right under the light one unless said otherwise.
    def tile(name, col, row, dark_row=None):
        one(name, [(col, row)], [(col, row + 1 if dark_row is None else dark_row)])

    def run(names, col, row, put=cap):
        for i, n in enumerate(names):
            put(n, col + i, row)

    run("123456789", 17, 1)
    cap("0", 26, 1)
    run("QWERTYUIOP", 17, 2)
    run("ASDFGHJKL", 18, 3)
    run("ZXCVBNM", 19, 4)
    run(["F%d" % n for n in range(1, 13)], 18, 0)
    cap("Escape", 17, 0)
    cap("Space", 17, 4)
    run(["Up", "Right", "Down", "Left"], 30, 4)
    run(["Minus", "Plus", "Equals"], 27, 1)
    run(["LeftBracket", "RightBracket"], 27, 2)
    cap("Backslash", 31, 2)
    cap("Apostrophe", 27, 3)
    run(["Semicolon", "Asterisk"], 30, 3)
    cap("Grave", 30, 0)
    cap("Comma", 27, 6)
    cap("Period", 27, 5)
    cap("Slash", 29, 4)
    # Two cells wide, three rows of five.
    for row, names in ((5, ["Alt", "Tab", "Delete", "End", "NumLock"]),
                       (6, ["Ctrl", "CapsLock", "Home", "PageUp", "PageDown"]),
                       (7, ["Shift", "Insert", "PrintScreen", "ScrollLock", "Pause"])):
        for i, n in enumerate(names):
            cap(n, 17 + 2 * i, row, 2)
    cap("Backspace", 32, 1, 2)
    one("KeyEnter", enter_key, enter_key, 2)

    run(["Mouse", "MouseLeft", "MouseRight", "MouseMiddle", "MouseWheelUp", "MouseWheelDown"], 8, 2, tile)

    tile("PadEmpty", 12, 0, 1)
    four = ["Up", "Right", "Down", "Left"]
    run(["Dpad" + d for d in four], 1, 1, tile)
    run(["ClassicDpad" + d for d in four], 1, 7, tile)
    run(["Stick" + d for d in four], 9, 12, tile)
    tile("StickLeftPress", 8, 12)
    tile("StickRightPress", 8, 14)

    # The PlayStation shapes are a button tile with the shape tile over it.
    for i, n in enumerate(["Triangle", "Circle", "Square", "Cross"]):
        one("Ps" + n, [(17 + 2 * i, 16), (18 + 2 * i, 16)], [(17 + 2 * i, 17), (18 + 2 * i, 17)])
    tile("PsL1", 23, 18)
    tile("PsR1", 24, 18)
    tile("PsL2", 17, 18)
    tile("PsR2", 18, 18)
    tile("PsSelect", 18, 20)
    tile("PsStart", 19, 20)
    tile("PsShare", 6, 18)
    tile("PsOptions", 5, 18)  # the pack's one three-lines tile, also Xbox Menu

    # Letter buttons: the light and the dark form are different tiles per family.
    def letters(family, light_col, light_row, dark_col, dark_row):
        for i, n in enumerate("ABXY"):
            one(family + n, [(light_col + i, light_row)], [(dark_col + i, dark_row)])

    letters("Xbox", 8, 0, 13, 1)
    tile("XboxLB", 9, 16, 17)
    tile("XboxRB", 10, 16, 17)
    tile("XboxLT", 7, 16, 17)
    tile("XboxRT", 8, 16, 17)
    tile("XboxView", 4, 18)
    tile("XboxMenu", 5, 18)

    letters("Nintendo", 13, 0, 8, 1)
    tile("NintendoL", 15, 18)
    tile("NintendoR", 16, 18)
    tile("NintendoZL", 13, 18)
    tile("NintendoZR", 14, 18)
    tile("NintendoMinus", 4, 20)
    tile("NintendoPlus", 5, 20)

    letters("Steam", 13, 1, 8, 1)
    tile("SteamL1", 19, 18)
    tile("SteamR1", 20, 18)
    tile("SteamL2", 21, 18)
    tile("SteamR2", 22, 18)
    tile("SteamView", 4, 18)  # the pack's one two-squares tile, also Xbox View
    tile("SteamMenu", 20, 22)
    return out


def cut(pack, col, row, cells):
    return pack.crop((col * CELL, row * CELL, (col + cells) * CELL, (row + 1) * CELL))


def enter_key(pack, dark):
    """The pack's two-cell tile at (32, 3), with the rows of the Backspace tile above its face (transparent or the
    outline colour at x = 3) copied over the top, so both wide keys have the same upper edge."""
    down = HALF if dark else 0
    blank = cut(pack, 32, 3 + down, 2).copy()
    backspace = cut(pack, 32, 1 + down, 2)
    edge = backspace.getpixel((3, 1))
    face = 0
    while backspace.getpixel((3, face)) in ((0, 0, 0, 0), edge):
        face += 1
    blank.paste(backspace.crop((0, 0, 2 * CELL, face)), (0, 0))
    return blank


def picture(pack, prompt, dark):
    source = prompt.dark if dark else prompt.light
    if callable(source):
        return source(pack, dark)
    img = Image.new("RGBA", (prompt.cells * CELL, CELL), (0, 0, 0, 0))
    for col, row in source:
        img.alpha_composite(cut(pack, col, row, prompt.cells))
    return img


def place(all_prompts):
    """Left to right, top to bottom; a prompt that does not fit the rest of its row starts the next one."""
    col = row = 0
    for p in all_prompts:
        if col + p.cells > HALF:
            col, row = 0, row + 1
        p.col, p.row = col, row
        col += p.cells
        if col == HALF:
            col, row = 0, row + 1


def build():
    pack = Image.open(os.path.join(PACK, "tilemap_packed.png")).convert("RGBA")
    all_prompts = prompts()
    place(all_prompts)
    names = [p.name for p in all_prompts]
    if len(set(names)) != len(names):
        sys.exit("a prompt is listed twice")
    if all_prompts[-1].row >= ROWS:
        sys.exit("the sheet is full")
    sheet = Image.new("RGBA", (2 * HALF * CELL, ROWS * CELL), (0, 0, 0, 0))
    for p in all_prompts:
        for dark in (False, True):
            img = picture(pack, p, dark)
            if img.getchannel("A").getbbox() is None:
                sys.exit("%s has an empty %s form" % (p.name, "dark" if dark else "light"))
            sheet.paste(img, ((p.col + (HALF if dark else 0)) * CELL, p.row * CELL))
    return all_prompts, sheet


def cells_text(all_prompts):
    lines = ["// Generated by tools/scripts/build_input_glyphs.py. Do not edit.",
             "// SH_INPUT_GLYPH(name, column, row, cells): where the light form of a prompt is on the sheet."]
    lines += ["SH_INPUT_GLYPH(%s, %d, %d, %d)" % (p.name, p.col, p.row, p.cells) for p in all_prompts]
    return "\n".join(lines) + "\n"


def png_bytes(sheet):
    out = io.BytesIO()
    sheet.save(out, format="PNG")
    return out.getvalue()


def read(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return None


def preview(all_prompts, sheet, path):
    zoom, gap, label = 3, 6, 170
    per_row = 4
    entry_w = label + 2 * (2 * CELL * zoom) + 3 * gap
    entry_h = CELL * zoom + gap
    rows = -(-len(all_prompts) // per_row)
    img = Image.new("RGBA", (per_row * entry_w, rows * entry_h + gap), (70, 80, 100, 255))
    pen = ImageDraw.Draw(img)
    for i, p in enumerate(all_prompts):
        x, y = (i % per_row) * entry_w, (i // per_row) * entry_h + gap
        pen.text((x + gap, y + 16), p.name, fill="white")
        for form in (0, 1):
            left = (p.col + form * HALF) * CELL
            c = sheet.crop((left, p.row * CELL, left + p.cells * CELL, (p.row + 1) * CELL))
            c = c.resize((p.cells * CELL * zoom, CELL * zoom), Image.NEAREST)
            img.alpha_composite(c, (x + label + form * (2 * CELL * zoom + gap), y))
    img.convert("RGB").save(path)


def main(argv):
    all_prompts, sheet = build()
    if argv[1:2] == ["--preview"] and len(argv) == 3:
        preview(all_prompts, sheet, argv[2])
        return 0
    licence = read(os.path.join(PACK, "License.txt"))
    wanted = {CELLS_PATH: cells_text(all_prompts).encode(), LICENCE_PATH: licence}
    # The sheet is compared by its pixels: PNG bytes depend on the Pillow that wrote them, so a sheet that already
    # shows the same picture is left as it is.
    try:
        same_sheet = Image.open(SHEET_PATH).convert("RGBA").tobytes() == sheet.tobytes()
    except OSError:
        same_sheet = False
    if argv[1:] == ["--check"]:
        stale = [p for p, data in wanted.items() if read(p) != data]
        if not same_sheet:
            stale.append(SHEET_PATH)
        for p in stale:
            print("stale:", os.path.relpath(p, REPO))
        return 1 if stale else 0
    if argv[1:]:
        sys.exit("usage: build_input_glyphs.py [--check | --preview <out.png>]")
    if not same_sheet:
        wanted[SHEET_PATH] = png_bytes(sheet)
    for path, data in wanted.items():
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
