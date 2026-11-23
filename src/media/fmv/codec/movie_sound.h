// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The sound of the disc's movies: CD-ROM XA ADPCM sectors that lie between the picture sectors, four bits a
// sample. The decoding itself is the one decoder of the engine (formats/convert/audio/xa_adpcm.h); a movie
// keeps the prediction's history unclamped, as the format's reference decoder does.

#include "formats/convert/audio/xa_adpcm.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sh::fmv {

inline constexpr std::size_t kMovieSectorBytes = convert::audio::kXaSectorBytes;
// Samples one sector decodes to, counting both channels of a stereo sector.
inline constexpr int kMovieSectorSamples = 18 * 8 * 28;

class MovieSoundDecoder {
public:
    // What a sector (2336 bytes from its subheader on) plays as; nothing when it is not four-bit sound of a
    // known rate, which the movies never hold.
    static std::optional<convert::audio::XaCoding> coding(const std::uint8_t* sector) {
        const std::optional<convert::audio::XaCoding> coding = convert::audio::parse_xa_coding(sector[3]);
        return coding && coding->bits == 4 ? coding : std::nullopt;
    }

    // Decodes one sector into `out` (room for kMovieSectorSamples); stereo comes out interleaved, left first.
    // Returns the count of samples written: kMovieSectorSamples, or 0 for a sector coding() refuses.
    int decode_sector(const std::uint8_t* sector, std::int16_t* out) {
        const std::optional<convert::audio::XaCoding> sector_coding = coding(sector);
        if (!sector_coding) return 0;
        convert::audio::decode_xa_sector(std::span<const std::uint8_t, kMovieSectorBytes>(sector, kMovieSectorBytes),
                                         *sector_coding, history_, out, convert::audio::XaHistory::Unclamped);
        return kMovieSectorSamples;
    }

    // Forgets the two samples of history per channel that the prediction runs on.
    void reset() { history_[0] = history_[1] = {}; }

private:
    convert::audio::AdpcmState history_[2];
};

}  // namespace sh::fmv
