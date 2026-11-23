// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/fmv/codec/pcm.h"

#include <cstring>

namespace sh::fmv {

namespace {

template <class T, class Scale>
void scale_each(void* samples, std::size_t bytes, Scale scale) {
    std::uint8_t* at = static_cast<std::uint8_t*>(samples);
    for (std::size_t i = 0; i + sizeof(T) <= bytes; i += sizeof(T)) {
        T sample;
        std::memcpy(&sample, at + i, sizeof(T));
        sample = scale(sample);
        std::memcpy(at + i, &sample, sizeof(T));
    }
}

}  // namespace

float movie_gain(float volume, bool console_level) { return volume * (console_level ? kConsoleMovieLevel : 1.0f); }

void apply_gain(void* samples, std::size_t bytes, SampleFormat format, float gain) {
    if (gain >= 0.999f) return;
    if (gain < 0.0f) gain = 0.0f;
    switch (format) {
        case SampleFormat::S16:
            scale_each<std::int16_t>(samples, bytes, [gain](std::int16_t s) {
                return static_cast<std::int16_t>(static_cast<float>(s) * gain);
            });
            break;
        case SampleFormat::U8:
            // Unsigned samples rest at 128.
            scale_each<std::uint8_t>(samples, bytes, [gain](std::uint8_t s) {
                return static_cast<std::uint8_t>(128 + static_cast<int>((static_cast<float>(s) - 128.0f) * gain));
            });
            break;
        case SampleFormat::F32:
            scale_each<float>(samples, bytes, [gain](float s) { return s * gain; });
            break;
    }
}

void narrow_to_s16(std::span<const std::uint8_t> samples, int bits, std::vector<std::int16_t>& out) {
    const std::size_t width = static_cast<std::size_t>(bits) / 8;
    out.clear();
    if (width < 2) return;
    out.reserve(samples.size() / width);
    for (std::size_t i = 0; i + width <= samples.size(); i += width) {
        const std::uint8_t* top = samples.data() + i + width - 2;
        out.push_back(static_cast<std::int16_t>(static_cast<std::uint16_t>(top[0] | top[1] << 8)));
    }
}

}  // namespace sh::fmv
