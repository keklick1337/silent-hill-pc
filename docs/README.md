# Documentation

All documentation is in English.

## For players

The root [README.md](../README.md): what you need, download, first start, controls, configuration, differences from the original, troubleshooting. Its screenshots are in [images/](images/).

## For modders

[modding/](modding/README.md) — the modding guide.

```text
docs/modding/
  README.md                overview, units and names
  getting-started.md       installing a mod, a first mod step by step, the example mods
  mod-json.md              the manifest, version ranges, dependencies, the load order
  data.md                  how files apply, JSON merging, names, items, weapons, characters, maps, flags, settings
  assets.md                textures, models, animations, sound, movies, shaders, text and its markup, fonts
  scripting.md             Lua: the sandbox, main.lua, script layers, menus, settings, console commands
  events.md                the event bus and the catalogue of events
  lua-api.md               reference of the sh.* functions (generated from gamedata/tools/lua_api.json)
  tools.md                 the built-in editor (F2), its panels, the Lua editor, the console
  editor.md                the map editor
  rig-transfer.md          your own character model on a skeleton of the game
  translation.md           adding a language, the translation workflow
  translation-handbook.md  the translator's handbook: markup to keep, the message window, glossaries, notes per language
  versioning.md            the engine version policy
  troubleshooting.md       what the log says and what to do
```

## For contributors

| Document | What |
| --- | --- |
| [../CONTRIBUTING.md](../CONTRIBUTING.md) | how to contribute: getting started, the rules, commits and pull requests |
| [../AGENTS.md](../AGENTS.md) | the same rules in short, for coding agents |
| [BUILDING.md](BUILDING.md) | building on Linux, the Windows cross-build, dependencies, CMake options, packages |
| [DEVELOPMENT.md](DEVELOPMENT.md) | running, the checks before a change is merged, releases, the tools |
| [ARCHITECTURE.md](ARCHITECTURE.md) | layers, modules, the order data is read in, the decisions in force, every subsystem in short |
| [RENDER.md](RENDER.md) | the renderers, graphics settings and presets, ray tracing, upscaling |
| [TESTING.md](TESTING.md) | unit tests, the replay harness, the smoke test, the map sweep and scenarios |
| [FORMATS.md](FORMATS.md) | formats of the disc and of the animation tables |

`python3 -I tools/scripts/check_docs.py` checks the links, the paths and the examples of all of these.
