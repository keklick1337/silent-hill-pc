// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The map's hook script, gamedata/maps/<map>/hooks.lua, after the functions maps share
// (gamedata/scripts/map_hooks.lua, map_rooms.lua) in one Lua state. Loaded with the map itself, before any hook of
// the map runs (the music event runs during the load), and dropped with it: its locals live for one visit, as the
// map's own statics did. Mods extend each file (Host::run_layered).

#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace sh::script {
class Host;
}

namespace sh::maps {

class MapHooks {
public:
    MapHooks();
    ~MapHooks();

    // A map without a hooks.lua still gets the shared functions (its rooms.json may name a room rule there).
    void load(std::string_view map);
    // The loaded map's script defines the global.
    bool has(std::string_view name) const;
    // false when it does not, or the call failed (the script is dropped then).
    bool call(std::string_view name, long long a, long long b, long long c);
    // Two integer arguments and the integer result (room rules of rooms.json); false also when there is no integer.
    bool call_int(std::string_view name, long long a, long long b, long long* out);

private:
    std::string map_;
    std::unique_ptr<sh::script::Host> script_;
};

MapHooks& map_hooks();

// The music event of a map whose header leaves it empty and whose hooks.lua defines bgm(init): init true when the
// room's track is chosen, false for the per-tick mix.
void map_hooks_bgm(bool init);

}  // namespace sh::maps
