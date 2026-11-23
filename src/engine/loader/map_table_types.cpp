// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The element types and the game functions and variables a map's tables.json may name (engine/maps/map_tables.h):
// how a disc element becomes the running game's element. Registered with the map tables at start.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "engine/chara/game/clip_info.h"
#include "engine/entities/character_record.h"
#include "engine/game_state/session.h"
#include "engine/loader/map_table_types.h"
#include "engine/maps/map_tables.h"

namespace cg = sh::chara::game;

extern "C" {
// The engine's playbacks (entities/game/model_runtime.cpp) and Harry's walk and run speed (player_host.cpp).
void Anim_BlendLinear(sh::entities::ModelRecord*, void*, sh::entities::BoneCoord*, cg::ClipInfo*);
void Anim_PlaybackOnce(sh::entities::ModelRecord*, void*, sh::entities::BoneCoord*, cg::ClipInfo*);
void Anim_PlaybackLoop(sh::entities::ModelRecord*, void*, sh::entities::BoneCoord*, cg::ClipInfo*);
std::int32_t Player_VariableAnimDurationGet(const sh::entities::ModelRecord* model);
// map7_s03's light beams (the final boss's effects): the size of one beam pattern and the boss bone position the
// beams write each frame.
unsigned BossFx_PatternBytes(void);
void* BossFx_BonePosition(void);
}

namespace sh::maps {
namespace {

constexpr TableField kClipFields[] = {
    {0, offsetof(cg::ClipInfo, playback), 4, FieldFill::Pointer},
    {4, offsetof(cg::ClipInfo, status), 1, FieldFill::Copy},
    {5, offsetof(cg::ClipInfo, variable_speed), 1, FieldFill::Copy},
    {6, offsetof(cg::ClipInfo, link), 1, FieldFill::Copy},
    {8, offsetof(cg::ClipInfo, speed), 4, FieldFill::PointerOrValue},
    {12, offsetof(cg::ClipInfo, start_keyframe), 2, FieldFill::Copy},
    {14, offsetof(cg::ClipInfo, end_keyframe), 2, FieldFill::Copy},
};
constexpr TableType kClip = {16, sizeof(cg::ClipInfo), kClipFields};

#define BEAM_END(i, disc)                                                                    \
    {disc, offsetof(BossBeamStep, ends) + (i) * sizeof(BossBeamEnd) + offsetof(BossBeamEnd, from), 4,         \
     FieldFill::Pointer},                                                                                      \
    {disc + 4, offsetof(BossBeamStep, ends) + (i) * sizeof(BossBeamEnd) + offsetof(BossBeamEnd, from_param), 4, \
     FieldFill::Copy},                                                                                         \
    {disc + 8, offsetof(BossBeamStep, ends) + (i) * sizeof(BossBeamEnd) + offsetof(BossBeamEnd, to), 4,        \
     FieldFill::Pointer},                                                                                      \
    {disc + 12, offsetof(BossBeamStep, ends) + (i) * sizeof(BossBeamEnd) + offsetof(BossBeamEnd, to_param), 4, \
     FieldFill::Copy},                                                                                         \
    {disc + 16, offsetof(BossBeamStep, ends) + (i) * sizeof(BossBeamEnd) + offsetof(BossBeamEnd, motion), 16,  \
     FieldFill::Copy}
constexpr TableField kBossBeamStepFields[] = {
    {0, offsetof(BossBeamStep, time), 4, FieldFill::Copy},
    BEAM_END(0, 4),
    BEAM_END(1, 36),
};
#undef BEAM_END
constexpr TableType kBossBeamStep = {68, sizeof(BossBeamStep), kBossBeamStepFields};

struct Named {
    std::string_view name;
    unsigned size;
};

// Plain element types: the disc bytes are copied as they are, so the element has the disc size.
constexpr Named kFlatTypes[] = {
    {"u8", 1}, {"s8", 1}, {"u16", 2}, {"s16", 2}, {"u32", 4}, {"s32", 4},
    {"VECTOR3", 12}, {"s_Pose", 20}, {"s_WorldObjectPose", 20}, {"s_UnkStruct3_Mo", 8},
};

unsigned size_of(const Named* list, std::size_t n, std::string_view type) {
    for (std::size_t i = 0; i < n; ++i) {
        if (list[i].name == type) {
            return list[i].size;
        }
    }
    return 0;
}

class GameTableTypes final : public MapTableTypes {
public:
    const TableType* relocating(std::string_view type) const override {
        return type == "s_AnimInfo" ? &kClip : type == "BossFxStep" ? &kBossBeamStep : nullptr;
    }
    unsigned flat_size(std::string_view type) const override {
        return size_of(kFlatTypes, sizeof(kFlatTypes) / sizeof(kFlatTypes[0]), type);
    }
    // Types of the arrays that have no place on the disc ("port_word" in tables.json): the running game's own
    // structures.
    unsigned port_size(std::string_view type) const override {
        if (type == "BoneCoord") {
            return sizeof(sh::entities::BoneCoord);
        }
        if (type == "BossFxPattern") {
            return BossFx_PatternBytes();
        }
        return 0;
    }
    void* function(std::string_view name) const override {
        if (name == "Anim_PlaybackOnce") return reinterpret_cast<void*>(&Anim_PlaybackOnce);
        if (name == "Anim_PlaybackLoop") return reinterpret_cast<void*>(&Anim_PlaybackLoop);
        if (name == "Anim_BlendLinear") return reinterpret_cast<void*>(&Anim_BlendLinear);
        if (name == "Player_VariableAnimDurationGet") return reinterpret_cast<void*>(&Player_VariableAnimDurationGet);
        return nullptr;
    }
    // Live game variables a map's tables may point at: the pointer resolves to the variable itself.
    void* variable(std::string_view name) const override {
        if (name == "boss_slot_position") {
            return &sh::game::session().system.characters.npcs[2].position;  // map7_s03's boss (NPC slot 2)
        }
        if (name == "boss_bone_position") {
            return BossFx_BonePosition();
        }
        return nullptr;
    }
};

const GameTableTypes s_types;

struct Registration {
    Registration() { set_map_table_types(&s_types); }
} s_registration;

}  // namespace
}  // namespace sh::maps
