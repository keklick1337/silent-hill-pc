// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/text/game_text.h"

#include "engine/text/debug_font.h"
#include "engine/text/number_format.h"
#include "engine/text/string_layout.h"

#include <cstring>

namespace sh::text {
namespace {

namespace d2 = sh::draw2d;

// Packet code byte of the strings' sprites (a textured free-size rectangle, opaque, modulated): the colour words
// of the table carry it in their high byte, and the sprite's flags are read back from there.
constexpr std::uint32_t kSpriteCode = 0x64u;

// The colour id the PAL dimming of set_light_grey rewrites (text_style.json "light_grey").
constexpr std::int32_t kLightGrey = 6;

// Authored line count of the PAL 9-line grid anchor (not the runtime cap).
constexpr std::int32_t kGridLines = 9;

struct Texel {
    d2::TexCoord uv;
    std::uint16_t extra;
};
Texel texel(std::uint32_t word) {
    return {{static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8)},
            static_cast<std::uint16_t>(word >> 16)};
}

d2::Rgb rgb(std::uint32_t word) {
    return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8),
            static_cast<std::uint8_t>(word >> 16)};
}

class Draw2dSink final : public GlyphSink {
public:
    Draw2dSink(GameTextHost& host, std::int32_t layer) : host_(host), layer_(layer) {}

    void glyph(const GlyphDraw& g) override {
        const GlyphCommands c = glyph_commands(g);
        // Sprites only come while hi-res is off, so they always take the run.
        const TextStream stream = g.sprite || !g.hi_res ? TextStream::Run : TextStream::Shared;
        for (std::uint8_t i = 0; i < c.count; ++i) {
            host_.submit(layer_, c.command[i], stream);
        }
    }

private:
    GameTextHost& host_;
    std::int32_t layer_;
};

class DebugSink final : public DebugGlyphSink {
public:
    DebugSink(GameTextHost& host, std::int32_t layer) : host_(host), layer_(layer) {}

    void glyph(const DebugGlyph& g) override {
        const std::int32_t idx = g.index;  // may be negative: packed anyway, as the original did
        const Texel tex = texel(static_cast<std::uint32_t>(((idx & 0x1F) * 8) + ((((idx >> 5) * 8) + 240) << 8)) +
                                (0x7FD2u << 16));
        d2::Sprite s;
        s.size = d2::RectSize::Px8;
        s.color = {128, 128, 128};
        s.at = {static_cast<std::int16_t>(g.x), static_cast<std::int16_t>(g.y)};
        s.uv = tex.uv;
        s.palette = d2::Palette::from_word(tex.extra);
        if (g.present) {
            host_.submit(layer_, s, TextStream::Run);
        } else {
            host_.reserve(s, TextStream::Run);
        }
    }

private:
    GameTextHost& host_;
    std::int32_t layer_;
};

}  // namespace

GlyphCommands glyph_commands(const GlyphDraw& g) {
    GlyphCommands out;
    if (g.sprite) {
        const std::uint32_t color = g.color | (kSpriteCode << 24);
        const Texel tex = texel(static_cast<std::uint32_t>(g.u) + (static_cast<std::uint32_t>(g.v) << 8) +
                                (static_cast<std::uint32_t>(g.clut) << 16));
        d2::Sprite s;
        s.color = rgb(color);
        s.shade = {(color >> 24 & 2) != 0, (color >> 24 & 1) != 0};
        s.size = static_cast<d2::RectSize>(color >> 27 & 3);
        s.at = {static_cast<std::int16_t>(g.x[0] + g.cut_left), static_cast<std::int16_t>(g.y[0])};
        s.uv = {static_cast<std::uint8_t>(tex.uv.u + g.cut_left), tex.uv.v};
        s.palette = d2::Palette::from_word(tex.extra);
        s.w = static_cast<std::uint16_t>(g.cell_width - (g.cut_left + g.cut_right));
        s.h = static_cast<std::uint16_t>(g.cell_height);

        d2::DrawMode mode;
        mode.page = d2::TexturePage::from_word(static_cast<std::uint16_t>(g.page));
        mode.dither = true;

        out.count = 2;
        out.command[0] = s;
        out.command[1] = mode;
        return out;
    }

    const std::uint32_t u = static_cast<std::uint32_t>(g.u);
    const std::uint32_t v = static_cast<std::uint32_t>(g.v);
    const std::uint32_t w = static_cast<std::uint32_t>(g.cell_width);
    const std::uint32_t h = static_cast<std::uint32_t>(g.cell_height);
    const Texel t0 = texel(u + (v << 8) + (static_cast<std::uint32_t>(g.clut) << 16));
    const Texel t1 = texel(u + (static_cast<std::uint32_t>(g.page) << 16) + ((v + h - 1) << 8));
    const Texel t2 = texel(static_cast<std::uint16_t>(u + w + (v << 8)));
    const Texel t3 = texel(static_cast<std::uint16_t>(u + w + ((v + h - 1) << 8)));

    d2::Polygon p;
    p.count = 4;
    p.textured = true;
    p.color[0] = rgb(g.color);
    p.xy[0] = {static_cast<std::int16_t>(g.x[0] + g.cut_left), static_cast<std::int16_t>(g.y[0])};
    p.xy[1] = {static_cast<std::int16_t>(g.x[1] + g.cut_left), static_cast<std::int16_t>(g.y[1])};
    p.xy[2] = {static_cast<std::int16_t>(g.x[2] - g.cut_right), static_cast<std::int16_t>(g.y[2])};
    p.xy[3] = {static_cast<std::int16_t>(g.x[3] - g.cut_right), static_cast<std::int16_t>(g.y[3])};
    p.uv[0] = {static_cast<std::uint8_t>(t0.uv.u + g.cut_left), t0.uv.v};
    p.uv[1] = {static_cast<std::uint8_t>(t1.uv.u + g.cut_left), t1.uv.v};
    p.uv[2] = {static_cast<std::uint8_t>(t2.uv.u - g.cut_right), t2.uv.v};
    p.uv[3] = {static_cast<std::uint8_t>(t3.uv.u - g.cut_right), t3.uv.v};
    p.palette = d2::Palette::from_word(t0.extra);
    p.page = d2::TexturePage::from_word(t1.extra);

    out.count = 1;
    out.command[0] = p;
    return out;
}

GameText::GameText(GameTextHost& host) : host_(host) {}

void GameText::set_style(const TextStyle& style) {
    style_ = style;
    std::memcpy(rgb_, style.rgb.data(), sizeof(rgb_));
}

void GameText::set_position(std::int32_t x, std::int32_t y) {
    if (x != kKeep) {
        cursor_.x = static_cast<std::int16_t>(x - kScreenOriginX);
        cursor_.line_start_x = static_cast<std::int16_t>(x - kScreenOriginX);
    }
    if (y != kKeep) {
        cursor_.y = static_cast<std::int16_t>(y - kScreenOriginY);
    }
    layer_ = kDefaultTextLayer;
}

void GameText::set_clip_x(std::int32_t x0, std::int32_t x1) {
    clip_x0_ = x0 - kScreenOriginX;
    clip_x1_ = x1 - kScreenOriginX;
}

void GameText::set_light_grey(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    rgb_[kLightGrey] = r | static_cast<std::uint32_t>(g) << 8 | static_cast<std::uint32_t>(b) << 16;
}

ColorTable GameText::colors() const {
    ColorTable c;
    std::memcpy(c.rgb, rgb_, sizeof(c.rgb));
    return c;
}

MessageSpec GameText::message_spec() {
    MessageSpec s;
    s.line_cap = host_.message_line_cap();
    s.display_all_length = style_.display_all_length;
    s.text_size_scale = static_cast<std::int32_t>((host_.text_size_percent() * 4096.0f) / 100.0f);
    s.msg_v_shift = host_.message_v_shift();
    s.cutscene_active = host_.cutscene_active();
    s.right_to_left = host_.right_to_left();
    using F = MessageBoxAnchors::Formula;
    s.anchors.formula[0] = F::Fixed;        // top
    s.anchors.formula[1] = F::RisePerLine;  // bottom, rises with the line count
    s.anchors.formula[2] = F::Fixed;        // upper middle
    s.anchors.formula[3] = F::RisePerLine;  // bottom, rises
    s.anchors.formula[4] = F::Grid8;        // PAL's 9-line grid
    for (std::size_t i = 0; i < kMessageBoxPositions; ++i) {
        s.anchors.y_base[i] = style_.message_box_y[i];
    }
    s.anchors.grid_lines = kGridLines;
    return s;
}

bool GameText::draw(const char* text, std::int32_t length) {
    const bool hi_res = host_.hi_res();
    host_.begin_run(hi_res);

    Draw2dSink sink(host_, layer_);
    StringDrawQuery q;
    q.text = host_.menu_text(text);
    q.length = length;
    q.cursor_x = cursor_.x;
    q.cursor_y = cursor_.y;
    q.line_start_x = cursor_.line_start_x;
    q.color_id = color_id_;
    q.hi_res = hi_res;
    q.scale = scale_;
    q.clip_x0 = clip_x0_;
    q.clip_x1 = clip_x1_;
    const StringDrawResult r = draw_string(sink, host_.chars(), host_.font(), colors(), q);

    host_.end_run(hi_res);
    cursor_.x = r.cursor_x;
    cursor_.y = r.cursor_y;
    color_id_ = static_cast<std::int16_t>(r.color_id);
    return r.terminated;
}

void GameText::draw_int(std::int32_t value) {
    const NumberText n = format_decimal(value);
    std::memcpy(number_, n.bytes, static_cast<std::size_t>(n.count));
    number_[n.count] = 0;
    draw(number_, 5);
}

void GameText::measure_message(std::int32_t index) {
    MessageMeasureQuery q;
    q.msg = host_.map_message(index);
    q.position_idx = box_position_;
    q.hi_res = host_.hi_res();
    std::memcpy(q.widths, line_widths_, sizeof(q.widths));
    const MessageMeasureResult r = layout_.measure(host_.chars(), host_.font(), message_spec(), q);

    std::memcpy(line_widths_, r.widths, sizeof(line_widths_));
    line_count_ = r.line_count;
    box_position_ = static_cast<std::uint8_t>(r.position_idx);
    host_.set_audio_load_block(static_cast<std::uint8_t>(r.audio_load_block));
    host_.set_hi_res(r.hi_res);
}

std::int32_t GameText::draw_message(const char* message, std::int32_t length) {
    const bool hi_res = host_.hi_res();
    host_.begin_run(hi_res);

    MessageDrawQuery q;
    q.msg = message;
    q.length = length;
    q.color_id = color_id_;
    q.hi_res = hi_res;
    q.timer_running = host_.message_timer_running();
    q.position_idx = box_position_;
    q.fallback_y = cursor_.y;
    std::memcpy(q.widths, line_widths_, sizeof(q.widths));
    q.line_count = line_count_;

    host_.release_text_layer_exemption();

    Draw2dSink sink(host_, kDefaultTextLayer);
    const MessageDrawResult r = layout_.draw(sink, host_.chars(), host_.font(), colors(), message_spec(), q);

    // The hi-res state at the end decides: a ~H mid-page switches it on and the run is then abandoned.
    host_.end_run(r.hi_res);
    cursor_.x = r.setup_x;
    cursor_.y = r.setup_y;
    cursor_.line_start_x = r.line_start_x;
    if (r.wrote_cursor) {
        cursor_.x = r.cursor_x;
        cursor_.y = r.cursor_y;
    }
    color_id_ = static_cast<std::int16_t>(r.color_id);
    host_.set_hi_res(r.hi_res);
    if (r.timer_written) {
        host_.set_message_timer(r.timer_value);
    }
    if (r.audio_load_block_written) {
        host_.set_audio_load_block(static_cast<std::uint8_t>(r.audio_load_block));
    }
    return r.code;
}

void GameText::reset_message_layout() {
    line_count_ = 1;
    box_position_ = 1;
    cursor_.line_start_x = static_cast<std::int16_t>(style_.message_line_start_x);
    color_id_ = static_cast<std::int16_t>(style_.message_color);
    host_.set_hi_res(false);
}

void GameText::set_debug_position(std::int32_t x_in, std::int32_t y_in) {
    // The original took 16-bit coordinates.
    const std::int16_t x = static_cast<std::int16_t>(x_in);
    const std::int16_t y = static_cast<std::int16_t>(y_in);
    if (x != kKeep) {
        debug_line_start_x_ = static_cast<std::int16_t>(x - kScreenOriginX);
        debug_x_ = static_cast<std::int16_t>(x - kScreenOriginX);
    }
    if (y != kKeep) {
        debug_y_ = static_cast<std::int16_t>(y - kScreenOriginY);
    }
}

void GameText::draw_debug(const char* text) {
    host_.begin_run(false);
    DebugSink sink(host_, kDefaultTextLayer);
    const DebugDrawResult r = sh::text::draw_debug(sink, DebugFontSpec{}, text, debug_x_, debug_y_, debug_line_start_x_);
    debug_x_ = static_cast<std::int16_t>(r.x);
    debug_y_ = static_cast<std::int16_t>(r.y);

    // One page for the whole string: the debug font's 4-bit atlas, page column 4 of row 1.
    d2::DrawMode mode;
    mode.page = d2::TexturePage::from_word(0x14);
    mode.dither = true;
    host_.submit(kDefaultTextLayer, mode, TextStream::Run);
    host_.end_run(false);
}

const char* GameText::debug_number(std::int32_t min_width, std::int32_t value) {
    const NumberText n = format_decimal_padded(value, min_width, '\v');
    std::memcpy(number_, n.bytes, static_cast<std::size_t>(n.count));
    number_[n.count] = 0;
    return number_;
}

}  // namespace sh::text
