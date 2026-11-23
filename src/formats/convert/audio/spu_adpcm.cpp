#include "formats/convert/audio/spu_adpcm.h"

#include <algorithm>

namespace sh::convert::audio {

SpuBlockInfo decode_spu_block(std::span<const std::uint8_t, kSpuBlockBytes> block, AdpcmState& state,
                              std::int16_t* out28) {
    int shift = block[0] & 0xF;
    int filter = block[0] >> 4;
    SpuBlockInfo info{block[1], false, false};
    if (shift >= 13) {
        shift = 9;
        info.shift_clamped = true;
    }
    if (filter > 4) {
        filter = 4;
        info.filter_clamped = true;
    }
    const std::int32_t k0 = kAdpcmK0[filter];
    const std::int32_t k1 = kAdpcmK1[filter];

    std::int32_t h1 = state.h1;
    std::int32_t h2 = state.h2;
    for (std::size_t i = 0; i < kSpuBlockSamples; ++i) {
        const std::uint8_t byte = block[2 + (i >> 1)];
        const int nibble = (i & 1) ? (byte >> 4) : (byte & 0xF);
        // The nibble goes to the top of a signed 16-bit value, so the shift keeps its sign.
        const std::int32_t scaled = static_cast<std::int16_t>(nibble << 12) >> shift;
        const std::int32_t sample = saturate16(scaled + ((h1 * k0 + h2 * k1) >> 6));
        out28[i] = static_cast<std::int16_t>(sample);
        h2 = h1;
        h1 = sample;
    }
    state.h1 = h1;
    state.h2 = h2;
    return info;
}

Result<SpuSample> decode_spu_sample(std::span<const std::uint8_t> data) {
    SpuSample sample;
    sample.blocks_in = data.size() / kSpuBlockBytes;
    sample.trailing_bytes = data.size() % kSpuBlockBytes;
    sample.pcm.reserve(sample.blocks_in * kSpuBlockSamples);

    AdpcmState state;
    std::size_t repeat_block = 0;
    for (std::size_t b = 0; b < sample.blocks_in; ++b) {
        const auto block = data.subspan(b * kSpuBlockBytes).first<kSpuBlockBytes>();
        std::int16_t out[kSpuBlockSamples];
        const SpuBlockInfo info = decode_spu_block(block, state, out);
        sample.clamped_shifts += info.shift_clamped;
        sample.clamped_filters += info.filter_clamped;
        sample.pcm.insert(sample.pcm.end(), out, out + kSpuBlockSamples);
        ++sample.blocks_played;

        // The hardware latches the repeat address when it reaches a block with the start flag.
        if (info.flags & kSpuFlagStart) {
            repeat_block = b;
        }
        if (info.flags & kSpuFlagEnd) {
            sample.has_end = true;
            if (info.flags & kSpuFlagRepeat) {
                sample.loop = WavLoop{static_cast<std::uint32_t>(repeat_block * kSpuBlockSamples),
                                      static_cast<std::uint32_t>((b + 1) * kSpuBlockSamples - 1)};
            }
            break;
        }
    }
    return Result<SpuSample>::ok(std::move(sample));
}

}  // namespace sh::convert::audio
