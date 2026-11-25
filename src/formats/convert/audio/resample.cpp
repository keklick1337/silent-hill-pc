#include "formats/convert/audio/resample.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace sh::convert::audio {
namespace {

constexpr int kHalfTaps = 16;
constexpr int kPhases = 256;
constexpr double kBeta = 8.6;
constexpr double kPi = 3.14159265358979323846;

double bessel_i0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 32; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

// Row p holds the taps for an output frame that lies p / kPhases of a frame after input frame `i`: tap j weighs
// input frame i - kHalfTaps + 1 + j. One extra row (p = kPhases) lets the rows be blended linearly.
using Table = std::vector<std::array<float, 2 * kHalfTaps>>;

Table make_table(double cutoff) {
    Table table(kPhases + 1);
    const double norm = bessel_i0(kBeta);
    for (int p = 0; p <= kPhases; ++p) {
        double sum = 0.0;
        for (int j = 0; j < 2 * kHalfTaps; ++j) {
            const double x = (j - kHalfTaps + 1) - static_cast<double>(p) / kPhases;
            const double sinc = x == 0.0 ? 1.0 : std::sin(kPi * cutoff * x) / (kPi * cutoff * x);
            const double r = x / kHalfTaps;
            const double window = std::abs(r) >= 1.0 ? 0.0 : bessel_i0(kBeta * std::sqrt(1.0 - r * r)) / norm;
            table[p][j] = static_cast<float>(sinc * window);
            sum += sinc * window;
        }
        // Each row sums to one, so silence stays silence and a constant level is kept exactly.
        for (float& tap : table[p]) {
            tap = static_cast<float>(tap / sum);
        }
    }
    return table;
}

}  // namespace

std::uint64_t resample_position(std::uint64_t frame, std::uint32_t from, std::uint32_t to) {
    return from == 0 ? frame : (frame * to + from / 2) / from;
}

std::vector<std::int16_t> resample_pcm(std::span<const std::int16_t> pcm, int channels, std::uint32_t from,
                                       std::uint32_t to) {
    if (channels <= 0 || from == 0 || to == 0) {
        return {};
    }
    const std::size_t width = static_cast<std::size_t>(channels);
    const std::uint64_t in_frames = pcm.size() / width;
    if (from == to) {
        return std::vector<std::int16_t>(pcm.begin(), pcm.begin() + static_cast<std::ptrdiff_t>(in_frames * width));
    }
    // A little below the Nyquist frequency of the lower rate, so the window's transition band stays under it.
    const double cutoff = 0.95 * std::min(1.0, static_cast<double>(to) / from);
    const Table table = make_table(cutoff);
    const std::uint64_t out_frames = resample_position(in_frames, from, to);
    std::vector<std::int16_t> out(out_frames * width);
    std::array<float, 2 * kHalfTaps> taps{};
    for (std::uint64_t frame = 0; frame < out_frames; ++frame) {
        // Output frame `frame` sits at input position frame * from / to.
        const std::uint64_t position = frame * from;
        const std::int64_t base = static_cast<std::int64_t>(position / to);
        const double fraction = static_cast<double>(position % to) / to * kPhases;
        const int row = std::min(static_cast<int>(fraction), kPhases - 1);
        const float blend = static_cast<float>(fraction - row);
        for (int j = 0; j < 2 * kHalfTaps; ++j) {
            taps[j] = table[row][j] + (table[row + 1][j] - table[row][j]) * blend;
        }
        const std::int64_t first = base - kHalfTaps + 1;
        for (std::size_t c = 0; c < width; ++c) {
            float sum = 0.0f;
            for (int j = 0; j < 2 * kHalfTaps; ++j) {
                const std::int64_t at = first + j;
                if (at >= 0 && static_cast<std::uint64_t>(at) < in_frames) {
                    sum += taps[j] * pcm[static_cast<std::size_t>(at) * width + c];
                }
            }
            out[frame * width + c] = static_cast<std::int16_t>(std::clamp(std::lround(sum), -32768L, 32767L));
        }
    }
    return out;
}

}  // namespace sh::convert::audio
