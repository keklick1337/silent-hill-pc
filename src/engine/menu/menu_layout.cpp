// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/menu/menu_layout.h"

#include <algorithm>

namespace sh::menu {

Window compute_window(const Metrics& m, int row_count, int top) {
    Window w;
    const int visible = std::max(1, m.visible);
    top = std::clamp(top, 0, std::max(0, row_count - visible));
    const int last = std::min(row_count, top + visible);
    for (int i = top; i < last; ++i) {
        w.slots.push_back({i, m.rows_y + (i - top) * m.pitch, m.pitch});
    }
    w.arrow_up = top > 0;
    w.arrow_down = last < row_count;
    w.up_y = m.rows_y - 6;
    w.down_y = m.rows_y + visible * m.pitch + 4;
    return w;
}

int row_at_point(const Metrics& m, int row_count, int top, int x, int y) {
    if (x < m.left_x || x >= m.right_x || y < m.rows_y) return -1;
    const int visible = std::max(1, m.visible);
    top = std::clamp(top, 0, std::max(0, row_count - visible));
    const int slot = (y - m.rows_y) / std::max(1, m.pitch);
    if (slot >= visible) return -1;
    const int row = top + slot;
    return row < row_count ? row : -1;
}

int last_top(const Metrics& m, const std::vector<int>& heights) {
    const int window = std::max(1, m.visible) * m.pitch;
    int top = static_cast<int>(heights.size()) - 1;
    if (top <= 0) return 0;
    int sum = heights[static_cast<std::size_t>(top)];
    while (top > 0 && sum + heights[static_cast<std::size_t>(top - 1)] <= window) {
        sum += heights[static_cast<std::size_t>(--top)];
    }
    return top;
}

Window compute_window(const Metrics& m, const std::vector<int>& heights, int top) {
    Window w;
    const int count = static_cast<int>(heights.size());
    const int window = std::max(1, m.visible) * m.pitch;
    top = std::clamp(top, 0, last_top(m, heights));
    int y = 0;
    int last = top;
    for (; last < count; ++last) {
        const int h = heights[static_cast<std::size_t>(last)];
        // A row taller than the whole window still shows when it is the first.
        if (y + h > window && last > top) break;
        w.slots.push_back({last, m.rows_y + y, h});
        y += h;
    }
    w.arrow_up = top > 0;
    w.arrow_down = last < count;
    w.up_y = m.rows_y - 6;
    w.down_y = m.rows_y + window + 4;
    return w;
}

int row_at_point(const Metrics& m, const std::vector<int>& heights, int top, int x, int y) {
    if (x < m.left_x || x >= m.right_x || y < m.rows_y) return -1;
    for (const Slot& s : compute_window(m, heights, top).slots) {
        if (y >= s.y && y < s.y + s.h) return s.row;
    }
    return -1;
}

Columns split_columns(const Metrics& m, int label_w, int value_w, bool arrows) {
    Columns c;
    const int total = std::max(0, m.right_x - m.left_x);
    const int reserve = arrows ? 2 * m.arrow_pad : 0;
    const int avail = std::max(0, total - reserve - (value_w > 0 ? m.gap : 0));
    if (label_w + value_w <= avail) {
        c.label_max = label_w;
        c.value_max = value_w;
        return c;
    }
    const int floor_w = total / 3;
    if (value_w <= avail - floor_w) {
        // The label gives way; the value stays whole.
        c.value_max = value_w;
        c.label_max = avail - value_w;
        c.label_cut = label_w > c.label_max;
        return c;
    }
    const int sum = std::max(1, label_w + value_w);
    int lm = static_cast<int>(static_cast<long long>(avail) * label_w / sum);
    lm = std::clamp(lm, std::min(floor_w, label_w), std::max(0, avail - std::min(floor_w, value_w)));
    c.label_max = lm;
    c.value_max = std::max(0, avail - lm);
    c.label_cut = label_w > c.label_max;
    c.value_cut = value_w > c.value_max;
    return c;
}

int keep_visible(int top, int selected, int count, int visible) {
    visible = std::max(1, visible);
    if (selected < top) top = selected;
    if (selected >= top + visible) top = selected - visible + 1;
    return std::clamp(top, 0, std::max(0, count - visible));
}

Span visible_span(int width, float wide) {
    if (!(wide > 1.0f)) return {0, width};
    const int extra = static_cast<int>(static_cast<float>(width) * (wide - 1.0f) * 0.5f);
    return {-extra, width + extra};
}

namespace {

bool is_break(char c) { return c == ' ' || c == '_'; }

// Rounded up: a width that fits at this size must not overflow by a rounding pixel.
int scaled_up(int w, int scale) { return static_cast<int>((static_cast<long long>(w) * scale + 4095) >> 12); }

// The down scroll arrow's centre sits 4 below the window and reaches 7 further.
constexpr int kArrowRoom = 12;

}  // namespace

std::vector<std::string> wrap_words(std::string_view text, int max_w,
                                    const std::function<int(std::string_view)>& width) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t sep_start = i;
        while (i < text.size() && is_break(text[i])) ++i;
        const std::size_t word_start = i;
        while (i < text.size() && !is_break(text[i])) ++i;
        if (word_start == i) break;  // trailing breaks
        const std::string_view sep = text.substr(sep_start, word_start - sep_start);
        const std::string_view word = text.substr(word_start, i - word_start);
        if (line.empty()) {
            line = word;
            continue;
        }
        std::string candidate = line;
        candidate += sep;
        candidate += word;
        if (width(candidate) <= max_w) {
            line = std::move(candidate);
        } else {
            lines.push_back(std::move(line));
            line = word;
        }
    }
    if (!line.empty()) lines.push_back(std::move(line));
    return lines;
}

HelpPlace place_help(const Metrics& m, int lines, int line_pitch) {
    HelpPlace p;
    p.first_y = lines <= 1 ? m.help_y : std::min(m.help_y, m.help_bottom - lines * line_pitch);
    p.visible = std::max(1, m.visible);
    if (lines > 0) {
        const int room = (p.first_y - kArrowRoom - m.rows_y) / std::max(1, m.pitch);
        p.visible = std::clamp(room, 1, p.visible);
    }
    return p;
}

namespace {

bool valued(const ColumnRow& r) { return r.bar || r.value > 0; }

int value_width(const ColumnInput& in, const ColumnRow& r, int s) {
    return (r.bar ? in.bar : 0) + scaled_up(r.value, s);
}

bool row_fits(const ColumnInput& in, const ColumnRow& r, int s) {
    const int avail = in.right - in.label_x;
    if (r.yields) return in.gap + value_width(in, r, s) <= avail;
    if (!valued(r)) return scaled_up(r.label, s) <= avail;
    return scaled_up(r.label, s) + in.gap + value_width(in, r, s) <= avail;
}

// The largest size, in small steps from `from` down to `floor_s`, at which `fits` holds (floor_s when none).
template <typename Fits>
int largest_scale(int from, int floor_s, const Fits& fits) {
    int s = std::max(from, floor_s);
    while (s > floor_s && !fits(s)) s = std::max(floor_s, s - 32);
    return s;
}

int floor_scale(const ColumnInput& in) { return std::clamp(in.min_scale, 1, 4096); }

int note_floor(const ColumnInput& in) {
    return in.note_min_scale > 0 ? std::clamp(in.note_min_scale, 1, 4096) : floor_scale(in);
}

}  // namespace

namespace {

// Where the column of the rows `use` starts at size `s`: after every label, every value whole before the right
// edge, at or right of min_value_x when there is room. `ok` is false when no column holds them all; the column
// then splits the row between the longest label and the widest value, the value never past the right half.
struct Column {
    int x = 0;
    bool ok = true;
};
template <typename Use>
Column column_at(const ColumnInput& in, int s, const Use& use) {
    int labels = in.label_x + in.gap;
    int values = 0;
    bool any = false;
    bool yielding = false;
    for (const ColumnRow& r : in.rows) {
        if (!valued(r) || !use(r)) continue;
        any = true;
        yielding = yielding || r.yields;
        if (!r.yields) labels = std::max(labels, in.label_x + scaled_up(r.label, s) + in.gap);
        values = std::max(values, value_width(in, r, s));
    }
    if (!any) return {in.right, true};
    const int lowest = in.label_x + in.gap;
    if (labels + values <= in.right) {
        int x = std::max(labels, yielding ? in.right - values : in.min_value_x);
        if (x + values > in.right) x = in.right - values;
        return {std::clamp(x, lowest, std::max(lowest, in.right)), true};
    }
    const int half = in.label_x + std::max(0, in.right - in.label_x) / 2;
    return {std::clamp(std::min(labels, std::max(in.right - values, half)), lowest, std::max(lowest, in.right)), false};
}

// Whether a row with a value is whole at size `s` with its value at `x`.
bool whole_at(const ColumnInput& in, const ColumnRow& r, int s, int x) {
    if (r.yields) return x + value_width(in, r, s) <= in.right;
    return in.label_x + scaled_up(r.label, s) + in.gap <= x && x + value_width(in, r, s) <= in.right;
}

}  // namespace

ColumnLayout options_columns(const ColumnInput& in) {
    ColumnLayout out;
    const int floor_s = floor_scale(in);
    // A row too long alone even at min_scale shrinks or pans by itself, so it does not shrink the page.
    const auto sizes_page = [&](const ColumnRow& r) { return row_fits(in, r, floor_s); };
    const int s = largest_scale(std::clamp(in.max_scale, 1, 4096), floor_s,
                                [&](int t) { return column_at(in, t, sizes_page).ok; });
    out.scale = s;
    out.value_x = column_at(in, s, sizes_page).x;
    out.fits = true;
    for (const ColumnRow& r : in.rows) {
        const RowPlace p = place_row(in, out, r);
        const bool whole = valued(r) ? whole_at(in, r, p.scale, out.value_x) : row_fits(in, r, p.scale);
        out.fits = out.fits && whole;
    }
    return out;
}

RowPlace place_row(const ColumnInput& in, const ColumnLayout& layout, const ColumnRow& row) {
    RowPlace p;
    p.scale = layout.scale;
    const int avail = std::max(0, in.right - in.label_x);
    const int least = std::min(layout.scale, note_floor(in));
    if (!valued(row)) {
        p.value_x = in.right;
        p.label_max = avail;
        p.scale = largest_scale(layout.scale, least, [&](int s) { return row_fits(in, row, s); });
        return p;
    }
    // Every value starts at the page's column. A row whose label or value does not fit beside it shrinks
    // alone, down to the size a note may take; what is still too long pans in its own side of the column.
    p.value_x = layout.value_x;
    p.label_max = std::max(0, layout.value_x - in.gap - in.label_x);
    p.scale = largest_scale(layout.scale, least, [&](int s) { return whole_at(in, row, s, layout.value_x); });
    return p;
}

}  // namespace sh::menu
