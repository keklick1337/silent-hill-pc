// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A movie file of the disc as a run of frames. The file is a run of 2336-byte sectors (subheader first): picture
// sectors, sound sectors (subheader submode bit 2) and filler. A picture sector carries, after the 8-byte
// subheader, a 32-byte header and 2016 bytes of the frame's bitstream:
//   +0  u16 0x0160, +2 u16 0x8001   the mark of a picture sector
//   +4  u16 the sector's place in its frame, +6 u16 the frame's sector count
//   +8  u32 the frame number (from 1), +12 u32 the frame's bitstream length in 16-bit words
//   +16 u16 width, +18 u16 height
//   +20 u32, +24 u32 the first eight bytes of the frame's bitstream, repeated in every one of its sectors
// The stream hands the sound sectors to a sink as it passes them and joins the picture sectors into frames.

#include <cstdint>
#include <functional>
#include <vector>

namespace sh::fmv {

struct StrFrame {
    int width = 0;
    int height = 0;
    std::uint32_t number = 0;
    std::vector<std::uint8_t> bitstream;  // for MdecDecoder
};

class StrStream {
public:
    // Fills `dst` (2336 bytes) with sector `index` of the movie; false when it cannot be read.
    using SectorReader = std::function<bool(std::uint32_t index, std::uint8_t* dst)>;
    using SoundSink = std::function<void(const std::uint8_t* sector)>;

    enum class Status {
        Frame,       // `frame` is the next one
        End,         // no more frames: the file ended or a sector could not be read
        OutOfOrder,  // a frame's sectors do not come in their order
        TooLarge,    // a frame of more than kMaxFrameSectors sectors
    };

    static constexpr int kMaxFrameSectors = 16;
    static constexpr std::size_t kChunkBytes = 2016;

    StrStream(std::uint32_t sector_count, SectorReader reader);
    void set_sound_sink(SoundSink sink) { sound_ = std::move(sink); }

    // Reads up to the end of the next frame. Sound sectors met on the way go to the sink first.
    Status next(StrFrame& frame);

private:
    std::uint32_t count_;
    std::uint32_t at_ = 0;
    SectorReader read_;
    SoundSink sound_;
};

}  // namespace sh::fmv
