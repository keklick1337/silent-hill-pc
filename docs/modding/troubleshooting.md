# Troubleshooting

[Back to the guide](README.md)

Everything the mod system decided is written to the log at start in lines beginning with `[MODS]`, and the console command `mods` shows the same: `N of M mods active (load order: later wins)`, then one line per mod — `[place] id version "name": X replaced, Y added`, or for a mod that is off `[-] id version "name": error; reason`. Switch the log file on with `log_file = 1` in `config.cfg`; it is written into `logs/`.

| What you see | Why | What to do |
| --- | --- | --- |
| the mod is not in the list; `<folder>: no mod.json, folder skipped` | there is no manifest | put `mod.json` into the root of the mod's folder, not into a folder inside it |
| `mod.json: ...; mod skipped` | an error in the manifest (the message has the line number) | fix the key ([mod-json.md](mod-json.md)) |
| `error; needs engine 1.2.0 or newer, this is 1.0.0` (or `... or older ...`) | the mod was made for another engine version | update the game, or take the mod's version for this engine; `version` in the console prints the engine version |
| `error; needs mod 'x' ..., which is not installed` / `switched off` / `could not be loaded` / `installed is ...` | a dependency is missing, off, broken or of the wrong version | install or enable the needed mod |
| `error; needs mod API ...` / `made for ...` | the mod is for another API version or another disc | update the mod or the game |
| `error; conflicts with mod 'x' ...` | the mod itself declares a conflict with an enabled mod | switch one of the two off |
| `error; dependency cycle: a -> b -> a` | mods require each other | fix `dependencies` |
| `error; id '...' is already used by folder '...'` | two folders with one `id` | delete the extra copy |
| `error; replaces <path> which mod 'x' also replaces` | two mods replace one file whole | switch one off; better, rewrite the mod as a layer or a patch |
| `warning: load_after 'x' ignored: it contradicts the rest of the load order` | an ordering hint argues with the dependencies | remove the hint; the mod works |
| `disabled` | the player switched it off, or it is new | enable it in *Options > Mods* or with `mods enable <id>`, then restart |
| an installed mod does nothing | new mods are off by default | enable it from the title screen's *Options > Mods* and restart |
| everything on the mods page is dimmed | *Options* was opened from the pause menu | go to the title screen and open *Options* there |
| changes made on the mods screen are not visible | the set and the order change after a restart | answer "Yes" to the restart question, or restart the game |
| a file was not applied | a wrong path | `mods files <id>`: when the file is not listed it lies in the wrong place; `add` instead of `replace` means the path does not match the game's (a typo, an extra folder, `HERO.TIM.png` instead of `HERO.png`). Compare with the path under `gamedata/` beside the game |
| `overridden; also in x` beside a file | a mod later in the order has the same file | move the mod with `mods down` / `mods up` or on the mods screen |
| `has Lua files but mod.json does not say "scripts": true` | the mod's Lua is not run | add `"scripts": true` |
| a Lua error in the log | an error in a script, or the instruction budget was exceeded | the message has the file and the line; a failing hook does not bring the game down |
| `hook <event> of <mod> error: ... (the handler is off)` | an event handler raised an error and was removed until the next start | fix the handler |
| `this event is over` | an event object was kept and touched after its handler returned | copy what you need inside the handler (`ev:fields()`) |
| `... (the extension is left out)` | a Lua layer of the mod failed to load | the message has the file and the line |
| `handlers.json: slot N names the handler 'X', which no events.lua of the map defines` | a slot's `name` has no function in the map's `events.lua` or a mod's layer | define the function, or fix the name ([scripting.md](scripting.md#script-layers)) |
| `extend('X'): nothing of that name to extend` | neither the game nor an earlier layer has a function `X` | check the name (a map's handlers are in its `handlers.json`) |
| a handler is never called | no subscription in this host, or the event is not sent in this case | `hooks <event>` shows the subscriptions, `hooks_trace on` shows whether the event arrives ([events.md](events.md)) |
| `does not declare "gameplay": true` | the mod changes gameplay data without the mark | set `"gameplay": true` |
| `unknown item`, `unknown flag`, `unknown character` | a name the data does not have, or a short name that several mods have | check the spelling; write the full name `<mod id>:Name` |
| `[MODEL] ... no joint is a game bone` | a replacement model has no joints named `boneNN` | [assets.md](assets.md#character-models) |
| `[FMV] FFmpeg: no usable set ...` | an mp4 movie of a mod, and no FFmpeg to decode it | use the release package, install FFmpeg, or ship the movie as an MJPEG `.avi` ([assets.md](assets.md#movies)) |
| `[FONT] no font has U+XXXX ...` | no font draws a character of the text | add a font or a glyph sheet ([assets.md](assets.md#fonts)) |
| `the folder is named 'a' but the mod id is 'b'` | the folder's name differs from the `id` | not an error; the mod's things are `b:Name` |
| `the save has event flag <name>, which no installed data has; it is dropped` | the save was made with a mod that is not installed (or renamed its flag) | not an error: the rest of the save loads; install the mod again to keep its progress |
| `save N was made with other mods: <id> <version>: not enabled; ...` | the save was made with mods that differ from the active ones (missing, switched off, another version, or a story mod added since) | switch the mods it lists on (the Mods screen) and load again; *Load anyway?* in the save list loads it as it is |
| `conversion: save_NNNN.sav: converted from save format N (deprecated ...)` | a save of an older build was rewritten in the current format at start | nothing to do; the old file is in `.savegames/old_format_backup/` |
| `conversion: ... not converted, the new file did not give the same game state` | the conversion would have changed the save | the old file is kept as it was and still loads; report it with the file |
| `[SCENE] convert/models/<mod>/<name>.glb: not found; its models stay undrawn` | a world object names a mod glb (`item_model`) that is not there yet | not an error: the object is simply not drawn until the file exists |

## Checking a mod step by step

1. `mods` — is the mod listed, enabled and without an error?
2. `mods files <id>` — is every file there, as `replace` or `add` as you expect?
3. The log — are there warnings naming the mod (`[MODS] mod <id>: ...`)?
4. For Lua: `hooks` lists the mod's handlers; an error names the file and the line.
5. After fixing a file of an active mod: `mods reload`. After changing `mod.json`, the set of mods or their order: restart.

When you report a problem with the engine, attach the newest file of `logs/`: its first lines name the engine version and the commit.
