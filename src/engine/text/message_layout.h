// SPDX-License-Identifier: GPL-3.0-or-later
// Map message layout: the per-line width measure, the ~X<d> markup parser and the
// paged draw. The renderer keeps the page-continuation state (where the current page
// of a message starts and what to resume with); everything the game owns (widths,
// timers, colors, the box position) is passed in and reported back.

#pragma once

#include "engine/text/text_types.h"

namespace sh::text {

// Vertical placement of line 0 for each message-box position, as a function of the
// rendered line count n. The formulas are data because they are the game's.
struct MessageBoxAnchors {
    enum class Formula : std::uint8_t {
        Fixed,         // y = y_base
        RisePerLine,   // y = y_base - (n - 1) * cell_height (bottom-anchored boxes)
        Grid8,         // y = (grid_lines - n) * 8 + y_base (a literal half-line grid)
        Keep           // position ids beyond the table leave y unchanged
    };
    Formula formula[5] = {};
    std::int32_t y_base[5] = {};
    std::int32_t grid_lines = 9;  // Grid8: authored line count, NOT the runtime cap
};

// Everything the markup needs beyond the font. The fit limits of the size setup and
// the result-code values are part of the game's interface, so they come in as data.
struct MessageSpec {
    std::int32_t line_cap = 9;              // rendered lines per page
    std::int32_t display_all_length = 400;  // the "show everything" budget
    std::int32_t end_code = -1;             // ~E
    std::int32_t page_code = 1;             // page over, more follows / bare NUL end
    std::int32_t align_center_code = 99;    // ~M
    std::int32_t set_by_t_code = 88;        // ~T
    std::int32_t tab_x = -120;              // the inset ~T starts lines at
    std::int32_t fit_half_width = 152;      // widest half line that must stay on screen
    std::int32_t fit_room_top = 116;        // vertical room the block is fitted into
    std::int32_t fit_inset_base = 120;      // inset lines extend from -base to the width
    std::int32_t text_size_scale = kUnityScale;  // (text size % * 4096 / 100), from the game
    std::int32_t msg_v_shift = 0;           // lift of the bottom boxes (gameplay only)
    bool cutscene_active = false;           // cutscenes skip the lift
    // A right-to-left language: the lines of the block end at its right edge (inset lines
    // at the mirrored inset), and a line being rolled out appears from its right end. The
    // text itself comes in visual order.
    bool right_to_left = false;
    MessageBoxAnchors anchors;
};

struct MessageMeasureQuery {
    const char* msg = nullptr;
    std::int32_t position_idx = 1;   // current box position (~L writes it)
    bool hi_res = false;             // ~H latches it on
    std::int32_t widths[12] = {};    // live width slots; only [0, line_cap) are cleared
};

struct MessageMeasureResult {
    std::int32_t widths[12] = {};
    std::int32_t line_count = 1;
    std::int32_t position_idx = 1;
    std::int32_t audio_load_block = 0;
    bool hi_res = false;
};

struct MessageDrawQuery {
    const char* msg = nullptr;
    std::int32_t length = 0;          // rollout budget (display-all re-arms it)
    std::int32_t color_id = 7;
    bool hi_res = false;
    bool timer_running = false;       // a ~J timer is already counting: skip the value
    std::int32_t position_idx = 1;
    std::int32_t fallback_y = 0;      // g_StringPosition.vy, for unknown position ids
    std::int32_t widths[12] = {};
    std::int32_t line_count = 1;
};

struct MessageDrawResult {
    std::int32_t code = 0;            // the markup result (end/page/select/align codes)
    std::int16_t setup_x = 0;         // g_StringPosition after the box placement
    std::int16_t setup_y = 0;
    std::int32_t line_start_x = 0;    // g_StringPositionX1 after the parse
    bool wrote_cursor = false;        // only the non-early-return path moves the cursor
    std::int16_t cursor_x = 0;
    std::int16_t cursor_y = 0;
    std::int32_t color_id = 7;
    bool hi_res = false;              // state at exit (a ~H mid-page switches it on)
    bool timer_written = false;       // a ~J value was parsed
    std::int32_t timer_value = 0;
    bool audio_load_block_written = false;
    std::int32_t audio_load_block = 0;
};

class MessageLayout {
public:
    // Measures the page currently on screen (the page state of this renderer decides
    // where that page starts) and parses the box-level markup (~N, ~L, ~J, ~H).
    MessageMeasureResult measure(CharSource& chars, const FontAtlas& font,
                                 const MessageSpec& spec, const MessageMeasureQuery& query) const;

    // Draws the current page. The page-continuation state lives here: a different
    // message resets it, a budget-limited or ~P-bounded page leaves a continuation.
    MessageDrawResult draw(GlyphSink& sink, CharSource& chars, const FontAtlas& font,
                           const ColorTable& colors, const MessageSpec& spec,
                           const MessageDrawQuery& query);

    void reset_pages();
    bool page_pending() const { return page_next_ != 0; }
    void page_advance();

private:
    std::int32_t page_start(const char* msg) const;
    std::int32_t break_page(const char* base, const char* at, std::int32_t color_id,
                            std::int32_t align, std::int32_t page_code);

    const char* page_msg_ = nullptr;
    std::int32_t page_off_ = 0;
    std::int32_t page_next_ = 0;
    std::int32_t page_color_id_ = 0;
    std::int32_t page_align_ = 0;
};

}  // namespace sh::text
