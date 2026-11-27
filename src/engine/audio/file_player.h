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
    // A level of its own on top of the volume (kFadeUnity = as set_volume says): the voices of `tag` move to `level`
    // over `frames` mix frames (0: at once), and stop when they get there if `stop_at_end`. A sound that is not
    // playing keeps the level for its next play() (the start of a fade-in from silence). False when no voice plays.
    static constexpr int kFadeUnity = 1 << 16;
    bool fade(int tag, int level, std::uint32_t frames, bool stop_at_end);
    int active(int tag) const;  // voices playing `tag`
    int active_count() const;
    // The voices playing now (the console's sound dump).
    struct VoiceInfo {
        int tag = -1;
        int left = 0, right = 0;  // of kUnity
        bool loop = false;
        int fade = kFadeUnity;  // the level of fade(), of kFadeUnity
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
        // fade(): the level now and where it goes, 32.32 of kFadeUnity; `fade_step` a frame, 0 when it is there.
        std::int64_t fade = static_cast<std::int64_t>(kFadeUnity) << 32;
        std::int64_t fade_target = static_cast<std::int64_t>(kFadeUnity) << 32;
        std::int64_t fade_step = 0;
        bool fade_stops = false;
    };
    struct StartLevel {
        int tag = -1;
        int level = kFadeUnity;
    };
    Voice voices_[kVoices];
    // Levels asked for sounds that were not playing: each is taken by the sound's next play().
    std::vector<StartLevel> start_levels_;
    std::uint64_t serial_ = 0;
};

}  // namespace sh::audio
