#include "engine/audio/music_player.h"

namespace sh::audio {

MusicPlayer::MusicPlayer() : synth_(std::make_unique<Synth>()) {
    // The game's settings at start-up: reverb room at depth 20, main volume 127. The engine applies the game's main
    // volume to the whole mix, so this synthesizer stays at full scale.
    synth_->set_main_volume(0x3FFF, 0x3FFF);
    synth_->set_reverb_mode(1);
    synth_->set_reverb_depth(20 << 8, 20 << 8);
    synth_->set_reverb_enabled(true);
    seq_ = std::make_unique<Sequencer>(*synth_);
}

void MusicPlayer::set_bank(std::shared_ptr<const SoundBank> bank) { bank_ = std::move(bank); }

void MusicPlayer::open(std::shared_ptr<const Song> song) {
    song_ = std::move(song);
    status_ = song_ ? Stop : None;
}

void MusicPlayer::play() {
    if (!song_ || !bank_) {
        return;
    }
    if (bank_in_sequencer_ != bank_) {
        seq_->open_bank(*bank_);
        bank_in_sequencer_ = bank_;
    }
    if (!seq_->play(*song_)) {
        return;
    }
    seq_->set_volume(volume_left_, volume_right_);
    status_ = Play;
}

void MusicPlayer::stop() {
    if (status_ == None) {
        return;
    }
    seq_->stop();
    volume_left_ = volume_right_ = 127;
    status_ = Stop;
}

void MusicPlayer::close() {
    seq_->stop();
    song_.reset();
    volume_left_ = volume_right_ = 127;
    status_ = None;
}

void MusicPlayer::pause() {
    if (status_ == Play) {
        status_ = Pause;
    }
}

void MusicPlayer::resume() {
    if (status_ == Pause) {
        status_ = Play;
    }
}

int MusicPlayer::status() {
    if (status_ == Play && !seq_->playing()) {
        status_ = End;
    }
    return status_;
}

void MusicPlayer::set_volume(int left, int right) {
    volume_left_ = left & 0x7F;
    volume_right_ = right & 0x7F;
    seq_->set_volume(volume_left_, volume_right_);
}

void MusicPlayer::set_channel_volume(int channel, int volume) { seq_->set_channel_volume(channel, volume); }

int MusicPlayer::channel_volume(int channel) const { return seq_->channel_volume(channel); }

void MusicPlayer::set_reverb_depth(int left, int right) {
    synth_->set_reverb_depth(static_cast<std::int16_t>((left << 16) >> 8), static_cast<std::int16_t>((right << 16) >> 8));
}

void MusicPlayer::timer_tick() {
    if (status_ == Play) {
        seq_->timer_tick();
    }
}

void MusicPlayer::render(std::int32_t* left, std::int32_t* right, std::uint32_t frames) {
    scratch_.resize(static_cast<std::size_t>(frames) * 2);
    synth_->render(scratch_.data(), frames);
    // A paused song keeps its voices running silently, as the driver mutes them.
    if (status_ == Pause) {
        return;
    }
    for (std::uint32_t i = 0; i < frames; ++i) {
        left[i] += scratch_[2 * i];
        right[i] += scratch_[2 * i + 1];
    }
}

}  // namespace sh::audio
