#include "engine/audio/voice_stream.h"

#include "engine/audio/synth.h"

#include <algorithm>

namespace sh::audio {
namespace {

// The CD audio interpolator of the console (psx-spx "25-point Zigzag Interpolation"): every six 37.8 kHz frames
// give seven 44.1 kHz frames, phase p from the 29 frames before it. Columns are the seven output phases.
constexpr std::int16_t kZigZag[7][29] = {
    {0, 0, 0, 0, 0, -0x0002, 0x000A, -0x0022, 0x0041, -0x0054, 0x0034, 0x0009, -0x010A, 0x0400, -0x0A78, 0x234C,
     0x6794, -0x1780, 0x0BCD, -0x0623, 0x0350, -0x016D, 0x006B, 0x000A, -0x0010, 0x0011, -0x0008, 0x0003, -0x0001},
    {0, 0, 0, -0x0002, 0, 0x0003, -0x0013, 0x003C, -0x004B, 0x00A2, -0x00E3, 0x0132, -0x0043, -0x0267, 0x0C9D, 0x74BB,
     -0x11B4, 0x09B8, -0x05BF, 0x0372, -0x01A8, 0x00A6, -0x001B, 0x0005, 0x0006, -0x0008, 0x0003, -0x0001, 0},
    {0, 0, -0x0001, 0x0003, -0x0002, -0x0005, 0x001F, -0x004A, 0x00B3, -0x0192, 0x02B1, -0x039E, 0x04F8, -0x05A6,
     0x7939, -0x05A6, 0x04F8, -0x039E, 0x02B1, -0x0192, 0x00B3, -0x004A, 0x001F, -0x0005, -0x0002, 0x0003, -0x0001, 0,
     0},
    {0, -0x0001, 0x0003, -0x0008, 0x0006, 0x0005, -0x001B, 0x00A6, -0x01A8, 0x0372, -0x05BF, 0x09B8, -0x11B4, 0x74BB,
     0x0C9D, -0x0267, -0x0043, 0x0132, -0x00E3, 0x00A2, -0x004B, 0x003C, -0x0013, 0x0003, 0, -0x0002, 0, 0, 0},
    {-0x0001, 0x0003, -0x0008, 0x0011, -0x0010, 0x000A, 0x006B, -0x016D, 0x0350, -0x0623, 0x0BCD, -0x1780, 0x6794,
     0x234C, -0x0A78, 0x0400, -0x010A, 0x0009, 0x0034, -0x0054, 0x0041, -0x0022, 0x000A, -0x0001, 0, 0, 0, 0, 0},
    {0x0002, -0x0008, 0x0010, -0x0023, 0x002B, 0x001A, -0x00EB, 0x027B, -0x0548, 0x0AFA, -0x16FA, 0x53E0, 0x3C07,
     -0x1249, 0x080E, -0x0347, 0x015B, -0x0044, -0x0017, 0x0046, -0x0023, 0x0011, -0x0005, 0, 0, 0, 0, 0, 0},
    {-0x0005, 0x0011, -0x0023, 0x0046, -0x0017, -0x0044, 0x015B, -0x0347, 0x080E, -0x1249, 0x3C07, 0x53E0, -0x16FA,
     0x0AFA, -0x0548, 0x027B, -0x00EB, 0x001A, 0x002B, -0x0023, 0x0010, -0x0008, 0x0002, 0, 0, 0, 0, 0, 0},
};

// Each product scaled by 1/0x8000, rounded towards zero.
constexpr std::int32_t tap(std::int32_t product) {
    return product >= 0 ? product >> 15 : -((-product + 0x7FFF) >> 15);
}

}  // namespace

void VoiceStream::start(std::shared_ptr<const std::vector<std::int16_t>> pcm, int channels, std::uint32_t rate,
                        std::uint32_t first, std::uint32_t frames) {
    channels_ = channels == 1 ? 1 : 2;
    rate_ = rate == 18900 ? 18900 : 37800;
    const std::size_t available = pcm ? pcm->size() / static_cast<std::size_t>(channels_) : 0;
    first_ = static_cast<std::uint32_t>(std::min<std::size_t>(first, available));
    frames_ = static_cast<std::uint32_t>(std::min<std::size_t>(frames, available - first_));
    pcm_ = frames_ ? std::move(pcm) : nullptr;
    // Seven output frames for every six 37.8 kHz frames (an 18.9 kHz frame counts twice).
    const std::uint64_t at_37800 = static_cast<std::uint64_t>(frames_) * (rate_ == 18900 ? 2 : 1);
    out_total_ = at_37800 / 6 * 7;
    out_done_ = 0;
    paused_ = false;
    ring_ = {};
    ring_pos_ = 0;
    six_step_ = 6;
    next_input_ = 0;
    input_repeat_ = 0;
    queued_ = 0;
    queue_read_ = 0;
}

void VoiceStream::stop() {
    pcm_.reset();
    out_total_ = out_done_ = 0;
    paused_ = false;
    queued_ = queue_read_ = 0;
}

void VoiceStream::push_input() {
    std::int16_t l = 0, r = 0;
    if (next_input_ < frames_) {
        const std::size_t at = (static_cast<std::size_t>(first_) + next_input_) * static_cast<std::size_t>(channels_);
        l = (*pcm_)[at];
        r = channels_ == 2 ? (*pcm_)[at + 1] : l;
    }
    if (rate_ == 18900 && input_repeat_ == 0) {
        input_repeat_ = 1;
    } else {
        input_repeat_ = 0;
        ++next_input_;
    }
    ring_[ring_pos_] = {l, r};
    ring_pos_ = (ring_pos_ + 1) & 31;
    if (--six_step_ != 0) {
        return;
    }
    six_step_ = 6;
    for (int phase = 0; phase < 7; ++phase) {
        std::int32_t left = 0, right = 0;
        for (std::uint32_t i = 1; i <= 29; ++i) {
            const Frame& f = ring_[(ring_pos_ - i) & 31];
            left += tap(f.left * kZigZag[phase][i - 1]);
            right += tap(f.right * kZigZag[phase][i - 1]);
        }
        queue_[static_cast<std::size_t>(phase)] = {static_cast<std::int16_t>(std::clamp(left, -0x8000, 0x7FFF)),
                                                   static_cast<std::int16_t>(std::clamp(right, -0x8000, 0x7FFF))};
    }
    queued_ = 7;
    queue_read_ = 0;
}

void VoiceStream::mix(std::int32_t* left, std::int32_t* right, std::uint32_t frames, std::int32_t volume_left,
                      std::int32_t volume_right) {
    if (!pcm_ || paused_) {
        return;
    }
    for (std::uint32_t f = 0; f < frames && out_done_ < out_total_; ++f, ++out_done_) {
        while (queue_read_ >= queued_) {
            push_input();
        }
        const Frame& o = queue_[queue_read_++];
        left[f] += (o.left * volume_left) >> 15;
        right[f] += (o.right * volume_right) >> 15;
    }
}

}  // namespace sh::audio
