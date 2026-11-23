// SPDX-License-Identifier: GPL-3.0-or-later
// ShVfs_ReadDataJson (vfs_c.h) for the C++ code of the game: a gamedata JSON file with every active mod's patch
// merged over it in load order. C++ only, so it stays out of vfs_c.h, which C-linkage blocks include.
#pragma once

#include "engine/vfs/vfs_c.h"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

// Nothing when no file has it. `fallback_dir` (optional): <fallback_dir>/<relative> read as it is when the VFS
// has nothing (a run without ShVfs_InitGame).
inline std::optional<std::string> ShVfs_ReadDataJsonText(const char* relative, const char* fallback_dir = nullptr) {
    size_t size = 0;
    char* raw = ShVfs_ReadDataJson(relative, &size);
    if (raw != nullptr) {
        std::string text(raw, size);
        std::free(raw);
        return text;
    }
    if (fallback_dir == nullptr || relative == nullptr) {
        return std::nullopt;
    }
    std::FILE* f = std::fopen((std::string(fallback_dir) + "/" + relative).c_str(), "rb");
    if (f == nullptr) {
        return std::nullopt;
    }
    std::string text;
    char chunk[16384];
    for (size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) {
        text.append(chunk, n);
    }
    std::fclose(f);
    return text;
}
