// The staff roll module (src/engine/screens) against its specification: the markup
// encoding, the flat and perspective text records and the roll timing. Every expected
// value is worked out by hand from the roll's integer pipeline; the tests never link
// the game layer.

#include "engine/screens/roll_text.h"
#include "engine/screens/staff_roll.h"
#include "engine/screens/staff_roll_data.h"

#include "framework/sh_test.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace sc = sh::screens;

class Recorder final : public sc::RollSink {
public:
    std::vector<sc::RollPrim> prims;
    void prim(const sc::RollPrim& r) override { prims.push_back(r); }
};

sc::RollFont test_font() {
    sc::RollFont f;
    f.advance['A'] = 19;
    f.advance['b'] = 15;
    f.advance['K'] = 11;
    const char* g1 = "\x7f" "19ACDEGHIKLMNOP";
    const char* g2 = ".YVUTSR ";
    for (int i = 0; i < 16; i++) f.logo_glyphs[i] = static_cast<std::uint8_t>(g1[i]);
    for (int i = 0; i < 8; i++) f.logo_glyphs_2[i] = static_cast<std::uint8_t>(g2[i]);
    for (int i = 0; i < 7; i++) f.colors[i] = 0x010203u * static_cast<std::uint32_t>(i + 1);
    return f;
}

const std::uint8_t* bytes(const std::string& s) { return reinterpret_cast<const std::uint8_t*>(s.c_str()); }

}  // namespace

SH_TEST(staff_roll_markup_encodes_codes) {
    const sc::RollFont font = test_font();
    auto r = sc::encode_roll_markup("{span}{back_half}Ab{/span}", font);
    SH_CHECK(r && r.value() == std::string("\xD0\xF0" "Ab" "\xD8"));
    r = sc::encode_roll_markup("{-32}{span1}x{/span1}{+2}{c7}{fwd_full1}{x82}", font);
    SH_CHECK(r && r.value() == std::string("\x1D\xD1x\xD9\x11\x07\xE9\x82"));
    r = sc::encode_roll_markup("{kcet}KONAMI {xA0}{-1}TR{/kcet}\nA", font);
    SH_CHECK(r && r.value() == std::string("\xAA\xAE\xAD\xA3\xAC\xA9\xBF\xA0\x18\xBC\xBE\nA"));
}

SH_TEST(staff_roll_markup_rejects_bad_input) {
    const sc::RollFont font = test_font();
    SH_CHECK(!sc::encode_roll_markup("{foo}", font));
    SH_CHECK(!sc::encode_roll_markup("{+3}", font));
    SH_CHECK(!sc::encode_roll_markup("{span}open", font));
    SH_CHECK(!sc::encode_roll_markup("a}b", font));
    SH_CHECK(!sc::encode_roll_markup("{x00}", font));
    SH_CHECK(!sc::encode_roll_markup("{kcet}B{/kcet}", font));
    SH_CHECK(!sc::encode_roll_markup("caf\xC3\xA9", font));
}

SH_TEST(staff_roll_flat_text_centres_a_span) {
    sc::RollFont font = test_font();
    sc::FlatRollText text(font);
    Recorder rec;
    text.set_position(0, 10);
    text.set_color(0x404040);
    text.draw(bytes("\xD0\xF0" "Ab" "\xD8"), rec);
    SH_CHECK(rec.prims.size() == 3);
    // The span measures 19 + 15 = 34: half 17 back from the cursor.
    SH_CHECK(font.advance[0xE0] == 17 && font.advance[0xF0] == -17);
    SH_CHECK(font.advance[0xE8] == 34 && font.advance[0xF8] == -34);
    SH_CHECK(rec.prims[0].kind == sc::RollPrim::Kind::Glyph);
    SH_CHECK(rec.prims[0].x[0] == -17 && rec.prims[0].y[0] == 10);
    SH_CHECK(rec.prims[0].uv[0] == 48 + 72 * 256);  // cell 32: column 2, row 3
    SH_CHECK(rec.prims[0].rgb == 0x404040);
    SH_CHECK(rec.prims[1].x[0] == 2 && rec.prims[1].uv[0] == 120 + 144 * 256);
    SH_CHECK(rec.prims[2].kind == sc::RollPrim::Kind::TexturePage);
    SH_CHECK(text.x() == 17 && text.y() == 10);
}

SH_TEST(staff_roll_flat_text_logo_cells_colors_and_null) {
    sc::RollFont font = test_font();
    sc::FlatRollText text(font);
    Recorder rec;
    text.set_position(5, 0);
    text.set_semi_transparent(true);
    text.draw(bytes("\x03\xA0\xB8"), rec);
    SH_CHECK(rec.prims.size() == 3);
    SH_CHECK(rec.prims[0].kind == sc::RollPrim::Kind::LogoGlyph && rec.prims[0].uv[0] == 240);
    SH_CHECK(rec.prims[0].y[0] == 4 && rec.prims[0].rgb == 0x030609 && rec.prims[0].semi_transparent);
    SH_CHECK(rec.prims[1].uv[0] == 61440 + 128);
    text.draw(nullptr, rec);
    SH_CHECK(rec.prims.size() == 3);
}

SH_TEST(staff_roll_perspective_projects_a_glyph) {
    sc::RollFont font = test_font();
    sc::PerspectiveRollText text(font);
    sc::RollPlane plane;
    plane.origin_y = 139264;  // 34.0
    plane.origin_z = 983040;  // 240.0
    plane.column_x = 2867;    // 0.7
    plane.row_y = 899;
    plane.row_z = -3996;
    text.set_plane(plane);
    Recorder rec;
    text.draw(bytes("A"), rec);
    SH_CHECK(rec.prims.size() == 1);
    const sc::RollPrim& q = rec.prims[0];
    // Top row at 1/z = 4096, bottom row at 4538; x step 2867 and 3176 per pixel.
    SH_CHECK(q.kind == sc::RollPrim::Kind::Quad);
    SH_CHECK(q.x[0] == 0 && q.y[0] == 34 && q.x[1] == 13 && q.y[1] == 34);
    SH_CHECK(q.x[2] == 0 && q.y[2] == 43 && q.x[3] == 14 && q.y[3] == 43);
    SH_CHECK(q.uv[0] == 18480 && q.uv[1] == 18499 && q.uv[2] == 24624 && q.uv[3] == 24643);
    SH_CHECK(q.rgb == 0xFFFFFF && !q.semi_transparent);
}

namespace {

class FakeHost final : public sc::StaffRollHost {
public:
    std::vector<std::string> rows{"A"};
    std::string logo_line = "K";
    std::int32_t clock_ = 0;
    std::int32_t vbl = 2;
    std::int32_t steps[3] = {0, 0, 0};
    std::int32_t music = 1;
    sc::Fade fade_{sc::FadeState::OutComplete, false};
    std::vector<std::uint32_t> blend_modes;
    std::vector<std::string> calls;

    std::int32_t row_count() override { return 400; }
    const std::uint8_t* row(std::int32_t i) override {
        return i < static_cast<std::int32_t>(rows.size()) ? bytes(rows[i]) : nullptr;
    }
    const std::uint8_t* logo() override { return bytes(logo_line); }
    std::int32_t clock() override { return clock_; }
    void set_clock(std::int32_t v) override { clock_ = v; }
    std::int32_t vblanks() override { return vbl; }
    sc::EndingTiming ending(std::int32_t) override { return {-120, 0x21FC}; }
    void preload_music(std::int32_t) override { calls.push_back("preload"); }
    void play_music(std::int32_t) override { calls.push_back("play"); }
    std::int32_t music_state() override { return music; }
    void stop_bgm() override {}
    void stop_sfx() override {}
    void stop_voice() override { calls.push_back("stop_voice"); }
    bool skip_pressed() override { return false; }
    void set_flat_blend_mode(std::uint32_t m) override { blend_modes.push_back(m); }
    void plane_changed() override {}
    std::int32_t sin(std::int32_t) override { return 0; }
    std::int32_t cos(std::int32_t) override { return 4096; }
    std::int32_t step(int l) override { return steps[l]; }
    void set_step(int l, std::int32_t v) override { steps[l] = v; }
    void reset_step_timer() override {}
    bool came_from_gameplay() override { return true; }
    void enter_roll_state() override { calls.push_back("enter"); }
    void return_to_previous_state() override {}
    std::int32_t file_queue_length() override { return 0; }
    sc::Fade fade() override { return fade_; }
    void set_fade(sc::Fade f) override { fade_ = f; }
    void set_fade_step(std::int32_t) override {}
    void set_background(std::uint32_t) override {}
    void save_screen() override {}
    void clear_for_roll(bool) override {}
    void set_roll_display(bool) override {}
    void set_frame_interval(std::int32_t) override {}
    void restore_screen() override {}
};

}  // namespace

SH_TEST(staff_roll_flat_timing_and_shadow_passes) {
    sc::RollFont font = test_font();
    sc::StaffRollStyle style;
    style.shadow_rgb = 0x303030;
    style.text_rgb = 0x404040;
    sc::StaffRoll roll(font, style);
    FakeHost host;
    roll.start_flat(0, host);
    SH_CHECK(host.calls.size() == 1 && host.calls[0] == "preload");
    SH_CHECK(!roll.music_started(host));
    SH_CHECK(roll.music_started(host) && host.clock_ == -120);

    // Roll state: (8700 * 2 - 504) / 400 = 42 px per row, 65536 / 42 = 1560.
    host.steps[0] = 1;
    host.clock_ = 0;
    Recorder text;
    Recorder bar;
    roll.update(host, text, bar);
    SH_CHECK(host.clock_ == 2);
    SH_CHECK(bar.prims.size() == 1 && bar.prims[0].kind == sc::RollPrim::Kind::Bar);
    SH_CHECK(bar.prims[0].x[0] == -256 && bar.prims[0].y[0] == -224 && bar.prims[0].x[1] == 512);
    // Row 0 enters at 240 - 4: shadows at 235 and 237, the text at 236.
    SH_CHECK(text.prims.size() == 6);
    SH_CHECK(text.prims[0].y[0] == 235 && text.prims[0].rgb == 0x303030 && text.prims[0].semi_transparent);
    SH_CHECK(text.prims[2].y[0] == 237);
    SH_CHECK(text.prims[4].y[0] == 236 && text.prims[4].rgb == 0x404040 && !text.prims[4].semi_transparent);
    SH_CHECK(host.blend_modes.size() == 2 && host.blend_modes[0] == 1 && host.blend_modes[1] == 0);
    // The fade-out was complete (black): a black fade-in starts.
    SH_CHECK(host.fade_.state == sc::FadeState::InStart && !host.fade_.white && host.steps[1] == 1);
}

SH_TEST(staff_roll_finish_fades_then_switches) {
    sc::RollFont font = test_font();
    sc::StaffRollStyle style;
    sc::StaffRoll roll(font, style);
    FakeHost host;
    host.fade_ = {sc::FadeState::None, false};
    SH_CHECK(!roll.finish(host));
    SH_CHECK(host.fade_.state == sc::FadeState::OutStart && host.fade_.white);
    SH_CHECK(!roll.finish(host));
    host.fade_ = {sc::FadeState::OutComplete, true};
    SH_CHECK(roll.finish(host) && host.calls.back() == "enter");
}

SH_TEST(staff_roll_shipped_data_parses) {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/menu/staff_roll.json", std::ios::binary);
    SH_CHECK(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto data = sc::parse_staff_roll(text);
    SH_CHECK(data);
    SH_CHECK(data.value().lines.size() == 399);
    SH_CHECK(data.value().lines[0] == std::string("\xD0\xF0" "Executive Producer\xD8"));
    SH_CHECK(data.value().style.logo.tilt_angle == 879);
    SH_CHECK(data.value().style.plane.origin_y == 0x22000 && data.value().style.plane.column_x == 0xB33);
    SH_CHECK(data.value().font.advance[0x18] == -1 && data.value().style.shadow_rgb == 0x303030);
    // The per-ending table of the original roll (music XA lines 723..726, start clocks, lengths).
    const auto& e = data.value().endings;
    SH_CHECK(e[0].music == "staff_roll_music_bad_plus" && e[0].timing.start == 0 && e[0].timing.length == 0x21FC);
    SH_CHECK(e[1].music == "staff_roll_music_good_plus" && e[1].timing.start == -2400 && e[1].timing.length == 0x20D0);
    SH_CHECK(e[2].music == "staff_roll_music_good" && e[2].timing.start == -900 && e[2].timing.length == 0x21FC);
    SH_CHECK(e[3].music == "staff_roll_music_bad_plus" && e[3].timing.start == -180 && e[3].timing.length == 0x2148);
    SH_CHECK(e[4].music == "staff_roll_music_bad" && e[4].timing.start == -180 && e[4].timing.length == 0x2274);
    SH_CHECK(e[5].music == "staff_roll_music_bad" && e[5].timing.start == -120 && e[5].timing.length == 0x20D0);
}

SH_TEST(staff_roll_endings_reject_unknown_and_missing) {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/menu/staff_roll.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string key = "\"good\": {";
    const std::size_t at = text.find(key);
    SH_CHECK(at != std::string::npos);
    std::string renamed = text;
    renamed.replace(at, key.size(), "\"great\": {");
    auto bad = sc::parse_staff_roll(renamed);
    SH_CHECK(!bad && bad.error().find("unknown ending \"great\"") != std::string::npos);
    std::string extra = text;
    extra.replace(at, key.size(), key + "\"tempo\": 1, ");
    bad = sc::parse_staff_roll(extra);
    SH_CHECK(!bad && bad.error().find("unknown key \"tempo\"") != std::string::npos);
}
