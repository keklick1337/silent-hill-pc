#include "formats/convert/audio/xa_adpcm.h"

namespace sh::convert::audio {
namespace {

// One sound block: 28 samples of one channel. The group has a parameter byte per block at offset 4 + block (bytes 0..3
// repeat bytes 4..7, bytes 8..11 repeat 12..15). The samples start at offset 16, four bytes per sample step: with 4-bit
// samples byte (step * 4 + block / 2) holds block 2k in its low nibble and block 2k+1 in its high nibble; with 8-bit
// samples byte (step * 4 + block) is the sample.
void decode_block(const std::uint8_t* group, unsigned block, unsigned bits, AdpcmState& state, std::int16_t* out,
                  std::size_t stride, XaHistory history) {
    const std::uint8_t param = group[4 + block];
    int shift = param & 0xF;
    const std::int32_t filter = (param >> 4) & 7;
    const std::int32_t k0 = filter < 5 ? kAdpcmK0[filter] : 0;
    const std::int32_t k1 = filter < 5 ? kAdpcmK1[filter] : 0;

    // Shifts above the sample width behave as the widest legal one: 9 for 4-bit samples (hardware quirk), 8 for 8-bit.
    if (bits == 4 && shift > 12) {
        shift = 9;
    } else if (bits == 8 && shift > 8) {
        shift = 8;
    }

    std::int32_t h1 = state.h1;
    std::int32_t h2 = state.h2;
    for (std::size_t j = 0; j < kXaBlockSamples; ++j) {
        std::int32_t scaled;
        if (bits == 4) {
            const std::uint8_t byte = group[16 + j * 4 + (block >> 1)];
            const int nibble = (block & 1) ? (byte >> 4) : (byte & 0xF);
            scaled = static_cast<std::int16_t>(nibble << 12) >> shift;
        } else {
            const std::uint8_t byte = group[16 + j * 4 + block];
            scaled = static_cast<std::int16_t>(byte << 8) >> shift;
        }
        const std::int32_t predicted = scaled + ((h1 * k0 + h2 * k1 + 32) >> 6);
        const std::int16_t sample = saturate16(predicted);
        out[j * stride] = sample;
        h2 = h1;
        h1 = history == XaHistory::Saturated ? sample : predicted;
    }
    state.h1 = h1;
    state.h2 = h2;
}

}  // namespace

std::optional<XaCoding> parse_xa_coding(std::uint8_t coding) {
    const unsigned rate = (coding >> 2) & 3;
    const unsigned depth = (coding >> 4) & 3;
    if (rate > 1 || depth > 1) {
        return std::nullopt;
    }
    XaCoding c;
    c.stereo = (coding & 1) != 0;
    c.sample_rate = rate == 0 ? 37800 : 18900;
    c.bits = depth == 0 ? 4 : 8;
    c.emphasis = (coding & 0x40) != 0;
    return c;
}

bool is_xa_audio_submode(std::uint8_t submode) {
    return (submode & kXaSubmodeForm2) && (submode & kXaSubmodeAudio);
}

std::size_t xa_frames_per_sector(const XaCoding& coding) {
    const std::size_t blocks_per_group = coding.bits == 4 ? 8 : 4;
    const std::size_t blocks_per_channel = coding.stereo ? blocks_per_group / 2 : blocks_per_group;
    return kXaGroups * blocks_per_channel * kXaBlockSamples;
}

void decode_xa_sector(std::span<const std::uint8_t, kXaSectorBytes> sector, const XaCoding& coding,
                      AdpcmState (&state)[2], std::int16_t* out, XaHistory history) {
    const unsigned blocks_per_group = coding.bits == 4 ? 8 : 4;
    const std::size_t channels = coding.stereo ? 2 : 1;
    for (std::size_t g = 0; g < kXaGroups; ++g) {
        const std::uint8_t* group = sector.data() + kXaSubheaderBytes + g * kXaGroupBytes;
        std::int16_t* group_out = out + g * (blocks_per_group / channels) * kXaBlockSamples * channels;
        if (coding.stereo) {
            for (unsigned pair = 0; pair < blocks_per_group / 2; ++pair) {
                std::int16_t* pair_out = group_out + pair * kXaBlockSamples * 2;
                decode_block(group, pair * 2, coding.bits, state[0], pair_out, 2, history);
                decode_block(group, pair * 2 + 1, coding.bits, state[1], pair_out + 1, 2, history);
            }
        } else {
            for (unsigned block = 0; block < blocks_per_group; ++block) {
                decode_block(group, block, coding.bits, state[0], group_out + block * kXaBlockSamples, 1, history);
            }
        }
    }
}

}  // namespace sh::convert::audio
