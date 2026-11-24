// SPDX-License-Identifier: GPL-3.0-or-later
// The player-facing view of an sh::menu::Controller: draws it in the pause menu's look (centred header,
// "[Label]" / "_Label" rows, value column on the right with arrows, scroll arrows, a dropdown list over
// the same area, one help line at the bottom) and turns the pointer position into a hover row.
// Everything is drawn through MenuCanvas, so the text layer underneath can be replaced without touching the view.
#pragma once

#include <map>
#include <string>
#include <vector>
#include <string_view>

#include "engine/menu/menu_layout.h"
#include "engine/menu/menu_model.h"
#include "engine/menu/touch_scroll.h"

namespace sh::menu {

enum class Tone { Normal, Dim, Info, Header, Group };
// Full: the glyphs' own size, whatever the screen mode does to rows. Fit: the size set_fit_scale picked
// for the page's rows (the Options look shrinks a page a little so its labels and values stay whole). Small: a note
// row's (RowDef::small), about two thirds of a row.
enum class TextSize { Row, Help, Full, Fit, Small };
// List: a compact list (rows left, value column right). Pause: the pause menu's look (rows centred under a
// blinking header, no value column, no help line). Options: the game's own Options screen (heading at the
// top, a bullet per row, the gliding gold underline, values in a column, volume-style bars for sliders).
enum class Style { List, Pause, Options };

// A localized string ready for the canvas. A literal (text that is not a key) is drawn as it is.
struct Label {
    std::string text;
    bool verbatim = false;
};

class MenuCanvas {
public:
    virtual ~MenuCanvas() = default;
    // Key -> the active language's text; anything else -> the text as it is. A key that no language
    // layer has comes back as the key itself (so a missing string is visible), as a literal.
    virtual Label localize(std::string_view key_or_literal) = 0;
    // Data text (a value, a Lua string without a key): drawn as it is.
    virtual Label literal(std::string_view text) = 0;
    virtual int text_width(const Label& s, TextSize size) = 0;
    virtual void text(int x, int y, TextSize size, Tone tone, const Label& s) = 0;
    // Draws `s` inside max_w: cut with "..." or, when `pan`, scrolling to its end. Returns the width drawn.
    virtual int fitted(int x, int y, TextSize size, Tone tone, const Label& s, int max_w, bool pan) = 0;
    // `s` as it will be drawn (translated, folded to the font): a literal that can be cut into lines.
    virtual Label as_drawn(const Label& s) { return s; }
    // Q12 size of TextSize::Fit, and the smallest the canvas still draws legibly.
    virtual void set_fit_scale(int /*q12*/) {}
    virtual int min_fit_scale() { return 4096; }
    // The smallest size a single note row (a row without a value) may take to stay whole: the help line's.
    virtual int min_note_scale() { return min_fit_scale(); }
    virtual void centred(int cx, int y, TextSize size, Tone tone, const Label& s) = 0;
    virtual void highlight(int x0, int x1, int y_bottom) = 0;
    virtual void value_arrows(int x_left, int x_right, int y) = 0;
    virtual void scroll_arrow(int cx, int y, bool up, bool enabled) = 0;
    // The options look's row marker; x is its left edge, y the row's label y.
    virtual void bullet(int /*x*/, int /*y*/, bool /*selected*/) {}
    // A bar of `notches` filled to `fraction` (0..1) from x, beside a row at y.
    virtual void bar(int /*x*/, int /*y*/, int /*notches*/, double /*fraction*/) {}
    // A darkening veil under the menu (the menu is drawn over the running game).
    virtual void veil() {}
    // A touch screen: the faint band of the row a finger presses, x0..x1 by y_top..y_bottom.
    virtual void press_band(int /*x0*/, int /*x1*/, int /*y_top*/, int /*y_bottom*/) {}
    // A touch screen: the bar beside a list that scrolls, x by y0..y1; the thumb is the shown share of the rows
    // (`first` and `shown` in rows of `total`).
    virtual void scroll_bar(int /*x*/, int /*y0*/, int /*y1*/, int /*first*/, int /*shown*/, int /*total*/) {}
    // Geometry for the current screen mode (row pitch and window follow the text scale).
    virtual Metrics metrics() = 0;
};

// A MenuCanvas on the game's menu text layer (Nm_*), the look of the pause menu.
MenuCanvas& nm_canvas();

// A 2D screen drawn this frame asks the present path for the whole window instead of the 4:3
// pillarbox (the 320-wide menu space stays centred, the sides show what is drawn there, black by
// default). Call every frame the screen draws; returns how many 4:3 frames wide the window is (1 when
// the screen stays 4:3: a 3D frame under it, native render, a window 4:3 or narrower).
float claim_full_width();

class MenuView {
public:
    explicit MenuView(MenuCanvas& canvas) : canvas_(&canvas) {}

    // Sets the controller's window to what the canvas can show, and on a touch screen moves the list with the finger
    // (the controller's top follows); call once a frame, before update and draw.
    void fit(Controller& c);
    // The visible-row index under the pointer (mouse in the canvas' space), or -1.
    int hover_row(const Controller& c, int mouse_x, int mouse_y) const;
    void draw(const Controller& c);

    void set_style(Style s) { style_ = s; }
    // Pause style: the header blinks; the host calls this each frame.
    void set_header_visible(bool v) { header_visible_ = v; }
    // Options style over the game (the debug menu): a veil goes under it.
    void set_veil(bool v) { veil_ = v; }
    // Which Yes / No answer of the confirm question is under the pointer (0 Yes, 1 No), or -1.
    int hover_answer(const Controller& c, int mouse_x, int mouse_y) const;
    // Where the pointer is along the bar of visible row `hover_row` (0..1, Options look, last draw); < 0 off it.
    double bar_share(int hover_row, int mouse_x, int mouse_y) const;
    // The pointer's part of `mi` (the hover row and answer; on a touch screen the finger's taps, its bar presses
    // and drags, and no hover while it only moves), from the pointer as nm::read_input gives it.
    void pointer_input(const Controller& c, bool pointer, int x, int y, MenuInput& mi) const;

private:
    Metrics metrics() const;
    void draw_confirm(const Controller& c);
    void draw_marked(int cx, int y, Tone tone, const Label& label, bool selected, TextSize size);
    void draw_rows(const Controller& c, const Metrics& m);
    void draw_dropdown(const Controller& c, const Metrics& m);
    void draw_row(const RowView& r, int y, bool selected, const Metrics& m);
    void draw_options(const Controller& c, const Metrics& m);
    void draw_options_row(const RowView& r, int y, bool selected, const RowPlace& place);
    void report_layout(const Controller& c, const std::vector<RowView>& rows, const ColumnInput& in,
                       const ColumnLayout& layout);
    Label value_label(const RowView& r);
    int options_right() const;
    int options_shift() const;  // the rows' move left on a wide window (0 at 4:3)
    int options_bullet_x() const;
    int options_label_x() const;
    // The help of the selected row, wrapped to the help area's width (the Options look uses the whole span).
    std::vector<std::string> help_lines(const std::string& help);
    int help_pitch() const;
    void draw_help(const std::vector<std::string>& lines, const Metrics& m);
    // A touch screen's rows: taller, tapped anywhere across the screen, a pressed band and a scroll bar.
    bool touch_rows() const;
    void follow_finger(Controller& c);
    // The rows of a list drawn from `top`, moved up by the finger's part-row offset: a row half out of the window
    // is left out.
    Window list_window(const Metrics& m, int count, int top, const std::vector<int>& heights = {}) const;
    void draw_touch_aids(const Window& w, int count, int top, const Metrics& m, const std::vector<int>& heights = {});

    // Rows of their own heights (RowDef::small notes, in the List and Options looks).
    bool row_heights_apply() const { return style_ == Style::List || style_ == Style::Options; }
    int small_pitch(const Metrics& m) const;
    // The heights of `rows`; empty when every row is one pitch tall (no note on the page).
    std::vector<int> row_heights(const std::vector<RowView>& rows, const Metrics& m) const;
    int row_height(const RowView& r, const Metrics& m) const;
    // Where a note's text starts and how wide it may be.
    int note_x(const Metrics& m) const;
    int note_width(const Metrics& m) const;
    // The label of a row as drawn: translated, its RowDef::args put in, a path's last component for Cut::Path.
    Label row_label(const RowDef& d) const;
    // A note's label and value as one line, drawn as it is.
    Label note_text(const RowView& r) const;
    std::vector<std::string> note_lines(const RowView& r, int width) const;
    void draw_note(const RowView& r, const Slot& s, const Metrics& m);
    // Draws `s` inside max_w the way `cut` says; returns the width drawn.
    int draw_cut(int x, int y, TextSize size, Tone tone, const Label& s, int max_w, Cut cut);

    static constexpr int kMenuWidth = 320;  // the menu space's width at 4:3

    MenuCanvas* canvas_;
    Span span_{0, kMenuWidth};  // the menu space's x range on screen, from the last draw
    Style style_ = Style::List;
    bool header_visible_ = true;
    bool veil_ = false;
    int help_lines_ = 0;        // most help lines of any row on the page (from fit): the help area's height
    std::string widths_page_;   // the page visit (title and window shape) the memory below belongs to
    // Widest value seen per row of the page, so the layout does not jump back and forth as values change.
    std::map<std::string, int> value_widths_;
    int page_scale_ = 4096;  // Q12: the page's text size so far this visit (it only shrinks while the page is open)
    std::map<std::string, std::string> reported_;  // page -> the layout fault last logged for it
    struct BarPlace {
        int slot = 0;  // visible-row index (as hover_row counts)
        int x0 = 0, x1 = 0, y = 0;
    };
    std::vector<BarPlace> bars_;  // the bars of the last draw

    // The touch screen's list (follow_finger): which list it is, its scroll, the part of a row it shows past the
    // controller's top (menu lines), the clock of the last frame, and where the finger landed.
    TouchScroll scroll_;
    std::string scroll_list_;
    int shift_ = 0;
    double scroll_clock_ = -1.0;
    bool finger_down_ = false;
    int press_bar_slot_ = -1;  // the finger landed on this visible row's bar: a sideways drag sets it
};

// Whether `s` is a text key (dotted lower-case identifier like ui.menu.on) rather than a literal.
bool looks_like_key(std::string_view s);

}  // namespace sh::menu
