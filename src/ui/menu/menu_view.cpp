// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/text/game_text.h"
#include "ui/menu/menu_view.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "engine/config/pc_config.h"
#include "engine/log/sh_log.h"
#include "engine/game_state/game_state.h"
#include "i18n/localization.h"
#include "ui/menu/nm_draw.h"
#include "render/gpu/render_queries.h"
#include "render/gpu/render_state.h"

namespace gs = sh::game_state;
namespace nm = sh::ui::nm;

namespace sh::menu {

bool looks_like_key(std::string_view s) {
    if (s.empty() || s.front() == '.' || s.back() == '.' || s.find('.') == std::string_view::npos) return false;
    for (const char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == ':' || c == '-';
        if (!ok) return false;
    }
    return true;
}

namespace {

constexpr int kScaleFull = 4096;  // Q12 1.0

// The longest data text a row shows, in bytes.
constexpr std::size_t kLiteralBytes = 255;

// The pause menu's rule: its 12x16 glyphs break up when shrunk below 480 lines. The lines are the frame's as
// rendered (a resized window, borderless), not the configured size. Native PSX resolution does not count: menu
// text is drawn at window resolution there too, so the Original preset keeps the sizes of every other one.
int row_scale() { return PcConfig_RenderHeight() < 480 ? kScaleFull : 3072; }

int help_scale() { return row_scale() == kScaleFull ? kScaleFull : 2560; }

int scaled(int v, int scale) { return (v * scale) >> 12; }

// A page shrinks to fit no further than the pause rows (their glyphs hold up at that size from 480 lines).
int min_fit_scale_now() { return row_scale(); }

int color_of(Tone t) {
    switch (t) {
    case Tone::Dim: return gs::text_color::kDarkGrey;
    case Tone::Info: return gs::text_color::kLightGrey;
    case Tone::Group: return gs::text_color::kGold;
    case Tone::Header:
    case Tone::Normal: break;
    }
    return gs::text_color::kWhite;
}

class NmCanvas final : public MenuCanvas {
public:
    Label localize(std::string_view s) override {
        Label out;
        if (looks_like_key(s)) {
            const std::string key(s);
            const char* t = sh::i18n::ui_menu_text(key.c_str());
            // sh::i18n::ui_menu_text answers the key itself when no layer has it: show it as it is.
            if (t != nullptr && key != t) {
                out.text = t;
                return out;
            }
        }
        return literal(s);
    }

    // Data text, drawn as it is, folded to the glyphs the menu font has.
    Label literal(std::string_view s) override {
        Label out;
        out.text = nm::font_text(s, kLiteralBytes);
        out.verbatim = true;
        return out;
    }

    Label as_drawn(const Label& s) override {
        if (s.verbatim) return s;
        return Label{sh::i18n::menu_text(s.text.c_str()), true};
    }

    void set_fit_scale(int q12) override { fit_scale_ = std::clamp(q12, 1, kScaleFull); }
    int min_fit_scale() override { return min_fit_scale_now(); }
    int min_note_scale() override { return help_scale(); }

    int text_width(const Label& s, TextSize size) override {
        const int w = s.verbatim ? sh::i18n::text_width(s.text.c_str()) : nm::text_width(s.text.c_str());
        return scaled(w, scale_of(size));
    }

    void text(int x, int y, TextSize size, Tone tone, const Label& s) override {
        const int sc = scale_of(size);
        sh::text::game_text().set_scale(sc);
        if (tone == Tone::Header) {
            const std::string h = std::string("\x07") + s.text;
            draw(x, y, color_of(tone), h.c_str(), s.verbatim);
        } else {
            draw(x, y, color_of(tone), s.text.c_str(), s.verbatim);
        }
        sh::text::game_text().set_scale(kScaleFull);
    }

    int fitted(int x, int y, TextSize size, Tone tone, const Label& s, int max_w, bool pan) override {
        const int sc = scale_of(size);
        const int w = text_width(s, size);
        if (w <= max_w) {
            text(x, y, size, tone, s);
            return w;
        }
        const char* shown = s.verbatim ? s.text.c_str() : sh::i18n::menu_text(s.text.c_str());
        const std::string copy(shown);
        sh::text::game_text().set_scale(sc);
        const int drawn = nm::text_fitted(x, y, color_of(tone), copy.c_str(), max_w, pan);
        sh::text::game_text().set_scale(kScaleFull);
        return drawn;
    }

    void centred(int cx, int y, TextSize size, Tone tone, const Label& s) override {
        text(cx - text_width(s, size) / 2, y, size, tone, s);
    }

    void highlight(int x0, int x1, int y_bottom) override { nm::highlight(x0, x1, y_bottom); }
    void value_arrows(int x_left, int x_right, int y) override { nm::value_arrows(x_left, x_right, y); }
    void scroll_arrow(int cx, int y, bool up, bool enabled) override { nm::scroll_arrow(cx, y, up, enabled); }
    void bullet(int x, int y, bool selected) override { nm::bullet(x, y, selected); }
    void bar(int x, int y, int notches, double fraction) override {
        nm::bar(x, y, notches, static_cast<int>(std::clamp(fraction, 0.0, 1.0) * 4096.0 + 0.5));
    }
    void veil() override { nm::dim_background(); }

    Metrics metrics() override {
        Metrics m;
        const bool full = row_scale() == kScaleFull;
        m.centre_x = 160;
        m.header_y = full ? 34 : 38;
        m.rows_y = 60;
        m.pitch = full ? 20 : 15;
        m.visible = full ? 7 : 9;
        m.left_x = 36;
        m.right_x = 284;
        m.arrow_pad = 16;
        m.gap = 12;
        m.help_y = full ? 204 : 208;
        m.help_bottom = 222;
        return m;
    }

private:
    int scale_of(TextSize s) const {
        if (s == TextSize::Full) return kScaleFull;
        if (s == TextSize::Fit) return fit_scale_;
        return s == TextSize::Help ? help_scale() : row_scale();
    }

    int fit_scale_ = kScaleFull;

    static void draw(int x, int y, int color, const char* s, bool verbatim) {
        if (verbatim) {
            nm::text_verbatim(x, y, color, s);
        } else {
            nm::text(x, y, color, s);
        }
    }
};

Tone tone_of(const RowView& r) {
    if (!r.enabled) return Tone::Dim;
    if (r.def != nullptr && r.def->kind == RowKind::Info) return Tone::Info;
    if (r.def != nullptr && r.def->kind == RowKind::Header) return Tone::Group;
    return Tone::Normal;
}

}  // namespace

MenuCanvas& nm_canvas() {
    static NmCanvas canvas;
    return canvas;
}

float claim_full_width() {
    // Over a 3D frame the frame's own widescreen mode decides: Hor+ already spans the window (also while
    // the framing gate still holds a closing world frame wide), pillarbox and stretch keep the 4:3 frame.
    if (sh::render::picture().hor_plus) return sh::render::picture().widescreen_mode == 1 ? GR_Ui2dWideFactor() : 1.0f;
    sh::render::picture().ui2d_wide_frames = 2;
    return GR_Ui2dWideFactor();
}

namespace {

constexpr int kPauseHeaderY = 64;
constexpr int kPauseRowsY = 96;
constexpr int kConfirmY = 104;
constexpr int kAnswerDy = 24;
constexpr int kAnswerCx[2] = {120, 200};
constexpr int kAnswerH = 16;
constexpr const char* kAnswerKeys[2] = {"ui.pause.yes", "ui.pause.no"};

}  // namespace

// The Options screen's geometry (as the original screen's, gamedata/menu/options.json): heading at y 20,
// bullets at x 40 with their labels at 64, a 16-line pitch, the rows above the bottom vignette band.
constexpr int kOptBulletX = 40;
constexpr int kOptLabelX = 64;
constexpr int kOptValueMin = 168;  // the retail column; a page of short labels keeps it
constexpr int kOptValueGap = 20;   // label end .. value column: the value's left arrow sits here
constexpr int kOptRight = 304;
constexpr int kOptBarNotches = 8;
constexpr int kOptBarWidth = kOptBarNotches * 6;
constexpr int kOptBarTextGap = 6;
constexpr int kOptMargin = 16;  // text keeps this far from the screen's sides
// How far the rows move left into the room a wide window adds: at 4:3 they sit where the retail screen has them,
// at 16:9 the block is better balanced and long labels and values get room for one value column at full size.
constexpr int kOptWideShift = 40;

int MenuView::options_shift() const { return std::max(span_.left, -kOptWideShift); }
int MenuView::options_bullet_x() const { return kOptBulletX + options_shift(); }
int MenuView::options_label_x() const { return kOptLabelX + options_shift(); }

// Labels, values and the help line may run into the room a wide window adds on the right.
int MenuView::options_right() const { return std::max(kOptRight, span_.right - kOptMargin); }

Metrics MenuView::metrics() const {
    Metrics m = canvas_->metrics();
    if (style_ == Style::Options) {
        m.centre_x = 160;
        m.header_y = 20;
        m.rows_y = 42;
        m.pitch = 16;
        m.visible = 9;
        m.left_x = options_bullet_x() - 4;
        m.right_x = options_right();
        m.arrow_pad = 16;
        m.gap = 12;
        m.help_y = 200;
        m.help_bottom = 220;
    } else if (style_ == Style::Pause) {
        m.header_y = kPauseHeaderY;
        m.rows_y = kPauseRowsY;
        m.left_x = 60;
        m.right_x = 260;
        return m;
    }
    if (help_lines_ > 0) m.visible = place_help(m, help_lines_, help_pitch()).visible;
    return m;
}

int MenuView::help_pitch() const { return std::max(8, scaled(16, help_scale())); }

std::vector<std::string> MenuView::help_lines(const std::string& help) {
    if (help.empty()) return {};
    const Label h = canvas_->as_drawn(canvas_->localize(help));
    int width = 0;
    if (style_ == Style::Options) {
        width = options_right() - std::min(kOptMargin, span_.left + kOptMargin);
    } else {
        const Metrics m = canvas_->metrics();
        width = m.right_x - m.left_x;
    }
    return wrap_words(h.text, width, [this](std::string_view piece) {
        return canvas_->text_width(Label{std::string(piece), true}, TextSize::Help);
    });
}

void MenuView::draw_help(const std::vector<std::string>& lines, const Metrics& m) {
    if (lines.empty()) return;
    const int pitch = help_pitch();
    const HelpPlace place = place_help(m, static_cast<int>(lines.size()), pitch);
    int left = m.left_x;
    int right = m.right_x;
    if (style_ == Style::Options) {
        left = std::min(kOptMargin, span_.left + kOptMargin);
        right = options_right();
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const Label line{lines[i], true};
        const int y = place.first_y + static_cast<int>(i) * pitch;
        const int tw = canvas_->text_width(line, TextSize::Help);
        // Centred on the menu's middle while it fits there; a word wider than the whole area pans.
        const int half = std::min(m.centre_x - left, right - m.centre_x);
        const int x = tw <= 2 * half ? m.centre_x - tw / 2 : (tw <= right - left ? right - tw : left);
        canvas_->fitted(x, y, TextSize::Help, Tone::Info, line, right - x, true);
    }
}

void MenuView::fit(Controller& c) {
    // The help area is as tall as the page's longest help, so the row window keeps its size while the
    // selection moves.
    help_lines_ = 0;
    if (style_ != Style::Pause && c.is_open()) {
        for (const RowView& r : c.rows()) {
            if (r.def == nullptr) continue;
            if (r.def->help_fn) {
                help_lines_ = std::max(help_lines_, static_cast<int>(help_lines(r.def->help_fn()).size()));
            }
            if (r.def->help.empty()) continue;
            help_lines_ = std::max(help_lines_, static_cast<int>(help_lines(r.def->help).size()));
        }
        help_lines_ = std::max(help_lines_, static_cast<int>(help_lines(c.help()).size()));
    }
    c.set_visible_rows(metrics().visible);
}

void MenuView::draw_marked(int cx, int y, Tone tone, const Label& label, bool selected, TextSize size) {
    // Centred the way the title rows are: the opening mark counts, the closing one hangs outside.
    const Label open{selected ? "[" : "_", true};
    const Label close{"]", true};
    const int open_w = canvas_->text_width(open, size);
    int x = cx - (canvas_->text_width(label, size) + canvas_->text_width(Label{"[", true}, size)) / 2;
    canvas_->text(x, y, size, tone, open);
    x += open_w;
    canvas_->text(x, y, size, tone, label);
    if (selected) canvas_->text(x + canvas_->text_width(label, size), y, size, tone, close);
}

void MenuView::draw_confirm(const Controller& c) {
    const Metrics m = metrics();
    canvas_->centred(m.centre_x, kConfirmY, TextSize::Full, Tone::Normal, canvas_->localize(c.confirm_question()));
    for (int i = 0; i < 2; ++i) {
        const bool sel = i == 0 ? c.confirm_yes() : !c.confirm_yes();
        draw_marked(kAnswerCx[i], kConfirmY + kAnswerDy, Tone::Normal, canvas_->localize(kAnswerKeys[i]), sel,
                    TextSize::Full);
    }
}

int MenuView::hover_answer(const Controller& c, int mouse_x, int mouse_y) const {
    if (!c.confirm_open() || mouse_y < kConfirmY + kAnswerDy || mouse_y >= kConfirmY + kAnswerDy + kAnswerH) return -1;
    for (int i = 0; i < 2; ++i) {
        const Label label = canvas_->localize(kAnswerKeys[i]);
        const int w = canvas_->text_width(Label{"[", true}, TextSize::Full) +
                      canvas_->text_width(label, TextSize::Full) + canvas_->text_width(Label{"]", true}, TextSize::Full);
        const int x = kAnswerCx[i] - (canvas_->text_width(label, TextSize::Full) +
                                      canvas_->text_width(Label{"[", true}, TextSize::Full)) / 2;
        if (mouse_x >= x && mouse_x < x + w) return i;
    }
    return -1;
}

int MenuView::hover_row(const Controller& c, int mouse_x, int mouse_y) const {
    const Metrics m = metrics();
    if (c.dropdown_open()) {
        const DropdownView d = c.dropdown();
        const int top = keep_visible(d.top, d.selected, static_cast<int>(d.items.size()), m.visible);
        // +3: the same lead-in above the glyph that the pause rows use.
        return row_at_point(m, static_cast<int>(d.items.size()), top, mouse_x, mouse_y + 3);
    }
    return row_at_point(m, static_cast<int>(c.rows().size()), c.scroll_top(), mouse_x, mouse_y + 3);
}

void MenuView::draw(const Controller& c) {
    if (!c.is_open()) return;
    const Metrics m = metrics();
    span_ = visible_span(kMenuWidth, claim_full_width());
    nm::keep_text_unfiltered(true);

    if (style_ == Style::Options) {
        if (veil_) canvas_->veil();
        if (c.confirm_open()) {
            draw_confirm(c);
            return;
        }
        draw_options(c, m);
        return;
    }
    if (c.confirm_open()) {
        draw_confirm(c);
        return;
    }
    if (style_ == Style::Pause) {
        if (header_visible_) {
            canvas_->centred(m.centre_x, m.header_y, TextSize::Full, Tone::Header, canvas_->localize(c.title()));
        }
    } else {
        canvas_->centred(m.centre_x, m.header_y, TextSize::Row, Tone::Header, canvas_->localize(c.title()));
    }
    if (c.dropdown_open()) {
        draw_dropdown(c, m);
    } else {
        draw_rows(c, m);
        draw_help(help_lines(c.help()), m);
    }
}

void MenuView::draw_rows(const Controller& c, const Metrics& m) {
    const std::vector<RowView> rows = c.rows();
    const Window w = compute_window(m, static_cast<int>(rows.size()), c.scroll_top());
    for (const Slot& s : w.slots) {
        const RowView& r = rows[static_cast<std::size_t>(s.row)];
        draw_row(r, s.y, s.row == c.selection(), m);
    }
    const int cx = m.centre_x;
    if (w.arrow_up) canvas_->scroll_arrow(cx, w.up_y, true, true);
    if (w.arrow_down) canvas_->scroll_arrow(cx, w.down_y, false, true);
}

void MenuView::draw_row(const RowView& r, int y, bool selected, const Metrics& m) {
    const Tone tone = tone_of(r);
    const Label label = canvas_->localize(r.def != nullptr ? r.def->label : std::string());
    if (style_ == Style::Pause) {
        draw_marked(m.centre_x, y, tone, label, selected, TextSize::Row);
        return;
    }
    Label value;
    if (!r.value.empty()) value = r.value_is_key ? canvas_->localize(r.value) : canvas_->literal(r.value);

    const bool info = r.def != nullptr && (r.def->kind == RowKind::Info || r.def->kind == RowKind::Header);
    const Label open{info ? "_" : (selected ? "[" : "_"), true};
    const Label close{"]", true};
    const int open_w = canvas_->text_width(open, TextSize::Row);
    const int close_w = (selected && !info) ? canvas_->text_width(close, TextSize::Row) : 0;
    const int label_w = canvas_->text_width(label, TextSize::Row);
    const int value_w = value.text.empty() ? 0 : canvas_->text_width(value, TextSize::Row);

    const Columns cols = split_columns(m, open_w + label_w + close_w, value_w, true);
    const int text_max = std::max(0, cols.label_max - open_w - close_w);

    canvas_->text(m.left_x, y, TextSize::Row, tone, open);
    const int drawn = canvas_->fitted(m.left_x + open_w, y, TextSize::Row, tone, label, text_max, selected);
    if (close_w > 0) canvas_->text(m.left_x + open_w + drawn, y, TextSize::Row, tone, close);

    if (value_w > 0) {
        const int right = m.right_x - m.arrow_pad;
        const int vw = std::min(value_w, cols.value_max);
        const int vx = right - vw;
        canvas_->fitted(vx, y, TextSize::Row, tone, value, cols.value_max, selected);
        if (selected && r.has_arrows && r.enabled) canvas_->value_arrows(vx, right, y);
    } else if (selected && r.has_arrows && r.enabled) {
        canvas_->value_arrows(m.right_x - m.arrow_pad - 8, m.right_x - m.arrow_pad, y);
    }
}

void MenuView::draw_dropdown(const Controller& c, const Metrics& m) {
    const DropdownView d = c.dropdown();
    const int count = static_cast<int>(d.items.size());
    const int top = keep_visible(d.top, d.selected, count, m.visible);
    const Window w = compute_window(m, count, top);
    const int width = m.right_x - m.left_x;
    for (const Slot& s : w.slots) {
        const bool sel = s.row == d.selected;
        const Label label = canvas_->localize(d.items[static_cast<std::size_t>(s.row)].label);
        const Label open{sel ? "[" : "_", true};
        const Label close{"]", true};
        const int open_w = canvas_->text_width(open, TextSize::Row);
        const int close_w = sel ? canvas_->text_width(close, TextSize::Row) : 0;
        canvas_->text(m.left_x, s.y, TextSize::Row, Tone::Normal, open);
        const int drawn = canvas_->fitted(m.left_x + open_w, s.y, TextSize::Row, Tone::Normal, label,
                                          std::max(0, width - open_w - close_w), sel);
        if (sel) {
            canvas_->text(m.left_x + open_w + drawn, s.y, TextSize::Row, Tone::Normal, close);
            canvas_->highlight(m.left_x - 1, m.left_x + open_w + drawn + close_w, s.y + 14);
        }
    }
    if (w.arrow_up) canvas_->scroll_arrow(m.centre_x, w.up_y, true, true);
    if (w.arrow_down) canvas_->scroll_arrow(m.centre_x, w.down_y, false, true);
}

// ---- Options style -----------------------------------------------------------------------

namespace {

// Values reach the game font through its translating chokepoint, which knows the options screen's
// literals ("Windowed", "Trilinear"): spaces become the font's '_'.
std::string engine_form(std::string_view s) {
    std::string out(s);
    for (char& ch : out) {
        if (ch == ' ') ch = '_';
    }
    return out;
}

}  // namespace

Label MenuView::value_label(const RowView& r) {
    if (r.value.empty()) return {};
    if (r.value_is_key || (r.value.starts_with("ui.") && looks_like_key(r.value))) return canvas_->localize(r.value);
    return Label{engine_form(r.value), false};
}

void MenuView::draw_options(const Controller& c, const Metrics& m) {
    canvas_->centred(m.centre_x, m.header_y, TextSize::Full, Tone::Normal, canvas_->localize(c.title()));

    if (c.dropdown_open()) {
        const DropdownView d = c.dropdown();
        const int count = static_cast<int>(d.items.size());
        const int top = keep_visible(d.top, d.selected, count, m.visible);
        const Window w = compute_window(m, count, top);
        for (const Slot& s : w.slots) {
            const bool sel = s.row == d.selected;
            const Label label = canvas_->localize(d.items[static_cast<std::size_t>(s.row)].label);
            canvas_->bullet(options_bullet_x(), s.y, sel);
            const int drawn = canvas_->fitted(options_label_x(), s.y, TextSize::Full, Tone::Normal, label,
                                              options_right() - options_label_x(), true);
            if (sel) canvas_->highlight(options_bullet_x() - 1, options_label_x() + drawn + 2, s.y + 14);
        }
        if (w.arrow_up) canvas_->scroll_arrow(m.centre_x, w.up_y, true, true);
        if (w.arrow_down) canvas_->scroll_arrow(m.centre_x, w.down_y, false, true);
        return;
    }

    const std::vector<RowView> rows = c.rows();
    // One value column and one text size for the whole page (not per window), so neither jumps while
    // scrolling; the widest value seen on the page and its smallest size are kept for the same reason when a
    // value changes or rows come and go. A new window shape starts over: the room is different.
    const std::string visit = c.title() + "|" + std::to_string(span_.left) + "|" + std::to_string(span_.right);
    if (widths_page_ != visit) {
        widths_page_ = visit;
        value_widths_.clear();
        page_scale_ = kScaleFull;
    }
    ColumnInput in;
    in.label_x = options_label_x();
    in.right = options_right();
    in.gap = kOptValueGap;
    in.min_value_x = kOptValueMin + options_shift();
    in.bar = kOptBarWidth + kOptBarTextGap;
    in.min_scale = canvas_->min_fit_scale();
    in.max_scale = page_scale_;
    in.note_min_scale = canvas_->min_note_scale();
    in.rows.reserve(rows.size());
    for (const RowView& r : rows) {
        ColumnRow cr;
        // A group caption spans the page and takes no part in the columns.
        if (r.def != nullptr && r.def->kind != RowKind::Header) {
            cr.label = canvas_->text_width(canvas_->localize(r.def->label), TextSize::Full);
            const Label value = value_label(r);
            int& seen = value_widths_[r.def->id.empty() ? r.def->label : r.def->id];
            seen = std::max(seen, value.text.empty() ? 0 : canvas_->text_width(value, TextSize::Full));
            for (const std::string& t : r.texts) {
                if (t.empty()) continue;
                RowView other = r;
                other.value = t;
                seen = std::max(seen, canvas_->text_width(value_label(other), TextSize::Full));
            }
            cr.value = seen;
            cr.bar = r.fraction >= 0.0;
        }
        in.rows.push_back(cr);
    }
    const ColumnLayout layout = options_columns(in);
    page_scale_ = layout.scale;
    report_layout(c, rows, in, layout);

    const Window w = compute_window(m, static_cast<int>(rows.size()), c.scroll_top());
    for (const Slot& s : w.slots) {
        const auto i = static_cast<std::size_t>(s.row);
        const RowPlace place = place_row(in, layout, in.rows[i]);
        canvas_->set_fit_scale(place.scale);
        draw_options_row(rows[i], s.y, s.row == c.selection(), place);
    }
    canvas_->set_fit_scale(kScaleFull);
    if (w.arrow_up) canvas_->scroll_arrow(m.centre_x, w.up_y, true, true);
    if (w.arrow_down) canvas_->scroll_arrow(m.centre_x, w.down_y, false, true);

    draw_help(help_lines(c.help()), m);
}

// A page whose text had to shrink, or with a row that had to shrink alone beside the value column (its label
// or value is longer than the column leaves room for), reads as broken: the label or its translation is too long.
// Said once per page as it is drawn (per language: the page title is translated) so a run of every page
// in every language finds them all.
void MenuView::report_layout(const Controller& c, const std::vector<RowView>& rows, const ColumnInput& in,
                             const ColumnLayout& layout) {
    const std::string page = canvas_->localize(c.title()).text;
    std::string off;
    // One column at full size needs the longest label and the widest value side by side.
    int label_w = 0;
    int value_w = 0;
    std::string label_name;
    std::string value_name;
    for (std::size_t i = 0; i < rows.size(); i++) {
        const ColumnRow& r = in.rows[i];
        if (rows[i].def == nullptr || (r.value <= 0 && !r.bar)) continue;
        const std::string name = canvas_->localize(rows[i].def->label).text;
        if (place_row(in, layout, r).scale < layout.scale) {
            if (!off.empty()) off += ", ";
            off += name;
        }
        if (r.label > label_w) {
            label_w = r.label;
            label_name = name;
        }
        const int v = (r.bar ? in.bar : 0) + r.value;
        if (v > value_w) {
            value_w = v;
            value_name = name;
        }
    }
    if (layout.scale >= kScaleFull && layout.fits && off.empty()) return;
    std::string& said = reported_[page];
    const std::string what = std::to_string(layout.scale) + "|" + off;
    if (said == what) return;
    said = what;
    char text[512];
    std::snprintf(text, sizeof(text),
                  "[MENU] options page \"%s\": text at %.2f%s, rows shrunk alone: %s; one column needs %d of %d "
                  "(label \"%s\" %d, value of \"%s\" %d)",
                  page.c_str(), layout.scale / 4096.0, layout.fits ? "" : " and still too long",
                  off.empty() ? "-" : off.c_str(), label_w + in.gap + value_w, in.right - in.label_x,
                  label_name.c_str(), label_w, value_name.c_str(), value_w);
    // Every page fits a 16:9 window in every shipped language; a narrower one (4:3, 16:10, 3:2) has less room
    // and is expected to shrink the longer pages.
    const Span wide = visible_span(kMenuWidth, (16.0f / 9.0f) / (4.0f / 3.0f));
    if (span_.right - span_.left >= wide.right - wide.left) {
        SH_WARN("%s", text);
    } else {
        SH_LOG("%s", text);
    }
}

void MenuView::draw_options_row(const RowView& r, int y, bool selected, const RowPlace& place) {
    const Tone tone = tone_of(r);
    const bool info = r.def != nullptr && r.def->kind == RowKind::Info;
    const Label label = canvas_->localize(r.def != nullptr ? r.def->label : std::string());
    if (r.def != nullptr && r.def->kind == RowKind::Header) {
        // A group caption starts where the bullets do, so it reads as the title of the rows under it.
        canvas_->fitted(options_bullet_x(), y, TextSize::Fit, tone, label,
                        std::max(0, options_right() - options_bullet_x()), true);
        return;
    }
    const Label value = value_label(r);
    const bool bar = r.fraction >= 0.0;
    const bool has_value = bar || !value.text.empty();

    const int bullet_x = options_bullet_x();
    const int label_x = options_label_x();
    if (!info) canvas_->bullet(bullet_x, y, selected && r.enabled);
    // Text that still does not fit pans on every row, selected or not, so nothing is ever cut.
    const int right = options_right();
    const int value_x = place.value_x;
    const int label_max = has_value ? place.label_max : right - label_x;
    const int drawn = canvas_->fitted(label_x, y, TextSize::Fit, tone, label, std::max(0, label_max), true);

    int value_end = value_x;
    if (bar) {
        canvas_->bar(value_x, y, kOptBarNotches, r.fraction);
        value_end = value_x + kOptBarWidth;
        if (!value.text.empty()) {
            const int vx = value_end + kOptBarTextGap;
            value_end = vx + canvas_->fitted(vx, y, TextSize::Fit, tone, value, std::max(0, right - vx), true);
        }
    } else if (!value.text.empty()) {
        value_end = value_x + canvas_->fitted(value_x, y, TextSize::Fit, tone, value, std::max(0, right - value_x), true);
    }

    if (!selected) return;
    if (!info) canvas_->highlight(bullet_x - 1, label_x + drawn + 2, y + 14);
    if (has_value && r.has_arrows && r.enabled) canvas_->value_arrows(value_x, value_end, y);
}

}  // namespace sh::menu
