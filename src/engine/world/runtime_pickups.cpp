// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/runtime_pickups.h"

#include <algorithm>

namespace sh::world {

namespace {

std::int64_t abs64(std::int64_t v) { return v < 0 ? -v : v; }

bool near_horizontally(const std::array<std::int32_t, 3>& a, const std::array<std::int32_t, 3>& b,
                       std::int32_t distance) {
    const std::int64_t dx = static_cast<std::int64_t>(a[0]) - b[0];
    const std::int64_t dz = static_cast<std::int64_t>(a[2]) - b[2];
    const std::int64_t d = distance;
    return abs64(dx) <= d && abs64(dz) <= d && dx * dx + dz * dz <= d * d;
}

bool same_storey(const std::array<std::int32_t, 3>& a, const std::array<std::int32_t, 3>& b) {
    return abs64(static_cast<std::int64_t>(a[1]) - b[1]) <= PickupRegistry::kStoreyHeight;
}

bool taken(const Pickup& p, const PickupRegistry::FlagQuery& flag_set) {
    return p.resolved.flag >= 0 && flag_set && flag_set(p.resolved.flag);
}

// Neither drawn nor reachable: taken, or still waiting for its ground height.
bool absent(const Pickup& p, const PickupRegistry::FlagQuery& flag_set) {
    return p.spec.ground || taken(p, flag_set);
}

}  // namespace

void PickupRegistry::begin_map(std::string_view map) {
    // A waiting message is a number of the map being left.
    clear_script_message();
    map_ = std::string(map);
    map_loaded_ = !map_.empty();
    if (!pickups_.empty()) {
        pickups_.clear();
        ++revision_;
    }
}

Result<std::string> PickupRegistry::create(PickupSpec spec) {
    if (!map_loaded_) {
        return Result<std::string>::fail("no map is loaded");
    }
    if (spec.item.empty()) {
        return Result<std::string>::fail("a pickup needs an item");
    }
    if (spec.flag.empty()) {
        return Result<std::string>::fail("a pickup needs a flag (it is set when the item is taken)");
    }
    if (pickups_.size() >= kMaxPickups) {
        return Result<std::string>::fail("too many pickups on this map (" + std::to_string(kMaxPickups) + ")");
    }
    if (spec.owner.empty()) {
        spec.owner = "silenthill";
    }
    if (spec.id.empty()) {
        // Numbered per owner so that one mod's choice does not shift another's ids.
        int n = 1;
        std::string id;
        do {
            id = spec.owner + ":pickup" + std::to_string(n++);
        } while (find(id) != nullptr);
        spec.id = std::move(id);
    } else if (find(spec.id) != nullptr) {
        return Result<std::string>::fail("a pickup '" + spec.id + "' already exists on " + map_);
    }
    if (spec.count < 0) {
        return Result<std::string>::fail("count must be positive");
    }
    Pickup p;
    if (resolver_ != nullptr) {
        Result<void> ok = resolver_->resolve(spec, p.resolved);
        if (!ok) {
            return Result<std::string>::fail(ok.error());
        }
    }
    if (spec.model.empty()) {
        return Result<std::string>::fail("no world model for item '" + spec.item + "' (give model=)");
    }
    if (spec.model.size() > 8) {
        return Result<std::string>::fail("model name '" + spec.model + "' is longer than 8 characters");
    }
    p.spec = std::move(spec);
    p.serial = ++serial_;
    pickups_.push_back(std::move(p));
    ++revision_;
    return Result<std::string>::ok(pickups_.back().spec.id);
}

bool PickupRegistry::remove(std::string_view id) {
    const auto it = std::find_if(pickups_.begin(), pickups_.end(), [&](const Pickup& p) { return p.spec.id == id; });
    if (it == pickups_.end()) {
        return false;
    }
    pickups_.erase(it);
    ++revision_;
    return true;
}

bool PickupRegistry::place_on_ground(std::string_view id, std::int32_t height) {
    for (Pickup& p : pickups_) {
        if (p.spec.id == id) {
            p.spec.position[1] = height;
            p.spec.ground = false;
            ++revision_;
            return true;
        }
    }
    return false;
}

std::size_t PickupRegistry::remove_owner(std::string_view owner) {
    const auto before = pickups_.size();
    std::erase_if(pickups_, [&](const Pickup& p) { return p.spec.owner == owner; });
    const std::size_t removed = before - pickups_.size();
    if (removed != 0) {
        ++revision_;
    }
    return removed;
}

const Pickup* PickupRegistry::find(std::string_view id) const {
    for (const Pickup& p : pickups_) {
        if (p.spec.id == id) {
            return &p;
        }
    }
    return nullptr;
}

std::vector<std::size_t> PickupRegistry::visible(const std::array<std::int32_t, 3>& player,
                                                 const FlagQuery& flag_set) const {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < pickups_.size(); ++i) {
        const Pickup& p = pickups_[i];
        if (absent(p, flag_set) || !same_storey(p.spec.position, player) ||
            !near_horizontally(p.spec.position, player, kDrawDistance)) {
            continue;
        }
        out.push_back(i);
    }
    return out;
}

const Pickup* PickupRegistry::reachable(const std::array<std::int32_t, 3>& player, const FlagQuery& flag_set,
                                        const std::function<bool(const Pickup&)>& in_reach) const {
    for (const Pickup& p : pickups_) {
        if (absent(p, flag_set) || !same_storey(p.spec.position, player)) {
            continue;
        }
        if (in_reach && in_reach(p)) {
            return &p;
        }
    }
    return nullptr;
}

namespace {
int g_script_message = -1;
}  // namespace

void request_script_message(int message) { g_script_message = message; }
int pending_script_message() { return g_script_message; }
void clear_script_message() { g_script_message = -1; }

PickupRegistry& pickups() {
    static PickupRegistry registry;
    return registry;
}

}  // namespace sh::world
