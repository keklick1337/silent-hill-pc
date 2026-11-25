#include "framework/sh_test.h"
#include "formats/convert/audio/ogg_opus.h"
#include "formats/convert/audio/resample.h"
#include "formats/convert/audio/wav.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

namespace au = sh::convert::audio;

// A tone of `hz` on the left and 1.5 * `hz` on the right (or only the first, mono).
au::Wav tone(std::uint16_t channels, std::uint32_t rate, std::size_t frames, double hz) {
    au::Wav wav;
    wav.channels = channels;
    wav.sample_rate = rate;
    for (std::size_t i = 0; i < frames; ++i) {
        for (std::uint16_t c = 0; c < channels; ++c) {
            const double f = hz * (c == 0 ? 1.0 : 1.5);
            wav.samples.push_back(static_cast<std::int16_t>(12000.0 * std::sin(2.0 * 3.14159265358979 * f * i / rate)));
        }
    }
    return wav;
}

// Signal-to-noise ratio of `got` against `want` in dB, over the frames both have, edges left out.
double snr_db(const std::vector<std::int16_t>& want, const std::vector<std::int16_t>& got) {
    const std::size_t n = std::min(want.size(), got.size());
    double signal = 0.0, noise = 0.0;
    for (std::size_t i = n / 20; i < n - n / 20; ++i) {
        signal += static_cast<double>(want[i]) * want[i];
        const double d = static_cast<double>(want[i]) - got[i];
        noise += d * d;
    }
    return 10.0 * std::log10(signal / std::max(noise, 1.0));
}

}  // namespace

SH_TEST(OggOpusRoundTripStereo) {
    au::Wav wav = tone(2, 44100, 44100, 440.0);
    wav.sampler = au::WavSampler{};
    wav.sampler->loop = au::WavLoop{11025, 44099};
    au::OpusEncoding encoding;
    encoding.comments.push_back("TITLE=test");
    const auto bytes = au::write_ogg_opus(wav, encoding);
    SH_CHECK(static_cast<bool>(bytes));
    if (!bytes) return;
    SH_CHECK(au::is_ogg(bytes.value()));
    // 128 kbit/s for a second, plus the headers and the Ogg framing.
    SH_CHECK(bytes.value().size() > 12000 && bytes.value().size() < 20000);

    const auto back = au::read_ogg_opus(bytes.value());
    SH_CHECK(static_cast<bool>(back));
    if (!back) return;
    SH_CHECK(back.value().channels == 2 && back.value().sample_rate == au::kOpusRate);
    // Pre-skip and end padding are cut: exactly the input's length at 48 kHz.
    SH_CHECK(back.value().frames() == 48000);
    SH_CHECK(back.value().sampler && back.value().sampler->loop);
    if (back.value().sampler && back.value().sampler->loop) {
        SH_CHECK(back.value().sampler->loop->start == 12000 && back.value().sampler->loop->end == 47999);
    }
    const std::vector<std::int16_t> reference = au::resample_pcm(wav.samples, 2, 44100, au::kOpusRate);
    SH_CHECK(snr_db(reference, back.value().samples) > 20.0);

    // Read in pieces of odd sizes, the stream gives the same frames.
    auto reader = au::OggOpusReader::open(bytes.value());
    SH_CHECK(static_cast<bool>(reader));
    if (!reader) return;
    SH_CHECK(reader.value()->channels() == 2 && reader.value()->input_rate() == 44100);
    std::vector<std::int16_t> streamed, block(2 * 333);
    for (;;) {
        auto got = reader.value()->read(block);
        SH_CHECK(static_cast<bool>(got));
        if (!got || got.value() == 0) break;
        streamed.insert(streamed.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(got.value() * 2));
    }
    SH_CHECK(streamed == back.value().samples);
}

SH_TEST(OggOpusRoundTripMonoOddLength) {
    // 48 kHz goes in unchanged; a length that is no whole number of 20 ms packets comes out exactly.
    const au::Wav wav = tone(1, au::kOpusRate, 12345, 1000.0);
    const auto bytes = au::write_ogg_opus(wav);
    SH_CHECK(static_cast<bool>(bytes));
    if (!bytes) return;
    const auto back = au::read_sound(bytes.value());
    SH_CHECK(static_cast<bool>(back));
    if (!back) return;
    SH_CHECK(back.value().channels == 1 && back.value().frames() == 12345 && !back.value().sampler);
    SH_CHECK(snr_db(wav.samples, back.value().samples) > 20.0);

    // A WAV goes through read_sound unchanged.
    const auto wav_bytes = au::write_wav(wav);
    const auto wav_back = au::read_sound(wav_bytes.value());
    SH_CHECK(wav_back && wav_back.value().samples == wav.samples && wav_back.value().sample_rate == au::kOpusRate);
}

SH_TEST(OggOpusRejectsBrokenFiles) {
    const auto good = au::write_ogg_opus(tone(2, au::kOpusRate, 9600, 440.0)).value();
    SH_CHECK(!au::read_ogg_opus({}));
    SH_CHECK(!au::read_sound(std::vector<std::uint8_t>{'O', 'g', 'g', 'S', 0, 2}));
    // A flipped byte in the sound fails the page checksum.
    std::vector<std::uint8_t> flipped = good;
    flipped[flipped.size() / 2] ^= 0x40;
    const auto bad = au::read_ogg_opus(flipped);
    SH_CHECK(!bad && bad.error().find("checksum") != std::string::npos);
    // A file cut in the middle of a page.
    const std::vector<std::uint8_t> cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(good.size() - 100));
    const auto short_read = au::read_ogg_opus(cut);
    SH_CHECK(!short_read && short_read.error().find("cut short") != std::string::npos);
    // Only the headers: no sound, but no error either.
    std::vector<std::uint8_t> header_pages;
    std::size_t at = 0;
    for (int page = 0; page < 2; ++page) {
        std::size_t size = 27 + good[at + 26];
        for (std::size_t i = 0; i < good[at + 26]; ++i) size += good[at + 27 + i];
        at += size;
    }
    header_pages.assign(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(at));
    const auto empty = au::read_ogg_opus(header_pages);
    SH_CHECK(empty && empty.value().frames() == 0);
    // An Ogg file of something else than Opus.
    std::vector<std::uint8_t> other = good;
    other[28] = 'X';  // "OpusHead" -> "OXusHead", and the checksum no longer matches either
    SH_CHECK(!au::read_ogg_opus(other));
    // A WAV that is no WAV.
    SH_CHECK(!au::read_sound(std::vector<std::uint8_t>(64, 0x55)));
}

SH_TEST(ResampleKeepsLevelAndLength) {
    const std::vector<std::int16_t> flat(2 * 4410, 5000);
    const auto up = au::resample_pcm(flat, 2, 44100, 48000);
    SH_CHECK(up.size() == 2 * 4800);
    // Away from the edges a constant stays constant.
    SH_CHECK(std::abs(up[2 * 2400] - 5000) <= 1 && std::abs(up[2 * 2400 + 1] - 5000) <= 1);
    SH_CHECK(au::resample_position(44100, 44100, 48000) == 48000);
    SH_CHECK(au::resample_pcm(flat, 2, 48000, 48000) == flat);
}
