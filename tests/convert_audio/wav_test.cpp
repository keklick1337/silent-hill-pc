#include "framework/sh_test.h"
#include "formats/convert/audio/wav.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using sh::convert::audio::read_wav;
using sh::convert::audio::Wav;
using sh::convert::audio::WavLoop;
using sh::convert::audio::WavSampler;
using sh::convert::audio::write_wav;

std::uint32_t le32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return std::uint32_t{b[at]} | (std::uint32_t{b[at + 1]} << 8) | (std::uint32_t{b[at + 2]} << 16) |
           (std::uint32_t{b[at + 3]} << 24);
}

Wav ramp(std::uint16_t channels, std::size_t frames) {
    Wav wav;
    wav.channels = channels;
    wav.sample_rate = 37800;
    for (std::size_t i = 0; i < frames * channels; ++i) {
        wav.samples.push_back(static_cast<std::int16_t>(static_cast<int>(i * 997) - 30000));
    }
    return wav;
}

}  // namespace

SH_TEST(AudioWavHeaderLayout) {
    const Wav wav = ramp(2, 10);
    const auto bytes = write_wav(wav);
    SH_CHECK(static_cast<bool>(bytes));
    if (!bytes) {
        return;
    }
    const auto& b = bytes.value();
    SH_CHECK(b.size() == 44 + 10 * 2 * 2);
    SH_CHECK(std::memcmp(b.data(), "RIFF", 4) == 0);
    SH_CHECK(le32(b, 4) == b.size() - 8);
    SH_CHECK(std::memcmp(b.data() + 8, "WAVEfmt ", 8) == 0);
    SH_CHECK(le32(b, 16) == 16);
    SH_CHECK(b[20] == 1 && b[22] == 2);        // PCM, two channels
    SH_CHECK(le32(b, 24) == 37800);            // rate
    SH_CHECK(le32(b, 28) == 37800u * 4);       // byte rate
    SH_CHECK(b[32] == 4 && b[34] == 16);       // block align, bits
    SH_CHECK(std::memcmp(b.data() + 36, "data", 4) == 0);
    SH_CHECK(le32(b, 40) == 40);
}

SH_TEST(AudioWavRoundTrip) {
    for (const std::uint16_t channels : {1, 2}) {
        Wav wav = ramp(channels, 100);
        const auto bytes = write_wav(wav);
        SH_CHECK(static_cast<bool>(bytes));
        if (!bytes) {
            continue;
        }
        const auto back = read_wav(bytes.value());
        SH_CHECK(static_cast<bool>(back));
        if (!back) {
            continue;
        }
        SH_CHECK(back.value().channels == channels);
        SH_CHECK(back.value().sample_rate == 37800);
        SH_CHECK(back.value().samples == wav.samples);
        SH_CHECK(!back.value().sampler.has_value());
    }
}

SH_TEST(AudioWavSamplerChunk) {
    Wav wav = ramp(1, 100);
    wav.sampler = WavSampler{};
    wav.sampler->unity_note = 48;
    wav.sampler->loop = WavLoop{28, 99};
    const auto bytes = write_wav(wav);
    SH_CHECK(static_cast<bool>(bytes));
    if (!bytes) {
        return;
    }
    const auto back = read_wav(bytes.value());
    SH_CHECK(static_cast<bool>(back));
    if (!back) {
        return;
    }
    SH_CHECK(back.value().sampler.has_value());
    SH_CHECK(back.value().sampler->unity_note == 48);
    SH_CHECK(back.value().sampler->loop.has_value());
    SH_CHECK(back.value().sampler->loop->start == 28 && back.value().sampler->loop->end == 99);

    // Without a loop the chunk is still there and holds only the root key.
    wav.sampler->loop.reset();
    const auto plain = write_wav(wav);
    SH_CHECK(static_cast<bool>(plain));
    if (plain) {
        const auto again = read_wav(plain.value());
        SH_CHECK(again && again.value().sampler && !again.value().sampler->loop);
    }
}

SH_TEST(AudioWavRejectsBadInput) {
    Wav wav = ramp(1, 8);
    wav.channels = 3;
    SH_CHECK(!write_wav(wav));
    wav = ramp(1, 8);
    wav.sample_rate = 0;
    SH_CHECK(!write_wav(wav));
    wav = ramp(2, 8);
    wav.samples.pop_back();
    SH_CHECK(!write_wav(wav));
    wav = ramp(1, 8);
    wav.sampler = WavSampler{60, WavLoop{4, 8}};  // the loop ends outside the sample
    SH_CHECK(!write_wav(wav));
    wav.sampler = WavSampler{200, std::nullopt};
    SH_CHECK(!write_wav(wav));

    const auto good = write_wav(ramp(1, 8));
    SH_CHECK(static_cast<bool>(good));
    if (!good) {
        return;
    }
    std::vector<std::uint8_t> bytes = good.value();
    SH_CHECK(!read_wav(std::span<const std::uint8_t>(bytes.data(), 11)));
    SH_CHECK(!read_wav(std::span<const std::uint8_t>(bytes.data(), bytes.size() - 1)));  // RIFF size larger than the file
    bytes[20] = 3;                                                                      // float, not PCM
    SH_CHECK(!read_wav(bytes));
    bytes = good.value();
    bytes[34] = 8;  // 8 bits
    SH_CHECK(!read_wav(bytes));
    bytes = good.value();
    bytes[0] = 'X';
    SH_CHECK(!read_wav(bytes));
}

SH_TEST(AudioWavSkipsUnknownChunks) {
    // A LIST chunk of odd size (with its pad byte) between fmt and data.
    const Wav wav = ramp(1, 4);
    const auto base = write_wav(wav);
    SH_CHECK(static_cast<bool>(base));
    if (!base) {
        return;
    }
    std::vector<std::uint8_t> bytes(base.value().begin(), base.value().begin() + 36);
    const std::uint8_t list[] = {'L', 'I', 'S', 'T', 3, 0, 0, 0, 'a', 'b', 'c', 0};
    bytes.insert(bytes.end(), std::begin(list), std::end(list));
    bytes.insert(bytes.end(), base.value().begin() + 36, base.value().end());
    const std::uint32_t riff = static_cast<std::uint32_t>(bytes.size() - 8);
    for (int i = 0; i < 4; ++i) {
        bytes[4 + i] = static_cast<std::uint8_t>(riff >> (8 * i));
    }
    const auto back = read_wav(bytes);
    SH_CHECK(static_cast<bool>(back));
    if (back) {
        SH_CHECK(back.value().samples == wav.samples);
    }
}
