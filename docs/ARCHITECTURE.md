# Architecture

Describes the code as it is. Build, run and the checks: [DEVELOPMENT.md](DEVELOPMENT.md).

## 1. What we build

Our own general-purpose C++20 engine, and the game Silent Hill (PS1, NTSC USA v1.1) as data for it:

- **the engine** (`src/`) knows nothing about the particular game wherever that can be moved into data: window, input, files, rendering, sound, entities, world, scripts, mods, console;
- **the game** is `gamedata/`: base data in the repository (maps, events, characters, items, weapons, flags, texts, configs, Lua) and resources that the import extracts from the user's image on the first start (glTF models, PNG textures, WAV sound, levels, animations, JSON cutscenes);
- behaviour is "like the original, about 1:1" (D42); the reference for behaviour is the upstream decompilation (a local copy outside the repository), read as a specification.

`src/` holds no code of the original, no PS1 emulation and none of its shape (D48, D51). The console's byte layout lives only in the converters (PS1 memory card records, slot −1; D51).

## 2. Layers

```
features/   cheats and debug bindings, freecam, overlay panels (hud: frame counter, profiler), quick save, state dump, modder tools (tools)
ui/         screens, menus, pause, HUD
i18n/  input/  audio/  media/fmv/  render/  platform/      subsystems behind interfaces
engine/     engine core and game hosts (engine/*/game)
import/     image import on the first start (and the CLI tools/sh-import)
formats/    pure readers and converters of disc and engine formats
third_party/ lua, miniz, stb, cgltf, bcdec, imgui, volk, vma, vulkan-headers, fonts; SDL3 from the system or from source
```

Rules:

- `formats/` does not depend on `engine/` or the subsystems: only the standard library and `third_party/`.
- `engine/` depends on `formats/` but not on `ui/` or `features/`; `features/` and `ui/` change game state only through the engine's public headers, hooks and Lua.
- The import (`import/`, `formats/convert`) is linked into the game only for the first start (D38); the image is not read during play.
- No cycles. Every module declares a target `sh_<name>` in its `CMakeLists.txt`.

## 3. Modules

| Directory | Contents |
|---|---|
| `src/engine/app` | entry point (`game_main.cpp`), quick setup (`quick_setup/`, before everything else on the first start), first start (`first_run/`: the disc import and its screen), what both screens draw with (`screen_kit/`: text from the TrueType and system fonts, the menus' widgets, layout for any window), main loop, game screen, restart, paths |
| `src/engine/platform`, `timing`, `loop` | window, mode, vsync, events (`sh::platform`, `window.h`); VBlank clock, replay virtual time (`sh::timing`); frame and logic tick |
| `src/engine/desktop` | the application's desktop face (`sh_desktop`): application name and id `silent-hill-pc` (X11 window class, Wayland app id — `SDL_SetAppMetadata`), window icon from `<exe>/assets/icon/app_<N>.png`, a Linux application menu entry on the user's request — `--install-desktop` / `--uninstall-desktop` write `~/.local/share/applications/silent-hill-pc.desktop` (absolute exe path, `StartupWMClass=silent-hill-pc`) and icons into `~/.local/share/icons/hicolor/<N>x<N>/apps/`; the template is `assets/linux/silent-hill-pc.desktop.in` (embedded in the exe), the same from the shell is `assets/linux/install-desktop.sh`. Whether anyone sees the windows (`video_driver.h`): `SDL_VIDEODRIVER=offscreen` or `dummy` means a headless run, and on macOS, whose SDL offscreen driver has no OpenGL, offscreen is answered by Cocoa with hidden windows. Everything about the icon is in `assets/icon/`: `master.svg` → `master.png` → sizes and `app.ico` (`generate.sh`); to replace the icon, replace `master.png` and run `generate.sh --keep-master`. Test: `tests/desktop/` |
| `src/engine/config`, `gconfig`, `log`, `console`, `debug`, `devtools`, `crash` | `config.cfg`; game tables `gamedata/config/`; log with categories; console; debug menus; crash handler |
| `src/engine/vfs`, `assets`, `loader`, `manifest`, `mods`, `hooks` | virtual file system (mods → `<exe>/gamedata`); resource layers; file queue; mod registry and load order, event bus (D49) |
| `src/engine/script`, `scripting` | sandboxed Lua 5.4: the `sh.*` API, map events, character behaviour, menus |
| `src/engine/ids`, `flags`, `tables` | names ↔ internal numbers at run time; flag registry from `gamedata/flags/`; tables `gamedata/tables/` |
| `src/engine/game_state`, `memory` | live game state (`sh::game::Session`, D50); work buffers `sh::memory::WorkBuffers` |
| `src/engine/world`, `maps`, `runtime`, `camera` | world and collision from `maps/areas/*.glb`, map data, cell streaming, cameras |
| `src/engine/entities`, `chara`, `player`, `combat`, `items`, `inventory` | skeleton and animation from glb, characters from `gamedata/chara/`, Harry, combat, items and weapons |
| `src/engine/cutscene`, `effects`, `gameflow`, `save`, `screens`, `menu`, `text`, `utf8` | cutscenes from `*.dms.json`, effects, game states, saves, screens, texts |
| `src/engine/math` | engine math `sh::math`: Q12 matrices and vectors, projection (`fixed_geometry.h`, `geometry_state.h`) |
| `src/render` | RHI (Vulkan, with ray tracing where available; GL 3.3 and SDL3 GPU), scene rendering from glb, 2D, post effects, presets ([RENDER.md](RENDER.md)) |
| `src/audio` | mixer, effects from VAB JSON, music sequencer, XA voice lines from WAV, output `sh::audio::output::OutputDevice` |
| `src/input` | semantic actions, bindings, gamepads, mouse, alternative cameras |
| `src/i18n` | languages from the text catalogues, text by key, the game font and fallback fonts; the library `src/i18n/core` (`sh_i18n`): language descriptors, right-to-left order, Arabic letter forms, line breaking, font catalogue |
| `src/media/fmv` | movies: disc (STR, MDEC, XA), AVI and containers through FFmpeg; the library `src/media/fmv/codec` (`sh_fmv`) holds the formats without the game |
| `src/ui`, `src/features` | screens and HUD; additional PC features |
| `src/formats`, `src/import` | BIN/CUE image, ISO 9660, SILENT./HILL. archives, converters TIM/ILM/ANM/IPD/DMS/VAB/KDT/XA → PNG/glTF/JSON/WAV ([FORMATS.md](FORMATS.md)) |

Game hosts — the code that connects the rules of Silent Hill to the engine's subsystems — live in `src/engine/*/game`, `src/engine/app`, `src/ui/screens`, `src/render/world`. Everything that can be described as data goes to `gamedata/`.

## 4. Data and read order

| Directory | Contents | In git |
|---|---|---|
| `gamedata/` (repository root) | base game data: `chara/`, `items/`, `weapons/`, `flags/`, `maps/<map>/`, `config/`, `menu/`, `menus/`, `text/`, `sounds/`, `scripts/`, `world/`, `effects/`, `npc/`, `game.json`; the build copies them next to the exe (`cmake/copy_gamedata.cmake`) | yes |
| `<exe>/gamedata/` | base data + import output: `convert/` (PNG, glb, WAV, JSON; `files/` the disc files no converter rebuilds), `tables/`, `maps/` (import output), `index.tsv`, `version.cfg` (import version); `raw/` (disc bytes) only with `keep_raw = 1` | no |
| `<exe>/mods/<id>/` | mods: `mod.json` and files at `gamedata/` paths (D49) | no |
| `assets/` | the engine's own resources: default `config.cfg`, icons, controller database, licenses | yes |
| `data/` | the user's disc image | no |

Read order: mods in load order (later wins; JSON merges by field, Lua runs in layers, whole replacement only through the `replace` list), then `<exe>/gamedata/`. Names are case-insensitive. Identifiers in data are namespaced string names (`silenthill:HealthDrink`, a mod's — `<mod id>:Name`); the engine hands out numbers at run time and stores them nowhere, saves hold names.

## 5. Extension points

| What changes | How |
|---|---|
| Models, textures, sounds, animations, texts, fonts | a file in a mod at `gamedata/convert/...` or `gamedata/text/...` |
| Characters, items, weapons, flags, game configs, map data | JSON in a mod, merged by field |
| Logic: map events, character behaviour, menus | Lua in a mod (`"scripts": true`), `extend` by function name, the `sh.hooks` bus |
| A new map | a folder `gamedata/maps/<map>/` + resources |
| User settings | `config.cfg` |

Guide: [modding/](modding/README.md).

## 6. How we check

- **Replay harness** (`tests/replay`, `tools/replay/sh_replay.py`): per-tick input, virtual time, state dump; routes are compared with references byte for byte. It guards against regressions; it does not prove faithfulness to the original (D42).
- **Map sweep** (`tests/smoke/map_sweep.sh`): all 43 maps load and run; **map scenarios** (`tools/replay/handler_diff.py`): event handlers run without errors and repeatably.
- **Unit tests** `sh_tests` and a smoke test with a screenshot ([TESTING.md](TESTING.md)).
- **Cleanliness**: `check_naming.py --strict` reports 0.

Commands and the order of checks: [DEVELOPMENT.md](DEVELOPMENT.md), section 4.

## 7. Decisions

The decisions in force. Code and documents refer to them by number, so the numbers are not reused; gaps are decisions that no longer apply.

- **D9. Platforms.** Linux x64 and Windows (mingw, dependencies built from source inside the project — [BUILDING.md](BUILDING.md)), macOS, and Android (an APK of all four ABIs, Vulkan only; the game is `libmain.so` under SDL's activity, `android/`).
- **D10, D12. Lua.** Lua 5.4 (`third_party/lua`) is the scripting layer. Sandbox: no `io`, `os`, `debug`, `package`, `dofile`, `loadfile`, bytecode; an instruction limit through a hook and a memory limit through our own allocator.
- **D13, D27. Time.** Logic runs on a virtual VBlank, as in the original: gameplay is 2 VBlanks per tick (~30 Hz), menus 1; rendering is separate, with interpolation. A slow frame skips rendering rather than slowing the logic down.
- **D19. Style.** `.clang-format` (Google style, indent 4, 120 columns) for `src/` and `tests/`: changed lines are formatted, the whole tree is not reformatted; comments only about the non-obvious "why", in English.
- **D29. Sound.** Output is `sh::audio::output::OutputDevice` with a shared timeline; the clock is the game's VBlank, and in replay the output is synchronous.
- **D31, D32, D38. First start.** If `<exe>/gamedata/` has no fresh `version.cfg`, the game imports the image (`.bin`+`.cue` or a raw image): the disc's files are extracted to `raw/` and converted into `convert/` (the engine formats), then `raw/` is deleted (`keep_raw = 1` keeps it). The game reads the disc's files through the converted data: a lossless converter rebuilds each file byte for byte, `maps/areas/*.glb` holds the map files, and `convert/files/` keeps the rest as it is (`src/formats/convert/gamefiles/game_files.h`); an older import loses `raw/` on its next start. After that the image is not needed. The import does not touch the repository's base data. Paths are relative to the exe directory.
- **D33. Engine formats.** Textures are PNG, models and levels glTF 2.0 (`.glb`), sound WAV + JSON (a mod may give Ogg Opus wherever a WAV goes), texts UTF-8 JSON, cutscenes and tables JSON, logic Lua.
- **D36, D41. Tables.** Behaviour constants a modder may want to change (animations, speeds, damage, timings) are JSON in `gamedata/tables/` or `gamedata/config/`; changing them is a gameplay mod.
- **D37. Settings and low-end PCs.** All settings are `config.cfg` keys (defaults in code, an unknown key gives a warning); quality keys do not change logic or timing.
- **D39. Completeness.** Each of the 43 maps loads and runs; replay routes match the references; default controls and camera are as upstream (deviations are options).
- **D40. Replay harness** is part of the repository (`tests/replay`, `tools/replay`), the gate for merges that change logic.
- **D42. "About 1:1".** The game plays like the original, but not bit for bit: loading is instant (`cd_speed_ps1 = 0`, `cd_fast_load = 1`), graphics improve through presets (`graphics_preset`), the atmosphere (fog, darkness, flashlight) is kept. Changes of story, balance and rules exist only as options marked GAMEPLAY CHANGE in `assets/config.cfg`, off by default.
- **D44. Debugging.** A log with categories and levels (terminal, file, game console) and an in-game console.
- **D45, D46. The game is data.** Maps are resources, not code: map data, events (`events.lua`), characters, items, texts are in `gamedata/`; adding a map or changing the story is an edit of `gamedata/` without a rebuild.
- **D47, D48. Not a line of the original.** `src/` has no code of the original or of the decompilation; the decompilation is read only as a description of behaviour, copying its code is forbidden.
- **D49. Mods.** A mod is `<exe>/mods/<folder>/mod.json` (id is the namespace, semver, dependencies, conflicts, requirements) and files at `gamedata/` paths. One registry decides the order (dependencies, the player's choice in `<exe>/mods/load_order.json`, the "Options > Mods" screen, the `mods` console command, then `priority`). Mods stack: the `sh.hooks` bus, Lua layers with `extend`, JSON merging (`data_merge.h`), `mod.export`/`sh.mods.get`. Mods contain no native code. There is one engine version, `MAJOR.MINOR.PATCH` in the `VERSION` file at the root (module `src/engine/version/`, CMake `project(... VERSION ...)`; build number and commit are metadata from `cmake/gen_build_info.cmake`); a mod requires it with the keys `min_engine_version` / `max_engine_version`, and a mod for another engine is not loaded; a release bumps the number (MAJOR — a breaking change of formats and API visible to mods, MINOR — additions, PATCH — fixes), and the tag `vX.Y.Z` must match it.
- **D50. Game state is engine classes.** `sh::game::Session`, `CharacterRecord`/`Character`, `PlayerState`, `MapRuntime`, `SaveRecord` with readable names; saves hold names.
- **D51. No PS1 shape.** The platform layer is `sh::platform`, `sh::timing`, `sh::disc`, `sh::input` (`input/pad.h`), `sh::audio::OutputDevice`; render state is `sh::render` structures (`render/gpu/render_state.h`); geometry is `sh::math`; working memory is `sh::memory::WorkBuffers`: each buffer has a name and its own capacity, memory is shared only by names the game links through addresses; the primitive renderer is `render/rhi/prim_batch`. `Session`, `CharacterRecord`, `SaveRecord`, `Settings` are ordinary engine classes: fields by name, without padding fields or `static_assert`s of the original layout, never read as bytes. The console's byte layout exists only in the converters, by field name: console character records (`engine/entities/console_character.h`: slot −1), the progress record and settings for PS1 memory cards, version 1 files and demos (`engine/save/console_records.h`). The replay dump hashes field values (`sav`, `gw`, `cmb`).

## 8. Subsystems in short

Where each part of the game lives. Every engine module computes what happens; the hosts in `src/engine/*/game/` (and `src/ui/`) connect it to the session (`sh::game::session()`), input, sound and the loader. Rules of this particular game are data in `gamedata/`, each file explained by its `description` key.

| Subsystem | Code | Data and notes |
| --- | --- | --- |
| Live state | `src/engine/game_state/` | all live state in engine classes with readable names; Lua sees it through `sh.*`. Units are the game's: positions in Q12 metres (+Y down), angles in Q12 turns (4096 = 360°), health and time in Q12 |
| Main loop and game flow | `src/engine/loop/`, `src/engine/gameflow/`, hosts in `src/engine/app/` | frame clock, pacing, soft reset, pads, RNG; which map event runs next, game states, map loading, the main frame: `gamedata/config/*.json` (`boot.json`, `main_frame.json`, ...) |
| File loading | `src/engine/loader/queue/`, `src/engine/app/loader/` | the file queue every read goes through, the disc file table, mod files and replacement pictures |
| Import | `src/import/`, `src/formats/convert/` | the first start converts the disc image into `<exe>/gamedata/` (`convert/` with PNG, glb, WAV and JSON and the game files no converter rebuilds, `tables/`, map files; `raw/` only with `keep_raw = 1`); `version.cfg` names the import version and a newer build re-imports by itself. Formats: [FORMATS.md](FORMATS.md) |
| Maps | `src/engine/maps/`, `src/engine/scripting/` | a map is data and Lua only, `gamedata/maps/<map>/` (`events.lua`, `handlers.json`, `rooms.json`, `map_vars.json`, `world_objects.json`, `charas.json`, `tables.json`, `lights.json`, ...); the engine has no map code |
| Characters and enemies | `src/engine/chara/`, `src/engine/entities/`, `src/engine/player/` | one folder per character, `gamedata/chara/<Name>/` (`character.json`, `state.json`, `behavior.lua`); spawning, NPC slots, the radio and enemy attacks follow the original |
| Combat and weapons | `src/engine/combat/`, `src/engine/items/` | bit-identical integer results; one file per weapon `gamedata/weapons/<Name>.json` (attack rows, rig, `muzzle_light`) |
| Items and inventory | `src/engine/items/`, `src/engine/inventory/` | one file per item `gamedata/items/<Name>.json`; the inventory screens follow the original, quirks included |
| Camera | `src/engine/camera/` | the game camera matches the original bit for bit; PC camera styles in `gamedata/config/alt_cameras.json`. Their look keeps the fraction of an angle unit, and the picture is drawn from their unrounded view on every presented frame, between two updates turned by what the mouse and the right stick have done since (`src/input/alt_camera_view.h`, `src/engine/app/loop/scene_aim.h`); the game's own angles and the update stay as they are |
| Collision | `src/engine/world/` | ground, rays, cylinder moves, walls, triggers and line of sight over the map's collision data |
| Effects | `src/engine/effects/` | particles, weather, blood, fire, water, map lighting; RNG draw order and 16-bit truncation as in the original; `gamedata/effects/*.json` |
| Cutscenes and movies | `src/engine/cutscene/`, `src/media/fmv/` | the disc's `DMS` cutscenes are converted to JSON once and evaluated by the engine; movies are decoded by the engine (mods may add mp4 through FFmpeg) |
| Screens and menus | `src/engine/screens/`, `src/ui/` | title, options, map, credits and the rest; menus are Lua in `gamedata/menus/` |
| Audio | `src/engine/audio/`, `src/audio/` | the engine plays the converted sound banks and music with the original driver's voice, envelope and reverb rules; no sound chip is emulated; sound names in `gamedata/sounds/` |
| Input | `src/input/` | keyboard and mouse bind to actions, which become the pad bits the logic reads; controllers keep their own bindings; the PC actions' controller binds also take two buttons together (`leftshoulder+rightshoulder`, `src/input/bind/pad_chord.h`: the second press within 167 ms of the first, the original's step tap memory; while the pair is held neither button fires its own bind), and the quick turn is that pair by default, as the original turns on both side step buttons; a touch screen's controls hold the same actions (`src/input/touch/`, pictures in `gamedata/ui/touch/`, the action button's kinds in `gamedata/config/touch_actions.json`); the game's pad vibration (`src/engine/gameflow/pad_vibration.h`, scripts in `gamedata/config/pad_vibration.json`, started from Lua with `sh.pad.effect`) drives a pad's motors, and on Android the phone's own vibrator while no pad with motors is in use (`src/input/phone_rumble.h`, SDL's `VIBRATOR_SERVICE` haptic, `phone_vibration`) |
| Text and languages | `src/i18n/`, `src/engine/text/` | all text under keys in `gamedata/text/<lang>/`, a language is a folder with `lang.json`; the game font plus glyph sheets in `gamedata/font/`, other scripts from bundled or system fonts |
| Saves | `src/engine/save/` | named files in `.savegames/` beside the executable, everything by name |
| Rendering | `src/render/` | [RENDER.md](RENDER.md) |
| Log, console, developer tools | `src/engine/log/`, `src/engine/console/`, `src/engine/debug/`, `src/features/` | the categorised log (`log_level`, `log_file` in `config.cfg`), the console (`` ` ``, `help`), the modding tools ([modding/tools.md](modding/tools.md)), state dumps and the replay harness ([TESTING.md](TESTING.md)) |
