// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's cell-font text: one engine object owns what the strings drawer
// remembers between calls (the cursor, colour, layer, clip window, scale, the map message's line widths and box
// position, the debug font's cursor) and draws through the layout modules into the 2D draw list. Every string the
// game draws goes through game_text(); the running game supplies the rest (font, characters, flags) through
// GameTextHost.
#pragma once

#include "engine/text/message_layout.h"
#include "engine/text/text_style.h"
#include "engine/text/text_types.h"
#include "render/draw2d/draw2d.h"

#include <cstdint>

namespace sh::text {

// Coordinates the callers pass are 320x240 screen pixels; the cursor keeps them relative to the screen's centre
// (x - 160, y - 112), 16-bit like the original's position vector.
constexpr std::int32_t kScreenOriginX = 160;
constexpr std::int32_t kScreenOriginY = 112;
// Passed for a coordinate, keeps the cursor's value.
constexpr std::int32_t kKeep = -1;

// The 2D layer strings go to unless a caller picks another (6 = not darkened by the screen fade).
constexpr std::int32_t kDefaultTextLayer = 6;

// Where the next string starts and where a newline returns to (the line start is 32-bit in the original and
// survives the 16-bit cursor's wrap).
struct TextCursor {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int32_t line_start_x = 0;
};

// Which run of the frame's 2D packet area a command takes (see GameTextHost::submit). A string's glyphs go to its
// own run, captured at the start of the draw and merged back at the end; a hi-res glyph goes straight to the
// shared area, because the original wrote those through the global cursor.
enum class TextStream : std::uint8_t { Run, Shared };

// What the drawer needs from the running game.
class GameTextHost {
public:
    virtual ~GameTextHost() = default;

    // Font and characters as they are this frame (the game font, i18n/game_font.h).
    virtual FontAtlas font() = 0;
    virtual CharSource& chars() = 0;
    // The menu string to draw for `text` (a translation, or `text` itself).
    virtual const char* menu_text(const char* text) = 0;

    // The game's hi-res glyph flag (the map messages' ~H switches it on).
    virtual bool hi_res() = 0;
    virtual void set_hi_res(bool on) = 0;

    // Map messages of the loaded map and the state the markup reads or writes.
    virtual const char* map_message(std::int32_t index) = 0;
    virtual std::int32_t message_line_cap() = 0;
    virtual std::int32_t message_v_shift() = 0;
    // The player's message text size in percent (100 = as authored).
    virtual float text_size_percent() = 0;
    virtual bool cutscene_active() = 0;
    // The language in force is written right to left (message lines align right).
    virtual bool right_to_left() { return false; }
    virtual bool message_timer_running() = 0;
    virtual void set_message_timer(std::int32_t value) = 0;
    virtual void set_audio_load_block(std::int32_t block) = 0;
    // A message draw makes the strings layer unusable for the world-geometry exemption.
    virtual void release_text_layer_exemption() = 0;

    // The 2D draw list. begin_run captures the run's packet cursor from the shared one unless hi-res is on;
    // end_run gives it back unless hi-res is on at the end. `reserve` takes a command's packet room without
    // drawing it (the debug font keeps a slot per byte).
    virtual void begin_run(bool hi_res) = 0;
    virtual void submit(std::int32_t layer, const draw2d::Command& command, TextStream stream) = 0;
    virtual void reserve(const draw2d::Command& command, TextStream stream) = 0;
    virtual void end_run(bool hi_res) = 0;
};

// One glyph record as draw2d commands, with the packing arithmetic of the original packets: texel coordinates and
// palette or page are summed into one word before splitting, so a u past 255 carries into v and a v past 255
// into the palette. A sprite becomes a Sprite and its DrawMode; a quad one textured Polygon.
struct GlyphCommands {
    std::uint8_t count = 0;
    draw2d::Command command[2];
};
GlyphCommands glyph_commands(const GlyphDraw& glyph);

class GameText {
public:
    explicit GameText(GameTextHost& host);

    // --- Strings ---
    // Position of the next string in screen pixels (kKeep keeps a coordinate); also resets the layer.
    void set_position(std::int32_t x, std::int32_t y);
    void set_color(std::int32_t color_id) { color_id_ = static_cast<std::int16_t>(color_id); }
    void set_layer(std::int32_t layer) { layer_ = layer; }
    void reset_layer() { layer_ = kDefaultTextLayer; }
    // Glyphs outside screen x [x0, x1) are cut, until cleared with x1 <= x0.
    void set_clip_x(std::int32_t x0, std::int32_t x1);
    // Q12 size of the following strings, about each string's start.
    void set_scale(std::int32_t scale) { scale_ = scale; }
    std::int32_t scale() const { return scale_; }
    // Draws up to `length` glyphs; true when it stopped at the string's end.
    bool draw(const char* text, std::int32_t length);
    // Draws a decimal number.
    void draw_int(std::int32_t value);
    // The colour table and the message box placement (gamedata/menu/text_style.json); all zero until set.
    void set_style(const TextStyle& style);
    // The colour id a new map message starts in.
    std::int32_t message_color() const { return style_.message_color; }
    // The light grey of the colour table (PAL dims it).
    void set_light_grey(std::uint8_t r, std::uint8_t g, std::uint8_t b);

    TextCursor& cursor() { return cursor_; }
    std::int32_t color() const { return color_id_; }

    // --- Map messages ---
    // Measures the lines of map message `index` and parses its box markup.
    void measure_message(std::int32_t index);
    // Draws the current page of `message`; returns the markup's result code.
    std::int32_t draw_message(const char* message, std::int32_t length);
    // Box placement, line start and colour a new message starts from.
    void reset_message_layout();
    void reset_pages() { layout_.reset_pages(); }
    bool page_pending() const { return layout_.page_pending(); }
    void advance_page() { layout_.page_advance(); }

    // --- Debug font (8x8) ---
    void set_debug_position(std::int32_t x, std::int32_t y);
    void draw_debug(const char* text);
    // `value` in decimal, left-padded with the debug font's blank to `min_width`; valid until the next call.
    const char* debug_number(std::int32_t min_width, std::int32_t value);

private:
    ColorTable colors() const;
    MessageSpec message_spec();

    GameTextHost& host_;

    TextCursor cursor_;
    std::int16_t color_id_ = 7;
    std::int32_t layer_ = kDefaultTextLayer;
    std::int32_t clip_x0_ = 0;
    std::int32_t clip_x1_ = 0;
    std::int32_t scale_ = kUnityScale;
    std::uint32_t rgb_[kTextColorCount] = {};
    TextStyle style_{};

    // The map message box: its position id, the measured widths of its lines and how many there are.
    std::int32_t box_position_ = 1;
    std::int32_t line_widths_[12] = {};
    std::int32_t line_count_ = 0;
    MessageLayout layout_;

    std::int16_t debug_x_ = 0;
    std::int16_t debug_y_ = 0;
    std::int16_t debug_line_start_x_ = 0;
    char number_[24] = {};
};

// The game's one text drawer.
GameText& game_text();

}  // namespace sh::text
