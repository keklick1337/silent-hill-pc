// The map event triggers against their specification (the original's event scan): selection order,
// flag gates, activation kinds, item slots and the shape tests, with expected values worked out by
// hand from the integer formulas. The tests do not link the game layer.

#include "engine/gameflow/event_triggers.h"

#include <map>
#include <vector>

#include "framework/sh_test.h"

namespace {

namespace gf = sh::gameflow;

constexpr std::int32_t kMetre = 4096;

class FakeHost final : public gf::EventHost {
public:
    std::map<int, bool> flags;
    std::vector<gf::TriggerPoint> points;
    std::vector<int> set;

    bool flag(std::int16_t f) const override {
        auto it = flags.find(f);
        return it != flags.end() && it->second;
    }
    void set_flag(std::int16_t f) override {
        flags[f] = true;
        set.push_back(f);
    }
    gf::TriggerPoint point(std::uint8_t i) const override { return points.at(i); }
};

gf::EventTrigger box_event(std::uint8_t state, std::uint8_t param) {
    gf::EventTrigger e;
    e.shape = gf::TriggerShape::Box;
    e.activation = gf::Activation::Touch;
    e.point = 0;
    e.state = state;
    e.param = param;
    return e;
}

FakeHost host_with_box() {
    FakeHost h;
    // 1 m x 0.5 m half extents around (10 m, 5 m).
    h.points.push_back(gf::TriggerPoint{10 * kMetre, 5 * kMetre, 4, 2});
    return h;
}

gf::TriggerFrame at(std::int32_t x, std::int32_t z) {
    gf::TriggerFrame f;
    f.player_x = x;
    f.player_z = z;
    f.facing_reach = 3276;  // 0.8 m
    return f;
}

gf::EventTriggers make() {
    gf::EventTriggers t;
    t.set_rules(gf::EventRules{[](std::int16_t flag) { return flag == 900; }});  // 900 marks the map
    return t;
}

constexpr std::uint8_t kReadMessage = static_cast<std::uint8_t>(gf::GameState::ReadMessage);
constexpr std::uint8_t kNone = static_cast<std::uint8_t>(gf::GameState::None);

}  // namespace

SH_TEST(event_triggers_box_edges_are_inside) {
    auto t = make();
    auto h = host_with_box();
    std::vector<gf::EventTrigger> ev{box_event(kReadMessage, 3)};

    auto r = t.update(at(11 * kMetre, 5 * kMetre + kMetre / 2), ev, true, h);
    SH_CHECK(r.has_event && r.event == 0 && r.state == kReadMessage && r.param == 3);

    r = t.update(at(11 * kMetre + 1, 5 * kMetre), ev, true, h);
    SH_CHECK(r.has_event && r.event == -1 && r.state == kNone && r.param == 0);
}

SH_TEST(event_triggers_no_list_keeps_selection) {
    auto t = make();
    auto h = host_with_box();
    auto r = t.update(at(0, 0), {}, false, h);
    SH_CHECK(!r.has_event && !r.has_state && !r.has_param);
    SH_CHECK(r.has_event_item && r.event_item == 0);
}

SH_TEST(event_triggers_flag_gates) {
    auto t = make();
    auto h = host_with_box();
    std::vector<gf::EventTrigger> ev{box_event(kReadMessage, 1)};
    ev[0].required_flag = 5;
    auto r = t.update(at(10 * kMetre, 5 * kMetre), ev, true, h);
    SH_CHECK(r.event == -1);

    h.flags[5] = true;
    ev[0].blocking_flag = 900;  // soft: only blocks exclusive and set-flag events
    h.flags[900] = true;
    r = t.update(at(10 * kMetre, 5 * kMetre), ev, true, h);
    SH_CHECK(r.event == 0);

    ev[0].blocking_flag = 866;  // hard: not a map mark
    h.flags[866] = true;
    r = t.update(at(10 * kMetre, 5 * kMetre), ev, true, h);
    SH_CHECK(r.event == -1);
}

SH_TEST(event_triggers_always_wins_without_shape_or_button) {
    auto t = make();
    auto h = host_with_box();
    gf::EventTrigger always;
    always.shape = gf::TriggerShape::Always;
    always.activation = gf::Activation::Button;
    always.state = kReadMessage;
    always.param = 15;
    std::vector<gf::EventTrigger> ev{always, box_event(kReadMessage, 2)};
    auto r = t.update(at(-50 * kMetre, 0), ev, true, h);
    SH_CHECK(r.event == 0 && r.param == 15);
}

SH_TEST(event_triggers_exclusive_holds_while_running) {
    auto t = make();
    auto h = host_with_box();
    std::vector<gf::EventTrigger> ev{box_event(kReadMessage, 1)};
    ev[0].activation = gf::Activation::Exclusive;
    auto f = at(10 * kMetre, 5 * kMetre);
    f.current_event = 0;
    auto r = t.update(f, ev, true, h);
    SH_CHECK(!r.has_event && r.has_state && r.state == kNone && !r.has_param);
}

SH_TEST(event_triggers_button_and_darkness) {
    auto t = make();
    auto h = host_with_box();
    std::vector<gf::EventTrigger> ev{box_event(kReadMessage, 2)};
    ev[0].activation = gf::Activation::Button;
    auto f = at(10 * kMetre, 5 * kMetre);
    SH_CHECK(t.update(f, ev, true, h).event == -1);
    f.action_pressed = true;
    SH_CHECK(t.update(f, ev, true, h).event == 0);
    f.player_busy = true;
    SH_CHECK(t.update(f, ev, true, h).event == -1);
    f.player_busy = false;
    f.dark_without_light = true;
    SH_CHECK(t.update(f, ev, true, h).event == -1);
    ev[0].param = 1;
    SH_CHECK(t.update(f, ev, true, h).event == 0);
    ev[0].param = 9;
    ev[0].state = static_cast<std::uint8_t>(gf::GameState::LoadRoom);
    SH_CHECK(t.update(f, ev, true, h).event == 0);
}

SH_TEST(event_triggers_set_flag_event_sets_blocking_flag_and_stops) {
    auto t = make();
    auto h = host_with_box();
    std::vector<gf::EventTrigger> ev{box_event(static_cast<std::uint8_t>(gf::GameState::EventSetFlag), 0),
                                     box_event(kReadMessage, 4)};
    ev[0].blocking_flag = 42;
    auto r = t.update(at(10 * kMetre, 5 * kMetre), ev, true, h);
    SH_CHECK(r.flag_set == 42 && h.set.size() == 1 && r.event == -1 && r.state == kNone);
    r = t.update(at(10 * kMetre, 5 * kMetre), ev, true, h);
    SH_CHECK(r.event == 1);
}

SH_TEST(event_triggers_item_slot_then_use) {
    auto t = make();
    auto h = host_with_box();
    std::vector<gf::EventTrigger> ev{box_event(kReadMessage, 7)};
    ev[0].activation = gf::Activation::Item;
    ev[0].item = 33;
    auto f = at(10 * kMetre, 5 * kMetre);
    auto r = t.update(f, ev, true, h);
    SH_CHECK(r.event == -1 && t.item_slots()[0].item == 33 && t.item_slots()[0].event == 0);
    SH_CHECK(t.item_slots()[1].item == -1);

    f.used_item = 33;
    r = t.update(f, {}, false, h);  // the list may be gone; the slot keeps the event
    SH_CHECK(r.consume_used_item && r.has_event && r.event == 0 && r.item_slot == 0);
    SH_CHECK(r.state == kReadMessage && r.param == 7 && r.event_item == 33);
    SH_CHECK(t.item_slots()[0].item == -1);

    f.used_item = 34;  // not on a slot: consumed, selection untouched
    r = t.update(f, ev, true, h);
    SH_CHECK(r.consume_used_item && !r.has_event && !r.has_state && !r.has_event_item);
}

SH_TEST(event_triggers_facing_point_probe_is_cached_per_tick) {
    auto t = make();
    // Player at the origin facing +Z (heading 0): the probe is 1/8 m behind him at tick 1.
    gf::TriggerFrame f = at(0, 0);
    f.tick = 1;
    gf::TriggerPoint ahead{0, kMetre / 2, 0, 0};
    SH_CHECK(t.facing_point(f, ahead));
    gf::TriggerPoint behind{0, -kMetre / 2, 0, 0};
    SH_CHECK(!t.facing_point(f, behind));
    // Same tick: the probe does not move with the player.
    f.player_z = 10 * kMetre;
    SH_CHECK(t.facing_point(f, ahead));
    // 0.8 m reach from the probe (-512): 0.8 m = 3276, so z = 2764 is in, 2765 out.
    f.player_z = 0;
    f.tick = 2;
    SH_CHECK(t.facing_point(f, gf::TriggerPoint{0, 2764, 0, 0}));
    SH_CHECK(!t.facing_point(f, gf::TriggerPoint{0, 2765, 0, 0}));
}

SH_TEST(event_triggers_facing_point_tick_zero_keeps_origin_probe) {
    auto t = make();
    gf::TriggerFrame f = at(100 * kMetre, 0);
    f.tick = 0;
    SH_CHECK(t.facing_point(f, gf::TriggerPoint{0, kMetre / 2, 0, 0}));
}

SH_TEST(event_triggers_oriented_box) {
    // param1 = 4: half width 1024 (0.25 m), x and z within 2048; depth 0..4 m along -sin/cos.
    gf::TriggerPoint p{0, 0, 0, 4};
    gf::TriggerFrame f = at(0, 1000);
    SH_CHECK(gf::EventTriggers::oriented_box(f, p));
    f.player_z = -1;  // behind the point: negative depth wraps and fails
    SH_CHECK(!gf::EventTriggers::oriented_box(f, p));
    f.player_z = 1000;
    f.player_x = 1025;
    SH_CHECK(!gf::EventTriggers::oriented_box(f, p));
}

SH_TEST(event_triggers_facing_segment) {
    // A segment along X 1 m ahead of a player facing +Z: param0 = 128 (half a turn) gives the half
    // vector (-cos(2048) * 16) >> 4 = +4096 in X; with param0 = 0 the segment runs the other way
    // and the side tests reject it.
    gf::TriggerPoint p{0, kMetre, 128, 16};
    gf::TriggerFrame f = at(0, 0);
    SH_CHECK(!gf::EventTriggers::facing_segment(f, p));  // the ray is 0.5 m long
    f.player_z = kMetre / 2 + 100;
    SH_CHECK(gf::EventTriggers::facing_segment(f, p));
    f.player_heading = 2048;  // facing away
    SH_CHECK(!gf::EventTriggers::facing_segment(f, p));
    f.player_heading = 0;
    p.param0 = 0;
    SH_CHECK(!gf::EventTriggers::facing_segment(f, p));
}
