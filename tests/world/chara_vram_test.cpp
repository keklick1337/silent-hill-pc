// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/world/chara_vram.h"

using namespace sh::world;

namespace {

const char* kLayout = R"({
  "format": "sh-chara-vram",
  "player_characters": ["None", "Harry"],
  "player": {"tpage": [0, 27], "u": 0, "v": 0, "clut_x": 736, "clut_y": 480},
  "slots": [{"tpage": [0, 28], "u": 0, "v": 0}, {"tpage": [0, 28], "u": 0, "v": 128},
            {"tpage": [0, 29], "u": 0, "v": 0}, {"tpage": [0, 29], "u": 0, "v": 128}],
  "slot_clut": {"x": 704, "step": 16, "y": 464}
})";

}  // namespace

SH_TEST(chara_vram_slots) {
    auto r = CharaVramLayout::parse(kLayout);
    SH_CHECK(static_cast<bool>(r));
    const CharaVramLayout& l = r.value();
    SH_CHECK(l.player_characters.size() == 2);
    const VramImage p = l.image(true, 3);
    SH_CHECK(p.tpage[1] == 27 && p.clut_x == 736 && p.clut_y == 480);
    const VramImage s3 = l.image(false, 3);
    SH_CHECK(s3.tpage[1] == 29 && s3.v == 128 && s3.clut_x == 704 + 48 && s3.clut_y == 464);
    // Past the list: slot 0's page, but the palette still follows the index given.
    const VramImage s5 = l.image(false, 5);
    SH_CHECK(s5.tpage[1] == 28 && s5.v == 0 && s5.clut_x == 704 + 80);
    const VramImage neg = l.image(false, -1);
    SH_CHECK(neg.tpage[1] == 28 && neg.clut_x == 688);
    const VramImage far = l.image(false, 4096);  // 704 + 65536 wraps to 704 in 16 bits
    SH_CHECK(far.clut_x == 704);
}

SH_TEST(chara_vram_rejects_bad_files) {
    SH_CHECK(!CharaVramLayout::parse("{}"));
    SH_CHECK(!CharaVramLayout::parse(R"({"format":"sh-chara-vram","player_characters":[],
      "player":{"tpage":[0,27],"u":0,"v":0,"clut_x":0,"clut_y":0},"slots":[],"slot_clut":{"x":0,"step":0,"y":0}})"));
    SH_CHECK(!CharaVramLayout::parse(R"({"format":"sh-chara-vram","player_characters":[1],
      "player":{"tpage":[0,27],"u":0,"v":0,"clut_x":0,"clut_y":0},"slots":[{"tpage":[0,1],"u":0,"v":0}],
      "slot_clut":{"x":0,"step":0,"y":0}})"));
}
