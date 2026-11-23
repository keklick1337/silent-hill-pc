// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The console's selection (src/input/object_pick.h) over a small world in memory: a camera at the origin looking
// down +z, and draw coordinates equal to window pixels.

#include "framework/sh_test.h"

#include "input/object_pick.h"

#include <map>
#include <string>
#include <vector>

using namespace sh::input;

namespace {

constexpr int kUnit = 4096;
constexpr int kGroaner = 5, kNurse = 9, kHarry = 1;

struct Body {
    int chara_id = 0;
    PickPosition at;
    int health = 0;
};

class World final : public PickWorld {
public:
    int npc_slots() const override { return 8; }
    int chara_id(int npc) const override { return body(npc).chara_id; }
    PickPosition position(int npc) const override { return body(npc).at; }
    int health(int npc) const override { return body(npc).health; }
    bool is_player_type(int id) const override { return id == kHarry; }
    const char* chara_name(int id) const override { return id == kGroaner ? "GROANER" : nullptr; }
    PickViewPoint view_point(const PickPosition& w) const override { return {w.x >> 4, w.y >> 4, w.z >> 4}; }
    PickProjection projection() const override { return {256, 0, 0}; }
    bool pointer(int wx, int wy, float* x, float* y) const override {
        *x = static_cast<float>(wx);
        *y = static_cast<float>(wy);
        return in_picture;
    }
    void print(std::string_view line) override { lines.emplace_back(line); }

    Body& at(int npc) { return bodies[npc]; }
    const Body& body(int npc) const {
        static const Body none;
        const auto it = bodies.find(npc);
        return it != bodies.end() ? it->second : none;
    }

    std::map<int, Body> bodies;
    std::vector<std::string> lines;
    bool in_picture = true;
};

PickCharacter drawn(const World& w, int npc) {
    PickCharacter c;
    c.npc = npc;
    c.chara_id = w.chara_id(npc);
    c.position = w.position(npc);
    c.box_top = -kUnit * 3 / 2;
    c.box_height = 0;
    c.radius = kUnit / 2;
    return c;
}

// A world with the player 4 units ahead of the camera and two NPCs.
World scene() {
    World w;
    w.at(-1) = {kHarry, {0, 0, 4 * kUnit}, 100 * kUnit};
    w.at(0) = {kGroaner, {2 * kUnit, 0, 8 * kUnit}, 30 * kUnit};
    w.at(3) = {kNurse, {-2 * kUnit, 0, 6 * kUnit}, 50 * kUnit};
    return w;
}

void draw_all(World& w, ObjectPick& pick) {
    pick.character_drawn(drawn(w, -1));
    pick.character_drawn(drawn(w, 0));
    pick.character_drawn(drawn(w, 3));
}

}  // namespace

SH_TEST(pick_projection_limits) {
    float x = 0, y = 0;
    int depth = 0;
    const PickProjection p{256, 10, -20};
    SH_CHECK(pick_project({100, -50, 1024}, p, &x, &y, &depth));
    SH_CHECK(x == 35.0f && y == -33.0f && depth == 256);  // 10 + 100/4, -20 - 50/4 rounded down
    SH_CHECK(!pick_project({0, 0, 0}, p, &x, &y, &depth));      // at the camera
    SH_CHECK(!pick_project({0, 0, -500}, p, &x, &y, &depth));   // behind it
    SH_CHECK(!pick_project({0, 0, 35}, p, &x, &y, &depth));     // nearer than the nearest depth key
    SH_CHECK(pick_project({0, 0, 36}, p, &x, &y, &depth) && depth == 9);
    // Closer than half the plane the ratio stops just under 2, and the screen box is +-1024.
    SH_CHECK(pick_project({100, 0, 100}, {256, 0, 0}, &x, &y, &depth));
    SH_CHECK(x == 199.0f);
    SH_CHECK(pick_project({30000, -30000, 100}, {256, 0, 0}, &x, &y, &depth));
    SH_CHECK(x == 1023.0f && y == -1024.0f);
    SH_CHECK(pick_project({0, 0, 70000}, {256, 0, 0}, &x, &y, &depth) && depth == 0xFFFF / 4);
}

SH_TEST(pick_click_selects_what_is_drawn_under_it) {
    World w = scene();
    ObjectPick pick(w);
    // The Groaner: 8 units away, 2 to the right -> x = 256 * 2 / 8 = 64, body from y = -48 to 0.
    pick.click_at(64, -24);
    draw_all(w, pick);
    pick.frame_end();   // the frame that tested the click has ended; the report comes with the next one
    SH_CHECK(pick.kind() == PickKind::None && w.lines.empty());
    draw_all(w, pick);
    pick.frame_end();
    SH_CHECK(pick.kind() == PickKind::Npc && pick.slot() == 0 && pick.selected_npc() == 0);
    SH_CHECK(w.lines.back() == "selected npc[0] GROANER  hp 30.0  pos (2.0, 0.0, 8.0)");
    SH_CHECK(pick.describe() == "npc[0] GROANER");

    // The player in the middle; a type without a name is described by its number.
    pick.click_at(0, -50);
    draw_all(w, pick);
    pick.frame_end();
    pick.frame_end();
    SH_CHECK(pick.kind() == PickKind::Player && pick.slot() == -1);
    SH_CHECK(w.lines.back() == "selected player  hp 100.0  pos (0.0, 0.0, 4.0)");
    pick.click_at(-85, -30);
    draw_all(w, pick);
    pick.frame_end();
    pick.frame_end();
    SH_CHECK(pick.describe() == "npc[3] chara 9");

    // Nothing there: the selection stays.
    pick.click_at(300, 200);
    draw_all(w, pick);
    pick.frame_end();
    pick.frame_end();
    SH_CHECK(w.lines.back() == "nothing there to select");
    SH_CHECK(pick.slot() == 3);
    // Outside the picture nothing is hit either.
    w.in_picture = false;
    pick.click_at(64, -24);
    draw_all(w, pick);
    pick.frame_end();
    pick.frame_end();
    SH_CHECK(w.lines.back() == "nothing there to select");
}

SH_TEST(pick_nearest_body_wins_an_overlap) {
    World w = scene();
    w.at(3).at = {0, 0, 12 * kUnit};  // behind the player, same place on screen
    ObjectPick pick(w);
    pick.click_at(0, -10);
    pick.character_drawn(drawn(w, 3));
    pick.character_drawn(drawn(w, -1));
    pick.frame_end();
    pick.frame_end();
    SH_CHECK(pick.kind() == PickKind::Player);
    pick.click_at(0, -10);
    pick.character_drawn(drawn(w, -1));
    pick.character_drawn(drawn(w, 3));
    pick.frame_end();
    pick.frame_end();
    SH_CHECK(pick.kind() == PickKind::Player);
}

SH_TEST(pick_select_commands) {
    World w = scene();
    ObjectPick pick(w);
    pick.clear(true);
    SH_CHECK(w.lines.back() == "nothing was selected");
    SH_CHECK(pick.describe() == "nothing");
    SH_CHECK(pick.nearest_npc() == 3);  // the nurse: 2 units from the player, the groaner 4.5
    w.at(3).health = 0;
    SH_CHECK(pick.nearest_npc() == 0);  // the dead are skipped
    w.at(0).chara_id = kHarry;
    SH_CHECK(pick.nearest_npc() == -1);  // and so are the player's own types
    w.at(0).chara_id = kGroaner;

    SH_CHECK(!pick.select_npc(5) && !pick.select_npc(-1) && !pick.select_npc(8));
    SH_CHECK(pick.select_npc(0));
    pick.clear(true);
    SH_CHECK(w.lines.back() == "deselected npc[0] GROANER");
    SH_CHECK(pick.kind() == PickKind::None && pick.slot() == -1);
    pick.select_player();
    SH_CHECK(pick.describe() == "player");
}

SH_TEST(pick_a_reused_slot_starts_clean) {
    World w = scene();
    ObjectPick pick(w);
    pick.select_npc(0);
    pick.set_no_collide(true);
    SH_CHECK(!pick.npc_tick(0, kGroaner, 30 * kUnit));
    pick.set_frozen(0, true);
    SH_CHECK(pick.npc_tick(0, kGroaner, 30 * kUnit));  // frozen
    SH_CHECK(pick.no_collide(PickKind::Npc, 0, kGroaner));
    SH_CHECK(pick.frozen(0));

    // Another monster moves into the slot.
    w.at(0) = {kNurse, {0, 0, 9 * kUnit}, 50 * kUnit};
    SH_CHECK(!pick.no_collide(PickKind::Npc, 0, kNurse));
    SH_CHECK(!pick.frozen(0));
    SH_CHECK(pick.selected_npc() == -1);
    SH_CHECK(!pick.npc_tick(0, kNurse, 50 * kUnit));
    pick.character_drawn(drawn(w, 0));
    SH_CHECK(pick.kind() == PickKind::None);  // the stale selection is dropped when the newcomer is drawn
}

SH_TEST(pick_health_freeze_and_collision_switches) {
    World w = scene();
    ObjectPick pick(w);
    SH_CHECK(pick.npc_max_health(0) == 0);
    SH_CHECK(!pick.npc_tick(0, kGroaner, 30 * kUnit));
    SH_CHECK(!pick.npc_tick(0, kGroaner, 12 * kUnit));
    SH_CHECK(pick.npc_max_health(0) == 30 * kUnit);  // the most seen
    SH_CHECK(!pick.npc_tick(-1, kGroaner, 1) && !pick.npc_tick(8, kGroaner, 1));
    SH_CHECK(pick.npc_max_health(5) == 0);

    SH_CHECK(!pick.set_frozen(5, true));  // an empty slot
    SH_CHECK(pick.set_frozen(0, true) && pick.frozen(0));
    SH_CHECK(pick.npc_tick(0, kGroaner, 12 * kUnit));
    SH_CHECK(pick.set_frozen(0, false) && !pick.frozen(0));

    SH_CHECK(!pick.set_no_collide(true));  // nothing selected
    pick.select_player();
    SH_CHECK(pick.set_no_collide(true) && pick.no_collide_selected());
    SH_CHECK(pick.no_collide(PickKind::Player, -1, kHarry));
    SH_CHECK(!pick.no_collide(PickKind::Npc, 0, kGroaner));
    pick.select_npc(3);
    SH_CHECK(!pick.no_collide_selected());
    SH_CHECK(pick.set_no_collide(true) && pick.no_collide_selected());
    SH_CHECK(pick.set_no_collide(false) && !pick.no_collide_selected());

    pick.reset();
    SH_CHECK(pick.kind() == PickKind::None);
    SH_CHECK(!pick.no_collide(PickKind::Player, -1, kHarry));
    SH_CHECK(pick.npc_max_health(0) == 0);
}

SH_TEST(pick_props) {
    World w = scene();
    ObjectPick pick(w);
    SH_CHECK(!pick.click_waiting());
    // An object 5 units ahead, half a unit left; placements have 8 fractional bits.
    pick.click_at(-25, 0);
    SH_CHECK(pick.click_waiting());
    pick.prop_drawn(-128, 0, 5 * 256);
    pick.prop_drawn(20 * 256, 0, 5 * 256);
    pick.frame_end();
    pick.frame_end();
    SH_CHECK(pick.kind() == PickKind::Prop);
    SH_CHECK(pick.describe() == "prop @ (-0.5, 0.0, 5.0)");
    SH_CHECK(w.lines.back() == "selected prop @ (-0.5, 0.0, 5.0)");
    int x = 0, y = 0, z = 0;
    SH_CHECK(pick.prop_position(&x, &y, &z) && x == -kUnit / 2 && y == 0 && z == 5 * kUnit);
    SH_CHECK(!pick.set_no_collide(true));  // objects have no collision of their own to switch

    // A character under the same click at the same depth is preferred to an object.
    w.at(0).at = {-kUnit / 2, 0, 5 * kUnit};
    pick.click_at(-25, 0);
    pick.character_drawn(drawn(w, 0));
    pick.prop_drawn(-128, 0, 5 * 256);
    pick.frame_end();
    pick.frame_end();
    SH_CHECK(pick.kind() == PickKind::Npc);
    pick.select_player();
    SH_CHECK(!pick.prop_position(&x, &y, &z));
}
