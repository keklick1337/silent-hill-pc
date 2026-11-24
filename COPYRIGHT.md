# Copyright and licensing

Silent Hill PC is a C++20 engine that plays Silent Hill (PlayStation, NTSC USA v1.1, SLUS-007.07) from the player's own disc image. This file says which parts of the repository are whose, and under which terms.

Short version: **the project is GPL-3.0-or-later. No game files are distributed: the game imports everything from the player's disc image on the first start.**

---

## 1. The project — GPL-3.0-or-later

Copyright (C) 2026 Vladislav Tislenko (keklick1337).

Covered: `src/`, `tests/`, `tools/`, `cmake/`, `docs/`, `examples/`, `assets/config.cfg`, the application icon (`assets/icon/`) and the base game data in `gamedata/` — configuration, Lua scripts, tables, the interface text, the translations of the game's text (below) and the glyph sheets of `gamedata/font/glyphs/` except `cyrillic.glyphs` — except what sections 2 and 3 list.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. It is distributed in the hope that it will be useful, but **WITHOUT ANY WARRANTY**; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. The full text is in [`LICENSE`](LICENSE).

The engine contains no code of the original game and no decompiled code. The behaviour of the original was written anew, using the community decompilation [silent-hill-decomp](https://github.com/shdecompilations/silent-hill-decomp) as a description of that behaviour; the work of its contributors is gratefully acknowledged. None of its sources are in this repository. Parts of the engine that are not the game itself — the settings, the console, the PC screens and some of the PC features and tools — were first adapted to C++ from the Silent Hill PC port [SlickAmogus/silent-hill-decomp](https://github.com/SlickAmogus/silent-hill-decomp) (`pc_port/`), Copyright (C) 2025-2026 Chris Hardin and the Silent Hill PC Port contributors, GPL-3.0-or-later, and have been changed and extended since; they are distributed here under the same license, and the work of that project's contributors is gratefully acknowledged.

**Translations.** Every translation in `gamedata/text/<language>/` is the project's own. The game's text in the twelve languages other than English — `common.json`, `items.json` and `maps/*.json` for Arabic, Chinese, French, German, Italian, Japanese, Korean, Polish, Portuguese, Russian, Spanish and Turkish — was translated for this project from the English of the disc, and the interface text (`menu.json`, `ui.json`, `options_menu.json` and the other files of those directories) from the project's English in `gamedata/text/en/`, without using the text of other releases or of fan translations. These translations are the project's work under the license above; the game's text is also a derivative of Konami's script (section 4). The English text of the game itself, `common.json`, `items.json` and `maps/*.json` of `gamedata/text/en/`, is Konami's (section 3).

## 2. Third-party components

Each keeps its own license; none is covered by section 1.

| Component | Where | License |
| --- | --- | --- |
| Lua 5.4.7 | `third_party/lua/` (built from source) | MIT (notice in `third_party/lua/src/lua.h`) |
| Dear ImGui v1.92.5 (`6d910d5487d1`) | `third_party/imgui/` (built from source) | MIT (`third_party/imgui/LICENSE.txt`, `assets/licenses/LICENSE-imgui.txt`) |
| miniz | `third_party/miniz/` | MIT |
| stb_image, stb_truetype | `third_party/stb/` | MIT or public domain (dual) |
| cgltf | `third_party/cgltf/` | MIT |
| bcdec | `third_party/bcdec/` | MIT or Unlicense (dual) |
| volk `vulkan-sdk-1.4.341.0` (`d979819fd03d`) | `third_party/volk/` (built from source; the Vulkan loader is opened at run time) | MIT (`third_party/volk/LICENSE.md`) |
| Vulkan Memory Allocator v3.3.0 (`1d8f600fd424`) | `third_party/vma/` (built from source) | MIT (`third_party/vma/LICENSE.txt`) |
| Vulkan-Headers `vulkan-sdk-1.4.341.0` (`b5c8f996196b`) | `third_party/vulkan-headers/` (the C headers only) | Apache-2.0 (`third_party/vulkan-headers/LICENSE.md`, `LICENSES/`) |
| JetBrains Mono v2.304 | `third_party/fonts/jetbrains-mono/` (console and tools font) | SIL Open Font License 1.1 (`OFL.txt` beside it) |
| Oswald, Barlow Semi Condensed | `gamedata/font/*.ttf` (draw characters the game font lacks; the free camera's help) | SIL Open Font License 1.1 (`assets/licenses/OFL.txt`) |
| Shinonome 16-dot JIS X 0208 font | `gamedata/font/shinonome16.shbf` (Japanese glyphs, by code point) | public domain |
| Kenney "Input Prompts Pixel" 1.0 | `assets/runtime/icons/input_glyphs.png`, `tools/scripts/kenney_input_prompts_pixel/` | CC0 1.0 (`assets/runtime/icons/License_Kenney.txt`) |
| SDL_GameControllerDB | `assets/gamecontrollerdb.txt` | zlib (`assets/licenses/LICENSE-SDL_GameControllerDB.txt`) |
| SDL3 `release-3.4.18` | linked; fetched by `tools/win-deps/fetch.sh`, not in git | zlib (`licenses/SDL3.txt`) |
| OpenAL Soft 1.24.3 | linked as a shared library, not in git | LGPL-2.1-or-later |
| libjpeg-turbo 3.1.4.1 | linked, not in git | IJG, BSD 3-Clause, zlib |
| FFmpeg `n8.1.3` (`1041abdc962f`) | built by `tools/ffmpeg/build.sh` into `third_party/deps/` (not in git), loaded at run time for the mods' mp4 movies | LGPL-2.1-or-later (configured without `--enable-gpl` / `--enable-nonfree`) |
| Mesa 26.2.6 llvmpipe, Windows x64 (mesa-dist-win 26.2.6, mingw build; LLVM 23.1.4, zlib and zstd linked in) | `software_gl/opengl32.dll`, `software_gl/libgallium_wgl.dll` of the Windows package; prebuilt binaries fetched and SHA-256-checked by `tools/win-deps/software_gl.sh`, not in git; loaded only when the system has no OpenGL 3.3 driver | MIT (Mesa, mesa-dist-win; individual Mesa files Apache-2.0, BSL-1.0, SGI-B-2.0), LLVM Apache-2.0 WITH LLVM-exception, zlib, zstd BSD-3-Clause (`licenses/LICENSE-Mesa*.txt`, `LICENSE-mesa-dist-win.txt`, `LICENSE-LLVM.txt`, `LICENSE-zlib.txt`, `LICENSE-zstd.txt`) |
| ncnn `20250916` (`c4193aadbbb5`), with its glslang submodule | built by `tools/ncnn/build.sh` into `third_party/deps/` (not in git), linked statically into the `sh-upscale` tool only (AI texture upscaling), not into the game | BSD 3-Clause (`LICENSE-ncnn.txt`; it lists glslang's BSD/MIT/Apache-2.0 parts) |
| MoltenVK `v1.4.2` (Khronos release `MoltenVK-macos.tar`) | `libMoltenVK.dylib` of the macOS package (Vulkan over Metal, for `renderer = vulkan` and `sh-upscale`); the prebuilt universal library fetched and SHA-256-checked by `tools/macos-deps/build.sh`, not in git | Apache-2.0 (`LICENSE-MoltenVK.txt`) |
| Real-ESRGAN model `realesrgan-x4plus` (ncnn `.param`/`.bin`) | fetched by `tools/ncnn/build.sh` from the Real-ESRGAN `v0.2.5.0` release (`realesrgan-ncnn-vulkan-20220424-ubuntu.zip`, SHA-256 pinned), not in git; beside `sh-upscale` in `upscale-models/` | BSD 3-Clause, Xintao Wang (the Real-ESRGAN repository's license at `v0.2.5.0`, whose release publishes the model; `LICENSE-Real-ESRGAN.txt`) |

A release package carries these texts in `licenses/` beside the executable (`tools/ci/package.sh`).

## 3. Files in the repository that are neither the project's nor freely licensed

No ownership is claimed over these and no license is granted for them. They are kept by the maintainer's decision:

- **Glyph art** — `gamedata/font/glyphs/cyrillic.glyphs`: the Cyrillic letters of the game font, converted from the repaint of the game's font by consolgames.ru (2011).

The maintainer also decided to keep in the repository:

- **The English game text** — `common.json`, `items.json` and `maps/*.json` of `gamedata/text/en/`: the messages, item names and item descriptions of Silent Hill (NTSC USA v1.1) in its original English, © Konami, in the project's readable markup. The game reads them from there instead of importing them from the disc; the translations are made from them.
- **Screenshots** — `docs/images/*.jpg`: pictures of the game running in this engine, shown in `README.md`. They show the game's art, © Konami; they are not part of any release package.

## 4. Game data

Silent Hill and all of its code, data, characters, text and audiovisual content are © Konami. Konami is not affiliated with this project and has not endorsed it.

**No game files are in this repository or in any release, except the English game text listed in section 3.** The player supplies a legally obtained disc image of Silent Hill (NTSC USA v1.1); on the first start the game extracts and converts models, textures, sound, video and the font from it into `gamedata/` beside the executable. What `gamedata/` in the repository holds is the project's own description of the game for its engine: configuration, scripts, tables, names, the English game text and the project's translations. The exceptions are listed in section 3.

Mods distributed by other people are the responsibility of their authors.

## 5. Contributing

Contributions are accepted under GPL-3.0-or-later; by submitting one you confirm you have the right to do so. Do not submit Konami-owned material, anything extracted from a game disc, or code from projects without a GPL-compatible license. Code of the decompilation may not be copied into `src/`, not even renamed.
