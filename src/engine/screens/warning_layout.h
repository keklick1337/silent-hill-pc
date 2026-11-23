// SPDX-License-Identifier: GPL-3.0-or-later
// Layout and timing of the boot warning screen as data: gamedata/menu/warning_screen.json,
// mod override by file name, built-in default when the file is missing or refused.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::screens {

struct WarningQuad {
    int x, w;    // framebuffer-space left edge and width
    int u0, u1;  // source texel range inside the page
};

struct WarningLayout {
    int fade_step = 8;      // fade level change per frame (255 -> 0 and back)
    int hold_frames = 180;  // frames the fully visible image stays
    int black_frames = 3;   // solid black frames flushed before returning
    int quad_y = -240, quad_h = 480;
    int first_page = 13;
    int tex_height = 224;
    std::vector<WarningQuad> quads = {{-320, 128, 64, 128}, {-192, 256, 0, 128}, {64, 256, 0, 128}};
};

Result<WarningLayout> parse_warning_layout(std::string_view json_text);
Result<WarningLayout> load_warning_layout(const std::filesystem::path& gamedata_dir,
                                          const std::filesystem::path& mods_dir, std::string* source);

}  // namespace sh::screens
