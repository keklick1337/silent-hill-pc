#include "framework/sh_test.h"
#include "convert_audio/test_support.h"
#include "formats/convert/audio/audio_converter.h"
#include "formats/convert/audio/wav.h"
#include "formats/convert/audio/xa_adpcm.h"

#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace {

using namespace sh::convert::audio;
using sh::convert::Output;
using sh::test_audio::JsonChecker;

// One ADPCM sector whose samples are all `value` (shift 12, filter 0, both nibbles set for stereo).
std::array<std::uint8_t, kXaSectorBytes> sector(std::uint8_t file, std::uint8_t channel, std::uint8_t submode,
                                                std::uint8_t coding, std::uint8_t nibble) {
    std::array<std::uint8_t, kXaSectorBytes> s{};
    s[0] = s[4] = file;
    s[1] = s[5] = channel;
    s[2] = s[6] = submode;
    s[3] = s[7] = coding;
    for (std::size_t g = 0; g < kXaGroups; ++g) {
        std::uint8_t* group = s.data() + kXaSubheaderBytes + g * kXaGroupBytes;
        for (int p = 0; p < 16; ++p) {
            group[p] = 0x0C;
        }
        for (int i = 16; i < 128; ++i) {
            group[i] = static_cast<std::uint8_t>((nibble << 4) | nibble);
        }
    }
    return s;
}

void append(std::vector<std::uint8_t>& track, const std::array<std::uint8_t, kXaSectorBytes>& s) {
    track.insert(track.end(), s.begin(), s.end());
}

const Output* find(const std::vector<Output>& outputs, const std::string& path) {
    for (const Output& o : outputs) {
        if (o.relative_path == path) {
            return &o;
        }
    }
    return nullptr;
}

}  // namespace

SH_TEST(AudioXaConverterContract) {
    const auto conv = make_xa_converter();
    SH_CHECK(conv->name() == "xa");
    SH_CHECK(!conv->lossless());
    SH_CHECK(!conv->encode({}));

    std::vector<std::uint8_t> track;
    append(track, sector(1, 0, 0x64, 0x01, 1));
    SH_CHECK(conv->accepts(track, "XA/05_02152"));
    SH_CHECK(conv->accepts(track, "xa/05_02152"));
    SH_CHECK(!conv->accepts(track, "SND/05_02152"));  // decided by the game path as well as by the bytes
    SH_CHECK(!conv->accepts({}, "XA/05_02152"));
    SH_CHECK(!conv->accepts(std::span<const std::uint8_t>(track.data(), 2335), "XA/05_02152"));

    // A track of video and filler sectors only has nothing to decode.
    std::vector<std::uint8_t> silent;
    append(silent, sector(0, 1, 0x48, 0x00, 0));
    append(silent, sector(255, 1, 0x04, 0x00, 0));
    SH_CHECK(!conv->accepts(silent, "XA/M1_03500"));
    SH_CHECK(!conv->decode(silent, "XA/M1_03500"));
}

SH_TEST(AudioXaSplitsChannelsAndSkipsOtherSectors) {
    const auto conv = make_xa_converter();
    std::vector<std::uint8_t> track;
    // file 1 channel 0 (2 sectors) and channel 3 (1 sector, EOF), interleaved with video, filler and empty sectors.
    append(track, sector(1, 0, 0x64, 0x01, 1));
    append(track, sector(1, 3, 0x64, 0x01, 2));
    append(track, sector(0, 1, 0x48, 0x00, 0));    // data
    append(track, sector(255, 1, 0x04, 0x00, 0));  // audio without Form 2: filler
    append(track, sector(1, 0, 0xE4, 0x01, 1));    // last sector of channel 0, EOF
    append(track, sector(0, 0, 0x00, 0x00, 0));    // empty
    append(track, sector(2, 0, 0x64, 0x05, 3));    // file 2, 18.9 kHz stereo
    append(track, sector(2, 0, 0x64, 0x00, 3));    // the coding changes: a second segment of the same channel

    const auto result = conv->decode(track, "XA/05_02152");
    SH_CHECK(static_cast<bool>(result));
    if (!result) {
        return;
    }
    const auto& out = result.value();
    SH_CHECK(out.size() == 5);  // 4 streams + the map
    const Output* c0 = find(out, "audio/xa/05_02152_f01_ch0.wav");
    const Output* c3 = find(out, "audio/xa/05_02152_f01_ch3.wav");
    const Output* f2 = find(out, "audio/xa/05_02152_f02_ch0.wav");
    const Output* f2b = find(out, "audio/xa/05_02152_f02_ch0_s1.wav");
    const Output* json = find(out, "audio/xa/05_02152.xa.json");
    SH_CHECK(c0 && c3 && f2 && f2b && json);
    if (!c0 || !c3 || !f2 || !f2b || !json) {
        return;
    }
    const auto w0 = read_wav(c0->bytes);
    const auto w3 = read_wav(c3->bytes);
    const auto w2 = read_wav(f2->bytes);
    const auto w2b = read_wav(f2b->bytes);
    SH_CHECK(w0 && w3 && w2 && w2b);
    if (!w0 || !w3 || !w2 || !w2b) {
        return;
    }
    SH_CHECK(w0.value().channels == 2 && w0.value().sample_rate == 37800 && w0.value().frames() == 2 * 2016);
    SH_CHECK(w3.value().frames() == 2016);
    SH_CHECK(w2.value().sample_rate == 18900 && w2.value().channels == 2 && w2.value().frames() == 2016);
    SH_CHECK(w2b.value().channels == 1 && w2b.value().sample_rate == 37800 && w2b.value().frames() == 4032);
    SH_CHECK(w0.value().samples.front() == 1 && w3.value().samples.front() == 2);

    const std::string text(json->bytes.begin(), json->bytes.end());
    SH_CHECK(JsonChecker::valid(text));
    SH_CHECK(text.find("\"first_sector\": 0, \"last_sector\": 4, \"eof\": true") != std::string::npos);
    SH_CHECK(text.find("\"data\": 1, \"audio_without_form2\": 1, \"empty\": 1") != std::string::npos);
}

SH_TEST(AudioXaSingleStreamTrackHasPlainName) {
    const auto conv = make_xa_converter();
    std::vector<std::uint8_t> track;
    append(track, sector(0, 1, 0x48, 0x00, 0));
    append(track, sector(1, 1, 0x64, 0x01, 4));
    append(track, sector(0, 1, 0x48, 0x00, 0));
    append(track, sector(1, 1, 0xE4, 0x01, 4));
    const auto result = conv->decode(track, "XA/M1_03500");
    SH_CHECK(static_cast<bool>(result));
    if (!result) {
        return;
    }
    const Output* wav = find(result.value(), "audio/xa/M1_03500.wav");
    SH_CHECK(wav != nullptr);
    if (wav) {
        const auto w = read_wav(wav->bytes);
        SH_CHECK(w && w.value().frames() == 2 * 2016);
    }
}

// All 30 tracks of the image: frames = ADPCM sectors on the channel * samples per sector. The expected totals were
// obtained with an independent decoder and sector scan (python3 -I, own code).
SH_TEST(AudioXaConvertsAllTracksOfTheImage) {
    const auto game = sh::test_audio::open_game();
    if (!game) {
        SH_SKIP("data/Silent Hill (v1.1).bin is not present");
    }
    const auto conv = make_xa_converter();
    std::size_t tracks = 0, streams = 0, sectors = 0, frames = 0;
    for (const auto& entry : game->table) {
        const auto raw = game->read(entry);
        SH_CHECK(raw.has_value());
        if (!raw) {
            continue;
        }
        const bool xa = entry.region == sh::archive::Region::Hill;
        SH_CHECK(conv->accepts(*raw, entry.name) == xa);
        if (!xa) {
            continue;
        }

        // Expected sector count for each (file, channel), straight from the subheaders.
        std::map<std::pair<int, int>, std::size_t> expected;
        for (std::size_t s = 0; s + kXaSectorBytes <= raw->size(); s += kXaSectorBytes) {
            if (((*raw)[s + 2] & 0x24) == 0x24) {
                ++expected[{(*raw)[s], (*raw)[s + 1]}];
            }
        }

        const auto result = conv->decode(*raw, entry.name);
        SH_CHECK(static_cast<bool>(result));
        if (!result) {
            std::printf("    %s: %s\n", entry.name.c_str(), result.error().c_str());
            continue;
        }
        ++tracks;
        std::size_t wavs = 0, track_sectors = 0;
        for (const Output& o : result.value()) {
            if (o.relative_path.size() > 8 && o.relative_path.substr(o.relative_path.size() - 8) == ".xa.json") {
                SH_CHECK(JsonChecker::valid(std::string(o.bytes.begin(), o.bytes.end())));
                continue;
            }
            const auto wav = read_wav(o.bytes);
            SH_CHECK(static_cast<bool>(wav));
            if (!wav) {
                continue;
            }
            SH_CHECK(wav.value().channels == 2 && wav.value().sample_rate == 37800);
            SH_CHECK(wav.value().frames() % 2016 == 0);
            track_sectors += wav.value().frames() / 2016;
            frames += wav.value().frames();
            ++wavs;
        }
        SH_CHECK(wavs == expected.size());
        std::size_t want = 0;
        for (const auto& [key, count] : expected) {
            want += count;
        }
        SH_CHECK(track_sectors == want);
        streams += wavs;
        sectors += track_sectors;
    }
    std::printf("    XA: tracks=%zu streams=%zu sectors=%zu frames=%zu (%.1f s at 37800 Hz)\n", tracks, streams, sectors,
                frames, static_cast<double>(frames) / 37800.0);
    SH_CHECK(tracks == 30);
    SH_CHECK(streams == 747);
    SH_CHECK(sectors == 71831);  // every Form 2 audio sector of HILL.
    SH_CHECK(frames == 71831u * 2016u);
}
