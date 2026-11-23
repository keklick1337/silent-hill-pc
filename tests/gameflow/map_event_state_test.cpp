// The engine's map event state: the map point record's packed word as the map data stores it (paper map 5 bits,
// weather on enter 4, loading screen 3, 4 unused, two 8-bit trigger parameters), and the state's starting values.

#include "engine/gameflow/map_event_state.h"

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

SH_TEST(map_point_record_unpacks_the_packed_word) {
    gf::MapPointRecord p;
    p.packed = 21u | (9u << 5) | (5u << 9) | (0xFu << 12) | (0x5Au << 16) | (0xC3u << 24);
    SH_CHECK(p.paper_map() == 21);
    SH_CHECK(p.weather_on_enter() == 9);
    SH_CHECK(p.loading_screen() == 5);
    SH_CHECK(p.param0() == 0x5A);
    SH_CHECK(p.param1() == 0xC3);

    // Every field at its widest, alone: no field reads its neighbours' bits.
    gf::MapPointRecord q;
    q.packed = 0x1Fu;
    SH_CHECK(q.paper_map() == 31 && q.weather_on_enter() == 0 && q.loading_screen() == 0);
    q.packed = 0xFu << 5;
    SH_CHECK(q.paper_map() == 0 && q.weather_on_enter() == 15 && q.loading_screen() == 0);
    q.packed = 0x7u << 9;
    SH_CHECK(q.weather_on_enter() == 0 && q.loading_screen() == 7 && q.param0() == 0);
    q.packed = 0xFFu << 24;
    SH_CHECK(q.param0() == 0 && q.param1() == 0xFF);
}

SH_TEST(map_event_state_starts_empty) {
    const gf::MapEventState s;
    SH_CHECK(s.event == nullptr);
    SH_CHECK(s.sys_state == 0 && s.param == 0 && s.used_item == 0);
    for (int i = 0; i < gf::MapEventState::kItemSlots; ++i) {
        SH_CHECK(s.item_events[static_cast<std::size_t>(i)] == nullptr && s.item_ids[static_cast<std::size_t>(i)] == 0);
    }
    // The event background's second picture sits at texture page (1, 21) with its palette at (224, 13).
    SH_CHECK(s.secondary_background.page[0] == 1 && s.secondary_background.page[1] == 21);
    SH_CHECK(s.secondary_background.clut_x == 224 && s.secondary_background.clut_y == 13);
    SH_CHECK(s.waypoints[1][7][2] == 0 && s.waypoint_headings[7] == 0);
}
