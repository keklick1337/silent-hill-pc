#include "framework/sh_test.h"
#include "formats/convert/audio/spu_adpcm.h"
#include "formats/convert/audio/xa_adpcm.h"

#include <array>
#include <cstdint>
#include <vector>

namespace {

using namespace sh::convert::audio;

using Block = std::array<std::uint8_t, kSpuBlockBytes>;

// A block whose 28 nibbles are `nibble(i)`.
template <typename F>
Block make_block(int shift, int filter, int flags, F nibble) {
    Block b{};
    b[0] = static_cast<std::uint8_t>((filter << 4) | shift);
    b[1] = static_cast<std::uint8_t>(flags);
    for (int i = 0; i < 28; ++i) {
        b[2 + i / 2] |= static_cast<std::uint8_t>((nibble(i) & 0xF) << ((i & 1) ? 4 : 0));
    }
    return b;
}

Block constant_block(int shift, int filter, int flags, int nibble) {
    return make_block(shift, filter, flags, [&](int) { return nibble; });
}

int signed_nibble(int n) {
    n &= 0xF;
    return n >= 8 ? n - 16 : n;
}

std::vector<std::uint8_t> join(std::initializer_list<Block> blocks) {
    std::vector<std::uint8_t> bytes;
    for (const Block& b : blocks) {
        bytes.insert(bytes.end(), b.begin(), b.end());
    }
    return bytes;
}

}  // namespace

SH_TEST(AudioSpuBlockFilterZero) {
    // Shift 12 leaves the sign-extended nibble unchanged.
    const Block b = make_block(12, 0, 0, [](int i) { return i; });
    AdpcmState state;
    std::int16_t out[28];
    const SpuBlockInfo info = decode_spu_block(b, state, out);
    SH_CHECK(info.flags == 0 && !info.shift_clamped && !info.filter_clamped);
    for (int i = 0; i < 28; ++i) {
        SH_CHECK(out[i] == signed_nibble(i));
    }
    SH_CHECK(state.h1 == out[27] && state.h2 == out[26]);
}

SH_TEST(AudioSpuBlockPrediction) {
    // Filter 1: y[n] = x + (60 * y[n-1]) / 64 with an arithmetic shift, no rounding term.
    AdpcmState state;
    std::int16_t out[28];
    decode_spu_block(constant_block(12, 1, 0, 7), state, out);
    SH_CHECK(out[0] == 7);
    SH_CHECK(out[1] == 7 + ((7 * 60) >> 6));    // 13
    SH_CHECK(out[2] == 7 + ((13 * 60) >> 6));   // 19
    // Filter 2 uses both history samples.
    state = AdpcmState{};
    decode_spu_block(constant_block(12, 2, 0, 1), state, out);
    SH_CHECK(out[0] == 1);
    SH_CHECK(out[1] == 1 + ((1 * 115) >> 6));                   // 2
    SH_CHECK(out[2] == 1 + ((2 * 115 + 1 * -52) >> 6));         // 1 + 2 = 3
    // The history crosses the block boundary.
    state = AdpcmState{};
    std::int16_t first[28], second[28];
    decode_spu_block(constant_block(12, 1, 0, 7), state, first);
    decode_spu_block(constant_block(12, 1, 0, 7), state, second);
    SH_CHECK(second[0] == 7 + ((first[27] * 60) >> 6));
}

SH_TEST(AudioSpuBlockSaturates) {
    AdpcmState state;
    std::int16_t out[28];
    decode_spu_block(constant_block(0, 1, 0, 7), state, out);  // 28672, then 28672 + 26880 > 32767
    SH_CHECK(out[0] == 28672);
    SH_CHECK(out[1] == 32767);
    SH_CHECK(out[2] == 32767);
    SH_CHECK(state.h1 == 32767);  // the history keeps the saturated value
    state = AdpcmState{};
    decode_spu_block(constant_block(0, 1, 0, 8), state, out);  // -32768 and below
    SH_CHECK(out[0] == -32768 && out[1] == -32768);
}

SH_TEST(AudioSpuBlockQuirks) {
    // Shifts 13..15 behave as 9, filters above 4 as 4: the same choices as the game's SPU core.
    AdpcmState a, b;
    std::int16_t out_a[28], out_b[28];
    const SpuBlockInfo clamped = decode_spu_block(constant_block(13, 0, 0x03, 5), a, out_a);
    const SpuBlockInfo plain = decode_spu_block(constant_block(9, 0, 0x03, 5), b, out_b);
    SH_CHECK(clamped.shift_clamped && !plain.shift_clamped);
    SH_CHECK(clamped.flags == 3);
    for (int i = 0; i < 28; ++i) {
        SH_CHECK(out_a[i] == out_b[i]);
    }
    a = b = AdpcmState{};
    const SpuBlockInfo odd = decode_spu_block(constant_block(12, 9, 0, 3), a, out_a);
    decode_spu_block(constant_block(12, 4, 0, 3), b, out_b);
    SH_CHECK(odd.filter_clamped);
    for (int i = 0; i < 28; ++i) {
        SH_CHECK(out_a[i] == out_b[i]);
    }
}

SH_TEST(AudioSpuSampleFlags) {
    // Loop: start flag on block 1, end + repeat on block 3, one more block that is never played.
    {
        const auto bytes = join({constant_block(12, 0, 0x00, 1), constant_block(12, 0, kSpuFlagStart, 1),
                                 constant_block(12, 0, 0x00, 1), constant_block(12, 0, kSpuFlagEnd | kSpuFlagRepeat, 1),
                                 constant_block(12, 0, 0x07, 0)});
        const auto sample = decode_spu_sample(bytes);
        SH_CHECK(static_cast<bool>(sample));
        if (sample) {
            const SpuSample& s = sample.value();
            SH_CHECK(s.blocks_in == 5 && s.blocks_played == 4 && s.pcm.size() == 4 * 28);
            SH_CHECK(s.has_end);
            SH_CHECK(s.loop && s.loop->start == 28 && s.loop->end == 4 * 28 - 1);
        }
    }
    // One-shot: end without repeat; the dummy loop block after it is dropped.
    {
        const auto bytes = join({constant_block(12, 0, 0x00, 1), constant_block(12, 0, 0x00, 1),
                                 constant_block(12, 0, kSpuFlagEnd, 1), constant_block(12, 0, 0x07, 0)});
        const auto sample = decode_spu_sample(bytes);
        SH_CHECK(static_cast<bool>(sample));
        if (sample) {
            SH_CHECK(sample.value().blocks_played == 3 && !sample.value().loop && sample.value().has_end);
        }
    }
    // A repeat end without a start flag loops from the first block.
    {
        const auto bytes = join({constant_block(12, 0, 0x00, 1), constant_block(12, 0, 0x03, 1)});
        const auto sample = decode_spu_sample(bytes);
        SH_CHECK(sample && sample.value().loop && sample.value().loop->start == 0 && sample.value().loop->end == 55);
    }
    // No end flag: everything is decoded; bytes after the last whole block are reported.
    {
        auto bytes = join({constant_block(12, 0, 0x00, 1), constant_block(12, 0, 0x00, 1)});
        bytes.push_back(0xAA);
        const auto sample = decode_spu_sample(bytes);
        SH_CHECK(sample && !sample.value().has_end && sample.value().blocks_played == 2 &&
                 sample.value().trailing_bytes == 1);
    }
    // The decoder state carries across blocks inside a sample, as in the game's core.
    {
        const auto bytes = join({constant_block(12, 1, 0, 7), constant_block(12, 1, kSpuFlagEnd, 7)});
        const auto sample = decode_spu_sample(bytes);
        AdpcmState state;
        std::int16_t a[28], b[28];
        decode_spu_block(constant_block(12, 1, 0, 7), state, a);
        decode_spu_block(constant_block(12, 1, 0, 7), state, b);
        SH_CHECK(sample && sample.value().pcm[28] == b[0] && sample.value().pcm[55] == b[27]);
    }
}

SH_TEST(AudioXaCoding) {
    auto c = parse_xa_coding(0x01);
    SH_CHECK(c && c->stereo && c->sample_rate == 37800 && c->bits == 4 && !c->emphasis);
    c = parse_xa_coding(0x05);
    SH_CHECK(c && c->stereo && c->sample_rate == 18900 && c->bits == 4);
    c = parse_xa_coding(0x10);
    SH_CHECK(c && !c->stereo && c->sample_rate == 37800 && c->bits == 8);
    c = parse_xa_coding(0x41);
    SH_CHECK(c && c->emphasis);
    SH_CHECK(!parse_xa_coding(0x08));  // rate code 2 is reserved
    SH_CHECK(!parse_xa_coding(0x20));  // depth code 2 is reserved

    SH_CHECK(is_xa_audio_submode(0x64) && is_xa_audio_submode(0xE4));
    SH_CHECK(!is_xa_audio_submode(0x04));  // audio without Form 2
    SH_CHECK(!is_xa_audio_submode(0x48));  // data
    SH_CHECK(!is_xa_audio_submode(0x00));

    SH_CHECK(xa_frames_per_sector(XaCoding{true, 37800, 4, false}) == 2016);
    SH_CHECK(xa_frames_per_sector(XaCoding{false, 37800, 4, false}) == 4032);
    SH_CHECK(xa_frames_per_sector(XaCoding{true, 37800, 8, false}) == 1008);
    SH_CHECK(xa_frames_per_sector(XaCoding{false, 18900, 8, false}) == 2016);
}

namespace {

std::array<std::uint8_t, kXaSectorBytes> blank_sector(std::uint8_t coding) {
    std::array<std::uint8_t, kXaSectorBytes> s{};
    s[0] = 1;
    s[1] = 0;
    s[2] = 0x64;
    s[3] = coding;
    s[4] = s[0];
    s[5] = s[1];
    s[6] = s[2];
    s[7] = s[3];
    return s;
}

}  // namespace

SH_TEST(AudioXaSectorStereo4Bit) {
    auto sector = blank_sector(0x01);
    for (std::size_t g = 0; g < kXaGroups; ++g) {
        std::uint8_t* group = sector.data() + kXaSubheaderBytes + g * kXaGroupBytes;
        for (int p = 0; p < 16; ++p) {
            group[p] = 0x0C;  // shift 12, filter 0
        }
        for (int j = 0; j < 28; ++j) {
            for (int k = 0; k < 4; ++k) {
                group[16 + j * 4 + k] = 0x21;  // low nibble 1 for the left blocks, high nibble 2 for the right
            }
        }
    }
    AdpcmState state[2];
    std::vector<std::int16_t> out(2016 * 2, 99);
    decode_xa_sector(sector, XaCoding{true, 37800, 4, false}, state, out.data());
    bool ok = true;
    for (std::size_t f = 0; f < 2016; ++f) {
        ok = ok && out[f * 2] == 1 && out[f * 2 + 1] == 2;
    }
    SH_CHECK(ok);
}

SH_TEST(AudioXaSectorMono4Bit) {
    auto sector = blank_sector(0x00);
    for (std::size_t g = 0; g < kXaGroups; ++g) {
        std::uint8_t* group = sector.data() + kXaSubheaderBytes + g * kXaGroupBytes;
        for (int p = 0; p < 16; ++p) {
            group[p] = 0x0C;
        }
        for (int j = 0; j < 28; ++j) {
            for (int k = 0; k < 4; ++k) {
                group[16 + j * 4 + k] = static_cast<std::uint8_t>(((2 * k + 1) << 4) | (2 * k));  // blocks 2k, 2k+1
            }
        }
    }
    AdpcmState state[2];
    std::vector<std::int16_t> out(4032, 99);
    decode_xa_sector(sector, XaCoding{false, 37800, 4, false}, state, out.data());
    // Eight blocks one after the other: block b holds the value b.
    bool ok = true;
    for (std::size_t g = 0; g < kXaGroups; ++g) {
        for (std::size_t b = 0; b < 8; ++b) {
            for (std::size_t j = 0; j < 28; ++j) {
                ok = ok && out[(g * 8 + b) * 28 + j] == static_cast<int>(b);
            }
        }
    }
    SH_CHECK(ok);
}

SH_TEST(AudioXaSectorStereo8Bit) {
    auto sector = blank_sector(0x11);
    for (std::size_t g = 0; g < kXaGroups; ++g) {
        std::uint8_t* group = sector.data() + kXaSubheaderBytes + g * kXaGroupBytes;
        for (int p = 0; p < 16; ++p) {
            group[p] = 0x08;  // shift 8 leaves the signed byte unchanged
        }
        for (int j = 0; j < 28; ++j) {
            for (int k = 0; k < 4; ++k) {
                group[16 + j * 4 + k] = static_cast<std::uint8_t>(k == 0 ? 0xFF : k + 1);  // -1, 2, 3, 4
            }
        }
    }
    AdpcmState state[2];
    std::vector<std::int16_t> out(1008 * 2, 99);
    decode_xa_sector(sector, XaCoding{true, 37800, 8, false}, state, out.data());
    bool ok = true;
    for (std::size_t g = 0; g < kXaGroups; ++g) {
        for (std::size_t j = 0; j < 28; ++j) {
            const std::int16_t* pair0 = &out[(g * 56 + j) * 2];
            const std::int16_t* pair1 = &out[(g * 56 + 28 + j) * 2];
            ok = ok && pair0[0] == -1 && pair0[1] == 2 && pair1[0] == 3 && pair1[1] == 4;
        }
    }
    SH_CHECK(ok);
}

SH_TEST(AudioXaChannelsKeepSeparateHistory) {
    auto sector = blank_sector(0x01);
    for (std::size_t g = 0; g < kXaGroups; ++g) {
        std::uint8_t* group = sector.data() + kXaSubheaderBytes + g * kXaGroupBytes;
        for (int p = 0; p < 16; ++p) {
            group[p] = 0x1C;  // shift 12, filter 1
        }
        for (int j = 0; j < 28; ++j) {
            for (int k = 0; k < 4; ++k) {
                group[16 + j * 4 + k] = 0x70;  // left 0, right 7
            }
        }
    }
    AdpcmState state[2];
    std::vector<std::int16_t> out(2016 * 2);
    decode_xa_sector(sector, XaCoding{true, 37800, 4, false}, state, out.data());
    SH_CHECK(out[0] == 0 && out[2] == 0);
    SH_CHECK(out[1] == 7);
    // Sector decoder uses +32 rounding: y = 7 + ((7 * 60 + 32) >> 6) = 7 + 7.
    SH_CHECK(out[3] == 14);
    SH_CHECK(state[0].h1 == 0 && state[1].h1 == out[2016 * 2 - 1]);
}
