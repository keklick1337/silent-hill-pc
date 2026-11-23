#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Vladislav Tislenko (keklick1337)
"""Keeps gamedata/tools/lua_api.json (the Lua editor's descriptions of the script API) in step with the engine.

The list of what exists comes from the running game, not from this script: the console command
`luaedit names <file>` writes the names of the live Lua states ("engine function sh.npc.spawn" per line). The
names are collected by the Lua editor panel, so it has to be open: with modding_tools = 1 in config.cfg, in a
game, `editor on`, `editorpanel tools.lua_editor on`, then `luaedit names <file>` (a -consolecmd file does it
unattended).

  lua_api_docs.py check <names file>   every described name must exist; prints the coverage per group
  lua_api_docs.py seed <names file>    adds an entry for every function that has none: the signature as
                                       the modding guide writes it when it does, else "name(...)";
                                       descriptions are written by hand afterwards and are never touched
  lua_api_docs.py markdown [--check]   writes docs/modding/lua-api.md, the reference of the modding guide, from
                                       the file (needs no game); --check only says whether it is up to date
"""

import json
import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "gamedata" / "tools" / "lua_api.json"
GUIDE = ROOT / "docs" / "modding"
REFERENCE = ROOT / "docs" / "modding" / "lua-api.md"

# What a table of the script API is about, for the reference (a table without a line here gets none).
GROUPS = {
    "sh": "Flat functions: the vocabulary the base game's map event scripts are written in. Many have a named twin "
          "in a table below; new scripts should prefer the named form.",
    "sh.audio": "Sound effects and the music track (the named twins of `sh.sfx`, `sh.sfx_stop`, `sh.bgm`).",
    "sh.camera": "The event camera and the camera's own state.",
    "sh.chara": "Character types of the loaded map: spawning, their loads and behaviours.",
    "sh.config": "A mod's own settings ([scripting.md](scripting.md#mod-settings-shconfig)).",
    "sh.console": "Console commands of a mod ([scripting.md](scripting.md#console-commands-shconsole)).",
    "sh.cutscene": "Cutscenes: the letterbox, the tracks of a cutscene file, the cutscene clock.",
    "sh.effects": "Effect pools and glow fields of the maps.",
    "sh.engine": "The engine version ([versioning.md](versioning.md)).",
    "sh.entity": "Characters as the behaviour scripts of `gamedata/chara/` see them: spawning, bones, clips, rays, "
                 "and the helpers of the base game's own characters.",
    "sh.event": "The step machine of a map event and its per-tick steppers.",
    "sh.flag": "Event flags by name ([data.md](data.md#flags)).",
    "sh.game": "The state of the game: screens, difficulty, timing.",
    "sh.hooks": "The event bus ([events.md](events.md)).",
    "sh.input": "Controls as the game reads them.",
    "sh.item": "The inventory.",
    "sh.item_view": "Close-ups built from the models of the item screen.",
    "sh.light": "The flashlight and its light.",
    "sh.load": "The file queue: reading files of the game into buffers.",
    "sh.map": "Map variables and the effects single maps start.",
    "sh.math": "The game's integer math, bit-exact.",
    "sh.menu": "Menus ([scripting.md](scripting.md#menus-and-option-pages)); only in menu scripts.",
    "sh.mods": "Other mods ([scripting.md](scripting.md#talking-to-other-mods)).",
    "sh.msg": "Map messages and selections.",
    "sh.music": "The music mixer.",
    "sh.npc": "Character slots: 0 the player, 1 and up the other characters.",
    "sh.player": "Harry.",
    "sh.render": "Screen effects and panels an event script draws.",
    "sh.rng": "The game's random number generator.",
    "sh.save": "Values kept in the save.",
    "sh.screen": "Full-screen pictures, fades, the staff roll.",
    "sh.sound": "Sounds by registry name: playing, positions, the sound driver.",
    "sh.timer": "The game's timers.",
    "sh.world": "The loaded world: ground, rays, walkable points, pickups, effect slots.",
}

# Described names the base script states do not have: they exist only in the environment of a mod's layer.
LAYER_ONLY = {"extend"}
# Described names of the menu scripts' Lua state (src/engine/script/bind_menu.cpp, bind_mods.cpp), which
# `luaedit names` does not list: it writes the names of the game and map states.
MENU_STATE = ("sh.menu.", "sh.mods.")

HEADER = (
    "What the modding tools' Lua editor shows about the script API: "
    "\"functions\" maps a name the scripts can call to its signature (arguments in call order, [optional], "
    "-> results) and a one-line description. Units: positions and lengths are Q12 (4096 = 1 m), angles Q12 "
    "(4096 = a full turn), times Q12 seconds, unless the description says otherwise. A character is its slot "
    "number (0 the player). The names are checked against the running game with tools/scripts/lua_api_docs.py; "
    "a mod describes the API it adds in its own tools/lua_api.json."
)


def live_functions(path):
    names = []
    for line in Path(path).read_text().splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] == "function" and parts[0] in ("engine", "script", "library"):
            names.append((parts[2], parts[0]))
    return names


def load():
    if not DOCS.exists():
        return {"description": HEADER, "functions": {}}
    return json.loads(DOCS.read_text())


def save(data):
    data["description"] = HEADER
    data["functions"] = dict(sorted(data["functions"].items()))
    DOCS.parent.mkdir(parents=True, exist_ok=True)
    DOCS.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n")


def documented_signatures():
    """name -> "name(args)" for every call form the modding guide shows in backticks."""
    found = {}
    call = re.compile(r"([A-Za-z_][\w.]*)\(([^()`]*)\)")
    lines = [l for p in sorted(GUIDE.glob("*.md")) if p.name != "lua-api.md" for l in p.read_text().splitlines()]
    for line in lines:
        group = None
        row = re.match(r"\|\s*`(sh\.[a-z_0-9]+)`\s*\|", line)
        if row:
            group = row.group(1)
        for span in re.findall(r"`([^`]+)`", line):
            for name, args in call.findall(span):
                if name.startswith("sh."):
                    full = name
                elif group and "." not in name:
                    full = group + "." + name
                else:
                    continue
                found.setdefault(full, "%s(%s)" % (full, args))
    return found


def seed(names_file):
    data = load()
    known = documented_signatures()
    added = 0
    for name, origin in live_functions(names_file):
        if origin != "engine" or name in data["functions"]:
            continue
        data["functions"][name] = {"signature": known.get(name, name + "(...)"), "description": ""}
        added += 1
    save(data)
    print("added %d entries (%d in the file)" % (added, len(data["functions"])))


def check(names_file):
    data = load()
    live = dict(live_functions(names_file))
    stale = sorted(n for n in data["functions"]
                   if n not in live and n not in LAYER_ONLY and not n.startswith(MENU_STATE))
    groups, described = Counter(), Counter()
    for name, origin in live.items():
        if origin != "engine":
            continue
        parts = name.split(".")
        group = parts[1] if len(parts) > 2 else "(flat)"
        groups[group] += 1
        entry = data["functions"].get(name)
        if entry and entry.get("description"):
            described[group] += 1
    for group in sorted(groups):
        print("%-16s %4d of %4d described" % (group, described[group], groups[group]))
    total = sum(groups.values())
    print("engine functions: %d, described: %d, with a signature only: %d" % (
        total, sum(described.values()),
        sum(1 for n, o in live.items() if o == "engine" and n in data["functions"]) - sum(described.values())))
    others = sum(1 for n in data["functions"] if live.get(n) in ("script", "library"))
    print("helpers and Lua library entries: %d" % others)
    for name in stale:
        print("NOT IN THE GAME: " + name)
    return 1 if stale else 0


def cell(text):
    return text.replace("|", "\\|")


def reference_text():
    functions = load()["functions"]
    groups = {}
    for name in sorted(functions):
        group = name.rsplit(".", 1)[0] if "." in name else "(globals)"
        groups.setdefault(group, []).append(name)
    described = sum(1 for e in functions.values() if e.get("description"))
    out = [
        "# Lua API reference",
        "",
        "<!-- Generated by tools/scripts/lua_api_docs.py markdown from gamedata/tools/lua_api.json. Do not edit: "
        "edit the JSON file and run the script. -->",
        "",
        "Every function a script can call, by table. The same text is what the in-game Lua editor shows under the "
        "cursor ([tools.md](tools.md#lua-editor)); it comes from `gamedata/tools/lua_api.json`, and a mod that adds "
        "functions describes them in its own `<mod>/tools/lua_api.json`. How scripts are loaded and what they may do is "
        "in [scripting.md](scripting.md).",
        "",
        "**Units.** Positions and lengths are Q12 integers (4096 = 1 m, Y grows downwards), angles Q12 (4096 = a "
        "full turn), times Q12 seconds, health Q12 (409600 = 100), unless a line says otherwise. A character is "
        "its slot number: 0 the player, 1 and up the others. Items, flags, characters, sounds and messages are "
        "given by name.",
        "",
        "**Coverage.** %d functions are listed, %d of them described. The rest are listed by signature under "
        "\"Not described yet\": they exist and can be called, but nobody has written down what they do; nearly "
        "all of them are helpers of the base game's own map and character scripts, and reading those scripts "
        "(`gamedata/maps/*/events.lua`, `gamedata/chara/*/behavior.lua`) is the way to learn them. `...` in a "
        "signature means the arguments have not been written down either."
        % (len(functions), described),
        "",
        "## Contents",
        "",
    ]
    order = sorted(groups, key=lambda g: (g != "(globals)", g))
    for group in order:
        anchor = re.sub(r"[^a-z0-9_ -]", "", group.lower()).replace(" ", "-")
        done = sum(1 for n in groups[group] if functions[n].get("description"))
        out.append("- [`%s`](#%s) — %d of %d described" % (group, anchor, done, len(groups[group])))
    for group in order:
        out += ["", "## %s" % group, ""]
        if group == "(globals)":
            out += ["Helpers the script hosts define beside the `sh` table, and the Lua library names that exist "
                    "in the sandbox.", ""]
        elif group in GROUPS:
            out += [GROUPS[group], ""]
        with_text = [n for n in groups[group] if functions[n].get("description")]
        without = [n for n in groups[group] if not functions[n].get("description")]
        if with_text:
            out += ["| Function | What it does |", "| --- | --- |"]
            for name in with_text:
                entry = functions[name]
                out.append("| `%s` | %s |" % (cell(entry.get("signature") or name), cell(entry["description"])))
        if without:
            if with_text:
                out.append("")
            out.append("Not described yet: " + ", ".join(
                "`%s`" % (functions[n].get("signature") or n) for n in without) + ".")
    return "\n".join(out) + "\n"


def markdown(check_only):
    text = reference_text()
    if check_only:
        if not REFERENCE.exists() or REFERENCE.read_text(encoding="utf-8") != text:
            print("%s is not what lua_api_docs.py markdown writes" % REFERENCE.relative_to(ROOT))
            return 1
        print("%s is up to date" % REFERENCE.relative_to(ROOT))
        return 0
    REFERENCE.parent.mkdir(parents=True, exist_ok=True)
    REFERENCE.write_text(text, encoding="utf-8")
    print("wrote %s" % REFERENCE.relative_to(ROOT))
    return 0


def main(argv):
    if len(argv) >= 2 and argv[1] == "markdown" and argv[2:] in ([], ["--check"]):
        return markdown(len(argv) == 3)
    if len(argv) == 3 and argv[1] == "seed":
        seed(argv[2])
        return 0
    if len(argv) == 3 and argv[1] == "check":
        return check(argv[2])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
