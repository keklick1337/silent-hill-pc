// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the game's text drawer (sh::text::GameText): cursor and state threading, the glyph records as
// draw2d commands and the packet runs. Expected values are hand-computed.

#include "engine/text/game_text.h"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "framework/sh_test.h"

namespace {

namespace t = sh::text;
namespace d2 = sh::draw2d;

struct Chars final : t::CharSource {
    int map_char(std::uint32_t code, t::GlyphEmit out[2]) override {
        out[0].cell = static_cast<std::int32_t>(code) - 39;
        out[0].advance = 10;
        return 1;
    }
};

struct Submitted {
    int layer;
    d2::Command command;
    t::TextStream stream;
};

class Host final : public t::GameTextHost {
public:
    bool hi = false;
    int timer = -1;
    int load_block = 0;
    bool released = false;
    int begins = 0, ends = 0;
    std::vector<Submitted> out;
    std::vector<std::string> messages;

    t::FontAtlas font() override { return {}; }
    t::CharSource& chars() override { return chars_; }
    const char* menu_text(const char* text) override { return text; }
    bool hi_res() override { return hi; }
    void set_hi_res(bool on) override { hi = on; }
    const char* map_message(std::int32_t index) override { return messages[index].c_str(); }
    std::int32_t message_line_cap() override { return 9; }
    std::int32_t message_v_shift() override { return 0; }
    float text_size_percent() override { return 100.0f; }
    bool cutscene_active() override { return false; }
    bool message_timer_running() override { return timer != -1; }
    void set_message_timer(std::int32_t value) override { timer = value; }
    void set_audio_load_block(std::int32_t block) override { load_block = block; }
    void release_text_layer_exemption() override { released = true; }
    void begin_run(bool) override { ++begins; }
    void submit(std::int32_t layer, const d2::Command& command, t::TextStream stream) override {
        out.push_back({layer, command, stream});
    }
    void reserve(const d2::Command&, t::TextStream) override {}
    void end_run(bool) override { ++ends; }

private:
    Chars chars_;
};

t::TextStyle shipped_style() {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/menu/text_style.json", std::ios::binary);
    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto style = t::parse_text_style(json);
    return style ? style.value() : t::TextStyle{};
}

}  // namespace

SH_TEST(text_game_text_position_is_centre_relative_and_keeps) {
    Host host;
    t::GameText text(host);
    text.set_style(shipped_style());
    text.set_layer(3);
    text.set_position(40, 160);
    SH_CHECK(text.cursor().x == -120 && text.cursor().y == 48 && text.cursor().line_start_x == -120);
    text.set_position(t::kKeep, 0);
    SH_CHECK(text.cursor().x == -120 && text.cursor().y == -112);
    // A position resets the layer to the default text layer.
    text.draw("A", 1);
    SH_CHECK(!host.out.empty() && host.out[0].layer == t::kDefaultTextLayer);
}

SH_TEST(text_game_text_sprite_glyph_commands) {
    Host host;
    t::GameText text(host);
    text.set_style(shipped_style());
    text.set_position(160, 112);
    text.set_layer(2);
    text.set_color(4);  // red
    const bool ended = text.draw("A", 10);
    SH_CHECK(ended);
    SH_CHECK(host.begins == 1 && host.ends == 1);
    SH_CHECK(host.out.size() == 2);
    const auto* s = std::get_if<d2::Sprite>(&host.out[0].command);
    SH_CHECK(s != nullptr);
    SH_CHECK(host.out[0].layer == 2 && host.out[0].stream == t::TextStream::Run);
    SH_CHECK(s->color == (d2::Rgb{128, 0, 0}));
    SH_CHECK(s->w == 12 && s->h == 16 && s->size == d2::RectSize::Free);
    SH_CHECK(!s->shade.translucent && !s->shade.raw_texture);
    SH_CHECK(std::holds_alternative<d2::DrawMode>(host.out[1].command));
    SH_CHECK(std::get<d2::DrawMode>(host.out[1].command).dither);
    // The cursor moved by the glyph's advance.
    SH_CHECK(text.cursor().x == 10 && text.cursor().y == 0);
}

SH_TEST(text_game_text_hi_res_quads_go_to_the_shared_area) {
    Host host;
    host.hi = true;
    t::GameText text(host);
    text.set_style(shipped_style());
    text.set_position(160, 112);
    text.draw("AB", 10);
    SH_CHECK(host.out.size() == 2);
    for (const Submitted& s : host.out) {
        SH_CHECK(std::holds_alternative<d2::Polygon>(s.command));
        SH_CHECK(s.stream == t::TextStream::Shared);
    }
}

SH_TEST(text_game_text_glyph_texel_words_carry) {
    // u past 255 carries into v, v past 255 into the palette word.
    t::GlyphDraw g;
    g.sprite = true;
    g.u = 250;
    g.v = 255;
    g.clut = 0x10;
    g.cut_left = 10;
    g.cut_right = 2;
    g.x[0] = 5;
    const t::GlyphCommands c = t::glyph_commands(g);
    SH_CHECK(c.count == 2);
    const d2::Sprite& s = std::get<d2::Sprite>(c.command[0]);
    SH_CHECK(s.uv.u == static_cast<std::uint8_t>(250 + 10) && s.uv.v == 255);
    SH_CHECK(s.at.x == 15 && s.w == 0);
    SH_CHECK(s.palette == d2::Palette::from_word(0x10));
}

SH_TEST(text_game_text_int_and_debug_number) {
    Host host;
    t::GameText text(host);
    text.set_style(shipped_style());
    text.draw_int(-42);
    SH_CHECK(host.out.size() == 6);  // '-', '4', '2' as sprite + mode each
    const std::string padded = text.debug_number(4, 7);
    SH_CHECK(padded == std::string("\v\v\v7"));
}

SH_TEST(text_game_text_message_reset_and_measure) {
    Host host;
    host.hi = true;
    host.messages = {"HELLO~N~LTHERE~E"};
    t::GameText text(host);
    text.set_style(shipped_style());
    text.reset_message_layout();
    SH_CHECK(!host.hi && text.color() == 7 && text.cursor().line_start_x == -120);
    text.measure_message(0);
    const std::int32_t code = text.draw_message(host.messages[0].c_str(), 400);
    SH_CHECK(host.released);
    SH_CHECK(code == -1);  // ~E
}

SH_TEST(text_style_shipped_values_and_checked_keys) {
    const t::TextStyle s = shipped_style();
    // The original's colour table, message start and box anchors.
    SH_CHECK(s.rgb[0] == (160u | 128u << 8 | 64u << 16) && s.rgb[4] == 128u && s.rgb[7] == 0x808080u);
    SH_CHECK(s.rgb[6] == 0x646464u && s.message_color == 7 && s.message_line_start_x == -120);
    SH_CHECK(s.message_box_y[0] == -92 && s.message_box_y[1] == 76 && s.message_box_y[2] == -60 &&
             s.message_box_y[3] == 44 && s.message_box_y[4] == -76 && s.display_all_length == 400);

    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/menu/text_style.json", std::ios::binary);
    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string unknown = json;
    unknown.replace(unknown.find("\"nuclear\""), 9, "\"neon\"");
    auto bad = t::parse_text_style(unknown);
    SH_CHECK(!bad && bad.error().find("unknown key \"neon\"") != std::string::npos);
    std::string missing = json;
    const std::size_t at = missing.find("\"display_all_length\"");
    missing.replace(at, missing.find('\n', at) - at, "\"description\": \"\"");
    bad = t::parse_text_style(missing);
    SH_CHECK(!bad && bad.error().find("missing \"display_all_length\"") != std::string::npos);
    std::string color = json;
    color.replace(color.find("\"color\": \"white\""), 16, "\"color\": \"pink\"");
    bad = t::parse_text_style(color);
    SH_CHECK(!bad && bad.error().find("\"color\" must name") != std::string::npos);
}
