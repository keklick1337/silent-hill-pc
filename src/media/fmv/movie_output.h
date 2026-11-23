// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What every kind of movie shares while it plays: the picture's way to the screen, the sound device and the
// player's wish to skip. Private to the movie player.

#include "media/fmv/codec/pcm.h"

#include <cstddef>
#include <cstdint>

struct SDL_AudioStream;

namespace sh::fmv {

// Shows one frame (packed RGB24 or RGBA32, top row first) as the next picture of the window.
void show_frame(const std::uint8_t* pixels, int width, int height, bool rgba);

// The movie's sound device. It opens paused, so sound queued before the first picture waits for start().
class SoundOutput {
public:
    SoundOutput() = default;
    ~SoundOutput() { close(); }
    SoundOutput(const SoundOutput&) = delete;
    SoundOutput& operator=(const SoundOutput&) = delete;

    bool open(int sample_rate, int channels, SampleFormat format);
    bool is_open() const { return stream_ != nullptr; }
    void start();
    void pause();
    // Queues samples at the movie volume of the moment; they are scaled in place. Any thread.
    void queue(void* samples, std::size_t bytes);
    // Stops and drops what was queued: a movie's sound ends with its picture.
    void close();

private:
    SDL_AudioStream* stream_ = nullptr;
    SampleFormat format_ = SampleFormat::S16;
};

// The skip of a movie: Enter, Escape, Space, or the pad's skip buttons. It only counts once those were seen
// released, so the press that started the movie does not end it.
class SkipWatch {
public:
    // Forgets the key presses that came before the movie.
    SkipWatch();
    // Keeps the window alive (a close request leaves the game) and tells whether the movie is to be skipped.
    bool requested();
    // Waits, for at most half a second, until the skip keys are up, so the press does not reach the next screen.
    void settle();

private:
    bool armed_ = false;
};

}  // namespace sh::fmv
