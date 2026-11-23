#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes the chara/<Name>/character.json patches that point characters of the game at the glbs rig_transfer.py made.

A patch is one key: {"model": "models/<folder>/<FILE>.glb"}. The glbs can sit in another mod (the one that only
carries the model library): the engine looks every converted file up in all active mods.

  python3 -I make_character_patches.py --chara <repo>/gamedata/chara --glbs <library mod>/convert/models/<folder> \
      --out <mod>/chara --categories CommonEnemies OtherworldEnemies Bosses
  python3 -I make_character_patches.py ... --names Harry
"""

import argparse
import json
import os
import sys


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--chara", required=True, help="the game's gamedata/chara folder")
    p.add_argument("--glbs", required=True, help="folder with the new glbs (<mod>/convert/models/<folder>)")
    p.add_argument("--out", required=True, help="the chara folder of the mod to write")
    p.add_argument("--categories", nargs="*", default=[], help="character.json categories to take")
    p.add_argument("--names", nargs="*", default=[], help="character folders to take")
    args = p.parse_args()

    glbs = os.path.abspath(args.glbs)
    parts = glbs.replace("\\", "/").split("/")
    if "convert" not in parts:
        sys.exit("--glbs must be below a convert/ folder: the model path of character.json starts there")
    below_convert = "/".join(parts[len(parts) - parts[::-1].index("convert"):])

    written, missing = [], []
    for name in sorted(os.listdir(args.chara)):
        file = os.path.join(args.chara, name, "character.json")
        if not os.path.isfile(file):
            continue
        with open(file, "r", encoding="utf-8") as f:
            data = json.load(f)
        if name not in args.names and data.get("category") not in args.categories:
            continue
        model = data.get("model") or ""
        stem = os.path.splitext(os.path.basename(model))[0]
        if not stem:
            continue
        if not os.path.isfile(os.path.join(glbs, stem + ".glb")):
            missing.append(f"{name} ({stem})")
            continue
        folder = os.path.join(args.out, name)
        os.makedirs(folder, exist_ok=True)
        with open(os.path.join(folder, "character.json"), "w", encoding="utf-8") as f:
            json.dump({"model": f"{below_convert}/{stem}.glb"}, f, indent=2)
            f.write("\n")
        written.append(f"{name} -> {below_convert}/{stem}.glb")
    print("\n".join(written))
    print(f"{len(written)} patches written to {args.out}")
    if missing:
        print("no glb for: " + ", ".join(missing))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
