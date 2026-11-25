# Modding Silent Hill PC

A mod is a folder with a `mod.json` and files laid out like the game's own `gamedata/`. It can replace textures, models, sounds, movies and text; patch any JSON data file of the game; add items, weapons, characters, languages, menus and console commands; and run sandboxed Lua that reacts to what happens in the game. A mod holds data and Lua only: no native code is ever loaded from a mod.

## The guide

| Page | What is in it |
| --- | --- |
| [Getting started](getting-started.md) | install a mod, make your first one step by step, the example mods |
| [mod.json](mod-json.md) | every key of the manifest, version ranges, dependencies, the load order, where mods live |
| [Data](data.md) | how a mod's files apply (replace, patch, merge), names and namespaces, items, weapons, characters, maps, flags, game settings |
| [Assets](assets.md) | textures and HD replacements, character models, animations, sounds, music, movies, shaders, text, languages, fonts |
| [Scripting](scripting.md) | Lua in a mod: the sandbox, `scripts/main.lua`, script layers, menus and option pages, settings, console commands, talking to other mods |
| [Events](events.md) | the event bus `sh.hooks` and the catalogue of game events |
| [Lua API reference](lua-api.md) | every function of the `sh` table |
| [Tools](tools.md) | the built-in editor (F2), its panels, the Lua editor, console commands for modders |
| [Making a mod in the editor](editor.md) | the Mod panel: a new mod, triggers, spawns and pickups placed on a map, scripts, saving and reloading |
| [Replacing a character model](rig-transfer.md) | the `rig_transfer` tool: your own model on a skeleton of the game |
| [Translating the game](translation.md) | adding a language or changing a translation |
| [Engine version policy](versioning.md) | what the engine's version number promises a mod |
| [Troubleshooting](troubleshooting.md) | what the log says and what to do about it |

How the engine is put together is in [ARCHITECTURE.md](../ARCHITECTURE.md); the other documents are listed in [docs/README.md](../README.md).

## In short

- A mod is `mods/<folder>/` beside the game's executable, with a `mod.json` in it. Only `id` and `version` are required.
- *Options > Mods > Import mods* installs mods from a folder or a `.zip` the player picks (or drops on the window): it finds every mod in it, shows what importing does and copies the chosen ones into `mods/` ([getting-started.md](getting-started.md#installing-a-mod)).
- Paths inside a mod repeat the paths of `gamedata/`. A file at a path the game has replaces or patches the game's file, depending on its kind; a file at a new path is added. Letter case does not matter and `\` equals `/`.
- A newly installed mod is **off**. The player switches mods on and off and orders them in *Options > Mods* on the title screen (or with the console command `mods`); the set changes after a restart.
- Mods load in order, and the later mod has the last word. Mods are meant to stack: many are active at once, each with its own Lua environment and its own event handlers.
- Lua of a mod runs only when `mod.json` says `"scripts": true`.
- A mod that changes rules, balance or story says `"gameplay": true` and is marked GAMEPLAY CHANGE on the mods screen. A mod that changes only looks, sound and text does not.
- With an empty `mods/` folder the game is the original.

## Units and names

Two conventions run through all data and the whole Lua API.

**Fixed point.** The game computes in integers. A position or length is *Q12*: 4096 units are one metre, and Y grows downwards (a point one metre above the ground has `y = -4096`). An angle is Q12 as well: 4096 is a full turn, so 1024 is 90 degrees. Times are Q12 seconds, health is Q12 (409600 is 100). To convert, multiply and truncate towards zero: in Lua `math.floor(m * 4096)` for a positive value and `math.ceil(m * 4096)` for a negative one. The console command `pos` prints where Harry stands, and the editor's point picker copies a point as ready Lua or JSON ([tools.md](tools.md)).

**Names, not numbers.** Every item, weapon, character, sound, flag and message is a readable name with a namespace: `silenthill:HealthDrink` in the base game (the prefix may be left out), `<mod id>:Crowbar` in a mod. The engine hands out internal numbers while it runs and stores them nowhere; saves hold names, so a save survives a change of the mod set ([data.md](data.md#names-and-namespaces)).

## What mods cannot do

- Load native code: `.so`, `.dll` and `.dylib` files in a mod folder do nothing.
- Read or write files, load code or touch the system from Lua ([the sandbox](scripting.md#the-sandbox)).
- Load from an archive: a mod is a folder. A `.zip` is unpacked into `mods/` by *Import mods*.
- Replace textures by the hash of a picture (DuckStation-style texture packs): a picture is replaced by its name.

Game files are never part of this project, and a mod you publish is your own responsibility: ship only what you may distribute ([COPYRIGHT.md](../../COPYRIGHT.md), section 4).
