// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/world/held_items.h"

using namespace sh::world;

namespace {

const char* kFile = R"({
  "format": "sh-held-items",
  "image_shared": {"tpage": [0, 27], "u": 0, "v": 0, "clut_x": 736, "clut_y": 480},
  "image_own": {"tpage": [0, 27], "u": 48, "v": 224, "clut_x": 736, "clut_y": 498},
  "items": {
    "none": {"texture_name": "HERO"},
    "Handgun": {"texture_name": "HERO", "model": "ITEM/HANDGUN.PLM"},
    "CutscenePhone": {"texture_name": "PHONE", "texture": "ITEM/PHONE.TIM", "model": "ITEM/PHONE.PLM", "hand": "left"},
    "Bare": {}
  }
})";

}  // namespace

SH_TEST(held_items_parse_and_find) {
    auto r = HeldItems::parse(kFile);
    SH_CHECK(static_cast<bool>(r));
    const HeldItems& h = r.value();
    SH_CHECK(h.items.size() == 4);
    const HeldItemDef* gun = h.find("Handgun");
    SH_CHECK(gun != nullptr && gun->model == "ITEM/HANDGUN.PLM" && gun->texture.empty() && !gun->left_hand);
    SH_CHECK(gun != nullptr && gun->texture_name && *gun->texture_name == "HERO");
    SH_CHECK(&h.image_for(gun) == &h.image_shared);
    const HeldItemDef* phone = h.find("CutscenePhone");
    SH_CHECK(phone != nullptr && phone->left_hand && &h.image_for(phone) == &h.image_own);
    SH_CHECK(h.image_own.u == 48 && h.image_own.v == 224 && h.image_own.clut_y == 498 && h.image_own.tpage[1] == 27);
    const HeldItemDef* bare = h.find("Bare");
    SH_CHECK(bare != nullptr && !bare->texture_name && bare->model.empty());
    SH_CHECK(h.find("Katana") == nullptr);
    SH_CHECK(&h.image_for(nullptr) == &h.image_shared);
}

SH_TEST(held_items_rejects_bad_files) {
    SH_CHECK(!HeldItems::parse("{}"));
    SH_CHECK(!HeldItems::parse(R"({"format":"sh-held-items","items":{}})"));
    const char* bad_hand = R"({"format":"sh-held-items",
      "image_shared":{"tpage":[0,27],"u":0,"v":0,"clut_x":0,"clut_y":0},
      "image_own":{"tpage":[0,27],"u":0,"v":0,"clut_x":0,"clut_y":0},
      "items":{"X":{"hand":"middle"}}})";
    SH_CHECK(!HeldItems::parse(bad_hand));
    const char* bad_u = R"({"format":"sh-held-items",
      "image_shared":{"tpage":[0,27],"u":300,"v":0,"clut_x":0,"clut_y":0},
      "image_own":{"tpage":[0,27],"u":0,"v":0,"clut_x":0,"clut_y":0},
      "items":{}})";
    SH_CHECK(!HeldItems::parse(bad_u));
}
