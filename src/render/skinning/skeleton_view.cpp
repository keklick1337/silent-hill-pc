// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// What the character drawer reads of a skeleton and its bone frames (render/game_view.h), from the engine's
// records: the part list (engine/world/skeleton.h), the resident models (engine/chara/runtime/chara_models.h) and
// the frame hierarchy (engine/camera/view_system.h).

#include <cstring>
#include <memory>
#include <string>

#include "engine/camera/view_system.h"
#include "engine/chara/runtime/chara_models.h"
#include "engine/entities/anim_bank.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/world/lm_materials.h"
#include "engine/world/skeleton.h"
#include "render/game_view.h"

namespace sh::render {

namespace {

namespace cam = sh::camera;
namespace sw = sh::world;

static_assert(sizeof(cam::Matrix) == sizeof(Transform), "frame matrices are read as records::Transform");

Transform as_transform(const cam::Matrix& m) {
    Transform t;
    std::memcpy(&t, &m, sizeof t);
    return t;
}

const sw::LmHeader* model_of(const void* model) { return static_cast<const sw::LmHeader*>(model); }

}  // namespace

void* game_frame_at(void* first, int index) { return static_cast<cam::FrameRecord*>(first) + index; }

void game_frame_world_and_view(void* frame, Transform& world, Transform& view) {
    const cam::Matrix w = cam::frame_world(static_cast<cam::FrameRecord*>(frame));
    world = as_transform(w);
    view = as_transform(cam::view_matrix(cam::view_system().world_screen(), w));
}

void frames_rest(void* frames, BoneRest* out, int count) {
    auto* first = static_cast<cam::FrameRecord*>(frames);
    const std::shared_ptr<const sh::entities::AnimationSet> set = sh::entities::anim_bank().posed_by(frames);
    const sh::entities::Skeleton* sk = set != nullptr ? &set->skeleton() : nullptr;
    for (int b = 0; b < count; ++b) {
        BoneRest& r = out[b];
        r = BoneRest{};
        if (sk != nullptr) {
            if (b >= static_cast<int>(sk->bone_count())) {
                continue;
            }
            const sh::entities::SkeletonBone& bone = sk->bones[static_cast<std::size_t>(b)];
            r.parent = bone.parent >= 0 && bone.parent != b && bone.parent < count ? bone.parent : -1;
            r.animated = b == 0 || r.parent < 0 || bone.translation_track != sh::entities::kNoTrack;
            for (std::size_t c = 0; c < 3; ++c) {
                r.offset[c] = static_cast<float>(std::int32_t{bone.rest[c]} * (1 << sk->scale_log2));
            }
            continue;
        }
        const cam::FrameRecord* parent = first[b].parent;
        if (parent >= first && parent < first + count && parent != first + b) {
            r.parent = static_cast<int>(parent - first);
        }
        r.animated = r.parent <= 0;
        for (std::size_t c = 0; c < 3; ++c) {
            r.offset[c] = static_cast<float>(first[b].local.t[c]);
        }
    }
}

bool skeleton_model(const void* skeleton, SkeletonModel& out) {
    const auto* skel = static_cast<const sw::SkeletonRecord*>(skeleton);
    if (skel == nullptr || skel->draw_list == nullptr || skel->draw_list->model.model == nullptr) {
        return false;
    }
    const sh::chara::CharaModels& models = sh::chara::chara_models();
    const sh::chara::CharaModelRecord* owner = models.owner_of_part(skel->draw_list->model.model);
    if (owner == nullptr) {
        return false;
    }
    // The glb stands in for the model file of the first resident model read from the same LM.
    const sh::chara::CharaModelRecord* named = models.model_of(owner->lm);
    if (named == nullptr) {
        return false;
    }
    // The glb the character's data names, else the one of the model file it loads now.
    const std::string path = sh::chara::CharaModels::model_glb(named->chara);
    if (path.empty() || path.size() >= sizeof(out.glb)) {
        return false;
    }
    std::memcpy(out.glb, path.c_str(), path.size() + 1);
    out.model = owner->lm;
    out.part_count = owner->lm->model_count;
    out.bone_count = 1;
    for (const sw::SkeletonPart* cur = skel->draw_list; cur != nullptr; cur = cur->next) {
        const int needed = static_cast<std::uint8_t>(cur->bone_index) + 1;
        if (needed > out.bone_count) {
            out.bone_count = needed;
        }
    }
    return true;
}

void model_part_name(const void* model, int index, char out[9]) {
    std::memcpy(out, model_of(model)->models[index].name, 8);
    out[8] = '\0';
}

ModelBlendState model_blend_state(const void* model) {
    ModelBlendState out;
    const sw::LmHeader* lm = model_of(model);
    if (lm == nullptr) {
        return out;
    }
    if (lm->material_count > 0 && lm->materials != nullptr) {
        out.abr = (lm->materials[0].tpage >> 5) & 3;
    }
    std::size_t on = 0, total = 0;
    for (int m = 0; m < lm->model_count; ++m) {
        const sw::LmModel& part = lm->models[m];
        for (int k = 0; k < part.mesh_count; ++k) {
            const auto* prims = static_cast<const sw::LmPrimitive*>(part.meshes[k].primitives);
            for (int p = 0; prims != nullptr && p < part.meshes[k].primitive_count; ++p) {
                ++total;
                on += (prims[p].draw & 0x8000u) != 0 ? 1 : 0;
            }
        }
    }
    if (total > 0 && on == 0) {
        out.translucent = 0;
    } else if (total > 0 && on == total) {
        out.translucent = 1;
    }
    return out;
}

PartState skeleton_part(const void* model, const void* part) {
    const auto* info = static_cast<const sw::SkeletonPartModel*>(part);
    PartState s;
    s.collapsed = (static_cast<std::uint32_t>(info->flags) & sw::kPartCollapsed) != 0;
    if (info->model != nullptr && model != nullptr) {
        s.index = static_cast<int>(info->model - model_of(model)->models);
    }
    return s;
}

int skeleton_parts(const void* skeleton, SkeletonPartRef* out, int max) {
    const auto* skel = static_cast<const sw::SkeletonRecord*>(skeleton);
    if (skel == nullptr || skel->draw_list == nullptr || skel->drawn == 0) {
        return 0;
    }
    int n = 0;
    for (const sw::SkeletonPart* cur = skel->draw_list; cur != nullptr && n < max; cur = cur->next) {
        if (cur->model.flags < 0) {
            continue;  // hidden
        }
        out[n].part = &cur->model;
        out[n].bone = static_cast<std::uint8_t>(cur->bone_index);
        ++n;
    }
    return n;
}

}  // namespace sh::render
