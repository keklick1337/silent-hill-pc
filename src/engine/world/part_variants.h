// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Mesh part variants of a skeletal character (hand holding an item, alternative heads,
// a parasite on the back): a packed selector picks, per slot, which model parts of the
// skeleton are shown. The tables are data, gamedata/chara/<Name>/part_variants.json;
// the game applies the toggles to its bones.

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::world {

struct PartToggle {
    std::uint8_t part = 0;  // model index of the skeleton
    bool visible = false;
};

class PartVariants {
public:
    struct Variant {
        std::uint32_t value = 0;  // the slot's value that selects it (never 0)
        std::vector<std::uint8_t> hide;
        std::vector<std::uint8_t> show;
    };
    // value = (selector >> shift) & ((1 << bits) - 1); 0 leaves the slot untouched. Otherwise
    // the slot's `hide` list goes first, then the selected variant's hide and show lists.
    struct Slot {
        std::uint32_t shift = 0;
        std::uint32_t bits = 4;
        std::vector<std::uint8_t> hide;
        std::vector<Variant> variants;
    };

    static Result<PartVariants> parse(std::string_view json);

    // Appends the toggles of `selector` to `out`, in the order they apply (a later toggle of
    // the same part wins).
    void toggles(std::uint32_t selector, std::vector<PartToggle>& out) const;

    std::vector<Slot> slots;
};

}  // namespace sh::world
