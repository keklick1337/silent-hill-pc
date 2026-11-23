# Silent Hill PC — guide for contributors and coding agents

Silent Hill PC is a C++20 engine that plays Silent Hill (PlayStation, NTSC USA v1.1, SLUS-007.07) from the player's own disc image. The engine is general; the game itself — maps, events, characters, items, weapons — is data and Lua in `gamedata/`. The first start imports models, textures, sound, video and the font from the disc image into `gamedata/` beside the executable. The engine and its base data, the English game text included (`gamedata/text/en/`), are published; the other game files are not.

Read first: [README.md](README.md), then [docs/BUILDING.md](docs/BUILDING.md) (build) and [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) (run, the checks), then [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (section 8: every subsystem in short; graphics: [docs/RENDER.md](docs/RENDER.md)). Mods: [docs/modding/](docs/modding/README.md). The rules below are the same as in [CONTRIBUTING.md](CONTRIBUTING.md), which explains them for human contributors; keep the two consistent.

## Hard rules

- **No game files in the repository.** Nothing extracted or converted from a disc image is committed: no models, textures, sound, video or fonts. The exceptions, by the owner's decision, are the English text of the game (messages, item names and descriptions) in `gamedata/text/en/`, edited there like the other languages and not imported, and the README screenshots in `docs/images/`. Otherwise `gamedata/` holds the project's own description of the game. The `.gitignore` covers the usual places; check `git status` before committing.
- **The decompilation is a specification, not a source.** Behaviour is taken from the community decompilation [silent-hill-decomp](https://github.com/shdecompilations/silent-hill-decomp), read as a description. Copying its code into the repository — even "temporarily", even renamed — is forbidden, and so is any PlayStation emulation or the console's data layout inside `src/`. A local checkout lives outside the tracked tree (`references/` is ignored); it is read, never built from and never shipped. `python3 -I tools/scripts/check_naming.py --strict` must stay at zero.
- **The game plays like the original.** Controls, camera, story, enemies, items and puzzles follow the original "about 1:1"; frame-exact console timing (disc speed, loading frames) is not a goal. A change of story, balance or rules exists only as an option marked `GAMEPLAY CHANGE` in `assets/config.cfg`, off by default. Loading is instant by default; graphics improve through presets and keep the fog, the darkness and the flashlight.
- **A change must not alter behaviour by accident.** Run the checks of [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md), section 4. If the replay references change, explain the first divergence and update them in the same commit.

## Code

- C++20, the engine's own classes; errors cross module borders as `sh::Result`. Follow the style of the code around you (`.clang-format`: Google style, 4 spaces, 120 columns; format the lines you change, not whole files).
- Comments say only the non-obvious "why", and only in English — in code, Lua (`gamedata/**/*.lua`), generators and the `description` keys of JSON data.
- Markdown documents are not wrapped by hand: a paragraph or a list item is one line, the viewer wraps it.
- Layers ([docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)): `features`, `ui` → subsystems (`render`, `audio`, `input`, `i18n`, `media`, `platform`) → `engine` → `formats` → `third_party`. Rules of Silent Hill belong in `gamedata/`; the hosts that connect the engine to the game are `src/engine/*/game`. The engine does not branch on one particular character, item, weapon or map: such a property is a key in the data.
- Dependencies are vendored in `third_party/` and built from source, pinned by tag and commit, with their license recorded in [COPYRIGHT.md](COPYRIGHT.md). Nothing is installed into the system by the build or by a script.

## Names in data

- Every thing in `gamedata/` has a readable, namespaced name, like the string ids of Minecraft: `silenthill:HealthDrink` (the prefix may be left out in the base data), a mod's — `<mod id>:Name`. This holds for items, weapons, characters, sounds, event flags (`gamedata/flags/<area>.json`), messages and handlers. The engine gives out internal numbers while it runs and stores them nowhere; saves hold names.
- A thing is a folder or a file: `chara/<Name>/`, `items/<Name>.json`, `weapons/<Name>.json`. An explanation inside a data file is the key `description`.
- No `field_XX`, `func_800XXXXX`, `D_800XXXXX` or `sharedFunc_*` names in `gamedata/` or in the Lua API. A name comes from the decompilation when it has one, otherwise from what the thing does.

## Fixed point

The game's arithmetic is fixed point, and carried-over behaviour must round the same way.

- `Q12(x)` is `(s32)(x * 4096.0f)`: a float multiplication truncated towards zero. In Lua that is `math.ceil(x * 4096)` for a negative fraction and `math.floor` for a positive one. Verify fractional constants with a C program that uses `volatile float`: Python computes in double (`134.6f` gives 551321, not 551193).
- `Q12_ANGLE(deg)` is `Q12(deg / 360.0f)` truncated the same way, no rounding (120° is 1365, 0.5° is 5).
- The original compiler evaluated call arguments right to left. Where one expression draws several random numbers, take their order from the disassembly, not from the text of the decompilation.

## Commits

- Messages in English, one logical change per commit, explicit paths in `git add`. No `Co-Authored-By` and no tool or AI attribution lines.
- Work files go into `.tmp/` (ignored). Do not commit build trees, disc images, imported data or saves.
- Contributions are accepted under GPL-3.0-or-later ([COPYRIGHT.md](COPYRIGHT.md), section 5).
