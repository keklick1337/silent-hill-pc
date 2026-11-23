// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The texture bake (importer 3): every 4/8-bit TIM becomes true-colour pictures that show each texel in the CLUT
// row the game draws it with. The consumers are the converted models and levels (convert/models/**.glb,
// convert/levels/**.glb) and the map areas (maps/areas/*.glb): each triangle of a material whose extras name a TIM
// and a palette row marks the texels it can sample (the coverage rule below). draw_usage.json adds the TIMs drawn
// without geometry (UI, effects, background pictures).
//
// Per TIM: textures/<DIR>/<NAME>.png holds every texel in its most used row (the base); a row that disagrees with
// the base on some texel it draws gets a variant textures/<DIR>/<NAME>.<label>.png (the base with those texels
// replaced; rows that never disagree with each other share one variant). The consumer glbs are rewritten so each
// material's image and extras.png name the picture of its (TIM, row). The bake reads tim/palette from the material
// extras, not from the image URI, so running it again gives the same files.
//
// Alpha: 0 for the CLUT word 0x0000; 128 for a word with STP (0x8000) at a texel drawn by a semi-transparent
// material of any consumer (maps/areas, convert/levels, convert/models: characters and items blend their STP texels
// like the map does) or a semi-transparent draw usage with that row; 255 otherwise.

#pragma once

#include "formats/common/status.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::convert::materials {

struct BakeReport {
    unsigned consumers = 0;         // glb files with at least one material the bake handles
    unsigned glbs_rewritten = 0;    // of those, files whose JSON chunk changed
    unsigned usages = 0;            // draw_usage entries applied
    unsigned tims = 0;              // TIMs baked
    unsigned variants = 0;          // variant pictures (NAME.label.png)
    unsigned pictures_written = 0;  // base and variant files whose bytes changed
    unsigned stale_removed = 0;     // variant files of an earlier bake that this one no longer makes
    std::vector<std::string> warnings;
    std::string json_path;  // convert/texture_bake.json
};

// Bakes <gamedata>/convert/textures/ from convert/indexed/, the .tim.json rows and the consumer glbs, rewrites the
// glbs and writes convert/texture_bake.json. threads = 0: one per hardware thread. The output does not depend on
// the thread count. A bad glb or TIM is a warning (left as it is); the call fails only without convert/textures/.
Result<BakeReport> bake_textures(const std::filesystem::path& gamedata_dir, unsigned threads = 0);

// The same with another draw usage table (tests); bake_textures uses the one compiled in.
Result<BakeReport> bake_textures_with_usage(const std::filesystem::path& gamedata_dir, unsigned threads,
                                            std::string_view draw_usage_json);

// draw_usage.json as compiled into the library.
std::string_view builtin_draw_usage_json();

// "textures: 723 TIMs baked, 393 variants, 812 consumers (812 rewritten), 1116 pictures written"
std::string format_bake_line(const BakeReport& report);

// The coverage rule on its own (tests, tools): the texels of a width x height picture that the triangles can
// sample. `uv` holds 6 floats per triangle (u0 v0 u1 v1 u2 v2, glTF texture coordinates). With t = round(uv * size)
// - 0.5, texel x is drawn when floor(min t) <= x <= floor(max t) (per axis) and the square (x - 0.5, x + 1.5) x
// (y - 0.5, y + 1.5) overlaps the open triangle; a zero-area triangle draws its whole box. That is the union of the
// PS1 texel, the GPU's nearest texel and the bilinear taps clamped to the box. Returns width * height bytes, 0 or 1.
std::vector<std::uint8_t> coverage_mask(int width, int height, std::span<const float> uv);

}  // namespace sh::convert::materials
