#pragma once

// Real-time music: the sequencer on its own synthesizer. The engine (engine.h)
// drives it: the game's commands arrive through it, and its mixer calls timer_tick() at the driver's timer rate and
// render() in between. Not thread safe on its own; the engine locks.
//
// The music has its own 24 voices and does not compete with the sound effects for them; the game never relied on
// that (the console driver simply took the first released voice).

#include "engine/audio/sequencer.h"
#include "engine/audio/song.h"
#include "engine/audio/sound_bank.h"
#include "engine/audio/synth.h"

#include <cstdint>
#include <memory>

namespace sh::audio {

class MusicPlayer {
public:
    // The driver's song states.
    enum Status : int { None = 0, Play = 1, Stop = 2, End = 3, Pause = 4 };

    MusicPlayer();

    void set_bank(std::shared_ptr<const SoundBank> bank);
    bool has_bank() const { return bank_ != nullptr; }
    void open(std::shared_ptr<const Song> song);  // the song the next play() starts
    void play();
    void stop();
    void close();
    void pause();
    void resume();
    int status();

    void set_volume(int left, int right);             // 0..127
    void set_channel_volume(int channel, int volume);  // control 7
    int channel_volume(int channel) const;
    void set_reverb_depth(int left, int right);        // 0..127

    void timer_tick();
    // Adds `frames` stereo frames of music at 44.1 kHz to `left`/`right`.
    void render(std::int32_t* left, std::int32_t* right, std::uint32_t frames);

private:
    std::unique_ptr<Synth> synth_;
    std::unique_ptr<Sequencer> seq_;
    std::shared_ptr<const SoundBank> bank_;
    std::shared_ptr<const SoundBank> bank_in_sequencer_;
    std::shared_ptr<const Song> song_;
    int status_ = None;
    int volume_left_ = 127, volume_right_ = 127;
    std::vector<std::int16_t> scratch_;
};

}  // namespace sh::audio
