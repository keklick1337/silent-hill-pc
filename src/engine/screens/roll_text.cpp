// SPDX-License-Identifier: GPL-3.0-or-later
// The staff roll's text drawers. All arithmetic is 32-bit with wrap-around and the
// Q.12 products are taken in 64 bits and truncated, as the roll's specification does.

#include "engine/screens/roll_text.h"

#include "engine/screens/roll_math.h"

namespace sh::screens {

namespace {

constexpr std::int32_t kGlyphSize = 24;
constexpr std::int32_t kAtlasColumns = 10;
constexpr std::int32_t kLogoCell = 16;
constexpr std::int32_t kLogoDrop = 4;  // logo cells sit 4 pixels below the text line

bool is_span_derived(std::uint32_t code) {
    // The four advance codes a span measure writes are skipped while measuring.
    return code >= roll_code::kForwardHalf;
}

bool is_logo(std::uint32_t code) {
    return (code >= roll_code::kLogoFirst && code < roll_code::kLogoEnd) ||
           (code >= roll_code::kLogo2First && code < roll_code::kLogo2End);
}

// Stores the measured width of span `code` (0xD0..0xD7) into its four advance codes.
void store_span(RollFont& font, std::uint32_t code, std::int32_t width) {
    const std::int16_t half = static_cast<std::int16_t>(width / 2);
    font.advance[code + 16] = half;
    font.advance[code + 32] = static_cast<std::int16_t>(-half);
    font.advance[code + 24] = static_cast<std::int16_t>(width);
    font.advance[code + 40] = static_cast<std::int16_t>(-static_cast<std::int16_t>(width));
}

bool ends_span(std::uint32_t next, std::uint32_t code) {
    return next == code + roll_code::kSpanCloseOffset || next == code || next == roll_code::kNewline ||
           next == roll_code::kReturn || next == roll_code::kHome;
}

// The atlas glyph a logo code is drawn with in the perspective drawer.
std::uint32_t logo_to_glyph(const RollFont& font, std::uint32_t code) {
    if (code >= roll_code::kLogoFirst && code < roll_code::kLogoEnd) {
        return font.logo_glyphs[code - roll_code::kLogoFirst];
    }
    return font.logo_glyphs_2[code - roll_code::kLogo2First];
}

}  // namespace

void FlatRollText::set_position(std::int32_t x, std::int32_t y) {
    x_ = x;
    y_ = y;
    margin_ = x;
}

void FlatRollText::draw(const std::uint8_t* text, RollSink& sink) {
    if (text == nullptr) {
        return;
    }
    std::int32_t x = x_;
    std::int32_t y = y_;
    std::uint32_t rgb = rgb_;

    for (const std::uint8_t* p = text; *p != 0; ++p) {
        const std::uint32_t code = *p;
        const std::int32_t advance = font_.advance[code];

        if (code >= roll_code::kGlyphFirst && code < roll_code::kGlyphEnd) {
            const std::int32_t cell = static_cast<std::int32_t>(code - roll_code::kGlyphFirst);
            RollPrim r;
            r.kind = RollPrim::Kind::Glyph;
            r.x[0] = x;
            r.y[0] = y;
            r.uv[0] = (cell % kAtlasColumns) * kGlyphSize + ((cell / kAtlasColumns) * kGlyphSize) * 256;
            r.rgb = rgb;
            r.semi_transparent = semi_;
            sink.prim(r);
        } else if (code >= roll_code::kLogoFirst && code < roll_code::kLogoEnd) {
            RollPrim r;
            r.kind = RollPrim::Kind::LogoGlyph;
            r.x[0] = x;
            r.y[0] = y + kLogoDrop;
            // First logo set: one column at u = 240, cells stacked down v.
            r.uv[0] = static_cast<std::int32_t>(code - roll_code::kLogoFirst) * 256 * kLogoCell +
                      kLogoCell * (kLogoCell - 1);
            r.rgb = rgb;
            r.semi_transparent = semi_;
            sink.prim(r);
        } else if (code >= roll_code::kLogo2First && code < roll_code::kLogo2End) {
            RollPrim r;
            r.kind = RollPrim::Kind::LogoGlyph;
            r.x[0] = x;
            r.y[0] = y + kLogoDrop;
            // Second logo set: along the bottom row v = 240, from u = 128.
            r.uv[0] = static_cast<std::int32_t>(code - roll_code::kLogo2First) * kLogoCell +
                      256 * kLogoCell * (kLogoCell - 1) + kLogoCell * 8;
            r.rgb = rgb;
            r.semi_transparent = semi_;
            sink.prim(r);
        } else if (code >= roll_code::kColorFirst && code <= roll_code::kColorLast) {
            rgb = font_.colors[code - roll_code::kColorFirst];
        } else if (code >= roll_code::kSpanFirst && code < roll_code::kSpanEnd) {
            std::int32_t width = 0;
            for (const std::uint8_t* q = p + 1;; ++q) {
                const std::uint32_t next = *q;
                if (ends_span(next, code) || next == 0) {
                    break;
                }
                if (!is_span_derived(next)) {
                    width += font_.advance[next];
                }
            }
            store_span(font_, code, width);
        }

        switch (code) {
            case roll_code::kNewline:
                x = margin_;
                y += line_height_;
                break;
            case roll_code::kLineDown:
                y += line_height_;
                break;
            case roll_code::kReturn:
                x = margin_;
                break;
            case roll_code::kHome:
                x = x_;
                y = y_;
                break;
            default:
                x += advance;
                break;
        }
    }

    RollPrim page;
    page.kind = RollPrim::Kind::TexturePage;
    sink.prim(page);
    x_ = x;
    y_ = y;
}

void PerspectiveRollText::set_plane(const RollPlane& plane) {
    plane_ = plane;
    x_ = 0;
    y_ = 0;
    margin_ = 0;
    line_height_ = 24;
    semi_ = false;
    rgb_ = 0xFFFFFF;
    p_ = project(0, 0);
}

PerspectiveRollText::Projection PerspectiveRollText::project(std::int32_t x, std::int32_t y) const {
    using namespace roll_math;
    const RollPlane& pl = plane_;
    const std::int32_t num = shl(pl.focal, 22);
    const std::int32_t top_inv = num / (add(pl.origin_z, q12_mul(pl.scale, mul(y, pl.row_z))) >> 2);
    const std::int32_t bottom_inv =
        num / (add(pl.origin_z, q12_mul(pl.scale, mul(add(y, kGlyphSize), pl.row_z))) >> 2);
    const std::int32_t at_x = add(pl.origin_x, q12_mul(pl.scale, mul(x, pl.column_x)));
    const std::int32_t one_x = add(pl.origin_x, q12_mul(pl.scale, pl.column_x));

    Projection p;
    p.top_x = q12_mul(top_inv, at_x);
    p.bottom_x = q12_mul(bottom_inv, at_x);
    p.top_dx = sub(q12_mul(top_inv, one_x), q12_mul(top_inv, pl.origin_x));
    p.bottom_dx = sub(q12_mul(bottom_inv, one_x), q12_mul(bottom_inv, pl.origin_x));
    p.top_y = q12_mul(top_inv, add(pl.origin_y, q12_mul(pl.scale, mul(y, pl.row_y))));
    p.bottom_y = q12_mul(bottom_inv, add(pl.origin_y, q12_mul(pl.scale, mul(add(y, kGlyphSize), pl.row_y))));
    return p;
}

void PerspectiveRollText::set_position(std::int32_t x, std::int32_t y) {
    using namespace roll_math;
    if (y != y_) {
        p_ = project(x, y);
        x_ = x;
        y_ = y;
        margin_ = x;
    } else if (x != x_) {
        const std::int32_t dx = sub(x, x_);
        x_ = x;
        p_.top_x = add(p_.top_x, mul(dx, p_.top_dx));
        p_.bottom_x = add(p_.bottom_x, mul(dx, p_.bottom_dx));
    }
}

void PerspectiveRollText::draw(const std::uint8_t* text, RollSink& sink) {
    using namespace roll_math;
    if (text == nullptr) {
        return;
    }
    std::int32_t x = x_;
    std::int32_t y = y_;
    std::uint32_t rgb = rgb_;
    Projection p = p_;

    for (const std::uint8_t* s = text; *s != 0; ++s) {
        std::uint32_t code = *s;
        const bool logo = is_logo(code);
        if (logo) {
            code = logo_to_glyph(font_, code);
        }
        std::int32_t advance = font_.advance[code];

        if (code >= roll_code::kGlyphFirst && code < roll_code::kGlyphEnd) {
            const std::int32_t cell = static_cast<std::int32_t>(code - roll_code::kGlyphFirst);
            const std::int32_t u = (cell % kAtlasColumns) * kGlyphSize;
            const std::int32_t row = (cell / kAtlasColumns) * kGlyphSize;
            RollPrim r;
            r.kind = RollPrim::Kind::Quad;
            r.uv[0] = u + row * 256;
            r.uv[1] = (u + advance) + row * 256;
            r.uv[2] = static_cast<std::int32_t>(static_cast<std::uint32_t>(u) +
                                                (static_cast<std::uint32_t>(row) * 256u + kGlyphSize * 256u));
            r.uv[3] = static_cast<std::int32_t>(static_cast<std::uint32_t>(u + advance) +
                                                (static_cast<std::uint32_t>(row) * 256u + kGlyphSize * 256u));

            std::int32_t top_w;
            std::int32_t bottom_w;
            std::int32_t bottom_y;
            if (logo) {
                // Logo glyphs are drawn at two thirds of the atlas glyph, top-aligned.
                advance = (advance * 2) / 3;
                top_w = mul(p.top_dx, advance);
                bottom_w = mul(p.bottom_dx, advance);
                bottom_w = add((sub(bottom_w, top_w) * 2) / 3, top_w);
                bottom_y = add((sub(p.bottom_y, p.top_y) * 2) / 3, p.top_y);
            } else {
                top_w = mul(p.top_dx, advance);
                bottom_y = p.bottom_y;
                bottom_w = mul(p.bottom_dx, advance);
            }
            r.x[0] = p.top_x >> 12;
            r.y[0] = p.top_y >> 12;
            r.x[1] = add(p.top_x, top_w) >> 12;
            r.y[1] = p.top_y >> 12;
            r.x[2] = p.bottom_x >> 12;
            r.y[2] = bottom_y >> 12;
            r.x[3] = add(p.bottom_x, bottom_w) >> 12;
            r.y[3] = bottom_y >> 12;
            r.rgb = rgb;
            r.semi_transparent = semi_;
            sink.prim(r);
        } else if (code >= roll_code::kColorFirst && code <= roll_code::kColorLast) {
            rgb = font_.colors[code - roll_code::kColorFirst];
        } else if (code >= roll_code::kSpanFirst && code < roll_code::kSpanEnd) {
            std::int32_t width = 0;
            for (const std::uint8_t* q = s + 1;; ++q) {
                std::uint32_t next = *q;
                if (ends_span(next, code) || next == 0) {
                    break;
                }
                if (is_span_derived(next)) {
                    continue;
                }
                if (is_logo(next)) {
                    next = logo_to_glyph(font_, next);
                    width += (font_.advance[next] * 2) / 3;
                } else {
                    width += font_.advance[next];
                }
            }
            store_span(font_, code, width);
        }

        bool x_moved = false;
        bool y_moved = false;
        switch (code) {
            case roll_code::kNewline:
                x = margin_;
                y += line_height_;
                y_moved = true;
                break;
            case roll_code::kLineDown:
                y += line_height_;
                y_moved = true;
                break;
            case roll_code::kReturn:
                // The original steps the projected cursor by the distance back to the
                // margin in the forward direction; kept as specified.
                advance = sub(x, margin_);
                x = margin_;
                x_moved = true;
                break;
            case roll_code::kHome:
                x = x_;
                y = y_;
                y_moved = true;
                break;
            default:
                x += advance;
                x_moved = true;
                break;
        }

        if (y_moved) {
            p = project(x, y);
        } else if (x_moved) {
            p.top_x = add(p.top_x, mul(advance, p.top_dx));
            p.bottom_x = add(p.bottom_x, mul(advance, p.bottom_dx));
        }
    }

    x_ = x;
    y_ = y;
    p_ = p;
}

}  // namespace sh::screens
