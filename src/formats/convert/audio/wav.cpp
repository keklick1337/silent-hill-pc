#include "formats/convert/audio/wav.h"

#include <cstring>
#include <limits>
#include <string>

namespace sh::convert::audio {
namespace {

constexpr std::uint32_t kFmtBytes = 16;
constexpr std::uint32_t kSmplBaseBytes = 36;
constexpr std::uint32_t kSmplLoopBytes = 24;

void put16(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    put16(out, v & 0xFFFF);
    put16(out, v >> 16);
}

void put_tag(std::vector<std::uint8_t>& out, const char (&tag)[5]) {
    out.insert(out.end(), tag, tag + 4);
}

std::uint32_t get16(const std::uint8_t* p) {
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8);
}

std::uint32_t get32(const std::uint8_t* p) {
    return get16(p) | (get16(p + 2) << 16);
}

bool loop_is_valid(const Wav& wav) {
    return wav.sampler && wav.sampler->loop && wav.sampler->loop->start <= wav.sampler->loop->end &&
           wav.sampler->loop->end < wav.frames();
}

}  // namespace

Result<std::vector<std::uint8_t>> write_wav(const Wav& wav) {
    using R = Result<std::vector<std::uint8_t>>;
    if (wav.channels < 1 || wav.channels > 2) {
        return R::fail("wav: only mono and stereo are supported");
    }
    if (wav.sample_rate == 0) {
        return R::fail("wav: sample rate is zero");
    }
    if (wav.samples.size() % wav.channels != 0) {
        return R::fail("wav: sample count is not a whole number of frames");
    }
    if (wav.sampler) {
        if (wav.sampler->unity_note > 127) {
            return R::fail("wav: unity note is outside 0..127");
        }
        if (wav.sampler->loop && !loop_is_valid(wav)) {
            return R::fail("wav: loop points are outside the sample");
        }
    }

    const std::uint64_t data_bytes = std::uint64_t{wav.samples.size()} * 2;
    const std::uint32_t smpl_bytes =
        wav.sampler ? kSmplBaseBytes + (wav.sampler->loop ? kSmplLoopBytes : 0) : 0;
    const std::uint64_t riff_bytes = 4 + (8 + kFmtBytes) + (wav.sampler ? 8 + smpl_bytes : 0) + 8 + data_bytes;
    if (riff_bytes > std::numeric_limits<std::uint32_t>::max()) {
        return R::fail("wav: more than 4 GiB of audio");
    }

    std::vector<std::uint8_t> out;
    out.reserve(static_cast<std::size_t>(riff_bytes) + 8);
    put_tag(out, "RIFF");
    put32(out, static_cast<std::uint32_t>(riff_bytes));
    put_tag(out, "WAVE");

    put_tag(out, "fmt ");
    put32(out, kFmtBytes);
    put16(out, 1);  // WAVE_FORMAT_PCM
    put16(out, wav.channels);
    put32(out, wav.sample_rate);
    put32(out, wav.sample_rate * wav.channels * 2);
    put16(out, wav.channels * 2u);
    put16(out, 16);

    if (wav.sampler) {
        put_tag(out, "smpl");
        put32(out, smpl_bytes);
        put32(out, 0);  // manufacturer
        put32(out, 0);  // product
        put32(out, 1000000000u / wav.sample_rate);
        put32(out, wav.sampler->unity_note);
        put32(out, 0);  // MIDI pitch fraction
        put32(out, 0);  // SMPTE format
        put32(out, 0);  // SMPTE offset
        put32(out, wav.sampler->loop ? 1 : 0);
        put32(out, 0);  // sampler data
        if (wav.sampler->loop) {
            put32(out, 0);  // cue point id
            put32(out, 0);  // forward loop
            put32(out, wav.sampler->loop->start);
            put32(out, wav.sampler->loop->end);
            put32(out, 0);  // fraction
            put32(out, 0);  // play count: infinite
        }
    }

    put_tag(out, "data");
    put32(out, static_cast<std::uint32_t>(data_bytes));
    for (const std::int16_t s : wav.samples) {
        put16(out, static_cast<std::uint16_t>(s));
    }
    return R::ok(std::move(out));
}

Result<Wav> read_wav(std::span<const std::uint8_t> bytes) {
    using R = Result<Wav>;
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        return R::fail("wav: not a RIFF/WAVE file");
    }
    const std::uint64_t riff_end = std::uint64_t{get32(bytes.data() + 4)} + 8;
    if (riff_end > bytes.size()) {
        return R::fail("wav: RIFF size is larger than the file");
    }

    Wav wav;
    bool have_fmt = false;
    bool have_data = false;
    std::uint64_t pos = 12;
    while (pos + 8 <= riff_end) {
        const std::uint8_t* head = bytes.data() + pos;
        const std::uint64_t size = get32(head + 4);
        const std::uint64_t body = pos + 8;
        if (body + size > riff_end) {
            return R::fail("wav: chunk '" + std::string(reinterpret_cast<const char*>(head), 4) +
                           "' runs past the end");
        }
        const std::uint8_t* p = bytes.data() + body;

        if (std::memcmp(head, "fmt ", 4) == 0) {
            if (size < kFmtBytes) {
                return R::fail("wav: fmt chunk is too short");
            }
            const std::uint32_t format = get16(p);
            wav.channels = static_cast<std::uint16_t>(get16(p + 2));
            wav.sample_rate = get32(p + 4);
            const std::uint32_t byte_rate = get32(p + 8);
            const std::uint32_t block_align = get16(p + 12);
            const std::uint32_t bits = get16(p + 14);
            if (format != 1) {
                return R::fail("wav: only PCM is supported");
            }
            if (bits != 16) {
                return R::fail("wav: only 16-bit samples are supported");
            }
            if (wav.channels < 1 || wav.channels > 2) {
                return R::fail("wav: only mono and stereo are supported");
            }
            if (wav.sample_rate == 0 || block_align != wav.channels * 2u ||
                byte_rate != std::uint64_t{wav.sample_rate} * block_align) {
                return R::fail("wav: inconsistent fmt chunk");
            }
            have_fmt = true;
        } else if (std::memcmp(head, "data", 4) == 0) {
            if (!have_fmt) {
                return R::fail("wav: data chunk before fmt chunk");
            }
            if (size % (wav.channels * 2u) != 0) {
                return R::fail("wav: data is not a whole number of frames");
            }
            wav.samples.resize(static_cast<std::size_t>(size / 2));
            for (std::size_t i = 0; i < wav.samples.size(); ++i) {
                wav.samples[i] = static_cast<std::int16_t>(get16(p + i * 2));
            }
            have_data = true;
        } else if (std::memcmp(head, "smpl", 4) == 0) {
            if (size < kSmplBaseBytes) {
                return R::fail("wav: smpl chunk is too short");
            }
            WavSampler sampler;
            const std::uint32_t note = get32(p + 12);
            if (note > 127) {
                return R::fail("wav: smpl unity note is outside 0..127");
            }
            sampler.unity_note = static_cast<std::uint8_t>(note);
            const std::uint32_t loops = get32(p + 28);
            const std::uint32_t extra = get32(p + 32);
            if (std::uint64_t{kSmplBaseBytes} + std::uint64_t{loops} * kSmplLoopBytes + extra > size) {
                return R::fail("wav: smpl chunk is shorter than its loop list");
            }
            if (loops > 0) {
                sampler.loop = WavLoop{get32(p + kSmplBaseBytes + 8), get32(p + kSmplBaseBytes + 12)};
            }
            wav.sampler = sampler;
        }
        pos = body + size + (size & 1);
    }
    if (!have_fmt || !have_data) {
        return R::fail("wav: fmt or data chunk is missing");
    }
    if (wav.sampler && wav.sampler->loop && !loop_is_valid(wav)) {
        return R::fail("wav: loop points are outside the sample");
    }
    return R::ok(std::move(wav));
}

}  // namespace sh::convert::audio
