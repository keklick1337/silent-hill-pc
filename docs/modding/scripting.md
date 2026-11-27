# Scripting: Lua in a mod

[Back to the guide](README.md)

**`"scripts": true` in `mod.json` is required for any Lua of a mod.** Without it the engine does not see a single `.lua` file of the mod; the log says once `mod X has Lua files but mod.json does not say "scripts": true; its scripts are not run`. The mod's other files (JSON, pictures, sounds) still apply.

## Where Lua lives in a mod

| File | What it is |
| --- | --- |
| `scripts/main.lua` | the mod's own script, in the game host. Every mod has its own environment: one mod's globals are invisible to other mods and to the game's script |
| `maps/<map>/events.lua`, `world_objects.lua`, `hooks.lua`; `scripts/map_hooks.lua`, `scripts/map_rooms.lua`; the Lua of `chara/` | [layers](#script-layers) over the game's files of the same path |
| `menus/*.lua` | [menus](#menus-and-option-pages) |

## The sandbox

The same in every script host. Available: the base library, `table`, `string` (without `string.dump`), `math`, `coroutine`; `print` writes to the log like `sh.log`; and the helpers `wait`, `wait_frames`, `wait_until`, `say`, `wait_fade`, `load_map`, `fmv`.

Not available (`nil`): `io`, `os`, `package`, `require`, `debug`, `load`, `loadstring`, `loadfile`, `dofile`, `collectgarbage`, `utf8`. A script cannot read or write files, load code or touch the system. Lua files are loaded as text only; bytecode is refused. Every call from the engine into a script has an instruction budget, so a script that loops forever is stopped with an error instead of hanging the game.

Each mod gets its own copies of `string`, `table`, `math`, `coroutine` and `sh`: changing them in one mod does not affect another.

## scripts/main.lua

The game host is created once when the scripts start and lives until the game exits; it is not recreated per map. First the game's `gamedata/scripts/game.lua` runs, then the `main.lua` of every mod in load order. An error in a mod's `main.lua` is logged (`mod <id>: <path>: <error> (the mod's hooks are not called)`) and that mod drops out; the game and the other mods go on.

The global table `mod`:

| Field | Meaning |
| --- | --- |
| `mod.id`, `mod.name`, `mod.version` | from `mod.json` |
| `mod.dir` | the mod's folder |
| `mod.storage` | a table for the whole session: it survives map changes and is not saved |
| `mod.export(api)` | publishes a table for other mods ([below](#talking-to-other-mods)) |

Lifecycle functions a mod may define:

| Function | When |
| --- | --- |
| `on_boot()` | once after all scripts have run: register console commands, set initial values |
| `on_new_game()` | a new game starts |
| `on_map_load(map)` | every map load; `map` is the name (`"map1_s02"`) |
| `on_update()` | every frame of the main loop, menus and the title screen included; keep it light. The 30 Hz tick of the game itself is the event `game.tick` |

The game's function runs first, then the mods' in load order. An error in such a function is logged once (`mod <id>: <hook> error: ... (the hook is off for this mod)`) and that function of that mod is not called again.

For reacting to the game, prefer [events](events.md) (`sh.hooks.on`): any number of mods can listen to one event, a handler can change or cancel it, and nothing of the game is overwritten.

Coroutines: `sh.spawn(fn)` starts one; inside it `wait(seconds)`, `wait_frames(n)` and `wait_until(fn)` yield until the time has passed. An error in a coroutine is logged and ends only that coroutine.

## The API

The table `sh` is described function by function in the [Lua API reference](lua-api.md). New mods write the named forms `sh.<group>.<name>` (`sh.flag.get`, `sh.item.give`, `sh.player.pos`); the flat names (`sh.flag_get`) are working aliases of some of them. What a mod uses most:

| | |
| --- | --- |
| log | `sh.log([level,] text)`; level 1 is a warning |
| flags | `sh.flag.get(name)`, `sh.flag.set(name, value)`, `sh.flag.clear(name)` |
| inventory | `sh.item.give(item[, n])`, `sh.item.count(item)`, `sh.item.has(item)`, `sh.item.take(item[, n])` |
| the player | `sh.player.pos()` → x, y, z; `sh.player.set_pos(x, y, z)`; `sh.player.facing()` |
| the game | `sh.game.difficulty()`, `sh.game.state()`, `sh.game.cutscene()`, `sh.map.name()` |
| the world | `sh.world.walkable(x, z[, from_x, from_z])`, `sh.world.nearest_walkable(...)`, `sh.world.ground_height(x, z)` |
| characters | `sh.entity.spawn{chara=, x=, z=, rot=, ai=}` → slot, or `nil, error` |
| pickups | `sh.world.pickup_create{...}`, `sh.world.pickup_remove(id)`, `sh.world.pickups()` |
| sound | `sh.sound.play(name, ...)`, `sh.sound.stop(name)` |
| text | `sh.text(key)` |
| events | `sh.hooks.on`, `sh.hooks.off`, `sh.hooks.emit` ([events.md](events.md)) |
| settings, console, menus | `sh.config`, `sh.console`, `sh.menu` (below) |

Rules of the API:

- **Units.** Positions are Q12 (4096 = 1 m, Y grows downwards), angles Q12 (4096 = a full turn; "forward" at angle `a` is `(sin a, cos a)`), times Q12 seconds, health Q12.
- **Integers.** Arguments of the named API are integers; a boolean reads as 0 or 1; a fraction where an integer is expected is an error — round with `math.floor`. `nil` is 0.
- **A character** is a slot number: 0 the player, 1 and up the other characters. A character *type* is the name of a folder of `chara/`.
- **Errors.** A wrong argument or name is a Lua error with a text (`sh.map.get: unknown map variable 'x'`). A function that belongs to one map answers on another map with `'<name>' is not available on this map`.

### Spawning characters

`sh.entity.spawn{chara=, x=, z=, rot=0, ai="auto", state=-1}` puts a character at `x, z` (the game sets the height) facing `rot`, and returns its slot, or `nil` and an error text (`unknown character`, `unknown ai`, `not in game`, `<chara> cannot be spawned here`). `ai` is `"auto"` (as the map decides), `"full"` (the character's own behaviour on any map), `"fallback"` (the shared fallback behaviour) or `"none"` (a statue). The spawn sends the event `entity.spawn` on the next tick. A monster spawned this way makes the pocket radio crackle like the map's own monsters.

`sh.world.walkable(x, z[, from_x, from_z])` returns whether loaded ground is at the point and its height; with `from` it also requires that the height differs by at most 0.75 m from the ground at `from` and that no wall lies between the two — a place one can walk to. `sh.world.nearest_walkable(x, z[, radius[, from_x, from_z]])` returns the nearest such point (`x, z, y`) within `radius` (default 2 m, at most 16 m), or `nil`.

### Pickups in the world

`sh.world.pickup_create{item=, x=, y=, z=, rot=, id=, flag=, count=, message=, model=}` places a real pickup on the loaded map and returns its id, or `nil` and an error.

- `item` is an item name; a short name is looked up in the calling mod first.
- `x`, `y`, `z` are Q12; without `y` the item is put on the ground under `x, z`. `rot` is a Q12 angle about the vertical axis, or a table `{x, y, z}`.
- `flag` is required: the "taken" flag from the mod's `flags.json`. A set flag hides the pickup for good.
- `id` is unique on the map; a bare name gets the mod's namespace.
- `count` defaults to what the game gives for a common pickup; `message` is a message key for the question, needed for an item that has no common question; `model` is the world model, by default the item's `world_model`.
- `model = false` draws nothing for the pickup: the mod shows the thing itself, as a world object of its own model ([assets.md](assets.md#world-objects-from-a-mods-model)) whose `flags_clear` names the pickup's flag, so the model goes when it is taken.

Pickups live while the map is loaded: every map load starts from an empty list, so create them on every load (`on_map_load` or the event `map.load`). The pickup works like the game's own: the action button, facing the item within 0.8 m, the usual question, the events `item.pickup` and `item.picked`. Up to 64 pickups per map.

`sh.world.pickup_take(id)` opens a pickup's question at once, wherever the player stands: the end of a scene hands the player its item with the game's own prompt (the item's model, Yes and No). It starts as soon as no map event runs and the player is free; the pickup's flag tells afterwards whether it was taken.

## A story mod: playing as another character

A mod can tell its own story with the game's maps. What it uses:

- **The start.** `config/difficulties.json` `"start_map"` of each difficulty names the map a New Game begins on (it wins over the `map` of `config.cfg`); `menu/title_menu.json` with `{"$keys": {"entries": "kind"}, "entries": [{"kind": "new_game", "next": "MainLoadScreen"}]}` leaves out the opening movie; `on_new_game` / `game.new_game` set the first flags.
- **The player.** `sh.player.set_character("Cybil")` dresses the player in the character's `"as_player"` model and portrait ([data.md](data.md#type-properties)); `sh.player.set_character(nil)` gives Harry back. Harry's controls, animations, weapons and sounds stay: the clips of another character are not the player's set. Not saved: call it again on `map.load` from the mod's flags.
- **Who stands where.** `sh.chara.model_override(type, glb)` dresses every character of a type in another model (Harry as the actor of a scene that had Cybil).
- **Going places.** `load_map(map, point)` from `main.lua` goes through the game's door path; a landing point the map does not have can be added by changing an unused point of `maps/<map>/map.json`.
- **Lines.** `sh.world.say(key)` shows a message of a map over play: the world goes on, the player can walk, the confirm button advances it; `sh.world.saying()` tells whether it still shows and `sh.world.hush()` ends it. A line is voiced by a file named after its key, `XA/msg_<mod>_<name>.opus` ([assets.md](assets.md#voices-of-messages)): the voice starts half a second after the line comes up and the line stays until half a second after it ends. `sh.world.message(key)` shows one the way a map's message event does, the world stopped (`sh.game.state() == "ReadMessage"` while it shows). A mod writes its lines once per language in `text/<lang>/messages.json`, messages of no map that every map can show ([assets.md](assets.md#text)); `text/<lang>/maps/<map>.json` that says `"map"` adds a message to one map only.
- **Doors and events.** A `maps/<map>/map.json` patch gives an event (`entities`, by `id`) a `"required_flag"` of the mod to shut it; `map.event` cancels a handler of the game. A patch applies to every game played with the mod on, also to a save of the game's own story: a flag the mod never sets shuts the event there too. To shut it in the mod's story only, a script sets the flag while the game played is not that story (the example's `harrys_game`), and events the game already keeps behind a flag of its own need no patch.
- **Scenes of its own.** A mod's `maps/<map>/handlers.json` adds a slot after the game's (merged by `index`, the game's slots stay) naming a function of its `maps/<map>/events.lua`; `sh.map_event_set(10, index, 0, -1)` starts it as an event of the map, which runs it every tick with the event steppers of the game (`sh.event_step_get`, `sh.event_wait_player_stop`, `sh.chara_anim_play_to_end`, `sh.overlay_screen_set`, `sh.event_msg`, fades) until it calls `sh.state_set(0)`. Inside a running event, `load_map(map, point)` followed by `sh.state_set(5)` hands the event straight to the map change, with no frame of play between. `sh.event_waypoint_set` / `sh.event_waypoint_player("walk_forward", n)` walk the player along points on any map whose player cutscene states include the walk and the turns (`harry_map_states` of `chara/Harry/map_states.lua`; a layer of `chara/Harry/behavior.lua` adds them for a map).
- **An event without a map handler.** `sh.event.run(fn)` from `main.lua` runs a function of the script as an event: the game calls it every tick in the event state, with the same steppers and the same drawing as a handler of a map (a paper map shown full screen with black round it, sheet after sheet; the player walked a few steps back from a way that is shut), on any map and with no `handlers.json`. It ends when the function returns `true`; a coroutine waits with `sh.event.running()`. It starts only while the player is not frozen.
- **Map marks and save points.** `menu/map_screen.json` takes marks of the mod (`"$append"` to a page's `marks`, the mod's own flags); `menu/save_screen.json` takes save point names ([data.md](data.md#game-settings)).
- **Sounds of the story.** A mod's sound with a file (`sounds/<any>.json`, [assets.md](assets.md)) is played with `sh.sound.play(name, 0, 0)` and stopped with `sh.sound.stop(name)`; `sh.sound.fade(name, level, seconds)` moves its level evenly in the mix (a fade-in from silence, a level that follows a distance tick by tick, a fade-out). `sh.sound.swap(sound, file)` makes every call of one of the game's sounds play a file of the mod while it is set; `sh.sound.swap(sound)` gives the game's sound back (not saved: set it again on `map.load`). The radio's noise can be a script's own: a handler of `radio.static` that cancels keeps the game's static silent, and the script sounds what it wants for the monsters it chooses (the example's walkie-talkie: louder the nearer a monster is, in and out softly).
- **A light for a character.** `sh.light.carry(id, {slot = n, bone = 1, position = {x, y, z}, direction = {x, y, z}, cone = {inner, outer}, color = {r, g, b}, intensity = 1.5, range = 9})` gives the character in a slot (0: the player) a light on one of its bones: a second flashlight for the other character of a scene, a lantern in a hand. It follows the bone as the animation moves it, through cuts too, until `sh.light.drop(id)` or a map change. Metres and degrees in the bone's own space (y down), colour and range as a light of `maps/<map>/lights.json` ([data.md](data.md#maps)); `bone` is an index of the character's skeleton or a bone name of the player's skeleton (`"Torso"`). It is part of the picture only (the game's rules never read it) and is shaded per pixel by every renderer, whatever the light settings; ray tracing with `rt_lights` gives it shadows, elsewhere it has none of its own (the player's flashlight keeps the shadow map). The example gives Harry one in Cybil's scene at Green Lion Antiques (`maps/map4_s01/events.lua`).
- **What the tools show.** `sh.world.mark(id, {x, z, r, label, kind})` puts a place of the story (a trigger of the script, a save point, a monster waiting to come) into the modding tools' overlay (F2, *Scripts*), so the editor shows the story as it runs; the marks go at a map change.
- **Remembering.** Every flag of a mod is saved by name, as many as the story needs: a flag per beat reached, per item taken, per monster killed keeps all of it across a load.

## Script layers

A layer is a mod's file at the path of a Lua file of the game:

- maps: `maps/<map>/events.lua`, `maps/<map>/hooks.lua`, `maps/<map>/world_objects.lua`, `scripts/map_hooks.lua`, `scripts/map_rooms.lua`;
- characters: `chara/common.lua`, `combat.lua`, `fallbacks.lua`, `chara/<Name>/behavior.lua`, `chara/<Name>/actions.lua`, `chara/fallback_ai.lua`, `chara/<Name>/fallback_ai.lua`.

The game's file runs in the host's global table. Then the copy of the same path of every active mod runs, in load order, each in its own environment. A layer's environment reads the game's globals, and has `mod` and its own `sh.hooks`.

- **`extend("Name", function(base, ...) ... return base(...) end)`** wraps a function by name. `base` is the definition visible before this layer: its own, else the last earlier layer's, else the game's. Mods chain: game → mod A → mod B. When there is nothing to wrap, the log warns and `base` does nothing.
- **A new function** (`function MapEvent_MyThing() ... end`) is a new handler or hook.
- The engine looks a function up by name (map event handlers, the functions of `hooks.lua` and `world_objects.lua`, a character's `update(slot)`, the hooks of `actions.lua`) first in the last layer that has it, then in the game.
- `on_boot`, `on_new_game`, `on_map_load` and `on_update` are called in every layer, the game's first. Do not wrap them with `extend`: the game's version would run twice.
- A limit: calls *inside* the game's file by a global name still go to the game's function. `extend` changes what the engine calls and what later layers see.
- A layer that fails to load is logged (`mod X: <path>: ... (the extension is left out)`) and skipped.

```lua
-- mods/knife_note/maps/map0_s01/events.lua: a layer over the cafe's events.lua
extend("MapEvent_KitchenKnifeItemTake", function(base, ...)
    sh.log("knife_note: Harry looks at the kitchen knife")
    return base(...)
end)
```

The names of a map's handlers are the `name` fields of `maps/<map>/handlers.json`; the engine calls the function of that name every tick while the event runs. A handler is always a Lua function named this way: the engine looks up no function of its own by name. A mod gives a slot a handler of its own by patching the slot (`index`) with a readable `name` and `symbol` (the same text; `symbol` is only the fallback name an older script may use) and a `shape` (`item`, `cutscene` or `script`), and defining that global function in its `events.lua` layer:

```json
{"handlers": [{"index": 12, "symbol": "MapEvent_KnifeNote", "name": "MapEvent_KnifeNote", "shape": "script"}]}
```

Address names (`func_800…`, `sharedFunc_…`, `D_800…`) are refused by the checks. A slot whose name no `events.lua` of the map defines is logged once when the map loads (`maps/<map>/handlers.json: slot N names the handler 'X', which no events.lua of the map defines`) and does nothing when its event runs.

**Whole replacement** is explicit: `"replace": ["maps/map0_s01/events.lua"]` in `mod.json`. The mod's copy runs instead of the game's file. Layers of other mods stay layers over the replaced file. It is the last resort: a layer or an event handler stacks with other mods, a replacement does not.

## Menus and option pages

`menus/*.lua` of a mod run after the game's menus (`gamedata/menus/`). They run in **one** Lua state shared by all menu files, without per-mod environments: write `local`. That state has `sh.menu`, `sh.config`, `sh.mods` and `sh.log`, and no `sh.hooks`. Labels are text keys of the mod; a string that is not a key is shown as it is.

| Function | What it does |
| --- | --- |
| `sh.menu.define{ id, title, rows, on_open?, on_update?, leave_confirm?, on_leave? }` | describes a menu. A second `define` with the same `id` replaces it. `on_update` runs every frame while the menu is the current one: a page that waits for something (the Import page waits for the file dialog) checks there and may `define` itself again |
| `sh.menu.append_rows(id, rows)` | adds rows to an existing menu; `false` when there is no such menu. Rows whose `id` is `tail` or begins with `tail.` stay last |
| `sh.menu.add_page{ parent, id, title, rows, label?, help? }` | describes a page and puts a link to it into the menu `parent`. With `parent = "options"` the link goes onto the page *Options > Mod Options* |
| `sh.menu.open(id)`, `sh.menu.close()`, `sh.menu.is_open()` | open a menu as a screen, close it, ask |
| `sh.menu.after_load(fn)` | `fn` runs once when all menu files have run |

A row is a table with `kind`, `id`, `label`, `help`, and optionally `visible` and `enabled` (a function or a boolean):

| `kind` | What it does | Fields |
| --- | --- | --- |
| `action` | confirming runs `activate` | `activate`, `close = true`, `confirm` (a question asked first) |
| `toggle` | left, right and confirm flip a boolean | `get`, `set` |
| `slider` | left and right move a number by a step | `min`, `max`, `step`, `integer`, `percent`, `get`, `set` |
| `number` | an integer; PageUp and PageDown take a big step | `min`, `max`, `step`, `get`, `set` |
| `choice` | left and right go through the choices | `choices`, `get`, `set` |
| `dropdown` | confirm opens a scrolling list | `choices`, `get`, `set` |
| `submenu` | confirm opens another menu | `submenu` |
| `setting` | the game does the step itself and the value is ready text (rows of the engine's own settings) | `step`, `text` |
| `info` | a read-only line | `text` or `get`, `small` (a note), `cut`, `args` |

`choices` is a list of `{ value = "x", label = "key" }` or of plain strings. A row with `config = "<mod>.<key>"` is bound to a [setting](#mod-settings-shconfig) and needs no `get` and `set`. A row can be shown by condition: `visible_if = { key = "<mod>.<key>", is = "value" }` (also `any = {...}`, `none = {...}`).

Read-only lines that explain the rows around them (a version, a path, a count, a long description) are notes: an `info` row with `small = true` is drawn in smaller, dimmer text about two thirds of a row, with a tighter pitch, left-aligned with the labels; its label and its value (`text` or `get`) read as one line, and it takes no part in the value column, so a page of notes neither shrinks nor scrolls more than it must. How a text that is too long for its room is shown is the row's `cut`: `"pan"` scrolls it to its end and back (the default of a full row), `"end"` ends it in "..." (the default of a note), `"middle"` keeps its start and its end with "..." between, `"path"` shows only the last component of a path (a picked file's name rather than where it lies), `"wrap"` breaks a note into as many lines as it needs. `cut` works on any row: a `toggle` or `choice` whose label is data (a mod's name) and has `cut = "end"` ends in "..." where it meets the value column, and a page with such rows moves its value column as far right as the values let it, so the labels get the room. `args = { name = "value" }` puts data text into the label's text where it has `{name}`, after it is translated, so every language orders the parts itself (`gamedata/text/<lang>/*.json`: `"mymod.found": "{count} found in {place}"`). After a page defines itself again, `sh.menu.focus(menu_id, row_id)` brings one of its rows to the top of the window (the first row from there that can be selected is selected): the Import mods page of `gamedata/menus/mods.lua` shows what an import did that way.

```lua
rows = {
    { kind = "toggle", id = "pack", label = pack.name, cut = "end", get = pack_on, set = set_pack },
    { kind = "info", small = true, label = "mymod.version_line", args = { version = pack.version } },
    { kind = "info", small = true, label = "mymod.source", text = pack.path, cut = "path" },
    { kind = "info", small = true, label = pack.description, cut = "wrap" },
}
```

Phones and tablets have two row flags of their own. `mobile = false` hides a row there (the pause menu's Controls row, the Options rows of the window and the keyboard), `mobile = true` shows it only there (the same as `visible_if` on the key `mobile`, which is 1 on a phone or tablet). `advanced = true` moves a row, on a phone or tablet only, to its page's Advanced page: the engine makes the page `<page id>.advanced` (title `ui.menu.advanced`) from the page's advanced rows and its group headers, in their order, and adds a row at the page's end (before the `tail.*` rows) that opens it there; on a desktop the row stays where it is and no such row shows. So a page keeps the settings most players change in front on a small screen, and nothing is lost: the Options pages of `gamedata/menus/options.lua` mark every other row so. Rows added later with `sh.menu.append_rows` follow the same flags. A row can carry both (`mobile = true, advanced = true`: on the Advanced page, and only on a phone or tablet).

```lua
rows = {
    { kind = "toggle", id = "speed", label = "mymod.speed", config = "mymod.speed" },
    { kind = "slider", id = "fine", label = "mymod.fine", config = "mymod.fine", advanced = true },
    { kind = "action", id = "keys", label = "mymod.keys", native = "options.edit_keybinds", mobile = false },
}
```

On a touch screen every row works by the finger without anything more in its definition: the list follows a drag and glides on, a tap picks the row, a tap on a `toggle`, `choice` or `setting` row (one whose `texts` lists its values) opens the list of its values to pick from, a `slider` or a bar of a `setting` is set where it is tapped or dragged sideways.

An option page with settings:

```lua
-- mods/mymod/menus/options.lua
sh.config.declare{ key = "mymod.enabled", type = "bool", default = true }
sh.config.declare{ key = "mymod.volume", type = "number", default = 0.8, min = 0, max = 1, step = 0.05 }
sh.config.declare{ key = "mymod.mode", type = "choice", default = "normal",
  choices = { { value = "calm", label = "Calm" }, { value = "normal", label = "Normal" } } }

sh.menu.add_page{ parent = "options", id = "mymod:options", title = "My Mod", rows = {
  { kind = "toggle", id = "mymod:enabled", label = "Enabled", config = "mymod.enabled" },
  { kind = "slider", id = "mymod:volume", label = "Volume", config = "mymod.volume", percent = true },
  { kind = "choice", id = "mymod:mode", label = "Mode", config = "mymod.mode" },
}}
```

Working examples: [`examples/mods/example_mod/menus/example.lua`](../../examples/mods/example_mod/menus/example.lua) (a menu reached from the pause menu) and [`examples/mods/ambush_dogs/menus/ambush_dogs.lua`](../../examples/mods/ambush_dogs/menus/ambush_dogs.lua) (an option page). The console command `debugmenu <menu id>` opens any menu.

## Mod settings: sh.config

A mod's settings are a store of the engine of their own, not `config.cfg`. They are kept in `mods_config.cfg` beside `config.cfg` and written at every change.

A key is `<mod id>.<key>`: letters, digits, `_` and dots.

| Function | What it does |
| --- | --- |
| `sh.config.declare{ key, type, default?, min?, max?, step?, choices?, restart? }` | declares a setting. `type` is `"number"` (the default), `"bool"`, `"choice"` or `"string"`. A value saved in the file wins over `default` |
| `sh.config.get(key)` | the current value; `nil` for an undeclared key |
| `sh.config.set(key, value)` | checks type and range (out of range is refused, not clamped), writes the file |
| `sh.config.default(key)`, `sh.config.reset(key)` | the default; back to it |
| `sh.config.on_change(key, fn)` | `fn(value)` after every change. Menu scripts only |
| `sh.config.needs_restart()` | `true` while a setting declared with `restart = true` differs from its value at start |

Declare settings in a menu file: menu scripts run before the game scripts, so the keys exist by `on_boot`. The game script reads them where it needs them:

```lua
-- mods/mymod/scripts/main.lua
function on_map_load(map)
    if sh.config.get("mymod.enabled") then
        sh.log("mymod: mode " .. sh.config.get("mymod.mode"))
    end
end
```

This is also how a menu talks to the game script: the two run in different Lua states and share only the settings.

## Console commands: sh.console

A mod adds commands to the same registry as the engine's, so they show up in `help` and in the completion of the input line. Register them in `on_boot`.

```lua
sh.console.register{
    name = "spawn2", aliases = {"s2"}, group = "mods",
    help = "one line", details = "longer text for `help spawn2`",
    args = {
        {name = "character", type = "string", help = "...",
         complete = function(prefix) return {"Lisa", {text = "Cybil", note = "cop"}} end},
        {name = "distance", type = "number", optional = true},
        {name = "mode", type = "enum", values = {"a", "b"}, optional = true},
    },
    run = function(args)
        return "spawned " .. args.character      -- a string or a table of strings (optional)
    end,
}
```

- Argument types: `string`, `int`, `float` (or `number`), `bool`, `enum` (needs `values`), `rest` (the rest of the line, last only). Required arguments come before optional ones.
- `run(args)`: `args[1]…` are the values in Lua types, `args.n` the count of words given, `args.<name>` the same by argument name. Returned strings are printed. An error in `run` is printed as a red line.
- `sh.console.print(text[, level])` prints a line (`"info"`, `"warn"`, `"error"`, `"plain"`); `sh.console.exec(line)` runs a console line; `sh.console.unregister(name)` removes the mod's own command.
- A name or alias that is taken by someone else is an error of `register`.

## Talking to other mods

Globals of mods are separate. Mods talk explicitly:

- `mod.export(table)` publishes a table (one per mod and Lua host).
- `sh.mods.get(id)` returns `{id, name, version, api}` for an active mod and `nil` for one that is not active; `api` is what that mod exported in the same host.

```lua
-- mods/lib_spawn/scripts/main.lua
mod.export{ behind = function(dist) --[[ ... ]] end }

-- mods/user/scripts/main.lua; mod.json: "optional_dependencies": {"lib_spawn": "^1.0"}
local lib = sh.mods.get("lib_spawn")
if lib and lib.api then lib.api.behind(4 * 4096) end
```

Take the table while loading only when the other mod is in your `dependencies` or `optional_dependencies`: only that guarantees it loaded and exported first. From a handler or from `on_boot` it works in any order, because all `main.lua` files have run by then. A mod can also announce things with an event of its own: `sh.hooks.emit("<mod id>:<name>", {...})`.

## Tests

A mod may keep Lua tests in `tests/*.lua`. They are not part of the game: nothing runs them at start, the modding tools do (Lua editor > Tests tab > Run tests, or the console `luaedit test <mod id>`). Each file runs in the live game script state with the whole `sh.*` API, in a table of its own: what the file defines does not leak into the game's globals, and the game's globals are seen through it. Changes a test makes to the running game (flags, items, map variables) stay, so a test that changes something puts it back.

- A test is a global function whose name starts with `test_` (run in name order), or a function given to `test("name", fn)` (run first, in the order given).
- A test passes when it returns and fails when it raises an error. Besides `assert` and `error` there are `expect(cond[, message])`, `expect_eq(got, want[, message])` (the message says what was expected and what came) and `expect_error(fn[, message])` (fails when `fn` does not raise).
- A file that does not compile or fails at its top level runs no test and is reported as such. What the tests print is listed under the file.

```lua
-- mods/my_mod/tests/flags_test.lua
function test_door_flag_round_trip()
    local before = sh.flag_get("my_mod:door_opened")
    sh.flag_set("my_mod:door_opened", not before)
    expect_eq(sh.flag_get("my_mod:door_opened"), not before, "flag_set")
    sh.flag_set("my_mod:door_opened", before)
end

test("unknown flags are errors", function()
    expect_error(function() sh.flag_get("my_mod:no_such_flag") end)
end)
```

The result of each test goes to the log too (`[LUATEST] PASS ...`, `[LUATEST] FAIL ...: <error>`). `examples/mods/example_mod/tests/` has a working file.

## How mods stack

From the gentlest means to the harshest:

1. **Event handlers** (`sh.hooks.on`): react to an event, change its fields or cancel it. No file of the game is touched.
2. **Script layers**: extend a Lua file of the game (`extend`, new functions).
3. **Data merging**: a JSON patch by fields and record keys ([data.md](data.md#merging-json-data)).
4. **An API between mods**: `mod.export` and `sh.mods.get`.
5. **Whole replacement**: only explicitly through `"replace"` in `mod.json`; two replacements of one file conflict.

The gentler the means, the better a mod lives with others.
