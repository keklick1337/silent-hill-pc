// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/world/skeleton.h"

#include <cstring>

using namespace sh::world;

namespace {

LmModel model_named(const char* name) {
    LmModel m{};
    std::memcpy(m.name, name, std::strlen(name) < 8 ? std::strlen(name) : 8);
    return m;
}

}  // namespace

SH_TEST(skeleton_from_model_links_every_model_in_draw_order) {
    LmModel models[3] = {model_named("00BODY"), model_named("05HEAD"), model_named("XXHAIR")};
    std::uint8_t order[3] = {2, 0, 1};
    LmHeader lm{};
    lm.magic = sh::formats::records::kLmMagic;
    lm.model_count = 3;
    lm.models = models;
    lm.model_order = order;

    static SkeletonRecord skel;
    skeleton_init(skel, skel.storage, kSkeletonMaxParts);
    SH_CHECK(skel.drawn == 1 && skel.draw_list == nullptr);
    skeleton_from_model(skel, &lm);
    SH_CHECK(skel.parts_used == 3);
    // Draw order follows the LM file's order list.
    SH_CHECK(skel.draw_list == &skel.storage[2]);
    SH_CHECK(skel.storage[2].next == &skel.storage[0]);
    SH_CHECK(skel.storage[0].next == &skel.storage[1]);
    SH_CHECK(skel.storage[1].next == nullptr);
    // Linking hides every part; set_visible shows them again.
    SH_CHECK((static_cast<std::uint32_t>(skel.storage[0].model.flags) & kPartHidden) != 0);
    skeleton_set_visible(skel, true);
    SH_CHECK((static_cast<std::uint32_t>(skel.storage[0].model.flags) & kPartHidden) == 0);

    skeleton_bone_index_from_names(skel);
    SH_CHECK(skel.storage[0].bone_index == 0);
    SH_CHECK(skel.storage[1].bone_index == 5);
    SH_CHECK(skel.storage[2].bone_index == 0);  // not two digits
    SH_CHECK(skel.storage[1].model.model == &models[1] && skel.storage[1].model.model_index == 1);
}

SH_TEST(part_model_assign_keeps_pointer_of_unconverted_file) {
    LmModel models[1] = {model_named("00A")};
    LmHeader lm{};
    lm.magic = 'x';
    lm.models = models;
    SkeletonPartModel part{};
    part_model_assign(part, &lm, 0);
    SH_CHECK(part.model_index == 0 && part.model == nullptr);
}
