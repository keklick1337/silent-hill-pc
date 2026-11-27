#pragma once

// Sound effects from own files ("own file" entries of the sound registry): a WAV played as it
// is, resampled to the mix rate, with the game's volume for the call on top of the entry's gain. Separate from the
// bank voices on purpose: what the game reads back about voices (key status, released voices) stays the bank
// voices', so a mod's file changes what is heard and nothing the logic sees.

#include <cstdint>
#include <memory>
#include <vector>

namespace sh::audio {

class FilePlayer {
public:
    static constexpr int kVoices = 16;
    static constexpr int kUnity = 0x4000;  // gain units of play()/set_volume()

    // `rate_scale` multiplies the file's own rate (pitch); `tag` names the voice for stop()/set_volume() (the
    // sound id). A new voice replaces the oldest when all are busy.
    void play(std::shared_ptr<const std::vector<std::int16_t>> pcm, int channels, std::uint32_t rate,
              double rate_scale, int left, int right, bool loop, int tag);
    void stop(int tag);
    void stop_all();
    void set_volume(int tag, int left, int right);
    int active(int tag) const;  // voices playing `tag`
    int active_count() const;
    // The voices playing now (the console's sound dump).
    struct VoiceInfo {
        int tag = -1;
        int left = 0, right = 0;  // of kUnity
        bool loop = false;
    };
    std::vector<VoiceInfo> voices() const;

    // Adds the voices into 44.1 kHz stereo accumulators.
    void mix(std::int32_t* left, std::int32_t* right, std::uint32_t frames);

private:
    struct Voice {
        std::shared_ptr<const std::vector<std::int16_t>> pcm;
        int channels = 1;
        std::uint64_t position = 0;  // frames, 32.32 fixed point
        std::uint64_t step = 0;
        std::uint64_t frames = 0;
        int left = 0, right = 0;
        bool loop = false;
        int tag = -1;
        std::uint64_t serial = 0;
        bool on = false;
    };
    Voice voices_[kVoices];
    std::uint64_t serial_ = 0;
};

}  // namespace sh::audio
