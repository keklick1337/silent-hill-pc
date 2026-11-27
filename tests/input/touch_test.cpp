// SPDX-License-Identifier: GPL-3.0-or-later
//
// The touch controls' plain parts (src/input/touch): the layout and its config line, hit tests, the stick's pad
// bytes, which controls show, the pinch, the scrolling drag and the action button's kinds.

#include "engine/input/touch_prompts.h"
#include "framework/sh_test.h"

#include "input/touch/action_kinds.h"
#include "input/touch/touch_gestures.h"
#include "input/touch/touch_icons.h"
#include "input/touch/touch_layout.h"
#include "input/touch/touch_sprites.h"

#include <cmath>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace sh::input::touch;

namespace {

bool near(float a, float b, float eps = 0.0015f) { return std::fabs(a - b) <= eps; }

const Area kScreen{0.0f, 0.0f, 2400.0f, 1080.0f};

}  // namespace

SH_TEST(touch_layout_round_trip) {
    Layout l = default_layout();
    l[Control::Action] = {0.5f, 0.25f, 0.2f};
    const std::string text = format_layout(l);
    SH_CHECK(text.size() < 240);  // one config line, the field's size
    const Layout back = parse_layout(text);
    for (int i = 0; i < kControlCount; ++i) {
        const Place& a = l.places[static_cast<std::size_t>(i)];
        const Place& b = back.places[static_cast<std::size_t>(i)];
        SH_CHECK(near(a.x, b.x) && near(a.y, b.y) && near(a.size, b.size));
    }
    SH_CHECK(parse_layout("") == default_layout());
}

SH_TEST(touch_layout_bad_entries_keep_defaults) {
    std::vector<std::string> warnings;
    const Layout l = parse_layout("aim:0.5,0.5,0.18; bogus:1,2,3; map:0.1,0.2; stick:x,0.5,0.3", &warnings);
    SH_CHECK(warnings.size() == 3);
    SH_CHECK(near(l[Control::Aim].x, 0.5f) && near(l[Control::Aim].y, 0.5f));
    SH_CHECK(l[Control::Map] == default_layout()[Control::Map]);
    SH_CHECK(l[Control::Stick] == default_layout()[Control::Stick]);
}

SH_TEST(touch_layout_clamps) {
    const Place base = default_layout()[Control::Attack];
    const Layout l = parse_layout("attack:1.7,-0.2,9");
    SH_CHECK(near(l[Control::Attack].x, 1.0f) && near(l[Control::Attack].y, 0.0f));
    SH_CHECK(near(l[Control::Attack].size, base.size * kMaxSizeFactor));
    const Place small = clamp_place(Control::Attack, {0.5f, 0.5f, 0.01f});
    SH_CHECK(near(small.size, base.size * kMinSizeFactor));
}

SH_TEST(touch_layout_hit_test) {
    const Layout l = default_layout();
    const Circle action = circle_of(l[Control::Action], kScreen, 1.0f);
    const std::uint32_t all = 0xFFFFFFFFu;
    SH_CHECK(hit_test(l, kScreen, 1.0f, all, action.x, action.y, 0.0f) == Control::Action);
    // Hidden controls take no fingers.
    SH_CHECK(!hit_test(l, kScreen, 1.0f, all & ~bit(Control::Action), action.x, action.y, 0.0f));
    // Just outside the circle only the slop reaches it.
    SH_CHECK(!hit_test(l, kScreen, 1.0f, all, action.x + action.r + 5.0f, action.y, 0.0f));
    SH_CHECK(hit_test(l, kScreen, 1.0f, all, action.x + action.r + 5.0f, action.y, 10.0f) == Control::Action);
    // The stick is never a button; its zone is the lower left. The d-pad and its run toggle sit in the same place
    // and are shown only in the d-pad movement style, instead of the stick.
    const Circle stick = circle_of(l[Control::Stick], kScreen, 1.0f);
    const std::uint32_t stick_style = all & ~(bit(Control::Dpad) | bit(Control::Run));
    SH_CHECK(!hit_test(l, kScreen, 1.0f, stick_style, stick.x, stick.y, 0.0f));
    const Circle dpad = circle_of(l[Control::Dpad], kScreen, 1.0f);
    SH_CHECK(hit_test(l, kScreen, 1.0f, all & ~bit(Control::Stick), dpad.x, dpad.y, 0.0f) == Control::Dpad);
    SH_CHECK(in_stick_zone(kScreen, stick.x, stick.y));
    SH_CHECK(!in_stick_zone(kScreen, 2000.0f, 800.0f));
    SH_CHECK(!in_stick_zone(kScreen, 100.0f, 50.0f));  // the pause button's corner
    // A safe area moves and scales the controls with it.
    const Area inset{120.0f, 0.0f, 2160.0f, 1080.0f};
    const Circle moved = circle_of(l[Control::Action], inset, 1.0f);
    SH_CHECK(near(moved.x, 120.0f + l[Control::Action].x * 2160.0f, 0.01f));
    SH_CHECK(near(circle_of(l[Control::Action], kScreen, 1.5f).r, action.r * 1.5f, 0.01f));
}

SH_TEST(touch_stick_bytes) {
    // Inside the dead zone the stick rests at the centre; past it, the game's threshold is crossed at once.
    SH_CHECK(axis_byte(0.1f, 0.18f) == 128);
    SH_CHECK(axis_byte(0.2f, 0.18f) - 128 >= 64);
    SH_CHECK(128 - axis_byte(-0.2f, 0.18f) > 64);
    SH_CHECK(axis_byte(1.0f, 0.18f) == 255);
    SH_CHECK(axis_byte(-1.0f, 0.18f) == 1);
    StickTuning t;
    StickOutput o = stick_output(0.0f, -100.0f, 100.0f, t);  // straight forward, full push
    SH_CHECK(o.axis_y == 1 && o.axis_x == 128 && o.run && near(o.magnitude, 1.0f));
    o = stick_output(0.0f, -60.0f, 100.0f, t);  // forward, part way: walks
    SH_CHECK(o.axis_y < 64 && !o.run);
    o = stick_output(0.0f, 300.0f, 100.0f, t);  // dragged far back: clamped, no running backwards
    SH_CHECK(o.axis_y == 255 && !o.run && near(o.y, 1.0f));
    o = stick_output(100.0f, 0.0f, 100.0f, t);  // a sideways push is a turn
    SH_CHECK(o.axis_x == 255 && !o.run);
    o = stick_output(70.0f, -70.0f, 100.0f, t);  // forward and to the right at the edge: runs while it turns
    SH_CHECK(o.run && o.axis_x > 128 + 64 && o.axis_y < 128 - 64);
    o = stick_output(0.0f, -70.0f, 100.0f, t);  // below the run push: walks...
    SH_CHECK(!o.run);
    t.running = true;  // ...but a run already going keeps going there (no flicker at the edge)
    SH_CHECK(stick_output(0.0f, -70.0f, 100.0f, t).run);
    SH_CHECK(!stick_output(0.0f, -50.0f, 100.0f, t).run);
    SH_CHECK(!stick_output(95.0f, -10.0f, 100.0f, t).run);  // no longer forward: the run ends
    t.running = false;
    t.run = false;
    SH_CHECK(!stick_output(0.0f, -100.0f, 100.0f, t).run);
}

SH_TEST(touch_stick_origin_follows) {
    const Point o{100.0f, 100.0f};
    SH_CHECK(follow_origin(o, {150.0f, 100.0f}, 50.0f, 1.6f).x == 100.0f);
    const Point moved = follow_origin(o, {300.0f, 100.0f}, 50.0f, 1.6f);
    SH_CHECK(near(moved.x, 220.0f, 0.01f) && near(moved.y, 100.0f));
}

SH_TEST(touch_visibility) {
    Visibility v;
    Facts f;
    f.gameplay = true;
    v.update(f, 0.05f);
    SH_CHECK(!v.shown() && v.visible() == 0);  // not yet: a frame of gameplay between screens shows nothing
    v.update(f, 0.1f);
    SH_CHECK(v.shown() && (v.visible() & bit(Control::Stick)) != 0);
    SH_CHECK((v.visible() & bit(Control::Action)) == 0 && (v.visible() & bit(Control::Attack)) == 0);
    SH_CHECK((v.visible() & bit(Control::Aim)) == 0);  // nothing to aim without a weapon
    f.armed = true;
    f.action_target = true;
    f.aiming = true;
    v.update(f, 0.016f);
    SH_CHECK((v.visible() & bit(Control::Action)) != 0 && (v.visible() & bit(Control::Attack)) != 0);
    SH_CHECK((v.visible() & bit(Control::Aim)) != 0);
    // The target leaves: the button lingers a moment, then goes.
    f.action_target = false;
    v.update(f, 0.2f);
    SH_CHECK((v.visible() & bit(Control::Action)) != 0);
    v.update(f, 0.2f);
    SH_CHECK((v.visible() & bit(Control::Action)) == 0);
    f.action_always = true;
    v.update(f, 0.016f);
    SH_CHECK((v.visible() & bit(Control::Action)) != 0);
    // A menu over the game takes everything away at once.
    f.gameplay = false;
    v.update(f, 0.016f);
    SH_CHECK(!v.shown() && v.visible() == 0 && v.alpha(Control::Stick) == 0.0f);
    // Fades in.
    f.gameplay = true;
    v.update(f, 0.13f);
    SH_CHECK(v.alpha(Control::Stick) > 0.0f && v.alpha(Control::Stick) <= 1.0f);
}

SH_TEST(touch_visibility_prompts) {
    Visibility v;
    Facts f;
    f.gameplay = true;
    f.armed = true;
    v.update(f, 0.2f);
    SH_CHECK((v.visible() & bit(Control::Camera)) != 0);  // the camera is there all through free play
    SH_CHECK((v.visible() & bit(Control::Struggle)) == 0 && (v.visible() & bit(Control::Attack)) == 0);
    // A downed enemy in reach: attack shows with the weapon down.
    f.prompted = bit(Control::Attack);
    v.update(f, 0.016f);
    SH_CHECK((v.visible() & bit(Control::Attack)) != 0);
    // Held: the struggle button alone takes the right thumb's place.
    f.prompted = bit(Control::Struggle);
    f.action_target = true;
    f.aiming = true;
    v.update(f, 0.016f);
    SH_CHECK((v.visible() & bit(Control::Struggle)) != 0);
    SH_CHECK((v.visible() & (bit(Control::Attack) | bit(Control::Aim) | bit(Control::Action))) == 0);
    SH_CHECK((v.visible() & bit(Control::Stick)) != 0);
    f.prompted = 0;
    v.update(f, 0.016f);
    SH_CHECK((v.visible() & bit(Control::Struggle)) == 0 && (v.visible() & bit(Control::Attack)) != 0);
}

SH_TEST(touch_prompt_press_taken_once) {
    // The save list asks for its Delete; the button's tap (or a long press) comes back as one press.
    sh::input::note_touch_prompt(sh::input::kTouchDelete, "delete");
    SH_CHECK(sh::input::touch_prompt(sh::input::kTouchDelete, 1000).value_or("") == "delete");
    SH_CHECK(!sh::input::take_touch_prompt_press(sh::input::kTouchDelete, 1000));
    sh::input::note_touch_prompt_press(sh::input::kTouchDelete);
    SH_CHECK(!sh::input::take_touch_prompt_press("struggle", 1000));
    SH_CHECK(sh::input::take_touch_prompt_press(sh::input::kTouchDelete, 1000));
    SH_CHECK(!sh::input::take_touch_prompt_press(sh::input::kTouchDelete, 1000));
    // A press the screen did not take in time (it was gone, a message was up) does not act later.
    sh::input::note_touch_prompt_press(sh::input::kTouchDelete);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    SH_CHECK(!sh::input::take_touch_prompt_press(sh::input::kTouchDelete, 1));
    SH_CHECK(!sh::input::take_touch_prompt_press(sh::input::kTouchDelete, 1000));
}

SH_TEST(touch_press_train) {
    PressTrain t;
    SH_CHECK(!t.step());
    // Three taps within one frame: three separate presses, each held kHold updates with kGap free between them.
    t.add();
    t.add();
    t.add();
    std::string seen;
    for (int i = 0; i < 3 * (PressTrain::kHold + PressTrain::kGap) + 2; ++i) seen += t.step() ? '#' : '.';
    SH_CHECK(seen == "###...###...###.....");
    // A burst is capped, not played out for seconds.
    for (int i = 0; i < 20; ++i) t.add();
    SH_CHECK(t.queued() == PressTrain::kMaxQueued);
    t.clear();
    SH_CHECK(!t.step());
}

SH_TEST(touch_stick_flicks) {
    Flicks k;
    // A thumb that pushes and stays is one press, not one per frame.
    SH_CHECK(k.update(0.0f, -0.9f));
    for (int i = 0; i < 10; ++i) SH_CHECK(!k.update(0.05f, -0.9f));
    // Back near the centre and out again: a new press.
    SH_CHECK(!k.update(0.0f, -0.2f));
    SH_CHECK(k.update(0.0f, -0.8f));
    // Straight over to the other side without resting: the direction change is a press.
    SH_CHECK(!k.update(0.0f, -0.4f));  // between rest and push: nothing
    SH_CHECK(k.update(0.0f, 0.8f));
    SH_CHECK(!k.update(0.1f, 0.85f));
    // A sideways wiggle too, and a thumb's jitter is none.
    SH_CHECK(k.update(0.8f, 0.0f));
    SH_CHECK(k.update(-0.8f, 0.0f));
    SH_CHECK(!k.update(-0.75f, 0.2f));
    // Under the push nothing counts at all.
    k.reset();
    SH_CHECK(!k.update(0.45f, 0.0f));
}

SH_TEST(touch_tap_and_pinch) {
    SH_CHECK(is_tap(0.1f, 3.0f, 10.0f));
    SH_CHECK(!is_tap(0.5f, 3.0f, 10.0f));
    SH_CHECK(!is_tap(0.1f, 30.0f, 10.0f));
    // A menu's tap: any length, no drag, not the finger that stopped a glide.
    SH_CHECK(pointer_tap(3.0f, 10.0f, false));
    SH_CHECK(!pointer_tap(30.0f, 10.0f, false));
    SH_CHECK(!pointer_tap(3.0f, 10.0f, true));
    Pinch p;
    p.start(200.0f);
    SH_CHECK(p.update(220.0f) == 0);
    SH_CHECK(p.update(260.0f) == 1);
    SH_CHECK(p.update(400.0f) == 0);  // once per pinch
    p.start(200.0f);
    SH_CHECK(p.update(150.0f) == -1);
    p.stop();
    SH_CHECK(!p.active() && p.update(10.0f) == 0);
}

SH_TEST(touch_scroller) {
    Scroller s;
    s.press();
    // 100 pixels down in steps of 25: four notches up, the list follows the finger.
    int total = 0;
    for (int i = 0; i < 10; ++i) total += s.drag(10.0f, 1.0f / 60.0f, 25.0f);
    SH_CHECK(total == 4);
    // Thrown at 600 px/s: the glide goes on, slows and stops.
    s.release();
    SH_CHECK(s.gliding());
    int glide = 0;
    for (int i = 0; i < 600 && s.gliding(); ++i) glide += s.glide(1.0f / 60.0f, 25.0f);
    SH_CHECK(glide > 0 && !s.gliding());
    // A slow drag does not glide.
    s.press();
    s.drag(1.0f, 1.0f / 60.0f, 25.0f);
    s.release();
    SH_CHECK(!s.gliding());
    // Upwards is the wheel down.
    s.press();
    SH_CHECK(s.drag(-60.0f, 0.1f, 25.0f) == -2);
}

SH_TEST(touch_pan_axis) {
    SH_CHECK(pan_axis(0.0f, 40.0f) == 128);
    SH_CHECK(pan_axis(0.5f, 40.0f) > 128 + 64);
    SH_CHECK(pan_axis(-0.5f, 40.0f) < 128 - 64);
    SH_CHECK(pan_axis(100.0f, 40.0f) == 255);
}

SH_TEST(touch_action_kinds) {
    SH_CHECK(glob_match("*Take", "MapEvent_KatanaItemTake"));
    SH_CHECK(glob_match("*Door*", "MapEvent_DoorLocked"));
    SH_CHECK(!glob_match("*Read", "MapEvent_Reader"));
    SH_CHECK(glob_match("a*b*c", "aXXbYYc") && !glob_match("a*b*c", "aXXbYY"));
    const auto kinds = ActionKinds::parse(R"({"states": {"LoadRoom": "open", "EventCallback": "check"},
        "handlers": [{"match": "*Take", "kind": "take"}, {"match": "*Read", "kind": "read"}]})");
    SH_CHECK(static_cast<bool>(kinds));
    SH_CHECK(kinds.value().classify("EventCallback", "MapEvent_ShotgunTake") == ActionKind::Take);
    SH_CHECK(kinds.value().classify("EventCallback", "MapEvent_NewspaperRead") == ActionKind::Read);
    SH_CHECK(kinds.value().classify("EventCallback", "MapEvent_Something") == ActionKind::Check);
    SH_CHECK(kinds.value().classify("LoadRoom", "") == ActionKind::Open);
    SH_CHECK(kinds.value().classify("GameOver", "") == ActionKind::Act);
    SH_CHECK(!ActionKinds::parse(R"({"states": {"LoadRoom": "fly"}})"));
    SH_CHECK(!ActionKinds::parse(R"({"handlers": [{"kind": "take"}]})"));
}

SH_TEST(touch_action_kinds_file) {
    // The shipped table parses.
    std::FILE* f = std::fopen(SH_PROJECT_ROOT "/gamedata/config/touch_actions.json", "rb");
    SH_CHECK(f != nullptr);
    std::string text;
    char buffer[4096];
    for (std::size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) > 0;) text.append(buffer, n);
    std::fclose(f);
    const auto kinds = ActionKinds::parse(text);
    SH_CHECK(static_cast<bool>(kinds));
    SH_CHECK(kinds.value().classify("EventCallback", "MapEvent_DoorLocked") == ActionKind::Open);
    SH_CHECK(kinds.value().classify("EventCallback", "MapEvent_CommonItemTake") == ActionKind::Take);
}

SH_TEST(touch_icon_triangulate) {
    // An L shape: concave, four triangles of the right total area.
    const std::vector<Point> l = {{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 2}};
    const auto tris = triangulate(l);
    SH_CHECK(tris.size() == 4);
    float area = 0.0f;
    for (const auto& t : tris)
        area += std::fabs((t[1].x - t[0].x) * (t[2].y - t[0].y) - (t[1].y - t[0].y) * (t[2].x - t[0].x)) * 0.5f;
    SH_CHECK(near(area, 3.0f, 0.001f));
    std::vector<Point> reversed(l.rbegin(), l.rend());
    SH_CHECK(triangulate(reversed).size() == 4);
    SH_CHECK(triangulate({{0, 0}, {1, 1}}).empty());
}

SH_TEST(touch_icon_parse) {
    const auto icon = parse_svg_icon(R"(<svg viewBox="0 0 64 64"><!-- a note -->
        <path d="M10 10 H20 V20 Z" fill="none" stroke="#fff" stroke-width="3"/>
        <path d="M30 30 l10 0 l0 10 z"/>
        <polygon points="0,0 4,0 4,4"/><rect x="1" y="1" width="2" height="2" stroke="#fff" fill="none"/>
        <circle cx="32" cy="32" r="5"/><circle cx="32" cy="32" r="9" fill="none" stroke="#fff" stroke-width="2"/>
        <line x1="0" y1="0" x2="5" y2="5" stroke="#fff"/></svg>)");
    SH_CHECK(static_cast<bool>(icon));
    const VectorIcon& v = icon.value();
    SH_CHECK(v.box_size == 64.0f);
    SH_CHECK(v.strokes.size() == 3);  // the open path, the rect's outline, the line
    SH_CHECK(v.strokes[0].closed && v.strokes[0].points.size() == 3 && v.strokes[0].width == 3.0f);
    SH_CHECK(v.triangles.size() == 2);  // the relative path and the polygon
    SH_CHECK(near(v.triangles[0][1].x, 40.0f) && near(v.triangles[0][2].y, 40.0f));
    SH_CHECK(v.rings.size() == 2 && v.rings[0].width == 0.0f && v.rings[1].width == 2.0f);
    SH_CHECK(!parse_svg_icon("<svg><path d=\"M0 0 C1 1 2 2 3 3\"/></svg>"));
    SH_CHECK(!parse_svg_icon("<g></g>"));
}

SH_TEST(touch_icon_files) {
    // Every picture the controls ask for is there and in the subset.
    for (const char* name : {"back", "skip", "pause", "map", "inventory", "aim", "act", "take", "read", "open", "use",
                             "check", "done", "reset", "attack", "attack_pistol", "attack_shotgun", "attack_rifle",
                             "attack_blaster", "attack_blade", "attack_blunt", "attack_axe", "attack_chainsaw",
                             "attack_drill", "struggle", "camera", "kick", "delete"}) {
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
        SH_CHECK(!icon.value().triangles.empty() || !icon.value().strokes.empty() || !icon.value().rings.empty());
    }
}

namespace {

int coverage_at(const Mask& m, int x, int y) { return m.alpha[static_cast<std::size_t>(y * m.size + x)]; }

float covered_area(const Mask& m) {
    float sum = 0.0f;
    for (std::uint8_t a : m.alpha) sum += static_cast<float>(a) / 255.0f;
    return sum;
}

}  // namespace

SH_TEST(touch_sprite_disc) {
    const Mask m = disc_mask(20.0f);
    SH_CHECK(m.size % 2 == 0 && m.size >= 40);
    SH_CHECK(m.alpha.size() == static_cast<std::size_t>(m.size * m.size));
    const int c = m.size / 2;
    SH_CHECK(coverage_at(m, c, c) == 255);
    SH_CHECK(coverage_at(m, 0, 0) == 0);
    // The edge is soft, one pixel wide, and the area is the circle's.
    int partial = 0;
    for (int x = c; x < m.size; ++x) partial += coverage_at(m, x, c) > 0 && coverage_at(m, x, c) < 255 ? 1 : 0;
    SH_CHECK(partial >= 1 && partial <= 2);
    SH_CHECK(near(covered_area(m), 3.14159265f * 400.0f, 15.0f));
}

SH_TEST(touch_sprite_ring_and_halo) {
    const Mask ring = ring_mask(30.0f, 4.0f);
    const int c = ring.size / 2;
    SH_CHECK(coverage_at(ring, c, c) == 0);
    SH_CHECK(coverage_at(ring, c + 30, c) == 255);
    SH_CHECK(near(covered_area(ring), 2.0f * 3.14159265f * 30.0f * 4.0f, 20.0f));
    const Mask halo = halo_mask(10.0f, 8.0f);
    const int h = halo.size / 2;
    SH_CHECK(coverage_at(halo, h, h) == 255);
    SH_CHECK(coverage_at(halo, h + 13, h) > 0 && coverage_at(halo, h + 13, h) < coverage_at(halo, h + 11, h));
    SH_CHECK(coverage_at(halo, h + 19, h) == 0);
}

SH_TEST(touch_sprite_icon) {
    // A filled square of half the viewBox and a line through it: united, not summed.
    const auto icon = parse_svg_icon(R"(<svg viewBox="0 0 64 64"><rect x="16" y="16" width="32" height="32"/>
        <line x1="0" y1="32" x2="64" y2="32" stroke="#fff" stroke-width="4"/></svg>)");
    SH_CHECK(static_cast<bool>(icon));
    const Mask m = icon_mask(icon.value(), 64.0f);
    SH_CHECK(m.size % 2 == 0 && m.size >= 64);
    const int c = m.size / 2;
    SH_CHECK(coverage_at(m, c - 8, c - 8) == 255);           // inside the square
    SH_CHECK(coverage_at(m, c - 28, c - 1) == 255);          // on the line outside the square
    SH_CHECK(coverage_at(m, c - 28, c - 10) == 0);           // beside the line
    // 32x32 of square plus the line's 4 x 32 outside it (and its round ends).
    SH_CHECK(near(covered_area(m), 32.0f * 32.0f + 4.0f * 32.0f + 3.14159265f * 4.0f, 12.0f));
}
