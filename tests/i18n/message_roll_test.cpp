// A prepared right-to-left message in the message box: wrapped by the preparation, then rolled out by the layout
// one glyph of budget at a time. Every budget draws exactly that many glyphs, the message does not report its end
// before the last glyph is out, and each line fills from its right end.

#include "engine/text/message_layout.h"
#include "i18n/core/text_prepare.h"
#include "i18n/core/unicode.h"

#include "framework/sh_test.h"

#include <string>
#include <vector>

namespace {

namespace t = sh::text;

struct Drawn {
    std::int32_t x, y;
};

class Sink final : public t::GlyphSink {
public:
    std::vector<Drawn> out;
    void glyph(const t::GlyphDraw& g) override { out.push_back({g.x[0], g.y[0]}); }
};

// Every character one located glyph, 10 pixels wide.
class FlatChars final : public t::CharSource {
public:
    bool wide_text() override { return true; }
    int map_char(std::uint32_t, t::GlyphEmit out[2]) override {
        out[0] = t::GlyphEmit{};
        out[0].located = true;
        out[0].advance = 10;
        return 1;
    }
};

t::MessageSpec spec() {
    t::MessageSpec s;
    s.right_to_left = true;
    s.anchors.formula[0] = t::MessageBoxAnchors::Formula::Fixed;
    s.anchors.y_base[0] = -92;
    return s;
}

}  // namespace

SH_TEST(i18n_message_right_to_left_rolls_out_to_its_end) {
    sh::i18n::PrepareOptions options;
    options.dialect = sh::i18n::TextDialect::Message;
    options.base = sh::i18n::Direction::Rtl;
    options.wrap_width = 200;
    options.advance = [](char32_t) { return 10; };
    // Arabic with a colour span, a number, a Latin word and brackets; long enough for three lines.
    const sh::i18n::PreparedText prepared = sh::i18n::prepare_text(
        "\tلا_توجد_كتب_نافعة_هنا،_فقط_ ~C2 مفتاح_المكتبة ~C7 _ورقم_الخزانة_0886_وجهاز_PTV_قديم_(معطل)_منذ_عام_1999. ~E ",
        options);
    SH_CHECK(prepared.changed && prepared.wrapped >= 2);
    const std::string msg = prepared.text;

    FlatChars chars;
    t::FontAtlas font;
    font.glyph_byte_min = '!';
    t::ColorTable colors;

    // The measure the game takes before it draws.
    t::MessageLayout measure_layout;
    t::MessageMeasureQuery mq;
    mq.msg = msg.c_str();
    const t::MessageMeasureResult measured = measure_layout.measure(chars, font, spec(), mq);
    SH_CHECK(measured.line_count == prepared.wrapped + 1);
    for (int i = 0; i < measured.line_count; ++i) {
        SH_CHECK(measured.widths[i] > 0 && measured.widths[i] <= 200);
    }

    const auto draw = [&](int budget, Sink& sink) {
        t::MessageLayout layout;
        t::MessageDrawQuery q;
        q.msg = msg.c_str();
        q.length = budget;
        q.position_idx = 0;
        q.line_count = measured.line_count;
        for (int i = 0; i < 12; ++i) {
            q.widths[i] = measured.widths[i];
        }
        return layout.draw(sink, chars, font, colors, spec(), q);
    };

    Sink whole;
    SH_CHECK(draw(399, whole).code == -1);
    const int total = static_cast<int>(whole.out.size());
    SH_CHECK(total > 40);
    for (int budget = 1; budget <= total + 2; ++budget) {
        Sink sink;
        const t::MessageDrawResult r = draw(budget, sink);
        const int expected = budget < total ? budget : total;
        if (static_cast<int>(sink.out.size()) != expected) {
            SH_CHECK(static_cast<int>(sink.out.size()) == expected);
            break;
        }
        // Still rolling: the end of the message is not reported while glyphs are held back.
        if (budget < total) {
            SH_CHECK(r.code == 0);
        }
        // What is out is a suffix of every line: each glyph sits where it sits in the whole message.
        for (std::size_t i = 0; i < sink.out.size(); ++i) {
            bool found = false;
            for (const Drawn& d : whole.out) {
                found = found || (d.x == sink.out[i].x && d.y == sink.out[i].y);
            }
            SH_CHECK(found);
        }
        // The line being rolled out ends at the block's right edge.
        if (!sink.out.empty() && budget < total) {
            int right = sink.out.front().x;
            int last_y = sink.out.back().y;
            for (const Drawn& d : sink.out) {
                if (d.y == last_y) {
                    right = d.x > right || d.y != sink.out.front().y ? d.x : right;
                }
            }
            int widest = 0;
            for (int i = 0; i < measured.line_count; ++i) {
                widest = widest > measured.widths[i] ? widest : measured.widths[i];
            }
            int line_right = -1000;
            for (const Drawn& d : sink.out) {
                if (d.y == last_y && d.x > line_right) {
                    line_right = d.x;
                }
            }
            SH_CHECK(line_right + 10 == -(widest >> 1) + widest);
            (void)right;
        }
    }
}
