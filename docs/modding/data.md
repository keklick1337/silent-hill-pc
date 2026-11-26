# Data: what a mod's files do

[Back to the guide](README.md)

A mod repeats the paths of `gamedata/`. What a file of a mod does depends on its kind:

- it **replaces**: the file of the last mod in the load order is taken, the others are not read;
- it **patches**: the files of all mods are applied in order over the game's, the later one has the last word;
- it **merges**: JSON data files — the game's file plus each mod's patch in load order, by fields and by record keys ([merging JSON](#merging-json-data)).

## Layout of a mod

| Kind | Path in the mod | How it applies | More |
| --- | --- | --- | --- |
| Pictures | `convert/textures/<FOLDER>/<NAME>.png` (or `.dds`), `<NAME>.pNN.png` | replaces a picture or one palette row; any size | [assets.md](assets.md#textures) |
| Models | `convert/models/**/*.glb` | replaces a model | [assets.md](assets.md#character-models) |
| Disc files | the disc's path in the mod's root: `CHARA/HERO.ILM`, `BG/*.TIM`, `ITEM/*.TMD`, `ANIM/*.ANM`, `XA/...` | replaces the disc file for everything that reads its bytes | [assets.md](assets.md#disc-files) |
| Animations | `convert/anim/ANIM/<name>.anim.json`; clip tables `tables/anim/<table>.json` | the first replaces, the second merges (records by `index`); changing timings is gameplay | [assets.md](assets.md#animations) |
| Map data | `maps/<map>/*.json` | merges | [maps](#maps) |
| Map scripts | `maps/<map>/events.lua`, `world_objects.lua`, `hooks.lua`; `scripts/map_hooks.lua`, `scripts/map_rooms.lua` | a layer over the game's file | [scripting.md](scripting.md#script-layers) |
| Map geometry | `maps/areas/*.glb` | replaces | — |
| Items | `items/<Name>.json`; another mod's: `items/<mod>/<Name>.json` | patches an item of the game or adds `<mod>:<Name>` | [items and weapons](#items-and-weapons) |
| Weapons | `weapons/<Name>.json`, `weapons/<mod>/<Name>.json` | patches (an attack row by its `input`) or adds | [items and weapons](#items-and-weapons) |
| Characters | `chara/<Name>/character.json`; Lua: `chara/<Name>/behavior.lua`, `actions.lua`, `fallback_ai.lua`, the shared `chara/common.lua`, `combat.lua`, `fallbacks.lua` | `character.json` patches by keys (`null` removes a key) or adds a character; `state.json`, `part_variants.json` merge; Lua is a layer | [characters](#characters) |
| Event flags | `flags.json`, `flags/*.json` | patches descriptions of the game's flags, adds flags `<mod>:<name>` | [flags](#flags) |
| Sound names | `sounds/<any>.json` | patches the sound registry | [assets.md](assets.md#sound) |
| Sound files | `convert/audio/...` | replaces a file | [assets.md](assets.md#sound) |
| Surfaces (footsteps) | `world/surfaces.json`; `maps/<map>/footsteps.json` | patches; merges (by `name`) | [assets.md](assets.md#an-example-footsteps) |
| Text | `text/<language>/**/*.json` | patches by keys; new keys are added; `text/<language>/messages.json` holds messages a script shows on any map | [assets.md](assets.md#text) |
| A language | `text/<code>/lang.json` and the translation beside it | adds a language to *Options > Language* | [translation.md](translation.md) |
| Menus | `menus/*.lua` (needs `"scripts": true`) | run after the game's menus | [scripting.md](scripting.md#menus-and-option-pages) |
| The mod's script | `scripts/main.lua` (needs `"scripts": true`) | the mod's own script in its own environment | [scripting.md](scripting.md) |
| Game settings | `config/*.json` | merges; gameplay, except `title_fog.json` | [game settings](#game-settings) |
| The game manifest | `game.json` | merges (`maps` by `name`) | [game settings](#game-settings) |
| Shaders | `shaders/<file>` | replaces the built-in shader of that name | [assets.md](assets.md#shaders) |
| Screens | `menu/*.json` (`staff_roll.json`, `menu_style.json`, `text_style.json`, `options.json`, `boot_logos.json`, `movies.json`, `title_menu.json`, `map_screen.json`, ...) | merges | each file's `description` |
| Effects | `effects/*.json`, `npc/rules.json` | merges | each file's `description` |
| Movies | `FMV/<name>.avi`, `FMV/<name>.mp4` | replaces a movie whole | [assets.md](assets.md#movies) |
| Glyphs of the game font | `font/glyphs/<name>.glyphs` | patches: a mod's glyph replaces the font's, new ones are added | [assets.md](assets.md#fonts) |
| Fonts for other characters | `font/*.ttf`, `*.otf`, `*.ttc`, `*.shbf` | adds | [assets.md](assets.md#fonts) |

Every JSON file of `gamedata/` may carry a top-level `"description"`: an explanation for modders of what the file is, what its fields mean and in which units. Loaders do not read it, and a mod may write its own.

## Merging JSON data

Several mods can change one JSON data file. The game takes its own file and lays the file of every active mod over it in load order. A mod's file is a patch: it holds only what the mod changes.

- **Objects** merge by key, recursively. A new key is added; `null` removes a key.
- **Arrays of objects** merge by the record's key field: the file's key from the table below, else the first of `id` and `name` that every record of the patch has. A record with a key the base has merges into it; a new one is appended, or placed with `"$after": <key>` / `"$before": <key>` (which also moves an existing record); `"$remove": true` removes a record. An array whose records have no key is replaced whole.
- **Arrays of values** (numbers, strings) are replaced whole. To extend one write `{"$append": [...]}` or `{"$prepend": [...]}`; to take values out, `{"$remove": [...]}`.
- **Pairs of a readable value and its fixed-point twin** (`speed` / `speed_q12`, `x` / `x_q12`): when a patch sets one of the pair and not the other, the base's other one is dropped, so the old copy cannot hide the change.
- **`"$replace": true`** in a nested object: that object replaces the base's whole. At the root of the file it replaces the whole file, and only when the file's path is in the `"replace"` list of `mod.json`; otherwise it is a warning and the file merges as a patch. Two mods replacing one path conflict ([load order, rule 7](mod-json.md#the-load-order)).
- **`"$keys": {"path": "field"}`** at the root of a patch names the key of an array the table does not have.
- Keys beginning with `$` never reach the result; an unknown directive is a warning.
- A broken file of a mod is skipped with a warning and the others apply.

The log says what happened: `[MODS] mod X: maps/map0_s01/world_objects.json: modified 2, added 1, removed 0`.

Array keys other than `id` / `name`:

| File | Array | Key |
| --- | --- | --- |
| `maps/<map>/world_objects.json` | `objects` | `name` |
| `maps/<map>/handlers.json` | `handlers` | `index` |
| `maps/<map>/tables.json` | `symbols`, `functions`, `variables` | `symbol` |
| `maps/<map>/footsteps.json` | `places` | `name` |
| `maps/<map>/spawn_overrides.json` | `spawn_overrides`; `spawn_variants.variants` | `id` (a record without one is appended); `name` |
| `maps/<map>/map.json` | `points`, `cameras`, `collision_triggers`; `spawns` | `index`; `group,slot` |
| `maps/<map>/points.json`, `events.json`, `cameras.json`, `collision.json` | `points`, `events`, `cameras`, `triggers` | `index` |
| `tables/anim/*.json` | `entries` | `index` |
| `config/difficulties.json` | `difficulties` | `label` |
| `config/pickups.json` | `pickups` | `item` |
| `game.json` | `maps` | `name` |

Lists that work by "first match" (`particles.json` `init` / `update`, `spawn_variants.choices`) and positional tables (`config/sound_tables.json`) have no key and are replaced whole.

## Names and namespaces

There are no numeric ids in the data. An item, weapon, character, flag, sound, message or menu is named, and numbers are the engine's private business.

**Form.** An identifier is `namespace:Name`. The name is PascalCase: letters, digits and `_`, not starting with a digit. The base game's namespace is `silenthill`; a mod's is its `id` from `mod.json`, not the folder name. The identifier comes from where the file is, not from a field inside it: `gamedata/items/HealthDrink.json` is `silenthill:HealthDrink`, `gamedata/chara/Harry/` is `silenthill:Harry`.

**Short names.** A name without `:` is resolved like this:

1. in the namespace of whoever writes it (a mod's data and scripts: the mod's namespace; the base: `silenthill`);
2. else `silenthill:<Name>`;
3. else the only namespace that has such a name.

So a mod's script may write `"done"` for its own flag `mymod:done` and `"HealthDrink"` for the game's item. When a name exists in several mods and no rule picks one, it is an error and the full name is needed.

**Adding and changing.**

- `mods/<mod>/items/<Name>.json`: when the base has `silenthill:<Name>` the file patches it; otherwise it is a new thing, `<mod id>:<Name>`. The same holds for `weapons/` and `chara/<Name>/character.json`.
- `mods/<mod>/items/<namespace>/<Name>.json` (and `weapons/<namespace>/`) patches a thing of another namespace.
- A mod never "overrides" a base thing with its own namespace: `mods/m/items/HealthDrink.json` is always a patch of `silenthill:HealthDrink`.
- Two mods with a thing of the same name do not conflict: they are `a:Crowbar` and `b:Crowbar`.

**Numbers.** The engine gives a new thing of a mod a free internal number at load, deterministically: the same set of mods gets the same numbers. Saves hold names, so a save with a mod's item is read after the mod set changes (when the item is gone the log warns). The few limits that numbers still impose:

- an item whose number is above 255 exists in the inventory, the save and Lua, but cannot be written into the byte tables of a map (the item of an event in `events.json`);
- a character whose number is above 127 cannot be put into a map's byte spawn table, only spawned through `spawn_overrides.json`, `charas.json` or Lua.

**Where names are written.** In data (`"item"`, `"ammo"` of a weapon; `"required_item"`, `"required_flag"`, `"complete_flag"` of a map event), in scripts (`sh.item.give("HealthDrink")`, `sh.flag.get("M1S03_ShotgunShells0")`, `sh.entity.spawn{chara = "Groaner"}`), in text keys (`item.HealthDrink.name`) and in saves. Files of the game are named by their disc path (`"TIM/LIBRBOOK.TIM"`), so a name is right on a disc of any region.

## Items and weapons

One file per thing: `gamedata/items/<Name>.json` and `gamedata/weapons/<Name>.json`. The file name is the identifier.

**An item** (`items/<Name>.json`):

- `group`: `health`, `keys`, `puzzle`, `melee`, `guns`, `ammo` or `portable`;
- `pickup_count`: how many one pickup gives (handgun bullets 15, shells 6; default 1);
- `world_model`: the model lying on the ground (`AIDKIT_N`, `DRINK_NE`, ...);
- `inventory`: the item's rules on the inventory screen — `order`, `stack_limit`, `keep_when_empty`, `command`, `command_in_hand`, `pickup` (`item` / `special`), `pickup_once`, `name_tag`, `usable_ammo`, `selectable`, `examine_picture`, `heal` (a row of `health_items` in `config/gameplay.json`) and others; the shipped files of `gamedata/items/` show each of them in use.

The name and the description are text, not item data: the keys `item.<Name>.name` and `item.<Name>.desc` in `text/<language>/items.json`.

**A weapon** (`weapons/<Name>.json`):

- `class`: `melee` or `gun`; `item`: the item the weapon is carried as; `clip`: the magazine (`null` is unlimited); `ammo`: the ammunition item;
- `attacks`: the rows `tap`, `hold` and `multitap` with damage (`power`), reach, the hit window (`keyframe_start`, `keyframe_length`) and more; a shot is the `tap` row of a `gun`, and `pellets` makes it a shotgun blast;
- `rig`: Harry's animation block for the weapon.
- `muzzle_light`: a gun's muzzle flash as a light of the modern lighting (colour, intensity, range, fade); rendering only.
- `touch_icon`: the picture of the touch screen's attack button while the weapon is in hand, a file of `gamedata/ui/touch/` without `.svg` (`attack_pistol`, `attack_shotgun`, `attack_rifle`, `attack_blaster`, `attack_blade`, `attack_blunt`, `attack_axe`, `attack_chainsaw`, `attack_drill`, or a mod's own); without it a gun shows `attack_pistol`, a melee weapon `attack_blunt`.

What a mod's file does:

```json
{ "pickup_count": 2 }
```

as `mods/<mod>/items/HealthDrink.json` patches the base item: only the named fields change.

```json
{ "clip": 30, "attacks": [ { "input": "tap", "power": 150 } ] }
```

as `mods/<mod>/weapons/Handgun.json` patches the weapon: the attack row is found by its `input`.

A file with a name the base does not have is a new item or weapon of the mod. A mod that changes `items/` or `weapons/` declares `"gameplay": true`.

### A new weapon

The example [`examples/mods/frontier_arms/`](../../examples/mods/frontier_arms/) adds a gun and a melee weapon. A new weapon needs:

- `weapons/<Name>.json` with `class`, `item`, `attacks` and `rig`. The simplest `rig` is a copy of a base weapon's of the same kind: the handgun's for a gun, the axe's or the pipe's for a melee weapon;
- `items/<Name>.json` of the group `guns` or `melee`, and for a gun its ammunition (`ammo`, `clip`) as one more item;
- its sounds in `rig.sounds`: `attack`, `reload`, `out_of_ammo` are names of the sound registry, which the mod declares in `sounds/<any>.json` ([assets.md](assets.md#sound));
- optionally `"like": "<base weapon>"`: whose details hard-wired in Harry's script the weapon takes (the shape of the blade's tip, the step speed while turning with the weapon raised). The default is `SteelPipe` for a melee weapon and `Handgun` for a gun.

In the game: `give <Name>`, `equip <Name>`, `spawn Groaner`, and the damage shows in the enemy's health (`health` in the console).

## Characters

A folder per character: `chara/<Name>/character.json` (`Harry`, `Cheryl`, `Groaner`, `AirScreamer`, ...). A mod's file patches the character with only the keys it holds; a folder with a name the base does not have is a new character, `<mod>:<Name>`.

- **Files**: `model` (`"models/CHARA/DOG.glb"`, a path from `convert/`), `animations` (`"anim/ANIM/DOG.anim.json"`), `textures` (a list of PNGs).
- **Look**: `blend_mode` (`average`, `additive`, `subtractive`), `shadow`, `camera_anchor`, `camera_offset_y`.
- **Sounds**: `{"sounds": {"death": "groaner_damage"}}` — names of the sound registry; footsteps by surface: `{"footsteps": {"grass": "footstep_concrete"}}`.
- **The rest**: `params`, `anims`, `controls`, `spawn`, `harry_reactions`, and the type properties below.
- **Lua**: `behavior.lua` (the behaviour), `actions.lua` (action hooks: `on_footstep`, `on_damage`, `on_death`, `on_attack`, `on_sound`), `fallback_ai.lua`. A mod's files are layers over the game's ([scripting.md](scripting.md#script-layers)).

A patch of looks only (`model`, `textures`, `shadow`, `blend_mode`, `description`) changes no rule and needs no `"gameplay": true`. Any other key does.

### Another model for a character of the game

```json
{ "model": "models/my_mod/HERO.glb" }
```

as `mods/<mod>/chara/Harry/character.json` points the character at another glb. It is drawn from that file as long as a manifest `<FILE>.model.json` lies beside the glb and names, in `source.path`, the same skeleton file the character has (`CHARA/HERO.ILM`): the engine still reads the parts and bones from it. The file is looked up in all active mods, so models can live in one library mod and the patches in mods that depend on it. Making such a glb from your own model: [rig-transfer.md](rig-transfer.md); the rules a model must follow: [assets.md](assets.md#character-models).

### Type properties

The engine does not branch on a particular character: everything that depends on the type is a key of `character.json`. A mod's character sets only what differs from the defaults; `null` restores a default.

| Key | Default | Meaning |
| --- | --- | --- |
| `blood` | `"Default"` | the blood of a hit: a name of `effects/blood.json`; `"None"` for none |
| `blood_at_zero_health` | `false` | blood still comes at exactly 0 health (Harry) |
| `blood_strength` | — | blood strength instead of the weapon row's |
| `blood_follows_body` | `true` | the splatter follows the body |
| `blood_depth_bias` | `32` | depth offset of the splatter |
| `impact_sound` | — | the sound of a melee hit instead of the weapon's |
| `is_player` | `false` | the player's character |
| `enemy` | `false` | an ordinary enemy: aim assistance targets it |
| `is_boss` | `false` | while alive it is a boss fight and quick saving is refused |
| `single_clip_table` | `false` | every clip table number gives the first table |
| `state_layout` | the behaviour's folder | whose `state.json` lays out the behaviour's variables |
| `pool` | whether `spawn` exists | the character pool loads the type on demand, so it can be spawned on any map |
| `pool_files` | — | disc files the pool loads instead of the file table's row: `{"model", "texture", "anim"}` |
| `preview` | — | the view in the spawn menu's preview: `{"rotate": [x, y, z], "zoom": 1, "lift": 0}` |
| `acid_attack` | `0` | the attack by which Harry receives this type's acid |
| `portrait` | — | the picture this type shows as the player's portrait on the inventory screen (`"convert/textures/ITEM/HEROPIC2.png"` for Harry), so it can be swapped |
| `as_player` | — | how the player wears this type (`sh.player.set_character`): `{"model": "models/<mod>/<NAME>.glb", "portrait": "<file in the mod>"}`; the model is bound to the player's skeleton ([assets.md](assets.md#a-model-of-the-import-on-another-skeleton)) |

## Maps

Every map is data in `gamedata/maps/<map>/`: `overlay.json`, `tables.json`, `handlers.json`, `rooms.json`, `map_vars.json`, `events.lua`, `hooks.lua`, `world_objects.json` and `.lua`, `charas.json`, `particles.json`, `footsteps.json`, `spawn_overrides.json`, plus what the import writes from the disc (`map.json`, `events.json`, `points.json`, the geometry). A mod changes any of these with a patch or a Lua layer. Each file states its fields in its `description`.

- **Conditional spawns** without Lua, `maps/<map>/spawn_overrides.json`:

  ```json
  {
    "spawn_overrides": [
      { "when": { "difficulty": "easy" },
        "set": [ { "chara": "GreyChild", "slot": 10, "flags": ["none"] } ] }
    ]
  }
  ```

  `when` also takes `{"flags_set": [...], "flags_clear": [...]}` and `{"difficulty_min": "normal"}`; whole spawn tables by condition are `spawn_variants`. Event-driven spawns are `sh.entity.spawn` in a script.
- **World objects**, `maps/<map>/world_objects.json`: a patch adds, removes or moves models and changes their cells and flag conditions; motion is `world_objects.lua`. Keep the names of objects the map's variables refer to and their `"script": true`. An object can be drawn from a mod's own model: `"item_model": {"file": "<mod>/<name>.glb", "scale": 1.0}` names a glb below the mod's `convert/models/` ([assets.md](assets.md#world-objects-from-a-mods-model)); while the file is missing the object is simply not drawn.
- **Map events** call the function of `events.lua` named by the `name` of the slot in `handlers.json` (`MapEvent_CafeCutscene`, ...). A mod wraps one with `extend` or adds a new one ([scripting.md](scripting.md#script-layers)); it can also cancel one through the event `map.event` ([events.md](events.md)).
- **Lights**, `maps/<map>/lights.json` (rendering only, no `"gameplay"` needed): point and spot lights for the modern lighting, patched light by light (`name`). Positions are metres in the game's axes (y grows downwards), colour is linear 0..1:

  ```json
  {
    "lights": [
      { "name": "BasementLamp", "extra": true, "position": [140.5, -2.8, -20.6],
        "color": [1.0, 0.85, 0.6], "intensity": 0.8, "range": 4.0,
        "when": "flag:M3S01_GeneratorOn" }
    ]
  }
  ```

  `when` switches a light with the story: one condition or a list that must all hold, `"flag:<name>"` (the event flag is set) or `"!flag:<name>"` (it is clear); it is checked every frame, so a light goes on or off the moment a script sets the flag (the older `flags_set` / `flags_clear` lists still work, and the sun takes `when` too). `"extra": true` marks a lamp placed by hand where the room shows a lit fixture: it shines only with the Extra Lights setting (`extra_lights`), on every renderer; other lights follow the map lights settings (`map_lights`, `rt_lights`). Other keys: `type` (`point` or `spot` with `direction` and `cone`), `radius`, `casts_shadows`, `gi`, `bone` / `while_effect` / `flicker` ([RENDER.md](../RENDER.md), 2.2b and 7.16). `"shadow_map": true` gives a light its shadows also without ray tracing: while the flashlight is off, the first such light takes the flashlight's shadow map (the `shadows` setting), looking from a light on Harry's bone through his body, so his shadow falls away from the flame (the match of the opening alley; RENDER.md 7.9).
- **Fire light**, `effects/fire_lights.json`: how much light each fire effect gives (colour, intensity at `full`, range, `flicker`, `lift`); the light follows the effect's own flames, so a mod's fire made with the same effects lights the scene by itself. A gun of a story character without a weapon file takes `muzzle_light` from its entry in `effects/combat_effects.json` "guns".
- **Map variables** that the map's scripts read with `sh.map.get` and `sh.map.set` are declared in `maps/<map>/map_vars.json` (shared ones in `config/map_vars.json`).

A mod that changes map data declares `"gameplay": true`.

## Flags

A flag is one yes-or-no fact of the story that the save keeps: an item taken, a door opened, a mark drawn on the map. A flag is its name; the engine gives it a save bit at load.

The game declares its flags in `gamedata/flags/<area>.json` with a description each. Their names are readable: `M1S03_ShotgunShells0` (a flag of one map), `MapMark_OldTown_SchoolCircle` (a mark of the paper map), `SeenSaveScreen` (shared by several maps).

A mod declares its own in `flags.json` (or `flags/*.json`):

```json
{
  "format": "sh-flags",
  "version": 1,
  "flags": {
    "door_opened": { "description": "The cellar door of my mod was opened." }
  }
}
```

A short name that is a flag of the base patches its description; any other name is a new flag, `<mod id>:door_opened`. Scripts read and write it by name — `sh.flag.get("door_opened")`, `sh.flag.set("door_opened", true)`, `sh.flag.clear("door_opened")` — and data names it the same way (`"required_flag"`, `"complete_flag"`, `"flags_set"`). An unknown name is a Lua error, `unknown flag`. The bit is stored nowhere: the save writes flags by name.

A mod may declare as many flags as it needs (thousands are fine). The base game's flags keep the bits of the original save layout; every flag of a mod gets an internal number past that layout when the game starts, a different one if the set of mods changes. That number is never written anywhere, so a mod can add, rename or drop flags between versions: what a save keeps is the flag's name. A flag a script sets once (a monster killed, a pickup taken, a beat of a story reached) is the way to make something stay done after a load.


## Game settings

Files of `config/*.json` merge. A mod that changes them declares `"gameplay": true` (except `title_fog.json`).

- `config/gameplay.json`: the player's maximum health, how much each health item heals (`health_items`), how many presses free Harry from a grab.
- `config/difficulties.json`: the difficulty list of the New Game menu. An entry is `{"label": "NIGHTMARE", "base_difficulty": 1}`; `base_difficulty` (−1, 0, 1) is the difficulty the game's mechanics run on, `"default": true` preselects an entry, `"start_map"` starts another map.
- `config/quick_actions.json`, `config/console_give.json`, `config/pickups.json`, `door_sounds.json`, `game_over_tips.json` and the rest are described by their own `"description"`.

Save points: `menu/save_screen.json` `location_keys` lists the text key of every save point name by its number (0..24 the game's). A mod appends its own with `{"location_keys": {"$append": ["mymod:save.tower"]}}`; they get the numbers after the game's in load order, the save keeps the number, and a script opens the save screen at one with `sh.map_event_set(9, number, 0)`.

`game.json` holds the movies of the intro (`intro_file`, `opening_file`), the map of a new game (`new_game_map`), the map of `-skiptogame` (`start_map`) and the list of maps (`maps`, each a `name`; a map's number is its place in the list). A mod's `game.json` is a patch: a new map is appended or placed with `"$after"` / `"$before"`.

## Saves

Mods do not touch saves. They are files in `.savegames/` beside the game, one per slot, and they store the names of things and flags, not numbers.

Since engine 1.3.0 a save (file format 3, game state `"sh-savegame/2"`) lists the set event flags by name only (`"flags"`); `"flag_bits"` holds only set bits of the original layout that have no name. A save made with a mod that is no longer installed loads: the flags nobody declares any more are dropped with a log line (`the save has event flag <name>, which no installed data has; it is dropped`), the rest of the game loads as saved.

A save also records the mods it was made with: the descriptive `META` chunk (format `"sh-save-meta/3"`) lists every active mod with its `id`, `name`, `version` and whether it is a `gameplay` change. A save without this list (an older build's) loads as before, with no note. The save list compares the list with the mods of the run: a save made with a mod that is not installed, switched off or at an older version now gets a red `!` beside its name and a line under the list (`Mods: Cybil: The Other Side (not enabled)`), and loading it asks first (*Load anyway?*). A newer version of the same mod, or a story (`gameplay`) mod active now that the save was made without, gives a grey line only. The log says what differs when such a save loads (`save N was made with other mods: ...`).

Saves of older builds (file formats 1 and 2) are converted once when the game starts: each old file is first copied to `.savegames/old_format_backup/`, then written in the current format, with the date it was made, and read back; if the new file does not give the same game state, the old one is put back and the log says so. The log has one `conversion:` line per file. The reader of the old formats and this conversion are deprecated and will be removed in a future version.

