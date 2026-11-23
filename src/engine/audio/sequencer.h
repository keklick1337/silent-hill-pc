#pragma once

// The game's music sequencer, following the behaviour of the console sound driver the game shipped with: it plays a
// song (song.h), drives the voices of a Synth with the instruments of a sound bank, and keeps the driver's voice
// allocation, volume, pan, pitch and envelope rules, its controllers and its loop markers.
//
// Timing: libsd runs on root counter 2 at 33.8688 MHz / 8 / 7328 (about 577.7 Hz). Every call of timer_tick() is one
// of those interrupts: the song advances by tempo / 256 ticks of 1/120 quarter note, and every 11th call runs the
// slow controllers (vibrato, tremolo, modulation, portamento, pitch bend) and the automatic key off.
//
// One song (MIDI channels 0..15), which is how the game plays its music; sound effects are not part of it
// (effects.h).

#include "engine/audio/song.h"
#include "engine/audio/sound_bank.h"
#include "engine/audio/synth.h"
#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace sh::audio {

// Root counter 2 rate as a fraction: 4233600 / 7328 interrupts per second.
inline constexpr std::uint32_t kTimerNumerator = 33868800 / 8;
inline constexpr std::uint32_t kTimerDenominator = 7328;

// The voice pitch of `note` + `cent`/128 semitones for a sample recorded at `sample_note` + fine tune.
std::uint16_t note_to_pitch(int note, int cent, int sample_note, int sample_cent);

class Sequencer {
public:
    explicit Sequencer(Synth& synth);

    // The instruments of the songs that follow. The sequencer keeps its own copy: songs edit tones.
    void open_bank(const SoundBank& bank);
    // Starts the song from the top.
    Result<void> play(const Song& song);
    void stop();

    // Song volume, 0..127 (the game's music runs at 39: volume 40 with the music option at 127).
    void set_volume(int left, int right);
    // Control change 7 on one channel, which is how the game fades music layers in and out.
    void set_channel_volume(int channel, int volume);
    int channel_volume(int channel) const;

    // One root-counter interrupt.
    void timer_tick();

    bool playing() const;       // some track has not reached its end
    std::uint32_t beat() const;  // ticks counted on track 1
    // Loop markers met so far: control 99 = 20 saves every track (start), 99 = 30 jumps back (end).
    std::uint32_t loop_starts() const { return loop_starts_; }
    std::uint32_t loop_ends() const { return loop_ends_; }

private:
    struct Track {
        std::size_t steps = 0;  // track number in the song
        std::uint32_t pos = 0, loop_pos = 0;
        std::uint16_t tempo_acc = 114, tempo = 114;
        std::uint16_t rest = 0, rest_loop = 0;        // time_hosei
        std::uint16_t delta = 0, delta_loop = 0;
        bool eof = true, eof_loop = false;
        std::uint8_t loop_count = 0;
        std::uint8_t status = 0, status_loop = 0;     // last key
        std::uint8_t channel = 0;
    };

    struct Channel {
        std::uint8_t program = 0, pan = 64, mod = 0, volume = 127, velocity = 0, expression = 127, pedal = 0;
        std::uint8_t bend = 64, bend_mode = 0, vol_mode = 0, mode = 0, before_note = 60;
        std::uint16_t mod_up = 0, mod_speed = 0;
        std::int16_t mod_add = 0, mod_depth = 0, mod_limit = 0;
        std::uint8_t mod_mode = 0, wide = 0;
        std::uint16_t key_pan = 0;
        std::uint8_t rev_depth = 0, nrpn_lsb = 0, nrpn_msb = 0, data_entry = 0;
        std::int16_t porta = 0, porta_depth = 0, porta_add = 0;
        std::uint16_t porta_limit = 0, porta_up = 0;
        std::uint8_t vib_wait = 0, vib_count = 0, vib_speed = 0;
        std::uint16_t vib_depth_max = 0, vib_add = 0;
        std::uint8_t tre_wait = 0, tre_count = 0, tre_speed = 0;
        std::uint16_t tre_depth_max = 0, tre_add = 0;
        std::uint8_t bank = 127;
    };

    struct Port {
        std::uint8_t voice = 0, program = 0, channel = 0;
        std::uint16_t tone = 0, note = 0, note_start = 0;
        std::uint16_t left = 0, right = 0;  // l_vol, r_vol before the song volume
        std::uint8_t prog_volume = 0, tone_volume = 0, prog_pan = 0, tone_pan = 0;
        std::uint16_t pan = 64, stat = 0;
        std::uint8_t velocity = 0, pedal = 0, bend_max = 0, bend_min = 0, center = 0, shift = 0;
        // Vibrato and tremolo of the voice, copied from the channel at key on.
        std::uint8_t vib_wait_count = 0;
        std::int8_t vib_phase = 0;
        std::uint16_t vib_depth = 0, vib_depth_max = 0;
        std::uint8_t vib_steps = 0, vib_wait = 0, vib_count = 0, vib_speed = 0;
        std::uint16_t vib_add = 0, vib_data = 0;
        std::uint8_t tre_wait_count = 0;
        std::int8_t tre_phase = 0;
        std::uint16_t tre_depth = 0, tre_depth_max = 0;
        std::uint8_t tre_steps = 0, tre_wait = 0, tre_count = 0, tre_speed = 0;
        std::uint16_t tre_add = 0;
        std::int16_t tre_data = 0, tre_written = 0;
        std::uint16_t adsr1 = 0, adsr2 = 0;
        std::uint16_t bend_seen = 64;
    };

    const SongStep* next_step(Track& t);
    std::uint32_t read_delta(Track& t);
    void convert_delta(Track& t);
    bool read_event(Track& t);  // true when no delta time follows
    void channel_message(Track& t, std::uint8_t controller, std::uint8_t value);
    void song_tick();
    void slow_tick();            // vibrato, tremolo, modulation, portamento, pitch bend
    void auto_key_off();
    void sound_off();

    void key_on(int channel, int key, int velocity);
    void key_off(int channel, int key);
    void control_change(int channel, int controller, int value);
    void data_entry(int channel);
    int voice_check(int channel, int note, bool only_free) const;

    void volume_calc(Port& p, const Channel& m);
    void set_voice_volume(int voice, int channel, int left, int right);
    void pitch_calc(Port& p, bool force);
    int pitch_bend(const Port& p, int bend) const;
    void vibrato(Port& p);
    void tremolo(Port& p);
    void release_voice(int voice);  // fastest release, then key off

    Synth& spu_;
    SoundBank bank_;
    Song song_;
    std::vector<Track> tracks_;
    std::array<Channel, 16> channels_{};
    std::array<Port, kVoices> ports_{};
    bool playing_ = false;
    std::uint16_t division_ = 480;
    int volume_left_ = 127, volume_right_ = 127;
    bool volume_dirty_ = false;
    std::uint16_t master_volume_ = 127;  // control 30
    std::uint8_t loop_flag_ = 0;
    std::uint32_t beat_ = 0, beat_loop_ = 0;
    std::uint8_t beat_stat_ = 16, control_stat_ = 0;
    int timer_sync_ = 0;
    std::uint32_t loop_starts_ = 0, loop_ends_ = 0;
};

}  // namespace sh::audio
