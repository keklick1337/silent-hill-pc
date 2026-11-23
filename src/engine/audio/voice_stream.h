#pragma once

// Streamed speech: the voice lines of the game (the CD audio input of the console's sound chip). A line is a run of
// frames of a converted stream WAV (convert/audio/xa/<track>_fNN_chN.wav), mono or stereo at 37.8 or 18.9 kHz,
// brought to 44.1 kHz by the console's zigzag interpolator and mixed at the CD volume.
//
// The game decides that a line has ended from the frames consumed, so the stream counts output frames exactly: every
// six 37.8 kHz frames (an 18.9 kHz frame counts twice) give seven, and drained() is true from the first mix after
// the last of them.

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace sh::audio {

class VoiceStream {
public:
    // Interleaved 16-bit frames shared with the cache of the caller; [first, first + frames) is the line.
    void start(std::shared_ptr<const std::vector<std::int16_t>> pcm, int channels, std::uint32_t rate,
               std::uint32_t first, std::uint32_t frames);
    void stop();

    bool active() const { return pcm_ != nullptr; }
    // Every frame of the line was mixed (or nothing plays).
    bool drained() const { return !pcm_ || out_done_ >= out_total_; }
    std::uint64_t output_frames_left() const { return out_done_ >= out_total_ ? 0 : out_total_ - out_done_; }

    // Frames stop being consumed (a frozen game); they continue where they stopped.
    void set_paused(bool paused) { paused_ = paused; }
    bool paused() const { return paused_; }

    // Adds the line to `left`/`right` at `volume_*` (0..0x8000 = unity) for `frames` output frames.
    void mix(std::int32_t* left, std::int32_t* right, std::uint32_t frames, std::int32_t volume_left,
             std::int32_t volume_right);

private:
    struct Frame {
        std::int16_t left = 0, right = 0;
    };
    void push_input();

    std::shared_ptr<const std::vector<std::int16_t>> pcm_;
    int channels_ = 2;
    std::uint32_t rate_ = 37800;
    std::uint32_t first_ = 0, frames_ = 0;
    std::uint64_t out_total_ = 0, out_done_ = 0;
    bool paused_ = false;
    std::array<Frame, 32> ring_{};
    std::uint32_t ring_pos_ = 0;
    int six_step_ = 6;
    std::uint32_t next_input_ = 0;
    int input_repeat_ = 0;
    std::array<Frame, 7> queue_{};
    std::uint32_t queued_ = 0, queue_read_ = 0;
};

}  // namespace sh::audio
