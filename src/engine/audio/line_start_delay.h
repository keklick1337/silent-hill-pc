// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// How long the console took before a voice line was heard. A line queued two tasks of the sound driver, a stop
// and a play, and the driver ran one state of a task per game frame: the stop faded the line before it out,
// muted and paused the disc; the play set the drive up, sought to the line and started reading. Everything but
// the seek took a fixed number of frames; the seek depended on how far the head had to travel from where the
// line before had left it. The engine starts a line at once, so this model gives the frames the console would
// have spent, and the player holds the line's start by them: the rhythm of a voiced scene stays the console's.
//
// Times are vertical blanks, the count the game itself paces its voice lines by; wall-clock time would make the
// start of a line depend on the frame rate.

#include <cstdint>

namespace sh::audio {

class LineStartDelay {
public:
    struct Moment {
        int vblank = 0;         // the blank count now
        int frame_vblanks = 2;  // blanks one game frame lasts
        int blank_rate = 60;    // blanks a second
    };
    // The drive model's head: where it last read, and a number that changes whenever something else moved it.
    struct Head {
        int lba = 0;
        unsigned epoch = 0;
    };
    // What line_requested() worked out, for the log.
    struct Estimate {
        int head_lba = 0;
        int seek_vblanks = 0;
        int stop_vblanks_left = 0;
        int seek_wait_frames = 0;
        int delay_vblanks = 0;
    };

    // Frames the play task spends outside the wait for the seek.
    static constexpr int kPlayFrames = 7;

    // The driver's stop task ran.
    void line_stopped(const Moment& now, unsigned head_epoch);
    // The driver's play task ran for a line whose sound starts at sector `target_lba`, at CD volume `volume`.
    // `gap_ms` is the setting cutscene_line_gap_ms: 300 is the console's pace, 0 no delay at all.
    Estimate line_requested(const Moment& now, Head head, int target_lba, int volume, int gap_ms);
    // The play task ran for a line that has no sound: nothing is to be delayed.
    void forget_delay() { pending_vblanks_ = 0; }
    // The delay of the line last requested; given out once.
    int take_delay();

private:
    int head_lba(const Moment& now, Head head) const;

    // The line the drive is (or was last) reading: where it began and when, and when the stop task paused it.
    bool reading_ = false;
    bool paused_ = false;
    int line_lba_ = 0;
    unsigned line_epoch_ = 0;
    int line_start_vblank_ = 0;
    int line_pause_vblank_ = 0;
    int volume_ = 0;
    int stop_end_vblank_ = 0;
    int pending_vblanks_ = 0;
};

}  // namespace sh::audio
