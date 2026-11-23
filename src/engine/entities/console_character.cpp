// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/entities/console_character.h"

#include <array>

namespace sh::entities::console {

namespace {

// On the console the upper body follows the player's record, and the NPC table follows the upper body: the record
// before the table starts this far into the player's record.
constexpr std::size_t kPlayerBlockBytes = kCharacterBytes + kUpperBodyBytes;
constexpr std::size_t kUnsetSlotStart = kPlayerBlockBytes - kCharacterBytes;

CharacterRecord s_unset_slot{};

std::array<std::uint8_t, kPlayerBlockBytes> player_block(const CharacterSlots& slots) {
    std::array<std::uint8_t, kPlayerBlockBytes> bytes{};
    const save::console::FieldWriter out(bytes);
    visit_character(out, 0, slots.player);
    visit_upper_body(out, kCharacterBytes, slots.player_upper);
    return bytes;
}

}  // namespace

CharacterRecord& unset_slot(const CharacterSlots& slots) {
    s_unset_slot = CharacterRecord{};
    const std::array<std::uint8_t, kPlayerBlockBytes> bytes = player_block(slots);
    visit_character(save::console::FieldReader(std::span<const std::uint8_t>(bytes).subspan(kUnsetSlotStart)), 0,
                    s_unset_slot);
    return s_unset_slot;
}

void unset_slot_commit(CharacterSlots& slots) {
    std::array<std::uint8_t, kPlayerBlockBytes> bytes = player_block(slots);
    visit_character(save::console::FieldWriter(std::span<std::uint8_t>(bytes).subspan(kUnsetSlotStart)), 0,
                    s_unset_slot);
    const save::console::FieldReader in(bytes);
    visit_character(in, 0, slots.player);
    visit_upper_body(in, kCharacterBytes, slots.player_upper);
}

}  // namespace sh::entities::console
