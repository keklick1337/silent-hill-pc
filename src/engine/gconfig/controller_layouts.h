// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Default controller layouts as data: gamedata/config/controller_layouts.json.
// Per region, three layout types (A, B, C); each maps the 14 input actions to a PS1 button mask.
// Mods patch the file in load order (engine/mods/data_merge.h).

#pragma once

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sh::gconfig {

// Action order matches the game's s_ControllerConfig field order.
constexpr std::size_t kLayoutActionCount = 14;
constexpr std::size_t kLayoutTypeCount = 3;

struct ControllerLayout {
    std::array<std::uint16_t, kLayoutActionCount> masks{};
};

struct ControllerLayoutLoadSource {
    std::string mod_id;
};

class ControllerLayoutLibrary {
public:
    static Result<ControllerLayoutLibrary> load(const std::filesystem::path& gamedata_dir,
                                                const std::filesystem::path& mods_dir,
                                                std::vector<std::string>& warnings,
                                                ControllerLayoutLoadSource* source = nullptr);
    static Result<ControllerLayoutLibrary> parse(std::string_view json);

    // region: "usa"; nullptr if the file has no such region.
    const std::array<ControllerLayout, kLayoutTypeCount>* region(const std::string& name) const;

private:
    std::vector<std::pair<std::string, std::array<ControllerLayout, kLayoutTypeCount>>> regions_;
};

}  // namespace sh::gconfig
