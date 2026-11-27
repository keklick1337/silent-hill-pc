#include "engine/audio/sound_driver.h"

#include <cstdlib>

namespace sh::audio {

namespace {

constexpr int kVoices = 24;
constexpr int kTaskSlots = 32;
constexpr int kLastVoice = kVoices - 1;

// Task numbers of the queue. Bank and song loads use their command number as the task.
constexpr std::uint8_t kTaskPlayLine = 1;
constexpr std::uint8_t kTaskStopLine = 2;
constexpr std::uint8_t kTaskSeekLine = 6;
constexpr std::uint8_t kTaskStartMusic = 7;
constexpr std::uint8_t kTaskStopMusic = 8;

constexpr std::uint16_t kFirstSongCommand = 32;
constexpr std::uint16_t kSongCommandEnd = 72;
constexpr std::uint16_t kBankCommandEnd = 245;
constexpr std::uint16_t kFirstMusicChannelCommand = 0x300;
constexpr std::uint16_t kMusicStopping = 0xFFFF;
constexpr int kMusicLayerChannels = 15;

// Loads move the file in pieces of this many bytes (25 sectors), one piece per read.
constexpr std::uint32_t kReadPiece = 0xC800;
constexpr std::uint32_t kSectorBytes = 2048;
constexpr std::uint8_t kMaxLoadAttempts = 16;

// Steps of a bank or song load (shared: one load runs at a time).
enum LoadStep : std::uint8_t {
    kLoadStart = 0, kLoadClose = 1, kLoadLocate = 2, kLoadRead = 3, kLoadStore = 4,
    kLoadPiece = 5, kLoadLocateNext = 6, kLoadReadNext = 7, kLoadStoreNext = 8, kLoadDone = 9,
};

// Failed disc commands in a row before the drive is reset.
constexpr int kDiscErrorLimit = 600;
// A line plays this many vertical blanks past its length before the queue stops it.
constexpr int kLineGrace = 32;
// Sectors of the lead-in before the first track.
constexpr std::uint32_t kLeadIn = 150;

constexpr int kMusicStopStep = 4;
constexpr int kMuteStep = 8;
constexpr int kUnmuteStep = 4;
constexpr int kFullVolume = 127;

std::uint8_t to_bcd(std::uint32_t v) { return static_cast<std::uint8_t>(v / 10 * 16 + v % 10); }

// Minute, second, sector (BCD) and track of a disc position.
std::array<std::uint8_t, 4> disc_position(std::uint32_t sector) {
    std::array<std::uint8_t, 4> pos{};
    pos[2] = to_bcd(sector % 75);
    sector /= 75;
    pos[1] = to_bcd(sector % 60);
    sector /= 60;
    pos[0] = to_bcd(sector);
    return pos;
}

std::int16_t s16(int v) { return static_cast<std::int16_t>(v); }

// The left and right volumes of an effect: the level attenuated by the call's volume, then
// panned (in stereo only the far side loses, by the near side's level or its own).
struct Pan {
    std::int16_t left;
    std::int16_t right;
};

}  // namespace

const EffectEntry* SoundDriver::effect(std::uint16_t id) const {
    if (id < t_.effect_base) {
        return nullptr;
    }
    const std::size_t index = id - t_.effect_base;
    return index < t_.effects.size() ? &t_.effects[index] : nullptr;
}

const FixedVoiceEffect* SoundDriver::fixed_voice(std::uint16_t id) const {
    for (const FixedVoiceEffect& f : t_.fixed_voices) {
        if (f.effect == id) {
            return &f;
        }
    }
    return nullptr;
}

const VoiceLine* SoundDriver::line(std::uint16_t index) const {
    return index < t_.lines.size() ? &t_.lines[index] : nullptr;
}

int SoundDriver::scaled_effect_volume(int volume) const {
    return s16((s16(volume) * s_.options_effects) >> 7);
}

void SoundDriver::set_music_bus(int left, int right) {
    host_->set_song_volume((s16(left) * s_.options_music) >> 7, (s16(right) * s_.options_music) >> 7);
}

void SoundDriver::set_line_bus(int left, int right) {
    host_->set_line_volume(((s16(left) * s_.options_lines) >> 7) << 8, ((s16(right) * s_.options_lines) >> 7) << 8);
    host_->line_player_volume(s16(left), s16(right));
}

void SoundDriver::set_reverb_depth(std::uint8_t depth) {
    s_.reverb_depth = depth;
    host_->set_reverb_depth(depth);
}

void SoundDriver::set_stereo(bool stereo) {
    host_->set_mono(!stereo);
    host_->disc_mix(stereo);
    s_.effects_volume = kFullVolume;
    s_.stereo = stereo;
}

void SoundDriver::init() {
    host_->set_music_slot(t_.music_slot);
    set_stereo(true);
    s_.options_effects = kFullVolume;
    s_.options_music = kFullVolume;
    s_.options_lines = kFullVolume;
    reset();
}

void SoundDriver::reset() {
    s_.reverb_depth = t_.default_reverb;
    host_->set_reverb_depth(t_.default_reverb);
    host_->set_line_mix(false);
    set_line_bus(0, 0);
    s_.disc_params[0] = static_cast<std::uint8_t>(DiscMode::Data);
    disc_try(DiscCommand::SetMode, s_.disc_params.data());

    // The last task slot is left as it is, as the original does.
    for (int i = 0; i < kTaskSlots - 1; ++i) {
        s_.tasks[i] = 0;
    }
    s_.voice_effect.fill(0);
    s_.song = 0;
    s_.last_bank.fill(0);
    s_.line_playing = 0;
    s_.disc_errors = 0;
    s_.music_fade = 0;
    s_.loading = false;
    s_.line_idle = false;
    s_.muted = false;
    s_.main_volume = kFullVolume;
    host_->set_main_volume(kFullVolume);
    s_.line_pending = false;
    s_.music_channels = 0;
    s_.music_channels_pending = 0;
    s_.load_step = kLoadStart;
    s_.line_seek_step = 0;
    s_.line_volume = t_.default_line_volume;
    s_.music_volume = t_.music_volume;
    s_.music_target = t_.music_volume;
    set_music_bus(s_.music_target, s_.music_target);
}

void SoundDriver::call(std::uint32_t command) {
    switch ((command >> 8) & 0xFF) {
    case 0:
        branch(static_cast<std::uint16_t>(command));
        return;
    case 3:
    case 4:
        queue_command(static_cast<std::uint16_t>(command));
        return;
    case 5:
    case 6:
        play_effect(static_cast<std::uint16_t>(command), 0, 0);
        return;
    case 7:
    case 8:
        stop_effect(static_cast<std::uint16_t>(command - 0x200));
        return;
    case 11:
    case 12:
        stop_last_voice();
        return;
    case 16: case 17: case 18: case 19: case 20: case 21: case 22:
        queue_line(static_cast<std::uint16_t>(command));
        return;
    default:
        return;
    }
}

void SoundDriver::branch(std::uint16_t command) {
    switch (command) {
    case kMono:
        set_stereo(false);
        break;
    case kStereo:
        set_stereo(true);
        break;
    case kStopEffects:
        stop_all_effects(false);
        stop_last_voice();
        break;
    case kStopEffectsCut:
        stop_all_effects(true);
        stop_last_voice();
        break;
    case kStopMusic:
        queue_music_stop();
        break;
    case kStopAllCut:
    case kStopAll:
    case kStopLine:
        if (command == kStopAllCut) {
            stop_all_effects(true);
        }
        if (command != kStopLine) {
            stop_all_effects(false);
            stop_last_voice();
            queue_music_stop();
        }
        queue_line_stop();
        break;
    case kFadeSlow:
        s_.music_fade = 1;
        break;
    case kFadeFast:
        s_.music_fade = 2;
        break;
    case kMute:
        s_.muted = true;
        break;
    case kUnmute:
        s_.muted = false;
        break;
    default:
        break;
    }

    if (command >= t_.first_bank_command && command < kBankCommandEnd) {
        queue_bank(command);
    }
    if (command >= kFirstSongCommand && command < kSongCommandEnd) {
        queue_song(command);
    }
}

std::uint8_t SoundDriver::streaming_state() {
    if (s_.line_playing != 0) {
        return 1;
    }
    if (s_.loading) {
        return 2;
    }
    if (s_.line_seek_step != 0) {
        s_.line_started = host_->vblank_count();
        s_.line_elapsed = 0;
        return 3;
    }
    if (s_.line_pending) {
        return 4;
    }
    return s_.current_task == 0 ? 0 : 5;
}

// ---- Effects ----

void SoundDriver::forget_released_voices() {
    std::uint32_t released = host_->take_released_voices();
    for (int i = 0; released != 0 && i < kVoices; ++i, released >>= 1) {
        if (released & 1) {
            s_.voice_effect[i] = 0;
        }
    }
}

void SoundDriver::play_file_effect(std::uint16_t id, int balance, std::uint8_t volume, int cents) {
    std::int16_t level = static_cast<std::int16_t>(s_.effects_volume);
    if (const EffectEntry* e = effect(id)) {
        level = s16(level + e->volume);
    }
    level = s16(level - (level * volume) / 255);
    std::int16_t left = level;
    std::int16_t right = level;
    if (s_.stereo) {
        if (balance < 0) {
            right = s16(right - ((left * std::abs(balance)) >> 7));
        } else {
            left = s16(left - ((left * balance) >> 7));
        }
    }
    if (left < 0) {
        left = 0;
    }
    if (right < 0) {
        right = 0;
    }
    // A file's gain is in the driver's finer units (<< 7, as a voice's volume): a full level is the file as it is.
    host_->effect_file_play(id, scaled_effect_volume(s16(left << 7)), scaled_effect_volume(s16(right << 7)), cents);
}

int SoundDriver::play_effect(std::uint16_t id, int balance, std::uint8_t volume) {
    constexpr int kNone = 0xFF;
    if (id == t_.effect_base) {
        return kNone;
    }
    if (host_->effect_swallowed(EffectRequest::Play, id, balance, volume, 0)) {
        return kNone;
    }
    if (host_->effect_is_file(id)) {
        play_file_effect(id, balance, volume, 0);
        return kNone;
    }
    const EffectEntry* e = effect(id);
    if (e == nullptr) {
        return kNone;
    }

    forget_released_voices();

    std::int16_t level = s16(s_.effects_volume + e->volume);
    level = s16(level - (level * volume) / 255);
    std::int16_t left = level;
    std::int16_t right = level;
    if (s_.stereo) {
        if (balance < 0) {
            right = s16(right - ((left * std::abs(balance)) >> 7));
        } else {
            left = s16(left - ((left * balance) >> 7));
        }
    }
    if (left < 0) {
        left = 0;
    }
    if (right < 0) {
        right = 0;
    }

    std::uint8_t voice;
    if (const FixedVoiceEffect* fixed = fixed_voice(id)) {
        host_->key_on_voice(fixed->voice, host_->effect_slot(id, e->slot), e->program, e->tone, e->note, fixed->fine,
                            scaled_effect_volume(left), scaled_effect_volume(right));
        voice = fixed->voice;
    } else {
        voice = static_cast<std::uint8_t>(host_->key_on_program(
            host_->effect_slot(id, e->slot), e->program & 0x7F, e->note, 0, static_cast<std::uint16_t>(scaled_effect_volume(left)),
            static_cast<std::uint16_t>(scaled_effect_volume(right))));
    }

    for (std::uint16_t& held : s_.voice_effect) {
        if (held == id) {
            held = 0;
        }
    }

    if (voice >= kVoices) {
        return kNone;
    }
    s_.voice_effect[voice] = id;
    s_.voice_pitch[voice] = static_cast<std::uint16_t>(host_->voice_pitch(voice));
    return voice;
}

void SoundDriver::update_effect(std::uint16_t id, int balance, std::uint8_t volume, int pitch) {
    if (host_->effect_swallowed(EffectRequest::Update, id, balance, volume, pitch)) {
        return;
    }
    forget_released_voices();
    if (id == t_.effect_base) {
        return;
    }
    const EffectEntry* e = effect(id);
    if (host_->effect_is_file(id)) {
        std::int16_t level = s16(s_.effects_volume + (e != nullptr ? e->volume : 0));
        level = s16(level - (level * volume) / 255);
        if (level < 0) {
            level = 0;
        }
        host_->effect_file_volume(id, scaled_effect_volume(s16(level << 7)), scaled_effect_volume(s16(level << 7)));
        return;
    }
    if (e == nullptr) {
        return;
    }

    const std::int16_t full = s16(s_.effects_volume + e->volume);
    int voice;
    if (const FixedVoiceEffect* fixed = fixed_voice(id)) {
        voice = fixed->voice;
        // A loop on its own voice that has stopped is keyed again.
        if (host_->key_status(voice) == 0) {
            play_effect(id, 0, 0);
            return;
        }
    } else {
        voice = -1;
        for (int i = 0; i < kVoices; ++i) {
            if (s_.voice_effect[i] == id) {
                voice = i;
            }
        }
        if (voice < 0) {
            return;
        }
    }

    const std::uint16_t new_pitch = static_cast<std::uint16_t>(s_.voice_pitch[voice] + pitch * 2);
    const std::int16_t level = s16(full - (full * volume) / 255);
    std::int16_t left = level;
    std::int16_t right = level;
    if (s_.stereo) {
        if (balance < 0) {
            right = s16(right - ((level * std::abs(balance)) >> 7));
        } else {
            left = s16(left - ((level * balance) >> 7));
        }
    }
    if (left < 0) {
        left = 0;
    }
    if (right < 0) {
        right = 0;
    }
    // The voice volume is in the driver's finer units (<< 7); the shift wraps as a 16-bit value.
    host_->set_voice_volume(voice, scaled_effect_volume(s16(left << 7)), scaled_effect_volume(s16(right << 7)));
    // Zero would freeze the voice: a voice whose pitch was never recorded keeps its own.
    if (new_pitch != 0) {
        host_->set_voice_pitch(voice, new_pitch);
    }
}

void SoundDriver::play_step(std::uint16_t id, int balance, std::uint8_t volume, int pitch) {
    if (id == t_.effect_base) {
        return;
    }
    if (host_->effect_is_file(id)) {
        // The tone path shifts the note by pitch * 5 / 127 semitones.
        play_file_effect(id, balance, volume, pitch * 500 / 127);
        return;
    }
    const EffectEntry* e = effect(id);
    if (e == nullptr) {
        return;
    }

    const std::int16_t note = s16(e->note + static_cast<std::int8_t>(pitch * 5 / 127));
    const int fine = pitch > 0 ? std::abs(pitch * 5) % 127 : 127 - std::abs(pitch * 5) % 127;

    const std::int16_t full = s16(s_.effects_volume + e->volume);
    std::int16_t left = s16(full - (full * volume) / 255);
    std::int16_t right = left;
    if (s_.stereo) {
        if (balance < 0) {
            right = s16(right - ((right * std::abs(balance)) >> 7));
        } else {
            left = s16(left - ((left * balance) >> 7));
        }
    }
    if (left < 0) {
        left = 0;
    }
    if (right < 0) {
        right = 0;
    }
    host_->key_on_tone(host_->effect_slot(id, e->slot), e->program, e->tone, note, fine, scaled_effect_volume(left),
                       scaled_effect_volume(right));
}

void SoundDriver::stop_effect(std::uint16_t id) {
    if (id == t_.effect_base) {
        return;
    }
    if (host_->effect_is_file(id)) {
        host_->effect_file_stop(id);
        return;
    }
    if (const EffectEntry* e = effect(id)) {
        host_->key_off_program(host_->effect_slot(id, e->slot), e->program & 0x7F, e->note);
    }
}

void SoundDriver::stop_all_effects(bool cut) {
    for (int i = 0; i < kVoices; ++i) {
        host_->key_off_voice(i, cut);
    }
}

void SoundDriver::stop_last_voice() { host_->key_off_voice(kLastVoice, false); }

bool SoundDriver::effect_has_voice(std::uint16_t id) {
    forget_released_voices();
    for (std::uint16_t held : s_.voice_effect) {
        if (held == id) {
            return true;
        }
    }
    return false;
}

// ---- Music ----

void SoundDriver::queue_command(std::uint16_t command) {
    if (s_.music_channels_pending != command && s_.music_channels != command) {
        s_.music_channels_pending = command;
        add_task(kTaskStartMusic);
    }
}

void SoundDriver::start_music() {
    s_.music_fade = 0;
    s_.music_channels = s_.music_channels_pending;
    s_.music_channels_pending = 0;
    host_->song_play();
    s_.music_target = t_.music_volume;
    s_.music_volume = t_.music_volume;
    set_music_bus(s_.music_target, s_.music_target);
    pop_task();
}

void SoundDriver::queue_music_stop() {
    s_.music_channels = kMusicStopping;
    add_task(kTaskStopMusic);
}

void SoundDriver::step_music_stop() {
    if (s_.music_volume > 0) {
        s_.music_volume = s16(s_.music_volume - kMusicStopStep);
    }
    if (s_.music_volume <= 0) {
        s_.music_volume = 0;
        stop_music_now();
        pop_task();
    }
    s_.music_target = s_.music_volume;
    set_music_bus(s_.music_target, s_.music_target);
}

void SoundDriver::stop_music_now() {
    set_music_bus(0, 0);
    host_->song_stop();
    s_.music_fade = 0;
    s_.music_channels = 0;
}

std::uint8_t SoundDriver::layer_volume(std::uint8_t layer) {
    if (layer == 0) {
        return 0;
    }
    const std::size_t rows = t_.layer_channels.size();
    if (s_.music_channels >= kFirstMusicChannelCommand + rows) {
        return 0;
    }
    const std::size_t row = s_.music_channels & 0xFF;
    if (row >= rows) {
        return 0;
    }
    for (int i = 0; i < kMusicLayerChannels; ++i) {
        if (t_.layer_channels[row][i] == layer) {
            return static_cast<std::uint8_t>(host_->channel_volume(i));
        }
    }
    return 0;
}

void SoundDriver::set_layer_volume(std::uint8_t layer, std::uint8_t volume) {
    if (layer == 0) {
        s_.music_target = s16((volume * t_.music_volume) / 127);
        return;
    }
    const std::size_t rows = t_.layer_channels.size();
    if (s_.music_channels >= kFirstMusicChannelCommand + rows) {
        return;
    }
    const std::size_t row = s_.music_channels & 0xFF;
    if (row >= rows) {
        return;
    }
    for (int i = 0; i < kMusicLayerChannels; ++i) {
        if (t_.layer_channels[row][i] == layer) {
            host_->set_channel_volume(i, volume);
        }
    }
}

void SoundDriver::set_option_volumes(std::uint8_t lines, std::uint8_t music, std::uint8_t effects) {
    s_.options_lines = lines;
    s_.options_music = music;
    s_.options_effects = effects;
    if (s_.music_channels != 0) {
        set_music_bus(s_.music_volume, s_.music_volume);
    }
    if (s_.line_playing != 0) {
        set_line_bus(s_.line_volume, s_.line_volume);
    }
}

// ---- Voice lines ----

int SoundDriver::line_volume_of(std::uint16_t index) const {
    for (const LineVolume& v : t_.line_volumes) {
        if (v.line == index) {
            return v.scaled ? scaled_effect_volume(v.volume) : v.volume;
        }
    }
    return t_.default_line_volume;
}

void SoundDriver::queue_line(std::uint16_t command) {
    s_.line_requested = command & 0xFFF;
    const VoiceLine* l = line(s_.line_requested);
    if (l == nullptr || l->file == 0) {
        return;
    }
    s_.line_pending = true;
    s_.line_started = host_->vblank_count();
    s_.line_elapsed = 0;
    add_task(kTaskStopLine);
    s_.line_playing = s_.line_requested;
    add_task(kTaskPlayLine);
}

void SoundDriver::preload_line(std::uint16_t index) {
    s_.line_requested = index & 0xFFF;
    s_.line_pending = true;
    if (s_.line_playing != 0) {
        add_task(kTaskStopLine);
    }
    add_task(kTaskSeekLine);
}

void SoundDriver::queue_line_stop() {
    add_task(kTaskStopLine);
    s_.line_idle = true;
}

void SoundDriver::seek_to(std::uint32_t sector) {
    s_.seek_position = disc_position(sector);
}

// Positions the drive on a line so that it starts at once: mode, channel filter, seek, pause.
void SoundDriver::step_line_seek() {
    ++s_.disc_errors;
    switch (s_.line_seek_step) {
    case 0:
        s_.seek_line = s_.line_requested;
        set_line_bus(0, 0);
        s_.disc_params[0] = static_cast<std::uint8_t>(DiscMode::VoiceStream);
        s_.line_seek_step = 1;
        break;
    case 1:
        if (disc_try(DiscCommand::SetMode, s_.disc_params.data())) {
            s_.disc_errors = 0;
            s_.line_seek_step = 2;
        }
        break;
    case 2: {
        const VoiceLine* l = line(s_.seek_line);
        s_.disc_params[0] = l != nullptr ? l->file_number : 0;
        s_.disc_params[1] = l != nullptr ? l->channel : 0;
        s_.line_seek_step = 3;
        break;
    }
    case 3:
        if (disc_try(DiscCommand::SetFilter, s_.disc_params.data())) {
            s_.disc_errors = 0;
            s_.line_seek_step = 4;
        }
        break;
    case 4: {
        const VoiceLine* l = line(s_.seek_line);
        const std::uint32_t file_sector = l != nullptr ? host_->voice_file_sector(l->file) : 0;
        const std::uint32_t sector = file_sector + kLeadIn + (l != nullptr ? l->sector : 0);
        s_.line_limit = static_cast<std::int32_t>((l != nullptr ? l->length : 0) + kLineGrace);
        s_.line_seek_step = 5;
        seek_to(sector);
        break;
    }
    case 5:
        if (disc_try(DiscCommand::SeekAudio, s_.seek_position.data())) {
            s_.disc_errors = 0;
            s_.line_seek_step = 6;
        }
        break;
    case 6:
        if (disc_try(DiscCommand::Pause, nullptr)) {
            s_.line_seek_step = 0;
            s_.line_pending = false;
            pop_task();
            s_.disc_errors = 0;
        }
        break;
    default:
        break;
    }
}

void SoundDriver::line_finished() {
    s_.line_playing = 0;
    s_.line_pending = false;
}

// ---- Task queue ----

// Queues a task once: an earlier copy is taken out (a stop also drops a queued play).
void SoundDriver::add_task(std::uint8_t task) {
    auto take_out = [this](int at) {
        for (int y = at; y < kTaskSlots - 2; ++y) {
            s_.tasks[y] = s_.tasks[y + 1];
        }
        s_.tasks[kTaskSlots - 1] = 0;
    };
    if (task == kTaskStopLine) {
        for (int i = 1; i < kTaskSlots - 2; ++i) {
            if (s_.tasks[i] == kTaskPlayLine) {
                take_out(i);
                s_.line_pending = false;
            }
        }
    }
    for (int i = 1; i < kTaskSlots - 2; ++i) {
        if (s_.tasks[i] == task) {
            take_out(i);
        }
    }
    for (int i = 0; i < kTaskSlots - 1; ++i) {
        if (s_.tasks[i] == 0) {
            s_.tasks[i] = task;
            break;
        }
    }
    s_.current_task = s_.tasks[0];
}

void SoundDriver::pop_task() {
    if (s_.tasks[0] == 0) {
        return;
    }
    for (int i = 0; i < kTaskSlots - 1; ++i) {
        s_.tasks[i] = s_.tasks[i + 1];
    }
    s_.tasks[kTaskSlots - 1] = 0;
}

void SoundDriver::run_tasks() {
    s_.current_task = s_.tasks[0];
    switch (s_.current_task) {
    case 0:
        break;
    case kTaskPlayLine: {
        if (s_.music_fade == 0) {
            s_.music_target = t_.music_ducked_volume;
        }
        const std::uint16_t index = s_.line_requested;
        host_->line_seek_play(index, line_volume_of(index));
        const int level = ((line_volume_of(index) * s_.options_lines) >> 7) << 8;
        host_->set_line_volume(level, level);
        host_->line_play(index);
        host_->set_line_mix(true);
        s_.line_playing = index;
        pop_task();
        break;
    }
    case kTaskStopLine:
        host_->set_line_mix(false);
        host_->line_seek_stop();
        host_->line_stop();
        if (s_.music_fade == 0) {
            s_.music_target = t_.music_volume;
        }
        pop_task();
        break;
    case kTaskSeekLine:
        step_line_seek();
        break;
    case kTaskStartMusic:
        start_music();
        break;
    case kTaskStopMusic:
        step_music_stop();
        break;
    default:
        if (s_.current_task >= t_.first_bank_command) {
            step_bank_load();
        } else if (s_.current_task >= kFirstSongCommand) {
            step_song_load();
        } else {
            pop_task();
        }
        break;
    }

    if (s_.line_playing != 0) {
        s_.line_elapsed = static_cast<std::int32_t>(host_->vblank_count() - s_.line_started);
    }

    if (s_.music_fade != 0) {
        s_.music_channels = kMusicStopping;
        if (s_.music_volume <= 0) {
            s_.music_volume = 0;
            stop_music_now();
        } else {
            s_.music_volume = s16(s_.music_volume - s_.music_fade);
            if (s_.music_volume <= 0) {
                s_.music_volume = 0;
                stop_music_now();
            }
        }
        s_.music_target = s_.music_volume;
        set_music_bus(s_.music_volume, s_.music_volume);
    } else if (s_.music_target != s_.music_volume) {
        // One step a frame towards the target; the last step snaps onto it.
        s_.music_volume = s16(s_.music_volume + (s_.music_volume < s_.music_target ? 1 : -1));
        if (std::abs(s_.music_volume - s_.music_target) < 2) {
            s_.music_volume = s_.music_target;
        }
        set_music_bus(s_.music_volume, s_.music_volume);
    }

    // A line that plays past its length (plus grace) is stopped once the queue is idle.
    if (static_cast<std::uint32_t>(s_.line_elapsed) > static_cast<std::uint32_t>(s_.line_limit) &&
        s_.current_task == 0) {
        if (!s_.line_idle) {
            add_task(kTaskStopLine);
        }
        s_.line_started = host_->vblank_count();
        s_.line_elapsed = 0;
    }

    if (s_.muted) {
        if (s_.main_volume > 0) {
            s_.main_volume = s16(s_.main_volume - kMuteStep);
            if (s_.main_volume <= 0) {
                s_.main_volume = 0;
            }
            host_->set_main_volume(s_.main_volume);
        }
    } else if (s_.main_volume < kFullVolume) {
        s_.main_volume = s16(s_.main_volume + kUnmuteStep);
        if (s_.main_volume >= kFullVolume) {
            s_.main_volume = kFullVolume;
        }
        host_->set_main_volume(s_.main_volume);
    }

    if (s_.disc_errors > kDiscErrorLimit) {
        host_->disc_reset(false);
        reset_disc_steps();
    }
}

void SoundDriver::drain_tasks() {
    // Instant loads: the steps of a load that wait for nothing run in the same frame. Not while
    // a line plays (its fades keep frame time) and not at the console's disc pace.
    if (s_.line_playing != 0 || host_->disc_paced()) {
        return;
    }
    for (int i = 0; i < 512; ++i) {
        if (s_.tasks[0] == 0) {
            return;
        }
        run_tasks();
    }
}

// ---- Disc ----

void SoundDriver::reset_disc_steps() {
    if (s_.load_step != kLoadStart) {
        s_.load_step = kLoadClose;
    }
    s_.line_seek_step = 0;
    s_.disc_errors = 0;
}

bool SoundDriver::disc_try(DiscCommand command, const std::uint8_t* params) {
    if (host_->disc_ready() && host_->disc_command(command, params)) {
        s_.disc_errors = 0;
        return true;
    }
    ++s_.disc_errors;
    if (s_.disc_errors > kDiscErrorLimit) {
        host_->disc_reset(true);
        reset_disc_steps();
    }
    return false;
}

// ---- Bank and song loads ----

void SoundDriver::queue_bank(std::uint16_t command) {
    if (s_.line_playing != 0) {
        add_task(kTaskStopLine);
    }
    s_.load_attempts = 0;
    add_task(static_cast<std::uint8_t>(command));
    s_.loading = true;
}

void SoundDriver::queue_song(std::uint16_t command) {
    if (s_.song == command) {
        return;
    }
    if (s_.line_playing != 0) {
        add_task(kTaskStopLine);
    }
    queue_music_stop();
    call(static_cast<std::uint16_t>(command + t_.song_bank_offset));
    add_task(static_cast<std::uint8_t>(command));
    s_.load_attempts = 0;
    s_.song = command;
    s_.loading = true;
}

void SoundDriver::step_bank_load() {
    const DiscFile* file = s_.bank_file >= 0 ? &t_.files[s_.bank_file] : nullptr;
    switch (s_.load_step) {
    case kLoadStart: {
        const std::uint8_t command = s_.tasks[0];
        s_.bank_file = command - t_.first_bank_command;
        s_.load_slot = t_.files[s_.bank_file].slot;
        if (s_.load_slot != 0) {
            // The slot already holds this bank: nothing to load.
            if (s_.last_bank[s_.load_slot - 1] == command) {
                s_.load_step = kLoadStart;
                s_.loading = false;
                pop_task();
                break;
            }
            s_.last_bank[s_.load_slot - 1] = command;
        }
        if (command >= t_.first_ambient_command && command <= t_.last_ambient_command) {
            const std::uint8_t depth = t_.ambient_reverb[command - t_.first_ambient_command];
            if (depth != s_.reverb_depth) {
                set_reverb_depth(depth);
            }
        }
        s_.load_step = kLoadClose;
        break;
    }
    case kLoadClose:
        s_.load_moved = 0;
        host_->close_bank(s_.load_slot);
        s_.load_step = kLoadLocate;
        break;
    case kLoadLocate: {
        const auto pos = disc_position(file->sector + s_.load_moved / kSectorBytes + kLeadIn);
        if (disc_try(DiscCommand::SetLocation, pos.data())) {
            s_.load_step = kLoadRead;
        }
        break;
    }
    case kLoadRead:
        if (host_->disc_ready()) {
            host_->disc_read(file->size < kReadPiece ? static_cast<int>((file->size + kSectorBytes - 1) / kSectorBytes)
                                                     : static_cast<int>(kReadPiece / kSectorBytes),
                             ReadTarget::BankHeader);
            s_.load_step = kLoadStore;
            s_.disc_errors = 0;
        }
        ++s_.disc_errors;
        break;
    case kLoadStore:
        if (host_->disc_read_remaining() == 0) {
            host_->store_read(ReadTarget::BankHeader, s_.load_slot, file->header_size);
            // The bank itself comes from its converted form; the reads keep the pacing.
            host_->load_bank(s_.load_slot, file->sector);
            s_.load_step = kLoadPiece;
        }
        ++s_.disc_errors;
        break;
    case kLoadPiece:
        if (file->size < kReadPiece) {
            s_.load_moved = file->size;
            s_.load_step = kLoadDone;
        } else {
            s_.load_moved = kReadPiece;
            s_.load_step = kLoadLocateNext;
        }
        break;
    case kLoadLocateNext: {
        const auto pos = disc_position(file->sector + (s_.load_moved + kSectorBytes - 1) / kSectorBytes + kLeadIn);
        if (disc_try(DiscCommand::SetLocation, pos.data())) {
            s_.load_step = kLoadReadNext;
        }
        break;
    }
    case kLoadReadNext: {
        if (!host_->disc_ready()) {
            return;
        }
        const std::uint32_t left = file->size - s_.load_moved;
        host_->disc_read(left < kReadPiece ? static_cast<int>((left + kSectorBytes - 1) / kSectorBytes)
                                           : static_cast<int>(kReadPiece / kSectorBytes),
                         ReadTarget::BankHeader);
        s_.load_step = kLoadStoreNext;
        break;
    }
    case kLoadStoreNext: {
        if (host_->disc_read_remaining() != 0) {
            return;
        }
        const std::uint32_t left = file->size - s_.load_moved;
        if (left < kReadPiece) {
            s_.load_moved = file->size;
            s_.load_step = kLoadDone;
        } else {
            s_.load_moved += kReadPiece;
            s_.load_step = kLoadLocateNext;
        }
        break;
    }
    case kLoadDone:
        s_.load_step = kLoadStart;
        s_.disc_errors = 0;
        s_.loading = false;
        pop_task();
        break;
    default:
        break;
    }
}

void SoundDriver::step_song_load() {
    const DiscFile* file = s_.song_file >= 0 ? &t_.files[s_.song_file] : nullptr;
    switch (s_.load_step) {
    case kLoadStart:
        s_.song_file = t_.song_file_offset + s_.tasks[0];
        s_.load_slot = t_.files[s_.song_file].slot;
        s_.load_step = kLoadClose;
        break;
    case kLoadClose:
        stop_music_now();
        host_->song_close();
        s_.load_step = kLoadLocate;
        break;
    case kLoadLocate: {
        const auto pos = disc_position(file->sector + kLeadIn);
        if (disc_try(DiscCommand::SetLocation, pos.data())) {
            s_.load_step = kLoadRead;
        }
        break;
    }
    case kLoadRead:
        if (host_->disc_ready()) {
            host_->disc_read(static_cast<int>((file->size + kSectorBytes - 1) / kSectorBytes), ReadTarget::Song);
            s_.load_step = kLoadStore;
            s_.disc_errors = 0;
        }
        ++s_.disc_errors;
        break;
    case kLoadStore:
        if (host_->disc_read_remaining() == 0) {
            host_->store_read(ReadTarget::Song, s_.load_slot, file->size);
            const bool opened = host_->open_song(file->sector);
            if (!opened && s_.load_attempts < kMaxLoadAttempts) {
                ++s_.load_attempts;
                s_.load_step = kLoadClose;
            } else {
                s_.load_step = kLoadStart;
                s_.loading = false;
                pop_task();
            }
            s_.disc_errors = 0;
        }
        ++s_.disc_errors;
        break;
    default:
        break;
    }
}

}  // namespace sh::audio
