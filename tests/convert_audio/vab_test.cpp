#include "framework/sh_test.h"
#include "convert_audio/test_support.h"
#include "formats/convert/audio/audio_converter.h"
#include "formats/convert/audio/spu_adpcm.h"
#include "formats/convert/audio/wav.h"

#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace sh::convert::audio;
using sh::convert::Output;
using sh::test_audio::JsonChecker;

void put16(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v);
    b[at + 1] = static_cast<std::uint8_t>(v >> 8);
}

void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    put16(b, at, v & 0xFFFF);
    put16(b, at + 2, v >> 16);
}

// A sample of `flags.size()` blocks whose nibbles are all 1 (shift 12, filter 0): every output sample is 1.
std::vector<std::uint8_t> sample_blocks(std::initializer_list<int> flags) {
    std::vector<std::uint8_t> bytes;
    for (const int f : flags) {
        std::array<std::uint8_t, 16> block{};
        block[0] = 12;
        block[1] = static_cast<std::uint8_t>(f);
        for (int i = 2; i < 16; ++i) {
            block[i] = 0x11;
        }
        bytes.insert(bytes.end(), block.begin(), block.end());
    }
    return bytes;
}

// Programs 0 (no tones), 1 (two tones) and 3 (one tone, but the header has only one tone table, so it gets none).
std::vector<std::uint8_t> make_vab() {
    const auto s1 = sample_blocks({0, 0, kSpuFlagEnd, 0x07});                // 4 blocks, one-shot
    const auto s2 = sample_blocks({0, kSpuFlagStart, kSpuFlagEnd | kSpuFlagRepeat});  // 3 blocks, loop
    const std::size_t ps = 1;
    const std::size_t header = 0xA20 + ps * 0x200;
    std::vector<std::uint8_t> b(header, 0);
    std::memcpy(b.data(), "pBAV", 4);
    put32(b, 4, 7);
    put16(b, 0x12, ps);
    put16(b, 0x14, 2);
    put16(b, 0x16, 2);
    b[0x18] = 0x7F;
    b[0x19] = 0x40;

    const auto program = [&](int index, int tones) {
        std::uint8_t* p = &b[32 + 16 * index];
        p[0] = static_cast<std::uint8_t>(tones);
        p[1] = 0x7F;
        p[2] = 0xFF;
        p[4] = 0x40;
    };
    program(1, 2);
    program(3, 1);

    const auto tone = [&](int slot, int sample, int center, int shift, int min, int max, int adsr1, int adsr2) {
        std::uint8_t* t = &b[0x820 + 32 * slot];
        t[0] = 0;
        t[1] = 4;  // reverb
        t[2] = 0x70;
        t[3] = 0x20;
        t[4] = static_cast<std::uint8_t>(center);
        t[5] = static_cast<std::uint8_t>(shift);
        t[6] = static_cast<std::uint8_t>(min);
        t[7] = static_cast<std::uint8_t>(max);
        put16(b, 0x820 + 32 * slot + 16, adsr1);
        put16(b, 0x820 + 32 * slot + 18, adsr2);
        put16(b, 0x820 + 32 * slot + 20, 1);
        put16(b, 0x820 + 32 * slot + 22, sample);
    };
    tone(0, 1, 60, 5, 0, 59, 0x80FF, 0x5FC0);
    tone(1, 2, 48, 0, 60, 127, 0x00FF, 0x0000);

    const std::size_t sizes = 0x820 + ps * 0x200;
    put16(b, sizes + 2, s1.size() / 8);
    put16(b, sizes + 4, s2.size() / 8);
    put16(b, sizes + 6, 0x4321);  // leftovers past the sample count must be ignored
    b.insert(b.end(), s1.begin(), s1.end());
    b.insert(b.end(), s2.begin(), s2.end());
    put32(b, 12, static_cast<std::uint32_t>(b.size()));
    return b;
}

const Output* find(const std::vector<Output>& outputs, const std::string& path) {
    for (const Output& o : outputs) {
        if (o.relative_path == path) {
            return &o;
        }
    }
    return nullptr;
}

std::string text_of(const Output& o) {
    return std::string(o.bytes.begin(), o.bytes.end());
}

}  // namespace

SH_TEST(AudioVabConverterContract) {
    const auto conv = make_vab_converter();
    SH_CHECK(conv->name() == "vab");
    SH_CHECK(!conv->lossless());
    const auto vab = make_vab();
    SH_CHECK(conv->accepts(vab, "SND/TEST.VAB"));
    SH_CHECK(!conv->encode({}));

    auto bad = vab;
    bad[0] = 'q';
    SH_CHECK(!conv->accepts(bad, "SND/TEST.VAB"));
    SH_CHECK(!conv->accepts(std::span<const std::uint8_t>(vab.data(), 100), "SND/TEST.VAB"));
    SH_CHECK(!conv->accepts({}, "SND/TEST.VAB"));
    bad = vab;
    put16(bad, 0x12, 129);  // more tone tables than programs
    SH_CHECK(!conv->accepts(bad, "SND/TEST.VAB"));
}

SH_TEST(AudioVabConvertsSyntheticBank) {
    const auto conv = make_vab_converter();
    const auto vab = make_vab();
    const auto result = conv->decode(vab, "SND/TEST.VAB");
    SH_CHECK(static_cast<bool>(result));
    if (!result) {
        return;
    }
    const auto& outputs = result.value();
    SH_CHECK(outputs.size() == 3);
    const Output* one = find(outputs, "audio/vab/TEST/001.wav");
    const Output* loop = find(outputs, "audio/vab/TEST/002.wav");
    const Output* json = find(outputs, "audio/vab/TEST.vab.json");
    SH_CHECK(one && loop && json);
    if (!one || !loop || !json) {
        return;
    }

    const auto wav1 = read_wav(one->bytes);
    const auto wav2 = read_wav(loop->bytes);
    SH_CHECK(wav1 && wav2);
    if (!wav1 || !wav2) {
        return;
    }
    // The dummy block after the end flag is not part of the sound.
    SH_CHECK(wav1.value().frames() == 3 * 28 && wav1.value().channels == 1 && wav1.value().sample_rate == 44100);
    SH_CHECK(wav1.value().sampler && wav1.value().sampler->unity_note == 60 && !wav1.value().sampler->loop);
    SH_CHECK(wav2.value().frames() == 3 * 28);
    SH_CHECK(wav2.value().sampler && wav2.value().sampler->unity_note == 48);
    SH_CHECK(wav2.value().sampler->loop && wav2.value().sampler->loop->start == 28 &&
             wav2.value().sampler->loop->end == 3 * 28 - 1);
    bool ones = true;
    for (const std::int16_t s : wav1.value().samples) {
        ones = ones && s == 1;
    }
    SH_CHECK(ones);

    const std::string text = text_of(*json);
    SH_CHECK(JsonChecker::valid(text));
    SH_CHECK(text.find("\"bank\": \"TEST\"") != std::string::npos);
    SH_CHECK(text.find("\"root_key\": 60") != std::string::npos);
    SH_CHECK(text.find("\"fine_tune\": 5") != std::string::npos);
    SH_CHECK(text.find("\"adsr1\": \"0x80FF\"") != std::string::npos);
    SH_CHECK(text.find("\"attack_mode\": \"exponential\"") != std::string::npos);
    SH_CHECK(text.find("\"reverb\": true") != std::string::npos);
    SH_CHECK(text.find("\"tone_table\": null") != std::string::npos);  // program 3 has no table
    SH_CHECK(text.find("\"programs_without_tone_table\": 1") != std::string::npos);
    SH_CHECK(text.find("\"layout_matches_file_size\": true") != std::string::npos);
    SH_CHECK(text.find("\"loop\": { \"start\": 28, \"end\": 83 }") != std::string::npos);
}

SH_TEST(AudioVabRejectsDamagedBank) {
    const auto conv = make_vab_converter();
    auto vab = make_vab();
    vab.resize(vab.size() - 8);  // the last sample runs past the end
    SH_CHECK(!conv->decode(vab, "SND/TEST.VAB"));
    SH_CHECK(!conv->decode({}, "SND/TEST.VAB"));
    auto many = make_vab();
    put16(many, 0x16, 256);  // sample count larger than the size table
    SH_CHECK(!conv->decode(many, "SND/TEST.VAB"));
}

// All 92 banks of the image. The expected numbers were obtained with an independent decoder (python3 -I, own code).
SH_TEST(AudioVabConvertsAllBanksOfTheImage) {
    const auto game = sh::test_audio::open_game();
    if (!game) {
        SH_SKIP("data/Silent Hill (v1.1).bin is not present");
    }
    const auto conv = make_vab_converter();
    std::size_t banks = 0, samples = 0, loops = 0, frames = 0;
    for (const auto& entry : game->table) {
        const auto raw = game->read(entry);
        SH_CHECK(raw.has_value());
        if (!raw) {
            continue;
        }
        const bool by_name = entry.name.size() > 4 && entry.name.substr(entry.name.size() - 4) == ".VAB";
        SH_CHECK(conv->accepts(*raw, entry.name) == by_name);
        if (!by_name) {
            continue;
        }
        const auto result = conv->decode(*raw, entry.name);
        SH_CHECK(static_cast<bool>(result));
        if (!result) {
            std::printf("    %s: %s\n", entry.name.c_str(), result.error().c_str());
            continue;
        }
        ++banks;
        std::size_t wavs = 0;
        bool json_seen = false;
        for (const Output& o : result.value()) {
            if (o.relative_path.size() > 9 && o.relative_path.substr(o.relative_path.size() - 9) == ".vab.json") {
                json_seen = true;
                SH_CHECK(JsonChecker::valid(text_of(o)));
                continue;
            }
            const auto wav = read_wav(o.bytes);
            SH_CHECK(static_cast<bool>(wav));
            if (!wav) {
                continue;
            }
            SH_CHECK(wav.value().channels == 1 && wav.value().sample_rate == 44100);
            SH_CHECK(wav.value().frames() > 0 && wav.value().frames() % 28 == 0);
            SH_CHECK(wav.value().sampler.has_value());
            loops += wav.value().sampler && wav.value().sampler->loop;
            frames += wav.value().frames();
            ++wavs;
        }
        SH_CHECK(json_seen);
        samples += wavs;
    }
    std::printf("    VAB: banks=%zu samples=%zu frames=%zu loops=%zu\n", banks, samples, frames, loops);
    SH_CHECK(banks == 92);
    SH_CHECK(samples == 1183);
    SH_CHECK(frames == 19119128);
    SH_CHECK(loops == 246);
}
