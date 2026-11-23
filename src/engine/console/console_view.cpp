// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/console/console_view.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace sh::console {

namespace {

constexpr std::uint32_t kPanelBg = 0x101018E8u;
constexpr std::uint32_t kAccent = 0x70E0B0FFu;
constexpr std::uint32_t kSelection = 0x3A6EA5A0u;
constexpr std::uint32_t kPopupBg = 0x181824F0u;
constexpr std::uint32_t kPopupSel = 0x3A6EA5FFu;
constexpr std::uint32_t kDim = 0x8090B0FFu;
constexpr std::uint32_t kText = 0xE0E0E0FFu;
constexpr std::uint32_t kBright = 0xFFFFFFFFu;
constexpr std::uint32_t kUsage = 0x8A93A0FFu;
constexpr std::uint32_t kProblem = 0xFF6060FFu;

float snap(float v) { return std::round(v); }

std::size_t cp_count(const std::string& s, std::size_t begin, std::size_t end) {
    std::size_t n = 0;
    std::size_t pos = begin;
    while (pos < end && pos < s.size()) {
        (void)next_code_point(s, pos);
        ++n;
    }
    return n;
}

// Byte offset after `count` code points starting at `begin` (stops at `limit`).
std::size_t advance_cps(const std::string& s, std::size_t begin, std::size_t limit, std::size_t count) {
    std::size_t pos = begin;
    while (count > 0 && pos < limit) {
        (void)next_code_point(s, pos);
        --count;
    }
    return std::min(pos, limit);
}

void solid(DrawList& dl, const FontAtlas& font, float x, float y, float w, float h, std::uint32_t rgba) {
    if (w <= 0 || h <= 0) return;
    const Glyph g = font.solid();
    dl.quads.push_back({snap(x), snap(y), snap(w), snap(h), g.u0, g.v0, g.u1, g.v1, rgba});
}

// Draws text[begin,end) starting at column 0 at (x,y); at most `max_cols` code points. Returns columns drawn.
std::size_t text_run(DrawList& dl, const FontAtlas& font, float x, float y, const std::string& s,
                     std::size_t begin, std::size_t end, std::uint32_t rgba, std::size_t max_cols = ~std::size_t{0}) {
    std::size_t pos = begin;
    std::size_t col = 0;
    end = std::min(end, s.size());
    while (pos < end && col < max_cols) {
        const char32_t cp = next_code_point(s, pos);
        const Glyph& g = font.glyph(cp);
        if (g.w > 0 && g.h > 0) {
            dl.quads.push_back({snap(x + static_cast<float>(col) * font.cell_width() + g.x), snap(y + g.y),
                                snap(g.w), snap(g.h), g.u0, g.v0, g.u1, g.v1, rgba});
        }
        ++col;
    }
    return col;
}

const Line* find_line(const Console& c, std::uint64_t id) {
    const auto& ls = c.lines();
    if (ls.empty() || id < ls.front().id || id > ls.back().id) return nullptr;
    const Line& l = ls[static_cast<std::size_t>(id - ls.front().id)];
    return l.id == id ? &l : nullptr;
}

}  // namespace

std::uint32_t ConsoleView::level_colour(Level level) {
    switch (level) {
        case Level::Plain: return 0xE0E0E0FFu;
        case Level::Info: return 0xC8D0D8FFu;
        case Level::Warn: return 0xFFD060FFu;
        case Level::Error: return 0xFF6060FFu;
        case Level::Debug: return 0x8090B0FFu;
        case Level::Command: return 0x70E0B0FFu;
        case Level::Help: return 0x90C0FFFFu;
    }
    return 0xE0E0E0FFu;
}

DrawList ConsoleView::build(Console& console, const FontAtlas& font, const ViewParams& p) {
    DrawList dl;
    console_ = &console;
    drawn_.clear();
    cell_w_ = std::max(1.0f, font.cell_width());
    cell_h_ = std::max(1.0f, font.cell_height());
    text_x_ = snap(cell_w_ * 0.5f);
    columns_ = std::max(1, static_cast<int>(std::floor(p.width / cell_w_)) - 1);

    visible_rows_ = std::max(1, static_cast<int>(std::lround(p.panel_fraction * p.height / cell_h_)));
    panel_w_ = p.width;
    panel_h_ = (static_cast<float>(visible_rows_) + 1.0f) * cell_h_;
    panel_x_ = 0;
    panel_y_ = snap(-(1.0f - p.slide) * panel_h_);
    panel_bottom_ = panel_y_ + panel_h_;

    // Wrap the whole scrollback into rows.
    std::vector<Row> rows;
    for (const Line& l : console.lines()) {
        const std::string& t = l.text;
        std::size_t start = 0;
        do {
            std::size_t pos = start;
            std::size_t cols = 0;
            std::size_t last_space_end = std::string::npos;  // byte after the last space seen in this row
            while (pos < t.size() && cols < static_cast<std::size_t>(columns_)) {
                const std::size_t at = pos;
                (void)next_code_point(t, pos);
                ++cols;
                if (t[at] == ' ' && at > start) last_space_end = pos;
            }
            std::size_t end = pos;
            if (pos < t.size()) {
                if (last_space_end != std::string::npos) end = last_space_end;
            }
            rows.push_back({l.id, start, end, l.level, 0});
            start = end;
        } while (start < t.size());
    }

    const int total = static_cast<int>(rows.size());
    const int max_scroll = std::max(0, total - visible_rows_);
    if (console.scroll() > max_scroll) console.set_scroll(max_scroll);
    const int scroll = console.scroll();

    // Panel background and the accent line at the bottom of the prompt row.
    solid(dl, font, panel_x_, panel_y_, panel_w_, panel_h_, kPanelBg);
    solid(dl, font, panel_x_, panel_bottom_ - 2, panel_w_, 2, kAccent);

    // Scrollback; the "rows below" marker is drawn over the newest slot while scrolled back.
    const int text_slots = visible_rows_;
    const int end_row = total - scroll;
    const int begin_row = std::max(0, end_row - text_slots);
    const int count = end_row - begin_row;
    const int first_slot = text_slots - count;
    for (int i = 0; i < count; ++i) {
        Row r = rows[static_cast<std::size_t>(begin_row + i)];
        r.y = snap(panel_y_ + static_cast<float>(first_slot + i) * cell_h_);
        drawn_.push_back(r);
    }

    // Selection highlight first, so the text sits on top of it.
    if (console.has_selection()) {
        TextPos lo = console.selection_begin();
        TextPos hi = console.selection_end();
        if (std::pair(hi.line, hi.offset) < std::pair(lo.line, lo.offset)) std::swap(lo, hi);
        for (const Row& r : drawn_) {
            if (r.line < lo.line || r.line > hi.line) continue;
            const Line* l = find_line(console, r.line);
            if (!l) continue;
            const std::size_t s = r.line == lo.line ? lo.offset : 0;
            const std::size_t e = r.line == hi.line ? hi.offset : l->text.size();
            const std::size_t a = std::max(s, r.begin);
            const std::size_t b = std::min(e, r.end);
            if (b <= a) continue;
            const float c0 = static_cast<float>(cp_count(l->text, r.begin, a));
            const float c1 = static_cast<float>(cp_count(l->text, r.begin, b));
            solid(dl, font, text_x_ + c0 * cell_w_, r.y, (c1 - c0) * cell_w_, cell_h_, kSelection);
        }
    }
    for (const Row& r : drawn_) {
        const Line* l = find_line(console, r.line);
        if (!l) continue;
        text_run(dl, font, text_x_, r.y, l->text, r.begin, r.end, level_colour(r.level));
    }

    if (scroll > 0) {
        solid(dl, font, panel_x_, panel_y_ + static_cast<float>(visible_rows_ - 1) * cell_h_, panel_w_, cell_h_, kPanelBg);
        const std::string marker = "-- " + std::to_string(scroll) + " rows below --";
        text_run(dl, font, text_x_, snap(panel_y_ + static_cast<float>(visible_rows_ - 1) * cell_h_), marker, 0,
                 marker.size(), 0x808090C0u, static_cast<std::size_t>(columns_));
        const float region_h = static_cast<float>(visible_rows_) * cell_h_;
        const float thumb = std::max(8.0f, region_h * static_cast<float>(visible_rows_) / static_cast<float>(total));
        const float frac = max_scroll > 0 ? 1.0f - static_cast<float>(scroll) / static_cast<float>(max_scroll) : 1.0f;
        solid(dl, font, panel_w_ - 4, panel_y_ + (region_h - thumb) * frac, 3, thumb, 0xFFFFFF50u);
    }

    // Prompt row.
    const float prompt_y = snap(panel_y_ + static_cast<float>(visible_rows_) * cell_h_);
    const std::string& text = console.editor().text();
    const std::size_t caret_cp = cp_count(text, 0, console.editor().caret());
    const std::size_t room = static_cast<std::size_t>(std::max(1, columns_ - 3));
    const std::size_t skip_cp = caret_cp > room ? caret_cp - room : 0;
    const std::size_t skip_byte = advance_cps(text, 0, text.size(), skip_cp);
    const std::string head = "> ";
    text_run(dl, font, text_x_, prompt_y, head, 0, head.size(), kAccent);
    text_run(dl, font, text_x_ + 2 * cell_w_, prompt_y, text, skip_byte, text.size(), kText,
             static_cast<std::size_t>(std::max(1, columns_ - 2)));
    if (p.show_caret && std::fmod(p.time_seconds, 1.0) < 0.5) {
        solid(dl, font, text_x_ + static_cast<float>(2 + caret_cp - skip_cp) * cell_w_, prompt_y + 1, 2,
              cell_h_ - 3, kText);
    }

    // Hint row under the prompt.
    const Completion& comp = console.completion();
    float below = panel_bottom_;
    if (!comp.usage.empty() || !comp.problem.empty()) {
        solid(dl, font, panel_x_, below, panel_w_, cell_h_, kPanelBg);
        std::size_t col = 0;
        const std::size_t max_cols = static_cast<std::size_t>(columns_);
        if (!comp.usage.empty()) {
            std::size_t tok = 0;
            std::size_t pos = 0;
            const std::string& u = comp.usage;
            const std::size_t active = comp.active_arg >= 0 ? static_cast<std::size_t>(comp.active_arg) + 1 : 0;
            const std::size_t last_tok = static_cast<std::size_t>(std::count(u.begin(), u.end(), ' '));
            const std::size_t hl = comp.active_arg >= 0 ? std::min(active, last_tok) : ~std::size_t{0};
            while (pos <= u.size() && col < max_cols) {
                std::size_t sp = u.find(' ', pos);
                if (sp == std::string::npos) sp = u.size();
                const std::uint32_t colour = tok == hl ? kBright : (tok == 0 ? 0xC8D0D8FFu : kUsage);
                col += text_run(dl, font, text_x_ + static_cast<float>(col) * cell_w_, below, u, pos, sp, colour,
                                max_cols - col);
                if (sp < u.size() && col < max_cols) ++col;  // the space
                pos = sp + 1;
                ++tok;
            }
        }
        std::string tail;
        std::uint32_t tail_colour = kDim;
        if (!comp.problem.empty()) {
            tail = comp.usage.empty() ? comp.problem : " - " + comp.problem;
            tail_colour = kProblem;
        } else {
            const std::string& help = comp.active_arg >= 0 && !comp.arg_help.empty() ? comp.arg_help : comp.command_help;
            if (!help.empty()) tail = " - " + help;
        }
        if (col < max_cols && !tail.empty()) {
            text_run(dl, font, text_x_ + static_cast<float>(col) * cell_w_, below, tail, 0, tail.size(), tail_colour,
                     max_cols - col);
        }
        below += cell_h_;
    }

    // Candidate popup under the hint.
    popup_x_ = text_x_;
    popup_y_ = below;
    popup_w_ = 0;
    popup_h_ = 0;
    if (console.popup_open()) {
        const auto& items = comp.items;
        const int n = static_cast<int>(items.size());
        const int shown = std::max(1, std::min(n, p.max_popup_rows));
        std::size_t max_text = 0;
        std::size_t max_note = 0;
        for (const Candidate& c : items) {
            max_text = std::max(max_text, cp_count(c.text, 0, c.text.size()));
            max_note = std::max(max_note, cp_count(c.note, 0, c.note.size()));
        }
        const std::size_t row_len = 1 + max_text + (max_note ? 2 + max_note : 0);
        const std::size_t screen_cols = static_cast<std::size_t>(std::max(1.0f, std::floor(p.width / cell_w_)));
        const std::size_t cols = std::min(screen_cols, std::max<std::size_t>(24, row_len + 2));
        const bool footer = n > shown || comp.total > static_cast<std::size_t>(n);
        const int sel = console.selected();
        int first = sel < shown ? 0 : sel - shown + 1;
        first = std::max(0, std::min(first, n - shown));

        popup_w_ = static_cast<float>(cols) * cell_w_;
        popup_h_ = static_cast<float>(shown + (footer ? 1 : 0)) * cell_h_;
        solid(dl, font, popup_x_, popup_y_, popup_w_, popup_h_, kPopupBg);
        for (int i = 0; i < shown; ++i) {
            const Candidate& c = items[static_cast<std::size_t>(first + i)];
            const float y = popup_y_ + static_cast<float>(i) * cell_h_;
            if (first + i == sel) solid(dl, font, popup_x_, y, popup_w_, cell_h_, kPopupSel);
            const std::size_t avail = cols > 1 ? cols - 1 : 1;
            text_run(dl, font, popup_x_ + cell_w_, y, c.text, 0, c.text.size(), kText, avail);
            if (!c.note.empty()) {
                const std::size_t nc = 1 + max_text + 2;
                if (nc < cols) {
                    text_run(dl, font, popup_x_ + static_cast<float>(nc) * cell_w_, y, c.note, 0, c.note.size(), kDim,
                             cols - nc);
                }
            }
        }
        if (footer) {
            const std::string counter = std::to_string(sel + 1) + "/" + std::to_string(std::max<std::size_t>(comp.total, n));
            const std::size_t len = counter.size();
            if (len + 1 <= cols) {
                text_run(dl, font, popup_x_ + static_cast<float>(cols - len - 1) * cell_w_,
                         popup_y_ + static_cast<float>(shown) * cell_h_, counter, 0, len, kDim);
            }
        }
    }
    return dl;
}

std::optional<TextPos> ConsoleView::hit_test(float x, float y) const {
    if (!console_ || x < panel_x_ || x >= panel_x_ + panel_w_) return std::nullopt;
    if (popup_h_ > 0 && x >= popup_x_ && x < popup_x_ + popup_w_ && y >= popup_y_ && y < popup_y_ + popup_h_) {
        return std::nullopt;
    }
    for (const Row& r : drawn_) {
        if (y < r.y || y >= r.y + cell_h_) continue;
        const Line* l = find_line(*console_, r.line);
        if (!l) return std::nullopt;
        const float fx = (x - text_x_) / cell_w_;
        const std::size_t col = fx <= 0 ? 0 : static_cast<std::size_t>(std::lround(fx));
        return TextPos{r.line, advance_cps(l->text, r.begin, std::min(r.end, l->text.size()), col)};
    }
    return std::nullopt;
}

std::optional<TextPos> ConsoleView::drag_test(float x, float y) const {
    if (!console_ || drawn_.empty()) return std::nullopt;
    const Row* row = &drawn_.front();
    for (const Row& r : drawn_) {
        if (y >= r.y) row = &r;
    }
    const Line* l = find_line(*console_, row->line);
    if (!l) return std::nullopt;
    const std::size_t end = std::min(row->end, l->text.size());
    // Past the last row the selection takes that row whole, above the first one it starts at its head.
    if (y >= row->y + cell_h_) return TextPos{row->line, end};
    if (y < row->y) return TextPos{row->line, std::min(row->begin, end)};
    const float fx = (x - text_x_) / cell_w_;
    const std::size_t col = fx <= 0 ? 0 : static_cast<std::size_t>(std::lround(fx));
    return TextPos{row->line, advance_cps(l->text, row->begin, end, col)};
}

bool ConsoleView::covers(float x, float y) const {
    if (x >= panel_x_ && x < panel_x_ + panel_w_ && y >= panel_y_ && y < popup_y_) return true;
    return popup_h_ > 0 && x >= popup_x_ && x < popup_x_ + popup_w_ && y >= popup_y_ && y < popup_y_ + popup_h_;
}

}  // namespace sh::console
