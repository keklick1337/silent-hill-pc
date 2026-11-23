// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/audio/line_start_delay.h"

#include "engine/audio/disc_drive.h"

#include <algorithm>
#include <utility>

namespace sh::audio {

namespace {

// The voice stream is read at double speed.
constexpr int kSectorsPerSecond = 150;
// The stop task's frames after the fade: mute, pause the disc, clean up.
constexpr int kStopFrames = 3;
// The fade takes the CD volume down by this much a frame.
constexpr int kFadeStep = 24;
// cutscene_line_gap_ms of the console's pace; other values scale the delay.
constexpr int kConsoleGapMs = 300;
constexpr int kLongestDelayMs = 3000;

int frame_vblanks(const LineStartDelay::Moment& now) {
    if (now.frame_vblanks < 1) return 2;
    return std::min(now.frame_vblanks, 4);
}

// The voice lines count in the two television rates only.
int voice_blank_rate(const LineStartDelay::Moment& now) { return now.blank_rate == 50 ? 50 : 60; }

}  // namespace

int LineStartDelay::head_lba(const Moment& now, Head head) const {
    // The drive keeps reading through a line and the silence after it until the stop task pauses it, so the head
    // is wherever the stream has got to — unless something else has moved it since.
    if (!reading_ || line_epoch_ != head.epoch) return head.lba;
    const int until = paused_ ? line_pause_vblank_ : now.vblank;
    if (until <= line_start_vblank_) return line_lba_;
    return line_lba_ + (until - line_start_vblank_) * kSectorsPerSecond / voice_blank_rate(now);
}

void LineStartDelay::line_stopped(const Moment& now, unsigned head_epoch) {
    const int frame = frame_vblanks(now);
    int fade_frames = 0;
    int volume = volume_;
    do {
        ++fade_frames;
        volume -= kFadeStep;
    } while (volume >= 2);

    if (reading_ && !paused_ && line_epoch_ == head_epoch) {
        paused_ = true;
        line_pause_vblank_ = now.vblank + (fade_frames + 1) * frame;
    }
    volume_ = 0;
    stop_end_vblank_ = now.vblank + (fade_frames + kStopFrames) * frame;
}

LineStartDelay::Estimate LineStartDelay::line_requested(const Moment& now, Head head, int target_lba, int volume,
                                                        int gap_ms) {
    const int frame = frame_vblanks(now);
    Estimate estimate;
    estimate.head_lba = head_lba(now, head);
    estimate.seek_vblanks = DiscDrive::seek_vblanks(estimate.head_lba, target_lba, now.blank_rate);
    estimate.stop_vblanks_left = std::max(stop_end_vblank_ - now.vblank, 0);
    // The seek command goes out one frame before the drive is first asked whether it is done.
    if (estimate.seek_vblanks > frame) estimate.seek_wait_frames = (estimate.seek_vblanks - 1) / frame;

    int delay = estimate.stop_vblanks_left + (kPlayFrames + estimate.seek_wait_frames) * frame;
    delay = gap_ms <= 0 ? 0 : delay * gap_ms / kConsoleGapMs;
    estimate.delay_vblanks = std::min(delay, kLongestDelayMs * voice_blank_rate(now) / 1000);
    pending_vblanks_ = estimate.delay_vblanks;

    reading_ = true;
    paused_ = false;
    line_lba_ = target_lba;
    line_epoch_ = head.epoch;
    line_start_vblank_ = now.vblank + pending_vblanks_;
    volume_ = volume;
    return estimate;
}

int LineStartDelay::take_delay() { return std::exchange(pending_vblanks_, 0); }

}  // namespace sh::audio
