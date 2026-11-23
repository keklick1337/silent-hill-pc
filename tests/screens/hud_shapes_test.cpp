// SPDX-License-Identifier: GPL-3.0-or-later
// The game-free parts of the engine's own screens in src/ui: the crosshair's shapes and when it shows
// (ui/hud/crosshair.h), the title style a disc can show (ui/screens/title_style.h), the frames of the boot
// warning (ui/screens/boot_warning.h) and the pause menu's save rule (ui/pause/pause_menu.h).

#include "ui/hud/crosshair.h"
#include "ui/pause/pause_menu.h"
#include "ui/screens/boot_warning.h"
#include "ui/screens/title_style.h"

#include "framework/sh_test.h"

#include <string>
#include <vector>

namespace {

using namespace sh::ui;

bool quad_is(const CrosshairQuad& q, int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3) {
    return q.x[0] == x0 && q.y[0] == y0 && q.x[1] == x1 && q.y[1] == y1 && q.x[2] == x2 && q.y[2] == y2 &&
           q.x[3] == x3 && q.y[3] == y3;
}

// An axis-aligned bar: corners top left, top right, bottom left, bottom right.
bool bar_is(const CrosshairQuad& q, int left, int top, int right, int bottom) {
    return quad_is(q, left, top, right, top, left, bottom, right, bottom);
}

// The darkness of every frame of a warning ("-" for a frame without darkening), `skip_at` the frame a skip
// is held on (-1: never).
std::string frames_of(WarningSequence w, int skip_at = -1) {
    std::string out;
    for (int frame = 0; !w.done() && frame < 1000; frame++) {
        if (!out.empty()) {
            out += ' ';
        }
        out += w.darkened() ? std::to_string(w.darkness()) : "-";
        if (w.final_frame()) {
            out += '!';
        }
        w.advance(frame == skip_at);
    }
    return out;
}

}  // namespace

SH_TEST(crosshair_shapes_as_designed) {
    const CrosshairShape cross(0, 100.0f, 0);
    SH_CHECK(cross.count() == 2 && bar_is(cross.quad(0), -6, -1, 6, 1) && bar_is(cross.quad(1), -1, -6, 1, 6));

    const CrosshairShape dot(1, 100.0f, 0);
    SH_CHECK(dot.count() == 1 && bar_is(dot.quad(0), -2, -2, 2, 2));

    // Dashes: left, right, up, down.
    const CrosshairShape dashes(3, 100.0f, 0);
    SH_CHECK(dashes.count() == 4 && bar_is(dashes.quad(0), -7, -1, -3, 1) && bar_is(dashes.quad(1), 3, -1, 7, 1) &&
             bar_is(dashes.quad(2), -1, -7, 1, -3) && bar_is(dashes.quad(3), -1, 3, 1, 7));

    // The circle: eight pieces of a ring, each from one corner of the octagon to the next, outer edge then inner.
    const CrosshairShape circle(2, 100.0f, 0);
    SH_CHECK(circle.count() == 8 && CrosshairShape::kMaxQuads == 8);
    SH_CHECK(quad_is(circle.quad(0), 6, 0, 4, 4, 4, 0, 3, 3));
    SH_CHECK(quad_is(circle.quad(1), 4, 4, 0, 6, 3, 3, 0, 4));
    SH_CHECK(quad_is(circle.quad(2), 0, 6, -4, 4, 0, 4, -3, 3));
    SH_CHECK(quad_is(circle.quad(3), -4, 4, -6, 0, -3, 3, -4, 0));
    SH_CHECK(quad_is(circle.quad(4), -6, 0, -4, -4, -4, 0, -3, -3));
    SH_CHECK(quad_is(circle.quad(5), -4, -4, 0, -6, -3, -3, 0, -4));
    SH_CHECK(quad_is(circle.quad(6), 0, -6, 4, -4, 0, -4, 3, -3));
    SH_CHECK(quad_is(circle.quad(7), 4, -4, 6, 0, 3, -3, 4, 0));

    // A style the config does not know is the cross.
    for (const int style : {4, 7, -1, 100}) {
        const CrosshairShape other(style, 100.0f, 0);
        SH_CHECK(other.count() == 2 && bar_is(other.quad(0), -6, -1, 6, 1));
    }
}

SH_TEST(crosshair_sizes) {
    // Halves round away from the middle; a bar a line thick stays a line thick.
    const CrosshairShape half(0, 50.0f, 0);
    SH_CHECK(bar_is(half.quad(0), -3, -1, 3, 1) && bar_is(half.quad(1), -1, -3, 1, 3));
    const CrosshairShape quarter(0, 25.0f, 0);
    SH_CHECK(bar_is(quarter.quad(0), -2, -1, 2, 1) && bar_is(quarter.quad(1), -1, -2, 1, 2));
    const CrosshairShape large(0, 125.0f, 0);
    SH_CHECK(bar_is(large.quad(0), -8, -1, 8, 1));
    SH_CHECK(bar_is(CrosshairShape(1, 125.0f, 0).quad(0), -3, -3, 3, 3));
    SH_CHECK(bar_is(CrosshairShape(3, 60.0f, 0).quad(0), -4, -1, -2, 1));
    SH_CHECK(quad_is(CrosshairShape(2, 125.0f, 0).quad(0), 8, 0, 5, 5, 5, 0, 4, 4));
    SH_CHECK(quad_is(CrosshairShape(2, 40.0f, 0).quad(1), 2, 2, 0, 2, 1, 1, 0, 2));

    // The size is a whole percentage between 25 and 125.
    SH_CHECK(bar_is(CrosshairShape(0, 3.0f, 0).quad(0), -2, -1, 2, 1));
    SH_CHECK(bar_is(CrosshairShape(0, 900.0f, 0).quad(0), -8, -1, 8, 1));
    SH_CHECK(bar_is(CrosshairShape(0, 99.6f, 0).quad(0), -6, -1, 6, 1));
    SH_CHECK(bar_is(CrosshairShape(0, 49.5f, 0).quad(0), -3, -1, 3, 1));
}

SH_TEST(crosshair_drop_moves_the_whole_mark) {
    const CrosshairShape cross(0, 100.0f, 4);
    SH_CHECK(bar_is(cross.quad(0), -6, 3, 6, 5) && bar_is(cross.quad(1), -1, -2, 1, 10));
    // The drop is in lines of the screen: it does not shrink with the mark.
    SH_CHECK(bar_is(CrosshairShape(0, 50.0f, 4).quad(0), -3, 3, 3, 5));
    SH_CHECK(quad_is(CrosshairShape(2, 100.0f, 4).quad(6), 0, -2, 4, 0, 0, 0, 3, 1));
}

SH_TEST(crosshair_shows_when_it_should) {
    CrosshairView v;
    SH_CHECK(!crosshair_visible(v));
    v.enabled = v.aimed_camera = v.in_play = true;
    SH_CHECK(crosshair_visible(v));  // shown whenever it is on: the weapon need not be raised
    v.only_while_aiming = true;
    SH_CHECK(!crosshair_visible(v));
    v.aiming = true;
    SH_CHECK(crosshair_visible(v));
    v.hide_without_gun = true;
    SH_CHECK(!crosshair_visible(v));
    v.gun_in_hand = true;
    SH_CHECK(crosshair_visible(v));
    v.in_play = false;  // a cutscene, a menu, the map
    SH_CHECK(!crosshair_visible(v));
    v.in_play = true;
    v.aimed_camera = false;  // the classic camera
    SH_CHECK(!crosshair_visible(v));
    v.aimed_camera = true;
    v.enabled = false;
    SH_CHECK(!crosshair_visible(v));
}

SH_TEST(title_style_shown_and_named) {
    SH_CHECK(shown_title_style(kTitleStyleJapanese) == kTitleStyleJapanese);
    SH_CHECK(shown_title_style(kTitleStyleUs) == kTitleStyleUs && shown_title_style(kTitleStyleAuto) == kTitleStyleUs);
    SH_CHECK(shown_title_style(3) == kTitleStyleUs && shown_title_style(-7) == kTitleStyleUs);

    SH_CHECK(std::string(title_style_name(kTitleStyleUs)) == "us");
    SH_CHECK(std::string(title_style_name(kTitleStyleJapanese)) == "japanese");
    SH_CHECK(std::string(title_style_name(kTitleStyleAuto)) == "auto" && std::string(title_style_name(9)) == "auto");
}

SH_TEST(boot_warning_frames) {
    // Step 100, held 2 frames, 1 black frame: in from black, the bare picture, out to black, black, the last.
    SH_CHECK(frames_of(WarningSequence(100, 2, 1)) == "255 155 55 - - 0 100 200 255 255!");
    // The shipped timing: 32 frames in, 180 held, 32 out, 3 black and the last.
    const std::string shipped = frames_of(WarningSequence(8, 180, 3));
    SH_CHECK(shipped.substr(0, 11) == "255 247 239" && shipped.substr(shipped.size() - 20) == "248 255 255 255 255!");
    int held = 0;
    for (const char c : shipped) {
        held += c == '-' ? 1 : 0;
    }
    SH_CHECK(held == 180);

    // No hold, no black frames.
    SH_CHECK(frames_of(WarningSequence(128, 0, 0)) == "255 127 0 128 255!");
    // A step that lands on 0 shows the bare picture on that fade frame too.
    SH_CHECK(frames_of(WarningSequence(255, 1, 0)) == "255 0 - 0 255!");
}

SH_TEST(boot_warning_skip) {
    // Held on the first frame, all black still: no picture is shown at all.
    SH_CHECK(frames_of(WarningSequence(100, 2, 1), 0) == "255 255 255!");
    // During the fade in: the fade out goes on from that darkness instead of showing the full picture first.
    SH_CHECK(frames_of(WarningSequence(100, 2, 1), 1) == "255 155 155 255 255!");
    SH_CHECK(frames_of(WarningSequence(100, 2, 1), 2) == "255 155 55 55 155 255 255!");
    // While held: the rest of the hold is left out.
    SH_CHECK(frames_of(WarningSequence(100, 5, 1), 3) == "255 155 55 - 0 100 200 255 255!");
    // Once it fades out it runs to its end.
    SH_CHECK(frames_of(WarningSequence(100, 2, 1), 6) == frames_of(WarningSequence(100, 2, 1)));
    const WarningSequence start(100, 2, 1);
    SH_CHECK(start.skippable() && start.darkened() && !start.final_frame() && !start.done());
}

SH_TEST(pause_menu_save_rule) {
    SH_CHECK(save_block(false, true, false) == SaveBlock::None && save_block(false, false, true) == SaveBlock::None);
    // Back brought the menu up and neither option allows saving here: only at the notebooks, as in the original.
    SH_CHECK(save_block(false, false, false) == SaveBlock::SavePointsOnly);
    SH_CHECK(save_block(true, true, true) == SaveBlock::BossFight && save_block(true, false, false) == SaveBlock::BossFight);
    SH_CHECK(save_allowed(SaveBlock::None) && !save_allowed(SaveBlock::BossFight) && !save_allowed(SaveBlock::SavePointsOnly));
    SH_CHECK(save_block_notice(SaveBlock::None) == nullptr);
    SH_CHECK(std::string(save_block_notice(SaveBlock::BossFight)).find("boss") != std::string::npos);
    SH_CHECK(std::string(save_block_notice(SaveBlock::SavePointsOnly)).find("notebook") != std::string::npos);
}
