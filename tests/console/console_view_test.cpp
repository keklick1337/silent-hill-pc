#include "framework/sh_test.h"

#include "engine/console/console_view.h"
#include "render/present/present_math.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
using namespace sh::console;

namespace {

FontAtlas make_font() {
    const fs::path p = fs::path(SH_PROJECT_ROOT) / "third_party/fonts/jetbrains-mono/JetBrainsMonoNL-Regular.ttf";
    std::ifstream in(p, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto r = FontAtlas::build(bytes, 16.0f);
    if (!r) throw sh::test::Skip{"font build failed: " + r.error()};
    return r.value();
}

struct Rig {
    FontAtlas font = make_font();
    Registry reg;
    Console con{reg, 1000};
    ConsoleView view;
    ViewParams p;

    Rig() {
        register_builtin_commands(reg);
        con.register_console_commands();
        p.width = 800;
        p.height = 400;
    }
    DrawList build() { return view.build(con, font, p); }
    // y of the middle of the newest scrollback row (the row just above the prompt)
    float newest_row_y() const { return view.panel_bottom() - font.cell_height() * 1.5f; }
};

}  // namespace

SH_TEST(console_view_geometry_and_slide) {
    Rig r;
    r.build();
    const float ch = r.font.cell_height();
    SH_CHECK(r.view.visible_rows() == static_cast<int>(std::lround(200.0f / ch)));
    SH_CHECK(std::abs(r.view.panel_bottom() - (static_cast<float>(r.view.visible_rows()) + 1) * ch) < 1.0f);
    SH_CHECK(r.view.columns() == static_cast<int>(800 / r.font.cell_width()) - 1);
    SH_CHECK(r.view.covers(100, 10));
    r.p.slide = 0;
    r.build();
    SH_CHECK(r.view.panel_bottom() <= 0.5f);
    SH_CHECK(!r.view.covers(100, 100));
}

SH_TEST(console_view_wraps_long_lines_at_spaces_and_hard_breaks) {
    Rig r;
    r.p.width = 160;
    r.build();
    const int cols = r.view.columns();
    SH_CHECK(cols > 4);
    std::string words;
    while (static_cast<int>(words.size()) < cols + 5) words += "abc ";
    r.con.print(Level::Plain, words);
    r.con.print(Level::Plain, std::string(static_cast<std::size_t>(cols) * 2 + 3, 'x'));
    const DrawList dl = r.build();
    SH_CHECK(!dl.quads.empty());
    const float ch = r.font.cell_height();
    const float y = r.newest_row_y();
    // the hard-broken line is three rows: the newest holds the tail
    const auto last = r.view.hit_test(2, y);
    SH_CHECK(last && last->line == r.con.lines().back().id && last->offset == static_cast<std::size_t>(cols) * 2);
    const auto mid = r.view.hit_test(2, y - ch);
    SH_CHECK(mid && mid->offset == static_cast<std::size_t>(cols));
    const auto first = r.view.hit_test(2, y - 2 * ch);
    SH_CHECK(first && first->offset == 0);
    // the words line wraps at a space, so its second row starts on a word
    const auto w2 = r.view.hit_test(2, y - 3 * ch);
    SH_CHECK(w2 && w2->line == r.con.lines().front().id && w2->offset > 0 && w2->offset <= static_cast<std::size_t>(cols));
    SH_CHECK(words[w2->offset - 1] == ' ');
}

SH_TEST(console_view_scroll_is_clamped) {
    Rig r;
    for (int i = 0; i < 100; ++i) r.con.print(Level::Info, "line " + std::to_string(i));
    r.build();
    r.con.set_scroll(100000);
    r.build();
    SH_CHECK(r.con.scroll() == 100 - r.view.visible_rows());
    // the oldest line is reachable at the top when scrolled to the end
    const auto top = r.view.hit_test(2, 1);
    SH_CHECK(top && top->line == r.con.lines().front().id);
    Rig few;
    few.con.print(Level::Info, "one");
    few.con.set_scroll(9);
    few.build();
    SH_CHECK(few.con.scroll() == 0);
}

SH_TEST(console_view_popup_is_below_the_prompt_and_covered) {
    Rig r;
    r.con.on_text("h");
    SH_CHECK(r.con.popup_open());
    const DrawList dl = r.build();
    const float below = r.view.panel_bottom();
    bool popup_quad = false;
    for (const Quad& q : dl.quads) {
        if (q.rgba == 0x181824F0u) {
            popup_quad = true;
            SH_CHECK(q.y >= below);
            SH_CHECK(q.x >= 0 && q.x + q.w <= r.p.width);
        }
    }
    SH_CHECK(popup_quad);
    SH_CHECK(r.view.covers(20, below + 1));
    SH_CHECK(!r.view.covers(20, 5000));
    SH_CHECK(!r.view.hit_test(20, below + 3).has_value());
    // a highlighted candidate gets a solid row
    r.con.on_key(Key::Down);
    bool selected = false;
    for (const Quad& q : r.build().quads) selected |= q.rgba == 0x3A6EA5FFu;
    SH_CHECK(selected);
}

SH_TEST(console_view_popup_limits_rows_and_scrolls_to_selection) {
    Rig r;
    r.con.on_key(Key::Tab);  // every command
    r.p.max_popup_rows = 3;
    const std::size_t n = r.con.completion().items.size();
    SH_CHECK(n > 3);
    for (std::size_t i = 0; i < n; ++i) r.con.on_key(Key::Down);
    const DrawList dl = r.build();
    float selected_y = -1;
    float popup_h = 0;
    for (const Quad& q : dl.quads) {
        if (q.rgba == 0x3A6EA5FFu) selected_y = q.y;
        if (q.rgba == 0x181824F0u) popup_h = q.h;
    }
    const float ch = r.font.cell_height();
    SH_CHECK(selected_y >= r.view.panel_bottom());
    SH_CHECK(popup_h <= ch * 4 + 1);  // 3 rows plus the counter row
    SH_CHECK(selected_y + ch <= r.view.panel_bottom() + popup_h + 1);
}

SH_TEST(console_view_selection_highlight_and_hit_offsets) {
    Rig r;
    r.con.print(Level::Plain, "hello world");
    r.build();
    const float cw = r.font.cell_width();
    const float y = r.newest_row_y();
    const auto a = r.view.hit_test(cw * 0.5f + cw * 2, y);
    SH_CHECK(a && a->offset == 2);
    const auto far = r.view.hit_test(r.p.width - 1, y);
    SH_CHECK(far && far->offset == 11);
    r.con.select_begin(*a);
    r.con.select_update({a->line, 7});
    bool found = false;
    for (const Quad& q : r.build().quads) found |= q.rgba == 0x3A6EA5A0u;
    SH_CHECK(found);
    SH_CHECK(!r.view.hit_test(10, r.view.panel_bottom() - 3).has_value());  // prompt row
}

SH_TEST(console_view_hint_row_uses_usage_and_problem) {
    Rig r;
    r.con.on_text("echo hi");
    bool bright = false;
    for (const Quad& q : r.build().quads) bright |= q.y >= r.view.panel_bottom() && q.rgba == 0xFFFFFFFFu;
    SH_CHECK(bright);
    SH_CHECK(r.view.covers(10, r.view.panel_bottom() + 1));
    r.con.on_key(Key::Escape);
    r.con.on_key(Key::Escape);
    r.con.on_text("zzzz");
    bool red = false;
    for (const Quad& q : r.build().quads) red |= q.y >= r.view.panel_bottom() && q.rgba == 0xFF6060FFu;
    SH_CHECK(red);
}

SH_TEST(console_view_level_palette) {
    SH_CHECK(ConsoleView::level_colour(Level::Error) == 0xFF6060FFu);
    SH_CHECK(ConsoleView::level_colour(Level::Command) == 0x70E0B0FFu);
    SH_CHECK(ConsoleView::level_colour(Level::Plain) == 0xE0E0E0FFu);
}

namespace {

// The console as the game shows it in a 1920x1080 window whose scene target is `scale` times larger: the view
// is laid out in target pixels with a font scaled to match, the pointer arrives in window units.
struct ScaledRig {
    int scale;
    FontAtlas font;
    Registry reg;
    Console con{reg, 1000};
    ConsoleView view;
    ViewParams p;
    sh::present::PointerInput ptr;

    explicit ScaledRig(int s) : scale(s), font(make_scaled_font(16.0f * static_cast<float>(s))) {
        p.width = 1920.0f * static_cast<float>(s);
        p.height = 1080.0f * static_cast<float>(s);
        ptr.window_w = ptr.drawable_w = 1920;
        ptr.window_h = ptr.drawable_h = 1080;
        ptr.present = {0, 0, 1920, 1080};
        ptr.target_w = 1920 * s;
        ptr.target_h = 1080 * s;
    }
    static FontAtlas make_scaled_font(float em) {
        const fs::path path = fs::path(SH_PROJECT_ROOT) / "third_party/fonts/jetbrains-mono/JetBrainsMonoNL-Regular.ttf";
        std::ifstream in(path, std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto r = FontAtlas::build(bytes, em);
        if (!r) throw sh::test::Skip{"font build failed: " + r.error()};
        return r.value();
    }
    void build() { (void)view.build(con, font, p); }
    // Window position of the middle of visible row `row` at the boundary before column `col`.
    sh::present::PointF window_at(int row, int col) const {
        const float s = static_cast<float>(scale);
        return {(font.cell_width() * (0.5f + static_cast<float>(col))) / s,
                (font.cell_height() * (0.5f + static_cast<float>(row))) / s};
    }
    sh::present::PointF to_view(sh::present::PointF window) const {
        return sh::present::pointer_to_overlay(ptr, window.x, window.y);
    }
};

void drag_selects_scrolled_text(int scale) {
    ScaledRig r(scale);
    for (int i = 0; i < 200; ++i) r.con.print(Level::Info, "line " + std::to_string(i));
    r.build();
    r.con.set_scroll(100000);  // the oldest line at the top row
    r.build();
    const auto from = r.to_view(r.window_at(0, 2));
    const auto to = r.to_view(r.window_at(2, 4));
    const auto a = r.view.hit_test(from.x, from.y);
    SH_CHECK(a.has_value());
    if (!a) return;
    r.con.select_begin(*a);
    const auto b = r.view.drag_test(to.x, to.y);
    SH_CHECK(b.has_value());
    if (!b) return;
    r.con.select_update(*b);
    SH_CHECK(r.con.selected_text() == "ne 0\nline 1\nline");
    bool highlighted = false;
    for (const Quad& q : r.view.build(r.con, r.font, r.p).quads) highlighted |= q.rgba == 0x3A6EA5A0u;
    SH_CHECK(highlighted);

    // scrolled halfway: the rows under the same window points are other lines
    r.con.set_scroll(r.con.scroll() - 50);
    r.build();
    const auto a2 = r.view.hit_test(from.x, from.y);
    const auto b2 = r.view.drag_test(to.x, to.y);
    SH_CHECK(a2 && b2);
    if (!a2 || !b2) return;
    r.con.select_begin(*a2);
    r.con.select_update(*b2);
    SH_CHECK(r.con.selected_text() == "ne 50\nline 51\nline");

    // without the mapping a supersampled target puts the window point on another row
    if (scale > 1) {
        const auto raw = r.window_at(2, 4);
        const auto wrong = r.view.hit_test(raw.x, raw.y);
        SH_CHECK(!wrong || !(*wrong == *b2));
    }
}

}  // namespace

SH_TEST(console_view_drag_selects_text_at_render_scale_1) { drag_selects_scrolled_text(1); }
SH_TEST(console_view_drag_selects_text_at_render_scale_2) { drag_selects_scrolled_text(2); }

SH_TEST(console_view_drag_outside_the_rows_takes_the_nearest) {
    Rig r;
    r.con.print(Level::Plain, "first");
    r.con.print(Level::Plain, "second");
    r.build();
    const float ch = r.font.cell_height();
    const float y = r.newest_row_y();
    const auto id0 = r.con.lines().front().id;
    // above the first drawn row (the empty top of the panel): the head of the first line
    const auto above = r.view.drag_test(300, 1);
    SH_CHECK(above && above->line == id0 && above->offset == 0);
    SH_CHECK(!r.view.hit_test(300, 1).has_value());
    // below the last row (prompt, scene): the end of the last line
    const auto below = r.view.drag_test(2, y + ch * 6);
    SH_CHECK(below && below->line == id0 + 1 && below->offset == 6);
    // left of the text on a row: column 0
    const auto left = r.view.drag_test(-50, y - ch);
    SH_CHECK(left && left->line == id0 && left->offset == 0);
    r.con.select_begin(*above);
    r.con.select_update(*below);
    SH_CHECK(r.con.selected_text() == "first\nsecond");
    ConsoleView empty;
    SH_CHECK(!empty.drag_test(1, 1).has_value());
}
