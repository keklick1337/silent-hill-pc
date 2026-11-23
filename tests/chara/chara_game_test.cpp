// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The character hosts' engine parts that need no game: the bone edits (src/engine/chara/game/bone_ops.cpp) and the
// map cast helpers over a record (map_cast.cpp). The link to the game is stubbed here (no clip tables).

#include "framework/sh_test.h"

#include "engine/chara/game/bone_ops.h"
#include "engine/chara/game/chara_link.h"
#include "engine/chara/game/chara_records.h"
#include "engine/chara/game/map_cast.h"
#include "engine/loop/frame_view.h"
#include "engine/math/fixed_geometry.h"

#include <cstdint>
#include <cstring>

namespace cg = sh::chara::game;
namespace ent = sh::entities;

// --- stubs of the link and of the game's clock (the game provides them) ---

namespace sh::chara::game::link {
ClipTableRef anim_table(const char*) { return {}; }
ClipPlayback clip_playback_blend() { return nullptr; }
ClipPlayback clip_playback_once() { return nullptr; }
ClipPlayback clip_playback_loop() { return nullptr; }
}  // namespace sh::chara::game::link

namespace sh::loop {
FrameTiming& frame_timing() {
    static FrameTiming timing = {.delta = 68};
    return timing;
}
}  // namespace sh::loop

extern "C" std::int32_t Anim_DurationGet(void*, void*) { return 0; }
// No game data file in the tests.
extern "C" char* ShVfs_ReadDataJson(const char*, size_t*) { return nullptr; }

namespace {

ent::BoneCoord bone_with(const sh::math::Matrix3& m) {
    ent::BoneCoord b{};
    cg::bones::set_rotation(b, m);
    b.local.t[0] = 11, b.local.t[1] = -22, b.local.t[2] = 33;
    b.world_current = 1;
    return b;
}

sh::math::Matrix3 sample() {
    sh::math::Matrix3 m;
    const std::int16_t v[9] = {4000, -300, 120, 250, 3900, -800, -100, 700, 4050};
    for (int i = 0; i < 9; ++i) {
        m.m[i / 3][i % 3] = v[i];
    }
    return m;
}

bool same(const sh::math::Matrix3& a, const sh::math::Matrix3& b) {
    return std::memcmp(a.m, b.m, sizeof(a.m)) == 0;
}

}  // namespace

SH_TEST(chara_game_bone_rotation_set_replaces_the_rotation_only) {
    ent::BoneCoord b = bone_with(sample());
    cg::bones::rotation_set(b, 100, -200, 300);
    SH_CHECK(same(cg::bones::rotation_of(b), sh::math::rot_matrix_zxy_neg({100, -200, 300})));
    SH_CHECK(b.local.t[0] == 11 && b.local.t[1] == -22 && b.local.t[2] == 33);
}

SH_TEST(chara_game_bone_turns_multiply_on_the_named_side) {
    const sh::math::Matrix3 r = sh::math::rot_matrix_zxy_neg({512, 0, -256});
    ent::BoneCoord own = bone_with(sample());
    cg::bones::turn_own(own, r);
    SH_CHECK(same(cg::bones::rotation_of(own), sh::math::mul_matrix(sample(), r)));
    ent::BoneCoord parent = bone_with(sample());
    cg::bones::turn_parent(parent, r);
    SH_CHECK(same(cg::bones::rotation_of(parent), sh::math::mul_matrix(r, sample())));
    SH_CHECK(own.local.t[2] == 33 && parent.local.t[2] == 33);
}

SH_TEST(chara_game_bone_scales) {
    ent::BoneCoord b = bone_with(sample());
    cg::bones::scale_rows(b, 2048, 4096, 8192);
    SH_CHECK(same(cg::bones::rotation_of(b), sh::math::scale_matrix(sample(), {2048, 4096, 8192})));
    ent::BoneCoord set[3] = {bone_with(sample()), bone_with(sample()), bone_with(sample())};
    cg::bones::scale_columns(set, 2, 4096, 4096, 4096);
    SH_CHECK(same(cg::bones::rotation_of(set[2]), sample()));
    SH_CHECK(set[0].world_current == 0 && set[2].world_current == 1);  // the first frame of the set is marked stale
}

SH_TEST(chara_game_cast_control_and_collision_words) {
    ent::CharacterRecord r{};
    r.model.control_step = 5;
    r.model.anim.flags = 0;
    cg::cast::control_end(&r, 1);
    SH_CHECK(cg::var_read<std::int32_t>(r, cg::npc_vars::kWalkDone) == 1);
    SH_CHECK(r.model.anim.status == 3 && r.model.anim.alpha == 4096 && (r.model.anim.flags & 1) != 0);
    SH_CHECK(r.model.control_step == 5);
    cg::var_write<std::int32_t>(r, cg::npc_vars::kControlState, 7);
    cg::cast::control_end(&r, 0);
    SH_CHECK(cg::var_read<std::int32_t>(r, cg::npc_vars::kControlState) == 0 && r.model.control_step == 0);

    cg::var_write<std::int32_t>(r, cg::npc_vars::kTimer, 9);
    cg::cast::control_state_set(&r, 12);
    SH_CHECK(cg::var_read<std::int32_t>(r, cg::npc_vars::kControlState) == 12);
    SH_CHECK(cg::var_read<std::int32_t>(r, cg::npc_vars::kTimer) == 0);
    cg::cast::control_reset(&r);
    SH_CHECK(cg::var_read<std::int32_t>(r, cg::npc_vars::kResetControlState) == 1);

    cg::cast::anim_lock(&r);
    SH_CHECK((r.model.anim.flags & 1) == 0);
    cg::cast::anim_unlock(&r);
    SH_CHECK((r.model.anim.flags & 1) != 0);

    r.collision.box.top = 4;
    r.collision.cylinder.radius = 8;
    r.model.control_state = 2;
    cg::cast::collision_reset(&r);
    SH_CHECK(r.collision.box.top == 0 && r.collision.cylinder.radius == 0);
    SH_CHECK(r.model.control_state == 3 && r.collision.state() == 3);
}

SH_TEST(chara_game_cast_without_a_map_has_no_tables) {
    SH_CHECK(!cg::cast::load(nullptr));
    ent::CharacterRecord r{};
    r.model.chara_id = 26;
    SH_CHECK(cg::cast::anim_playback_state(&r) == -1);
    SH_CHECK(cg::cast::anim_start_keyframe(&r) == 0);
}
