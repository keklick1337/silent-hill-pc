// SPDX-License-Identifier: GPL-3.0-or-later
// Text for the screens shown before the game font exists (the quick setup and the disc import): lines drawn
// from the TrueType fonts of gamedata/font and, for the characters those lack (CJK, Arabic), the system's fonts,
// chosen by the same catalogue the game's fallback text uses (i18n/core/font_catalog.h). Arabic letters take their
// joined forms and right-to-left text its visual order (i18n/core/text_prepare.h).
#pragma once

#include "i18n/core/font_catalog.h"
#include "i18n/core/outline_font.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::screen_kit {

// One line as coverage (0..255), rows top down.
struct TextBitmap {
    int width = 0;
    int height = 0;
    int baseline = 0;  // rows from the top to the line the letters stand on
    std::vector<std::uint8_t> alpha;
};

class TextRaster {
public:
    explicit TextRaster(const std::filesystem::path& gamedata_fonts);

    // `px` is the em size in pixels; `families` are the faces wanted, best first (the rest by the catalogue's
    // ranking). `rtl` is the base direction of the line.
    TextBitmap render(std::string_view utf8, float px, std::span<const std::string> families, bool rtl);

private:
    const sh::i18n::OutlineFont* font(const sh::i18n::FontFace& face);

    sh::i18n::FontCatalog catalog_;
    bool system_added_ = false;
    std::map<std::string, std::unique_ptr<sh::i18n::OutlineFont>> fonts_;
};

}  // namespace sh::screen_kit
