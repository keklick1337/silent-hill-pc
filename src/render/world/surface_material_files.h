// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The files of the surface materials (render/scene/materials.h): gamedata/materials/<Name>.json over the import's
// inferred defaults, with the mods' patches.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "render/scene/materials.h"

namespace sh::world {

std::vector<scene::SurfaceMaterial> load_surface_materials(const std::filesystem::path& gamedata_dir,
                                                           const std::filesystem::path& mods_dir,
                                                           std::vector<std::string>* errors);

}  // namespace sh::world
