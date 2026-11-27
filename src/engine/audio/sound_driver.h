#pragma once

// The game's sound driver: the command bus the game talks to
// (one number per request: bank and song loads, effects, music commands, voice lines), the queue
// of driver tasks that runs one step per frame, the volumes of the three buses (effects, music,
// voice lines) and their fades, which effect each of the 24 voices holds, and the step-by-step
// bank and song loads that keep the pacing of a disc read.
//
// Everything the driver touches outside itself goes through DriverHost: the mixer of the
// engine (AudioEngine through sound_api.h), the disc drive, the voice-line player and the
// game's hooks. The game supplies its tables (DriverTables): effects, bank and song files,
// voice lines, music layers, reverb of the ambient banks.
//
// Bit-exactness contract: given the same host answers, the driver makes the same host calls
// with the same arguments, in the same order, and reports the same state as the original driver.

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace sh::audio {

// One sound effect of the game's table (ids from DriverTables::effect_base on).
struct EffectEntry {
    std::uint8_t tone = 0;     // tone of the program, for the calls that key one tone
    std::uint8_t slot = 0;     // bank slot (0 base, 1 weapon, 2 ambient, 3 music)
    std::uint8_t program = 0;  // program in the bank
    std::uint8_t note = 0;
    std::int8_t volume = 0;    // added to the effects volume before the call's attenuation
};

// A bank or song file on the disc.
struct DiscFile {
    std::uint8_t slot = 0;          // bank slot it loads into; 0 for songs
    std::uint16_t header_size = 0;  // bytes of the bank header (kept in the slot's header buffer)
    std::uint32_t size = 0;
    std::int32_t sector = 0;
};

// A voice line: an audio channel of one of the interleaved voice files.
struct VoiceLine {
    std::uint8_t file = 0;       // 0: no line
    std::uint32_t sector = 0;    // first sector inside the file
    std::uint8_t channel = 0;
    std::uint32_t length = 0;    // vertical blanks
    std::uint8_t file_number = 0;
};

// A voice line that plays at its own volume instead of the default.
struct LineVolume {
    std::uint16_t line = 0;
    std::uint8_t volume = 0;
    bool scaled = false;  // scaled by the effects volume of the options
};

// An effect that always keys a fixed voice (the radio loops) instead of a free one.
struct FixedVoiceEffect {
    std::uint16_t effect = 0;
    std::uint8_t voice = 0;
    std::uint8_t fine = 0;
};

struct DriverTables {
    std::uint16_t effect_base = 0;               // id of effects[0]; the id itself plays nothing
    std::vector<EffectEntry> effects;
    std::vector<DiscFile> files;                 // command (first_bank_command + i) loads files[i]
    std::vector<std::uint8_t> ambient_reverb;    // reverb depth of the ambient bank commands
    std::vector<std::array<std::uint8_t, 16>> layer_channels;  // music: layer of each channel by song row
    std::vector<VoiceLine> lines;
    std::vector<LineVolume> line_volumes;
    std::uint8_t default_line_volume = 0;
    std::vector<FixedVoiceEffect> fixed_voices;
    std::uint8_t first_bank_command = 0;         // bank load commands: first .. first + files - 1 (to 244)
    std::uint8_t first_ambient_command = 0;      // ambient banks: their own reverb depth
    std::uint8_t last_ambient_command = 0;
    std::uint8_t song_file_offset = 0;           // song command c loads files[song_file_offset + c]
    std::uint8_t song_bank_offset = 0;           // song command c also loads bank command c + offset
    std::uint8_t music_slot = 0;                 // bank slot of the music instruments
    std::uint8_t music_volume = 0;               // music bus volume while the song plays
    std::uint8_t music_ducked_volume = 0;        // ... under a voice line
    std::uint8_t default_reverb = 0;
};

enum class DiscCommand : std::uint8_t { SetMode, SetFilter, SetLocation, SeekAudio, Pause };

// Disc read modes the driver asks for.
enum class DiscMode : std::uint8_t { Data, VoiceStream };

enum class ReadTarget : std::uint8_t { BankHeader, Song };

// Effect requests the game's observer may swallow.
enum class EffectRequest : std::uint8_t { Play = 0, Update = 1 };

class DriverHost {
public:
    virtual ~DriverHost() = default;

    // Mixer.
    virtual void set_mono(bool mono) = 0;
    virtual void set_music_slot(int slot) = 0;
    virtual void set_reverb_depth(int depth) = 0;
    virtual void set_line_mix(bool on) = 0;
    virtual void set_main_volume(int volume) = 0;
    virtual void set_song_volume(int left, int right) = 0;
    virtual void set_line_volume(int left, int right) = 0;
    virtual void song_play() = 0;
    virtual void song_stop() = 0;
    virtual void song_close() = 0;
    virtual void close_bank(int slot) = 0;
    virtual int key_on_program(int slot, int program, int note, int fine, int left, int right) = 0;
    virtual int key_on_tone(int slot, int program, int tone, int note, int fine, int left, int right) = 0;
    virtual int key_on_voice(int voice, int slot, int program, int tone, int note, int fine, int left, int right) = 0;
    virtual void key_off_program(int slot, int program, int note) = 0;
    virtual void key_off_voice(int voice, bool cut) = 0;
    virtual int key_status(int voice) = 0;  // 0: off
    virtual int voice_pitch(int voice) = 0;
    virtual void set_voice_pitch(int voice, int pitch) = 0;
    virtual void set_voice_volume(int voice, int left, int right) = 0;
    virtual std::uint32_t take_released_voices() = 0;
    virtual int channel_volume(int channel) = 0;
    virtual void set_channel_volume(int channel, int volume) = 0;

    // Banks and songs come from their converted form, by the disc sector they start at.
    virtual void load_bank(int slot, std::int32_t sector) = 0;
    virtual bool open_song(std::int32_t sector) = 0;

    // Voice-line player.
    virtual void line_play(std::uint16_t line) = 0;
    virtual void line_stop() = 0;  // may call SoundDriver::line_finished
    virtual void line_player_volume(int left, int right) = 0;
    virtual void line_seek_play(std::uint16_t line, int volume) = 0;
    virtual void line_seek_stop() = 0;
    virtual std::uint32_t voice_file_sector(int file) = 0;  // first sector of a voice file

    // Disc drive.
    virtual void disc_mix(bool stereo) = 0;
    virtual bool disc_ready() = 0;
    virtual bool disc_command(DiscCommand command, const std::uint8_t* params) = 0;
    virtual void disc_read(int sectors, ReadTarget target) = 0;
    virtual int disc_read_remaining() = 0;  // 0: the read is done
    virtual void disc_reset(bool wait) = 0;
    virtual void store_read(ReadTarget target, int slot, std::uint32_t bytes) = 0;
    virtual std::uint32_t vblank_count() = 0;
    virtual bool disc_paced() = 0;  // reads at the console's pace: one task step a frame

    // Game hooks.
    virtual bool effect_is_file(std::uint16_t id) = 0;
    virtual void effect_file_play(std::uint16_t id, int left, int right, int cents) = 0;
    virtual void effect_file_volume(std::uint16_t id, int left, int right) = 0;
    virtual void effect_file_stop(std::uint16_t id) = 0;
    virtual bool effect_swallowed(EffectRequest request, std::uint16_t id, int balance, int volume, int pitch) = 0;
    // The bank slot effect `id` keys: its table slot, or the guest bank of a character spawned off its maps
    // (guest_banks.h).
    virtual int effect_slot(std::uint16_t id, int slot) {
        (void)id;
        return slot;
    }
};

// The state the game reads.
struct DriverState {
    std::array<std::uint8_t, 32> tasks{};
    std::uint8_t current_task = 0;
    std::array<std::uint16_t, 24> voice_effect{};
    std::array<std::uint16_t, 24> voice_pitch{};
    std::uint16_t disc_errors = 0;
    std::uint16_t line_requested = 0;
    std::uint16_t line_playing = 0;
    std::uint16_t song = 0;
    std::array<std::uint16_t, 3> last_bank{};
    std::uint16_t music_channels = 0;          // song row of the playing music (0xFFFF: stopping)
    std::uint16_t music_channels_pending = 0;
    bool stereo = false;
    std::uint8_t music_fade = 0;
    bool loading = false;
    bool line_idle = false;
    bool muted = false;
    std::uint8_t load_step = 0;
    std::uint8_t line_seek_step = 0;
    std::int16_t line_volume = 0;
    std::uint16_t effects_volume = 0;
    std::int16_t music_target = 0;
    std::int16_t music_volume = 0;
    std::int16_t main_volume = 0;
    std::uint8_t options_effects = 0;
    std::uint8_t options_music = 0;
    std::uint8_t options_lines = 0;
    std::uint8_t reverb_depth = 0;
    std::int32_t line_limit = 0;
    std::int32_t line_elapsed = 0;
    std::uint32_t line_started = 0;
    bool line_pending = false;
    std::uint8_t load_slot = 0;
    std::uint32_t load_moved = 0;
    std::uint8_t load_attempts = 0;
    std::int32_t bank_file = -1;
    std::int32_t song_file = -1;
    std::uint16_t seek_line = 0;
    std::array<std::uint8_t, 2> disc_params{};
    std::array<std::uint8_t, 4> seek_position{};
};

class SoundDriver {
public:
    // Commands below 256 (SD bus): mono/stereo, mute, stops, fades, bank and song loads.
    enum Command : std::uint16_t {
        kMono = 1, kStereo = 2, kMute = 3, kUnmute = 4,
        kStopEffects = 16, kStopEffectsCut = 17, kStopMusic = 18, kStopLine = 19,
        kStopAll = 20, kStopAllCut = 21, kFadeSlow = 22, kFadeFast = 23,
    };

    SoundDriver(DriverTables tables, DriverHost& host) : t_(std::move(tables)), host_(&host) {}

    void set_host(DriverHost& host) { host_ = &host; }
    const DriverTables& tables() const { return t_; }

    // Start-up and the reset of the work state.
    void init();
    void reset();

    void call(std::uint32_t command);
    void branch(std::uint16_t command);

    // 0 idle, 1 line playing, 2 loading, 3 line being positioned, 4 line queued, 5 other task.
    std::uint8_t streaming_state();
    std::uint16_t music_channels() const { return s_.music_channels; }

    // Effects. Balance -128..127 (negative: left), volume 0 loud .. 255 silent.
    int play_effect(std::uint16_t id, int balance, std::uint8_t volume);
    void update_effect(std::uint16_t id, int balance, std::uint8_t volume, int pitch);
    void play_step(std::uint16_t id, int balance, std::uint8_t volume, int pitch);
    void stop_effect(std::uint16_t id);
    void stop_all_effects(bool cut);
    void stop_last_voice();
    bool effect_has_voice(std::uint16_t id);

    std::uint8_t layer_volume(std::uint8_t layer);
    void set_layer_volume(std::uint8_t layer, std::uint8_t volume);

    void preload_line(std::uint16_t line);
    void set_option_volumes(std::uint8_t lines, std::uint8_t music, std::uint8_t effects);
    void set_reverb_depth(std::uint8_t depth);

    // One frame of the task queue, and the extra steps of instant loads.
    void run_tasks();
    void drain_tasks();

    // The voice-line player reports the end of a line (or its stop).
    void line_finished();
    // The level the CD input gets for a line the tables give no volume of its own, with the voice option applied:
    // what a mod's voice file plays at, as the game's lines do.
    int default_line_level() const;
    std::uint16_t line_playing() const { return s_.line_playing; }

    const DriverState& state() const { return s_; }

private:
    void set_stereo(bool stereo);
    void queue_command(std::uint16_t command);
    void start_music();
    void queue_music_stop();
    void step_music_stop();
    void stop_music_now();
    void queue_line(std::uint16_t command);
    void queue_line_stop();
    void step_line_seek();
    void set_music_bus(int left, int right);
    void set_line_bus(int left, int right);
    int scaled_effect_volume(int volume) const;
    int line_volume_of(std::uint16_t line) const;
    void add_task(std::uint8_t task);
    void pop_task();
    void queue_bank(std::uint16_t command);
    void queue_song(std::uint16_t command);
    void step_bank_load();
    void step_song_load();
    bool disc_try(DiscCommand command, const std::uint8_t* params);
    void reset_disc_steps();
    void forget_released_voices();
    void play_file_effect(std::uint16_t id, int balance, std::uint8_t volume, int cents);
    const EffectEntry* effect(std::uint16_t id) const;
    const FixedVoiceEffect* fixed_voice(std::uint16_t id) const;
    const VoiceLine* line(std::uint16_t index) const;
    void seek_to(std::uint32_t sector);

    DriverTables t_;
    DriverHost* host_;
    DriverState s_;
};

}  // namespace sh::audio
