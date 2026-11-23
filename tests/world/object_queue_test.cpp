// SPDX-License-Identifier: GPL-3.0-or-later

#include "framework/sh_test.h"

#include <cstring>

#include "engine/world/held_item.h"
#include "engine/world/object_queue.h"

using namespace sh::world;

namespace {

ObjectModel model_named(const char* name, int library) {
    ObjectModel m{};
    m.set_name(name);
    m.library = library;
    return m;
}

}  // namespace

SH_TEST(object_queue_packs_pose_and_skips_duplicates) {
    ObjectQueue q;
    ObjectModel a = model_named("PAPER", 2);
    ObjectModel b = model_named("PAPER", 2);
    const auto none = [](ObjectModel&) { return 0; };
    q.add(a, {16 * 100 + 7, -16 * 3, 16 * 200}, {1025, -2048, 2047}, none);
    q.add(a, {16 * 100 + 15, -16 * 3, 16 * 200}, {1026, -2048, 2044}, none);  // same once truncated
    q.add(b, {16 * 100, -16 * 3, 16 * 200}, {1025, -2048, 2047}, none);       // other record: kept
    SH_CHECK(q.count() == 2);
    const QueuedObject o = q.at(0);
    SH_CHECK(o.model == &a && o.x == 100 && o.y == -3 && o.z == 200);
    // x and z angles keep quarter steps; the y angle is stored 12 bits wide.
    SH_CHECK(o.angle_x == 1024 && o.angle_y == -2048 && o.angle_z == 2044);
}

SH_TEST(object_queue_wraps_wide_values_and_never_matches_them) {
    ObjectQueue q;
    ObjectModel a = model_named("BOX", 3);
    const auto none = [](ObjectModel&) { return 0; };
    // y = 9000 * 16 does not fit 14 bits: stored wrapped, so the same request is queued again.
    q.add(a, {0, 9000 * 16, 0}, {0, 2048, 0}, none);
    q.add(a, {0, 9000 * 16, 0}, {0, 2048, 0}, none);
    SH_CHECK(q.count() == 2);
    SH_CHECK(q.at(0).y == 9000 - 16384);
    // The y angle 2048 wraps to -2048 in 12 bits.
    SH_CHECK(q.at(0).angle_y == -2048);
}

SH_TEST(object_queue_resolves_once_and_caps) {
    ObjectQueue q;
    int asked = 0;
    const auto resolve = [&](ObjectModel& m) {
        ++asked;
        return std::strncmp(m.name, "GONE", 4) == 0 ? 0 : 4;
    };
    ObjectModel gone = model_named("GONE", 0);
    q.add(gone, {0, 0, 0}, {0, 0, 0}, resolve);
    SH_CHECK(q.count() == 0 && gone.library == 0 && asked == 1);
    ObjectModel m[40];
    for (int i = 0; i < 40; ++i) {
        m[i] = model_named("ROCK", 0);
        q.add(m[i], {i * 16, 0, 0}, {0, 0, 0}, resolve);
    }
    SH_CHECK(q.count() == ObjectQueue::kCapacity);
    SH_CHECK(m[0].library == 4 && m[39].library == 0);  // a full queue does not look the model up
    int drawn = 0;
    q.drain([&](const QueuedObject&) { ++drawn; });
    SH_CHECK(drawn == ObjectQueue::kCapacity && q.count() == 0);
}

// The cell stream has 256 resident slots: a model of a cell in slot 200 (library 3 + 200) keeps its library.
SH_TEST(object_queue_keeps_a_cell_slot_past_the_signed_byte) {
    ObjectQueue q;
    const auto resolve = [](ObjectModel&) { return 3 + 200; };
    ObjectModel key = model_named("KEY", 0);
    q.add(key, {0, 0, 0}, {0, 0, 0}, resolve);
    SH_CHECK(q.count() == 1 && key.library == 203);
    SH_CHECK(q.at(0).model->library == 203);
}

SH_TEST(object_model_name_resets_library) {
    ObjectModel m = model_named("X", 5);
    m.set_name("LONGNAME_X");
    SH_CHECK(m.library == 0 && m.name_view() == "LONGNAME");
    m.set_name(nullptr);
    SH_CHECK(m.name_view().empty());
}

SH_TEST(held_item_state_follows_reads) {
    HeldItemState h;
    SH_CHECK(h.item() == 0 && !h.change(0));
    SH_CHECK(h.change(160));
    h.set_ticket(7, false);
    SH_CHECK(!h.settle_loaded());  // a texture read alone brings no model
    h.set_ticket(8, true);
    SH_CHECK(h.ticket() == 8 && h.settle_loaded());
    SH_CHECK(h.change(161));
    h.set_ticket(9, false);
    SH_CHECK(h.settle_loaded());  // the last model stays in hand
    h.hide();
    SH_CHECK(h.hidden());
    h.free();
    SH_CHECK(h.item() == HeldItemState::kNone && !h.hidden() && !h.settle_loaded());
}
