# Tools for modders

[Back to the guide](README.md)

The game carries its own modding tools: an editor that stops the game and lets you fly through the map, viewers for map data, textures and models, a Lua editor, and a developer console.

## Switching the tools on

*Options > Interface > Modding Tools (F2)*, or `modding_tools = 1` in `config.cfg`. Then **F2** during free gameplay (or the console command `editor`) opens the editor. It opens only while the player controls Harry: not in a cutscene, a message, a menu or the pause.

While the editor is open the game stands still: no AI, no events, no timers. Leaving it (F2, `editor off`) restores everything the editor borrowed — fog, draw distance, brightness, the camera — and the game continues unchanged. Only explicit actions change the game: "Place player here", "Teleport player", "Warp", and the edits of the placement editor, which last until the map is loaded again.

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
- spawn points, camera zones, collision triggers, rooms and world objects;
- the scripts' live things (*Scripts: pickups, marks, characters*): the pickups a script put down (`sh.world.pickup_create`), the places a script marked (`sh.world.mark`: its trigger zones, save points, the doors it shuts, monsters waiting to come) and every character in the world now.

The inspector's *live* switch (on by default) shows what runs now rather than what the files hold: an event whose flags shut it (its condition unmet, or already done) and a world object that is not drawn now are left out. A story mod that shuts the game's doors, takes the game's pickups away and places its own shows its own world this way; switch *live* off to see the files as they are.

The world inspector (*Panels*) reacts to a click on the world: for a character it shows its model and textures, for a world object or a cell the model name, its glb, the file and the mod it comes from, vertex and triangle counts and the materials.

### Placement editor

*Panels > World > Placement editor*, with the overlay *Placement editor: selection, handles, cursor, path*. It moves, turns and resizes the things placed on the loaded map in the running game, and gives the line of the file as it should stand afterwards. It writes no file: you copy the line into your mod and read the mod again (`mods reload`).

**The list.** Every thing of the map by kind, each kind with a switch: world objects, pickups a script placed, monsters and other characters in the world, script zones (`sh.world.mark`), the map's event triggers, map points, spawn rows. *only within N m* keeps what lies near the camera; switched off, the list holds the whole map. The search box matches the name, the kind and what the thing does. A click on a row selects it, a double click also flies the camera there. A thing whose flags or cells shut it now is marked *(off now)*.

**Selecting in the world.** With *click selects* on, a left click on a thing selects it (for a zone, a click inside its circle); the selected thing is drawn white with its name.

**Moving and turning.** Tool *move* (key 1): drag the yellow dot to move the thing on its level, the red and blue handles to move it along x or z only, the green handle for its height. Tool *rotate* (key 2): drag the green ring to turn it about the vertical, the red and blue rings for the other two angles. Ctrl moves by the step of the fields and turns by 15 degrees. The fields under the tools hold the same numbers (x, y, z in metres, y grows down; three angles in degrees): drag a field or double-click it to type; *step* sets how far a drag of a field moves. The thing moves in the game as the numbers change.

**Zones.** A script zone is drawn as its circle with its label; a box trigger of the map as its box. Drag the orange dot on the edge, or the edge of a circle itself, to resize it, or use the *radius* field (*half x*, *half z* for a box, kept in quarter metres as the map stores them). For a script zone the editor moves the mark the script drew of it: the script's own test changes when its file does.

**Step back.** *Undo* (Ctrl+Z) and *Redo* (Ctrl+Shift+Z) step through the edits of the map, one drag a step. *Reset to the file* gives the selected thing what it had before its first edit. A map load puts everything back as the files say.

**Where it is written.** Under the fields: the file that holds the thing, base data or a mod's, with the line.

| Kind | Where the editor finds it |
| --- | --- |
| world object | every `maps/<map>/world_objects.json` that has a record of that name (the base file and each mod's), with the line; the one holding `"position"` is marked |
| script pickup, script zone, script-spawned character | the script file and line of the call that made it (`sh.world.pickup_create`, `sh.world.mark`, `sh.entity.spawn`) with the function that called it, and the line of that file whose numbers are its place (and, for a zone, the line with its label and radius) |
| map trigger, map point | the point's entry of `points.json`; a mod changes it in its `map.json` |
| spawn row | `spawns.json` and `spawn_overrides.json` |

The engine notes the script line when the thing is made, so a mod needs nothing for it. The line with the numbers is found by the numbers themselves: a place written as two literals on one line (a row of a table, a `{ x = ..., z = ... }` constant) is found; one computed in the script is not, and the editor then gives the new numbers to put by hand. When several lines match, *next matching line* steps through them. *Open in the Lua editor* opens the script.

**The corrected line.** After an edit the panel shows each place to change: the file and line, the old text in red and the new in green. Only the changed numbers are rewritten, in the form the file wrote them (metres stay metres, a Q12 integer stays an integer); the rest of the line and its comment stay. *Copy* puts the new line on the clipboard and prints file, old and new to the log; for a world object *Copy the whole record* gives the record with every other key as the file has it, and for a base object a second entry gives the record a mod writes to patch it by name. *Copy all* and *Print to the console* take every entry.

**The cursor point** is a point of its own, for places with no ground under them. *Place by a click* puts it on the surface under the next click, *At the player* and *In front of the camera* where they say; then drag its handles or its three fields. *snap to the ground* keeps it on the surface under the pointer; *free height* keeps its height on a drag, and the green handle changes it. Its place is shown in metres and in the game's integers, and *Copy* gives the row a script writes: `{ x = -40, y = -2, z = -74729 } -- -0.01, -0.0005, -18.2444 m`. *Player to the cursor* teleports the player there.

**The path.** *Add point* drops a point at the cursor and the cursor stays for the next one. The points are joined by a line with each segment's length and the running total; a click on a point's dot (or its row) puts the handles on it, the *x* of its row deletes it. Type a *speed* to read the time each segment and the whole path take, or a *time* to read the speed it needs. The table is copied as Lua in the game's integers, as Lua in metres or as JSON, and printed to the log.

**Teleporting.** *Player here*, *Player to the cursor* and the room buttons of the menu bar hold the player at the place until its floor has loaded. Every teleport of the tools and of the console (`tp`, `goto`) takes the play camera along in the same frame: it is put behind the player as after a map load, and the camera zone of the place takes over from there. `sh.player.set_pos` of a script does not move the camera: a scene sets its own.

The console command `edit` does all of this for scripted runs (`help edit`), and `editormouse` clicks and drags at a place of the picture or at a world point where no pointer exists.

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
| `mods`, `mods files <id>`, `mods enable <id>`, `mods disable <id>`, `mods up <id>`, `mods down <id>`, `mods reload`, `modimport <path> [install]` | the mod system ([mod-json.md](mod-json.md#the-mods-screen-and-the-console)) |
| `hooks [event]` | the handlers of every event in call order, and how often the event happened |
| `hooks_trace [on\|off]` | write every event with its fields to the log |
| `debugmenu <menu id>` | open any menu (`options`, `options.mods`, a mod's page) |
| `version` | the engine version, the build and the mod API version |
| `pos` | Harry's position, heading, map and room |
| `tp x y z` | teleport Harry; the camera goes with him |
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
| `sounds` | what sounds now: the radio (on or off, whom its two static channels follow, the monsters in reach) and every sound file playing with its level |
| `editor`, `editorview`, `editorroom`, `editorcam`, `editorpick`, `editormouse`, `editortexture`, `editormodel`, `luaedit` | drive the editor from the console, for scripted runs |
| `edit list\|select\|set\|rot\|radius\|undo\|reset\|origin\|copy\|cursor\|path\|tp ...` | the placement editor: select a thing of the map, move, turn and resize it, print where it is written and its corrected line; the cursor point and the path |

A file of console commands can be run at start with `-consolecmd <file>`; a line that begins with `@N` runs N seconds after the start, one with `@+N` N sixtieths of a second of game time after the line before it. Mods add their own commands with [`sh.console.register`](scripting.md#console-commands-shconsole).

The log is the other half of the console: `log_file = 1` in `config.cfg` writes it into `logs/`. Lines of the mod system begin with `[MODS]`; [troubleshooting.md](troubleshooting.md) lists what they mean.

## Tools outside the game

| Tool | What it is for |
| --- | --- |
| `sh-import` (in the package) | the importer on the command line: `sh-import <image.cue> --out <dir>` |
| [`tools/modding/rig_transfer/`](../../tools/modding/rig_transfer/) | binds your own character model to a skeleton of the game ([rig-transfer.md](rig-transfer.md)) |
| [`tools/translation/`](../../tools/translation/), `tools/scripts/text_po.py`, `tools/scripts/check_translation.py` | build and check a translation ([translation.md](translation.md)) |
