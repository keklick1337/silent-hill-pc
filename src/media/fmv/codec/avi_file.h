// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// An AVI file as the movie player needs it: the two stream formats and the picture and sound chunks in file
// order. Written from the RIFF/AVI and OpenDML descriptions. The chunks are found by walking every `movi` list
// (of the first `RIFF AVI ` segment and of the `RIFF AVIX` segments that follow it in files past 1 GB), never from
// the `idx1` index: writers disagree on what its offsets count from, it does not cover the extension segments,
// and some files have none.

#include "formats/common/byte_source.h"
#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace sh::fmv {

// The picture stream's BITMAPINFOHEADER.
struct AviVideoFormat {
    int width = 0;
    int height = 0;  // negative: the rows of an uncompressed frame run top to bottom
    int bits_per_pixel = 0;
    std::array<char, 4> fourcc{};  // all zero for uncompressed RGB
};

// The sound stream's WAVEFORMATEX; an extensible format is given by the tag of its subformat.
struct AviAudioFormat {
    int format_tag = 0;  // 1 integer PCM, 3 float PCM
    int channels = 0;
    int sample_rate = 0;
    int bits_per_sample = 0;
};

class AviFile {
public:
    enum class ChunkKind : std::uint8_t { Picture, Sound };
    struct Chunk {
        ChunkKind kind;
        std::uint32_t size;
        std::uint64_t offset;  // of the payload in the file
    };

    // Fails when the source is not an AVI file or holds no picture or sound chunk.
    static Result<AviFile> open(std::unique_ptr<ByteSource> source);

    // The frame period the main header gives, 0 when it gives none.
    std::uint32_t microseconds_per_frame() const { return microseconds_per_frame_; }
    std::uint32_t declared_frames() const { return declared_frames_; }
    const AviVideoFormat& video() const { return video_; }
    const AviAudioFormat& audio() const { return audio_; }

    const std::vector<Chunk>& chunks() const { return chunks_; }
    // The payload of a chunk. A chunk that runs past the end of the file comes back with zeros for the missing part.
    void read(const Chunk& chunk, std::vector<std::uint8_t>& out) const;

private:
    AviFile() = default;
    void scan_segment(std::uint64_t begin, std::uint64_t end, bool first);
    void scan_headers(std::uint64_t begin, std::uint64_t end);
    void scan_stream_list(std::uint64_t begin, std::uint64_t end);
    void scan_movie_data(std::uint64_t begin, std::uint64_t end);

    std::shared_ptr<const ByteSource> source_;
    std::uint32_t microseconds_per_frame_ = 0;
    std::uint32_t declared_frames_ = 0;
    AviVideoFormat video_;
    AviAudioFormat audio_;
    std::vector<Chunk> chunks_;
};

}  // namespace sh::fmv
