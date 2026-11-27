#pragma once

// The engine's sound system: sound effects, music and voice lines, mixed into one
// 44.1 kHz stereo stream. Everything the game hears comes from converted data in gamedata/convert/audio (banks as
// JSON + WAV, songs as JSON, voice streams as WAV); no console format is read.
//
// Threads: the game calls the commands, the output device (or, in replay, the virtual output driven by VSync) calls
// render(). One mutex covers both. What the game reads back (key status, song state, channel volumes, whether a
// voice line drained) changes only inside render(), so with the virtual output it depends only on the VBlank count.

#include "engine/audio/effects.h"
#include "engine/audio/file_player.h"
#include "engine/audio/music_player.h"
#include "engine/audio/song.h"
#include "engine/audio/sound_bank.h"
#include "engine/audio/synth.h"
#include "engine/audio/voice_stream.h"
#include "formats/common/status.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace sh::audio {

// Reads a converted file by its path under convert/ ("audio/vab/BASE.vab.json"); mods may answer first.
using ConvertedFileReader = std::function<Result<std::vector<std::uint8_t>>(std::string_view relative)>;

// One stream of a voice track: the ADPCM sectors of one (file, channel) pair, decoded to one WAV.
struct VoiceTrackStream {
    int file = 0, channel = 0;
    std::string wav;
    int channels = 2;
    std::uint32_t rate = 37800;
    std::uint32_t frames_per_sector = 2016;
    // Track sectors of the stream, in order: runs of (first, stride, count).
    struct Run {
        std::uint32_t first = 0, stride = 1, count = 0;
    };
    std::vector<Run> runs;
    // The ordinal of `sector` among the stream's sectors, or -1.
    std::int64_t ordinal(std::uint32_t sector) const;
    std::uint32_t sectors() const;
};

class AudioEngine {
public:
    AudioEngine();

    void set_reader(ConvertedFileReader reader);

    // --- Effects ---------------------------------------------------------------------------------------------
    // Loads convert/audio/vab/<name>.vab.json into a bank slot (cached by name). Music instruments go to the
    // music player as well when `slot` is the music slot.
    Result<void> load_bank(int slot, std::string_view name);
    void close_bank(int slot);
    // The bank in a slot ("MEP000"; empty when none) and whether bank `name` keys a tone for (program, note).
    std::string bank_name(int slot);
    bool bank_has_note(std::string_view name, int program, int note);
    int key_on_program(int slot, int program, int note, int fine, int left, int right);
    int key_on_tone(int slot, int program, int tone, int note, int fine, int left, int right);
    int key_on_voice(int voice, int slot, int program, int tone, int note, int fine, int left, int right);
    void key_off_program(int slot, int program, int note, bool cut);
    int key_off_voice(int voice, bool cut);
    KeyStatus key_status(int voice);
    std::uint16_t voice_pitch(int voice);
    void set_voice_pitch(int voice, std::uint16_t pitch);
    void set_voice_volume(int voice, int left, int right);
    std::uint32_t take_released_voices();
    void set_mono(bool mono);
    void set_main_exempt(bool on);

    // Own files of the sound registry (sound_registry.h): `path` is a WAV on disk (cached by path), `tag` the
    // sound id, volumes in the units of the key-on calls (0..0x3FFF), `semitones` on top of the file's rate.
    // False when the file cannot be read.
    bool play_file(const std::filesystem::path& path, int tag, int left, int right, float semitones, bool loop);
    void stop_file(int tag);
    void set_file_volume(int tag, int left, int right);
    // FilePlayer::fade with the time in seconds. True when a voice of `tag` plays.
    bool fade_file(int tag, float level, float seconds, bool stop_at_end);
    std::uint64_t files_started() const { return files_started_; }
    std::vector<FilePlayer::VoiceInfo> file_voices();

    // --- Music -----------------------------------------------------------------------------------------------
    void set_music_slot(int slot) { music_slot_ = slot; }
    // convert/audio/seq/<name>.kdt.json, e.g. "SND/B".
    Result<void> open_song(std::string_view name);
    void song_play();
    void song_stop();
    void song_close();
    void set_song_volume(int left, int right);
    void set_channel_volume(int channel, int volume);
    int channel_volume(int channel);

    // --- Whole mix -------------------------------------------------------------------------------------------
    void set_main_volume(int left, int right);    // 0..127
    void set_reverb_depth(int left, int right);   // 0..127
    void set_voice_gain(float gain);

    // --- Voice lines -----------------------------------------------------------------------------------------
    // The line that starts at `sector` of voice track `track` ("XA/05_02152") and lasts `sectors` sectors of its
    // stream; the stream is the one that holds `sector`. False when there is no such audio.
    bool voice_start(std::string_view track, std::uint32_t sector, std::uint32_t length_vblanks);
    // A loose file standing in for a line or played on its own: interleaved frames.
    void voice_start_pcm(std::shared_ptr<const std::vector<std::int16_t>> pcm, int channels, std::uint32_t rate,
                         bool side);
    void voice_stop(bool side);
    bool voice_active(bool side);
    bool voice_drained(bool side);
    void voice_set_paused(bool paused);
    // The CD input volume, library units (0..0x7FFF = unity), and the CD mix switch.
    void set_voice_volume(int left, int right);
    void set_voice_mix(bool on);
    // Rate and channels of the last voice_start, for the caller's lip sync and timing.
    std::uint32_t voice_rate() const { return voice_rate_; }
    int voice_channels() const { return voice_channels_; }
    std::shared_ptr<const std::vector<std::int16_t>> voice_pcm() const { return voice_pcm_; }
    std::uint32_t voice_first() const { return voice_first_; }
    std::uint32_t voice_frames() const { return voice_frames_; }

    // Interleaved stereo 16-bit frames at 44.1 kHz.
    void render(std::int16_t* out, std::uint32_t frames);

private:
    Result<std::shared_ptr<const SoundBank>> bank_by_name(std::string_view name);
    Result<std::vector<VoiceTrackStream>*> track_by_name(std::string_view track);
    std::shared_ptr<const std::vector<std::int16_t>> stream_pcm(const std::string& wav, int& channels,
                                                               std::uint32_t& rate);
    void timer_tick();
    void mix_block(std::int16_t* out, std::uint32_t frames);

    std::mutex mutex_;
    ConvertedFileReader reader_;

    Synth effects_synth_;
    Effects effects_;
    MusicPlayer music_;
    VoiceStream voice_, side_;
    FilePlayer files_;
    struct FilePcm {
        std::shared_ptr<const std::vector<std::int16_t>> pcm;
        int channels = 1;
        std::uint32_t rate = 44100;
    };
    std::map<std::string, FilePcm> file_cache_;
    std::uint64_t files_started_ = 0;
    int music_slot_ = 3;

    std::map<std::string, std::shared_ptr<const SoundBank>> banks_;
    std::array<std::string, kBankSlots> slot_names_{};
    std::map<std::string, std::vector<VoiceTrackStream>> tracks_;
    std::map<std::string, std::shared_ptr<const std::vector<std::int16_t>>> stream_cache_;
    std::vector<std::string> stream_order_;  // least recent first

    std::shared_ptr<const std::vector<std::int16_t>> voice_pcm_;
    std::uint32_t voice_rate_ = 37800, voice_first_ = 0, voice_frames_ = 0;
    int voice_channels_ = 2;

    std::int16_t main_left_ = 0x3FFF, main_right_ = 0x3FFF;  // library units
    std::int32_t cd_left_ = 0, cd_right_ = 0;
    bool cd_mix_ = false;
    float effects_gain_ = 1.0f, music_gain_ = 1.0f, voice_gain_ = 1.0f;

    std::uint64_t timer_phase_ = 0;
    int timer_slow_ = 0;
    std::vector<std::int16_t> fx_scratch_, fx_side_;
    std::vector<std::int32_t> left_, right_;
};

// The engine of the running game.
AudioEngine& audio_engine();

}  // namespace sh::audio
