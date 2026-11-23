// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game-state API over the running map (sh::maps::map_runtime()): the event list and its points, starting an
// event, the NPC update table, the message layers of the language and the mods, Harry's reaction rows and the map
// load.

#include "engine/loader/active_map.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_map_overlay.h"
#include "engine/game_state/game_state_old_calls.h"
#include "engine/game_state/game_state_ui.h"
#include "engine/game_state/map_event_record.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"

#include "engine/gameflow/map_event_state.h"
#include "engine/maps/map_runtime.h"
#include "engine/script/named_api_calls.h"

extern "C" {
// Rows the reaction scan walks in the map's table.
int Pc_HarryReactions_RowScan(void);
}

namespace sh::game_state {

namespace {

sh::maps::MapRuntime& map() { return sh::maps::map_runtime(); }
static_assert(sizeof(MapEventRecord) == sizeof(sh::maps::MapEventRecord));
MapEventRecord* events() { return reinterpret_cast<MapEventRecord*>(map().events()); }

constexpr int kMapEventsMax = 256;  // the trigger scan's cap
constexpr int kItemNone = 0;        // the "unequipped" item number
constexpr int kTriggerEndOfArray = -1;  // the trigger type that ends the event list
constexpr int kActivationItem = 3;      // an event started by using an item

}  // namespace

// ---- the event table ----

bool map_events_loaded() { return map().loaded(); }

int map_event_count() {
    if (!map().loaded()) {
        return 0;
    }
    int n = 0;
    while (n < kMapEventsMax && events()[n].trigger_type() != kTriggerEndOfArray) {
        n++;
    }
    return n;
}

MapEvent map_event(int idx) {
    const MapEventRecord& e = events()[idx];
    MapEvent m;
    m.trigger_type = e.trigger_type();
    m.activation_type = e.activation_type();
    m.sys_state = e.sys_state();
    m.param = e.param();
    m.point = e.point;
    m.required_flag = e.required_flag;
    m.disabled_flag = e.disabled_flag;
    return m;
}

void map_event_place_player(int idx) {
    const MapEventRecord& e = events()[idx];
    const sh::gameflow::MapPointRecord& p = map().points()[e.point];
    place_player(p.x, p.z, nullptr);
}

void map_event_start(int idx) {
    MapEventRecord& e = events()[idx];
    if (e.sys_state() == static_cast<int>(SysState::EventSetFlag)) {
        const auto flag = static_cast<std::uint32_t>(static_cast<std::int32_t>(e.disabled_flag));
        sh::game::save_record().event_flags[static_cast<std::int16_t>(flag / 32)] |= 1u << (flag % 32);
        return;
    }
    sh::gameflow::MapEventState& state = sh::gameflow::map_event_state();
    state.event = &e;
    state.used_item = e.activation_type() == kActivationItem ? e.required_item : kItemNone;
    state.sys_state = e.sys_state();
    state.param = static_cast<std::uint32_t>(e.param());
    sh::game::session().set_sys_state_next(state.sys_state);
}

// ---- the NPC update table ----

bool chara_has_behaviour(int chara_id) { return map().chara_update(chara_id) != sh::maps::CharaUpdate::None; }

// ---- the map's message tables ----

bool map_header_loaded() { return map().loaded(); }
const char** map_messages() { return map().loaded() ? map().messages() : nullptr; }

void map_messages_install(MapMessageCopy copy, const char** messages) {
    map().install_messages(copy == MapMessageCopy::Mod ? sh::maps::MessageLayer::Mod : sh::maps::MessageLayer::Lang,
                           messages);
}


bool map_harry_anim_row(int idx, MapHarryAnimRow* out) {
    if (!map().loaded() || idx < 0 || idx >= Pc_HarryReactions_RowScan()) {
        return false;
    }
    const sh::maps::PlayerAnimRow* rows = map().player_anim_rows();
    if (rows == nullptr) {
        return false;
    }
    out->map_status = rows[idx].status;
    out->harry_status = rows[idx].harry_status;
    return true;
}

bool map_overlay_install(int map_index) { return sh::loader::load_active_map(map_index); }

}  // namespace sh::game_state
