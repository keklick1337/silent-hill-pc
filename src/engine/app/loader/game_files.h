// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's file queue: one sh::loader::FileQueue with the game's numbers, its host steps
// (where the bytes come from, the image upload, the animation bind) and the blocking waits. Files are named by their
// disc path (sh::loader::disc_files().find); the number is only the queue's handle of the current table.
#pragma once

#include <cstdint>

namespace sh::loader {

class FileQueue;

// Where an image read lands in video memory (the request's 8 extra bytes): texture page, u/v inside it and the
// palette cell; u = 0xFF leaves the picture where the file says, palette_x = -1 the palette.
struct ImagePlace {
    std::uint8_t page[2];
    std::uint8_t u;
    std::uint8_t v;
    std::int16_t palette_x;
    std::int16_t palette_y;
};
static_assert(sizeof(ImagePlace) == 8);

FileQueue& game_file_queue();

// A read of the whole file into `dest`; a read whose picture is uploaded after the read (`place` null: where the
// file says); a read of a character's animation file bound to the animation slot afterwards; a drive seek.
std::int32_t queue_read(std::int32_t file, void* dest);
std::int32_t queue_read_image(std::int32_t file, void* dest, const ImagePlace* place);
std::int32_t queue_read_anim(int slot, int chara_id, void* dest, void* bones);
std::int32_t queue_seek(std::int32_t file);
// A read of the file into `dest` that transfers nothing: it takes the queue's steps of a disc read (and the buffer
// checks see `dest`), so it completes on the same update as the read it stands for. The map cells' window uses it:
// their contents come from the area files.
std::int32_t queue_pace(std::int32_t file, void* dest);

bool queue_loaded(std::int32_t ticket);
int queue_length();
void queue_update();
void queue_clear();
void queue_reset();
// Reads the map chunks at storage speed (a bounded drain); true once the queue is empty.
bool queue_load_chunks();
// With fast blocking loads: drains the queue at storage speed now.
void queue_drain_now();
// Blocks until the queue is empty (one vertical blank per update), the pad's vibration paused meanwhile.
void queue_wait_empty();

// The first sector of the XA container the sound code addresses as `base` (1..9); 0 for the zero ends 0 and 10 and
// for a container the disc lacks.
std::uint32_t xa_container_sector(int base);

}  // namespace sh::loader
