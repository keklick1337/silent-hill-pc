// The text layout engine (src/engine/text) against its specification: every expected
// value below is worked out by hand from the original integer pipeline — the Q.12
// scaling shifts, the 16-bit cursor truncation, the markup parse and the box anchor
// formulas. The tests never link the game layer.

#include "engine/text/debug_font.h"
#include "engine/text/message_layout.h"
#include "engine/text/number_format.h"
#include "engine/text/string_layout.h"
#include "engine/text/text_types.h"

#include "framework/sh_test.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

namespace t = sh::text;

// One recorded glyph, flattened for exact comparisons.
struct Rec {
    bool sprite = false;
    bool hi_res = false;
    std::int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0;
    std::int32_t u = 0, v = 0, page = 0, clut = 0;
    std::int32_t cut_l = 0, cut_r = 0;
    std::uint32_t color = 0;
};

class RecordSink final : public t::GlyphSink {
public:
    std::vector<Rec> recs;

    void glyph(const t::GlyphDraw& g) override {
        Rec r;
        r.sprite = g.sprite;
        r.hi_res = g.hi_res;
        r.x0 = g.x[0];
        r.y0 = g.y[0];
        r.x1 = g.x[1];
        r.y1 = g.y[1];
        r.x2 = g.x[2];
        r.y2 = g.y[2];
        r.x3 = g.x[3];
        r.y3 = g.y[3];
        r.u = g.u;
        r.v = g.v;
        r.page = g.page;
        r.clut = g.clut;
        r.cut_l = g.cut_left;
        r.cut_r = g.cut_right;
        r.color = g.color;
        recs.push_back(r);
    }
};

// Test character source: cell = code - 39 ('\''), one emission, configurable advance
// and an optional second emission (the combining-mark shape).
class TestChars final : public t::CharSource {
public:
    std::int32_t advance = 5;
    bool dual = false;
    std::int32_t dual_dy = -3;
    std::int32_t dual_advance = 8;

    int map_char(std::uint32_t code, t::GlyphEmit out[2]) override {
        out[0].cell = static_cast<std::int32_t>(code) - 39;
        out[0].dy = 0;
        out[0].advance = advance;
        if (dual) {
            out[1].cell = out[0].cell + 1;
            out[1].dy = dual_dy;
            out[1].advance = dual_advance;
            return 2;
        }
        return 1;
    }
};

// The USA font layout: 21-column strip rows, one row per page, v base 240.
t::FontAtlas usa_font() {
    t::FontAtlas f;
    f.v_base = 240;
    f.rows_per_page = 1;
    f.tpage_base = 0x10;
    f.packed_clut = 0x7FD3;
    f.hi_res_bottom = 30;
    return f;
}

// id -> (id, id, id), so a record's color names its color id.
t::ColorTable id_colors() {
    t::ColorTable c;
    for (int i = 0; i < 8; ++i) {
        c.rgb[i] = static_cast<std::uint32_t>(i) * 0x010101u;
    }
    return c;
}

t::MessageSpec usa_spec() {
    t::MessageSpec s;
    s.anchors.formula[0] = t::MessageBoxAnchors::Formula::Fixed;
    s.anchors.formula[1] = t::MessageBoxAnchors::Formula::RisePerLine;
    s.anchors.formula[2] = t::MessageBoxAnchors::Formula::Fixed;
    s.anchors.formula[3] = t::MessageBoxAnchors::Formula::RisePerLine;
    s.anchors.formula[4] = t::MessageBoxAnchors::Formula::Grid8;
    s.anchors.y_base[0] = -92;
    s.anchors.y_base[1] = 76;
    s.anchors.y_base[2] = -60;
    s.anchors.y_base[3] = 44;
    s.anchors.y_base[4] = -76;
    s.anchors.grid_lines = 9;
    return s;
}

std::string bytes_of(const t::NumberText& n) { return std::string(n.bytes, n.bytes + n.count); }

}  // namespace

// ---------- string layout ----------

SH_TEST(text_string_draws_mapped_glyphs_and_cursor) {
    RecordSink sink;
    TestChars chars;
    t::StringDrawQuery q;
    q.text = "AB";
    q.length = 2;
    q.cursor_x = 100;
    q.cursor_y = 50;
    const t::StringDrawResult r = t::draw_string(sink, chars, usa_font(), id_colors(), q);

    SH_CHECK(sink.recs.size() == 2);
    // 'A' = cell 26: row 1, column 5; 'B' = cell 27: row 1, column 6.
    SH_CHECK(sink.recs[0].sprite && !sink.recs[0].hi_res);
    SH_CHECK(sink.recs[0].x0 == 100 && sink.recs[0].y0 == 50);
    SH_CHECK(sink.recs[0].u == 5 * 12 && sink.recs[0].v == 240);
    SH_CHECK(sink.recs[0].page == 0x11 && sink.recs[0].clut == 0x7FD3);
    SH_CHECK(sink.recs[1].x0 == 105 && sink.recs[1].u == 6 * 12 && sink.recs[1].page == 0x11);
    SH_CHECK(!r.terminated);
    SH_CHECK(r.cursor_x == 110 && r.cursor_y == 50);
}

SH_TEST(text_string_remaps_and_control_bytes) {
    RecordSink sink;
    TestChars chars;
    t::StringDrawQuery q;
    q.text = "!&";  // drawn from the cells of '\\' (53) and '^' (55)
    q.length = 2;
    q.cursor_x = 100;
    const t::StringDrawResult r = t::draw_string(sink, chars, usa_font(), id_colors(), q);

    SH_CHECK(sink.recs.size() == 2);
    SH_CHECK(sink.recs[0].u == 11 * 12 && sink.recs[0].page == 0x12);  // row 2
    SH_CHECK(sink.recs[1].u == 13 * 12 && sink.recs[1].page == 0x12);
    SH_CHECK(r.cursor_x == 110);
}

SH_TEST(text_string_space_wide_space_step_and_color_bytes) {
    RecordSink sink;
    TestChars chars;
    t::StringDrawQuery q;
    q.text = "_\v\x01\x02""A";  // +6, +10, -1, color 2 (no advance), glyph
    q.length = 5;
    q.cursor_x = 100;
    q.cursor_y = 50;
    const t::StringDrawResult r = t::draw_string(sink, chars, usa_font(), id_colors(), q);

    SH_CHECK(sink.recs.size() == 1);
    SH_CHECK(sink.recs[0].x0 == 115);  // 100 + 6 + 10 - 1
    SH_CHECK(sink.recs[0].color == 0x020202u);
    SH_CHECK(r.color_id == 2);
    SH_CHECK(r.cursor_x == 120);
}

SH_TEST(text_string_nul_terminates_and_keeps_cursor) {
    RecordSink sink;
    TestChars chars;
    t::StringDrawQuery q;
    q.text = "A\0B";
    q.length = 5;
    q.cursor_x = 100;
    const t::StringDrawResult r = t::draw_string(sink, chars, usa_font(), id_colors(), q);

    SH_CHECK(sink.recs.size() == 1);
    SH_CHECK(r.terminated);
    SH_CHECK(r.cursor_x == 105);  // the NUL neither draws nor advances
}

SH_TEST(text_string_newline_returns_to_line_start) {
    RecordSink sink;
    TestChars chars;
    t::StringDrawQuery q;
    q.text = "\nA";
    q.length = 2;
    q.cursor_x = 100;
    q.cursor_y = 50;
    q.line_start_x = -40;
    t::draw_string(sink, chars, usa_font(), id_colors(), q);

    SH_CHECK(sink.recs.size() == 1);
    SH_CHECK(sink.recs[0].x0 == -40 && sink.recs[0].y0 == 66);
}

SH_TEST(text_string_ignored_bytes_do_not_consume_budget) {
    RecordSink sink;
    TestChars chars;
    t::StringDrawQuery q;
    q.text = "A B";  // ' ' is below the glyph floor: ignored, budget kept
    q.length = 1;
    q.cursor_x = 100;
    const t::StringDrawResult r = t::draw_string(sink, chars, usa_font(), id_colors(), q);

    SH_CHECK(sink.recs.size() == 1);
    SH_CHECK(!r.terminated);
    SH_CHECK(r.cursor_x == 105);
}

SH_TEST(text_string_clip_cuts_edges) {
    RecordSink sink;
    TestChars chars;
    t::StringDrawQuery q;
    q.text = "A";
    q.length = 1;
    q.cursor_x = 100;
    q.clip_x0 = 103;  // cell [100, 112) in [103, 111): 3 off the left, 1 off the right
    q.clip_x1 = 111;
    t::draw_string(sink, chars, usa_font(), id_colors(), q);

    SH_CHECK(sink.recs.size() == 1);
    SH_CHECK(sink.recs[0].cut_l == 3 && sink.recs[0].cut_r == 1);
}

SH_TEST(text_string_clip_window_inactive_when_empty) {
    RecordSink sink;
    TestChars chars;
    t::StringDrawQuery q;
    q.text = "A";
    q.length = 1;
    q.cursor_x = 100;
    q.clip_x0 = 300;
    q.clip_x1 = 200;  // x1 <= x0: clip nothing
    t::draw_string(sink, chars, usa_font(), id_colors(), q);

    SH_CHECK(sink.recs.size() == 1);
    SH_CHECK(sink.recs[0].cut_l == 0 && sink.recs[0].cut_r == 0);
}

SH_TEST(text_string_scaled_quad_about_start) {
    RecordSink sink;
    TestChars chars;
    t::StringDrawQuery q;
    q.text = "A";
    q.length = 1;
    q.cursor_x = 100;
    q.cursor_y = 50;
    q.scale = 2048;  // 0.5
    const t::StringDrawResult r = t::draw_string(sink, chars, usa_font(), id_colors(), q);

    SH_CHECK(sink.recs.size() == 1);
    SH_CHECK(!sink.recs[0].sprite && !sink.recs[0].hi_res);
    SH_CHECK(sink.recs[0].x0 == 100 && sink.recs[0].y0 == 50);
    SH_CHECK(sink.recs[0].x1 == 100 && sink.recs[0].y1 == 57);   // + h (15 * 0.5)
    SH_CHECK(sink.recs[0].x2 == 106 && sink.recs[0].y2 == 50);   // + w (12 * 0.5)
    SH_CHECK(sink.recs[0].x3 == 106 && sink.recs[0].y3 == 57);
    SH_CHECK(sink.recs[0].cut_l == 0 && sink.recs[0].cut_r == 0);  // scaled: never cut
    SH_CHECK(r.cursor_x == 105);  // the advance stays unscaled
}

SH_TEST(text_string_hires_quad_doubles_y) {
    RecordSink sink;
    TestChars chars;
    t::StringDrawQuery q;
    q.text = "A";
    q.length = 1;
    q.cursor_x = 100;
    q.cursor_y = 50;
    q.hi_res = true;
    t::draw_string(sink, chars, usa_font(), id_colors(), q);

    SH_CHECK(sink.recs.size() == 1);
    SH_CHECK(!sink.recs[0].sprite && sink.recs[0].hi_res);
    SH_CHECK(sink.recs[0].x0 == 100 && sink.recs[0].y0 == 100);
    SH_CHECK(sink.recs[0].x1 == 100 && sink.recs[0].y1 == 130);  // + hi_res_bottom
    SH_CHECK(sink.recs[0].x2 == 112 && sink.recs[0].y2 == 100);
    SH_CHECK(sink.recs[0].x3 == 112 && sink.recs[0].y3 == 130);
}

SH_TEST(text_string_dual_emission_advances_per_emission) {
    RecordSink sink;
    TestChars chars;
    chars.dual = true;
    chars.advance = 0;  // mark keeps the x, base letter moves on
    t::StringDrawQuery q;
    q.text = "\xC0";  // any mapped byte
    q.length = 1;
    q.cursor_x = 100;
    q.cursor_y = 50;
    t::draw_string(sink, chars, usa_font(), id_colors(), q);

    SH_CHECK(sink.recs.size() == 2);
    SH_CHECK(sink.recs[0].x0 == 100 && sink.recs[0].y0 == 50);
    SH_CHECK(sink.recs[0].u == (153 % 21) * 12);  // cell 153 (code 0xC0)
    SH_CHECK(sink.recs[1].x0 == 100 && sink.recs[1].y0 == 47);  // dy -3, advance 0 in between
    SH_CHECK(sink.recs[1].u == (154 % 21) * 12);                 // cell 154
}

SH_TEST(text_string_dual_emission_clip_skip_still_advances) {
    RecordSink sink;
    TestChars chars;
    chars.dual = true;
    chars.advance = 0;
    t::StringDrawQuery q;
    q.text = "\xC0";
    q.length = 1;
    q.cursor_x = 100;
    q.clip_x0 = 500;  // everything clipped away
    q.clip_x1 = 600;
    const t::StringDrawResult r = t::draw_string(sink, chars, usa_font(), id_colors(), q);

    SH_CHECK(sink.recs.empty());
    SH_CHECK(r.cursor_x == 108);  // 0 (mark) + 8 (base)
}

SH_TEST(text_string_cursor_truncates_to_16_bits) {
    RecordSink sink;
    TestChars chars;
    t::StringDrawQuery q;
    q.text = "AAA";
    q.length = 3;
    q.cursor_x = 32760;
    const t::StringDrawResult r = t::draw_string(sink, chars, usa_font(), id_colors(), q);

    SH_CHECK(r.cursor_x == -32761);  // 32775 wraps as the 16-bit store does
}

// ---------- number formatting ----------

SH_TEST(text_number_format_decimal) {
    SH_CHECK(bytes_of(t::format_decimal(0)) == "0");
    SH_CHECK(bytes_of(t::format_decimal(-25)) == "-25");
    SH_CHECK(bytes_of(t::format_decimal(12345)) == "12345");
    SH_CHECK(bytes_of(t::format_decimal(-2147483647)) == "-2147483647");
}

SH_TEST(text_number_format_padded) {
    SH_CHECK(bytes_of(t::format_decimal_padded(7, 3, '\v')) == "\v\v7");
    SH_CHECK(bytes_of(t::format_decimal_padded(-42, 5, '\v')) == "\v\v-42");
    SH_CHECK(bytes_of(t::format_decimal_padded(123, 2, '\v')) == "123");
    SH_CHECK(bytes_of(t::format_decimal_padded(9, 0, '\v')) == "9");
}

// ---------- message measure ----------

SH_TEST(text_message_measure_widths_markup_and_jump_scan) {
    TestChars chars;
    t::MessageLayout layout;
    t::MessageMeasureQuery q;
    q.msg = "~L1_AB~N ~J2(1.5) C~E ";
    q.widths[10] = 555;  // above the cap: kept (the original clears only [0, cap))
    const t::MessageMeasureResult r = layout.measure(chars, usa_font(), usa_spec(), q);

    SH_CHECK(r.widths[0] == 6 + 5 + 5);  // '_' + A + B
    SH_CHECK(r.widths[1] == 5);          // C
    SH_CHECK(r.line_count == 2);
    SH_CHECK(r.position_idx == 1);   // ~L1
    SH_CHECK(r.audio_load_block == 3);  // ~J2
    SH_CHECK(r.widths[10] == 555);
}

SH_TEST(text_message_measure_line_count_clamped_to_cap) {
    TestChars chars;
    t::MessageLayout layout;
    t::MessageSpec spec = usa_spec();
    spec.line_cap = 2;
    t::MessageMeasureQuery q;
    q.msg = "A~N B~N C~N D";
    const t::MessageMeasureResult r = layout.measure(chars, usa_font(), spec, q);

    SH_CHECK(r.line_count == 2);  // 3 counted, clamped
    SH_CHECK(r.widths[0] == 5);   // A
    SH_CHECK(r.widths[1] == 5);   // B
}

SH_TEST(text_message_measure_high_res_latch) {
    TestChars chars;
    t::MessageLayout layout;
    t::MessageMeasureQuery q;
    q.msg = "~HA";
    const t::MessageMeasureResult r = layout.measure(chars, usa_font(), usa_spec(), q);
    SH_CHECK(r.hi_res);
}

// ---------- message draw ----------

SH_TEST(text_message_draw_top_box_and_end_code) {
    RecordSink sink;
    TestChars chars;
    t::MessageLayout layout;
    t::MessageDrawQuery q;
    q.msg = "AB~E ";
    q.length = 400;
    q.widths[0] = 16;
    q.line_count = 1;
    q.position_idx = 1;  // bottom-rise box, one line: y = 76
    const t::MessageDrawResult r = layout.draw(sink, chars, usa_font(), id_colors(), usa_spec(), q);

    SH_CHECK(r.code == -1);  // ~E
    SH_CHECK(r.setup_x == -8 && r.setup_y == 76);  // -(16 >> 1)
    SH_CHECK(r.line_start_x == -8);
    SH_CHECK(sink.recs.size() == 2);
    SH_CHECK(sink.recs[0].x0 == -8 && sink.recs[0].y0 == 76 && sink.recs[0].u == 60);
    SH_CHECK(sink.recs[1].x0 == -3 && sink.recs[1].u == 72);
    SH_CHECK(r.wrote_cursor);
    SH_CHECK(r.cursor_x == 2 && r.cursor_y == 76);
}

SH_TEST(text_message_draw_bare_nul_reports_page_over) {
    RecordSink sink;
    TestChars chars;
    t::MessageLayout layout;
    t::MessageDrawQuery q;
    q.msg = "AB";
    q.length = 400;
    q.widths[0] = 16;
    q.line_count = 1;
    q.position_idx = 0;
    const t::MessageDrawResult r = layout.draw(sink, chars, usa_font(), id_colors(), usa_spec(), q);
    SH_CHECK(r.code == 1);
    SH_CHECK(!layout.page_pending());
}

SH_TEST(text_message_draw_center_alignment_per_line) {
    RecordSink sink;
    TestChars chars;
    t::MessageLayout layout;
    t::MessageDrawQuery q;
    q.msg = "~M0A~N0B~E ";
    q.length = 400;
    q.widths[0] = 10;
    q.widths[1] = 5;
    q.line_count = 2;
    q.position_idx = 0;
    const t::MessageDrawResult r = layout.draw(sink, chars, usa_font(), id_colors(), usa_spec(), q);

    SH_CHECK(r.code == -1);
    SH_CHECK(sink.recs.size() == 2);
    SH_CHECK(sink.recs[0].x0 == -5 && sink.recs[0].y0 == -92);   // line 0: -(10 >> 1)
    SH_CHECK(sink.recs[1].x0 == -2 && sink.recs[1].y0 == -76);   // line 1: -(5 >> 1)
}

SH_TEST(text_message_draw_default_alignment_uses_longest_line) {
    RecordSink sink;
    TestChars chars;
    t::MessageLayout layout;
    t::MessageDrawQuery q;
    q.msg = "A~N B~E ";
    q.length = 400;
    q.widths[0] = 10;
    q.widths[1] = 5;
    q.line_count = 2;
    q.position_idx = 0;
    layout.draw(sink, chars, usa_font(), id_colors(), usa_spec(), q);

    SH_CHECK(sink.recs[1].x0 == -5);  // ~N default: -(longest >> 1), not the line's own width
}

SH_TEST(text_message_draw_tab_insets_line_start) {
    RecordSink sink;
    TestChars chars;
    t::MessageLayout layout;
    t::MessageDrawQuery q;
    q.msg = "~T AB~E ";
    q.length = 400;
    q.widths[0] = 10;
    q.line_count = 1;
    q.position_idx = 0;
    const t::MessageDrawResult r = layout.draw(sink, chars, usa_font(), id_colors(), usa_spec(), q);

    SH_CHECK(sink.recs.size() == 2);
    SH_CHECK(sink.recs[0].x0 == -120 && sink.recs[1].x0 == -115);
    SH_CHECK(r.line_start_x == -120);
}

SH_TEST(text_message_draw_select_returns_digit) {
    RecordSink sink;
    TestChars chars;
    t::MessageLayout layout;
    t::MessageDrawQuery q;
    q.msg = "~S3";
    q.length = 400;
    q.line_count = 1;
    q.position_idx = 0;
    const t::MessageDrawResult r = layout.draw(sink, chars, usa_font(), id_colors(), usa_spec(), q);
    SH_CHECK(r.code == 3);
}

SH_TEST(text_message_draw_unknown_line_position_keeps_y) {
    RecordSink sink;
    TestChars chars;
    t::MessageLayout layout;
    t::MessageDrawQuery q;
    q.msg = "A~E ";
    q.length = 400;
    q.widths[0] = 10;
    q.line_count = 1;
    q.position_idx = 7;   // beyond the anchor table
    q.fallback_y = -5;    // the live cursor y
    const t::MessageDrawResult r = layout.draw(sink, chars, usa_font(), id_colors(), usa_spec(), q);
    SH_CHECK(r.setup_y == -5);
}

SH_TEST(text_message_draw_color_code_and_out_of_range_keeps_color) {
    RecordSink sink;
    TestChars chars;
    t::MessageLayout layout;
    t::MessageDrawQuery q;
    q.msg = "~C3A~C9B~E ";
    q.length = 400;
    q.widths[0] = 10;
    q.line_count = 1;
    q.position_idx = 0;
    const t::MessageDrawResult r = layout.draw(sink, chars, usa_font(), id_colors(), usa_spec(), q);

    SH_CHECK(sink.recs.size() == 2);
    SH_CHECK(sink.recs[0].color == 0x030303u);  // ~C3
    SH_CHECK(sink.recs[1].color == 0x030303u);  // ~C9 is out of the table: kept
    SH_CHECK(r.color_id == 3);
}

SH_TEST(text_message_draw_jump_timer_parse) {
    RecordSink sink;
    TestChars chars;
    t::MessageLayout layout;
    t::MessageDrawQuery q;
    q.msg = "~J0(1.5)A~E ";
    q.length = 400;
    q.widths[0] = 5;
    q.line_count = 1;
    q.position_idx = 0;
    const t::MessageDrawResult r = layout.draw(sink, chars, usa_font(), id_colors(), usa_spec(), q);

    SH_CHECK(r.timer_written && r.timer_value == 6144);  // 1.5 s in Q.12
    SH_CHECK(r.audio_load_block_written && r.audio_load_block == 1);
    SH_CHECK(sink.recs.empty());  // the walk past ')' and the shared step eat the next byte
    SH_CHECK(r.code == -1);
}

SH_TEST(text_message_draw_jump_timer_fraction_digits) {
    RecordSink sink;
    TestChars chars;
    t::MessageLayout layout;
    t::MessageDrawQuery q;
    q.msg = "~J1(2.25)A~E ";
    q.length = 400;
    q.widths[0] = 5;
    q.line_count = 1;
    q.position_idx = 0;
    const t::MessageDrawResult r = layout.draw(sink, chars, usa_font(), id_colors(), usa_spec(), q);

    SH_CHECK(r.timer_written && r.timer_value == 9216);  // 2.25 s
    SH_CHECK(r.audio_load_block == 2);
}

SH_TEST(text_message_draw_running_timer_scans_to_space) {
    RecordSink sink;
    TestChars chars;
    t::MessageLayout layout;
    t::MessageDrawQuery q;
    q.msg = "~J1(2.25) A~E ";
    q.length = 400;
    q.widths[0] = 5;
    q.line_count = 1;
    q.position_idx = 0;
    q.timer_running = true;
    const t::MessageDrawResult r = layout.draw(sink, chars, usa_font(), id_colors(), usa_spec(), q);

    SH_CHECK(!r.timer_written);
    SH_CHECK(sink.recs.size() == 1);  // A after the scan consumed the timer text + space
}

SH_TEST(text_message_draw_display_all_rearms_budget) {
    RecordSink sink;
    TestChars chars;
    t::MessageLayout layout;
    t::MessageDrawQuery q;
    q.msg = "~D AB~E ";
    q.length = 1;  // one glyph of rollout left
    q.widths[0] = 15;
    q.line_count = 1;
    q.position_idx = 0;
    const t::MessageDrawResult r = layout.draw(sink, chars, usa_font(), id_colors(), usa_spec(), q);

    SH_CHECK(sink.recs.size() == 2);  // both drawn: ~D refilled the budget
    SH_CHECK(r.code == -1);
    SH_CHECK(r.wrote_cursor);
}

SH_TEST(text_message_draw_high_res_switches_mid_page) {
    RecordSink sink;
    TestChars chars;
    t::MessageLayout layout;
    t::MessageDrawQuery q;
    q.msg = "A~H B~E ";
    q.length = 400;
    q.widths[0] = 10;
    q.line_count = 1;
    q.position_idx = 0;
    const t::MessageDrawResult r = layout.draw(sink, chars, usa_font(), id_colors(), usa_spec(), q);

    SH_CHECK(sink.recs.size() == 2);
    SH_CHECK(sink.recs[0].sprite && !sink.recs[0].hi_res);  // before ~H
    SH_CHECK(!sink.recs[1].sprite && sink.recs[1].hi_res);  // after
    SH_CHECK(r.hi_res);
}

// ---------- right-to-left messages ----------

SH_TEST(text_message_right_to_left_lines_end_at_the_right_edge) {
    RecordSink sink;
    TestChars chars;  // 5 px a glyph
    t::MessageLayout layout;
    t::MessageSpec spec = usa_spec();
    spec.right_to_left = true;
    t::MessageDrawQuery q;
    q.msg = "ABCD~N EF~E ";
    q.length = 400;
    q.widths[0] = 20;
    q.widths[1] = 10;
    q.line_count = 2;
    q.position_idx = 0;
    layout.draw(sink, chars, usa_font(), id_colors(), spec, q);

    // The block is centred on the screen (x -10..10); the short line ends where the long one does.
    SH_CHECK(sink.recs.size() == 6);
    SH_CHECK(sink.recs[0].x0 == -10 && sink.recs[3].x0 == 5);
    SH_CHECK(sink.recs[4].x0 == 0 && sink.recs[5].x0 == 5);
    SH_CHECK(sink.recs[4].y0 == sink.recs[0].y0 + 16);
}

SH_TEST(text_message_right_to_left_inset_is_mirrored) {
    RecordSink sink;
    TestChars chars;
    t::MessageLayout layout;
    t::MessageSpec spec = usa_spec();
    spec.right_to_left = true;
    t::MessageDrawQuery q;
    q.msg = "~T AB~N C~E ";
    q.length = 400;
    q.widths[0] = 10;
    q.widths[1] = 5;
    q.line_count = 2;
    q.position_idx = 0;
    layout.draw(sink, chars, usa_font(), id_colors(), spec, q);

    // Inset lines start at -120 left to right; right to left they end at +120.
    SH_CHECK(sink.recs.size() == 3);
    SH_CHECK(sink.recs[0].x0 == 110 && sink.recs[1].x0 == 115 && sink.recs[2].x0 == 115);
}

SH_TEST(text_message_right_to_left_rolls_out_from_the_right) {
    TestChars chars;
    t::MessageSpec spec = usa_spec();
    spec.right_to_left = true;
    t::MessageDrawQuery q;
    q.msg = "ABCD~N EF~E ";
    q.widths[0] = 20;
    q.widths[1] = 10;
    q.line_count = 2;
    q.position_idx = 0;

    // Two glyphs of the budget: the two at the right end of the first line, where the sentence begins.
    {
        RecordSink sink;
        t::MessageLayout layout;
        q.length = 2;
        layout.draw(sink, chars, usa_font(), id_colors(), spec, q);
        SH_CHECK(sink.recs.size() == 2);
        SH_CHECK(sink.recs[0].x0 == 0 && sink.recs[1].x0 == 5);
    }
    // Five: the whole first line and the right glyph of the second.
    {
        RecordSink sink;
        t::MessageLayout layout;
        q.length = 5;
        layout.draw(sink, chars, usa_font(), id_colors(), spec, q);
        SH_CHECK(sink.recs.size() == 5);
        SH_CHECK(sink.recs[0].x0 == -10 && sink.recs[4].x0 == 5 && sink.recs[4].y0 == sink.recs[0].y0 + 16);
    }
    // Left to right the same budget draws from the left end.
    {
        RecordSink sink;
        t::MessageLayout layout;
        spec.right_to_left = false;
        q.length = 2;
        layout.draw(sink, chars, usa_font(), id_colors(), spec, q);
        SH_CHECK(sink.recs.size() == 2 && sink.recs[0].x0 == -10 && sink.recs[1].x0 == -5);
    }
}

// ---------- message pages ----------

SH_TEST(text_message_page_break_and_continuation) {
    RecordSink sink;
    TestChars chars;
    t::MessageSpec spec = usa_spec();
    spec.line_cap = 2;
    spec.display_all_length = 4;
    t::MessageLayout layout;

    t::MessageDrawQuery q;
    q.msg = "ABCDE~E";
    q.length = 4;  // at the ceiling: a page break is left when the budget runs out
    q.widths[0] = 25;
    q.line_count = 1;
    q.position_idx = 0;
    const t::MessageDrawResult r1 = layout.draw(sink, chars, usa_font(), id_colors(), spec, q);

    SH_CHECK(r1.code == 1);
    SH_CHECK(!r1.wrote_cursor);  // the early path leaves the cursor alone
    SH_CHECK(sink.recs.size() == 4);  // the budget spends one per glyph: A..D
    SH_CHECK(layout.page_pending());

    layout.page_advance();
    q.length = 400;
    sink.recs.clear();
    const t::MessageDrawResult r2 = layout.draw(sink, chars, usa_font(), id_colors(), spec, q);

    SH_CHECK(sink.recs.size() == 1);  // the E left on the page, then its ~E terminator
    SH_CHECK(r2.code == -1);
    SH_CHECK(r2.wrote_cursor);
    SH_CHECK(!layout.page_pending());
}

SH_TEST(text_message_page_reset_on_different_message) {
    RecordSink sink;
    TestChars chars;
    t::MessageSpec spec = usa_spec();
    spec.line_cap = 2;
    spec.display_all_length = 4;
    t::MessageLayout layout;

    t::MessageDrawQuery q;
    q.msg = "ABCDE~E";
    q.length = 4;
    q.widths[0] = 25;
    q.line_count = 1;
    q.position_idx = 0;
    layout.draw(sink, chars, usa_font(), id_colors(), spec, q);
    SH_CHECK(layout.page_pending());

    layout.reset_pages();
    SH_CHECK(!layout.page_pending());

    sink.recs.clear();
    const t::MessageDrawResult r = layout.draw(sink, chars, usa_font(), id_colors(), spec, q);
    SH_CHECK(sink.recs.size() == 4);  // from the top again: A, B, C, then D cuts the page
    SH_CHECK(r.code == 1);
}

SH_TEST(text_message_explicit_page_code_breaks) {
    RecordSink sink;
    TestChars chars;
    t::MessageSpec spec = usa_spec();
    spec.line_cap = 2;
    t::MessageLayout layout;

    t::MessageDrawQuery q;
    q.msg = "AB~P CD~E ";
    q.length = 400;
    q.widths[0] = 10;
    q.line_count = 1;
    q.position_idx = 0;
    const t::MessageDrawResult r = layout.draw(sink, chars, usa_font(), id_colors(), spec, q);

    SH_CHECK(r.code == 1);  // ~P: page over
    SH_CHECK(sink.recs.size() == 2);
    SH_CHECK(layout.page_pending());

    layout.page_advance();
    sink.recs.clear();
    const t::MessageDrawResult r2 = layout.draw(sink, chars, usa_font(), id_colors(), spec, q);
    SH_CHECK(sink.recs.size() == 2);
    SH_CHECK(r2.code == -1);
}

// ---------- message size setup ----------

SH_TEST(text_message_scale_setup_enlarges_about_anchor) {
    RecordSink sink;
    TestChars chars;
    t::MessageSpec spec = usa_spec();
    spec.text_size_scale = 8192;  // 2.0
    t::MessageLayout layout;

    t::MessageDrawQuery q;
    q.msg = "A~E ";
    q.length = 400;
    q.widths[0] = 10;
    q.line_count = 1;
    q.position_idx = 0;  // top box: grows down from y = -92
    layout.draw(sink, chars, usa_font(), id_colors(), spec, q);

    SH_CHECK(sink.recs.size() == 1);
    SH_CHECK(!sink.recs[0].sprite);
    // About (0, -92): x = -5 maps to -10, cell 12x15 doubles to 24x30.
    SH_CHECK(sink.recs[0].x0 == -10 && sink.recs[0].y0 == -92);
    SH_CHECK(sink.recs[0].x1 == -10 && sink.recs[0].y1 == -62);
    SH_CHECK(sink.recs[0].x2 == 14 && sink.recs[0].y2 == -92);
    SH_CHECK(sink.recs[0].x3 == 14 && sink.recs[0].y3 == -62);
}

SH_TEST(text_message_scale_fit_never_shrinks_below_one) {
    RecordSink sink;
    TestChars chars;
    t::MessageSpec spec = usa_spec();
    spec.text_size_scale = 8192;
    t::MessageLayout layout;

    t::MessageDrawQuery q;
    q.msg = "A~E ";
    q.length = 400;
    q.widths[0] = 800;  // the width fit computes 1556, but the floor is 1.0
    q.line_count = 1;
    q.position_idx = 0;
    layout.draw(sink, chars, usa_font(), id_colors(), spec, q);

    // (152 << 12) / 400 = 1556 < 4096: the original takes max(scale, 1.0) -> sprites.
    SH_CHECK(sink.recs[0].sprite);
}

SH_TEST(text_message_scale_fit_height_of_bottom_box) {
    RecordSink sink;
    TestChars chars;
    t::MessageSpec spec = usa_spec();
    spec.text_size_scale = 8192;
    t::MessageLayout layout;

    t::MessageDrawQuery q;
    q.msg = "A~E ";
    q.length = 400;
    q.widths[0] = 10;
    q.line_count = 9;  // 144 px of lines in a 208 px bottom box: scale 5916
    q.position_idx = 1;
    layout.draw(sink, chars, usa_font(), id_colors(), spec, q);

    // anchor y = 76 - 8*16 = -52, grows up from -52 + 144 = 92; room 92 + 116 = 208;
    // scale = (208 << 12) / 144 = 5916. x -5 -> (-5*5916)>>12 = -8 (the shift floors),
    // y -52 -> 92 - 208 = -116, w = 17, h = 21.
    SH_CHECK(!sink.recs[0].sprite);
    SH_CHECK(sink.recs[0].x0 == -8 && sink.recs[0].y0 == -116);
    SH_CHECK(sink.recs[0].x2 == 9 && sink.recs[0].y3 == -95);
}

SH_TEST(text_message_scale_never_below_one) {
    RecordSink sink;
    TestChars chars;
    t::MessageSpec spec = usa_spec();
    spec.text_size_scale = 2048;  // 0.5 requested: ignored
    t::MessageLayout layout;

    t::MessageDrawQuery q;
    q.msg = "A~E ";
    q.length = 400;
    q.widths[0] = 10;
    q.line_count = 1;
    q.position_idx = 0;
    layout.draw(sink, chars, usa_font(), id_colors(), spec, q);

    SH_CHECK(sink.recs[0].sprite);  // 1.0: the original sprite path
}

// ---------- debug font ----------

namespace {

struct DebugRec {
    bool present = false;
    std::int32_t x = 0, y = 0, index = 0;
};

class DebugRecordSink final : public t::DebugGlyphSink {
public:
    std::vector<DebugRec> recs;
    void glyph(const t::DebugGlyph& g) override {
        recs.push_back(DebugRec{g.present, g.x, g.y, g.index});
    }
};

}  // namespace

SH_TEST(text_debug_font_records_and_advances) {
    DebugRecordSink sink;
    t::DebugFontSpec spec;
    const t::DebugDrawResult r = t::draw_debug(sink, spec, "a ~\nB", 100, 50, -40);

    SH_CHECK(sink.recs.size() == 5);  // one record per byte, spaces included
    SH_CHECK(sink.recs[0].present && sink.recs[0].x == 100 && sink.recs[0].index == 23);  // 'A' - '*'
    SH_CHECK(!sink.recs[1].present && sink.recs[1].x == 108);                            // ' '
    SH_CHECK(!sink.recs[2].present);                                                      // '~'
    SH_CHECK(!sink.recs[3].present);                                                      // '\n'
    SH_CHECK(sink.recs[4].present && sink.recs[4].x == -40 && sink.recs[4].y == 58);
    SH_CHECK(sink.recs[4].index == 24);  // 'B' - '*'
    SH_CHECK(r.x == -32 && r.y == 58);   // -40 + 8
}

SH_TEST(text_debug_font_underscore_tab_vtab_advance) {
    DebugRecordSink sink;
    t::DebugFontSpec spec;
    const t::DebugDrawResult r = t::draw_debug(sink, spec, "_\t\v", 0, 0, 0);
    SH_CHECK(sink.recs.size() == 3);
    for (const DebugRec& rec : sink.recs) {
        SH_CHECK(!rec.present);  // '_', '\t', '\v' draw nothing
    }
    SH_CHECK(r.x == 24);
}

SH_TEST(text_debug_font_negative_index_from_high_bytes) {
    DebugRecordSink sink;
    t::DebugFontSpec spec;
    const char text[] = {static_cast<char>(0x80), '\0'};
    t::draw_debug(sink, spec, text, 0, 0, 0);
    SH_CHECK(sink.recs.size() == 1);
    SH_CHECK(sink.recs[0].present);
    SH_CHECK(sink.recs[0].index == -170);  // (int8)0x80 - '*'
}
