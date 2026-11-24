// SPDX-License-Identifier: GPL-3.0-or-later
//
// The d-pad of the touch controls (`touch_movement = dpad`, src/input/touch): the arrows a finger presses in each
// of the 8 sectors, the dead centre, a finger sliding across, the run toggle, and the layout line with the d-pad
// and the run button in it.

#include "framework/sh_test.h"

#include "input/touch/touch_gestures.h"
#include "input/touch/touch_icons.h"
#include "input/touch/touch_layout.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace sh::input::touch;

namespace {

constexpr float kRadius = 200.0f;
constexpr float kPi = 3.14159265f;

// A finger `distance` radii from the centre at `degrees` (0 = right, 90 = down on the screen).
std::uint8_t at(float degrees, float distance, std::uint8_t previous = 0) {
    const float a = degrees * kPi / 180.0f;
    return dpad_bits(std::cos(a) * distance * kRadius, std::sin(a) * distance * kRadius, kRadius, previous);
}

bool near(float a, float b, float eps = 0.0015f) { return std::fabs(a - b) <= eps; }

}  // namespace

SH_TEST(touch_dpad_eight_sectors) {
    SH_CHECK(at(0.0f, 0.7f) == kDpadRight);
    SH_CHECK(at(45.0f, 0.7f) == (kDpadRight | kDpadDown));
    SH_CHECK(at(90.0f, 0.7f) == kDpadDown);
    SH_CHECK(at(135.0f, 0.7f) == (kDpadDown | kDpadLeft));
    SH_CHECK(at(180.0f, 0.7f) == kDpadLeft);
    SH_CHECK(at(-180.0f, 0.7f) == kDpadLeft);
    SH_CHECK(at(225.0f, 0.7f) == (kDpadLeft | kDpadUp));
    SH_CHECK(at(270.0f, 0.7f) == kDpadUp);
    SH_CHECK(at(-90.0f, 0.7f) == kDpadUp);
    SH_CHECK(at(315.0f, 0.7f) == (kDpadUp | kDpadRight));
    // Each sector is 45 degrees wide, centred on its direction.
    SH_CHECK(at(-90.0f + 20.0f, 0.7f) == kDpadUp);
    SH_CHECK(at(-90.0f + 25.0f, 0.7f) == (kDpadUp | kDpadRight));
    SH_CHECK(at(-90.0f - 25.0f, 0.7f) == (kDpadUp | kDpadLeft));
    SH_CHECK(at(20.0f, 0.7f) == kDpadRight);
    SH_CHECK(at(-20.0f, 0.7f) == kDpadRight);
}

SH_TEST(touch_dpad_dead_centre) {
    DpadTuning t;
    SH_CHECK(dpad_bits(0.0f, 0.0f, kRadius, 0) == 0);
    SH_CHECK(at(-90.0f, t.dead * 0.9f) == 0);
    SH_CHECK(at(-90.0f, t.dead * 1.1f) == kDpadUp);
    // Pressed, the arrow lets go only nearer the centre, so a thumb resting at the edge of the dead zone holds.
    SH_CHECK(at(-90.0f, 0.5f * (t.dead + t.dead_keep), kDpadUp) == kDpadUp);
    SH_CHECK(at(-90.0f, t.dead_keep * 0.9f, kDpadUp) == 0);
    // No d-pad without a size.
    SH_CHECK(dpad_bits(10.0f, 0.0f, 0.0f, 0) == 0);
}

SH_TEST(touch_dpad_slide) {
    // A finger slides from up over the diagonal to right without lifting: up, up and right, right.
    std::uint8_t bits = 0;
    std::vector<std::uint8_t> seen;
    for (float deg = -90.0f; deg <= 0.0f; deg += 5.0f) {
        bits = at(deg, 0.7f, bits);
        if (seen.empty() || seen.back() != bits) seen.push_back(bits);
    }
    SH_CHECK(seen.size() == 3);
    if (seen.size() == 3) {
        SH_CHECK(seen[0] == kDpadUp);
        SH_CHECK(seen[1] == (kDpadUp | kDpadRight));
        SH_CHECK(seen[2] == kDpadRight);
    }
    // A pressed sector holds a few degrees past its edge, so a finger on the border does not flicker ...
    SH_CHECK(at(-90.0f + 25.0f, 0.7f, kDpadUp) == kDpadUp);
    SH_CHECK(at(-90.0f + 25.0f, 0.7f, 0) == (kDpadUp | kDpadRight));
    // ... and lets go beyond that.
    SH_CHECK(at(-90.0f + 35.0f, 0.7f, kDpadUp) == (kDpadUp | kDpadRight));
    // A finger that slid off the cross keeps steering, and a jump to the far side follows at once.
    SH_CHECK(at(180.0f, 3.0f, kDpadUp) == kDpadLeft);
    SH_CHECK(at(90.0f, 1.5f, kDpadUp) == kDpadDown);
}

SH_TEST(touch_run_toggle) {
    Toggle run;
    SH_CHECK(!run.on());  // off by default
    run.press();
    SH_CHECK(run.on() && run.held());
    run.release();
    SH_CHECK(run.on() && !run.held());  // latched: lifting the finger keeps running
    run.press();
    run.release();
    SH_CHECK(!run.on());
    // A second finger landing on the button while one holds it changes nothing.
    run.press();
    run.press();
    SH_CHECK(run.on());
    run.release();
    run.release();
    run.release();  // one release too many is harmless
    SH_CHECK(run.on() && !run.held());
    // The screen changed under the fingers: the latch stays, the next press toggles again.
    run.press();
    SH_CHECK(!run.on());
    run.release_all();
    run.press();
    SH_CHECK(run.on());
    run.reset();
    SH_CHECK(!run.on() && !run.held());
}

SH_TEST(touch_dpad_visibility) {
    Visibility v;
    Facts f;
    f.gameplay = true;
    f.dpad = true;
    v.update(f, 0.2f);
    SH_CHECK((v.visible() & bit(Control::Dpad)) != 0 && (v.visible() & bit(Control::Run)) != 0);
    SH_CHECK((v.visible() & bit(Control::Stick)) == 0);
    f.dpad = false;
    v.update(f, 0.016f);
    SH_CHECK((v.visible() & (bit(Control::Dpad) | bit(Control::Run))) == 0);
    SH_CHECK((v.visible() & bit(Control::Stick)) != 0);
}

SH_TEST(touch_layout_dpad_and_run) {
    SH_CHECK(control_name(Control::Dpad) == "dpad" && control_name(Control::Run) == "run");
    SH_CHECK(find_control("dpad") == Control::Dpad && find_control("run") == Control::Run);
    // Every control moved and as large as it may be: still one config line.
    Layout l = default_layout();
    for (int i = 0; i < kControlCount; ++i) {
        const Control c = static_cast<Control>(i);
        l[c] = clamp_place(c, {0.123f + 0.07f * static_cast<float>(i), 0.987f - 0.05f * static_cast<float>(i), 9.0f});
    }
    const std::string text = format_layout(l);
    SH_CHECK(text.size() < 240);
    const Layout back = parse_layout(text);
    for (int i = 0; i < kControlCount; ++i) {
        const Place& a = l.places[static_cast<std::size_t>(i)];
        const Place& b = back.places[static_cast<std::size_t>(i)];
        SH_CHECK(near(a.x, b.x) && near(a.y, b.y) && near(a.size, b.size));
    }
    // The numbers are short: no leading zero, no trailing zeros.
    Layout d = default_layout();
    d[Control::Dpad] = {0.2f, 0.6f, 0.4f};
    d[Control::Run] = {1.0f, 0.0f, 0.14f};
    const std::string short_text = format_layout(d);
    SH_CHECK(short_text.find("dpad:.2,.6,.4") != std::string::npos);
    SH_CHECK(short_text.find("run:1,0,.14") != std::string::npos);
    SH_CHECK(short_text.find("0.") == std::string::npos);
    // A line written before the d-pad existed keeps the d-pad and run in their default places.
    std::vector<std::string> warnings;
    const Layout old = parse_layout(
        "stick:0.140,0.700,0.380;action:0.500,0.250,0.200;aim:0.680,0.800,0.180;attack:0.880,0.740,0.230;"
        "map:0.920,0.110,0.130;inventory:0.800,0.110,0.130;pause:0.060,0.110,0.130;struggle:0.800,0.640,0.320;"
        "camera:0.680,0.110,0.130",
        &warnings);
    SH_CHECK(warnings.empty());
    SH_CHECK(near(old[Control::Action].x, 0.5f));
    SH_CHECK(old[Control::Dpad] == default_layout()[Control::Dpad]);
    SH_CHECK(old[Control::Run] == default_layout()[Control::Run]);
    // And a line that names them moves them.
    const Layout moved = parse_layout("dpad:.3,.5,.3;run:.4,.3,.12");
    SH_CHECK(near(moved[Control::Dpad].x, 0.3f) && near(moved[Control::Dpad].y, 0.5f));
    SH_CHECK(near(moved[Control::Run].size, 0.12f));
}

SH_TEST(touch_layout_dpad_defaults) {
    // The d-pad takes the stick's place; the run button sits clear of it on a wide phone and on a 4:3 tablet.
    const Layout l = default_layout();
    for (const Area area : {Area{0.0f, 0.0f, 2400.0f, 1080.0f}, Area{0.0f, 0.0f, 1440.0f, 1080.0f}}) {
        const Circle pad = circle_of(l[Control::Dpad], area, 1.0f);
        const Circle run = circle_of(l[Control::Run], area, 1.0f);
        SH_CHECK(pad.x - pad.r >= area.x && pad.y + pad.r <= area.y + area.h);
        SH_CHECK(std::hypot(pad.x - run.x, pad.y - run.y) > pad.r * 1.25f + run.r);
        // A finger on the d-pad's centre hits the d-pad, on the run button the run button.
        const std::uint32_t shown = bit(Control::Dpad) | bit(Control::Run);
        SH_CHECK(hit_test(l, area, 1.0f, shown, pad.x, pad.y, 0.0f) == Control::Dpad);
        SH_CHECK(hit_test(l, area, 1.0f, shown, run.x, run.y, 0.0f) == Control::Run);
    }
}

SH_TEST(touch_dpad_icon_files) {
    for (const char* name : {"run", "dpad_up", "dpad_right", "dpad_down", "dpad_left"}) {
        const std::string path = std::string(SH_PROJECT_ROOT "/gamedata/ui/touch/") + name + ".svg";
        std::FILE* f = std::fopen(path.c_str(), "rb");
        SH_CHECK(f != nullptr);
        if (f == nullptr) continue;
        std::string text;
        char buffer[4096];
        for (std::size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) > 0;) text.append(buffer, n);
        std::fclose(f);
        const auto icon = parse_svg_icon(text);
        SH_CHECK(static_cast<bool>(icon));
        if (icon) {
            SH_CHECK(!icon.value().triangles.empty() || !icon.value().strokes.empty());
        }
    }
}
