// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/fmv/codec/str_stream.h"

#include "media/fmv/codec/movie_sound.h"

#include <utility>

namespace sh::fmv {

namespace {

constexpr std::size_t kSubheaderBytes = 8;
constexpr std::size_t kPictureHeaderBytes = 32;
constexpr std::uint8_t kSubmodeSound = 0x04;

std::uint32_t u16_at(const std::uint8_t* p) { return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8; }
std::uint32_t u32_at(const std::uint8_t* p) { return u16_at(p) | u16_at(p + 2) << 16; }

}  // namespace

StrStream::StrStream(std::uint32_t sector_count, SectorReader reader) : count_(sector_count), read_(std::move(reader)) {}

StrStream::Status StrStream::next(StrFrame& frame) {
    frame.bitstream.clear();
    bool started = false;
    std::uint32_t expected_place = 0;
    // A frame whose first sector does not start with the eight bytes its header promises is passed over whole.
    bool skipping = false;
    std::uint32_t skipped_number = 0;
    std::uint8_t sector[kMovieSectorBytes];

    for (;;) {
        const bool have = at_ < count_ && read_ && read_(at_, sector);
        if (at_ < count_) ++at_;
        // What was gathered of a frame cut short still goes out.
        if (!have) return started ? Status::Frame : Status::End;

        if ((sector[2] & kSubmodeSound) != 0) {
            if (sound_) sound_(sector);
            continue;
        }
        const std::uint8_t* header = sector + kSubheaderBytes;
        // Filler sectors sit between the picture sectors, also in the middle of a frame.
        if (u16_at(header) != 0x0160 || u16_at(header + 2) != 0x8001) continue;

        const std::uint32_t place = u16_at(header + 4);
        const std::uint32_t frame_sectors = u16_at(header + 6);
        const std::uint32_t number = u32_at(header + 8);
        const std::uint8_t* chunk = header + kPictureHeaderBytes;

        if (skipping) {
            if (number == skipped_number) continue;
            skipping = false;
        }
        if (!started) {
            if (place == 0 && (u32_at(chunk) != u32_at(header + 20) || u32_at(chunk + 4) != u32_at(header + 24))) {
                skipping = true;
                skipped_number = number;
                continue;
            }
            frame.width = static_cast<int>(u16_at(header + 16));
            frame.height = static_cast<int>(u16_at(header + 18));
            frame.number = number;
            started = true;
        } else if (number != frame.number) {
            // The next frame began before this one had all its sectors: that sector is read again next time.
            --at_;
            return Status::Frame;
        }

        if (place != expected_place) return Status::OutOfOrder;
        if (expected_place >= static_cast<std::uint32_t>(kMaxFrameSectors)) return Status::TooLarge;
        frame.bitstream.insert(frame.bitstream.end(), chunk, chunk + kChunkBytes);
        ++expected_place;

        if (expected_place >= frame_sectors) {
            // The header counts the bitstream in 16-bit words; the tail of the last sector is padding.
            const std::uint64_t bytes = static_cast<std::uint64_t>(u32_at(header + 12)) * 2;
            if (bytes < frame.bitstream.size()) frame.bitstream.resize(static_cast<std::size_t>(bytes));
            return Status::Frame;
        }
    }
}

}  // namespace sh::fmv
