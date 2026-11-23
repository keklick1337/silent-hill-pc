// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the game's file queue (game_files.h) needs of the running game: the animation slots, the character file
// table, the cutscene files and the entity animation tracker. Implemented by src/engine/app/game_view_frame.cpp.
#pragma once

#include <cstdint>

namespace sh::loader::view {

// A file's bytes landed at `data` (every read): the entity animation tracker learns which animation file is where.
void entity_file_loaded(std::int32_t file, void* data);
// The cutscene player serves `file` into `dest` when the file is one of its own.
bool cutscene_file_read(void* dest, std::int32_t file);
// An animation file landed for `chara_id` in animation slot `slot`; `bones` are the character's bone transforms.
void anim_file_loaded(int slot, int chara_id, void* data, void* bones);
// The animation file of a character type (its disc file number).
std::int32_t chara_anim_file(int chara_id);
// A chunk load started (the game marks the staff roll's clock word on it).
void chunk_load_mark();
// Debug probe: the character texture pool was written over at this VRAM rectangle.
void pool_write_probe(int x, int y, int w, int h);

}  // namespace sh::loader::view
