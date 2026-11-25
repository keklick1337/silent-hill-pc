#pragma once

// Ogg Opus (RFC 7845): the compressed sound a mod may ship wherever it may ship a WAV, and the opus music export of
// the import. The codec is libopus; the Ogg container (RFC 3533) is read and written here, which is all of Ogg an
// Opus file uses: one logical stream of packets in checksummed pages.

#include "formats/common/status.h"
#include "formats/convert/audio/wav.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sh::convert::audio {

// Opus decodes and encodes at 48 kHz.
inline constexpr std::uint32_t kOpusRate = 48000;

// The bytes begin with an Ogg page.
bool is_ogg(std::span<const std::uint8_t> bytes);

// The first Opus stream of an Ogg file, decoded page by page as it is asked for. Mono and stereo streams only (the
// engine mixes no more). The header's output gain is applied, the encoder's delay (pre-skip) and the padding after
// the end are cut. A loop comes from the tags LOOPSTART and LOOPLENGTH (or LOOPEND), in 48 kHz frames.
class OggOpusReader {
public:
    // `bytes` must stay alive while the reader is used. Fails when the headers are missing or broken.
    static Result<std::unique_ptr<OggOpusReader>> open(std::span<const std::uint8_t> bytes);
    ~OggOpusReader();
    OggOpusReader(const OggOpusReader&) = delete;
    OggOpusReader& operator=(const OggOpusReader&) = delete;

    int channels() const;
    // The rate of the source before it was encoded (information only: the output is 48 kHz).
    std::uint32_t input_rate() const;
    std::optional<WavLoop> loop() const;

    // Up to out.size() / channels() frames into `out`, interleaved; 0 at the end. A broken page or packet fails.
    Result<std::size_t> read(std::span<std::int16_t> out);

private:
    struct State;
    explicit OggOpusReader(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

// The whole stream at 48 kHz, its loop as the sampler loop. At most 30 minutes.
Result<Wav> read_ogg_opus(std::span<const std::uint8_t> bytes);

struct OpusEncoding {
    int bitrate = 128000;               // bits per second, all channels together
    std::uint32_t serial = 0x53480001;  // the Ogg stream's serial number (a fixed one keeps the output reproducible)
    std::vector<std::string> comments;  // "KEY=value" in the tags, after the loop
};

// Mono or stereo PCM16 at any rate (resampled to 48 kHz first) as an Ogg Opus file. The sampler loop becomes the
// tags LOOPSTART and LOOPLENGTH at 48 kHz.
Result<std::vector<std::uint8_t>> write_ogg_opus(const Wav& wav, const OpusEncoding& encoding = {});

// A sound file by its first bytes: Ogg Opus (48 kHz), otherwise WAV (read_wav).
Result<Wav> read_sound(std::span<const std::uint8_t> bytes);

}  // namespace sh::convert::audio
