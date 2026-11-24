# Contributing to Silent Hill PC

Thank you for wanting to help. Contributions of every size are welcome: a fixed typo, a bug report with a log, a test on a GPU or a Windows machine we do not have, a translation, a mod, or a change of the engine. This page says how to start and which rules keep the project what it is.

## Getting started

1. **Build it.** Linux and macOS build natively; Windows and Android are cross-compiled on Linux. Everything is in [docs/BUILDING.md](docs/BUILDING.md).
2. **Run it** with your own disc image of Silent Hill (NTSC USA v1.1, `SLUS-007.07`): the first start imports it ([README.md](README.md#first-start)). Without an image you can still build, run the unit tests and the static checks.
3. **Read** [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (layers, modules, where each subsystem lives) and, for graphics, [docs/RENDER.md](docs/RENDER.md). Mods and data: [docs/modding/](docs/modding/README.md).
4. **Pick something.** Look at the [issues](https://github.com/keklick1337/silent-hill-pc/issues); a difference from the original game, a crash with its log, or a missing translation are all good first contributions. For a larger change, open an issue first so we can agree on the approach.

## The rules

These are few, but firm. They exist so the project can stay public and so the game keeps playing like the original.

- **No game files in the repository.** Nothing extracted or converted from a disc image is committed: no models, textures, sounds, movies, fonts, saves or imported `gamedata/` folders. The exceptions, by the maintainer's decision, are the English game text in `gamedata/text/en/` and the screenshots in `docs/images/` ([COPYRIGHT.md](COPYRIGHT.md), section 3). Check `git status` before committing.
- **The decompilation is a specification, never a source.** Behaviour is taken from the community decompilation [silent-hill-decomp](https://github.com/shdecompilations/silent-hill-decomp), read as a description of what the game does. Copying its code into the repository — even temporarily, even renamed — is not allowed, and neither is PlayStation emulation or the console's data layout inside `src/`. Keep a checkout outside the tracked tree (`references/` is ignored). `tools/scripts/check_naming.py --strict` must report zero.
- **The game plays like the original.** Controls, camera, story, enemies, items and puzzles follow the original "about 1:1". A change of story, balance or rules is only ever an option marked `GAMEPLAY CHANGE` in `assets/config.cfg`, off by default. Graphics may improve, but the fog, the darkness and the flashlight stay.
- **No accidental changes of behaviour.** The replay harness compares the game state tick by tick with recorded references. If your change alters behaviour on purpose, explain the first divergence and update the references in the same commit ([docs/TESTING.md](docs/TESTING.md)).
- **The game is data.** Rules of Silent Hill belong in `gamedata/` (JSON and Lua), not in engine code. The engine does not branch on one particular character, item, weapon or map: such a property is a key in the data.
- **Readable, namespaced names.** Every thing in `gamedata/` has a name like `silenthill:HealthDrink` (a mod's: `<mod id>:Name`); the engine numbers things at run time and stores no numbers. No `field_XX`, `func_800XXXXX` or `D_800XXXXX` names in data or in the Lua API: a name comes from the decompilation when it has one, otherwise from what the thing does.
- **Fixed point rounds like the original.** `Q12(x)` is `(s32)(x * 4096.0f)`, truncated towards zero (in Lua: `math.floor` for a positive and `math.ceil` for a negative value); `Q12_ANGLE(deg)` is `Q12(deg / 360.0f)`, truncated the same way. Verify fractional constants in C with `volatile float`, not in Python (which computes in double). The original compiler evaluated call arguments right to left: where one expression draws several random numbers, take their order from the disassembly.

## Code

- C++20 in the engine's own classes; errors cross module borders as `sh::Result`. Follow the layers of [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
- Style: `.clang-format` (Google style, 4 spaces, 120 columns). Format the lines you change, not whole files.
- Comments are in English and say only the non-obvious "why". The same holds for Lua, generators and the `description` keys of JSON data.
- Dependencies are vendored in `third_party/` or fetched by the dependency scripts, pinned by tag and commit, with their license recorded in [COPYRIGHT.md](COPYRIGHT.md). Nothing is installed into the system.
- Markdown is not wrapped by hand: a paragraph or a list item is one line.

## Checks

Before you open a pull request, run the checks of [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md), section 4. Without a disc image that is the build, the unit tests and the static checks; `tools/ci/gate.sh -b build` runs them. With an imported game, also the replay check, the map sweep and the map scenarios (`tools/ci/gate.sh -b build -i <image.cue>`). A change of CMake or of platform code also needs the Windows cross-build, and the *Build macOS* and *Build Android* workflows must pass.

## Commits and pull requests

- One logical change per commit, with a message in English that says what changed and why.
- Add files by explicit path; never commit build trees, disc images, imported data, saves or work files (put those in `.tmp/`, which is ignored).
- In the pull request, describe what you changed, how you checked it (which checks, which maps or scenes you played), and anything that changed on purpose, such as updated replay references or a new `GAMEPLAY CHANGE` option.

## License

The project is licensed under the GNU General Public License, version 3 or later. By contributing you agree that your contribution is licensed under GPL-3.0-or-later ([COPYRIGHT.md](COPYRIGHT.md), section 5).
