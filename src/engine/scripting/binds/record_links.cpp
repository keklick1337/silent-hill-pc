// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The accessors of record_links.h that engine state answers: the session's character slots and bone pool, the
// numbered file buffers, the world object records of the map and the C entry points of other modules.

#include "engine/effects/map_effect_view.h"
#include "engine/scripting/binds/record_links.h"

#include <cstdio>
#include <cstring>

#include "engine/chara/runtime/chara_models.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/maps/map_runtime.h"
#include "engine/runtime/world_object_runtime.h"
#include "engine/scripting/script_buffers.h"
#include "engine/scripting/script_game_link.h"
#include "engine/app/loader/game_files.h"
#include "engine/gameflow/anim_slots.h"

extern "C" {
void Pc_CharaBehavior_Scripted(void* chara, void* anim_header, void* bones);
void Vw_CoordHierarchyMatrixCompute(void* bone, void* matrix);
std::int32_t MapOverlay_SlotAlloc(std::int32_t kind);
}

namespace sh::script::links {

namespace {

sh::entities::CharacterRecord* record(int slot) {
    return sh::game::session().system.characters.record(slot);
}

using sh::loader::ImagePlace;

ImagePlace image_place(int page, int u, int v, int palette_x, int palette_y) {
    ImagePlace place = {};
    place.page[1] = static_cast<std::uint8_t>(page);
    place.u = static_cast<std::uint8_t>(u);
    place.v = static_cast<std::uint8_t>(v);
    place.palette_x = static_cast<std::int16_t>(palette_x);
    place.palette_y = static_cast<std::int16_t>(palette_y);
    return place;
}

}  // namespace

bool chara_slot_valid(int slot) { return record(slot) != nullptr; }

void chara_behavior_update(int slot) { Pc_CharaBehavior_Scripted(record(slot), nullptr, nullptr); }

void* chara_position_record(int slot) { return &record(slot)->position; }

void chara_record_reset(int slot, int type) {
    sh::entities::CharacterRecord* ch = record(slot);
    std::memset(static_cast<void*>(ch), 0, sizeof(*ch));
    ch->model.chara_id = type;
    ch->model.control_state = 0;
    ch->model.anim.flags |= 2;
}

void* npc_bones(int index) {
    return index >= 0 && index < sh::entities::kNpcBonePoolSize
               ? &sh::game::session().system.characters.npc_bone_pool[index]
               : nullptr;
}

void chara_anim_data_alloc(int anim_slot, int type, void* buffer, void* bones) {
    sh::gameflow::anim_slots().alloc(anim_slot, type, buffer, bones);
}

void chara_model_load(int type, int model_slot, void* buffer, int page, int u, int v, int palette_x, int palette_y) {
    ImagePlace place = image_place(page, u, v, palette_x, palette_y);
    sh::chara::chara_models().load(type, model_slot, static_cast<sh::world::LmHeader*>(buffer),
                                   reinterpret_cast<const sh::world::TextureImage*>(&place));
}

void* file_buffer(int number) {
    switch (number) {
        case 0:
        case 1:
        case 2:
        case 26:
        case 29:
        case 30:
        case 31:
        case 32:
        case 33:
        case 34: return sh::scripting::file_buffer(number);
        default: return nullptr;
    }
}

void* model_buffer_2() { return sh::scripting::model_buffer(2); }

int queue_tim_at(int file, void* buffer, int page, int u, int v, int palette_x, int palette_y) {
    // The queue copies the place into its entry.
    const ImagePlace place = image_place(page, u, v, palette_x, palette_y);
    return sh::loader::queue_read_image(file, buffer, &place);
}

int queue_loading_screen_image(int file) {
    auto* image = static_cast<ImagePlace*>(sh::scripting::link::picture_record(sh::scripting::link::Picture::LoadingScreen));
    image->v = 0;
    return sh::loader::queue_read_image(file, sh::scripting::image_buffer(4), image);
}

bool world_object_draw(const char* name, const std::int32_t position[3], const std::int16_t rotation[3], char* err,
                       int err_cap) {
    const sh::Result<void*> bound = sh::world::world_object_runtime().binding(name, 1);
    if (!bound) {
        std::snprintf(err, static_cast<std::size_t>(err_cap), "%s", bound.error().c_str());
        return false;
    }
    sh::scripting::link::object_record_draw(bound.value(), position, rotation);
    return true;
}

void bone_world_matrix(void* bone, std::int16_t rotation[3][3], std::int32_t translation[3]) {
    sh::entities::BoneMatrix m = {};
    Vw_CoordHierarchyMatrixCompute(bone, &m);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            rotation[row][col] = m.m[row][col];
        }
        translation[row] = m.t[row];
    }
}

int effect_slot_alloc(int kind) { return MapOverlay_SlotAlloc(kind); }

void chara_update_set(int type, CharaUpdate update, int as) {
    sh::maps::map_runtime().set_chara_update(type, update == CharaUpdate::Scripted      ? sh::maps::CharaUpdate::Behaviour
                                            : update == CharaUpdate::ScriptedAlt ? sh::maps::CharaUpdate::BehaviourAlt
                                                                                  : sh::maps::CharaUpdate::BehaviourAs);
    sh::maps::map_runtime().set_chara_update_as(type, update == CharaUpdate::ScriptedAs ? as : -1);
}

bool effect_slots_present() { return sh::maps::map_runtime().loaded(); }
bool smoke_column_present() { return sh::maps::map_runtime().effect_block(sh::maps::EffectBlock::Smoke) != nullptr; }
int effect_slot_count() { return sh::effects::effect_slot_count(); }
sh::effects::EffectSlot effect_slot_load(int slot) { return sh::effects::load_effect_slot(slot); }
void effect_slot_store(int slot, const sh::effects::EffectSlot& s) { sh::effects::store_effect_slot(slot, s); }

}  // namespace sh::script::links

namespace sh::scripting::link {

// Only a map with a cast walks its NPCs along waypoints.
bool chara_path_walker_present() { return sh::maps::map_runtime().has_cast(); }

}  // namespace sh::scripting::link
