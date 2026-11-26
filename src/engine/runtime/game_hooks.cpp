// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/runtime/game_hooks.h"

#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_world.h"
#include "engine/hooks/hook_bus.h"
#include "engine/ids/ids.h"
#include "engine/log/sh_log.h"
#include "engine/runtime/pickup_runtime.h"
#include "engine/script/host_interfaces.h"
#include "engine/script/sh_script.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace sh::hooks {

namespace {

namespace gs = sh::game_state;
using gs::GameState;

bool listening(std::string_view name) { return bus().has(name); }

std::string short_id(const char* full) { return full != nullptr ? std::string(sh::ids::display(full)) : std::string(); }
std::string item_name(int item) { return short_id(gs::item_name(item)); }
std::string flag_name(int flag) { return short_id(gs::flag_name(flag)); }
std::string chara_name(int chara) { return short_id(gs::chara_name(chara)); }

std::string map_name() {
    const char* name = gs::map_name(gs::save_map_index());
    return name != nullptr ? name : "";
}

int game_state_now() { return gs::game_state_raw(); }
bool in_game_state() { return game_state_now() == static_cast<int>(GameState::InGame); }

std::uint64_t s_ticks = 0;

// ---- map / room ----

std::string s_map;  // the map map.load reported last
int s_room = -1;    // the room map.room_enter knows, -1 right after a map load

// ---- flag.set by comparison ----

std::vector<std::uint32_t> s_flags;
bool s_flags_valid = false;

void flags_take() {
    s_flags.assign(static_cast<std::size_t>(gs::save_flag_words()), 0u);
    for (std::size_t w = 0; w < s_flags.size(); ++w) {
        s_flags[w] = gs::save_flag_word(static_cast<int>(w));
    }
    s_flags_valid = true;
}

void flags_compare() {
    if (!listening("flag.set")) {
        s_flags_valid = false;  // a handler registered later starts from what is set then
        return;
    }
    const std::uint32_t* flags = gs::save_event_flags();
    if (flags == nullptr) {
        return;
    }
    if (!s_flags_valid) {
        flags_take();
        return;
    }
    if (const std::size_t words = static_cast<std::size_t>(gs::save_flag_words()); words > s_flags.size()) {
        s_flags.resize(words, 0u);
    }
    for (std::size_t w = 0; w < s_flags.size(); ++w) {
        const std::uint32_t now = gs::save_flag_word(static_cast<int>(w));
        const std::uint32_t changed = now ^ s_flags[w];
        if (changed == 0) {
            continue;
        }
        s_flags[w] = now;
        for (int b = 0; b < 32; ++b) {
            if ((changed & (1u << b)) == 0) {
                continue;
            }
            const std::string name = flag_name(static_cast<int>(w * 32 + static_cast<std::size_t>(b)));
            if (name.empty()) {
                continue;  // a bit no flag name covers cannot be addressed by a mod either
            }
            Event ev("flag.set");
            ev.set("flag", name, true).set("value", (now & (1u << b)) != 0, true);
            bus().emit(ev);
        }
    }
}

// ---- entity.spawn / entity.death by comparison (NPC slots 1..kNpcSlots) ----

std::array<int, gs::kNpcSlots> s_npc_chara{};
std::array<bool, gs::kNpcSlots> s_npc_alive{};
bool s_npcs_valid = false;

void npcs_take() {
    for (int i = 0; i < gs::kNpcSlots; ++i) {
        s_npc_chara[static_cast<std::size_t>(i)] = gs::chara_id(i + 1);
        s_npc_alive[static_cast<std::size_t>(i)] = gs::chara_health(i + 1) > 0;
    }
    s_npcs_valid = true;
}

void npcs_compare() {
    const bool spawn = listening("entity.spawn");
    const bool death = listening("entity.death");
    if (!spawn && !death) {
        s_npcs_valid = false;
        return;
    }
    if (!s_npcs_valid) {
        npcs_take();
        return;
    }
    for (int i = 0; i < gs::kNpcSlots; ++i) {
        const auto k = static_cast<std::size_t>(i);
        const int id = gs::chara_id(i + 1);
        const bool alive = gs::chara_health(i + 1) > 0;
        if (id != s_npc_chara[k]) {
            s_npc_chara[k] = id;
            s_npc_alive[k] = alive;
            if (id != gs::chara::kNone && spawn) {
                const sh::entities::Vec3 p = gs::chara_position(i + 1);
                Event ev("entity.spawn");
                ev.set("chara", chara_name(id), true)
                    .set("slot", std::int64_t{i + 1}, true)
                    .set("x", std::int64_t{p.x}, true)
                    .set("z", std::int64_t{p.z}, true);
                bus().emit(ev);
            }
            continue;
        }
        if (id == gs::chara::kNone) {
            continue;
        }
        if (s_npc_alive[k] && !alive && death) {
            Event ev("entity.death");
            ev.set("chara", chara_name(id), true).set("slot", std::int64_t{i + 1}, true);
            bus().emit(ev);
        }
        s_npc_alive[k] = alive;
    }
}

// ---- menu.open of the game's own screens ----

int s_game_state = -1;

void screens_compare() {
    const int state = game_state_now();
    if (state == s_game_state) {
        return;
    }
    s_game_state = state;
    const char* menu = state == static_cast<int>(GameState::InventoryScreen) ? "inventory"
                       : state == static_cast<int>(GameState::PaperMapScreen) ? "map"
                       : state == static_cast<int>(GameState::SaveScreen)     ? "save"
                                                                              : nullptr;
    if (menu != nullptr && listening("menu.open")) {
        Event ev("menu.open");
        ev.set("menu", std::string(menu), true);
        bus().emit(ev);
    }
}

// ---- cutscenes ----

std::string s_cutscene;         // started and not ended
bool s_cutscene_event = false;  // the game left free gameplay since it started

void cutscene_end() {
    if (s_cutscene.empty()) {
        return;
    }
    if (listening("cutscene.end")) {
        Event ev("cutscene.end");
        ev.set("id", s_cutscene, true);
        bus().emit(ev);
    }
    s_cutscene.clear();
    s_cutscene_event = false;
}

void cutscene_compare() {
    if (s_cutscene.empty() || !in_game_state()) {
        return;
    }
    if (gs::sys_state_raw() != static_cast<int>(gs::SysState::Gameplay)) {
        s_cutscene_event = true;
    } else if (s_cutscene_event) {
        cutscene_end();
    }
}

// ---- item.pickup: the count a handler set, kept from the start of the pickup to the add ----

struct PendingPickup {
    bool active = false;
    int item = -1;
    int flag = -1;
    int count = 0;
};
PendingPickup s_pickup;

// ---- map.event: one emit per run of a handler (it is asked every tick the event runs) ----

int s_event_param = -1;
std::uint64_t s_event_tick = 0;
bool s_event_allowed = true;

// ---- the queries of mods ----

constexpr int kStepHeight = 4096 * 3 / 4;  // Q12: more than this between two grounds is no walk
constexpr int kRayHeight = 4096 / 2;       // Q12: the walk ray runs half a metre above the ground

bool ground_at(int x, int z, int* y) {
    if (!gs::cell_has_collision(x, z)) {
        return false;  // no geometry loaded there
    }
    const gs::GroundSample s = gs::ground_sample(x, z);
    if (s.type == gs::kGroundTypeNone) {
        return false;
    }
    *y = s.height;
    return true;
}

class GameWorldQuery final : public sh::script::WorldQueryHost {
public:
    bool in_game() override {
        const int state = game_state_now();
        return state >= static_cast<int>(GameState::InGame) && state != static_cast<int>(GameState::DebugMoviePlayer);
    }
    int difficulty() override { return gs::save_difficulty(); }
    int player_facing() override { return gs::chara_rotation(0).y & 0xFFF; }

    bool walkable(int x, int z, const int* from, int* ground_y) override {
        int y = 0;
        if (!ground_at(x, z, &y)) {
            return false;
        }
        *ground_y = y;
        if (from == nullptr) {
            return true;
        }
        int from_y = 0;
        if (!ground_at(from[0], from[1], &from_y) || std::abs(y - from_y) > kStepHeight) {
            return false;
        }
        // Y grows downwards: above the ground is minus.
        return !gs::ray_blocked({from[0], from_y - kRayHeight, from[1]}, {x, y - kRayHeight, z});
    }

    int spawn(const char* chara, int x, int z, int rot, const char* ai, int state, char* err, int errCap) override {
        const int id = gs::chara_find(chara);
        if (id <= 0) {
            std::snprintf(err, static_cast<std::size_t>(errCap), "unknown character '%s'", chara);
            return -1;
        }
        const int mode = gs::spawn_ai_find(ai);
        if (mode < 0) {
            std::snprintf(err, static_cast<std::size_t>(errCap), "unknown ai '%s' (auto, full, fallback, none)", ai);
            return -1;
        }
        if (!in_game_state()) {
            std::snprintf(err, static_cast<std::size_t>(errCap), "not in game");
            return -1;
        }
        const int slot = gs::spawn_character_ai(id, state, x, z, rot, mode);
        if (slot < 0) {
            const char* note = gs::chara_spawn_note(id);
            std::snprintf(err, static_cast<std::size_t>(errCap), "%s cannot be spawned here%s%s", chara,
                          note[0] != '\0' ? ": " : " (no free slot or its files did not load)", note);
        }
        return slot;
    }
};

GameWorldQuery s_world_query;

}  // namespace

void install_game_script_hosts() { sh::script::set_world_query_host(&s_world_query); }

void game_boot() {
    bus().set_logger([](const std::string& message) {
        ShLog_Write(SH_LOGCAT_GAME, SH_LOGLV_WARN, "[HOOKS] %s", message.c_str());
    });
    if (listening("game.boot")) {
        Event ev("game.boot");
        bus().emit(ev);
    }
}

void game_new(int difficulty) {
    s_flags_valid = false;
    if (listening("game.new_game")) {
        Event ev("game.new_game");
        ev.set("difficulty", std::string(difficulty < 0 ? "easy" : difficulty > 0 ? "hard" : "normal"), true);
        bus().emit(ev);
    }
}

void game_load(int slot, bool quick) {
    s_flags_valid = false;
    if (listening("game.load")) {
        Event ev("game.load");
        ev.set("slot", std::int64_t{slot}, true).set("quick", quick, true);
        bus().emit(ev);
    }
}

void game_save(int slot, bool quick) {
    if (listening("game.save")) {
        Event ev("game.save");
        ev.set("slot", std::int64_t{slot}, true).set("quick", quick, true);
        bus().emit(ev);
    }
}

void game_frame_tick() {
    ++s_ticks;
    screens_compare();
    if (!in_game_state()) {
        return;
    }
    if (listening("game.tick")) {
        Event ev("game.tick");
        ev.set("tick", static_cast<std::int64_t>(s_ticks), true);
        bus().emit(ev);
    }
    flags_compare();
    npcs_compare();
    cutscene_compare();
}

void game_map_load(const char* map, bool restored) {
    const std::string name = map != nullptr ? map : "";
    if (!s_map.empty() && listening("map.unload")) {
        Event ev("map.unload");
        ev.set("map", s_map, true);
        bus().emit(ev);
    }
    s_map = name;
    s_room = -1;
    s_pickup = PendingPickup{};
    s_event_param = -1;
    cutscene_end();
    // The old map's characters are gone; whatever stands in a slot on the first tick has spawned.
    s_npc_chara.fill(gs::chara::kNone);
    s_npc_alive.fill(false);
    s_npcs_valid = true;
    if (restored) {
        s_flags_valid = false;
    }
    if (listening("map.load")) {
        Event ev("map.load");
        ev.set("map", name, true);
        bus().emit(ev);
    }
}

void game_room_update(int room) {
    if (room == s_room) {
        return;
    }
    const int prev = s_room;
    s_room = room;
    if (listening("map.room_enter")) {
        Event ev("map.room_enter");
        ev.set("map", s_map, true).set("room", std::int64_t{room}, true).set("prev_room", std::int64_t{prev}, true);
        bus().emit(ev);
    }
}

void game_cutscene_start(const char* game_path) {
    cutscene_end();
    std::string_view path = game_path != nullptr ? game_path : "";
    const std::size_t slash = path.find_last_of("/\\");
    if (slash != std::string_view::npos) {
        path.remove_prefix(slash + 1);
    }
    path = path.substr(0, path.find('.'));
    s_cutscene = std::string(path);
    s_cutscene_event = false;
    if (!s_cutscene.empty() && listening("cutscene.start")) {
        Event ev("cutscene.start");
        ev.set("id", s_cutscene, true);
        bus().emit(ev);
    }
}

bool game_item_pickup(int item, int count, int flag) {
    s_pickup = PendingPickup{};
    if (!listening("item.pickup")) {
        return true;
    }
    const sh::entities::Vec3 player = gs::chara_position(0);
    std::int64_t x = player.x;
    std::int64_t y = player.y;
    std::int64_t z = player.z;
    std::array<std::int32_t, 3> runtime{};
    std::int32_t px = 0;
    std::int32_t pz = 0;
    if (sh::world::runtime_pickups_active(nullptr, &runtime)) {
        // A mod's runtime pickup runs as a synthetic map event without a map point: report the item's own spot.
        x = runtime[0];
        y = runtime[1];
        z = runtime[2];
    } else if (gs::current_map_event_point(&px, &pz)) {
        x = px;
        z = pz;
    }
    Event ev("item.pickup", true);
    ev.set("item", item_name(item), true)
        .set("count", std::int64_t{count})
        .set("flag", flag_name(flag), true)
        .set("map", map_name(), true)
        .set("x", x, true)
        .set("y", y, true)
        .set("z", z, true);
    if (!bus().emit(ev)) {
        return false;
    }
    const std::int64_t wanted = ev.get_int("count", count);
    if (wanted != count) {
        // The inventory keeps counts in a byte.
        s_pickup = PendingPickup{true, item, flag, static_cast<int>(wanted < 0 ? 0 : wanted > 255 ? 255 : wanted)};
    }
    return true;
}

int game_item_pickup_count(int item, int flag, int count) {
    if (s_pickup.active && s_pickup.item == item && s_pickup.flag == flag) {
        return s_pickup.count;
    }
    return count;
}

void game_item_picked(int item, int count, int flag) {
    s_pickup = PendingPickup{};
    if (listening("item.picked")) {
        Event ev("item.picked");
        ev.set("item", item_name(item), true)
            .set("count", std::int64_t{count}, true)
            .set("flag", flag_name(flag), true)
            .set("map", map_name(), true);
        bus().emit(ev);
    }
}

bool game_item_use(int item) {
    if (!listening("item.use")) {
        return true;
    }
    Event ev("item.use", true);
    ev.set("item", item_name(item), true);
    return bus().emit(ev);
}

int game_item_given(int item, int count, const char* source) {
    if (!listening("item.given")) {
        return count;
    }
    Event ev("item.given", true);
    ev.set("item", item_name(item), true)
        .set("count", std::int64_t{count})
        .set("map", map_name(), true)
        .set("source", std::string(source != nullptr ? source : ""), true);
    if (!bus().emit(ev)) {
        return -1;
    }
    // The inventory keeps counts in a byte.
    const std::int64_t wanted = ev.get_int("count", count);
    return static_cast<int>(wanted < 0 ? 0 : wanted > 255 ? 255 : wanted);
}

bool game_quick_heal(int item, int health, int& amount) {
    if (!listening("player.quick_heal")) {
        return true;
    }
    Event ev("player.quick_heal", true);
    ev.set("item", item_name(item), true)
        .set("amount", std::int64_t{amount})
        .set("health", std::int64_t{health}, true);
    if (!bus().emit(ev)) {
        return false;
    }
    const std::int64_t heal = ev.get_int("amount", amount);
    amount = static_cast<int>(heal < 0 ? 0 : heal > INT32_MAX ? INT32_MAX : heal);
    return true;
}

namespace {

bool quick_key(const char* event, bool instant) {
    if (!listening(event)) {
        return true;
    }
    Event ev(event, true);
    ev.set("map", map_name(), true).set("instant", instant, true);
    return bus().emit(ev);
}

}  // namespace

bool game_quick_save(bool instant) { return quick_key("game.quick_save", instant); }
bool game_quick_load(bool instant) { return quick_key("game.quick_load", instant); }

int game_player_damage(int amount, int attacker_npc) {
    if (!listening("player.damage")) {
        return amount;
    }
    const bool attacker = attacker_npc >= 0 && attacker_npc < gs::kNpcSlots &&
                          gs::chara_id(attacker_npc + 1) != gs::chara::kNone;
    Event ev("player.damage", true);
    ev.set("amount", std::int64_t{amount})
        .set("source", attacker ? chara_name(gs::chara_id(attacker_npc + 1)) : std::string(), true)
        .set("source_slot", std::int64_t{attacker ? attacker_npc + 1 : -1}, true);
    if (!bus().emit(ev)) {
        return 0;
    }
    const std::int64_t taken = ev.get_int("amount", amount);
    return static_cast<int>(taken < 0 ? 0 : taken > INT32_MAX ? INT32_MAX : taken);
}

void game_player_death() {
    if (listening("player.death")) {
        Event ev("player.death");
        ev.set("map", map_name(), true);
        bus().emit(ev);
    }
}

bool game_map_event_allowed(int param) {
    const bool continuing = param == s_event_param && s_ticks <= s_event_tick + 1;
    s_event_param = param;
    s_event_tick = s_ticks;
    if (continuing) {
        return s_event_allowed;
    }
    s_event_allowed = true;
    if (listening("map.event")) {
        Event ev("map.event", true);
        ev.set("map", map_name(), true)
            .set("handler", std::string(ShScript_MapHandlerName(param)), true)
            .set("index", std::int64_t{param}, true);
        s_event_allowed = bus().emit(ev);
    }
    return s_event_allowed;
}

}  // namespace sh::hooks
