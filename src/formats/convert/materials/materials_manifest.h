// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// convert/materials.json: which texture PNGs of
// convert/textures/ each converted model, level and map area uses, and which consumers use
// each picture. A texture modder replaces the files consumers actually reference.
//
// The consumers are the .glb files the import wrote. After the texture bake (texture_bake.h)
// every material's image names a baked true-colour picture (NAME.png or a variant
// NAME.<label>.png); a .pNN.png name is left only where the bake could not handle the
// material (no indexed source, a palette outside the TIM) and is built on demand.

#pragma once

#include "formats/common/status.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace sh::convert::materials {

struct Report {
    unsigned consumers = 0;       // .glb files parsed
    unsigned referenced = 0;      // distinct texture PNGs some consumer references
    unsigned textures = 0;        // PNGs in convert/textures/
    unsigned variants = 0;        // referenced baked variants (NAME.<label>.png)
    unsigned palette_names = 0;   // referenced .pNN.png names (not baked, built on demand)
    std::vector<std::string> warnings;
    std::string json_path;        // "" when nothing was written
};

// Walks <gamedata>/convert/ (models/**.glb, levels/**.glb and <gamedata>/maps/areas/*.glb as consumers, textures/**.png as
// the texture set) and writes <gamedata>/convert/materials.json. Never fails on one bad glb:
// the message goes to `warnings` and the file is skipped.
Result<Report> write_manifest(const std::filesystem::path& gamedata_dir);

// "materials: 826 consumers, 1116/2410 textures referenced (393 baked variants, 0 palette-row names)"
std::string format_report_line(const Report& report);

// For tests and tools: the manifest of specific consumer dirs, as JSON text.
std::string build_manifest_text(const std::vector<std::filesystem::path>& consumer_dirs,
                                const std::filesystem::path& textures_dir, Report& report);

}  // namespace sh::convert::materials
