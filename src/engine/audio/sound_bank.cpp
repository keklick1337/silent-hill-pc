#include "engine/audio/sound_bank.h"

#include "formats/common/json.h"
#include "formats/convert/audio/spu_adpcm.h"
#include "formats/convert/audio/wav.h"

#include <algorithm>

namespace sh::audio {
namespace {

std::uint16_t le16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }

constexpr std::size_t kHeader = 32;
constexpr std::size_t kToneTables = kHeader + 128 * 16;  // 0x820

std::int64_t int_of(const json::Value& object, std::string_view key, std::int64_t fallback = 0) {
    const json::Value* v = object.find(key);
    return v && v->is_integer() ? v->integer() : fallback;
}

std::uint8_t byte_of(const json::Value& object, std::string_view key, std::uint8_t fallback = 0) {
    return static_cast<std::uint8_t>(int_of(object, key, fallback) & 0xFF);
}

// "0x80FF" or a plain number.
std::uint16_t hex16_of(const json::Value& object, std::string_view key) {
    const json::Value* v = object.find(key);
    if (!v) {
        return 0;
    }
    if (v->is_integer()) {
        return static_cast<std::uint16_t>(v->integer());
    }
    if (v->is_string()) {
        return static_cast<std::uint16_t>(std::stoul(v->string(), nullptr, 0));
    }
    return 0;
}

}  // namespace

std::shared_ptr<const Sample> SoundBank::sample(int number) const {
    if (number < 1 || static_cast<std::size_t>(number) > samples.size()) {
        return nullptr;
    }
    return samples[static_cast<std::size_t>(number) - 1];
}

std::size_t SoundBank::table_of(int program) const {
    std::size_t table = 0;
    for (int i = 0; i < program && i < 128; ++i) {
        table += programs[static_cast<std::size_t>(i)].tones != 0;
    }
    return table;
}

Result<SoundBank> load_sound_bank(std::string_view json_text, const BankFileReader& read_file) {
    using R = Result<SoundBank>;
    auto parsed = json::parse(json_text);
    if (!parsed) {
        return R::fail("bank: " + parsed.error());
    }
    const json::Value& root = parsed.value();
    const json::Value* header = root.find("header");
    const json::Value* programs = root.find("programs");
    const json::Value* samples = root.find("samples");
    if (!header || !programs || !programs->is_array() || !samples || !samples->is_array()) {
        return R::fail("bank: header, programs or samples missing");
    }
    SoundBank bank;
    if (const json::Value* name = root.find("bank"); name && name->is_string()) {
        bank.name = name->string();
    }
    bank.master_volume = byte_of(*header, "master_volume", 127);
    bank.master_pan = byte_of(*header, "master_pan", 64);
    bank.tone_tables = static_cast<std::size_t>(std::clamp<std::int64_t>(int_of(*header, "tone_tables"), 0, 128));
    bank.tones.assign(bank.tone_tables * 16, BankTone{});

    for (const json::Value& p : programs->array()) {
        const auto index = int_of(p, "index", -1);
        if (index < 0 || index >= 128) {
            return R::fail("bank: program index out of range");
        }
        BankProgram& prog = bank.programs[static_cast<std::size_t>(index)];
        prog.tones = byte_of(p, "tone_count");
        prog.volume = byte_of(p, "volume");
        prog.pan = byte_of(p, "pan", 64);
        prog.priority = byte_of(p, "priority");
        prog.mode = byte_of(p, "mode");
        const json::Value* table = p.find("tone_table");
        const json::Value* tones = p.find("tones");
        if (!table || !table->is_integer() || !tones || !tones->is_array()) {
            continue;
        }
        const auto t = static_cast<std::size_t>(table->integer());
        for (const json::Value& tv : tones->array()) {
            const auto slot = static_cast<std::size_t>(int_of(tv, "slot", 16));
            BankTone* tone = slot < 16 ? bank.tone(t * 16 + slot) : nullptr;
            if (!tone) {
                continue;
            }
            tone->priority = byte_of(tv, "priority");
            tone->mode = byte_of(tv, "mode");
            tone->volume = byte_of(tv, "volume");
            tone->pan = byte_of(tv, "pan");
            tone->root_key = byte_of(tv, "root_key");
            tone->fine_tune = byte_of(tv, "fine_tune");
            tone->key_min = byte_of(tv, "key_min");
            tone->key_max = byte_of(tv, "key_max");
            tone->vibrato_width = byte_of(tv, "vibrato_width");
            tone->vibrato_time = byte_of(tv, "vibrato_time");
            tone->portamento_width = byte_of(tv, "portamento_width");
            tone->portamento_time = byte_of(tv, "portamento_time");
            tone->bend_min = byte_of(tv, "pitch_bend_min");
            tone->bend_max = byte_of(tv, "pitch_bend_max");
            tone->adsr1 = hex16_of(tv, "adsr1");
            tone->adsr2 = hex16_of(tv, "adsr2");
            tone->program = static_cast<std::int16_t>(int_of(tv, "program"));
            tone->sample = static_cast<std::int16_t>(int_of(tv, "sample"));
            tone->saved_adsr1 = tone->adsr1;
            tone->saved_adsr2 = tone->adsr2;
        }
    }

    for (const json::Value& s : samples->array()) {
        const auto number = int_of(s, "number", 0);
        if (number < 1 || number > 255) {
            return R::fail("bank: sample number out of range");
        }
        if (bank.samples.size() < static_cast<std::size_t>(number)) {
            bank.samples.resize(static_cast<std::size_t>(number));
        }
        const json::Value* file = s.find("file");
        if (!file || !file->is_string()) {
            continue;
        }
        auto bytes = read_file(file->string());
        if (!bytes) {
            return R::fail("bank: " + file->string() + ": " + bytes.error());
        }
        auto wav = convert::audio::read_wav(bytes.value());
        if (!wav) {
            return R::fail("bank: " + file->string() + ": " + wav.error());
        }
        auto sample = std::make_shared<Sample>();
        sample->rate = wav.value().sample_rate ? wav.value().sample_rate : 44100;
        const std::uint16_t channels = wav.value().channels ? wav.value().channels : 1;
        // A stereo replacement is folded to mono: a voice of the driver is mono.
        sample->pcm.resize(wav.value().frames());
        for (std::size_t i = 0; i < sample->pcm.size(); ++i) {
            int sum = 0;
            for (std::uint16_t c = 0; c < channels; ++c) {
                sum += wav.value().samples[i * channels + c];
            }
            sample->pcm[i] = static_cast<std::int16_t>(sum / channels);
        }
        if (wav.value().sampler && wav.value().sampler->loop && wav.value().sampler->loop->start < sample->pcm.size()) {
            sample->loop_start = wav.value().sampler->loop->start;
        }
        bank.samples[static_cast<std::size_t>(number) - 1] = std::move(sample);
    }
    return R::ok(std::move(bank));
}

Result<SoundBank> parse_vab(std::span<const std::uint8_t> raw) {
    using R = Result<SoundBank>;
    if (raw.size() < kToneTables + 512 || raw[0] != 'p' || raw[1] != 'B' || raw[2] != 'A' || raw[3] != 'V') {
        return R::fail("vab: not a VAB bank");
    }
    SoundBank b;
    const std::uint8_t* d = raw.data();
    b.tone_tables = le16(d + 0x12);
    const std::size_t sample_count = le16(d + 0x16);
    b.master_volume = d[0x18];
    b.master_pan = d[0x19];
    if (b.tone_tables > 128) {
        return R::fail("vab: more than 128 tone tables");
    }
    const std::size_t sizes_at = kToneTables + b.tone_tables * 512;
    const std::size_t body_at = sizes_at + 512;
    if (raw.size() < body_at) {
        return R::fail("vab: header runs past the end of the file");
    }
    for (std::size_t p = 0; p < 128; ++p) {
        const std::uint8_t* rec = d + kHeader + p * 16;
        b.programs[p] = BankProgram{rec[0], rec[1], rec[4], rec[2], rec[3]};
    }
    b.tones.resize(b.tone_tables * 16);
    for (std::size_t t = 0; t < b.tones.size(); ++t) {
        const std::uint8_t* p = d + kToneTables + t * 32;
        BankTone& tone = b.tones[t];
        tone.priority = p[0];
        tone.mode = p[1];
        tone.volume = p[2];
        tone.pan = p[3];
        tone.root_key = p[4];
        tone.fine_tune = p[5];
        tone.key_min = p[6];
        tone.key_max = p[7];
        tone.vibrato_width = p[8];
        tone.vibrato_time = p[9];
        tone.portamento_width = p[10];
        tone.portamento_time = p[11];
        tone.bend_min = p[12];
        tone.bend_max = p[13];
        tone.adsr1 = le16(p + 16);
        tone.adsr2 = le16(p + 18);
        tone.program = static_cast<std::int16_t>(le16(p + 20));
        tone.sample = static_cast<std::int16_t>(le16(p + 22));
        tone.saved_adsr1 = tone.adsr1;
        tone.saved_adsr2 = tone.adsr2;
    }
    // Sample n starts after the sizes of entries 1..n-1, as the importer lays the bank out.
    std::size_t at = body_at;
    b.samples.resize(std::min<std::size_t>(sample_count, 255));
    for (std::size_t n = 1; n <= b.samples.size(); ++n) {
        const std::size_t bytes = std::size_t{le16(d + sizes_at + n * 2)} * 8;
        if (bytes == 0 || at + bytes > raw.size()) {
            at += bytes;
            continue;
        }
        auto decoded = convert::audio::decode_spu_sample(raw.subspan(at, bytes));
        at += bytes;
        if (!decoded) {
            continue;
        }
        auto sample = std::make_shared<Sample>();
        sample->pcm = std::move(decoded.value().pcm);
        if (decoded.value().loop) {
            sample->loop_start = decoded.value().loop->start;
        }
        b.samples[n - 1] = std::move(sample);
    }
    return R::ok(std::move(b));
}

}  // namespace sh::audio
