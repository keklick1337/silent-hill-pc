// SPDX-License-Identifier: GPL-3.0-or-later
// The "violent and disturbing images" warning shown when the game starts, before the publisher's logo: the
// disc's own picture faded in from black, held, and faded out again. A confirm key or pad button cuts it short.
// `skip_intros` leaves it out. The picture's placement and the timing are gamedata/menu/warning_screen.json.
#pragma once

namespace sh::ui {

// The frames of the warning, one after another: how dark each is, and when it is over.
class WarningSequence {
public:
    // `fade_step` is the change of darkness (0..255) per frame while fading; the picture is held for
    // `hold_frames` and `black_frames` of full black follow the fade out, so that nothing of the picture is left
    // on any buffer of the display when the next screen sets it up.
    WarningSequence(int fade_step, int hold_frames, int black_frames)
        : fade_step_(fade_step > 0 ? fade_step : 1), hold_left_(hold_frames), black_left_(black_frames) {}

    bool done() const { return phase_ == Phase::Done; }
    // The frame to show now: whether it is darkened and by how much (255 is black).
    bool darkened() const { return phase_ != Phase::Hold; }
    int darkness() const { return phase_ == Phase::Black || phase_ == Phase::Final ? 255 : darkness_; }
    // The last frame: nothing follows it.
    bool final_frame() const { return phase_ == Phase::Final; }
    // Whether a skip counts on this frame: once the picture is fading out it runs to its end.
    bool skippable() const { return phase_ == Phase::FadeIn || phase_ == Phase::Hold; }

    // The frame was shown; `skip` is whether the player asks to go on.
    void advance(bool skip) {
        switch (phase_) {
            case Phase::FadeIn:
                if (skip) {
                    // The fade out goes on from this darkness: starting it from the full picture would show
                    // the warning a second time.
                    begin_fade_out();
                    break;
                }
                darkness_ -= fade_step_;
                if (darkness_ < 0) {
                    darkness_ = 0;
                    phase_ = Phase::Hold;
                    if (hold_left_ <= 0) {
                        begin_fade_out();
                    }
                }
                break;
            case Phase::Hold:
                hold_left_--;
                if (skip || hold_left_ <= 0) {
                    begin_fade_out();
                }
                break;
            case Phase::FadeOut:
                darkness_ += fade_step_;
                if (darkness_ >= 255) {
                    begin_black();
                }
                break;
            case Phase::Black:
                black_left_--;
                if (black_left_ <= 0) {
                    phase_ = Phase::Final;
                }
                break;
            case Phase::Final:
                phase_ = Phase::Done;
                break;
            case Phase::Done:
                break;
        }
    }

private:
    enum class Phase { FadeIn, Hold, FadeOut, Black, Final, Done };

    void begin_fade_out() {
        if (darkness_ >= 255) {
            begin_black();
        } else {
            phase_ = Phase::FadeOut;
        }
    }

    void begin_black() { phase_ = black_left_ > 0 ? Phase::Black : Phase::Final; }

    Phase phase_ = Phase::FadeIn;
    int darkness_ = 255;
    int fade_step_;
    int hold_left_;
    int black_left_;
};

// Shows the warning and returns when it is over. Runs its own frames: the game's loop has not started yet.
void play_boot_warning();

}  // namespace sh::ui
