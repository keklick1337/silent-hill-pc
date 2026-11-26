# Assets: pictures, models, sound, movies, text, fonts

[Back to the guide](README.md)

The import puts models, textures, animations and sound into `gamedata/convert/`. A mod repeats the path from `convert/`: the file of the last mod in the load order is taken, then the game's.

## Textures

Every picture of the game is `gamedata/convert/textures/<FOLDER>/<NAME>.png`, where folder and name are those of the picture file on the disc without its extension (`CHARA/HERO.TIM` becomes `convert/textures/CHARA/HERO.png`). `gamedata/convert/materials.json` lists which pictures and palettes every model and map uses.

A mod puts a file with exactly the name the import wrote:

- `<NAME>.png` — the picture as the import wrote it;
- `<NAME>.pNN.png` — the variant of the same picture with palette row `NN` (`HERO.p09.png`). A picture with several palettes is imported as one file per palette, and a mod replaces any of them; the rest stay the game's.

Rules:

- Any size: 2×, 4×, not a multiple of the original — the picture is stretched over the original's place.
- A texel with alpha 0 is transparent.
- For pictures the game draws from video memory (2D screens, fonts, effects), `.dds` (BC7) may be used instead of `.png`, and wins over it. Textures of glb models are PNG only.
- The same picture also replaces the texture of the imported glb model, because the image URIs inside the glb point into `convert/textures/`.

AI texture upscaling (Options > Graphics, `texture_upscale`, experimental) never touches a mod's picture: a texture a mod replaces (`.png` or `.dds` under `convert/textures/`, or the disc's TIM) is drawn exactly as the mod made it, at any size, and is not sent through the network nor swapped for an upscaled copy of the game's picture; the upscaler skips it. The upscaled copies are made only of the imported pictures, and only used while the bytes the game reads are those of the imported file.

Lettering: textures with letters (signs, paper maps, notes, posters) would be repainted by the upscaling network, so `config/texture_upscale.json` lists them and they are scaled with a plain filter instead. A mod that adds a picture with lettering to a texture the list does not have adds an entry with a patch of the same path; records merge by `name`:

```json
{
  "lettering": [
    {"name": "BG/THR0301F", "description": "the whole page: street signs"},
    {"name": "TIM/ALESSA", "rects": [[184, 184, 52, 26]], "description": "only the signature"}
  ]
}
```

`name` is `<FOLDER>/<NAME>` (it covers the palette variants `<NAME>.<label>` too); without `rects` the whole picture is filtered, with `rects` (`[x, y, w, h]` in texels of the imported picture) only those rectangles. `{"name": "...", "$remove": true}` takes an entry out. The upscaler makes a changed entry's picture again on the next start.

Check: `mods files <id>` shows the line `replace convert/textures/CHARA/HERO.png`. The built-in texture viewer shows every loaded texture, its palettes and what uses it ([tools.md](tools.md#asset-panels)).

## Character models

`gamedata/convert/models/CHARA/<NAME>.glb` (`HERO.glb` is Harry). The game poses its own bones from its animation clips and draws the glb through its skin, so a replacement must fit the game's skeleton.

Making an HD model by hand:

1. Open `gamedata/convert/models/CHARA/<NAME>.glb` in Blender (glTF import). The bones are named `bone00` … `boneNN`, the meshes are the parts of the original model (`01CHEST_`, `02HEAD1`, `10RHAND2`, ...).
2. Make your mesh and bind it to these bones, up to 4 bones per vertex. Helper bones of your own (fingers, twists) are allowed: a vertex on a bone without a `boneNN` name moves with the nearest ancestor that has one.
3. Export glTF 2.0 (`.glb`, or `.gltf` with its `.bin`) with the skin; animations are not needed.
4. Put it at `mods/<mod>/convert/models/CHARA/<NAME>.glb`.

Rules:

- **Joint names** are `boneNN` (case does not matter, `bone_NN` works too). Without them the model is not loaded (`[MODEL] ... no joint is a game bone`) and the character is drawn from the original.
- **Scale and axes**: metres, +Y up, facing +Z. Any bind pose works, T-pose included, as long as the joints stand where the game's bones stand; otherwise elbows and knees bend in another place.
- **Own proportions.** A model with other bone lengths (wider shoulders, a shorter neck, longer arms) sets `"asset": {"extras": {"proportions": "model"}}` in the glb. The game's clips store only a rotation for every bone; a translation is stored only for the root bones, and the other bones stand at a constant offset from their parent. For such a model the engine takes that offset from the model's own bind pose and the rotations and root translations from the clip. Therefore the axes of every joint in the bind pose must point where the game bone's axes point, and the legs must reach the floor at the height of the game's root. The game's logic (hit boxes, collision, the camera, the flashlight) still computes on the game's skeleton.
- **Parts.** A mesh node named like a part of the original is drawn only while the game draws that part (Harry's hands switch between `06LHAND`, `06LHAND2`, `10RHAND` … `10RHAND4` with the weapon). A node with another name is always drawn, so a model in one mesh does not switch hands.
- **Materials**: base colour (PNG or JPEG, inside the file or beside it), alpha mode MASK (a texel with alpha below 0.5 is not drawn). Normals come from the file.
- A bone above 63 and a vertex without weights are not supported.

A character can also be pointed at a glb under another name with a one-key patch of its `character.json` ([data.md](data.md#another-model-for-a-character-of-the-game)); the tool that binds a foreign model to a skeleton of the game writes such files ([rig-transfer.md](rig-transfer.md)).

Model upscaling (Options > Graphics, `model_upscaling`, experimental) never touches a mod's mesh: a character glb found in a mod (a replacement under `convert/models/`, or the file a `character.json` patch points at), a library of world objects or an area file (`maps/areas/<AREA>.glb`) from a mod is drawn exactly as the mod made it, without PN triangles, so an HD model keeps its own silhouette. Only the game's own imported meshes are rounded. Together with the texture rule above, an "HD Harry" mod with its own model and texture is shown as made whatever the graphics preset.

### A model of the import on another skeleton

A mod may dress a character in another character's imported model without shipping a file of the game: a manifest `convert/models/<mod>/<NAME>.model.json` with no glb beside it and a `"derive"` object. The engine reads the glb `"from"` names (below `convert/models/`), keeps its own bone lengths with `"proportions": "model"`, and draws its meshes as the parts `"parts"` maps them to (a mesh not named keeps its name; `[]` leaves it out). Meshes match the wearer's parts by name. `"source"` names the skeleton file of the wearer, as for any model:

```json
{
  "format": "sh-model/1",
  "source": { "path": "CHARA/HERO.ILM", "kind": "ilm" },
  "derive": {
    "from": "CHARA/SIBYL.glb",
    "proportions": "model",
    "parts": { "10RHAND": ["10RHAND", "10RHAND2", "10RHAND3", "10RHAND4"], "10RGUN": [] }
  }
}
```

It is used like a glb: `"model": "models/<mod>/<NAME>.glb"` in a `character.json`, `"as_player"` (below), or `sh.chara.model_override(type, "<mod>/<NAME>.glb")` while a script wants it. The skeletons must have the same bones (the humans of the game share one layout); the game's clips still turn the wearer's skeleton. The mesh is the import's own, so model upscaling treats it like the game's. [`examples/mods/cybil_story/`](../../examples/mods/cybil_story/) wears Cybil on Harry's skeleton and Harry on Cybil's.

### An item model of a mod

A mod's item can have a model of its own: `"close_up_model": "<mod>/<NAME>.glb"` in `items/<Name>.json` names a glb below `convert/models/` (any node with a mesh, metres, +Y up, front +Z, its picture embedded as the base colour). The inventory, the pickup screen and the close-up draw it with its own picture; it is centred and scaled so its face (the mean of its two largest sides) matches the game's item models, and `display_tilt` turns it. `"display_scale": 1.25` draws it larger or smaller there, and `"display_center": {"x": 0, "y": 0.05, "z": 0}` (metres of the glb, +Y up) names the point of the model that sits at the middle of its place on the item screens, where the middle of its bounds would push a tall part's body (a radio under its antenna) out of the selection frame; the model turns about that point. [`examples/mods/cybil_story/`](../../examples/mods/cybil_story/) has its walkie-talkie this way.

### World objects from a mod's model

A world object of a map (`maps/<map>/world_objects.json`) can be a mod's own model: a prop, a vehicle, a note lying on a table. `"item_model": {"file": "<mod>/<name>.glb", "scale": 1.0}` names a glb below `convert/models/` of the mod (metres, +Y up, front +Z, the origin where it stands: bottom centre; its pictures embedded as the base colour, alpha "MASK" or "BLEND" for cut-outs). It is drawn with the map's light, fog and the flashlight like the map's own objects. `"position"` (metres, Y grows downwards: a negative Y is above the ground) and `"rotation"` (degrees about X, Y and Z) place it; laying a model on its side is a rotation of 90 about its forward axis plus a lift of half its width. `"flags_set"` / `"flags_clear"` show it only while the story says so (a pickup's flag hides the model once it is taken). While the glb is missing the object is not drawn and the log says so once.

A model is only a picture: characters walk through it unless the object has `"collision"`, a list of upright cylinders `{"offset": [x, y, z], "radius": r, "height": h}` in metres. `offset` is the middle of the cylinder's foot from the object's `position` along the world's axes (Y grows downwards; it does not turn with `rotation`, so a lying or turned model gets the offsets of where its parts are in the world); the cylinder stands `height` up from there. While the object is drawn, the player and the monsters are stopped by the cylinders as by a character that never gives way; shots and the camera pass. Follow a long shape with a row of cylinders, not one big one. The collision view of the debug options shows them as bodies.

A model can wear a picture of the game without shipping it: a material whose `extras` name one of the game's item sheets (`"textured": true, "tim": "ITEM/TIM01.TIM", "png": "textures/ITEM/TIM01.png", "palette": 6, "tim_width": 256, "tim_height": 256, "tim_page_u": 0, "tim_page_v": 0`, the keys the importer writes for the game's own item models in `convert/models/ITEM/*.glb`) is drawn with that sheet and palette from the player's import; its texture coordinates are `(texel + 0.5) / 256` of the sheet. Such a material embeds no image.

```json
{"objects": [{"name": "MyBike", "model": "MYBIKE", "item_model": {"file": "mymod/bike.glb", "scale": 1.0},
  "position": [-6.0, -0.54, 158.0], "rotation": [0, 0, 90], "zones": [{"x": -1, "z": 3}],
  "flags_set": ["mymod:story_started"],
  "collision": [{"offset": [0.4, 0.0, -0.6], "radius": 0.4, "height": 0.6},
                {"offset": [0.5, 0.0, 0.3], "radius": 0.45, "height": 1.0}]}]}
```


### Replacing a file while a script wants it

`sh.assets.replace(path, file)` makes the game read the data path `path` (`"convert/textures/ITEM/HEROPIC2.png"`, any path a mod could replace) from `file`, a path inside an active mod folder, until `sh.assets.restore(path)` (or `sh.assets.restore()` for all). It is not saved: a mod sets it again after a load, from its condition (a flag of its story). A picture the game already read is read again the next time the game loads it.

```lua
sh.assets.replace("convert/textures/ITEM/HEROPIC2.png", "portraits/mymod/HEROPIC2.png")
```

## Disc files

Any file of the disc is replaced by a file at the same disc path in the mod's root: `mods/<mod>/CHARA/HERO.ILM`, `BG/*.TIM`, `ITEM/*.TMD`. The path is the one in the game's file table (every one is listed in `gamedata/index.tsv`). A character model file larger than the disc's is checked before it is read; one that fails the check is refused with a `[BIGLM]` line in the log and the disc's model stays.

## Animations

`convert/anim/ANIM/<name>.anim.json` is the animation file without loss: the engine builds the same bytes from it and the logic sees the same numbers. Limits: the file may not be longer than the original in disc sectors; changing keys changes movement and attack timings, so the mod needs `"gameplay": true`; a broken file is skipped with an `[ANIM]` warning. The clip tables (`tables/anim/<table>.json`) merge by `index`.

## Sound

All sound comes from `convert/audio/`. Each file is looked up by itself, so a mod can replace one sound without copying a bank.

**Formats.** Wherever a WAV goes, an Ogg Opus file (`.opus`) may go instead: the same name with the other extension (`convert/audio/vab/BASE/005.opus` for `005.wav`, `XA/xa_0123.opus`), or the path itself in a `"file"`. The game tells the two apart by their content.

| Format | Channels | Rate | Loop |
| --- | --- | --- | --- |
| WAV, 16-bit integer PCM (voice lines: 8 or 16 bits) | mono or stereo | any (voice lines: 4 to 96 kHz) | the `smpl` chunk |
| Ogg Opus | mono or stereo | 48 kHz, as Opus always decodes | the tags `LOOPSTART` and `LOOPLENGTH` (or `LOOPEND`), in 48 kHz frames |

Opus is about a tenth of the size of a WAV at 128 kbit/s and sounds the same in the game; it is lossy, so keep the WAV as the master. A tool that writes it: `ffmpeg -i in.wav -c:a libopus -b:a 128k out.opus` or `opusenc --bitrate 128 in.wav out.opus`; a loop: `opusenc --comment LOOPSTART=48000 --comment LOOPLENGTH=96000 ...`. The file is decoded whole when it is first played and kept like a WAV, so a voice line or a sound effect is a good use; an hour-long file is not (at most 30 minutes). If a mod has both names, the WAV is taken; between mods, the later one in the load order wins as always.

- **Names.** The registry `gamedata/sounds/sounds.json` gives every sound a readable name (`footstep_concrete`, `groaner_death`, `door_locked`). A mod adds an alias or points a name at another sound of the game with a file `sounds/<any>.json`:

  ```json
  { "sounds": { "my_step": "footstep_grass" } }
  ```

- **Your own file for one sound**:

  ```json
  { "sounds": { "footstep_concrete": { "file": "sounds/my_step.wav", "volume": 0.9 } } }
  ```

  The sound plays the mod's file everywhere (16-bit WAV or Ogg Opus, the path from the mod's folder; also `pitch` in semitones, `pan`, `loop`). A new name gets a free number at load and can be used in footstep tables, character sound slots and `sh.sound.play`.
- **A sample of a bank**: `convert/audio/vab/<BANK>/<NNN>.wav` — sample NNN of the bank (the number is `samples[].number` in `<BANK>.vab.json`). 16-bit WAV, mono (stereo is mixed down), any rate. A loop is the WAV's `smpl` chunk; without one the sound plays once.
- **Bank parameters**: `convert/audio/vab/<BANK>.vab.json` — volume and pan of programs and tones, root key, envelope, reverb. Do not renumber programs and tone slots: the game addresses them by number.
- **Music**: a song is `convert/audio/seq/SND/<NAME>.kdt.json` (its events, without loss) and its instruments are the bank `<NAME>` in `convert/audio/vab/`. The sequencer plays the song live, so the game's layers and fades work on a replacement too.
- **Recordings of the songs** (to listen to, not read by the game): with `import_music = wav` (the desktop default in `config.cfg`) the import renders every song with the game's synthesizer into `convert/audio/music/<NAME>.wav` (44.1 kHz stereo, loop in `smpl`), with `import_music = opus` into `<NAME>.opus` (Ogg Opus, 48 kHz stereo, 128 kbit/s, loop in `LOOPSTART`/`LOOPLENGTH`, about a tenth of the size), with `none` not at all (the Android default). `<NAME>.music.json` beside it gives the loop and the render settings in the frames of that file.
- **A voice line**: `XA/xa_NNNN.wav` in the mod's root, NNNN being the line's number in the game's table. The end of a line that the logic waits for comes from the game's tables: a longer recording does not delay the scene and is cut at the same moment.

### An example: footsteps

A step's sound is chosen in a chain: the floor's collision material, the surface (`gamedata/world/surfaces.json`), the character's `footsteps` table, the map's place rules (`maps/<map>/footsteps.json`), the hook `on_footstep`, the sound registry. Snow off the roads in town is the surface `grass`.

```text
mods/snow_steps/
  mod.json                        {"id": "snow_steps", "version": "1.0.0"}
  sounds/snow_steps.json          {"sounds": {"harry_snow_step": {"file": "sounds/snow_step.wav", "volume": 0.9}}}
  sounds/snow_step.wav            16-bit WAV, mono or stereo, any rate (or snow_step.opus, named so in the json)
  chara/Harry/character.json      {"footsteps": {"grass": "harry_snow_step"}}
```

Only in town, a hook instead of the patch — `chara/Harry/actions.lua` (needs `"scripts": true`):

```lua
function on_footstep(e)
    local map = sh.map.name()
    if e.surface == "grass" and (map:sub(1, 4) == "map0" or map:sub(1, 4) == "map2") then
        return "harry_snow_step"
    end
    return nil
end
```

This is sound, not rules: no `"gameplay": true`.

## Movies

A mod replaces a movie of the disc with a video file: `mods/<mod>/FMV/<name>.<extension>`, `<name>` being the movie's name on the disc. The movie plays whole from the mod's file, picture and sound; it is skipped with the same keys as the original.

| Format | Video | Sound | Decoded by |
| --- | --- | --- | --- |
| `.avi` | MJPEG, uncompressed RGB, YUY2 / UYVY / I420 / YV12 / NV12 | PCM or none | the built-in decoder, always there |
| `.mp4` (`.mov`) | H.264 or HEVC, 8 bit 4:2:0 | AAC or none | FFmpeg, loaded at run time |

- Any resolution and frame rate: the frame is fitted into the window keeping its aspect.
- The FFmpeg that a release package carries is built with mp4 / mov, H.264, HEVC and AAC only. `.mkv`, `.webm`, VP9, AV1 and **H.264 inside an `.avi`** play only with a full FFmpeg (6 to 8) installed in the system. For a mod that has to work for everyone, use mp4 with H.264 and AAC, or an MJPEG `.avi`.
- Without any usable FFmpeg an mp4 does not play: the log says `[FMV] FFmpeg: no usable set ...` and the disc's movie plays. A file that cannot be opened never breaks the game: `[FMV] <file> could not be played: the disc movie instead`.

The order of choice, first found plays: `FMV/<name>.avi` of the mods (the mod later in the load order first); `FMV/<name>.mp4` of the mods (then `.mkv`, `.webm`, `.mov`); the same without a mod, for development, in `gamedata/fmv/` — **relative to the working directory of the process**, not to the executable's folder; the disc's movie.

A recommended encoding:

```sh
ffmpeg -i source.mov -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p \
       -c:a aac -b:a 192k -ar 44100 -ac 2 -movflags +faststart C1_20670.mp4
```

Without FFmpeg on the player's side: `ffmpeg -i in.mp4 -c:v mjpeg -q:v 3 -c:a pcm_s16le out.avi`.

Movie names (`XA/<name>` on the disc; which plays when is in `menu/movies.json` and `game.json`):

| Name | What |
| --- | --- |
| `C1_20670` | the intro (US), after the logos; `C2_20670` is the Japanese one |
| `M1_03500` | the start of a new game |
| `M2_01190` … `M9_01730`, `MA_03590`, `MB_04850`, `MC_01930`, `MD_03780`, `ME_03300` | story movies |
| `Z1_16180`, `Z3_02340`, `Z4_01590`, `ZC_14392`, `ZZ_14239` | the endings |

The list with descriptions is `gamedata/config/movie_files.json`. The other files of `XA/` are speech and music, replaced as [sound](#sound), not as movies. The console command `fmv` lists and plays movies.

## Shaders

`shaders/<file>` of a mod replaces the built-in shader with the same file name. The names are those of the built-in sources under `src/render/**/shaders/`: `scene.vert` / `scene.frag` (light, flashlight, fog), `post.vert` / `post.frag` (post-processing), `prim_batch.vert` / `prim_batch.frag`, and others. Start from the built-in source: inputs, outputs, uniform blocks and samplers must match.

- `renderer = gl` (the default): GLSL 450 text, like the built-in one. A shader that does not compile or link is a warning and the built-in shader is used.
- `renderer = vulkan`: SPIR-V `shaders/<file>.spv`, built with `glslc --target-env=vulkan1.0 -DSH_RHI_VULKAN -DSH_RHI_CLIP_Z01`. This backend does not read GLSL text.
- Shaders are read when the renderer is created; `mods reload` does not rebuild them.

## Text

A text file is

```json
{
  "format": "sh-text",
  "version": 1,
  "lang": "en",
  "strings": { "key": "text" }
}
```

at `text/<language>/<any name>.json`. A mod's keys win over the game's and over those of earlier mods; a new key is added. The game's keys are in `gamedata/text/en/`: `ui.*` and `menu.*` for the interface, `item.<Name>.name` and `item.<Name>.desc` for items, `<map>.*` for the messages of a map (in `text/<language>/maps/<map>.json`). A mod's own keys carry its namespace: `mymod:ui.title`, `item.mymod:Crowbar.name`. In a script, `sh.text("mymod:ui.title")` returns the text in the current language.

**Messages a script shows.** A message is shown by its key (`sh.world.say`, `sh.world.message`, the `message` of `sh.world.pickup_create`, `sh.msg_draw` and the message steps of a map event), and a key is looked up first in the messages of the loaded map — the game's, and those a mod adds to that map with a text file that names it, `"map": "map0_s01"` — then in the shared block `common.*`, then in the messages of no map: every string of `text/<language>/messages.json` (this name, directly in the language's folder; the same format, no `"map"`). Such a message can be shown on any map, so the lines of a story that crosses maps are written once per language. The English file of a mod (or of the game) says which messages there are; another language translates them by key, and a line it lacks, or a language without the file, shows the English. A message of the map itself with the same key wins on that map.

The markup of messages (colours, waits, choices) is in [Message markup](#message-markup) below; adding a language and the translation workflow are in [translation.md](translation.md).

### Message markup

| Text | Meaning |
| --- | --- |
| `{wait 2.5}` / `{wait1 2.5}` / `{wait2 2.5}` | the line holds N seconds (the digit is the sound mode) |
| `{end}` | end of the message (without it the next message continues) |
| `{color 2}…{/color}` | colour (`{/color}` returns to white) |
| `{choice 4}` | a choice (Yes / No) |
| `{page}` | page break |
| `{instant}` `{hires}` `{center}` `{inset}` `{pos 4}` | show at once, HD glyphs, centred, indented, window position |
| `{wide}` `{kern}` `{byte 0b}` | spacing and other bytes of menu and item strings |
| `{{` `}}` | literal braces |
| `{raw ~Q1}` | a code of the original the format does not model, kept as it is |

A newline is a line break. `item.*` and `menu.*` strings are drawn by the game's string drawer; `ui.*` strings take no markup (their braces are substitutions the program fills in, such as `{n}`, `{key}`, `{name}`).

## Fonts

All fonts are data: `gamedata/font/` and a mod's `font/`.

- **Glyphs of the game font.** A sheet `font/glyphs/<name>.glyphs` draws a character in a 12×16 cell in the game's style (the format is in [Glyph sheets](#glyph-sheets) below; the game's own sheets in `gamedata/font/glyphs/` are examples). Use it to add a few signs or redraw letters. A mod's sheets go over the game's in load order.
- **A font for a whole script.** A TrueType or OpenType file in `font/` (`.ttf`, `.otf`, `.ttc`), or a bitmap `.shbf`, draws the characters the game font lacks. It is asked before the font families of the language, before the fonts of `gamedata/font/` and before system fonts. Characters the game font has stay the game's.
- Without a fitting font a character is drawn as a fallback box and the log says `[FONT] no font has U+XXXX ...`. The console command `langcheck` prints the languages found and which font draws their letters.
- The font's license is the mod author's business: ship only a font you may distribute.

### Glyph sheets

A sheet (`sh-glyphs v1`) is plain text, edited by hand:

```text
glyph U+0025 advance 13         then 16 rows of 12 characters: '.' transparent, '#' ink
                                (shadow and per-row shade are added), '1'-'9','a'-'f' palette index
mark grave                      then 16 rows: a mark for a lowercase letter (top rows of the cell)
alias U+0451 U+00EB             the same glyph as another character
compose U+00C0 U+0041 grave     a letter with a mark centred over its ink
compose U+00E7 U+0063 cedilla below   a mark under the letter
flip U+00BF U+003F              another character's ink rotated 180°
```

Ink drawn with `#` takes the game's colours: the shade follows the cell row and a shadow is added, so new letters match the disc's. The order of the base sheets is `gamedata/font/font.json`; a mod's sheets go on top in load order.
