// Collision triggers and the character rules of the collision queries (engine/world/collision_triggers.h,
// engine/world/chara_collision.h).

#include "engine/world/chara_collision.h"
#include "engine/world/collision_triggers.h"

#include "framework/sh_test.h"

#include <vector>

namespace {

namespace wd = sh::world;

constexpr std::int32_t kOne = 4096;

}  // namespace

SH_TEST(world_triggers_nearby) {
    const std::vector<wd::TriggerBox> boxes = {{0, 0, 2, 2, 1}, {40, 40, 1, 1, 0}, {-20, 0, 1, 1, 0}};
    const auto near = wd::nearby_triggers(10 * kOne, 0, boxes);
    SH_CHECK(near.size() == 1 && near[0] == 0);
    // The 16 m margin is inclusive.
    const auto edge = wd::nearby_triggers(-36 * kOne, 0, boxes);
    SH_CHECK(edge.size() == 1 && edge[0] == 2);
}

SH_TEST(world_triggers_sweep) {
    const std::vector<wd::TriggerBox> boxes = {{2, -1, 2, 2, 1}};
    // A move of 4 m along +x meets the box at 2 m: half the move.
    const wd::TriggerSweep s = wd::sweep_triggers(0, 0, 4 * kOne, 0, boxes);
    SH_CHECK(s.fraction == kOne / 2);
    SH_CHECK(s.height == -(kOne / 2) - 3 * kOne / 2);  // one half-metre step below 1.5 m
    const wd::SlopeForce f = wd::slope_force(s);
    SH_CHECK(f.distance == 2 * kOne);
    SH_CHECK(f.height == s.height);
    // Starting inside: no distance at all.
    const wd::TriggerSweep inside = wd::sweep_triggers(3 * kOne, 0, kOne, 0, boxes);
    SH_CHECK(inside.fraction == 0);
    // Nothing met.
    const wd::SlopeForce none = wd::slope_force(wd::sweep_triggers(0, 0, -kOne, 0, boxes));
    SH_CHECK(none.distance == 32 * kOne && none.height == -16 * kOne);
}

SH_TEST(world_triggers_slab_crossing) {
    SH_CHECK(wd::slab_crossing(16, 0, 4, 0, 100) == kOne);    // no movement on the axis
    SH_CHECK(wd::slab_crossing(16, 32, -4, 0, 100) == kOne);  // slab behind
    SH_CHECK(wd::slab_crossing(16, 32, 0, -1, 1) == 0);       // on the slab, inside the bounds
    SH_CHECK(wd::slab_crossing(0, 32, 16, -1, 1) == kOne / 2);
}

SH_TEST(world_chara_collidable) {
    const wd::CollisionStates states{0, 1, 4};
    const std::vector<wd::CollisionCandidate> c = {
        {true, false, 3}, {false, false, 3}, {true, true, 3}, {true, false, 0}, {true, false, 2}, {true, false, 1}};
    const auto all = wd::collidable_characters(c, nullptr, -1, false, states);
    SH_CHECK((all == std::vector<std::size_t>{0, 4, 5}));
    // With the player included separately, the player's state is left out; the asker is skipped.
    const auto with_player = wd::collidable_characters(c, &c[0], 0, true, states);
    SH_CHECK((with_player == std::vector<std::size_t>{4}));
    // A low-state asker meets only the same or higher states.
    const wd::CollisionCandidate low{true, false, 4};
    SH_CHECK(wd::collidable_characters(c, &low, -1, true, states).empty());
    // A phased asker meets nobody.
    const wd::CollisionCandidate phased{true, true, 3};
    SH_CHECK(wd::collidable_characters(c, &phased, -1, false, states).empty());
}

SH_TEST(world_chara_heard_and_scale) {
    SH_CHECK(wd::heard_loudness(kOne, 2 * kOne, 256 * kOne, 0, 0) == 2 * kOne - kOne);
    // Zero distance: measured from the offset (3-4-5 triangle, 5 m).
    SH_CHECK(wd::heard_loudness(kOne, kOne, 0, 3 * kOne, 4 * kOne) == kOne - (5 * kOne >> 8));
    SH_CHECK(wd::heard_loudness(kOne, 0, kOne, 0, 0) == 0);
    std::array<std::array<std::int16_t, 3>, 3> m{{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}};
    wd::scale_columns(m, 2048, 4096, 8192);
    SH_CHECK(m[0][0] == 2048 && m[1][1] == 4096 && m[2][2] == 8192);
}

SH_TEST(collision_trigger_unpacks_the_map_word) {
    // end 0, x -3, z 5, size 2 x 15, height 7.
    const std::uint32_t bits = (0u << 0) | ((static_cast<std::uint32_t>(-3) & 0x3FFu) << 1) | (5u << 11) |
                               (2u << 21) | (15u << 25) | (7u << 29);
    const sh::world::TriggerBox b = sh::world::unpack_trigger(sh::world::PackedTrigger{bits});
    SH_CHECK(b.x == -3 && b.z == 5 && b.size_x == 2 && b.size_z == 15 && b.height_steps == 7);
    SH_CHECK(!sh::world::is_list_end(sh::world::PackedTrigger{bits}));
    SH_CHECK(sh::world::is_list_end(sh::world::PackedTrigger{1}));
}

SH_TEST(collision_trigger_active_set_keeps_list_entries) {
    // Two triggers 1 m apart and one 100 m away, then the end marker.
    const sh::world::PackedTrigger list[] = {
        {(0u << 1) | (0u << 11) | (1u << 21) | (1u << 25)},
        {(1u << 1) | (0u << 11) | (1u << 21) | (1u << 25)},
        {(100u << 1) | (100u << 11) | (1u << 21) | (1u << 25)},
        {1u},
    };
    sh::world::ActiveTriggers active;
    sh::world::find_nearby_triggers(active, 0, 0, list);
    SH_CHECK(active.count == 2 && active.near[0] == &list[0] && active.near[1] == &list[1]);
}
