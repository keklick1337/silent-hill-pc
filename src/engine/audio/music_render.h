#pragma once

// Offline rendering of a song with its bank through the sequencer and the synthesizer, set up the way the game plays
// its music: reverb mode 1 (room) at depth 20, main volume 127, song volume 39.
//
// Every song of the game loops: control 99 = 20 marks the loop start, 99 = 30 jumps back to it. The render is the
// first pass up to the jump (`loop_end`) followed by the spill: what the voices and the reverb that were sounding at
// the jump still play while the second pass starts. A player that loops [loop_start, loop_end) and mixes the spill
// over the start of every repeat reproduces the game's endless playback; one that ignores the spill cuts the release
// tails at the loop point. A song without a loop end is rendered until it and its voices fall silent.

#include "engine/audio/song.h"
#include "engine/audio/sound_bank.h"
#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sh::audio {

// A volume change at a time of the song: control 7 of `channel`, or the song volume when `channel` is -1. Used to
// replay the layer fades the game made in a recorded session (tests/audio/fit_layers.py).
struct VolumeEvent {
    double seconds = 0;
    int channel = -1;
    int volume = 0;
};

struct MusicOptions {
    int song_volume = 39;
    int main_volume = 127;      // 127 << 7 in library units
    int reverb_mode = 1;        // room
    int reverb_depth = 20;      // 20 << 8
    // -1 keeps what the song sets; 0..127 holds the channel at that volume (control 7), the way the game opens a
    // music layer.
    std::array<int, 16> channel_volume{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    std::vector<VolumeEvent> events;  // sorted by time
    double max_seconds = 900.0;     // safety limit for a song that never reaches its loop end
    double max_spill_seconds = 12.0;
};

struct RenderedMusic {
    std::vector<std::int16_t> pcm;  // interleaved stereo, 44.1 kHz
    std::optional<std::uint32_t> loop_start, loop_end;  // frames
    std::uint32_t spill_frames = 0;                     // frames after loop_end
    std::uint64_t timer_ticks = 0;
    std::array<int, 16> channel_volume_at_loop{};       // control 7 of each channel when the loop starts
    std::uint32_t voices_peak = 0;                      // most voices sounding at once

    std::uint32_t frames() const { return static_cast<std::uint32_t>(pcm.size() / 2); }
};

// Holds every channel of a layer 1..7 at volume 127 (the game's layer update with every layer on and the default
// limits);
// channels the options already hold keep their volume.
void open_layers(MusicOptions& options, const std::array<std::uint8_t, 16>& layers);

Result<RenderedMusic> render_music(const Song& song, const SoundBank& bank, const MusicOptions& options = {});

}  // namespace sh::audio
