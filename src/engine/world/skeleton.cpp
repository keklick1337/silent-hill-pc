// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/skeleton.h"

#include <vector>

#include "engine/world/bone_order.h"

namespace sh::world {

namespace {

// One stream for every skeleton: its running index carries over between streams (bone_order.h).
BoneModelStream& model_stream() {
    static BoneModelStream stream;
    return stream;
}

void assign_models(SkeletonRecord& skel, LmHeader* lm, const std::int8_t* stream) {
    BoneModelStream& s = model_stream();
    s.reset(stream);
    for (std::int8_t model = s.next(); model != kBoneStreamEnd; model = s.next()) {
        part_model_assign(skel.parts[skel.parts_used].model, lm, model);
        skel.parts_used++;
    }
}

void link_in_draw_order(SkeletonPart** link, SkeletonPart* parts, int count, const LmHeader* lm) {
    const std::size_t n = count > 0 ? static_cast<std::size_t>(count) : 0;
    std::vector<std::int32_t> model_index(n);
    std::vector<std::uint32_t> order(n);
    for (std::size_t i = 0; i < n; i++) {
        model_index[i] = parts[i].model.model_index;
    }
    const std::size_t linked = bone_link_order(lm->model_order, lm->model_count, model_index.data(), n, order.data());
    for (std::size_t i = 0; i < linked; i++) {
        *link = &parts[order[i]];
        link = &parts[order[i]].next;
    }
    *link = nullptr;
}

}  // namespace

void skeleton_init(SkeletonRecord& skel, SkeletonPart* parts, std::uint8_t part_count) {
    skel.parts = parts;
    skel.part_count = part_count;
    skel.parts_used = 0;
    skel.drawn = 1;
    skel.draw_list = nullptr;
    for (SkeletonPart* p = parts; p < parts + part_count; p++) {
        p->model.flags = 0;
    }
}

void part_model_assign(SkeletonPartModel& part, LmHeader* lm, std::int32_t model_index) {
    part.model_index = model_index;
    if (lm->magic == sh::formats::records::kLmMagic) {
        part.model = &lm->models[model_index];
    }
}

void skeleton_link(SkeletonRecord& skel, LmHeader* lm, const std::int8_t* stream, bool append) {
    if (!append) {
        skel.parts_used = 0;
        skel.draw_list = nullptr;
    }
    const int first = skel.parts_used;
    assign_models(skel, lm, stream);

    SkeletonPart** tail = &skel.draw_list;
    while (*tail != nullptr) {
        tail = &(*tail)->next;
    }
    link_in_draw_order(tail, &skel.parts[first], skel.parts_used - first, lm);
    skeleton_set_visible(skel, false);
}

void skeleton_from_model(SkeletonRecord& skel, LmHeader* lm) {
    // Model 0, then the range marker up to the last model; the count byte wraps as the stream's signed bytes do.
    std::uint8_t stream[4] = {0, 0, 0, 0};
    switch (lm->model_count) {
        case 0:
            stream[0] = static_cast<std::uint8_t>(kBoneStreamEnd);
            break;
        case 1:
            stream[1] = static_cast<std::uint8_t>(kBoneStreamEnd);
            break;
        default:
            stream[1] = static_cast<std::uint8_t>(kBoneStreamMulti);
            stream[2] = static_cast<std::uint8_t>(lm->model_count - 1);
            stream[3] = static_cast<std::uint8_t>(kBoneStreamEnd);
            break;
    }
    skeleton_link(skel, lm, reinterpret_cast<const std::int8_t*>(stream), false);
}

void skeleton_bone_index_from_names(SkeletonRecord& skel) {
    for (SkeletonPart* p = skel.draw_list; p != nullptr; p = p->next) {
        const char* name = p->model.model->name;
        const std::uint32_t ones = static_cast<std::uint32_t>(name[1] - '0');
        const int tens = name[0] - '0';
        p->bone_index = static_cast<std::int8_t>((ones < 10 && tens >= 0 && tens < 10) ? tens * 10 + static_cast<int>(ones)
                                                                                          : 0);
    }
}

void skeleton_set_visible(SkeletonRecord& skel, bool visible) {
    for (SkeletonPart* p = skel.parts; p < skel.parts + skel.part_count; p++) {
        const std::uint32_t flags = static_cast<std::uint32_t>(p->model.flags);
        p->model.flags = static_cast<std::int32_t>(visible ? (flags & ~kPartHidden) : (flags | kPartHidden));
    }
}

}  // namespace sh::world
