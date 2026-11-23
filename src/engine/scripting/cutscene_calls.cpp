// SPDX-License-Identifier: GPL-3.0-or-later
// The cutscene calls (cutscene_calls.h) answered by the engine's cutscene player from convert/cutscenes/*.dms.json.

#include "engine/scripting/cutscene_calls.h"

#include "engine/loader/queue/disc_files.h"
#include "engine/cutscene/cutscene_c.h"
#include "engine/entities/character_record.h"
#include "engine/runtime/game_hooks.h"
#include "engine/text/game_text.h"

namespace {

std::uintptr_t key(const void* slot) { return reinterpret_cast<std::uintptr_t>(slot); }

// Where a missing track leaves the debug text cursor (the prototype printed the track's name there): 15.75% and 37.5%
// of the 320x240 screen.
constexpr std::int32_t kMissingTrackTextX = 50;
constexpr std::int32_t kMissingTrackTextY = 90;
constexpr int kDmsFileType = 3;  // the file table's type of a .DMS file

}  // namespace

extern "C" {

int PcCutscene_FileRead(void* dest, int file) {
    if (file < 0 || file >= sh::loader::disc_files().count() || sh::loader::disc_files().type(file) != kDmsFileType) {
        return 0;
    }
    ShCutscene_Assign(key(dest), sh::loader::disc_files().path(file).c_str());
    return 1;
}

int PcCutscene_Load(const char* path, void* slot) {
    ShCutscene_Assign(key(slot), path);
    const int started = ShCutscene_Start(key(slot));
    sh::hooks::game_cutscene_start(path);
    return started;
}

int PcCutscene_FrameCount(void* slot) { return ShCutscene_FrameCount(key(slot)); }

void PcCutscene_CharacterApply(void* chara, const char* track, int time, void* slot) {
    auto* c = static_cast<sh::entities::CharacterRecord*>(chara);
    sh::scripting::cutscene_track_pose(&c->position, &c->rotation, track, time, slot);
}

}  // extern "C"

void sh::scripting::cutscene_start(void* header) {
    ShCutscene_Start(key(header));
    sh::hooks::game_cutscene_start(ShCutscene_Path(key(header)));
}

void sh::scripting::cutscene_track_pose(void* position, void* rotation, const char* track, std::int32_t time,
                                         void* header) {
    ShCutsceneActorPose pose{};
    if (ShCutscene_ActorPose(key(header), track, time, &pose) != 1) {
        sh::text::game_text().set_debug_position(kMissingTrackTextX, kMissingTrackTextY);
    }
    auto* p = static_cast<std::int32_t*>(position);
    auto* r = static_cast<std::int16_t*>(rotation);
    for (int i = 0; i < 3; ++i) {
        p[i] = pose.position[i];
        r[i] = pose.rotation[i];
    }
}

std::int32_t sh::scripting::cutscene_camera_pose(void* position, void* look_at, std::uint16_t* angle, std::int32_t time,
                                                void* header) {
    ShCutsceneCameraPose pose{};
    ShCutscene_CameraPose(key(header), time, &pose);
    auto* eye = static_cast<std::int32_t*>(position);
    auto* target = static_cast<std::int32_t*>(look_at);
    for (int i = 0; i < 3; ++i) {
        eye[i] = pose.eye[i];
        target[i] = pose.target[i];
    }
    if (angle != nullptr) {
        *angle = static_cast<std::uint16_t>(pose.roll);
    }
    return pose.projection;
}
