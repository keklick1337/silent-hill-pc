// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game's animation and skeleton entry points on the engine (model_runtime.h). The names are the ones the game's
// clip tables and character code link against; the bodies work on engine records only.

#include "engine/entities/game/model_runtime.h"

#include <cstdint>

#include "engine/entities/anim_player.h"
#include "engine/entities/bind_pose.h"
#include "engine/math/precise_capture.h"
#include "engine/math/geometry_state.h"
#include "engine/world/bone_matrix.h"
#include "engine/world/skeleton.h"
#include "render/gpu/render_state.h"

namespace ent = sh::entities;
namespace sw = sh::world;

extern "C" {

// The animation bank of the running game (its loader and smoothing are installed by the game side).
void PcEntityAnim_BoneUpdate(void* anm_header, ent::BoneCoord* bones, int keyframe0, int keyframe1, int alpha);
void PcEntityAnim_Playback(int kind, ent::ModelRecord* model, void* anm_header, ent::BoneCoord* bones,
                           ent::ClipInfoRecord* clip);

// The precise-capture twins of matrices (engine/math/precise_capture.cpp).

// ---- Animation ----

void Anim_BoneInit(void* anm_header, ent::BoneCoord* bones) { ent::bind_pose_init(anm_header, bones); }

void Anim_BoneUpdate(void* anm_header, ent::BoneCoord* bones, std::int32_t keyframe0, std::int32_t keyframe1,
                     std::int32_t alpha) {
    PcEntityAnim_BoneUpdate(anm_header, bones, keyframe0, keyframe1, alpha);
}

// The entry of the current status: the player's map clips (from map_status_start on) come from the map's table.
ent::ClipInfoRecord* ModelAnim_ActiveInfoGet(ent::ModelAnimRecord* anim) {
    auto* table = static_cast<ent::ClipInfoRecord*>(const_cast<void*>(anim->clip_table));
    auto* map_table = static_cast<ent::ClipInfoRecord*>(const_cast<void*>(anim->map_clip_table));
    if (map_table != nullptr && anim->status >= anim->map_status_start) {
        return map_table - anim->map_status_start + anim->status;
    }
    return table + anim->status;
}

void Chara_ModelAnimPlay(ent::CharacterRecord* chara, void* anm_header, ent::BoneCoord* bones) {
    ent::ClipInfoRecord* clip = ModelAnim_ActiveInfoGet(&chara->model.anim);
    clip->playback(&chara->model, anm_header, bones, clip);
}

std::int32_t Anim_DurationGet(ent::ModelRecord* model, ent::ClipInfoRecord* clip) {
    if (!clip->variable_duration) {
        return clip->duration.constant;
    }
    // Both arguments: Harry's duration reads the model, Monster Cybil's walk reads the entry.
    return reinterpret_cast<ent::ClipDurationFn>(clip->duration.function)(model, clip);
}

void Anim_PlaybackOnce(ent::ModelRecord* model, void* anm_header, ent::BoneCoord* bones, ent::ClipInfoRecord* clip) {
    PcEntityAnim_Playback(static_cast<int>(ent::ClipKind::Once), model, anm_header, bones, clip);
}

void Anim_PlaybackLoop(ent::ModelRecord* model, void* anm_header, ent::BoneCoord* bones, ent::ClipInfoRecord* clip) {
    PcEntityAnim_Playback(static_cast<int>(ent::ClipKind::Loop), model, anm_header, bones, clip);
}

void Anim_BlendLinear(ent::ModelRecord* model, void* anm_header, ent::BoneCoord* bones, ent::ClipInfoRecord* clip) {
    PcEntityAnim_Playback(static_cast<int>(ent::ClipKind::Blend), model, anm_header, bones, clip);
}

// ---- Bones ----

// Turns a bone by a ZXY rotation (world/bone_matrix.h). The bone's previous rotation is loaded into the geometry
// state first and the result gets a precise twin, as the original's multiply left them.
void Bone_CoordRotationSet(ent::BoneCoord* bone, std::int16_t rot_z, std::int16_t rot_x, std::int32_t rot_y) {
    sh::math::geometry().load_rotation(sh::math::transform_of(bone->local));

    static_assert(sizeof(sw::BoneMatrix) == sizeof(ent::BoneMatrix), "matrix layout");
    sw::bone_coord_rotation(*reinterpret_cast<sw::BoneMatrix*>(&bone->local), rot_x, rot_y, rot_z);

    if (sh::render::precise().enabled) {
        double exact[9];
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                exact[r * 3 + c] = static_cast<double>(bone->local.m[r][c]) / 4096.0;
            }
        }
        sh::math::capture::note_rotation(&bone->local, exact);
    }
}

// ---- Skeleton part lists (world/skeleton.h) ----

void Skeleton_Init(sw::SkeletonRecord* skel, sw::SkeletonPart* parts, std::uint8_t part_count) {
    sw::skeleton_init(*skel, parts, part_count);
}

// Set by the character draw only while it draws Harry with the first-person camera: the drawer leaves his head
// out (render/skinning/gltf_chara.h).
int g_PcHideHarryFpsBody = 0;

}  // extern "C"
