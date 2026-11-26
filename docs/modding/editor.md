# Making a mod in the editor

[Back to the guide](README.md)

The editor (F2, [tools.md](tools.md)) has a panel for building a small mod without writing its files by hand: *Panels > Data > Mod*. It makes the mod's folder, places objects on the current map, draws them in the world and writes them to the mod's files in the usual [layout](data.md#layout-of-a-mod). With the mod switched on, the objects work in the normal game; nothing changes in the game without the mod.

## A new mod

Type an id (Latin letters, digits, `_`, `-`, `.`; see [the mod id](mod-json.md#the-mod-id)) and a name, then *Create*. The panel makes `mods/<id>/mod.json` beside the game with `"scripts": true`, `"gameplay": true` and an `engine` range from this engine's version up to the next major one, switches the mod on in `mods/load_order.json` and binds the panel to the current map. A mod made this way runs from the next start of the game: *Reload mod* says so until then, and the console command `restart` starts the game again.

*Open* takes the id of a mod that is already in `mods/` and reads its objects for the current map. When the editor moves to another map, the panel saves the objects of the map it leaves and reads those of the new one.

## Objects

| Kind | What it does |
| --- | --- |
| `message` | a trigger zone that shows a text. The text is English, typed in the panel; it becomes a new message of the map |
| `script` | a trigger zone that sends the event `<mod id>:<object id>`; its handler is Lua that you write (below) |
| `spawn` | a character of `chara/` (the list in the panel) appears at the object's place, facing its yaw, with the chosen `ai` ([spawning](scripting.md#spawning-characters)) |
| `pickup` | an item lying at the object's place, taken with the action button like the game's own ([pickups](scripting.md#pickups-in-the-world)) |

*Add* arms a kind: the next left click on the world places the object at the clicked point, or *at player* and *at camera* place it where Harry or the free camera stands. Each object has an id, unique on the map, given by the panel (`message1`, `spawn2`, ...).

A selected object is moved:

- with the arrow keys: Left and Right along x, Up and Down along z, Shift+Up and Shift+Down up and down. The *step* slider sets how far one press goes, from 1 cm to 2 m;
- by dragging the x, y, z and yaw fields (*strong drag* moves ten times faster), or by typing a number into them (metres and degrees);
- with *To player*, *To camera* and *To last pick*. *Player here* puts Harry on the object to try it.

Triggers (every kind but `pickup`) also have a radius and a height: the zone is a circle on the ground, and Harry counts as inside when he stands within the radius and his height differs from the object's by at most the height. *activation* is `enter` (Harry walks into the zone), `action` (Harry presses the action button inside it) or, for spawns, `load` (when the map loads). *once per playthrough* keeps a "done" flag of the mod so the trigger fires only once. *flag* is an optional condition: the name of an event flag of the game or of a mod, which must be set (or, with the box cleared, clear) for the object to work. A pickup with a flag is placed only while the condition holds.

The overlay *Mod objects* (menu *Overlays*) draws every object: a cross at its place, a short line along its yaw, the zone's circle and height, its id; the selected one has three axis lines (x red, y green, z blue).

Triggers fire only while the player controls Harry: not during a cutscene, a map event or a message. A `message` stops the world until it is read, like the game's own messages.

## Scripts

For a `script` object, *Create script* adds a handler to `maps/<map>/hooks.lua` of the mod (a [layer](scripting.md#script-layers) over the map's file) and opens the file in the Lua editor:

```lua
-- The script trigger 'lever1' (ev.object, ev.map).
sh.hooks.on("mymod:lever1", function(ev)
    sh.log("mymod: lever1 runs on " .. ev.map)
end, { id = "lever1" })
```

Write what the trigger does inside the function; everything of the map's script host is there ([lua-api.md](lua-api.md)). The panel never changes a handler once it is in the file; *Create script* again only opens the file.

## The files

*Save* writes:

| File | What it holds |
| --- | --- |
| `maps/<map>/editor_objects.json` | the objects of the map: the panel reads them back from here |
| `text/en/editor/<map>.json` | the texts of the map's `message` objects, keys `<mod id>:<map>.<object id>`, as new messages of the map |
| `flags/editor.json` | the flags the objects need: `<id>_taken` of a pickup, `<id>_done` of a trigger fired once |
| `scripts/main.lua` | the runtime that makes the objects work, between the lines `-- editor:begin` and `-- editor:end` |

The runtime is generated from the `editor_objects.json` of every map of the mod. Do not edit it: the next *Save* writes the region again. Code of your own in `scripts/main.lua` outside the two marker lines is kept. Texts can be translated like any text of a mod ([translation.md](translation.md)): a file `text/<language>/editor/<map>.json` with the same keys and the same `"map"`.

`editor_objects.json` is plain JSON and can be edited by hand. Positions, radius and height are Q12 (4096 = 1 m, y grows downwards), yaw a Q12 angle (4096 = a full turn):

```json
{
  "format": "sh-editor-objects",
  "version": 1,
  "map": "map0_s01",
  "objects": [
    {"id": "message1", "kind": "message", "x": 18640, "y": -2464, "z": 1111648, "yaw": 1024, "radius": 4096, "height": 8192, "activation": "enter", "once": false, "text": "Something is written here."},
    {"id": "pickup1", "kind": "pickup", "x": 8400, "y": -2464, "z": 1106732, "yaw": 0, "item": "HealthDrink", "count": 0}
  ]
}
```

Other keys: `flag` and `flag_value` (the condition), `chara` and `ai` (a spawn), `count` (a pickup; 0 is what the game gives for a common pickup).

## Reload mod

*Reload mod* saves and, when the mod is already running, runs `mods reload` ([tools.md](tools.md#reloading-a-mod-while-the-game-runs)): the texts, the flags and `scripts/main.lua` are read again and the current map loads again in place, so the changed objects work at once. A mod that is not running yet (just made, or switched on after the start) needs a restart.

## The console

The panel's actions have console commands, for scripted runs (`-consolecmd`):

| Command | What it does |
| --- | --- |
| `modnew <id> [name]` | makes and switches on a new mod bound to the current map |
| `modobj open <id>` | opens a mod of `mods/` |
| `modobj add <kind> [player\|camera\|pick\|<x> <y> <z>] [id]` | adds an object at Harry, the camera, the last pick or a point in metres, and selects it |
| `modobj select <id>`, `modobj del` | selects an object, deletes the selected one |
| `modobj move <dx> <dy> <dz>` | moves the selected object, in metres |
| `modobj set <field> <value>` | `x`, `y`, `z`, `radius`, `height` (metres), `yaw` (degrees), `activation`, `once`, `flag` (`-` clears it), `flag_value`, `text`, `chara`, `ai`, `item`, `count` |
| `modobj list`, `modobj save`, `modobj script`, `modobj reload` | lists the objects, saves, creates the handler of the selected script object, reloads |

```
modnew lanterns Lanterns
modobj add message player
modobj move 2 0 0
modobj set text The lantern is cold.
modobj save
restart
```

A script can show a map message the same way: `sh.world.message(key)` ([lua-api.md](lua-api.md)).

## Placing things exactly

*Panels > World > Placement* finds the exact place of anything a mod puts into the world and tries a model there before a line of data is written.

1. Tick *click sets the point* and click the world: the point is where the click hits the drawn geometry (metres; Y grows downwards, so a negative Y is above the ground), with its 40 m cell (the `zones` entry of a world object) and the room. *at player* takes the player's place instead. The arrow keys move the point along x and z, Shift+Up and Shift+Down up and down, by the *arrow step*; the fields take typed numbers. *snap to ground* puts it on the floor of the loaded collision, *player here* moves the player onto it.
2. Type a model into *model*: a world model name of the map (`BULLET_N`, at most 8 characters), an item model file (`ITEM/UNQ41.TMD`) or a mod's own glb below `convert/models/` (`mymod/bike.glb`). With *draw it* ticked the model stands at the point in the world, lit and fogged like the map's own objects, while the editor is open. Turn it about x, y and z (degrees; the ±90 buttons lay a model on its side), and scale a file model.
3. *Copy* puts the place on the clipboard as a `world_objects.json` object, a `sh.world.pickup_create` call or a Lua table row `{ x, z }`, with the exact numbers shown.

Look at a placed model from several sides (fly the free camera around it) before you copy it: a model should neither float above the ground nor sink into it.

The console command `placement <x> <y> <z> [model] [rx] [ry] [rz] [scale]` sets the panel's point and try-out from a script (`-consolecmd`) and prints the `world_objects.json` entry; `editorcam` and `editorshot` then frame and capture it.
