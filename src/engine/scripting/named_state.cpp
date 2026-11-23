// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game state of the named script API (src/engine/script/named_api_state.h) over the engine's records: the
// session's character slots and running save, the map variables of the data (map_vars.h), the map event state and
// the file buffers of the game's memory map.

#include "engine/loader/active_map.h"
#include "engine/script/named_api_state.h"

#include <cstdio>
#include <string>

#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/memory/work_buffers.h"
#include "engine/scripting/map_vars.h"
#include "engine/scripting/script_buffers.h"
#include "engine/scripting/script_game_link.h"
#include "engine/script/sh_script.h"
#include "engine/app/loader/game_files.h"
#include "engine/loader/queue/disc_files.h"

extern "C" {
// Other modules' functions by their C names (named_api_calls.h has the rest).
void Vc_LookAtFrameSet(void* frame, void* rotation, void* position);
void StaffRoll_StartPerspective(void);
int StaffRoll_UpdatePerspective(void);
void Player_EmptyWeaponHandSet(void);
void Player_WeaponAttackRestore(void);
}

namespace sh::script::named_state {

namespace {

sh::gameflow::ImagePlace s_images[kScratchImages];

sh::entities::CharacterRecord* record(void* p) {
    return static_cast<sh::entities::CharacterRecord*>(p);
}

sh::entities::CharacterSlots& slots() {
    return sh::game::session().system.characters;
}

template <typename T>
void access(T& field, bool set, long long& v) {
    if (set) {
        field = static_cast<T>(v);
    } else {
        v = static_cast<long long>(field);
    }
}

VarType var_type(sh::scripting::MapVarType t) {
    using M = sh::scripting::MapVarType;
    switch (t) {
        case M::S8: return VarType::S8;
        case M::U8: return VarType::U8;
        case M::S16: return VarType::S16;
        case M::U16: return VarType::U16;
        case M::S32: return VarType::S32;
        case M::U32: return VarType::U32;
        case M::Ref: break;
    }
    return VarType::Ref;
}

void frame(sh::scripting::link::FrameValue f, bool set, long long& v) {
    if (set) {
        sh::scripting::link::set_frame_value(f, v);
    } else {
        v = sh::scripting::link::frame_value(f);
    }
}

}  // namespace

VarLookup map_variable(std::string_view name, long long index, void** address, VarType* type, char* err, int err_cap) {
    const char* loaded = sh::loader::active_map_name();
    const char* script_map = ShScript_MapName();
    const std::string_view map = script_map != nullptr ? script_map : (loaded != nullptr ? loaded : "");
    sh::scripting::MapVarAddress found;
    std::string why;
    const sh::scripting::MapVarLookup r = sh::scripting::map_variable(map, name, index, &found, &why);
    *type = var_type(found.type);
    switch (r) {
        case sh::scripting::MapVarLookup::Found:
            *address = found.address;
            return VarLookup::Found;
        case sh::scripting::MapVarLookup::Unknown:
            return VarLookup::Unknown;
        case sh::scripting::MapVarLookup::Error:
            break;
    }
    std::snprintf(err, static_cast<std::size_t>(err_cap), "%s", why.c_str());
    return VarLookup::Error;
}

void* character_record(int slot) {
    return slots().record(slot);
}

void* character_position(void* r) {
    return &record(r)->position;
}

void* character_rotation(void* r) {
    return &record(r)->rotation;
}

void* character_model(void* r) {
    return &record(r)->model;
}

void* player_bone(int n) {
    return n >= 0 && n < sh::entities::kPlayerBoneCount ? &slots().player_bones[n] : nullptr;
}

void* npc_bone(int n) {
    return n >= 0 && n < sh::entities::kNpcBonePoolSize ? &slots().npc_bone_pool[n] : nullptr;
}

void* model_buffer_2() {
    return sh::scripting::model_buffer(2);
}

void* file_buffer(int n) {
    // The named binds reach the buffers the cutscene and scene loads use.
    return (n >= 0 && n <= 20) || n == 24 ? sh::scripting::file_buffer(n) : nullptr;
}

void* scratch_image(int n) {
    return n >= 0 && n < kScratchImages ? &s_images[n] : nullptr;
}

void chara_field(void* r, CharaField field, bool set, long long& v) {
    sh::entities::CharacterRecord& ch = *record(r);
    switch (field) {
        case CharaField::PosX: access(ch.position.x, set, v); break;
        case CharaField::PosY: access(ch.position.y, set, v); break;
        case CharaField::PosZ: access(ch.position.z, set, v); break;
        case CharaField::RotX: access(ch.rotation.x, set, v); break;
        case CharaField::RotY: access(ch.rotation.y, set, v); break;
        case CharaField::RotZ: access(ch.rotation.z, set, v); break;
        case CharaField::CharaId: access(ch.model.chara_id, set, v); break;
        case CharaField::PaletteIdx: access(ch.model.palette, set, v); break;
        case CharaField::ControlState: access(ch.model.control_state, set, v); break;
        case CharaField::StateStep: access(ch.model.control_step, set, v); break;
        case CharaField::AnimStatus: access(ch.model.anim.status, set, v); break;
        case CharaField::AnimFlags: access(ch.model.anim.flags, set, v); break;
        case CharaField::AnimTime: access(ch.model.anim.time, set, v); break;
        case CharaField::AnimKeyframe: access(ch.model.anim.keyframe, set, v); break;
        case CharaField::AnimAlpha: access(ch.model.anim.alpha, set, v); break;
        case CharaField::AngleToTarget: access(ch.angle_to_target, set, v); break;
        case CharaField::FallSpeed: access(ch.fall_speed, set, v); break;
        case CharaField::MoveSpeed: access(ch.move_speed, set, v); break;
        case CharaField::HeadingAngle: access(ch.heading, set, v); break;
        case CharaField::Flags: access(ch.flags, set, v); break;
        case CharaField::AttackReceived: access(ch.attack_received, set, v); break;
        case CharaField::Health: access(ch.health, set, v); break;
        case CharaField::DeathTimer: access(ch.death_timer, set, v); break;
        case CharaField::AuxTimer: access(ch.timer, set, v); break;
        case CharaField::CollisionState:
            if (set) {
                ch.collision.set_state(static_cast<std::int8_t>(v));
            } else {
                v = ch.collision.state();
            }
            break;
        case CharaField::ScratcherFlagsLow: {
            // The type's first variable byte, signed as the record's byte view.
            auto& b = reinterpret_cast<std::int8_t&>(ch.vars.bytes[0]);
            access(b, set, v);
            break;
        }
    }
}

void game_field(GameField field, bool set, long long& v) {
    using F = sh::scripting::link::FrameValue;
    sh::game::SaveRecord& save = sh::game::save_record();
    switch (field) {
        case GameField::DeltaTime: frame(F::DeltaTime, set, v); break;
        case GameField::DeltaTimeRaw: frame(F::DeltaTimeRaw, set, v); break;
        case GameField::GravitySpeed: frame(F::GravitySpeed, set, v); break;
        case GameField::VBlanks: frame(F::VBlanks, set, v); break;
        case GameField::ActiveBuffer: frame(F::ActiveBuffer, set, v); break;
        case GameField::ScreenFadeStatus: frame(F::ScreenFadeStatus, set, v); break;
        case GameField::BackgroundGamma: frame(F::BackgroundGamma, set, v); break;
        case GameField::SaveMapIdx: access(save.map, set, v); break;
        case GameField::SaveLocationId: access(save.location, set, v); break;
        case GameField::SaveClearGameCount: access(save.clear_count, set, v); break;
        case GameField::SaveClearGameEndings: access(save.endings, set, v); break;
        case GameField::SaveCurrentEndingFlags: access(save.current_ending, set, v); break;
        case GameField::SaveNextFearMode:  // bit 0 of the mode bits
            if (set) {
                save.mode_bits = static_cast<std::uint8_t>((save.mode_bits & ~1u) | (static_cast<unsigned>(v) & 1u));
            } else {
                v = save.mode_bits & 1u;
            }
            break;
        case GameField::SaveGameDifficulty:
            if (set) {
                save.set_difficulty(static_cast<int>(v));
            } else {
                v = save.difficulty();
            }
            break;
        case GameField::SaveEnemyState: access(save.enemy_states[save.map], set, v); break;
    }
}

bool set_anim_buffer_size(int group, std::int32_t bytes) {
    return sh::scripting::link::set_anim_buffer_size(group, bytes);
}

bool event_point(std::int32_t* x, std::int32_t* z) {
    namespace gg = sh::gameflow::game;
    const void* event = sh::gameflow::map_event_state().event;
    if (event == nullptr || !gg::map_points_present()) {
        return false;
    }
    const sh::gameflow::MapPointRecord point = gg::map_point(gg::map_event(event).point);
    *x = point.x;
    *z = point.z;
    return true;
}

bool event_item_used() {
    return sh::gameflow::map_event_state().used_item != 0;  // 0: no item (unequipped)
}

int event_used_item() {
    return sh::gameflow::map_event_state().used_item;
}

void camera_look_at_frame(void* frame, void* rotation, void* position) {
    Vc_LookAtFrameSet(frame, rotation, position);
}

void credits_start() {
    StaffRoll_StartPerspective();
}

bool credits_update() {
    return StaffRoll_UpdatePerspective() != 0;
}

void player_weapon_stash() {
    Player_EmptyWeaponHandSet();
}

void player_weapon_restore() {
    Player_WeaponAttackRestore();
}

int plate_door_image_load() {
    // Staged at the address the original reads the picture to.
    return sh::loader::queue_read_image(
        sh::loader::disc_files().find("TIM/COLORS.TIM"), sh::memory::work_buffers().data(sh::memory::WorkBuffer::PlateDoorImage),
        static_cast<const sh::loader::ImagePlace*>(
            sh::scripting::link::picture_record(sh::scripting::link::Picture::ItemInspection)));
}

}  // namespace sh::script::named_state
