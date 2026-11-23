// Wide (UTF-8) text in the cell-font layout: a sequence is one character for the budget and for map_char, a stray
// byte >= 0x80 is its Latin-1 character, and a located emission draws from its own texel position.

#include "engine/text/message_layout.h"
#include "engine/text/string_layout.h"
#include "engine/text/text_types.h"

#include "framework/sh_test.h"

#include <cstdint>
#include <vector>

namespace {

namespace t = sh::text;

struct Drawn {
    std::int32_t x, y, u, v, page, clut;
};

class Sink final : public t::GlyphSink {
public:
    std::vector<Drawn> out;
    void glyph(const t::GlyphDraw& g) override { out.push_back({g.x[0], g.y[0], g.u, g.v, g.page, g.clut}); }
};

// Codepoints >= 0x80 come back located on a page of their own (u = low byte of the codepoint), ASCII as cells.
class WideChars final : public t::CharSource {
public:
    bool wide = true;
    std::vector<std::uint32_t> seen;
    bool wide_text() override { return wide; }
    int map_char(std::uint32_t code, t::GlyphEmit out[2]) override {
        seen.push_back(code);
        out[0] = t::GlyphEmit{};
        out[0].advance = 10;
        if (code >= 0x80) {
            out[0].located = true;
            out[0].page = 0x0C;
            out[0].u = static_cast<std::int32_t>(code & 0xFF);
            out[0].v = 16;
            out[0].clut = 0x3FF3;
        } else {
            out[0].cell = static_cast<std::int32_t>(code) - 39;
        }
        return 1;
    }
};

t::FontAtlas usa_font() {
    t::FontAtlas f;
    f.v_base = 240;
    f.rows_per_page = 1;
    f.tpage_base = 0x10;
    f.packed_clut = 0x7FD3;
    f.glyph_byte_min = '!';
    return f;
}

}  // namespace

SH_TEST(text_wide_char_decode) {
    std::uint32_t cp = 0;
    SH_CHECK(t::read_wide_char("A", cp) == 1 && cp == 'A');
    SH_CHECK(t::read_wide_char("\xD0\x96x", cp) == 2 && cp == 0x416);             // U+0416
    SH_CHECK(t::read_wide_char("\xE2\x80\xA6", cp) == 3 && cp == 0x2026);         // …
    SH_CHECK(t::read_wide_char("\xF0\x9D\x84\x9E", cp) == 4 && cp == 0x1D11E);    // a 4-byte one
    SH_CHECK(t::read_wide_char("\xE9t\xE9", cp) == 1 && cp == 0xE9);              // Latin-1 é, not UTF-8
    SH_CHECK(t::read_wide_char("\xC3", cp) == 1 && cp == 0xC3);                   // cut sequence at the NUL
    SH_CHECK(t::read_wide_char("\xC0\x80", cp) == 1 && cp == 0xC0);               // overlong: a byte
    SH_CHECK(t::read_wide_char("\xE0\x80\x80", cp) == 1 && cp == 0xE0);           // overlong 3-byte
    SH_CHECK(t::read_wide_char("\xED\xA0\x80", cp) == 1 && cp == 0xED);           // a surrogate
}

SH_TEST(text_wide_string_budget_and_location) {
    Sink sink;
    WideChars chars;
    t::ColorTable colors;
    t::StringDrawQuery q;
    const char* text = "a\xD0\x96%\xE9z";  // a, U+0416, %, Latin-1 U+00E9, z
    q.text = text;
    q.length = 4;  // stops before 'z': four characters, not five bytes
    const t::StringDrawResult r = t::draw_string(sink, chars, usa_font(), colors, q);
    SH_CHECK(!r.terminated);
    SH_CHECK(chars.seen.size() == 4);
    SH_CHECK(chars.seen[1] == 0x416 && chars.seen[2] == '%' && chars.seen[3] == 0xE9);
    SH_CHECK(sink.out.size() == 4);
    // 'a' from the atlas arithmetic (cell 58 -> row 2: page 0x12, u 16 * 12 = 192)...
    SH_CHECK(sink.out[0].page == 0x12 && sink.out[0].u == 192 && sink.out[0].v == 240 && sink.out[0].clut == 0x7FD3);
    // ...U+0416 and U+00E9 where the source put them.
    SH_CHECK(sink.out[1].page == 0x0C && sink.out[1].u == 0x16 && sink.out[1].v == 16 && sink.out[1].clut == 0x3FF3);
    SH_CHECK(sink.out[3].u == 0xE9);
    SH_CHECK(sink.out[3].x == 30);
}

SH_TEST(text_wide_off_keeps_bytes) {
    Sink sink;
    WideChars chars;
    chars.wide = false;
    t::ColorTable colors;
    t::StringDrawQuery q;
    q.text = "\xD0\x96";
    q.length = 8;
    t::FontAtlas f = usa_font();
    t::draw_string(sink, chars, f, colors, q);
    SH_CHECK(chars.seen.size() == 2 && chars.seen[0] == 0xD0 && chars.seen[1] == 0x96);
}

SH_TEST(text_wide_message_widths) {
    WideChars chars;
    t::MessageLayout layout;
    t::MessageSpec spec;
    t::MessageMeasureQuery q;
    q.msg = "\xD0\x96\xD0\x96_a~E";
    const t::MessageMeasureResult m = layout.measure(chars, usa_font(), spec, q);
    SH_CHECK(m.widths[0] == 10 + 10 + 6 + 10);
    SH_CHECK(chars.seen.size() == 3 && chars.seen[0] == 0x416);
}
