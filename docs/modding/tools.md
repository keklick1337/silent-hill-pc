# Tools for modders

[Back to the guide](README.md)

The game carries its own modding tools: an editor that stops the game and lets you fly through the map, viewers for map data, textures and models, a Lua editor, and a developer console.

## Switching the tools on

*Options > Interface > Modding Tools (F2)*, or `modding_tools = 1` in `config.cfg`. Then **F2** during free gameplay (or the console command `editor`) opens the editor. It opens only while the player controls Harry: not in a cutscene, a message, a menu or the pause.

While the editor is open the game stands still: no AI, no events, no timers. Leaving it (F2, `editor off`) restores everything the editor borrowed — fog, draw distance, brightness, the camera — and the game continues unchanged. Only explicit actions change the game: "Place player here", "Teleport player", "Warp".

## The editor

**The camera** flies free: hold the right mouse button to look, W A S D to fly, Q / C down and E up, Shift faster, Ctrl slower, the wheel changes the speed.

**The menu bar** has the list of maps by area (a click warps to a map; PageUp and PageDown go to the previous and next map), the rooms of the current map (Shift+PageUp and Shift+PageDown), a scale slider for the interface, and the menus *Panels* and *Overlays*.

### Viewer

| Option | Effect |
| --- | --- |
| Fog off | no fog, a doubled draw distance, the wide window of outdoor cells |
| Full bright | flat light on the map and the characters, no flashlight |
| Map light scale | a multiplier of the map's light |
| Flashlight off | no flashlight light |
| Brightness, Contrast | multipliers of the frame's grade |
| Wireframe | the wireframe of cells, objects and characters |
| Collision view | the collision geometry in the world |
| Cell bounds | the 40 m cells of the streaming window with their file names |
| Player marker | where Harry stands |

The options act only while the editor is open. "Save viewer settings" keeps them in `config.cfg`.

### Navigation

The map and its description; the player's position in metres and Q12, heading and cell; the camera's position. Buttons: "Camera to player", "Player to camera", a field for x y z in metres with "Teleport camera" and "Teleport player", and the list of all maps with a filter and "Warp" (a new game on that map).

### Picking a point

A left click on the world casts a ray from the camera and finds the first solid point of the drawn geometry. The point is shown in metres and Q12 with its normal and cell, and "Copy" buttons put it on the clipboard in the form you need:

| Form | Example |
| --- | --- |
| the player's position | `sh.player_set_pos(-245760, 0, -260915) -- -60.0, 0.0, -63.7 m` |
| a scratch vector | `sh.vec.set(0, -245760, 0, -260915)` |
| a spawn on the ground | `sh.chara_spawn("GreyChild", 0, -245760, -260915, <the player's heading>, 3)` |
| a Lua table | `{ x = -245760, y = 0, z = -260915 }` |
| `world_objects.json` | `"position": [-60.0, 0.0, -63.7]` |

"Place player here" puts Harry at the point; "Camera here" puts the camera 1.7 m above it.

### Map data overlays and the inspector

The menu *Overlays* draws the data the game actually runs on, so what a mod merged is what you see:

- map events and pickups with the shape of their trigger, the handler's name and what kind of event it is;
- doors and map transitions with their targets;
- spawn points, camera zones, collision triggers, rooms and world objects.

The world inspector (*Panels*) reacts to a click on the world: for a character it shows its model and textures, for a world object or a cell the model name, its glb, the file and the mod it comes from, vertex and triangle counts and the materials.

### Asset panels

*Panels > Assets*:

- **Textures**: the textures the scene has loaded, or every PNG under `convert/textures/` of the game and the active mods (a mod's copy is marked). Zoom, the texel under the pointer, size, format, palettes, the file and the mod that owns it, what uses the picture, and "Highlight in world".
- **Model viewer**: characters and models with an orbit camera, lit or unlit, wireframe, the skeleton, and for a character its animation clips on a timeline.
- **Reload from disk** reads a changed picture or model again.

### Lua editor

*Panels > Data > Lua editor*.

- **Files**: every `.lua` of the base data and of the active mods, the files of the current map first. Each copy is marked `[base]` or `[<mod>]`, with a hint saying what the copy does in the game by the rules of [script layers](scripting.md#script-layers): base, base plus extensions, replaced by a mod, not run because `"scripts": true` is missing.
- **Editing**: line numbers, syntax highlighting, completion of `sh.` and its tables from the live script states (Ctrl+Space), and under the code the signature and description of the function at the cursor — the text of the [Lua API reference](lua-api.md). A member of `sh.*` that the running game does not have is underlined in red.
- **Signature help**: while the cursor is inside the parentheses of a described function, a tooltip over the line shows its parameters with the one being typed highlighted, and its one-line description.
- **API tab**: every function of the live state with search; a click inserts a call.
- **Running code**: the Lua tab runs one line, and "Run file" (F5) runs the whole open file, or the selected part of it ("Run selection"), in the live game or map script state; what it prints and returns, or its error, appears under it.
- **Tests tab**: runs a mod's `tests/*.lua` ([Tests](scripting.md#tests)) and lists each test as passed or failed with its error.
- **New script...** creates a script in a mod: the mod's `main.lua`, a layer over a map's `events.lua`, `hooks.lua` or `world_objects.lua`, a layer over a character's `behavior.lua`, or a menu file. The mod's `mod.json` gets `"scripts": true`.
- **Saving** never writes the base data. "Save to mod" writes the file at the same relative path into a mod you choose or a new one, creating `mod.json` when needed. The syntax is checked first; a file with an error is not written.
- **Taking effect**: a new mod and a changed `mod.json` are read at start. For a file a mod already provides, "Re-run map script" runs the current map's `events.lua` again without reloading the map, and "Reload" does a `mods reload`.

### Flags

*Panels > Data > Flags*. "Event flags" lists every named event flag of the running game (`gamedata/flags/*.json` and the mods' `flags.json`) with a filter and an "only set" switch; a click sets or clears a flag at once, and the log says so (`[FLAGS] set ...`). "Map variables" shows the variables of the current map (`maps/<map>/map_vars.json` and `config/map_vars.json`, up to 16 elements of an array) with their live values; type a number or `true`/`false` and press Enter to write one through `sh.map.set`. The console command `flagpanel list [filter] | toggle <flag> | vars | setvar <name[i]> <value>` does the same from a script.

A mod that adds functions of its own describes them for the editor in `<mod>/tools/lua_api.json`, in the form of [`gamedata/tools/lua_api.json`](../../gamedata/tools/lua_api.json): `{"functions": {"name": {"signature": "...", "description": "..."}}}`.

## Reloading a mod while the game runs

`mods reload` (console) builds the list of the active mods' files again and compares it with the previous one:

- text (`text/`) is read again at once;
- menus (`menus/*.lua`) and the game script with the mods' `scripts/main.lua` run again at the start of the next frame (`on_boot` runs again; `mod.storage` starts empty);
- when a data file changed (items, weapons, flags, `config/`, characters, maps and their scripts, models, textures, sounds), everything parsed at start is read again, and in a game the current map is loaded again in place, like loading a save: items, flags, Harry's place and health are kept;
- files that are read only at start are reported as `changed <file>: read when the game starts`: shaders, fonts and a few boot configuration files.

Switching mods on and off and their order always need a restart.

## The console

`` ` `` opens it. `help` lists the commands and `help <command>` explains one; Tab completes. The commands a modder uses most:

| Command | What it does |
| --- | --- |
| `mods`, `mods files <id>`, `mods enable <id>`, `mods disable <id>`, `mods up <id>`, `mods down <id>`, `mods reload` | the mod system ([mod-json.md](mod-json.md#the-mods-screen-and-the-console)) |
| `hooks [event]` | the handlers of every event in call order, and how often the event happened |
| `hooks_trace [on\|off]` | write every event with its fields to the log |
| `debugmenu <menu id>` | open any menu (`options`, `options.mods`, a mod's page) |
| `version` | the engine version, the build and the mod API version |
| `pos` | Harry's position, heading, map and room |
| `tp x y z` | teleport Harry |
| `map [name]` | warp to a map, or show the current one and the list |
| `give <item>` | give an item, weapon or ammunition by name |
| `equip <weapon>` | take an owned weapon in hand |
| `spawn [character]` | spawn a character in front of Harry, or list them |
| `charas` | the spawned characters: slot, type, health, position |
| `select`, `info`, `health`, `kill`, `freeze` | work on a selected character |
| `flags`, `setflag <flag>` | the event flags that are set; set one |
| `event` | list or start the events of the current map |
| `difficulty easy\|normal\|hard` | set the difficulty |
| `god`, `infammo`, `notarget`, `noclip` | cheats for testing |
| `fmv [name]` | list or play a movie |
| `langcheck` | check the text layers of the current language and say which font draws its letters |
| `shot [file]` | save a screenshot |
| `log` | log categories and levels |
| `editor`, `editorview`, `editorroom`, `editorcam`, `editorpick`, `editortexture`, `editormodel`, `luaedit` | drive the editor from the console, for scripted runs |

A file of console commands can be run at start with `-consolecmd <file>`; a line that begins with `@N` runs N seconds after the start, one with `@+N` N sixtieths of a second of game time after the line before it. Mods add their own commands with [`sh.console.register`](scripting.md#console-commands-shconsole).

The log is the other half of the console: `log_file = 1` in `config.cfg` writes it into `logs/`. Lines of the mod system begin with `[MODS]`; [troubleshooting.md](troubleshooting.md) lists what they mean.

## Tools outside the game

| Tool | What it is for |
| --- | --- |
| `sh-import` (in the package) | the importer on the command line: `sh-import <image.cue> --out <dir>` |
| [`tools/modding/rig_transfer/`](../../tools/modding/rig_transfer/) | binds your own character model to a skeleton of the game ([rig-transfer.md](rig-transfer.md)) |
| [`tools/translation/`](../../tools/translation/), `tools/scripts/text_po.py`, `tools/scripts/check_translation.py` | build and check a translation ([translation.md](translation.md)) |
