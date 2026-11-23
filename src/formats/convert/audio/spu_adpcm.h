#pragma once

#include "formats/common/status.h"
#include "formats/convert/audio/adpcm.h"
#include "formats/convert/audio/wav.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sh::convert::audio {

inline constexpr std::size_t kSpuBlockBytes = 16;
inline constexpr std::size_t kSpuBlockSamples = 28;

// Flag byte of a block (byte 1).
inline constexpr std::uint8_t kSpuFlagEnd = 1;
inline constexpr std::uint8_t kSpuFlagRepeat = 2;
inline constexpr std::uint8_t kSpuFlagStart = 4;

struct SpuBlockInfo {
    std::uint8_t flags;
    bool shift_clamped;   // shift 13..15, which the SPU treats as 9
    bool filter_clamped;  // filter 5..15, which has no coefficients; treated as 4 like the game's SPU core
};

// Decodes one 16-byte block into 28 samples, exactly as the sound chip does: the prediction has no rounding term,
// the history keeps the saturated samples.
SpuBlockInfo decode_spu_block(std::span<const std::uint8_t, kSpuBlockBytes> block, AdpcmState& state,
                              std::int16_t* out28);

struct SpuSample {
    std::vector<std::int16_t> pcm;  // from block 0 to the first end-flag block, inclusive
    std::size_t blocks_in = 0;      // blocks in the input
    std::size_t blocks_played = 0;  // blocks in pcm
    bool has_end = false;           // an end-flag block was found
    std::optional<WavLoop> loop;    // end block has the repeat flag: loop from the last start-flag block (or 0)
    std::size_t trailing_bytes = 0; // input bytes after the last whole block
    std::size_t clamped_shifts = 0;
    std::size_t clamped_filters = 0;
};

// Plays the sample the way a voice does from its start address: decoding stops at the first block with the end flag.
// The blocks after it are not part of the sound (a VAB keeps a silent dummy block there).
Result<SpuSample> decode_spu_sample(std::span<const std::uint8_t> data);

}  // namespace sh::convert::audio
