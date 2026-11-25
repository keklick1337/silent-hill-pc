// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/audio/voice_clip.h"

#include "formats/convert/audio/ogg_opus.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace sh::audio {

namespace {

constexpr std::size_t kSmallestWav = 44;
constexpr std::size_t kLargestWav = 64u * 1024u * 1024u;
constexpr std::uint32_t kPcmTag = 1;
constexpr std::uint32_t kExtensibleTag = 0xFFFE;

std::uint32_t le16(const std::uint8_t* at) { return at[0] | at[1] << 8; }
std::uint32_t le32(const std::uint8_t* at) { return le16(at) | le16(at + 2) << 16; }

bool tagged(const std::uint8_t* at, const char* tag) { return std::memcmp(at, tag, 4) == 0; }

}  // namespace

std::vector<std::int16_t> resample_voice(std::span<const std::int16_t> pcm, int channels, std::uint32_t rate) {
    const std::size_t width = static_cast<std::size_t>(channels);
    const std::uint64_t in_frames = pcm.size() / width;
    const std::uint64_t out_frames = in_frames * kVoiceRate / rate;
    std::vector<std::int16_t> out(out_frames * width);
    for (std::uint64_t frame = 0; frame < out_frames; ++frame) {
        // Output frame `frame` sits at input position frame * rate / 37800.
        const std::uint64_t position = frame * rate;
        const std::uint64_t before = position / kVoiceRate;
        const std::int64_t part = static_cast<std::int64_t>(position % kVoiceRate);
        const std::uint64_t after = before + 1 < in_frames ? before + 1 : before;
        for (std::size_t channel = 0; channel < width; ++channel) {
            const std::int64_t from = pcm[before * width + channel];
            const std::int64_t to = pcm[after * width + channel];
            out[frame * width + channel] = static_cast<std::int16_t>(from + (to - from) * part / kVoiceRate);
        }
    }
    return out;
}

Result<VoiceClip> parse_voice_wav(std::span<const std::uint8_t> file) {
    using R = Result<VoiceClip>;
    if (convert::audio::is_ogg(file)) {
        Result<convert::audio::Wav> opus = convert::audio::read_ogg_opus(file);
        if (!opus) {
            return R::fail("Ogg Opus: " + opus.error());
        }
        VoiceClip clip;
        clip.channels = opus.value().channels;
        clip.pcm = resample_voice(opus.value().samples, clip.channels, opus.value().sample_rate);
        return R::ok(std::move(clip));
    }
    if (file.size() < kSmallestWav || !tagged(file.data(), "RIFF") || !tagged(file.data() + 8, "WAVE")) {
        return R::fail("not a RIFF WAVE file");
    }
    std::uint32_t tag = 0, channels = 0, rate = 0, bits = 0;
    std::span<const std::uint8_t> data;
    bool has_data = false;
    for (std::size_t at = 12; at + 8 <= file.size();) {
        // A chunk that claims to run past the end of the file is cut there.
        const std::size_t length = std::min<std::size_t>(le32(file.data() + at + 4), file.size() - at - 8);
        const std::uint8_t* body = file.data() + at + 8;
        if (tagged(file.data() + at, "fmt ") && length >= 16) {
            tag = le16(body);
            channels = le16(body + 2);
            rate = le32(body + 4);
            bits = le16(body + 14);
            // The extensible header keeps the real tag at the start of its sub-format.
            if (tag == kExtensibleTag && length >= 26) tag = le16(body + 24);
        } else if (tagged(file.data() + at, "data")) {
            data = std::span<const std::uint8_t>(body, length);
            has_data = true;
        }
        at += 8 + length + (length & 1);
    }
    if (!has_data || tag != kPcmTag || (channels != 1 && channels != 2) || rate < 4000 || rate > 96000 ||
        (bits != 8 && bits != 16)) {
        return R::fail("needs 8- or 16-bit PCM, mono or stereo, 4 to 96 kHz (tag " + std::to_string(tag) + ", " +
                       std::to_string(channels) + " channels, " + std::to_string(rate) + " Hz, " +
                       std::to_string(bits) + " bits)");
    }

    VoiceClip clip;
    clip.channels = static_cast<int>(channels);
    clip.rate = rate;
    const std::size_t samples = data.size() / (channels * (bits / 8)) * channels;
    clip.pcm.resize(samples);
    for (std::size_t i = 0; i < samples; ++i) {
        // Eight-bit WAV samples are unsigned around 128.
        clip.pcm[i] = bits == 16 ? static_cast<std::int16_t>(le16(data.data() + 2 * i))
                                 : static_cast<std::int16_t>((static_cast<int>(data[i]) - 128) * 256);
    }
    if (rate != kVoiceRate && rate != kVoiceHalfRate) {
        clip.pcm = resample_voice(clip.pcm, clip.channels, rate);
        clip.rate = kVoiceRate;
    }
    return R::ok(std::move(clip));
}

Result<VoiceClip> load_voice_wav(const std::filesystem::path& path) {
    using R = Result<VoiceClip>;
    std::FILE* file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr) return R::fail("");
    std::vector<std::uint8_t> bytes;
    std::uint8_t block[65536];
    for (std::size_t got; bytes.size() <= kLargestWav && (got = std::fread(block, 1, sizeof block, file)) > 0;) {
        bytes.insert(bytes.end(), block, block + got);
    }
    std::fclose(file);
    if (bytes.size() > kLargestWav) return R::fail("larger than 64 MiB");
    return parse_voice_wav(bytes);
}

}  // namespace sh::audio
