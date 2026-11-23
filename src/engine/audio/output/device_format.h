// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The sample layouts a device may ask for, and the mix written in them. The mix is 16-bit, so every layout holds
// it exactly: a wider one carries the sample in its top bits.

#include "engine/audio/output/frame_queue.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace sh::audio::output {

// The rate the engine mixes at. A device runs at it or at a whole multiple of it.
inline constexpr std::uint32_t kMixRate = 44100;
inline constexpr std::uint32_t kHighestRateMultiple = 8;

enum class SampleFormat : int {
    Unknown,
    S16,      // two bytes
    S24,      // three bytes, packed
    S24In32,  // four bytes, the sample in the upper three
    S32,      // four bytes
};

// Bytes of one stereo frame; 0 for Unknown.
std::uint32_t frame_bytes(SampleFormat format);
// Bits of one sample's container and the bits of it that carry sound.
std::uint16_t container_bits(SampleFormat format);
std::uint16_t valid_bits(SampleFormat format);

// Writes one frame at `out` (frame_bytes(format) bytes), left first, in the byte order of the machine's WAVE
// formats (little-endian).
void encode_frame(StereoFrame frame, SampleFormat format, std::uint8_t* out);

// How many times a device at `device_rate` plays each mix frame: 1, 2, 4 or 8; 0 when the rate is not one the
// output supports.
std::uint32_t rate_multiple(std::uint32_t device_rate);

// A PCM stream format as a device describes it, without the platform's own structures.
struct WaveLayout {
    bool integer_pcm = false;  // false: floating point or a compressed tag
    std::uint16_t channels = 0;
    std::uint32_t rate = 0;
    std::uint16_t container_bits = 0;
    std::uint16_t valid_bits = 0;
    std::uint16_t block_align = 0;
};

// The format to write for a device mixing in `layout`, when the mix can be handed over unchanged: two channels
// of integer PCM at exactly `device_rate` in one of the layouts above. Nothing otherwise — the output never
// resamples and never converts to floating point.
std::optional<SampleFormat> format_for_layout(const WaveLayout& layout, std::uint32_t device_rate);

// The formats to offer a device opened for exclusive use, the mix's own first.
struct ExclusiveOffer {
    SampleFormat format = SampleFormat::S16;
    // Described by the plain 16-byte PCM header instead of the extensible one: some drivers take only that.
    bool plain_header = false;
};
std::vector<ExclusiveOffer> exclusive_offers();

}  // namespace sh::audio::output
