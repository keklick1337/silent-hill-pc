// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/fmv/movie_output.h"

#include "media/fmv/fmv_present.h"
#include "media/fmv/movie_player.h"

#include "engine/app/command_script.h"
#include "engine/debug/dbg_trace.h"
#include "engine/log/sh_log.h"
#include "engine/platform/window.h"
#include "input/pad.h"

#include <SDL3/SDL.h>

// The -consolecmd script's step at a present (engine/app/command_script.h): a `shot` that falls due while a movie
// holds the loop captures the movie frame just shown.

namespace sh::fmv {

namespace {

bool skip_keys_down() {
    SDL_PumpEvents();
    const bool* keys = SDL_GetKeyboardState(nullptr);
    return keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_SPACE] || input::skip_held();
}

void forget_key_events() {
    SDL_PumpEvents();
    SDL_FlushEvent(SDL_EVENT_KEY_DOWN);
    SDL_FlushEvent(SDL_EVENT_KEY_UP);
}

}  // namespace

void show_frame(const std::uint8_t* pixels, int width, int height, bool rgba) {
    DbgTrace_FmvFrame();
    present_frame(pixels, width, height, rgba);
    sh::app::command_script_present();
}

bool SoundOutput::open(int sample_rate, int channels, SampleFormat format) {
    close();
    // The game's own sound goes through OpenAL, so SDL's audio may not be up yet.
    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0 && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SH_LOG_DEBUG(FMV, "[FMV] SDL audio did not start: %s", SDL_GetError());
        return false;
    }
    SDL_AudioSpec spec{};
    spec.freq = sample_rate;
    spec.channels = channels;
    spec.format = format == SampleFormat::F32 ? SDL_AUDIO_F32LE : format == SampleFormat::U8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream_ == nullptr) {
        SH_LOG_DEBUG(FMV, "[FMV] no sound device for the movie: %s", SDL_GetError());
        return false;
    }
    format_ = format;
    return true;
}

void SoundOutput::start() {
    if (stream_ != nullptr) SDL_ResumeAudioStreamDevice(stream_);
}

void SoundOutput::pause() {
    if (stream_ != nullptr) SDL_PauseAudioStreamDevice(stream_);
}

void SoundOutput::queue(void* samples, std::size_t bytes) {
    if (stream_ == nullptr || bytes == 0) return;
    const MovieSound& sound = movie_sound();
    apply_gain(samples, bytes, format_, movie_gain(sound.volume, sound.console_level));
    SDL_PutAudioStreamData(stream_, samples, static_cast<int>(bytes));
}

void SoundOutput::close() {
    if (stream_ == nullptr) return;
    SDL_PauseAudioStreamDevice(stream_);
    SDL_ClearAudioStream(stream_);
    // Also frees the device: left open, one per movie, they run out.
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
}

SkipWatch::SkipWatch() { forget_key_events(); }

bool SkipWatch::requested() {
    SDL_PumpEvents();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            SH_LOG_DEBUG(FMV, "[FMV] the window was closed during a movie: leaving the game");
            platform::exit_game();
        }
    }
    const bool down = skip_keys_down();
    if (!armed_) {
        armed_ = !down;
        return false;
    }
    return down;
}

void SkipWatch::settle() {
    for (int tries = 0; tries < 30 && skip_keys_down(); ++tries) SDL_Delay(16);
    forget_key_events();
}

}  // namespace sh::fmv
