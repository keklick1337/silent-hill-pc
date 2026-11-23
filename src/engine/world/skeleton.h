// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The part list of a skeletal character model (RENDER.md section 6): every model of the
// character's LM file is a part hung on a bone; the skeleton keeps the parts in an array and links them in the
// LM file's draw order. The character drawer walks that list (render/game_view.h skeleton_parts).
//
// The character model record (engine/chara/runtime/chara_models.h) embeds one.

#pragma once

#include <cstddef>
#include <cstdint>

#include "formats/game/model_records.h"

namespace sh::world {

using sh::formats::records::LmHeader;
using sh::formats::records::LmModel;

// Bit 31 of SkeletonPartModel::flags: the part is hidden (the drawer skips it). Bit 0: the drawer collapses it.
inline constexpr std::uint32_t kPartHidden = 1u << 31;
inline constexpr std::uint32_t kPartCollapsed = 1u << 0;

struct SkeletonPartModel {
    std::int32_t flags;
    void* bone;           // the bone frame the drawer last used for it
    LmModel* model;       // the part's model in the LM file
    std::int32_t model_index;
};

struct SkeletonPart {
    SkeletonPartModel model;
    std::int8_t bone_index;  // the bone the part hangs on (the first two digits of the model name)
    SkeletonPart* next;      // draw order
};

inline constexpr int kSkeletonMaxParts = 56;

struct SkeletonRecord {
    std::uint8_t part_count;
    std::uint8_t parts_used;   // parts assigned a model so far
    std::uint8_t drawn;        // 0: the drawer leaves the skeleton out
    std::int8_t reserved;
    SkeletonPart* draw_list;   // first part in draw order
    SkeletonPart* parts;
    SkeletonPart storage[kSkeletonMaxParts];
};

// The skeleton over `parts` (part_count of them): nothing assigned, nothing linked, every part's flags cleared.
void skeleton_init(SkeletonRecord& skel, SkeletonPart* parts, std::uint8_t part_count);

// Part `part` shows model `model_index` of `lm` (the model pointer only when `lm` is a converted LM file).
void part_model_assign(SkeletonPartModel& part, LmHeader* lm, std::int32_t model_index);

// Assigns the models the stream names (bone_order.h BoneModelStream; ends at kBoneStreamEnd) to the next parts,
// then links the new parts in the LM file's draw order behind the list (a fresh list unless `append`) and hides
// every part.
void skeleton_link(SkeletonRecord& skel, LmHeader* lm, const std::int8_t* stream, bool append);

// One part per model of `lm` (skeleton_link with the stream "0" or "0, all models", or none).
void skeleton_from_model(SkeletonRecord& skel, LmHeader* lm);

// Every linked part hangs on the bone its model's name starts with ("05HEAD" -> bone 5; 0 when not two digits).
void skeleton_bone_index_from_names(SkeletonRecord& skel);

// Shows (true) or hides (false) every part.
void skeleton_set_visible(SkeletonRecord& skel, bool visible);

}  // namespace sh::world
