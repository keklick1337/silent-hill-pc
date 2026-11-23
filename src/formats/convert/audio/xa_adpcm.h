#pragma once

#include "formats/convert/audio/adpcm.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sh::convert::audio {

// A Form 2 sector as the HILL. stream holds it: subheader (8 bytes), 18 sound groups of 128 bytes, 20 spare bytes, EDC.
inline constexpr std::size_t kXaSectorBytes = 2336;
inline constexpr std::size_t kXaSubheaderBytes = 8;
inline constexpr std::size_t kXaGroupBytes = 128;
inline constexpr std::size_t kXaGroups = 18;
inline constexpr std::size_t kXaBlockSamples = 28;

// Submode byte (subheader byte 2).
inline constexpr std::uint8_t kXaSubmodeEof = 0x80;
inline constexpr std::uint8_t kXaSubmodeForm2 = 0x20;
inline constexpr std::uint8_t kXaSubmodeData = 0x08;
inline constexpr std::uint8_t kXaSubmodeAudio = 0x04;

// Coding info (subheader byte 3). Level A is 8-bit at 37.8 kHz, level B is 4-bit at 37.8 kHz, level C is 4-bit at
// 18.9 kHz; each is mono or stereo.
struct XaCoding {
    bool stereo = true;
    std::uint32_t sample_rate = 37800;
    unsigned bits = 4;  // bits per ADPCM sample: 4 or 8
    bool emphasis = false;

    bool operator==(const XaCoding&) const = default;
};

// Null for the reserved rate and depth values (rate code 2..3, depth code 2..3).
std::optional<XaCoding> parse_xa_coding(std::uint8_t coding);

// True for a Form 2 sector with the audio bit: the sectors the CD controller feeds to the ADPCM decoder.
bool is_xa_audio_submode(std::uint8_t submode);

// Frames (one sample per channel) in one sector: 2016 for 4-bit stereo, 4032 for 4-bit mono, 1008 and 2016 for 8-bit.
std::size_t xa_frames_per_sector(const XaCoding& coding);

// What the prediction of the next sample runs on. The console's CD controller keeps the samples as it clipped
// them to 16 bits; the format's reference decoder keeps them unclipped. The two differ only for a few samples
// after a peak that clips (on this game's disc: in four sectors of the movies).
enum class XaHistory {
    Saturated,  // the console: voice lines, and everything the importer converts
    Unclamped,  // the reference decoder: the sound of the disc's movies
};

// Decodes one sector. `out` receives xa_frames_per_sector() frames, interleaved L,R for stereo.
// `state` is the history of the left (or only) and the right channel; it carries on from sector to sector.
// The prediction has the +32 rounding term of the game's XA player.
void decode_xa_sector(std::span<const std::uint8_t, kXaSectorBytes> sector, const XaCoding& coding,
                      AdpcmState (&state)[2], std::int16_t* out, XaHistory history = XaHistory::Saturated);

}  // namespace sh::convert::audio
