// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the effect hosts' link records (src/engine/effects/game/effect_link.h): the half-word packing of
// the effect slot words and the record layouts the effect writers share.

#include "engine/effects/game/effect_link.h"

#include <cstddef>
#include <cstdint>

#include "framework/sh_test.h"

namespace fg = sh::effects::game_link;

SH_TEST(effect_link_halves_sign_and_keep_the_other_half) {
    const std::uint32_t w = 0x8001FFFEu;
    SH_CHECK(fg::low_half(w) == -2);
    SH_CHECK(fg::high_half(w) == -32767);
    SH_CHECK(fg::with_low_half(w, 0x1234) == 0x80011234u);
    SH_CHECK(fg::with_high_half(w, -1) == 0xFFFFFFFEu);
    // Values wider than 16 bits are cut, as a 16-bit store cuts them.
    SH_CHECK(fg::with_low_half(0, 0x12345) == 0x2345u);
}

SH_TEST(effect_link_words_and_bytes) {
    const std::int32_t w = static_cast<std::int32_t>(0x7FFF8000u);
    SH_CHECK(fg::low_word(w) == -32768);
    SH_CHECK(fg::high_word(w) == 32767);
    SH_CHECK(fg::with_low_word(w, 5) == 0x7FFF0005);
    SH_CHECK(fg::with_high_word(w, -2) == static_cast<std::int32_t>(0xFFFE8000u));
    SH_CHECK(fg::byte_of(0xAABBCCDDu, 0) == 0xDD);
    SH_CHECK(fg::byte_of(0xAABBCCDDu, 3) == 0xAA);
    SH_CHECK(fg::with_byte(0xAABBCCDDu, 1, 0x11) == 0xAABB11DDu);
}

SH_TEST(effect_link_record_layouts) {
    SH_CHECK(sizeof(fg::WaterStreamBlock) == 0x7C);
    SH_CHECK(offsetof(fg::WaterStreamBlock, drops) == 0x74);
    SH_CHECK(offsetof(fg::packet::QuadGouraudTextured, r1) - offsetof(fg::packet::QuadGouraudTextured, r0) == 12);
    SH_CHECK(sizeof(fg::packet::QuadTextured) == sizeof(std::uintptr_t) + 4 + 36);
}
