# Getting started

[Back to the guide](README.md)

## Installing a mod

1. Copy the mod's folder into `mods/` beside the game's executable, so that `mods/<folder>/mod.json` exists.
2. Start the game. On the title screen open *Options > Mods*, pick the mod and switch it on. A newly installed mod is off until you do.
3. Leave the page and answer "Yes" to the restart question. The set of mods changes when the game starts.

The same from the console (`` ` ``): `mods` lists the mods with their state, `mods enable <id>` and `mods disable <id>` switch one, `mods up <id>` and `mods down <id>` move it in the load order. Mods can be changed only from the title screen's *Options*; in the pause menu the page is read-only, because a running game keeps the set it started with.

To try the examples of the repository, copy any folder of [`examples/mods/`](../../examples/mods/) into `mods/`.

## Before you start: look at the game's data

Run the game once with your disc image. The first start imports the disc into `gamedata/` beside the executable:

| Folder | What is in it |
| --- | --- |
| `gamedata/convert/textures/` | every picture of the game as PNG, by the disc's folder and file name |
| `gamedata/convert/models/` | models as glTF (`.glb`) with a `.model.json` beside each |
| `gamedata/convert/audio/` | sound effects, music and speech as WAV with JSON descriptions |
| `gamedata/convert/anim/`, `convert/cutscenes/` | animations and cutscenes as JSON |
| `gamedata/raw/` | the disc's files as they are |
| `gamedata/items/`, `weapons/`, `chara/`, `maps/`, `config/`, `flags/`, `text/`, `menus/`, `sounds/` | the game itself as data and Lua: the same files as [`gamedata/`](../../gamedata/) of the repository |

A mod is written against these files: you put a file at the same path in your mod.

Two settings in `config.cfg` (beside the executable) help while you work:

- `modding_tools = 1` switches on the built-in editor, opened with F2 ([tools.md](tools.md));
- `log_file = 1` writes a log into `logs/`; lines of the mod system begin with `[MODS]`.

## Your first mod: a line of text

This mod changes one label of the pause menu. It needs no Lua and does not change the gameplay.

1. Make the folder `mods/my_first_mod/` and put this `mod.json` in it:

   ```json
   {
     "id": "my_first_mod",
     "name": "My first mod",
     "version": "1.0.0",
     "authors": ["Me"],
     "min_engine_version": "1.0.0"
   }
   ```

   `id` is the mod's name for the engine and its namespace; `version` is the mod's own version. Write into `min_engine_version` the engine version you made the mod on (`SilentHillPC --version` prints it).

2. Add `mods/my_first_mod/text/en/my_first_mod.json`:

   ```json
   {
     "format": "sh-text",
     "version": 1,
     "lang": "en",
     "strings": {
       "ui.pause.continue": "KEEP GOING"
     }
   }
   ```

   The file name is free. The key `ui.pause.continue` is a key of the game's own text (`gamedata/text/en/ui.json`); a mod's value for a key wins over the game's.

3. Start the game, switch the mod on in *Options > Mods*, restart, start a game and pause it.

4. Check what the engine made of the mod: the console command `mods files my_first_mod` lists every file of the mod as `replace` (the game has a file at that path) or `add` (a new path). After editing a file of an active mod, `mods reload` applies the change without a restart.

## Next steps

Each step below is a complete small mod. The pages linked explain the rules behind it.

### Replace a texture

```text
mods/hd_harry/mod.json
mods/hd_harry/convert/textures/CHARA/HERO.png
```

The picture has the name the import gave it (`gamedata/convert/textures/CHARA/HERO.png`) and may be of any size: it is stretched over the original. The example [`examples/mods/hd_harry/`](../../examples/mods/hd_harry/) is this mod without the picture, which you supply. Details: [assets.md](assets.md#textures).

### Add an item

```text
mods/crowbar/mod.json
mods/crowbar/items/Crowbar.json
mods/crowbar/text/en/items.json
```

`mod.json`:

```json
{ "id": "crowbar", "version": "1.0.0", "gameplay": true }
```

`items/Crowbar.json` — a name the game does not have makes a new item, `crowbar:Crowbar`:

```json
{ "group": "puzzle" }
```

`text/en/items.json` — its name and description are text:

```json
{
  "format": "sh-text",
  "version": 1,
  "lang": "en",
  "strings": {
    "item.crowbar:Crowbar.name": "Crowbar",
    "item.crowbar:Crowbar.desc": "A heavy crowbar."
  }
}
```

In the game: `give crowbar:Crowbar` in the console, or `sh.item.give("crowbar:Crowbar")` from a script. An item is gameplay data, hence `"gameplay": true`. Details: [data.md](data.md#items-and-weapons).

### Add a console command

```text
mods/hello/mod.json            {"id": "hello", "version": "1.0.0", "scripts": true}
mods/hello/scripts/main.lua
```

```lua
function on_boot()
    sh.console.register{
        name = "hello", help = "say hello",
        args = {{name = "who", type = "string", optional = true}},
        run = function(args) return "hello, " .. (args.who or mod.name) end,
    }
end
```

`scripts/main.lua` runs only because `mod.json` says `"scripts": true`. Details: [scripting.md](scripting.md#console-commands-shconsole).

### React to the game: an ambush

The mod listens to a game event and spawns an enemy behind Harry. It touches no file of the game.

```text
mods/my_ambush/mod.json     {"id": "my_ambush", "version": "1.0.0", "scripts": true, "gameplay": true}
mods/my_ambush/flags.json   {"format": "sh-flags", "version": 1, "flags": {"done": {}}}
mods/my_ambush/scripts/main.lua
```

```lua
-- Positions and angles are Q12 integers: 4096 = 1 m, 4096 = a full turn.
local BEHIND = 4 * 4096

sh.hooks.on("item.picked", function(ev)
    if ev.map ~= "map2_s00" or ev.flag ~= "M2S00_HandgunBullets11" then return end
    if sh.flag.get("my_ambush:done") then return end      -- once per playthrough, saved by name
    local px, py, pz = sh.player.pos()
    local facing = sh.player.facing()
    local a = (facing + 2048) * 2 * math.pi / 4096         -- half a turn: behind Harry
    local x = px + math.floor(math.sin(a) * BEHIND)
    local z = pz + math.floor(math.cos(a) * BEHIND)
    -- the nearest point Harry could walk to from where he stands
    local sx, sz = sh.world.nearest_walkable(x, z, 2 * 4096, px, pz)
    if not sx then return end
    local slot, err = sh.entity.spawn{chara = "Groaner", x = sx, z = sz, rot = facing, ai = "full"}
    if not slot then sh.log(1, "my_ambush: " .. err) return end
    sh.flag.set("my_ambush:done", true)
end, {id = "ambush"})
```

- The handler is registered in the body of `main.lua`: the script lives for the whole session, so the handler works on every map and checks the map itself.
- `ai = "full"` gives the character its own behaviour on any map; `"auto"` behaves as the map decides, `"none"` is a statue.
- The flag `done` is declared in the mod's `flags.json` and becomes `my_ambush:done`; it is saved by name.

Details: [events.md](events.md), [scripting.md](scripting.md). The full version of this mod is [`examples/mods/ambush_dogs/`](../../examples/mods/ambush_dogs/).

### Put an item into the world

```lua
-- mods/my_supplies/scripts/main.lua; mods/my_supplies/flags.json declares "ampoule_taken"
sh.hooks.on("map.load", function(ev)
    if ev.map ~= "map0_s01" then return end
    -- (5.19, -1.01, 273.92) m and 32 degrees, in Q12
    sh.world.pickup_create{item = "Ampoule", x = 21258, y = -4136, z = 1121976, rot = 364,
                           id = "ampoule", flag = "ampoule_taken"}
end)
```

The item is a real pickup: a model on the map, the game's usual "Take it?" question, the item in the inventory and a flag that hides it for good. The list of script pickups is emptied by every map load, so they are created on every load. The full version is [`examples/mods/cafe_supplies/`](../../examples/mods/cafe_supplies/).

## The example mods

All of them use only the public interface and can be installed together in any combination.

| Mod | Shows |
| --- | --- |
| [`example_mod`](../../examples/mods/example_mod/) | the parts of a mod together: `mod.json`, a changed line of the pause menu (English and Russian), a menu of its own reached from the pause menu, the console command `example_hello`, the lifecycle function `on_map_load`, handlers of `map.load` and `item.picked`, an exported API |
| [`hd_harry`](../../examples/mods/hd_harry/) | a texture mod, data only |
| [`cafe_supplies`](../../examples/mods/cafe_supplies/) | two real pickups on the cafe counter with `sh.world.pickup_create`, each once per playthrough through flags of the mod |
| [`ambush_dogs`](../../examples/mods/ambush_dogs/) | an ambush on the street when Harry takes the bullets by the cafe: walkable points behind him, a count by difficulty, an option page, an exported API and an event of its own |
| [`screamer_ambush`](../../examples/mods/screamer_ambush/) | a second mod on the same event: `load_after`, the other mod's API through `sh.mods.get`, its own option page beside the first |
| [`frontier_arms`](../../examples/mods/frontier_arms/) | a new gun with its own ammunition and sounds, a new melee weapon, an item that counts once as a special pickup |
| [`broken_dependency`](../../examples/mods/broken_dependency/) | what a missing dependency looks like on the mods screen |
| [`needs_newer_engine`](../../examples/mods/needs_newer_engine/) | a mod the engine refuses because of `min_engine_version` |

Useful console commands while trying them: `hooks item.picked` lists the handlers of an event in call order, `debugmenu options` opens *Options* with the pages of the mods, `difficulty easy|normal|hard` sets the difficulty the ambushes read, `map map2_s00` warps to a map, `pos` prints where Harry stands.
