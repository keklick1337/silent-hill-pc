#pragma once

// An instrument bank of the engine: programs, tones and decoded samples. The game keeps every sound effect and every
// music instrument in such banks; the importer writes them as convert/audio/vab/<NAME>.vab.json plus one WAV per
// sample, and the engine plays them from there.
//
// The layout follows the instrument model the game was written for: up to 128 programs, each with up to 16 tones in
// a tone table, a tone naming a key range, a sample, its root key, envelope, volume and pan. Tables are numbered in
// file order and the tones are kept at `table * 16 + slot`, because the sound driver addresses them both by table
// (when it looks a note up) and by `program * 16 + slot` (when it keys a known tone), as the game's driver did.

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::audio {

// One mono sample. Pitch 0x1000 advances one frame per output frame at `rate` 44100; a sample stored at another
// rate (a replacement from a mod) is stepped proportionally, so the same pitch keeps its musical meaning.
struct Sample {
    std::vector<std::int16_t> pcm;
    std::optional<std::uint32_t> loop_start;  // after the last frame the voice continues here; none: it stops
    std::uint32_t rate = 44100;
};

struct BankProgram {
    std::uint8_t tones = 0;
    std::uint8_t volume = 0;
    std::uint8_t pan = 64;
    std::uint8_t priority = 0;
    std::uint8_t mode = 0;
};

// `saved_adsr1/2` keep the envelope the bank was opened with, which a song can restore after it changed the
// envelope with a data entry.
struct BankTone {
    std::uint8_t priority = 0, mode = 0, volume = 0, pan = 0;
    std::uint8_t root_key = 0, fine_tune = 0, key_min = 0, key_max = 0;
    std::uint8_t vibrato_width = 0, vibrato_time = 0, portamento_width = 0, portamento_time = 0;
    std::uint8_t bend_min = 0, bend_max = 0;
    std::uint16_t adsr1 = 0, adsr2 = 0;
    std::int16_t program = 0;
    std::int16_t sample = 0;  // 1-based number of the bank's sample; 0: the tone has none
    std::uint16_t saved_adsr1 = 0, saved_adsr2 = 0;
};

struct SoundBank {
    std::string name;
    std::uint8_t master_volume = 127;
    std::uint8_t master_pan = 64;
    std::size_t tone_tables = 0;
    std::array<BankProgram, 128> programs{};
    std::vector<BankTone> tones;                       // tone_tables * 16
    std::vector<std::shared_ptr<const Sample>> samples;  // sample n is samples[n - 1]; null when it has no data

    // Sample `number` (1-based), or null.
    std::shared_ptr<const Sample> sample(int number) const;
    // The tone at table * 16 + slot, or null past the end.
    BankTone* tone(std::size_t index) { return index < tones.size() ? &tones[index] : nullptr; }
    const BankTone* tone(std::size_t index) const { return index < tones.size() ? &tones[index] : nullptr; }
    // The table of `program` as the driver finds it when it looks a note up: the number of programs with tones
    // before it.
    std::size_t table_of(int program) const;
};

// Reads a file of the bank relative to its JSON (`BASE/001.wav`). Fails when the file is missing.
using BankFileReader = std::function<Result<std::vector<std::uint8_t>>(std::string_view relative)>;

// The importer's bank description (convert/audio/vab/<NAME>.vab.json) and its WAVs.
Result<SoundBank> load_sound_bank(std::string_view json_text, const BankFileReader& read_file);

// A VAB file from the disc, decoded the same way the importer decodes it. For the importer and the tools; the game
// reads only the converted banks.
Result<SoundBank> parse_vab(std::span<const std::uint8_t> raw);

}  // namespace sh::audio
