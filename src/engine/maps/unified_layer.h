#pragma once

// The engine reads the map files from the unified map files (formats/convert/level/map_unified.h) when
// map_format = unified: a VFS layer that answers BG/*.IPD and BG/*.PLM with the
// bytes packed in <root>/maps/areas/*.glb. The game loads those files by LBA (DiscSectors), so a path the layer has
// replaces the sectors of raw/; paths it lacks fall through to the layers below.

#include "engine/vfs/vfs.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace sh::maps {

// level 0: info, 1: warning.
using UnifiedLogFn = std::function<void(int level, const std::string& message)>;

// Null when <root>/maps/areas holds no readable unified file. Broken files are logged and skipped.
std::unique_ptr<sh::vfs::Layer> make_unified_maps_layer(const std::filesystem::path& root, std::string name,
                                                        const UnifiedLogFn& log);

}  // namespace sh::maps
