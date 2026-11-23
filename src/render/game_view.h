// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the renderer reads of the game's state that the game keeps in its own records: the linked
// frames (bones, model and view frames), the current eye, and the light state of a lit model. The game-side
// bridge reads the records. A frame handle is the record's address, the same
// pointer the game passes to the render entry points; matrices have the engine layout of
// sh::formats::records::Transform (Q12 rotation, 32-bit translation), which the bridge checks against the game's.

#include <cstddef>
#include <cstdint>

#include "engine/assets/skin_model.h"
#include "engine/camera/view_space.h"
#include "engine/world/bone_light.h"
#include "formats/game/model_records.h"
#include "render/frame/frame.h"

namespace sh::render {

using Transform = sh::formats::records::Transform;
using ShortVec4 = sh::formats::records::ShortVec4;

// The path of the loose (mod or extracted) copy of disc file `file_index`, as the file queue probes it
// ("FOLDER/NAME.EXT"); false for an index outside the file table or a path longer than `size`.
bool disc_file_loose_path(int file_index, char* out, std::size_t size);
// The size of disc file `file_index` rounded up to whole sectors, as the file table gives it.
std::int32_t disc_file_sector_size(int file_index);

// The frame `index` places after `first` in an array of the game's frames (a skeleton's bones).
void* game_frame_at(void* first, int index);

// World and view matrices of `frame`, through the game's view system (the matrices its own drawers use).
void game_frame_world_and_view(void* frame, Transform& world, Transform& view);

// The world-to-screen matrix of a point (Q12 position): the view matrix of a camera-facing quad there.
void world_screen_at(Transform& out, std::int32_t x, std::int32_t y, std::int32_t z);
// Loads the geometry state's rotation and translation from `m`.
void projection_matrix_set(const Transform& m);

// The player's flashlight is on.
bool flashlight_on();

// One normal of a model mesh as the model file packs it: s8 components and how many shade slots it covers.
struct PackedNormal {
    std::int8_t x, y, z;
    std::uint8_t count;
};
// Slots of the game's per-mesh shade and normal pools.
inline constexpr int kShadeSlots = 261;
inline constexpr int kNormalSlots = 258;

// ---- Characters (the skinned-glb drawer, render/skinning/gltf_chara.h) ----

// The model a skeleton's parts come from. `model` is the loaded model's identity (the part records point into it),
// `glb` the converted file that stands in for it ("CHARA/HERO.glb"), `bone_count` how many bone frames the parts
// hang on (the frames array is no longer than that).
struct SkeletonModel {
    const void* model = nullptr;
    char glb[64] = {};
    int part_count = 0;
    int bone_count = 0;
};
// False when the skeleton has no parts or they belong to no loaded character model.
bool skeleton_model(const void* skeleton, SkeletonModel& out);
// Name of part `index` of `model` (up to eight characters, NUL-terminated).
void model_part_name(const void* model, int index, char out[9]);
// The game's current semi-transparency state of a character model: the mode its texture was bound with (ABR 0..3)
// and whether every primitive (1), none (0) or only those the model file marks (-1) are translucent; the game
// switches them all on and off (map scripts' fades), together with the STP bit of the whole palette.
struct ModelBlendState {
    int abr = 1;
    int translucent = -1;
};
ModelBlendState model_blend_state(const void* model);
// A part the skeleton draws this frame: its index in `model` (-1 when it is none of its parts) and whether the drawer
// collapses it (draws nothing for it).
struct PartState {
    int index = -1;
    bool collapsed = false;
};
PartState skeleton_part(const void* model, const void* part);
// One part a skeleton shows this frame, in its draw order: the part record and the index of the bone frame it hangs
// on. Parts the game switched off are not listed.
struct SkeletonPartRef {
    const void* part = nullptr;
    int bone = 0;
};
// The parts of `skeleton` (at most `max`); 0 when it has none or the game does not draw it.
int skeleton_parts(const void* skeleton, SkeletonPartRef* out, int max);

// How the animation places one bone of a frame array (engine/assets/skin_model.h).
using BoneRest = sh::assets::BoneRest;
// The first `count` bones of `frames` as the skeleton that last posed them has them (entities' AnimBank); an array
// no animation file posed is read from the frames themselves: a bone hanging from the root counts as animated, any
// other keeps the offset it has now.
void frames_rest(void* frames, BoneRest* out, int count);

using BoneLight = sh::world::BoneLight;

// ---- Ordering table ----

// The world layer's slot `bucket`, clamped into 1 .. slots - 1 (the scene's ordering table never takes slot 0).
inline sh::frame::OrderKey world_order_key(int bucket) {
    const int last = static_cast<int>(sh::frame::slot_count(sh::frame::Layer::World)) - 1;
    return {sh::frame::Layer::World, static_cast<std::uint32_t>(bucket < 1 ? 1 : (bucket > last ? last : bucket))};
}

}  // namespace sh::render
