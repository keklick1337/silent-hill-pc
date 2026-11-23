// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/runtime/map_hooks.h"

#include "engine/game_state/game_state.h"
#include "engine/log/sh_log.h"
#include "engine/script/script_host.h"

namespace sh::maps {

namespace {

// One layer set of `relative`; *own: some layer existed.
bool run(sh::script::Host& host, const std::string& map, const std::string& relative, bool* own) {
    const Result<bool> ran = host.run_layered(relative);
    if (!ran) {
        SH_ERR("[MAPHOOKS] %s: %s: %s (the map's hooks are off)", map.c_str(), relative.c_str(), ran.error().c_str());
        return false;
    }
    if (own != nullptr) {
        *own = ran.value();
    }
    return true;
}

}  // namespace

MapHooks::MapHooks() = default;
MapHooks::~MapHooks() = default;

MapHooks& map_hooks() {
    static MapHooks hooks;
    return hooks;
}

void MapHooks::load(std::string_view map) {
    script_.reset();
    map_ = std::string(map);
    if (map_.empty()) {
        return;
    }
    const std::string relative = "maps/" + map_ + "/hooks.lua";
    bool own = false;
    auto host = std::make_unique<sh::script::Host>(sh::script::game_host_config());
    if (!run(*host, map_, "scripts/map_hooks.lua", nullptr) || !run(*host, map_, "scripts/map_rooms.lua", nullptr) ||
        !run(*host, map_, relative, &own)) {
        return;
    }
    script_ = std::move(host);
    SH_LOG("[MAPHOOKS] %s: script %s", map_.c_str(), own ? relative.c_str() : "(shared functions only)");
}

bool MapHooks::has(std::string_view name) const {
    const char* loaded = sh::game_state::map_overlay_name();
    return script_ != nullptr && loaded != nullptr && map_ == loaded && script_->has_global(name);
}

bool MapHooks::call(std::string_view name, long long a, long long b, long long c) {
    if (!has(name)) {
        return false;
    }
    if (!script_->call_global_ints(name, {a, b, c})) {
        SH_ERR("[MAPHOOKS] %s: hooks.lua %.*s failed, the map's hooks are off", map_.c_str(),
               static_cast<int>(name.size()), name.data());
        script_.reset();
        return false;
    }
    return true;
}

bool MapHooks::call_int(std::string_view name, long long a, long long b, long long* out) {
    if (!has(name)) {
        return false;
    }
    if (!script_->call_global_int(name, {a, b}, out)) {
        SH_ERR("[MAPHOOKS] %s: hooks.lua %.*s failed or returned no integer, the map's hooks are off", map_.c_str(),
               static_cast<int>(name.size()), name.data());
        script_.reset();
        return false;
    }
    return true;
}

void map_hooks_bgm(bool init) { map_hooks().call("bgm", init ? 1 : 0, 0, 0); }

}  // namespace sh::maps
