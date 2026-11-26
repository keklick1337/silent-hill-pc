# Translating the game

[Back to the guide](README.md)

There are no languages in the engine's code: a language is a folder of text with a descriptor. The game ships thirteen (`ar de en es fr it ja ko pl pt ru tr zh` under [`gamedata/text/`](../../gamedata/text/)); a mod can add one, complete one or change any line. The full translator's handbook — markup, the limits of the message window, glossaries, notes per language — is the [translation handbook](translation-handbook.md); the text file format and markup are in [assets.md](assets.md#text).

## What the text consists of

| Files of a language | What they are |
| --- | --- |
| `common.json` | the shared block of messages (keys `common.*`) |
| `maps/<map>.json` | the messages of a map (43 maps, keys `<map>.*`) |
| `messages.json` | messages of no map, which a script shows on any map by key (a mod's story lines; the game has none) |
| `items.json` | names and descriptions of items (`item.<Name>.name`, `item.<Name>.desc`) |
| `menu.json`, `ui.json`, `options_menu.json`, `pause_menu.json`, `mods_menu.json`, `debug_menu.json`, `menu_engine.json`, `controls_*.json`, `setup.json` | the interface |
| `lang.json` | the descriptor that makes the folder a language |

The game's own text has 1515 keys: 1365 messages, 75 item names and 75 descriptions.

**The game's English is in the repository**, in `gamedata/text/en/` (`common.json`, `items.json`, `maps/*.json`), and the tools below read it from there. `<G>` stands for an imported `gamedata` folder beside the game: `build_lang.py` and `check_translation.py` measure lines with the game font it holds (`convert/fonts/game.font.json`). Templates and PO files are work files: keep them under `.tmp/`.

## A translation as a mod

```text
mods/my_hebrew/
  mod.json
  text/he/lang.json          the descriptor
  text/he/menu.json          the translation; files and keys as in gamedata/text/en
  text/he/ui.json, options_menu.json, items.json, common.json, maps/map0_s00.json, ...
  font/MyHebrew-Bold.ttf     optional: a font for the language's letters
```

`text/he/lang.json`:

```json
{
  "format": "sh-text-lang",
  "version": 1,
  "code": "he",
  "name": "עברית",
  "english_name": "Hebrew",
  "direction": "rtl",
  "fonts": ["Noto Sans Hebrew", "Arial"]
}
```

- `code` is the folder's name; `config.cfg` stores it (`language = he`). `name` is the language's name in itself, shown in *Options > Language*. `direction` is `ltr` or `rtl`. `fonts` is an optional list of font families for letters the game font lacks; without it the game finds a system font that has them. `glyph_cell_height` (optional, 16..32, default 16) is the height in game pixels of the cells those letters are drawn into, and the spacing of the lines of text: a script whose letters reach far above and below the line (Arabic uses 21) is then drawn larger instead of being shrunk into the 16 rows of the game font. Letters of the game font keep their size and baseline. `menu_order` (optional, default 0) places the language in *Options > Language*: English first, then lower values, then by code.
- A translation may be partial: what is missing is shown in English.
- Write text in logical order, as it is read, without presentation forms of Arabic letters and without direction marks: shaping, drawing order, mirrored brackets and the wrapping of long message lines are the engine's work.
- In an `rtl` language the lines of the message window are aligned to the right and the engine's own menus are mirrored.
- To change lines of an existing language a mod needs no descriptor: a file `text/<code>/<any name>.json` with the keys it changes is enough ([assets.md](assets.md#text)).

Check in the game: the console command `langcheck` prints the languages found and which font draws their letters.

## The workflow

The tools are in the repository; run them from its root.

```sh
# 1. a template: every key with its English text
python3 -I tools/translation/build_lang.py xx .tmp/tr/en_template.txt --gamedata <G> --template
#    or the same as a gettext PO template, for a PO editor
python3 -I tools/scripts/text_po.py export pot -o .tmp/tr/SilentHill.pot

# 2. translate a copy of the template, then build the language's files
python3 -I tools/translation/build_lang.py <lang> .tmp/tr/<lang>.txt --gamedata <G>
#    or, from a PO file
python3 -I tools/scripts/text_po.py import my.po <lang> --name "..." --english-name "..." --direction ltr

# 3. check the translation: 0 problems
python3 -I tools/scripts/check_translation.py <lang> --gamedata <G>

# 4. check the format of all text files
python3 -I tools/scripts/gen_text.py --check
```

In the draft of `build_lang.py` you write only words, one line per key. The codes at the start and end of a line (waits, positions, colours, choices) are taken from the English line by the tool; a coloured word inside a sentence is written as `<...>`; `|` is a line break you choose, and a line without one is wrapped automatically.

`check_translation.py` verifies that every English key is there and in its file, that the codes match the English ones in kind, arguments and order, that the lines fit the message window, that no English is left, and that every character has a glyph in the game font.

By default the tools write into `gamedata/text/<lang>/` of the repository (`--out` of `build_lang.py` and of `text_po.py import` names another folder, for example your mod's `text/`).

## Things that must survive a translation

- **Markup.** `{wait 2.0}`, `{color 3}…{/color}`, `{choice 4}`, `{end}` and the other codes keep their kind, their arguments and their order.
- **The message window does not wrap by itself.** Line breaks are part of the text, and a message has a limit of lines and of width in pixels; the build tool wraps for you and the check enforces the limits.
- **Puzzles.** Some texts carry the solution of a puzzle — poems, numbers, the order of things. The handbook lists them and what has to stay.
- **Glyphs.** A character the game font does not have is drawn from a font file; to draw it in the game's own style add a glyph sheet ([assets.md](assets.md#fonts)).
