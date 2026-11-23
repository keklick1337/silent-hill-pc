// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The oversized-model registry of the running game: the active mods' files and the disc file table.

#include <cstdio>

#include "engine/log/sh_log.h"
#include "engine/mods/asset_files_c.h"
#include "engine/mods/mods_c.h"
#include "render/game_view.h"
#include "render/geometry/oversized_models.h"

namespace sh::models {

namespace {

class GameHost final : public OversizedModelHost {
public:
    bool mods_active() override { return ShMods_ActiveCount() > 0; }
    std::optional<std::string> mod_path(int file) override {
        char path[176];
        if (!sh::render::disc_file_loose_path(file, path, sizeof(path))) return std::nullopt;
        return std::string(path);
    }
    std::int64_t disc_bytes(int file) override { return sh::render::disc_file_sector_size(file); }
    std::int64_t mod_file_bytes(const std::string& path) override {
        std::FILE* f = ShModAsset_Open(path.c_str(), "rb");
        if (f == nullptr) return -1;
        const std::int64_t bytes = std::fseek(f, 0, SEEK_END) == 0 ? std::ftell(f) : -1;
        std::fclose(f);
        return bytes;
    }
    std::optional<std::vector<std::uint8_t>> mod_file(const std::string& path, std::int64_t bytes) override {
        std::FILE* f = ShModAsset_Open(path.c_str(), "rb");
        if (f == nullptr) return std::nullopt;
        std::vector<std::uint8_t> content(static_cast<std::size_t>(bytes));
        const bool whole = std::fread(content.data(), 1, content.size(), f) == content.size();
        std::fclose(f);
        if (!whole) return std::nullopt;
        return content;
    }
    void note(const std::string& line) override { SH_DBG("%s", line.c_str()); }
};

}  // namespace

OversizedModels& oversized_models() {
    static GameHost host;
    static OversizedModels registry(host);
    return registry;
}

}  // namespace sh::models
