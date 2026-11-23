// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Where a character's texture goes in video memory: the player's sheet for the player's
// characters, otherwise one of the character slots by the model's load index, each slot with
// its own palette row. Data: gamedata/config/chara_vram.json (a mod replaces the file whole);
// the game's character loader reads it.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::world {

struct VramImage {
    std::uint8_t tpage[2] = {0, 0};
    std::uint8_t u = 0;
    std::uint8_t v = 0;
    std::int16_t clut_x = 0;
    std::int16_t clut_y = 0;
};

struct CharaVramLayout {
    std::vector<std::string> player_characters;  // character names that use `player`
    VramImage player;
    // Per load index the page and position of the texture; an index past the list uses slot 0.
    // The palette x is clut_x + index * clut_step for the index as given (even past the list,
    // as the original computed it before falling back), y is clut_y.
    std::vector<VramImage> slots;
    std::int32_t clut_x = 0;
    std::int32_t clut_step = 0;
    std::int32_t clut_y = 0;

    static Result<CharaVramLayout> parse(std::string_view json);

    VramImage image(bool player_character, std::int32_t model_idx) const;
};

}  // namespace sh::world
