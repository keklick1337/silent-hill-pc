// SPDX-License-Identifier: GPL-3.0-or-later
// The shared glyph emission: one laid-out glyph becomes a Sprite, a hi-res quad or a
// scaled quad, with the original's exact fixed-point arithmetic. Internal to the module.

#pragma once

#include "engine/text/text_types.h"

namespace sh::text::detail {

struct GlyphStyle {
    std::uint32_t color = 0;   // r | g << 8 | b << 16
    bool hi_res = false;       // state at emission time
    std::int32_t scale = kUnityScale;
    std::int32_t anchor_x = 0; // scaling origin
    std::int32_t anchor_y = 0;
};

// Emits one glyph at (x, y) from atlas cell (u, v) on page/clut. With a scale active
// the quad is mapped about the anchor (and never edge-cut, as in the original scaled
// path); in hi-res mode the quad spans y*2 .. y*2 + hi_res_bottom; otherwise a sprite.
void emit_glyph(GlyphSink& sink, const FontAtlas& font, const GlyphStyle& style,
                std::int32_t x, std::int32_t y, std::int32_t u, std::int32_t v,
                std::int32_t page, std::int32_t clut, std::int32_t cut_left, std::int32_t cut_right,
                std::int32_t height = 0);

}  // namespace sh::text::detail
