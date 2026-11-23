// SPDX-License-Identifier: GPL-3.0-or-later
// Persistent remains over the running game -- see remains_host.h.
//
// A body is noted while it lies in the world (every tick, so the pose is its last one) and written to the store
// when the game lets it go: the director frees a corpse the player walked away from, a room change wipes every
// slot. A stored body near the player stands in one of the engine's own NPC slots (past the room spawner's 32) as
// a statue: no behaviour, no collision, negative health (nothing targets it), posed at the keyframe its death
// ended on. The room spawner's slots, rows and kill bits are never touched, so spawning goes on as in the original.
// A body's pools are the map's body pool slots; they go back on the floor as body pools of an NPC index no slot
// has, which the pool drawer keeps for good.

#include "engine/entities/game/remains_host.h"

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

#include "engine/chara/runtime/chara_data.h"
#include "engine/chara/runtime/chara_spawn.h"
#include "engine/config/pc_config.h"
#include "engine/effects/combat_effects.h"
#include "engine/effects/map_effect_view.h"
#include "engine/entities/entity_world.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_world.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/log/sh_log.h"
#include "engine/player/player.h"

extern "C" std::int32_t MapOverlay_SlotAlloc(std::int32_t kind);

namespace sh::entities::game::remains {

namespace {

namespace fx = sh::effects;
namespace gs = sh::game_state;
namespace link = sh::gameflow::game;

constexpr int kSlotCount = kNpcSlotCount;
constexpr int kLegacySlots = 32;
// Bodies near the player are looked over this often (ticks), a few at a time.
constexpr int kStreamEvery = 8;
constexpr int kStandPerPass = 2;
// A pool that has spread to its full size, and the owner of a pool that outlives its body: past the NPC slots, so
// the drawer never finds the owner gone and note_lying() never takes the pool for a lying body's.
constexpr std::uint32_t kPoolGrown = 20480;
constexpr std::uint8_t kPoolFollowsNobody = 255;

struct Lying {
    CorpseRemains corpse;
    std::vector<BloodStain> stains;
};

struct State {
    RemainsStore store;
    std::array<std::optional<Lying>, kSlotCount> lying;     // bodies the game still holds, by NPC slot
    std::array<std::optional<CorpseRemains>, kSlotCount> statues;  // stored bodies standing in the world
    std::vector<Vec3> placed;   // stored pools on the floor of this room
    std::string map;            // the map the noted bodies lie on
    int ticks = 0;
    // Between a load or a new game and the next room: the world on screen is not the store's.
    bool waiting_for_room = true;
};

State& state() {
    static State s;
    return s;
}

CharacterRecord& npc(int slot) { return sh::game::session().system.characters.npcs[slot]; }

std::string current_map() {
    const char* name = gs::map_name(gs::current_map());
    return name != nullptr ? name : "";
}

bool same_body(const CorpseRemains& a, const CorpseRemains& b) {
    return a.chara == b.chara && a.position == b.position;
}

bool standing(const State& s, const CorpseRemains& c) {
    for (const std::optional<CorpseRemains>& statue : s.statues) {
        if (statue && same_body(*statue, c)) {
            return true;
        }
    }
    return false;
}

// In an interior drawn room by room a body belongs to its room; outdoors the distance alone decides.
bool room_matches(int room) { return !link::map_chunked_interior() || room == gs::save_room(); }

void player_xz(std::int32_t& x, std::int32_t& z) {
    x = static_cast<std::int32_t>(sh::game::player().get(sh::game::PlayerVar::PositionX));
    z = static_cast<std::int32_t>(sh::game::player().get(sh::game::PlayerVar::PositionZ));
}

void place_stain(State& s, const BloodStain& stain) {
    for (const Vec3& p : s.placed) {
        if (p == stain.position) {
            return;
        }
    }
    // Half of the table stays free for the effects of the fights to come.
    if (static_cast<int>(s.placed.size()) >= fx::effect_slot_count() / 2) {
        return;
    }
    const std::int32_t slot = MapOverlay_SlotAlloc(fx::combat_kind::kBodyPool);
    if (slot < 0) {
        return;
    }
    fx::EffectSlot e;
    e.kind = fx::combat_kind::kBodyPool;
    e.x = stain.position.x;
    e.y = static_cast<std::int16_t>(stain.position.y);
    e.z = stain.position.z;
    e.flags = static_cast<std::uint8_t>(stain.shape & 3);
    fx::set_hi16(e.a, stain.size);
    fx::set_byte(e.a, 1, stain.color);
    fx::set_byte(e.a, 0, kPoolFollowsNobody);
    e.b = kPoolGrown;
    fx::store_effect_slot(slot, e);
    s.placed.push_back(stain.position);
}

void keep(State& s, int slot, bool place_pools) {
    std::optional<Lying>& lying = s.lying[static_cast<std::size_t>(slot)];
    if (!lying || s.map.empty()) {
        lying.reset();
        return;
    }
    s.store.add_corpse(s.map, lying->corpse);
    for (const BloodStain& stain : lying->stains) {
        s.store.add_stain(s.map, stain);
        if (place_pools) {
            place_stain(s, stain);
        }
    }
    SH_LOG("[REMAINS] %s kept on %s at %.2f %.2f (%d pool(s))", lying->corpse.chara.c_str(), s.map.c_str(),
           static_cast<double>(lying->corpse.position.x) / 4096.0, static_cast<double>(lying->corpse.position.z) / 4096.0,
           static_cast<int>(lying->stains.size()));
    lying.reset();
}

void note_lying(State& s, bool (*is_monster)(int)) {
    // The body pools on the floor, by the NPC slot they belong to.
    std::array<std::vector<BloodStain>, kSlotCount> pools;
    const int room = gs::save_room();
    const int count = fx::effect_slot_count();
    for (int i = 0; i < count; ++i) {
        const fx::EffectSlot e = fx::load_effect_slot(i);
        const int owner = fx::byte_of(e.a, 0);
        if (e.kind != fx::combat_kind::kBodyPool || owner >= kSlotCount) {
            continue;
        }
        BloodStain stain;
        stain.room = room;
        stain.position = {e.x, e.y, e.z};
        stain.size = static_cast<std::uint16_t>(fx::hi16(e.a));
        stain.color = fx::byte_of(e.a, 1);
        stain.shape = static_cast<std::uint8_t>(e.flags & 3);
        pools[static_cast<std::size_t>(owner)].push_back(stain);
    }
    for (int slot = 0; slot < kSlotCount; ++slot) {
        const auto at = static_cast<std::size_t>(slot);
        const CharacterRecord& r = npc(slot);
        const char* name = gs::chara_name(r.model.chara_id);
        if (s.statues[at] || r.model.chara_id == 0 || r.health >= 0 || name == nullptr ||
            !is_monster(r.model.chara_id)) {
            s.lying[at].reset();
            continue;
        }
        Lying lying;
        lying.corpse.chara = name;
        lying.corpse.room = room;
        lying.corpse.position = r.position;
        lying.corpse.rotation = r.rotation;
        lying.corpse.clip_status = r.model.anim.status;
        lying.corpse.clip_time = r.model.anim.time;
        lying.corpse.keyframe = r.model.anim.keyframe;
        lying.corpse.palette = r.model.palette;
        lying.corpse.timer = r.timer;
        lying.corpse.variant = r.type_variant;
        lying.stains = std::move(pools[at]);
        s.lying[at] = std::move(lying);
    }
}

void remove_statue(State& s, int slot) {
    const auto at = static_cast<std::size_t>(slot);
    if (!s.statues[at]) {
        return;
    }
    s.statues[at].reset();
    if (npc(slot).model.chara_id != 0) {
        gs::characters().remove(slot + 1);
    }
}

bool stand(State& s, const CorpseRemains& c) {
    const int chara = Pc_CharaIdFind(c.chara.c_str());
    if (chara <= 0) {
        return false;  // a character this build does not have (a mod that is gone): the record waits
    }
    const int created = Pc_Spawn_CharacterAi(chara, 0, c.position.x, c.position.z, c.rotation.y, PcSpawnAi_None);
    if (created < 1) {
        return false;  // its model is still loading, or every slot is taken
    }
    const int slot = created - 1;
    if (slot < kLegacySlots) {
        // The engine's slots are full: the room spawner's are not ours to take.
        gs::characters().remove(created);
        return false;
    }
    CharacterRecord& r = npc(slot);
    r.position = c.position;
    r.rotation = c.rotation;
    r.model.palette = c.palette;
    r.model.anim.status = c.clip_status;
    r.model.anim.time = c.clip_time;
    r.model.anim.keyframe = c.keyframe;
    r.timer = c.timer;
    r.type_variant = c.variant;
    r.health = -1;
    r.flags = static_cast<std::int16_t>(r.flags | kFlagDead | kFlagCameraExcluded | kFlagNoRadioStatic);
    s.statues[static_cast<std::size_t>(slot)] = c;
    return true;
}

void stream(State& s) {
    const RemainsReach reach;
    std::int32_t px = 0, pz = 0;
    player_xz(px, pz);
    for (int slot = 0; slot < kSlotCount; ++slot) {
        const std::optional<CorpseRemains>& statue = s.statues[static_cast<std::size_t>(slot)];
        if (!statue) {
            continue;
        }
        if (npc(slot).model.chara_id == 0) {
            s.statues[static_cast<std::size_t>(slot)].reset();  // something else cleared the slot
        } else if (!room_matches(statue->room) ||
                   !remains_within(statue->position.x, statue->position.z, px, pz, reach.hide)) {
            remove_statue(s, slot);
        }
    }
    const MapRemains* here = s.store.find(s.map);
    if (here == nullptr) {
        return;
    }
    int stood = 0;
    for (const CorpseRemains& c : here->corpses) {
        if (stood >= kStandPerPass) {
            break;
        }
        if (room_matches(c.room) && remains_within(c.position.x, c.position.z, px, pz, reach.show) &&
            !standing(s, c) && stand(s, c)) {
            ++stood;
        }
    }
}

void remove_all_statues(State& s) {
    for (int slot = 0; slot < kSlotCount; ++slot) {
        remove_statue(s, slot);
    }
}

}  // namespace

RemainsStore& store() { return state().store; }

bool enabled() { return g_PcConfig.persistentRemains != 0; }

bool is_statue(int slot) {
    return slot >= 0 && slot < kSlotCount && state().statues[static_cast<std::size_t>(slot)].has_value();
}

void tick(bool (*is_monster)(int chara)) {
    State& s = state();
    if (!enabled()) {
        // Switched off in the options: the statues go, the store waits.
        remove_all_statues(s);
        for (std::optional<Lying>& lying : s.lying) {
            lying.reset();
        }
        return;
    }
    if (s.waiting_for_room) {
        return;
    }
    s.map = current_map();
    note_lying(s, is_monster);
    if (s.ticks++ % kStreamEvery == 0) {
        stream(s);
    }
}

void slot_freed(int slot) {
    State& s = state();
    if (slot < 0 || slot >= kSlotCount) {
        return;
    }
    if (s.statues[static_cast<std::size_t>(slot)]) {
        s.statues[static_cast<std::size_t>(slot)].reset();
        return;
    }
    if (enabled() && !s.waiting_for_room) {
        keep(s, slot, true);
    } else {
        s.lying[static_cast<std::size_t>(slot)].reset();
    }
}

void room_left() {
    State& s = state();
    for (int slot = 0; slot < kSlotCount; ++slot) {
        if (enabled() && !s.waiting_for_room) {
            keep(s, slot, false);
        }
        s.lying[static_cast<std::size_t>(slot)].reset();
        s.statues[static_cast<std::size_t>(slot)].reset();  // the caller wipes the records
    }
    s.placed.clear();
}

void room_entered() {
    State& s = state();
    s.waiting_for_room = false;
    s.placed.clear();
    s.ticks = 0;
    s.map = current_map();
    if (!enabled()) {
        return;
    }
    const MapRemains* here = s.store.find(s.map);

    if (here == nullptr) {
        return;
    }
    // Nearest first: the table has room for so many.
    std::int32_t px = 0, pz = 0;
    player_xz(px, pz);
    std::vector<const BloodStain*> order;
    for (const BloodStain& stain : here->stains) {
        if (room_matches(stain.room)) {
            order.push_back(&stain);
        }
    }
    const auto distance = [&](const BloodStain* stain) {
        const std::int64_t dx = static_cast<std::int64_t>(stain->position.x) - px;
        const std::int64_t dz = static_cast<std::int64_t>(stain->position.z) - pz;
        return dx * dx + dz * dz;
    };
    std::stable_sort(order.begin(), order.end(),
                     [&](const BloodStain* a, const BloodStain* b) { return distance(a) < distance(b); });
    for (const BloodStain* stain : order) {
        place_stain(s, *stain);
    }
    SH_DBG("[REMAINS] room %d of %s: %d body(ies) and %d pool(s) stored for the map, %d pool(s) laid here",
           gs::save_room(), s.map.c_str(), static_cast<int>(here->corpses.size()),
           static_cast<int>(here->stains.size()), static_cast<int>(s.placed.size()));
}

void session_reset() {
    State& s = state();
    s.store.clear();
    for (int slot = 0; slot < kSlotCount; ++slot) {
        s.lying[static_cast<std::size_t>(slot)].reset();
    }
    s.waiting_for_room = true;
}

std::string save_text() {
    const State& s = state();
    // The bodies the game still holds lie there as well: a load must find them.
    RemainsStore st = s.store;
    if (enabled() && !s.waiting_for_room && !s.map.empty()) {
        for (const std::optional<Lying>& lying : s.lying) {
            if (!lying) {
                continue;
            }
            st.add_corpse(s.map, lying->corpse);
            for (const BloodStain& stain : lying->stains) {
                st.add_stain(s.map, stain);
            }
        }
    }
    for (const auto& [name, m] : st.maps()) {
        if (!m.corpses.empty() || !m.stains.empty()) {
            return st.to_json();
        }
    }
    return {};
}

void load_text(std::string_view text) {
    State& s = state();
    session_reset();
    if (text.empty()) {
        return;
    }
    Result<RemainsStore> loaded = RemainsStore::from_json(text);
    if (!loaded) {
        SH_ERR("[REMAINS] the save's remains are unreadable (%s): nothing is restored", loaded.error().c_str());
        return;
    }
    s.store = std::move(loaded.value());
}

}  // namespace sh::entities::game::remains
