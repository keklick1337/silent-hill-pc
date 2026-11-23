// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// UTF-8 core for the engine's future i18n: strict decode (rejects invalid and
// overlong sequences, surrogates, out-of-range codepoints; the error carries
// the byte offset), counting, and codepoint-wise view advance for text shaping.

#pragma once

#include "formats/common/status.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace sh::utf8 {

Result<std::vector<char32_t>> decode(std::string_view s);

bool valid(std::string_view s);

std::size_t codepoint_count(std::string_view s);

// Advances the view by N codepoints; returns false (view unchanged) if it
// would run past the end or hit an invalid sequence.
bool advance(std::string_view& s, std::size_t codepoints);

// Future codepoint -> font-cell hook for localized text rendering: the default
// maps ASCII to itself and everything else to "no cell"; the font system will
// override this once per-language glyph atlases exist.
struct GlyphMap {
    virtual ~GlyphMap() = default;
    virtual int cell_of(char32_t cp) const { return cp < 0x80 ? static_cast<int>(cp) : -1; }
};

}  // namespace sh::utf8
