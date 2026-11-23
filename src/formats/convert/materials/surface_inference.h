// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// convert/surface_materials.json: surface materials the import proposes for the map's texture pages
// (docs/RENDER.md section 10), read from the pictures themselves so the result is the same on every
// import and can be reviewed: a page of floor tiles (a regular grid of grout lines) gets a faint wet sheen, a page of
// bare grey metal (unsaturated, smooth, mid bright) a metallic gloss. Every other page is left out, so it stays
// neutral. A file in gamedata/materials/ of the page's name overrides an entry key by key, and any hand-written
// material that names the page wins over it.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "formats/common/status.h"

namespace sh::convert::materials {

struct PictureStats {
    float luminance = 0;   // mean of the opaque texels, 0..1
    float saturation = 0;  // mean (max - min) / max
    float detail = 0;      // mean luminance step to the right and below neighbours
    float grid = 0;        // 0..1: how strongly rows and columns repeat with a tile's period (8, 16 or 32 texels)
    int grid_period = 0;
    unsigned opaque = 0;  // texels with alpha > 0
    float filler = 0;     // the share of the opaque texels that have the page's most common colour (empty space)
};

enum class SurfaceKind { None, Tiles, Metal };

struct InferredSurface {
    std::string page;  // "HP0101F"
    SurfaceKind kind = SurfaceKind::None;
    PictureStats stats;
};

// The statistics of an RGBA picture (unit tested).
PictureStats picture_stats(const unsigned char* rgba, unsigned width, unsigned height);
SurfaceKind classify(const PictureStats& s);

struct InferenceReport {
    unsigned pages = 0;  // map texture pages looked at
    unsigned tiles = 0;
    unsigned metal = 0;
    std::vector<std::string> warnings;
};

// Looks at <gamedata>/convert/textures/BG/*.png (the pages themselves, not their palette variants) and writes
// <gamedata>/convert/surface_materials.json.
Result<InferenceReport> write_surface_inference(const std::filesystem::path& gamedata_dir);

// "surface materials: 1840 pages, 12 tiles, 4 metal"
std::string format_inference_line(const InferenceReport& report);

}  // namespace sh::convert::materials
