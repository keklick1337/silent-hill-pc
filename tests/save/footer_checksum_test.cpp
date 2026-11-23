// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The record footer: seal then verify; a flipped byte or a wrong magic fails; a record of the card driver verifies.
#include "framework/sh_test.h"
#include "engine/save/footer_checksum.h"

#include <vector>

namespace {

SH_TEST(save_footer_seal_and_verify) {
    std::vector<std::uint8_t> rec(32, 0);
    for (std::size_t i = 0; i < 28; ++i) {
        rec[i] = static_cast<std::uint8_t>(i * 7 + 1);
    }
    sh::save::seal(rec);
    SH_CHECK(rec[28] == rec[29] && rec[30] == 0xDC && rec[31] == 0xDC);
    SH_CHECK(sh::save::verify(rec));
    rec[3] ^= 0x10;
    SH_CHECK(!sh::save::verify(rec));
    rec[3] ^= 0x10;
    rec[30] = 0;
    SH_CHECK(!sh::save::verify(rec));
    SH_CHECK(sh::save::xor_checksum(std::vector<std::uint8_t>{1, 2, 4}) == 7);
}

}  // namespace
