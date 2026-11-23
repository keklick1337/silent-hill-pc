"""The game's own English for the tools.

The English of the game's messages and items is in the repository: gamedata/text/en/common.json, items.json and
maps/*.json. Those files are what a translation of the game's text has to cover.

  english_keys()   every key of the game's English -> the gamedata/text/<lang>/ file it belongs to
  english()        key -> (file, readable text, ws record), in file order
"""

from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TEXT_EN = ROOT / "gamedata" / "text" / "en"


def _game_files() -> list[Path]:
    return [TEXT_EN / "common.json", TEXT_EN / "items.json"] + sorted((TEXT_EN / "maps").glob("*.json"))


def english() -> dict[str, tuple[Path, str, str]]:
    out: dict[str, tuple[Path, str, str]] = {}
    for path in _game_files():
        doc = json.loads(path.read_text(encoding="utf-8"))
        rel = path.relative_to(TEXT_EN)
        ws = doc.get("ws", {})
        for key, text in doc.get("strings", {}).items():
            out.setdefault(key, (rel, text, ws.get(key, "")))
    return out


def english_keys() -> dict[str, Path]:
    return {key: rel for key, (rel, _text, _ws) in english().items()}
