# Development

How to build the engine, run it, test it and check that a change broke nothing. The rules a change has to follow are in [AGENTS.md](../AGENTS.md); how the engine is put together is in [ARCHITECTURE.md](ARCHITECTURE.md).

## 1. What is in the repository

| Path | What |
| --- | --- |
| `src/engine/` | the engine core and the game hosts (`src/engine/*/game`) |
| `src/render/`, `src/audio/`, `src/input/`, `src/ui/`, `src/media/`, `src/i18n/`, `src/platform/` | subsystems |
| `src/features/` | modding tools, quick save, free camera, dumps |
| `src/formats/`, `src/import/` | disc and file formats, the importer and its converters |
| `gamedata/` | the game as data: `chara/`, `items/`, `weapons/`, `flags/`, `maps/<map>/`, `config/`, `menu/`, `menus/`, `text/`, `sounds/`, `scripts/` |
| `assets/` | the default `config.cfg`, the icon, the Linux desktop entry, the controller database |
| `examples/mods/` | example mods ([modding guide](modding/README.md)); `tools/ci/package_mods.sh` packs them for a release (`.github/workflows/example-mods.yml`) |
| `tests/` | unit tests (`tests/<module>/*_test.cpp`, one binary `sh_tests`), their recorded tables (`tests/data/`), `tests/replay/`, `tests/smoke/` |
| `tools/` | see section 6 |
| `third_party/` | vendored libraries built from source: Lua, Dear ImGui, miniz, stb, cgltf, bcdec, the console font |

Not in the repository, and never to be committed: the disc image, anything the import writes (`raw/`, `convert/`, `tables/`, `maps/areas/` and the per-map files of the import inside a game data directory), saves, and the decompilation that serves as the behaviour specification.

The game data directory of a running game is `gamedata/` beside the executable. The build copies the repository's `gamedata/` there (`cmake/copy_gamedata.cmake`), and the first start adds the import of the player's disc image to the same directory.

## 2. Build

Linux and the Windows cross-build, the dependencies and the CMake options: [BUILDING.md](BUILDING.md). In short:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## 3. Run

```sh
cd build
./SilentHillPC --image "/path/to/Silent Hill (v1.1).cue"
```

The first start imports the image into `build/gamedata/` and then starts the game; later starts need no image. A disc image in `build/` or `build/data/` is found without `--image`.

| Argument or key | Meaning |
| --- | --- |
| `--quit-after-import` | import if needed and exit: 0 ready, 1 no image, 2 import failed |
| `--reimport` (or `import_overwrite = 1`) | import again |
| `-data <dir>` (or `gamedata_dir`) | another game data directory |
| `-skiptogame` | start on the map named by the config key `map`, past the title |
| `-consolecmd <file>` | run console commands from a file |
| `--dialogs-to-console` | print error dialogs instead of showing them |

Settings are in `config.cfg` beside the executable; the defaults and the explanation of every key are [assets/config.cfg](../assets/config.cfg). The importer is also a tool of its own: `build/tools/sh-import/sh-import <image.cue> --out <dir>`.

Runs without a window and without sound, on the real GPU:

```sh
export SDL_VIDEODRIVER=offscreen ALSOFT_DRIVERS=null SDL_AUDIODRIVER=dummy
```

## 4. Checks

A change is ready when all of these pass. Steps 1 to 3 need no disc image and are what the *Checks* and build workflows of [.github/workflows/](../.github/workflows/) run; steps 4 to 7 need an imported game.

`tools/ci/gate.sh` runs them all in this order and prints one `ok` or `FAIL` line per step, with the logs in its work directory:

```sh
tools/ci/gate.sh -b build -i "/path/to/Silent Hill (v1.1).cue"     # everything; -g 8 games at once, -j 4 build jobs
tools/ci/gate.sh -b build                                          # without a disc image: steps 1 to 3
tools/ci/gate.sh -s                                                # the static checks of step 3 only
```

The same steps by hand:

```sh
B=build
export SDL_VIDEODRIVER=offscreen ALSOFT_DRIVERS=null SDL_AUDIODRIVER=dummy

# 1. clean build: no errors, no new warnings in the files you changed
cmake --build $B

# 2. unit tests: 0 failed (tests that need a disc image are skipped without one)
$B/tests/sh_tests

# 3. static checks: every one reports 0 problems
python3 -I tools/scripts/check_naming.py --strict   # readable names in data and the Lua API
python3 -I tools/scripts/check_map_handlers.py
python3 -I tools/scripts/gen_text.py --check
python3 -I tools/scripts/gen_map_tables.py --check
python3 -I tools/scripts/gen_flags.py --check
python3 -I tools/scripts/gen_paper_maps.py --check
python3 -I tools/scripts/gen_weapon_rig.py --check
python3 -I tools/scripts/build_input_glyphs.py --check
python3 -I tools/scripts/gen_unicode_tables.py --check   # needs the Python whose Unicode data the file names (3.14)
python3 -I tools/scripts/lua_api_docs.py markdown --check
python3 -I tools/scripts/check_docs.py              # links, paths and examples of the documentation

# 4. import once (writes $B/gamedata)
(cd $B && ./SilentHillPC --image "/path/to/Silent Hill (v1.1).cue" --quit-after-import)
python3 -I tools/scripts/check_anim_clips.py $B/gamedata/tables/anim

# 5. replay: every route runs twice, both runs are identical and equal the stored reference
python3 -I tools/replay/sh_replay.py check --build $B --work-dir .tmp/replay

# 6. map sweep: all 43 maps alive, no error lines in their logs (the game data outside the build directory)
cp -a $B/gamedata .tmp/gamedata
tests/smoke/map_sweep.sh -j 8 -d "$PWD/.tmp/gamedata" $B "/path/to/Silent Hill (v1.1).bin" .tmp/sweep

# 7. map scenarios: every scenario equals its reference and repeats
python3 -I tools/replay/handler_diff.py run --build $B -j 8 --repeat
```

Notes:

- The replay check is the main proof that behaviour did not change: a route is recorded input, and the state of the game is compared tick by tick. When a change alters behaviour on purpose, explain the first diverging tick and field (`sh_replay.py compare`, `handler_diff.py compare`) and update the references in the same commit (`update-ref`).
- The base data is copied to `<build>/gamedata` only when the game is relinked. After editing `gamedata/`, copy the changed files there yourself, and delete there what you deleted in the repository.
- `map_sweep.sh` refuses to import into a build directory that already has a `gamedata/`; give it one with `-d`.
- Game runs can go in parallel (`-j 8`); keep them silent and off the screen with the three variables above.
- A change of CMake or of platform code also needs the mingw build ([BUILDING.md](BUILDING.md), section 2); `tools/ci/local_ci.sh` builds both platforms as the workflows do (section 5).
- Work files belong in `.tmp/` (ignored by git).
- `assets/config.cfg` explains every key the game accepts; the unit test `tests/gconfig/config_reference_test.cpp` fails when a key is read by the game and missing there, or set there and not read. A removed key goes into `src/engine/config/retired_keys.cpp`.
- Writing unit tests, the recorded tables of `tests/data/`, the smoke test and the frame-rate independence test: [TESTING.md](TESTING.md). A test holds no second implementation of a rule to compare the engine with: it compares with a recorded table and states the rule in words.

## 5. Versions, packages and releases

- **Engine version**: the `VERSION` file at the repository root (`MAJOR.MINOR.PATCH`) is the only place. CMake reads it, the code gets it from `src/engine/version/engine_version.h`, and mods require it with `min_engine_version`. The build adds the commit count and hash as metadata (`1.0.0+2890.1a2b3c4d5`). When which number grows is the modding guide's [engine version policy](modding/versioning.md).
- **Package**: `tools/ci/package.sh <build> <linux|windows> <version> [<out dir>]` assembles the executable, `sh-import`, `sh-upscale` with `upscale-models/`, on Windows `software_gl/` (Mesa llvmpipe, [design/RENDER.md](RENDER.md) section 2.2c), the libraries, `assets/`, the licenses and `gamedata/` strictly from the files tracked by git. It follows the import tables of every binary to decide which libraries go in and fails when one is missing. `python3 -I tools/ci/check_package.py check <archive|dir> <linux|windows> [--run]` verifies a package: required files, nothing disc-derived, every imported library present, and with `--run` a start from an empty directory.
- **The workflows locally**: `tools/ci/local_ci.sh [linux|windows|checks|all]` runs the jobs of `build-linux.yml`, `build-windows.yml` and `check.yml` step by step in Docker — `ubuntu:24.04` with a sudo user `runner` and g++-14, `debian:trixie` with mingw-w64 and wine, `python:3.14` — on a clone of `HEAD` with the working tree laid over it. GitHub's toolchains are older than a typical development machine's (GCC 14, CMake 3.28 on Ubuntu, glslangValidator instead of glslc), so run it before pushing a change of CMake, of platform code or of shaders. It installs nothing on the host: images, apt downloads and the dependency caches (the `actions/cache` steps) are Docker volumes `sh-ci-*`, the log of a job is `.tmp/local-ci/<job>.log`. `-j <cpus>` limits the CPUs a job sees (default 8), `--fresh` drops the dependency caches first. The steps are transcribed from the yml files: a change of a workflow belongs in the script too.
- **Release**: raise `VERSION` in the commit that is released, tag it `vX.Y.Z` (or `vX.Y.Z-rc1`) and push the tag. `release.yml` checks the tag against `VERSION` (`tools/ci/check_release_version.sh`), builds both platforms and publishes the packages.

## 6. Tools

| Tool | What it is for |
| --- | --- |
| `tools/sh-import/` | the importer as a command-line program (`sh-import`) |
| `tools/sh-audio-check/` | opens the sound output without the game and reports what the device took (`sh-audio-check --help`) |
| `tools/replay/sh_replay.py` | the replay harness: run a route, check all routes, update references, compare dumps |
| `tools/replay/handler_diff.py` | regression of the map scenarios (`tools/replay/*_scenarios.json`, references in `tests/replay/handlers/`) |
| `tools/replay/gen_route.py` | writes a seeded random-input route |
| `tests/smoke/smoke.sh`, `map_sweep.sh`, `quit_after_import.sh`, `quit_in_demo.sh`, `fps_ab.py` | run the game: screenshot, all maps, import only, a quit during the title's demo, frame-rate independence |
| `tests/audio/compare_music.py`, `compare_mix.py`, `fit_layers.py` | compare rendered music and recorded mixes |
| `tools/scripts/check_naming.py` | readable names in data and code (section 4) |
| `tools/scripts/check_map_handlers.py`, `check_anim_clips.py` | shape checks of map data and of an import (the last one needs an imported game) |
| `tools/scripts/gen_text.py`, `gen_flags.py`, `gen_paper_maps.py`, `gen_weapon_rig.py` | check (and normalise) text, flags, paper maps and weapon rigs in `gamedata/` |
| `tools/scripts/gen_map_tables.py` + `map_tables_manifest.json`, `map_tables_data.py` | generate `gamedata/maps/<map>/tables.json`; add readable data to its arrays from an import |
| `tools/scripts/gen_unicode_tables.py` | generates `src/i18n/core/unicode_data.inc` |
| `tools/scripts/build_input_glyphs.py` | builds the input prompt sheet `assets/runtime/icons/input_glyphs.png` from Kenney's CC0 pack |
| `src/formats/convert/mapdata/gen_map_layout.py` | one-time generator of the committed `map_layout.inc`; needs a checkout of the decompilation |
| `tools/scripts/lua_api_docs.py` | keeps `gamedata/tools/lua_api.json` in step with the running game and writes [modding/lua-api.md](modding/lua-api.md) |
| `tools/scripts/check_docs.py` | checks links, paths and examples of the documentation |
| `tools/scripts/sh_text.py`, `en_text.py`, `text_po.py`, `check_translation.py`, `tools/translation/` | the translation workflow ([modding/translation.md](modding/translation.md)) |
| `tools/modding/rig_transfer/` | binds a foreign character model to a skeleton of the game ([modding/rig-transfer.md](modding/rig-transfer.md)) |
| `tools/win-deps/`, `tools/linux-deps/`, `tools/ffmpeg/`, `tools/ncnn/` | fetch and build the dependencies of a release |
| `tools/sh-upscale/` | the AI texture upscaler the game starts in the background (`texture_upscale`) |
| `tools/ci/gate.sh` | the whole verification of section 4 in one run |
| `tools/ci/local_ci.sh` | the GitHub workflows in Docker, as section 5 says |
| `tools/ci/` | packaging and the package check |

## 7. Working with the specification

Behaviour comes from the community decompilation [silent-hill-decomp](https://github.com/shdecompilations/silent-hill-decomp), read as a description of what the game does. Keep a checkout outside the tracked tree (the repository ignores `references/`); none of its files, and no code copied from it, goes into this repository. The fixed-point and evaluation-order rules that matter when behaviour is carried over are in [AGENTS.md](../AGENTS.md).
