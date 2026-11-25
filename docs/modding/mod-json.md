# mod.json: the manifest and the load order

[Back to the guide](README.md)

`mods/<folder>/mod.json` is a JSON object in UTF-8. Only `id` and `version` are required.

## Keys

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| `format` | integer | 1 | version of the manifest format. A number larger than the engine knows is a warning; unknown keys are skipped |
| `id` | string | required | the mod's identifier and namespace: new things of the mod are `<id>:Name`. Rules below |
| `name` | string | `id` | the name on the mods screen |
| `version` | string | required | the mod's version, `MAJOR[.MINOR[.PATCH]][-PRERELEASE][+BUILD]`: `"1"` is `1.0.0`, `"1.2.0-beta.2"` is fine |
| `description` | string | — | shown on the mods screen |
| `authors` | list of strings, or a string | — | the authors; the key `author` is read too |
| `homepage` | string | — | the mod's page |
| `min_engine_version` | string `"X.Y.Z"` | no lower bound | the oldest engine the mod works on. On an older engine the mod is not loaded (`needs engine 1.2.0 or newer, this is 1.0.0`). See [versioning.md](versioning.md) |
| `max_engine_version` | string `"X.Y.Z"` | no upper bound | the newest engine the mod works on (the bound itself is allowed) |
| `engine` | version range | any | the same as a range, for cases the two keys above cannot say (`"^1.2 \|\| ^3"`). `min_engine_version` is usually enough |
| `api` | version range | any | versions of the mod API (now `1.0`) |
| `game` | list of strings, or a string | any | disc serial numbers (`"SLUS-00707"`); on another disc the mod is switched off |
| `dependencies` | object `{"id": "range"}` or list of ids | — | mods this mod cannot work without: they must be installed, enabled and of a fitting version; they load earlier |
| `optional_dependencies` | the same | — | mods that are used when present; they load earlier then. A wrong version is a warning |
| `conflicts` | the same | — | mods this mod does not work with: when one is enabled and its version fits, **this** mod is switched off |
| `load_after` | list of ids | — | an ordering hint: load after these mods, if they are there |
| `load_before` | list of ids | — | load before these mods |
| `priority` | integer, −1000000…1000000 | 0 | the fallback order of mods the player has not ordered yet: larger loads later, which means it wins |
| `gameplay` | boolean | `false` | GAMEPLAY CHANGE: the mod changes rules, balance or story, not only looks |
| `scripts` | boolean | `false` | the mod runs Lua. Without `true` **no** `.lua` file of the mod is run: not `scripts/main.lua`, not the layers of maps and characters, not `menus/*.lua` |
| `requires_restart` | boolean | `true` | `false`: switching and moving the mod takes effect at once, without a restart (textures and models on the next map). Only for a mod that has nothing but such files; with `scripts`, `gameplay` or `replace` the key is ignored with a warning |
| `replace` | list of paths | — | files of the game the mod replaces whole instead of extending: Lua layers (`"maps/map0_s01/events.lua"`) and JSON files with `"$replace": true` at their root. Two mods naming one path conflict |
| `icon` | path inside the mod | — | an icon for the mods screen |
| `preview` | path inside the mod | — | a preview picture |

Keys that begin with `_`, and `$schema`, are comments and editor hints and are skipped silently. Any other unknown key is a warning.

An example with the common keys:

```json
{
  "format": 1,
  "id": "hd_harry_extras",
  "name": "HD Harry: extras",
  "version": "1.2.0",
  "description": "Extra outfits for HD Harry.",
  "authors": ["Me"],
  "min_engine_version": "1.0.0",
  "api": "^1.0",
  "dependencies": { "hd_harry": "^1.0" },
  "optional_dependencies": { "hd_town": ">=2.1" },
  "conflicts": { "low_poly_harry": "*" },
  "load_after": ["ui_tweaks"],
  "gameplay": false,
  "scripts": false
}
```

### The mod id

Latin letters, digits, `_`, `-` and `.`; at most 64 bytes; not starting with `.` or `-`; `silenthill` is taken by the game. Case matters: `HD_TEST` and `hd_test` are two mods. The folder may be named differently from the `id` (the log warns): the namespace is always the `id`. Name the folder like the `id`.

### Version ranges

| Written | Means |
| --- | --- |
| `*` or `""` | any version |
| `1.2.3`, `=1.2.3` | exactly this one |
| `>1.2`, `>=1.2`, `<2`, `<=2.1` | a comparison |
| `^1.2` | `>=1.2.0 <2.0.0`; `^0.3` is `>=0.3.0 <0.4.0` |
| `~1.2` | `>=1.2.0 <1.3.0` |
| `1.x`, `1.2.x`, `1`, `1.2` | any version with these leading parts |
| `>=1.0 <2` | separated by a space: all conditions at once |
| `^1.0 \|\| ^3.0` | `\|\|`: any of the alternatives |

A version with a pre-release tag (`1.0.0-beta`) is lower than the same version without it; metadata after `+` is not compared.

### Manifest errors

Broken JSON, a missing `id` or `version`, a key of the wrong type, a bad version range, `min_engine_version` or `max_engine_version` not written as `X.Y.Z`, a dependency on itself: the mod is not loaded, and the reason with the line number is in the log (`[MODS] ... mod.json: ...; mod skipped`) and in the output of `mods`. An unknown key, an `icon` outside the mod's folder, a folder name that differs from the `id`: a warning only, the mod works. A folder without `mod.json` is skipped with a warning. A manifest error never stops the game.

### Files that are not data

The game does not treat these files in the root of a mod as data: `mod.json`, the `icon` and `preview` files of the manifest, `icon.png`, `preview.png`, `README*`, `LICENSE*` / `LICENCE*`, `CHANGELOG*`, `CREDITS*`.

## Where mods live

The game is portable: `config.cfg`, the saves and `gamedata/` sit beside the executable, and so do the mods, in `mods/`. A second folder can be named with the key `mods_dir` in `config.cfg`, as an absolute path or one relative to the executable's folder:

```text
mods_dir = /home/me/.local/share/silent-hill-pc/mods
mods_dir = C:\Users\me\AppData\Roaming\silent-hill-pc\mods
```

Environment variables (`~`, `%APPDATA%`) are not expanded. Mods of both folders form one list and one order. The player's choice, `load_order.json`, is always in the first folder, `mods/` beside the executable.

## The load order

One registry decides at start which mods work and in which order, and tells every loader from which mod to take a file. **Later in the order wins**: when several mods have a file, the last one's is taken; data that is patched (items, characters, text, sounds, flags) is applied from the first mod to the last.

The rules, in order:

1. **Two folders with one `id`.** The first folder keeps the mod (`mods/` beside the executable before `mods_dir`, inside a folder by name); the second is an error, `id '...' is already used by folder '...'`.
2. **The engine version does not fit** (`min_engine_version`, `max_engine_version`, `engine`): an error whether the mod is enabled or not, so the mods screen shows the reason before the player enables it.
3. **Switched off, or not switched on yet**: the state `disabled`. A new mod is off until the player enables it, so a fresh installation plays as the original even when something lies in the mods folder.
4. **`api` or `game` of an enabled mod do not fit**: an error (`needs mod API ...`, `made for SLUS-..., this disc is ...`).
5. **Dependencies and conflicts**, repeated until nothing changes:
   - a required dependency is missing, switched off, failed to load or has the wrong version: the mod is switched off (`needs mod 'x' >=2.0, which is not installed` / `which is switched off` / `which could not be loaded` / `installed is 1.4.0`), and so are the mods that need it;
   - a mod of `conflicts` with a fitting version is enabled: the mod **that declared the conflict** is switched off (`conflicts with mod 'x' 1.0.0, which is enabled; switch one of them off`); the other one works;
   - a cycle of required dependencies: every mod of the cycle is switched off (`dependency cycle: a -> b -> a`).
6. **Sorting** the working mods: first what must come earlier (required and optional dependencies, `load_after`, `load_before`; a hint that contradicts the rest is dropped with a warning and the mod works), then the player's order from `load_order.json`, then `priority` (smaller is earlier), then `id`.
7. **Two whole replacements of one file.** When two working mods list the same path in `"replace"`, the later one is switched off: `replaces <path> which mod '<x>' also replaces; switch one of them off`.

A mod in the state `error` loads no file and runs no line of Lua, but it is shown on the mods screen and by `mods` with its reason. Mods that do not depend on it work as usual.

### The player's choice: `mods/load_order.json`

The game writes it (the mods screen, the console); it can be edited by hand and is read at start:

```json
{
  "format": 1,
  "mods": [
    { "id": "hd_harry", "enabled": true },
    { "id": "example_mod", "enabled": false }
  ]
}
```

`mods` is the player's order from top to bottom (the first loads first) with the switches. A mod that is not in the list is new and **off**; an entry without `"enabled"` is on. Without the file, or with a broken one, all mods are off. Enabling a mod also enables its required dependencies when they are installed.

### The mods screen and the console

*Options > Mods* lists the mods in load order with state (active, disabled, error and why), version, authors, description, the counts of replaced, added and overridden files and the marks GAMEPLAY CHANGE and "runs scripts". The same page enables, disables and moves a mod. Every change is written to `load_order.json` at once and **takes effect after a restart**; when leaving the page after such a change the game offers to restart itself. The exception is a mod with `"requires_restart": false`, which is applied at once.

Mods can be changed only in the *Options* opened from the title screen.

| Command | What it does |
| --- | --- |
| `mods` | the list of mods with states, reasons and warnings |
| `mods files <id>` | the files of an active mod: `replace` (the game has that path) or `add` (a new one), and `overridden; also in ...` when a later mod has the same file |
| `mods enable <id>`, `mods disable <id>` | switch a mod; saved at once, effective after a restart |
| `mods up <id>`, `mods down <id>` | move a mod earlier or later |
| `modimport <path>`, `modimport <path> install` | list the mods a folder or `.zip` holds and what importing does; import them (as *Options > Mods > Import mods*) |
| `mods reload` | read the files of the active mods again ([tools.md](tools.md#reloading-a-mod-while-the-game-runs)) |
