// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The run-long words of the maps by readable name: what the map data, the map variables
// (map_vars.json "symbol") and the engine's own readers find when the loaded map's tables (tables.json) have no array
// of that name. On the PS1 these were the overlay's data; here one copy serves every map and lives through map
// changes. Two kinds:
//   - the engine's own block, declared by gamedata/config/map_globals.json (size, first values; mods patch it),
//     made on first use and kept for the whole run;
//   - a record the game owns, bound by name (bind).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sh::maps {

class MapGlobals {
public:
    // The address of `name`: a bound record or the declared block; nullptr when neither knows it.
    void* find(std::string_view name);

    // A record of the game under a readable name; it wins over a declared block.
    void bind(std::string_view name, void* address);

    // Declarations from a map_globals.json text (the base file with every mod's patch merged over it). Returns false
    // with *error set on a malformed text; well-formed entries before the error are kept. A name declared again
    // keeps its block (and address) and takes the new first values.
    bool declare_from_json(std::string_view text, std::string* error);

    std::size_t declared() const { return blocks_.size(); }
    // Set once the game data's declarations were read (by the map loader, before the first lookup).
    bool loaded() const { return loaded_; }
    void set_loaded() { loaded_ = true; }

private:
    struct Block {
        std::string name;
        std::vector<std::uint8_t> bytes;
    };
    struct Bound {
        std::string name;
        void* address;
    };
    bool loaded_ = false;
    std::vector<std::unique_ptr<Block>> blocks_;
    std::vector<Bound> bound_;
};

MapGlobals& map_globals();

}  // namespace sh::maps
