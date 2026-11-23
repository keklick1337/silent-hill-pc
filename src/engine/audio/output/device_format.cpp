// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/audio/output/device_format.h"

namespace sh::audio::output {

namespace {

void put_le(std::uint8_t* out, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) out[i] = static_cast<std::uint8_t>(value >> (8 * i));
}

// The sample moved to the top of a `bits`-wide two's complement word.
std::uint32_t widened(std::int16_t sample, int bits) {
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(sample)) << (bits - 16);
}

}  // namespace

std::uint32_t frame_bytes(SampleFormat format) { return 2u * container_bits(format) / 8u; }

std::uint16_t container_bits(SampleFormat format) {
    switch (format) {
        case SampleFormat::S16: return 16;
        case SampleFormat::S24: return 24;
        case SampleFormat::S24In32:
        case SampleFormat::S32: return 32;
        case SampleFormat::Unknown: break;
    }
    return 0;
}

std::uint16_t valid_bits(SampleFormat format) {
    return format == SampleFormat::S24In32 ? 24 : container_bits(format);
}

void encode_frame(StereoFrame frame, SampleFormat format, std::uint8_t* out) {
    const std::int16_t samples[2] = {frame.left, frame.right};
    for (int channel = 0; channel < 2; ++channel) {
        const std::int16_t sample = samples[channel];
        switch (format) {
            case SampleFormat::S16: put_le(out + channel * 2, widened(sample, 16), 2); break;
            case SampleFormat::S24: put_le(out + channel * 3, widened(sample, 24), 3); break;
            case SampleFormat::S24In32:
            case SampleFormat::S32: put_le(out + channel * 4, widened(sample, 32), 4); break;
            case SampleFormat::Unknown: break;
        }
    }
}

std::uint32_t rate_multiple(std::uint32_t device_rate) {
    for (std::uint32_t multiple = 1; multiple <= kHighestRateMultiple; multiple *= 2) {
        if (device_rate == kMixRate * multiple) return multiple;
    }
    return 0;
}

std::optional<SampleFormat> format_for_layout(const WaveLayout& layout, std::uint32_t device_rate) {
    if (!layout.integer_pcm || layout.channels != 2 || layout.rate != device_rate || rate_multiple(device_rate) == 0) {
        return std::nullopt;
    }
    for (const SampleFormat format :
         {SampleFormat::S16, SampleFormat::S24, SampleFormat::S24In32, SampleFormat::S32}) {
        if (layout.container_bits == container_bits(format) && layout.valid_bits == valid_bits(format) &&
            layout.block_align == frame_bytes(format)) {
            return format;
        }
    }
    return std::nullopt;
}

std::vector<ExclusiveOffer> exclusive_offers() {
    return {{SampleFormat::S16, false},
            {SampleFormat::S16, true},
            {SampleFormat::S24, false},
            {SampleFormat::S24In32, false},
            {SampleFormat::S32, false}};
}

}  // namespace sh::audio::output
