// SPDX-License-Identifier: GPL-3.0-or-later
// Shared reading of a screen's data file: gamedata/<relative>, replaced by the newest mod that
// ships the same relative path. Used by the screen loaders written after save_layout.
#pragma once

#include <filesystem>
#include <string>

#include "formats/common/json.h"
#include "formats/common/status.h"

namespace sh::screens {

// Whole text of the winning file; `source` (optional) receives its path.
Result<std::string> read_screen_file(const std::filesystem::path& gamedata_dir,
                                     const std::filesystem::path& mods_dir,
                                     const std::filesystem::path& relative, std::string* source);

// "line N: " prefix for an error about `value`.
std::string at_line(const json::Value& value);

// [r, g, b, a] with every channel a number in 0..1.
Result<void> read_rgba(const json::Value& value, const std::string& key, float out[4]);

}  // namespace sh::screens
