// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/runtime/pickup_runtime.h"

#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_world.h"
#include "engine/log/sh_log.h"
#include "engine/world/runtime_pickups.h"
#include "i18n/text_bridge.h"

#include <map>
#include <vector>

namespace sh::world {

namespace gs = sh::game_state;

namespace {

constexpr std::int32_t kNoGround = 8 * 4096;  // the ground query's "nothing loaded here"

bool flag_set(int flag) { return gs::event_flag(flag); }

class GameResolver final : public PickupResolver {
public:
    Result<void> resolve(PickupSpec& spec, PickupResolved& out) override {
        out.item = gs::item_number(spec.item.c_str());
        if (out.item <= 0) {
            return Result<void>::fail("unknown item '" + spec.item + "'");
        }
        out.flag = gs::flag_number(spec.flag.c_str());
        if (out.flag <= 0) {
            return Result<void>::fail("unknown flag '" + spec.flag + "' (a mod declares its flags in flags.json or flags/*.json)");
        }
        out.common_kind = gs::common_pickup_number(spec.item.c_str());
        if (!spec.message.empty()) {
            out.message = Pc_TextMsgIndex(spec.message.c_str());
            if (out.message < 0) {
                return Result<void>::fail("no message '" + spec.message + "' in this map");
            }
        } else if (out.common_kind < 0) {
            return Result<void>::fail("item '" + spec.item + "' has no common pickup prompt (give message=)");
        } else if (spec.count > 0) {
            out.message = gs::common_pickup_message(out.common_kind);
        }
        if (spec.model.empty()) {
            if (const char* model = gs::item_world_model(spec.item.c_str())) {
                spec.model = model;
            }
        }
        return Result<void>::ok();
    }
};

// The draw record of each pickup by id; a removed pickup's record lives until the next map (this frame's draw list
// may still hold it).
struct Records {
    std::uint64_t revision = ~0ull;
    std::map<std::string, gs::PickupRecord> live;
};

Records& records() {
    static Records r;
    return r;
}

void sync_records() {
    Records& rec = records();
    const PickupRegistry& reg = pickups();
    if (rec.revision == reg.revision()) {
        return;
    }
    rec.revision = reg.revision();
    std::map<std::string, gs::PickupRecord> next;
    for (const Pickup& p : reg.all()) {
        if (p.spec.unseen) {
            continue;  // drawn by nobody here
        }
        auto it = rec.live.find(p.spec.id);
        gs::PickupRecord record = nullptr;
        if (it != rec.live.end()) {
            record = it->second;
            rec.live.erase(it);
        } else {
            record = gs::pickup_record_create(p.spec.model.substr(0, 8).c_str());
        }
        gs::pickup_record_set_pose(record, p.spec.position, p.spec.rotation);
        next.emplace(p.spec.id, record);
    }
    rec.live = std::move(next);  // the rest stay allocated (retired) until pickup_records_clear
}

std::array<std::int32_t, 3> player_position() {
    const sh::entities::Vec3 p = gs::chara_position(0);
    return {p.x, p.y, p.z};
}

// The running prompt: copied at its start, so a script removing the pickup meanwhile does not cut it short.
struct Running {
    bool active = false;
    std::string id;
    PickupResolved resolved;
    int count = 0;
    std::array<std::int32_t, 3> position{};
};

Running& running() {
    static Running r;
    return r;
}

}  // namespace

void runtime_pickups_begin_map(std::string_view map) {
    static GameResolver resolver;
    PickupRegistry& reg = pickups();
    reg.set_resolver(&resolver);
    reg.begin_map(map);
    Records& rec = records();
    rec.live.clear();
    rec.revision = ~0ull;
    gs::pickup_records_clear();
    running() = Running{};
}

void runtime_pickups_draw() {
    PickupRegistry& reg = pickups();
    if (reg.empty()) {
        return;
    }
    // Pickups placed on the ground wait for the collision under them.
    for (const Pickup& p : reg.all()) {
        if (!p.spec.ground) {
            continue;
        }
        const std::int32_t height = gs::ground_height(p.spec.position[0], p.spec.position[2]);
        if (height != kNoGround) {
            const std::string id = p.spec.id;
            reg.place_on_ground(id, height);
            break;  // the list may have changed; the next one waits a frame
        }
    }
    sync_records();
    const std::vector<std::size_t> shown = reg.visible(player_position(), flag_set);
    const Records& rec = records();
    for (const std::size_t i : shown) {
        auto it = rec.live.find(reg.all()[i].spec.id);
        if (it != rec.live.end()) {
            gs::pickup_record_draw(it->second);
        }
    }
}

bool runtime_pickups_try_start(const std::array<std::int32_t, 3>& player,
                               const std::function<bool(std::int32_t x, std::int32_t z)>& facing) {
    const PickupRegistry& reg = pickups();
    if (reg.empty()) {
        return false;
    }
    const Pickup* p = reg.reachable(player, flag_set, [&](const Pickup& c) {
        return facing(c.spec.position[0], c.spec.position[2]);
    });
    if (p == nullptr) {
        return false;
    }
    Running& r = running();
    r.active = true;
    r.id = p->spec.id;
    r.resolved = p->resolved;
    r.count = p->spec.count;
    r.position = p->spec.position;
    gs::start_pickup_event();
    SH_LOG("[PICKUP] %s: %s taken up (%s)", reg.map().c_str(), r.id.c_str(), p->spec.item.c_str());
    return true;
}

bool runtime_pickups_start(std::string_view id) {
    const PickupRegistry& reg = pickups();
    const Pickup* p = reg.present(id, flag_set);
    if (p == nullptr) {
        return false;
    }
    Running& r = running();
    r.active = true;
    r.id = p->spec.id;
    r.resolved = p->resolved;
    r.count = p->spec.count;
    r.position = p->spec.position;
    gs::start_pickup_event();
    SH_LOG("[PICKUP] %s: %s taken up by its script (%s)", reg.map().c_str(), r.id.c_str(), p->spec.item.c_str());
    return true;
}

bool runtime_pickups_active(std::string* id, std::array<std::int32_t, 3>* position) {
    const Running& r = running();
    if (!r.active || !gs::pickup_event_current()) {
        return false;
    }
    if (id != nullptr) {
        *id = r.id;
    }
    if (position != nullptr) {
        *position = r.position;
    }
    return true;
}

bool runtime_pickups_run_event() {
    const Running& r = running();
    if (!r.active || !gs::pickup_event_current()) {
        return false;
    }
    const PickupResolved& res = r.resolved;
    if (r.count <= 0 && res.common_kind >= 0) {
        gs::common_item_take(res.common_kind, res.flag);
    } else {
        gs::item_take(res.item, r.count > 0 ? r.count : 1, res.flag, res.message);
    }
    return true;
}

}  // namespace sh::world
