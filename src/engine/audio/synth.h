#pragma once

// The engine's 24-voice sample synthesizer. Its voices behave like the console's sound chip as psx-spx "Sound
// Processing Unit" describes it (envelope and reverb timing as measured by Mednafen/DuckStation), so that the game's
// sounds and music keep their character, but they play decoded samples (sound_bank.h) rather than sound memory.
//
// What it models: the pitch counter (0x1000 = one sample frame per output frame at 44.1 kHz, steps above 0x3FFF
// clipped to 0x4000), 4-point Gaussian interpolation, the ADSR generator (rates, exponential modes, the frozen rate
// 0x7F), fixed voice and main volumes, and the reverb unit at 22.05 kHz with its 39-tap resampling filter and its
// own work memory. Left out because the game does not use them: volume sweeps, pitch modulation, noise.
//
// Key status follows the sound library's bookkeeping, which voice allocation depends on: a "keyed" bit per voice
// (set by key on, cleared by key off) combined with the envelope level.

#include "engine/audio/sound_bank.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sh::audio {

inline constexpr int kVoices = 24;
inline constexpr std::uint32_t kOutputRate = 44100;

// Off, On, OffEnvOn, OnEnvOff: the four answers of the library's key status query.
enum class KeyStatus : int { Off = 0, On = 1, OffEnvOn = 2, OnEnvOff = 3 };

// The ten reverb modes: 32 register values (in port order) and the size of the work area.
struct ReverbPreset {
    const char* name;
    std::uint32_t work_bytes;
    std::array<std::uint16_t, 32> regs;
};
inline constexpr int kReverbModes = 10;
const ReverbPreset& reverb_preset(int mode);

// The 512-entry interpolation table of the hardware.
std::span<const std::int16_t, 512> gauss_table();

class Synth {
public:
    Synth();

    // Silent voices, main volume 0, reverb off and cleared.
    void reset();

    // The sample the next key on of `voice` plays.
    void set_sample(int voice, std::shared_ptr<const Sample> sample);
    void set_pitch(int voice, std::uint16_t pitch);
    // Fixed-mode volumes in library units: -0x4000..0x3FFF, clamped; the level is twice that.
    void set_volume(int voice, int left, int right);
    void set_adsr(int voice, std::uint16_t adsr1, std::uint16_t adsr2);
    void key_on(std::uint32_t voice_mask);
    void key_off(std::uint32_t voice_mask);
    void set_reverb_voices(std::uint32_t voice_mask, bool on);

    KeyStatus key_status(int voice) const;
    std::int16_t envelope(int voice) const;
    std::uint16_t pitch(int voice) const;
    int volume_left(int voice) const;   // library units, as set
    int volume_right(int voice) const;

    // Main volume in library units: -0x4000..0x3FFF.
    void set_main_volume(int left, int right);

    // Registers and work area of a preset; the work area is cleared.
    void set_reverb_mode(int mode);
    // The reverb output volume, signed 16-bit.
    void set_reverb_depth(std::int16_t left, std::int16_t right);
    // Off stops the work-area writes; the unit still reads and outputs.
    void set_reverb_enabled(bool on);

    // Voices keyed on while `tag` is current carry it; render() mixes only voices whose tag is in `mix_tags`, and
    // feeds only those to the reverb. Used to render the spill of one pass of a loop into the next (music_render).
    void set_key_on_tag(std::uint8_t tag) { key_on_tag_ = tag; }
    std::uint8_t voice_tag(int voice) const;
    bool voice_active(int voice) const;  // the envelope is not off

    // Renders interleaved stereo 16-bit frames at 44.1 kHz.
    void render(std::int16_t* out, std::size_t frames, std::uint32_t mix_tags = ~0u);
    // The same with two outputs: voices tagged 0 (and the reverb) into `main`, the others dry into `side`, both
    // after the main volume.
    void render_buses(std::int16_t* main, std::int16_t* side, std::size_t frames);

private:
    enum class Phase : std::uint8_t { Off, Attack, Decay, Sustain, Release };

    struct Envelope {
        std::uint32_t counter = 0;
        std::uint32_t increment = 0;
        std::int32_t step = 0;
        std::uint8_t rate = 0;
        bool decreasing = false;
        bool exponential = false;

        void reset(std::uint8_t rate, std::uint8_t rate_mask, bool decreasing, bool exponential);
        void tick(std::int16_t& level);
    };

    struct Voice {
        std::shared_ptr<const Sample> next;     // set_sample
        std::shared_ptr<const Sample> playing;  // the sample of the last key on
        std::uint32_t step_scale = 0x10000;     // 16.16: sample rate / 44100
        std::uint16_t pitch = 0;
        std::int16_t volume_left = 0, volume_right = 0;  // levels: twice the library value
        std::uint16_t adsr1 = 0, adsr2 = 0;
        Phase phase = Phase::Off;
        std::int16_t level = 0;
        std::int16_t target = 0;
        Envelope envelope;
        std::array<std::int16_t, 4> history{};  // the last four sample frames, oldest first
        std::uint32_t position = 0;             // the next frame of the sample to enter the history
        std::uint32_t counter = 0;              // fraction of a frame, 12 bits
        bool ended = false;                     // the sample ran out with no loop
        bool keyed = false;
        bool reverb = false;
        std::uint8_t tag = 0;
    };

    void update_envelope(Voice& v);
    void tick_envelope(Voice& v);
    void advance(Voice& v);
    std::int32_t sample_voice(Voice& v);
    void reverb_tick(std::int32_t in_left, std::int32_t in_right, std::int32_t& out_left, std::int32_t& out_right);
    std::int16_t reverb_read(std::uint32_t reg, std::int32_t extra) const;
    void reverb_write(std::uint32_t reg, std::int16_t value);

    std::array<Voice, kVoices> voices_{};
    std::int16_t main_left_ = 0, main_right_ = 0;
    std::uint8_t key_on_tag_ = 0;

    // Reverb unit. The work area is the end of a 256 Ki-halfword memory, as on the console.
    std::vector<std::int16_t> rev_mem_;
    std::array<std::uint16_t, 32> rev_{};
    std::int16_t rev_out_left_ = 0, rev_out_right_ = 0;
    bool rev_enabled_ = false;
    std::uint32_t rev_base_ = 0;     // halfword address
    std::uint32_t rev_current_ = 0;  // halfword address
    std::array<std::array<std::int16_t, 128>, 2> rev_down_{};
    std::array<std::array<std::int16_t, 64>, 2> rev_up_{};
    std::uint32_t rev_phase_ = 0;
};

}  // namespace sh::audio
