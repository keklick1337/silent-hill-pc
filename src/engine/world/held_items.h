// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// What the player character holds in a hand (a weapon, a cutscene object): per item the model
// file, the texture file and the material name the model's texture binds to, and the hand.
// Data: gamedata/config/held_items.json (a mod replaces the file whole); the game loads and draws
// it.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::world {

// Where an item texture goes in video memory.
struct HeldItemImage {
    std::uint8_t tpage[2] = {0, 0};
    std::uint8_t u = 0;
    std::uint8_t v = 0;
    std::int16_t clut_x = 0;
    std::int16_t clut_y = 0;
};

struct HeldItemDef {
    std::string item;                          // item name (gamedata/items); "none" for an empty hand
    std::optional<std::string> texture_name;   // material name; absent keeps the previous one
    std::string texture;                       // game file of the texture; empty: none to read
    std::string model;                         // game file of the model; empty: nothing drawn
    bool left_hand = false;
};

struct HeldItems {
    HeldItemImage image_shared;    // an item without its own texture (the character's sheet)
    HeldItemImage image_own;       // an item that reads its own texture file
    std::vector<HeldItemDef> items;

    static Result<HeldItems> parse(std::string_view json);

    // nullptr: the item has no entry (nothing to read, the material name stays).
    const HeldItemDef* find(std::string_view item) const;
    const HeldItemImage& image_for(const HeldItemDef* def) const {
        return def != nullptr && !def->texture.empty() ? image_own : image_shared;
    }
};

}  // namespace sh::world
