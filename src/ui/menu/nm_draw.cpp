// SPDX-License-Identifier: GPL-3.0-or-later
//
// The menus' draw layer (nm_draw.h): text through the game's text drawer, widgets, rules, bars and backgrounds as
// draw2d commands in the menu list's 2D layers. Everything the game owns goes through NmDrawHost
// (ui/menu/nm_draw_host.h).
//
// In a right-to-left language the whole layer is mirrored about the screen's middle: a menu laid out for x from
// the left is drawn from the right (labels end where they began, values and arrows change sides), and the mouse
// and the Left/Right keys are mirrored back, so the screens keep one layout.
#include "ui/menu/nm_draw.h"

#include "engine/menu/text_fit.h"
#include "engine/text/game_text.h"
#include "engine/world/fixed_trig.h"
#include "i18n/game_font.h"
#include "i18n/localization.h"
#include "input/mouse_pointer.h"
#include "ui/menu/nm_draw_host.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>


namespace {

namespace d2 = sh::draw2d;
using sh::ui::nm_draw_host;

// 2D layers of the menu list (the options screen's own layer numbers in the comments).
constexpr int kLayerArrow = 10;  // LAYER_40
constexpr int kLayerShade = 9;   // LAYER_36
constexpr int kLayerText = 8;
constexpr int kLayerRule = 6;    // LAYER_24
constexpr int kLayerBack = 15;
constexpr int kLayerScene = 12;  // between the backing and the arrows: the menu draws over a preview model

// A string longer than the text drawer's per-call budget is drawn in chunks.
constexpr int kTextBudget = 99;
// String colour ids of the game's table.
constexpr int kGold = 0;
constexpr int kWhite = 7;

constexpr int kScreenWidth = 320;
constexpr int kBlendAdd = 1;  // the page's blend: back + front

bool mirrored() { return sh::i18n::right_to_left(); }

std::int16_t nm_px(int x) { return static_cast<std::int16_t>((mirrored() ? kScreenWidth - x : x) - 160); }

// The width a string takes at the text drawer's current size, as the mode in force will draw it.
int drawn_width(const char* s) {
    const char* text = sh::i18n::draw_mode() == sh::i18n::DrawMode::Translate ? sh::i18n::menu_text(s) : s;
    const std::int32_t scale = std::max<std::int32_t>(1, sh::text::game_text().scale());
    return static_cast<int>((static_cast<std::int64_t>(sh::i18n::text_width(text)) * scale) >> 12);
}

std::int16_t nm_py(int y) {
    return static_cast<std::int16_t>(nm_draw_host().interlaced() ? (y - 112) * 2 : (y - 112));
}

}  // namespace

namespace sh::ui::nm {

void text(int x, int y, int color, const char* s) {
    sh::text::GameText& drawer = sh::text::game_text();
    sh::ui::NmDrawHost& host = nm_draw_host();
    drawer.set_color(color);
    if (mirrored()) {
        x = kScreenWidth - x - drawn_width(s);
    }

    // A row is 16 lines of the layer on both screens: the 448-line one has glyphs twice as tall for it.
    if (host.interlaced()) {
        host.set_hi_res_glyphs(true);
    }
    drawer.set_position(x, y);
    // set_position reads x == -1 as "keep x", and a panning line passes through -1: it drew where the last
    // string ended.
    if (x == sh::text::kKeep) {
        sh::text::TextCursor& cursor = drawer.cursor();
        cursor.x = static_cast<std::int16_t>(x - sh::text::kScreenOriginX);
        cursor.line_start_x = cursor.x;
    }

    drawer.set_layer(kLayerText);
    drawer.draw(s, kTextBudget);

    host.set_hi_res_glyphs(false);
    drawer.reset_layer();
}

// A scaled glyph is a quad, and on a world frame (the in-game pause still is one) every quad counts as world
// geometry and is filtered with the world. Naming the strings layer exempts it; safe because a frame that shows
// a menu over the frozen capture puts no world geometry there. The value persists, so the caller clears it.
void keep_text_unfiltered(bool on) { nm_draw_host().set_ui_layer(on ? kLayerText : -1); }

void text_verbatim(int x, int y, int color, const char* s) {
    const sh::i18n::ScopedDrawMode verbatim(sh::i18n::DrawMode::Verbatim);
    text(x, y, color, s);
}

int text_width(const char* s) { return sh::i18n::text_width(sh::i18n::menu_text(s)); }

void text_centred(int cx, int y, int color, const char* s) { text(cx - (text_width(s) / 2), y, color, s); }

void text_right(int right_x, int y, int color, const char* s) { text(right_x - text_width(s), y, color, s); }

std::string font_text(std::string_view utf8, std::size_t max_bytes) {
    return sh::menu::font_spelling(utf8, max_bytes, [](char32_t cp) { return sh::i18n::GameFont::get().has(cp); });
}

}  // namespace sh::ui::nm

namespace {

// Wall-clock ms: the pan has to read at one speed whatever the frame rate.
constexpr Uint32 kMarqueeHoldMs = 1200;
constexpr Uint32 kMarqueePxPerS = 36;
// The text drawer stops at its budget of characters.
constexpr std::size_t kMarqueeChunk = 96;

// Pans on screen together share one cycle: each runs to its end and holds there until the longest has arrived,
// then all start over at once. The longest is last frame's, so a text drawn early in a frame still knows it.
struct {
    Uint32 epoch = 0;
    Uint32 last = 0;
    Uint32 move = 0;
    Uint32 move_prev = 0;
    std::int32_t tick = 0;
} s_marquee;

void marquee_restart() { s_marquee.epoch = SDL_GetTicks(); }

int marquee_offset(int overflow) {
    const Uint32 now = SDL_GetTicks();
    const Uint32 move = (static_cast<Uint32>(overflow) * 1000u) / kMarqueePxPerS;
    const std::int32_t tick = nm_draw_host().tick();

    if (tick != s_marquee.tick) {
        s_marquee.tick = tick;
        s_marquee.move_prev = s_marquee.move;
        s_marquee.move = 0;
    }
    if (move > s_marquee.move) {
        s_marquee.move = move;
    }
    const Uint32 group_move = (s_marquee.move_prev > s_marquee.move) ? s_marquee.move_prev : s_marquee.move;

    // A screen that was closed starts its pans over when it reopens.
    if (now - s_marquee.last > 250u) {
        s_marquee.epoch = now;
    }
    s_marquee.last = now;

    if (now - s_marquee.epoch >= (2u * kMarqueeHoldMs) + group_move) {
        s_marquee.epoch = now;
    }

    Uint32 t = now - s_marquee.epoch;
    if (t < kMarqueeHoldMs) {
        return 0;
    }
    t -= kMarqueeHoldMs;
    if (t < move) {
        return static_cast<int>((t * static_cast<Uint32>(overflow)) / move);
    }
    return overflow;
}

// The underline glides to a new row the way the options menus move theirs: 8 frames of a sine ease. A screen
// calls snap_highlight when the mouse picked the row, which the options menus also leave unanimated.
constexpr int kGlideFrames = 8;

struct {
    int from_x0, from_x1, from_y;
    int to_x0, to_x1, to_y;
    int timer;
    int snap;
    std::int32_t tick;
    int drawn;
} s_glide;

int glide_lerp(int from, int to, std::int32_t alpha) { return from + (((to - from) * alpha) >> 12); }

void quad(d2::Point q[4], int x0, int y0, int x1, int y1) {
    q[0] = {nm_px(x0), nm_py(y0)};
    q[1] = {nm_px(x0), nm_py(y1)};
    q[2] = {nm_px(x1), nm_py(y0)};
    q[3] = {nm_px(x1), nm_py(y1)};
}

}  // namespace

namespace sh::ui::nm {

int text_fitted(int x, int y, int color, const char* s, int maxW, bool pan) {
    char chunk[kMarqueeChunk + 1];
    std::size_t left = std::strlen(s);
    // The font measures unscaled glyphs; a scaled string shrinks about its own start, so every width
    // here (the pan, the clip, the next chunk's x) is taken at the drawer's current size.
    const std::int32_t sc = std::max<std::int32_t>(1, sh::text::game_text().scale());
    const auto width = [sc](const char* t) { return static_cast<int>((static_cast<std::int64_t>(sh::i18n::text_width(t)) * sc) >> 12); };
    const int w = width(s);

    if (w > maxW && !pan) {
        const std::string fit =
            sh::menu::fit_with_ellipsis(s, static_cast<int>((static_cast<std::int64_t>(maxW) << 12) / sc),
                                        sh::i18n::text_width)
                .text;
        text_verbatim(x, y, color, fit.c_str());
        return std::min(maxW, width(fit.c_str()));
    }

    if (w <= maxW && left <= kMarqueeChunk) {
        text_verbatim(x, y, color, s);
        return w;
    }

    int cx = x;
    if (w > maxW) {
        cx -= marquee_offset(w - maxW);
        if (mirrored()) {
            sh::text::game_text().set_clip_x(kScreenWidth - x - maxW, kScreenWidth - x);
        } else {
            sh::text::game_text().set_clip_x(x, x + maxW);
        }
    }
    while (left > 0) {
        std::size_t n = left;
        if (n > kMarqueeChunk) {
            n = kMarqueeChunk;
            while (n > kMarqueeChunk / 2 && s[n - 1] != '_') {
                n--;
            }
            if (s[n - 1] != '_') {
                n = kMarqueeChunk;
                // Not inside a character of several bytes.
                while (n > 1 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) {
                    n--;
                }
            }
        }
        std::memcpy(chunk, s, n);
        chunk[n] = '\0';
        if (cx < x + maxW) {
            text_verbatim(cx, y, color, chunk);
        }
        cx += width(chunk);
        s += n;
        left -= n;
    }
    sh::text::game_text().set_clip_x(0, 0);
    return (w > maxW) ? maxW : w;
}

void scroll_arrow(int cx, int y, bool up, bool enabled) {
    const int dir = up ? -1 : 1;
    int off = 0;
    if (enabled) {
        const int phase = nm_draw_host().frame_counter() & 0x3F;
        off = ((phase < 32) ? phase : (63 - phase)) * 3 / 32;
    }

    d2::Point tri[3];
    tri[0] = {nm_px(cx), nm_py(y + (dir * (4 + off)))};
    tri[1] = {nm_px(cx - 8), nm_py(y - (dir * (4 - off)))};
    tri[2] = {nm_px(cx + 8), tri[1].y};

    if (enabled) {
        nm_draw_host().widget_arrow(tri);
        return;
    }

    // Twice the dark grey string colour: text colours modulate the near-white font at 128 = 1.0, this
    // untextured polygon shows its colour as is.
    d2::Polygon grey;
    grey.count = 3;
    grey.gouraud = true;
    grey.color[0] = grey.color[1] = grey.color[2] = {64, 64, 64};
    grey.xy[0] = tri[0];
    grey.xy[1] = tri[1];
    grey.xy[2] = tri[2];
    nm_draw_host().submit(kLayerArrow, grey);
}

void snap_highlight() { s_glide.snap = 1; }

namespace {

// A flat grey box: opaque, or added to what is under it (a lighter band). The shade layer's own page subtracts
// (it darkens under the underline), so an added box sets its page itself, drawn just before it.
void box(int x0, int y0, int x1, int y1, std::uint8_t grey, bool lighten) {
    d2::Polygon poly;
    poly.count = 4;
    poly.shade.translucent = lighten;
    poly.color[0] = poly.color[1] = poly.color[2] = poly.color[3] = {grey, grey, grey};
    quad(poly.xy, x0, y0, x1, y1);
    nm_draw_host().submit(kLayerShade, poly);
    if (lighten) {
        nm_draw_host().texture_page_blend(kLayerShade, kBlendAdd);
    } else {
        nm_draw_host().reset_texture_page(kLayerShade);
    }
}

}  // namespace

// Faint: the row stays readable and the gold underline stays the selection's mark.
void press_band(int x0, int x1, int y_top, int y_bottom) { box(x0, y_top, x1, y_bottom, 22, true); }

void scroll_bar(int x, int y0, int y1, int first, int shown, int total) {
    if (total <= 0 || y1 <= y0) return;
    constexpr int kWidth = 3;
    const int span = y1 - y0;
    const int thumb = std::max(8, span * std::min(shown, total) / total);
    const int travel = span - thumb;
    const int rest = std::max(1, total - shown);
    const int at = y0 + travel * std::clamp(first, 0, rest) / rest;
    // A layer draws what was submitted last first: the thumb goes in before the track it lies on.
    box(x, at, x + kWidth, at + thumb, 170, false);
    box(x, y0, x + kWidth, y1, 48, false);
}

void highlight(int x0, int x1, int yBottom) {
    sh::ui::NmDrawHost& host = nm_draw_host();
    const std::int32_t tick = host.tick();

    // Not drawn last frame: a screen just opened or the underline was hidden, so there is no previous position
    // worth gliding from.
    if (!s_glide.drawn || s_glide.snap || (tick != s_glide.tick && tick != s_glide.tick + 1)) {
        s_glide.from_x0 = s_glide.to_x0 = x0;
        s_glide.from_x1 = s_glide.to_x1 = x1;
        s_glide.from_y = s_glide.to_y = yBottom;
        s_glide.timer = kGlideFrames;
        s_glide.snap = 0;
        s_glide.drawn = 1;
        marquee_restart();
    } else if (x0 != s_glide.to_x0 || x1 != s_glide.to_x1 || yBottom != s_glide.to_y) {
        const std::int32_t alpha = static_cast<std::int16_t>(sh::world::fixed_sin(s_glide.timer << 7));
        s_glide.from_x0 = glide_lerp(s_glide.from_x0, s_glide.to_x0, alpha);
        s_glide.from_x1 = glide_lerp(s_glide.from_x1, s_glide.to_x1, alpha);
        s_glide.from_y = glide_lerp(s_glide.from_y, s_glide.to_y, alpha);
        s_glide.to_x0 = x0;
        s_glide.to_x1 = x1;
        s_glide.to_y = yBottom;
        s_glide.timer = 0;
        marquee_restart();
    }

    if (tick != s_glide.tick) {
        s_glide.tick = tick;
        if (s_glide.timer < kGlideFrames) {
            s_glide.timer++;
        }
    }

    const std::int32_t alpha = static_cast<std::int16_t>(sh::world::fixed_sin(s_glide.timer << 7));
    x0 = glide_lerp(s_glide.from_x0, s_glide.to_x0, alpha);
    x1 = glide_lerp(s_glide.from_x1, s_glide.to_x1, alpha);
    yBottom = glide_lerp(s_glide.from_y, s_glide.to_y, alpha);

    const d2::Point a = {nm_px(x0), nm_py(yBottom)};
    const d2::Point b = {nm_px(x1), a.y};

    if (!host.interlaced()) {
        host.widget_underline(a, b, true);
        return;
    }

    // The widget's shadow is a fixed 16 pixels, half a tall row on the 448-line screen; same quad, colours and
    // blend, at the doubled height.
    host.widget_underline(a, b, false);
    const std::int16_t top = nm_py(yBottom - 16);
    const std::uint8_t shade = 96;  // 0.375

    d2::Polygon poly;
    poly.count = 4;
    poly.gouraud = true;
    poly.shade.translucent = true;
    poly.color[0] = poly.color[2] = {0, 0, 0};
    poly.color[1] = poly.color[3] = {shade, shade, shade};
    poly.xy[0] = {a.x, top};
    poly.xy[1] = {a.x, a.y};
    poly.xy[2] = {b.x, top};
    poly.xy[3] = {b.x, b.y};
    host.submit(kLayerShade, poly);
    host.reset_texture_page(kLayerShade);
}

void bullet(int x, int y, bool selected) {
    d2::Point front[4];
    d2::Point border[4];
    quad(front, x, y + 1, x + 12, y + 13);
    quad(border, x - 1, y, x + 13, y + 14);
    nm_draw_host().widget_bullet(front, false, !selected);
    nm_draw_host().widget_bullet(border, true, !selected);
}

void rule(int x0, int x1, int y) {
    d2::Line line;
    line.xy[0] = {nm_px(x0), nm_py(y)};
    line.xy[1] = {nm_px(x1), nm_py(y)};
    line.color[0] = {128, 128, 128};
    nm_draw_host().submit(kLayerRule, line);
}

void value_arrows(int xLeft, int xRight, int y) {
    d2::Point tri[3];
    tri[0] = {nm_px(xLeft - 14), nm_py(y + 6)};
    tri[1] = {nm_px(xLeft - 6), nm_py(y - 2)};
    tri[2] = {tri[1].x, nm_py(y + 14)};
    nm_draw_host().widget_arrow(tri);

    tri[0].x = nm_px(xRight + 14);
    tri[1].x = nm_px(xRight + 6);
    tri[2].x = tri[1].x;
    nm_draw_host().widget_arrow(tri);
}

// The look of the options screen's volume bars (gamedata/menu/options.json, volume_bar): 6 px pitch, 5 x 13
// notches, a brighter inner part, the notch holding the level part-lit.
void bar(int x, int y, int notches, int fill) {
    const int lit = 160;
    const int unlit = 64;
    const int inner = 64;
    const int level = (fill < 0 ? 0 : fill > 4096 ? 4096 : fill) * notches;
    const int full = level / 4096;
    const int part = unlit + ((lit - unlit) * (level % 4096)) / 4096;

    for (int i = 0; i < notches; i++) {
        for (int j = 1; j >= 0; j--) {
            const int base = i < full ? lit : i > full ? unlit : part;
            int c = base + (inner * j);
            if (c > 255) {
                c = 255;
            }
            const std::uint8_t cc = static_cast<std::uint8_t>(c);

            d2::Polygon q;
            q.count = 4;
            q.color[0] = {cc, cc, cc};
            const int left = x + (i * 6) + j;
            const int right = x + (i * 6) - j + 5;
            const int top = y + 1 + j;
            const int bottom = y + 1 + 13 - j;
            q.xy[0] = {nm_px(left), nm_py(top)};
            q.xy[1] = {nm_px(right), nm_py(top)};
            q.xy[2] = {nm_px(left), nm_py(bottom)};
            q.xy[3] = {nm_px(right), nm_py(bottom)};
            nm_draw_host().submit(kLayerRule, q);
        }
    }
}

void dim_background() {
    d2::Polygon q;
    q.count = 4;
    q.shade.translucent = true;
    q.color[0] = {72, 72, 72};
    q.xy[0] = {-480, -240};
    q.xy[1] = {480, -240};
    q.xy[2] = {-480, 240};
    q.xy[3] = {480, 240};
    nm_draw_host().submit(kLayerBack, q);
    nm_draw_host().reset_texture_page(kLayerBack);
}

void black_background() {
    // Wider than 4:3 so a Hor+ widened title screen is covered too.
    d2::Fill fill;
    fill.at = {-480, -240};
    fill.w = 960;
    fill.h = 480;
    nm_draw_host().submit(kLayerBack, fill);
}

FramePoint to_frame(int x, int y) {
    return {static_cast<float>(nm_px(x)), static_cast<float>(nm_py(y)), nm_draw_host().interlaced() ? 2.0f : 1.0f};
}

void link_scene_item(int item) {
    if (item >= 0) {
        nm_draw_host().link_scene(kLayerScene, static_cast<std::uint32_t>(item) + 1u);
    }
}

bool pointer_position(int* x, int* y) {
    int mx = 0;
    int my = 0;
    if (!sh::input::mouse_pointer().menu_position(&mx, &my)) return false;
    *x = mirrored() ? kScreenWidth - mx : mx;
    // UiPos reports the 448-line authoring space (-112..336) on the title.
    *y = nm_draw_host().interlaced() ? (my + 112) / 2 : my;
    return true;
}

Input read_input() {
    int mx = 0;
    int my = 0;
    // While the cursor is live the mouse reports its own clicks and wheel below, so its bound pad bits would make
    // one click act twice. With the cursor off those fields stay 0 and the binds are the only path.
    const int mouse_ok = sh::input::mouse_pointer().menu_position(&mx, &my);
    const std::uint32_t mouse_bits = mouse_ok ? sh::input::mouse_pointer().bound_pad_bits() : 0;
    const sh::ui::NmPad pad = nm_draw_host().pad(mouse_bits);

    Input in;
    in.up = pad.up;
    in.down = pad.down;
    // A mirrored menu moves the way the key points: its bars fill and its values step towards the left.
    in.left = mirrored() ? pad.right : pad.left;
    in.right = mirrored() ? pad.left : pad.right;
    in.mirrored = mirrored();
    in.confirm = pad.confirm;
    in.cancel = pad.cancel;

    if (mouse_ok) {
        in.wheel = sh::input::mouse_pointer().wheel_notches();
        in.pointer = pointer_position(&in.pointer_x, &in.pointer_y);
    }
    in.pointer_moved = sh::input::mouse_pointer().moved() != 0;
    in.click = sh::input::mouse_pointer().left_clicked() != 0;
    in.right_click = sh::input::mouse_pointer().right_clicked() != 0;
    return in;
}

void yes_no(const sh::menu::YesNoPrompt& prompt, const char* question, int cy) {
    if (!prompt.is_open()) {
        return;
    }

    struct Answer {
        const char* label;
        int centre_x;
        bool chosen;
    };
    const Answer answers[] = {{sh::i18n::ui_menu_text("ui.dialog.yes"), 120, prompt.on_yes()},
                              {sh::i18n::ui_menu_text("ui.dialog.no"), 200, !prompt.on_yes()}};
    const int answers_y = cy + 24;

    text_centred(kScreenWidth / 2, cy, kWhite, question);
    for (const Answer& a : answers) {
        const int w = text_width(a.label);
        const int x = a.centre_x - (w / 2);
        text(x, answers_y, a.chosen ? kGold : kWhite, a.label);
        if (a.chosen) {
            highlight(x - 4, x + w + 4, answers_y + 14);
        }
    }
}

}  // namespace sh::ui::nm
