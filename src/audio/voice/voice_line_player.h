// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The game's voice lines on the engine's mixer. A line's frames come from the
// converted speech of the disc, or from a mod's file; what the game's sound tasks read back follows the console:
//  - a line's sound starts after the delay the console's drive and sound tasks took (LineStartDelay);
//  - a line has ended only when every frame was mixed and the blank count passed its start + its authored
//    length + 32, the watchdog of the console's sound task pool, which is the rhythm of every voiced scene;
//  - in a replay the mixer takes the frames by the replay's clock, so both moments depend on the blank count
//    alone.

#include "engine/audio/line_start_delay.h"
#include "engine/audio/voice_clip.h"

#include <cstdint>

namespace sh::audio {

class VoiceLinePlayer {
public:
    // The sound driver's play task: line `line` of the table of voice lines. A line that cannot play reports
    // its end at once, so that no scene waits for it.
    void play_line(std::uint16_t line);
    // Stops whatever plays and reports the line's end to the driver.
    void stop();
    // Once per game frame: starts the sound when its delay is over, ends the line when it is due.
    void update();

    // A mod's file as a voice line of its own (the voice of a text box). While a line of the disc is still heard
    // the file is mixed over it and plays on alone if it lasts longer. False, with nothing changed, when the
    // file is missing or cannot be played.
    bool play_file(const char* path);
    // Stops only a file; a line of the disc is left alone.
    void stop_file();
    bool file_audible() const;

    // A line is still producing sound. Unlike the driver's "a line is streaming" this is false during the
    // silence that pads a line's end: the pages of a voiced message turn when the sound is over.
    bool sounding() const;
    // The game clock is frozen: the line and its timing stop with it, or the voice would run ahead of the scene.
    void hold(bool held);

    // The voice volume of the settings, 0..1, on top of the volume the game gives each line.
    void set_volume(float volume);
    float volume() const { return volume_; }

    // The driver's stop and play tasks ran: they feed the model of the line's start delay.
    void stop_task_ran();
    void play_task_ran(std::uint16_t line, int volume);

private:
    void start_clip(const VoiceClip& clip, bool is_file);
    void finish();

    bool playing_ = false;
    // The line's frames are being mixed (its start delay is over).
    bool sounding_ = false;
    bool is_file_ = false;
    std::uint16_t line_ = 0;
    int start_vblank_ = 0;
    int end_vblank_ = 0;
    // The blank the start delay ends at; 0 when there is none.
    int delay_end_vblank_ = 0;
    bool held_ = false;
    int hold_start_vblank_ = 0;
    float volume_ = 1.0f;
    LineStartDelay start_delay_;
};

// The player of the running game.
VoiceLinePlayer& voice_lines();

}  // namespace sh::audio
