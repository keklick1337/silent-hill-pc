# HD Harry — an example mod with an HD texture

The mod replaces Harry's texture (`CHARA/HERO.TIM`) with a picture of any size.

The picture is not in the repository: it is made from the original's graphics, and game files are not distributed (AGENTS.md).

Installation:

1. Copy the `hd_harry` folder into `<exe>/mods/`.
2. Put your picture at `<exe>/mods/hd_harry/convert/textures/CHARA/HERO.png`. This is the same name as the imported PNG (`<exe>/gamedata/convert/textures/CHARA/HERO.png`); an upscaled copy of that file is a convenient start. RGB and RGBA PNGs of any size work; a `.dds` (BC7) with the same name takes precedence over the `.png`.
3. Start the game. The log shows `[MODS] ... hd_harry 1.0.0 "HD Harry": 1 replaced, 0 added`. Console: `mods files hd_harry`.

To replace only some palette rows, put `HERO.pNN.png` next to it.
