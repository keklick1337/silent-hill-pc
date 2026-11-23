#include "formats/convert/audio/audio_converter.h"

#include "formats/convert/audio/json_writer.h"
#include "formats/convert/audio/spu_adpcm.h"
#include "formats/convert/audio/wav.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

namespace sh::convert::audio {
namespace {

// VAB layout, as the game's sound driver reads it (SdVabOpenHead and the note-on path):
// 32-byte header, 128 program records of 16 bytes, one table of 16 tone records of 32 bytes for each of the `ps`
// programs, a table of 256 sample sizes in units of 8 bytes, then the samples. Table k belongs to the k-th program whose
// tone count is not zero; sample numbers are 1-based and entry 0 of the size table is unused.
constexpr std::size_t kHeaderBytes = 32;
constexpr std::size_t kProgramCount = 128;
constexpr std::size_t kProgramBytes = 16;
constexpr std::size_t kToneSlots = 16;
constexpr std::size_t kToneBytes = 32;
constexpr std::size_t kToneTableBytes = kToneSlots * kToneBytes;
constexpr std::size_t kSizeTableEntries = 256;
constexpr std::size_t kSizeTableBytes = kSizeTableEntries * 2;
constexpr std::size_t kFixedBytes = kHeaderBytes + kProgramCount * kProgramBytes + kSizeTableBytes;  // 0xA20 less the tone tables
constexpr std::uint32_t kWavRate = 44100;  // pitch 0x1000 of an SPU voice: the rate the root key refers to

std::uint32_t le16(const std::uint8_t* p) {
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8);
}

std::uint32_t le32(const std::uint8_t* p) {
    return le16(p) | (le16(p + 2) << 16);
}

struct Program {
    std::uint8_t tones;
    std::uint8_t volume;
    std::uint8_t priority;
    std::uint8_t mode;
    std::uint8_t pan;
    std::int16_t attr;
    int table = -1;  // index of its tone table, -1 when the header has none for it
};

struct Tone {
    std::uint8_t priority, mode, volume, pan, center, shift, key_min, key_max;
    std::uint8_t vib_width, vib_time, port_width, port_time, bend_min, bend_max;
    std::uint16_t adsr1, adsr2;
    std::int16_t program;
    std::int16_t sample;
};

Tone parse_tone(const std::uint8_t* p) {
    Tone t;
    t.priority = p[0];
    t.mode = p[1];
    t.volume = p[2];
    t.pan = p[3];
    t.center = p[4];
    t.shift = p[5];
    t.key_min = p[6];
    t.key_max = p[7];
    t.vib_width = p[8];
    t.vib_time = p[9];
    t.port_width = p[10];
    t.port_time = p[11];
    t.bend_min = p[12];
    t.bend_max = p[13];
    t.adsr1 = static_cast<std::uint16_t>(le16(p + 16));
    t.adsr2 = static_cast<std::uint16_t>(le16(p + 18));
    t.program = static_cast<std::int16_t>(le16(p + 20));
    t.sample = static_cast<std::int16_t>(le16(p + 22));
    return t;
}

// SPU envelope registers (psx-spx, "SPU ADSR Generator").
void write_adsr(JsonWriter& json, std::uint16_t adsr1, std::uint16_t adsr2) {
    json.key("adsr");
    json.begin_object(true);
    json.field("attack_mode", (adsr1 & 0x8000) ? "exponential" : "linear");
    json.field("attack_shift", (adsr1 >> 10) & 0x1F);
    json.field("attack_step", (adsr1 >> 8) & 3);
    json.field("decay_shift", (adsr1 >> 4) & 0xF);
    json.field("sustain_level", adsr1 & 0xF);
    json.field("sustain_mode", (adsr2 & 0x8000) ? "exponential" : "linear");
    json.field("sustain_direction", (adsr2 & 0x4000) ? "decrease" : "increase");
    json.field("sustain_shift", (adsr2 >> 8) & 0x1F);
    json.field("sustain_step", (adsr2 >> 6) & 3);
    json.field("release_mode", (adsr2 & 0x20) ? "exponential" : "linear");
    json.field("release_shift", adsr2 & 0x1F);
    json.end_object();
}

std::string bank_name(std::string_view game_path) {
    const std::size_t slash = game_path.find_last_of('/');
    std::string_view name = slash == std::string_view::npos ? game_path : game_path.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    if (dot != std::string_view::npos && dot > 0) {
        name = name.substr(0, dot);
    }
    return name.empty() ? std::string("vab") : std::string(name);
}

bool looks_like_vab(std::span<const std::uint8_t> raw) {
    if (raw.size() < kFixedBytes || raw[0] != 'p' || raw[1] != 'B' || raw[2] != 'A' || raw[3] != 'V') {
        return false;
    }
    const std::uint32_t programs = le16(raw.data() + 0x12);
    return programs <= kProgramCount && raw.size() >= kFixedBytes + programs * kToneTableBytes;
}

class VabConverter final : public Converter {
public:
    std::string_view name() const override { return "vab"; }
    bool lossless() const override { return false; }

    bool accepts(std::span<const std::uint8_t> raw, std::string_view) const override { return looks_like_vab(raw); }

    Result<std::vector<Output>> decode(std::span<const std::uint8_t> raw, std::string_view game_path) const override;

    Result<std::vector<std::uint8_t>> encode(std::span<const Output>) const override {
        return Result<std::vector<std::uint8_t>>::fail("vab: the WAV form is a representation, the raw bank is not rebuilt from it");
    }
};

Result<std::vector<Output>> VabConverter::decode(std::span<const std::uint8_t> raw, std::string_view game_path) const {
    using R = Result<std::vector<Output>>;
    if (!looks_like_vab(raw)) {
        return R::fail("vab: not a VAB bank");
    }
    const std::uint8_t* d = raw.data();
    const std::uint32_t version = le32(d + 4);
    const std::uint32_t id = le32(d + 8);
    const std::uint32_t fsize = le32(d + 12);
    const std::size_t program_tables = le16(d + 0x12);
    const std::uint32_t tone_total = le16(d + 0x14);
    const std::size_t sample_count = le16(d + 0x16);
    const std::size_t tables_offset = kHeaderBytes + kProgramCount * kProgramBytes;
    const std::size_t sizes_offset = tables_offset + program_tables * kToneTableBytes;
    const std::size_t body_offset = sizes_offset + kSizeTableBytes;
    if (sample_count >= kSizeTableEntries) {
        return R::fail("vab: sample count is larger than the size table");
    }

    // Entries past `sample_count` hold leftovers of the tool that wrote the bank and are ignored.
    std::array<std::size_t, kSizeTableEntries> offsets{};
    std::array<std::size_t, kSizeTableEntries> sizes{};
    std::size_t body_used = 0;
    for (std::size_t n = 1; n <= sample_count; ++n) {
        sizes[n] = std::size_t{le16(d + sizes_offset + n * 2)} * 8;
        offsets[n] = body_offset + body_used;
        body_used += sizes[n];
    }
    if (body_offset + body_used > raw.size()) {
        return R::fail("vab: sample sizes run past the end of the bank");
    }

    std::array<Program, kProgramCount> programs{};
    int next_table = 0;
    for (std::size_t p = 0; p < kProgramCount; ++p) {
        const std::uint8_t* rec = d + kHeaderBytes + p * kProgramBytes;
        programs[p] = Program{rec[0], rec[1], rec[2], rec[3], rec[4], static_cast<std::int16_t>(le16(rec + 6)), -1};
        if (rec[0] != 0 && static_cast<std::size_t>(next_table) < program_tables) {
            programs[p].table = next_table++;
        }
    }

    std::size_t programs_without_table = 0;
    std::size_t tone_count = 0;
    std::size_t tones_over_16 = 0;
    std::size_t tones_without_sample = 0;
    std::size_t tones_bad_sample = 0;
    std::array<int, kSizeTableEntries> first_tone_program{};
    std::array<int, kSizeTableEntries> first_tone_slot{};
    first_tone_program.fill(-1);
    for (std::size_t p = 0; p < kProgramCount; ++p) {
        const Program& prog = programs[p];
        if (prog.tones == 0) {
            continue;
        }
        if (prog.table < 0) {
            ++programs_without_table;
            continue;
        }
        const std::size_t listed = std::min<std::size_t>(prog.tones, kToneSlots);
        tones_over_16 += prog.tones > kToneSlots;
        for (std::size_t t = 0; t < listed; ++t) {
            const Tone tone = parse_tone(d + tables_offset + static_cast<std::size_t>(prog.table) * kToneTableBytes + t * kToneBytes);
            ++tone_count;
            if (tone.sample == 0) {
                ++tones_without_sample;
            } else if (tone.sample < 0 || static_cast<std::size_t>(tone.sample) > sample_count) {
                ++tones_bad_sample;
            } else if (first_tone_program[static_cast<std::size_t>(tone.sample)] < 0) {
                first_tone_program[static_cast<std::size_t>(tone.sample)] = static_cast<int>(p);
                first_tone_slot[static_cast<std::size_t>(tone.sample)] = static_cast<int>(t);
            }
        }
    }

    const std::string bank = bank_name(game_path);
    std::vector<Output> outputs;
    JsonWriter json;
    json.begin_object();
    json.field("format", "silent-hill-vab");
    json.field("version", 2);
    json.field("source", std::string(game_path));
    json.field("bank", bank);
    json.field("wav_sample_rate", kWavRate);
    json.field("note",
               "The sound engine plays the bank from this file and the WAVs. "
               "root_key and fine_tune are the VAB center and shift fields. Pitch 0x1000 plays a WAV frame per frame "
               "at 44.1 kHz; a WAV at another rate is stepped proportionally. The smpl loop of a WAV is the loop the "
               "engine plays; the loop here is the original one, for reference.");

    json.key("header");
    json.begin_object(true);
    json.field("version", version);
    json.field("id", id);
    json.field("file_size", fsize);
    json.field("tone_tables", program_tables);
    json.field("tones", tone_total);
    json.field("samples", sample_count);
    json.field("master_volume", d[0x18]);
    json.field("master_pan", d[0x19]);
    json.field("attr1", d[0x1A]);
    json.field("attr2", d[0x1B]);
    json.end_object();

    json.key("programs");
    json.begin_array();
    for (std::size_t p = 0; p < kProgramCount; ++p) {
        const Program& prog = programs[p];
        if (prog.tones == 0) {
            continue;
        }
        json.begin_object();
        json.field("index", p);
        json.field("volume", prog.volume);
        json.field("priority", prog.priority);
        json.field("mode", prog.mode);
        json.field("pan", prog.pan);
        json.field("attr", static_cast<int>(prog.attr));
        json.field("tone_count", prog.tones);
        json.key("tone_table");
        if (prog.table < 0) {
            json.null();
        } else {
            json.value(prog.table);
        }
        json.key("tones");
        json.begin_array();
        if (prog.table >= 0) {
            // Every slot of the table, not only the first tone_count: the sound driver keys a tone by its slot, and a
            // game table may name a slot past the count.
            for (std::size_t t = 0; t < kToneSlots; ++t) {
                const Tone tone = parse_tone(d + tables_offset + static_cast<std::size_t>(prog.table) * kToneTableBytes + t * kToneBytes);
                json.begin_object(true);
                json.field("slot", t);
                json.field("sample", static_cast<int>(tone.sample));
                json.field("volume", tone.volume);
                json.field("pan", tone.pan);
                json.field("root_key", tone.center);
                json.field("fine_tune", tone.shift);
                json.field("key_min", tone.key_min);
                json.field("key_max", tone.key_max);
                json.field("priority", tone.priority);
                json.field("mode", tone.mode);
                json.field("reverb", (tone.mode & 4) != 0);
                json.field("vibrato_width", tone.vib_width);
                json.field("vibrato_time", tone.vib_time);
                json.field("portamento_width", tone.port_width);
                json.field("portamento_time", tone.port_time);
                json.field("pitch_bend_min", tone.bend_min);
                json.field("pitch_bend_max", tone.bend_max);
                json.key("adsr1");
                json.value_hex16(tone.adsr1);
                json.key("adsr2");
                json.value_hex16(tone.adsr2);
                write_adsr(json, tone.adsr1, tone.adsr2);
                json.field("program", static_cast<int>(tone.program));
                json.end_object();
            }
        }
        json.end_array();
        json.end_object();
    }
    json.end_array();

    json.key("samples");
    json.begin_array();
    std::size_t blocks_total = 0;
    std::size_t trailing_blocks = 0;
    std::size_t wav_count = 0;
    for (std::size_t n = 1; n <= sample_count; ++n) {
        json.begin_object(true);
        json.field("number", n);
        json.field("bytes", sizes[n]);
        if (sizes[n] == 0) {
            json.key("file");
            json.null();
            json.end_object();
            continue;
        }
        auto decoded = decode_spu_sample(raw.subspan(offsets[n], sizes[n]));
        if (!decoded) {
            return R::fail("vab: sample " + std::to_string(n) + ": " + decoded.error());
        }
        const SpuSample& sample = decoded.value();

        Wav wav;
        wav.channels = 1;
        wav.sample_rate = kWavRate;
        wav.samples = sample.pcm;
        wav.sampler = WavSampler{};
        if (first_tone_program[n] >= 0) {
            const Program& prog = programs[static_cast<std::size_t>(first_tone_program[n])];
            const Tone tone = parse_tone(d + tables_offset + static_cast<std::size_t>(prog.table) * kToneTableBytes +
                                         static_cast<std::size_t>(first_tone_slot[n]) * kToneBytes);
            wav.sampler->unity_note = tone.center <= 127 ? tone.center : 60;
        }
        wav.sampler->loop = sample.loop;
        auto bytes = write_wav(wav);
        if (!bytes) {
            return R::fail("vab: sample " + std::to_string(n) + ": " + bytes.error());
        }

        char number[16];
        std::snprintf(number, sizeof(number), "%03zu", n);
        const std::string wav_name = std::string("audio/vab/") + bank + "/" + number + ".wav";
        outputs.push_back(Output{wav_name, std::move(bytes.value())});
        ++wav_count;
        blocks_total += sample.blocks_played;
        trailing_blocks += sample.blocks_in - sample.blocks_played;

        json.field("file", bank + "/" + number + ".wav");
        json.field("frames", sample.pcm.size());
        json.key("duration_s");
        json.value_fixed(static_cast<double>(sample.pcm.size()) / kWavRate, 6);
        json.field("blocks", sample.blocks_played);
        json.field("blocks_after_end", sample.blocks_in - sample.blocks_played);
        if (sample.loop) {
            json.key("loop");
            json.begin_object(true);
            json.field("start", sample.loop->start);
            json.field("end", sample.loop->end);
            json.end_object();
        } else {
            json.key("loop");
            json.null();
        }
        json.field("end_flag", sample.has_end);
        json.end_object();
    }
    json.end_array();

    json.key("checks");
    json.begin_object(true);
    json.field("programs_without_tone_table", programs_without_table);
    json.field("tones_listed", tone_count);
    json.field("tones_over_16", tones_over_16);
    json.field("tones_without_sample", tones_without_sample);
    json.field("tones_with_bad_sample", tones_bad_sample);
    json.field("body_bytes", body_used);
    json.field("layout_matches_file_size", body_offset + body_used == fsize);
    json.field("blocks_decoded", blocks_total);
    json.field("blocks_after_end", trailing_blocks);
    json.end_object();
    json.end_object();

    std::string text = json.take();
    outputs.push_back(Output{"audio/vab/" + bank + ".vab.json", std::vector<std::uint8_t>(text.begin(), text.end())});
    (void)wav_count;
    return R::ok(std::move(outputs));
}

}  // namespace

std::unique_ptr<Converter> make_vab_converter() {
    return std::make_unique<VabConverter>();
}

}  // namespace sh::convert::audio
