// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The cutscene calls of the map scripts and the file queue over the engine's cutscene player
// (src/engine/cutscene/cutscene_c.h). A cutscene is named by the buffer a file was read
// into; the buffer address is only the player's slot key, and no DMS byte reaches it.

#include <cstdint>

extern "C" {

// A file read of the queue starts: a cutscene file goes to the player and true means the disc read is skipped.
int PcCutscene_FileRead(void* dest, int file);

// Plays the cutscene `path` ("ANIM/CAFE.DMS" or a mod's "ANIM/MYSCENE") in `slot` at once; 1 when it loaded.
int PcCutscene_Load(const char* path, void* slot);
// Timeline length in frames of what `slot` plays, -1 without a cutscene.
int PcCutscene_FrameCount(void* slot);
// Moves a character record (sh::entities::CharacterRecord) along its track.
void PcCutscene_CharacterApply(void* chara, const char* track, int time, void* slot);

}  // extern "C"

namespace sh::scripting {

// The cutscene in `header` starts playing.
void cutscene_start(void* header);
// A track's position (3 x Q12) and rotation (3 x Q12 angle) at `time` (Q12 frames).
void cutscene_track_pose(void* position, void* rotation, const char* track, std::int32_t time, void* header);
// The camera's eye and target (3 x Q12 each) and roll at `time` (`angle` may be null); returns the projection
// distance.
std::int32_t cutscene_camera_pose(void* position, void* look_at, std::uint16_t* angle, std::int32_t time,
                                  void* header);

}  // namespace sh::scripting
