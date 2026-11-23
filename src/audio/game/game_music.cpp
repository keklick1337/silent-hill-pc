// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Music and ambience of the running game on the engine: the music layer mix (sh::gameflow::MusicMix
// section 5) each frame, an event's change of track (sh::audio::music_crossfade_step), the map-start step machines of
// music and ambience (sh::loader::BootAudio, gamedata/config/boot_audio.json) with the progress-dependent ambient
// banks of gamedata/config/sound_driver.json, and the room the player stands in. State goes through Session.audio,
// sound commands through the game's sound driver. The Bgm_*, Sd_Ambient* and Game_MapRoomIdxUpdate functions are
// the game's entry points.

#include "engine/runtime/game_hooks.h"
#include "engine/ids/game_numbers.h"
#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

#include "audio/game/game_sound_driver.h"
#include "engine/audio/music_change.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_ui.h"
#include "engine/game_state/session_audio.h"
#include "engine/gameflow/music_mix.h"
#include "engine/loader/queue/boot_audio.h"
#include "engine/log/sh_log.h"

extern "C" {
const char* PcPort_GetGameDataPath(void);

// Defined below; the mix and the step machines call back into them.
void Bgm_TrackUpdate(unsigned int first);
void Bgm_AllLayersMute(void);
void Bgm_Update(std::int32_t bgmFlags, std::int32_t fadeSpeed, const std::uint8_t* layerLimits);
unsigned int Bgm_ActiveBgmTrackCheck(std::int32_t bgmIdx);
void Bgm_TrackSet(std::int32_t bgmIdx);
void Bgm_ChannelSet(void);
void Sd_AmbientSfxSet(std::int32_t idx);
}

namespace {

namespace au = sh::audio;
namespace gf = sh::gameflow;
namespace gs = sh::game_state;
namespace ld = sh::loader;
namespace sa = sh::game_state::audio;

static_assert(sa::kMusicVolumes == gf::kMusicVolumes, "the game keeps one volume per mix channel");

// Track 0 is no music; track 1 keeps whatever plays (the map's "keep playing" marker).
constexpr std::int32_t kTrackNone = 0;
constexpr std::int32_t kTrackKeep = 1;
constexpr std::int32_t kMixLayer0 = 1 << 0;
constexpr std::int32_t kDefaultFadeSpeed = 240 * 4096;  // Q12 per second
// What the step machines answer while the audio or the file queue is busy.
constexpr unsigned int kBusy = 0xFFFFFFFFu;

au::SoundDriver& driver() { return au::game_sound_driver(); }
const au::SoundRules::Commands& commands() { return au::game_sound_rules().commands; }

bool music_is_real(std::int32_t track) { return track != kTrackNone && track != kTrackKeep; }

class GameMusicDriver final : public gf::MusicDriver {
public:
    std::uint16_t status() const override { return driver().music_channels(); }
    std::uint8_t layer_volume(std::uint8_t layer) const override { return driver().layer_volume(layer); }
    void set_layer_volume(std::uint8_t layer, std::uint8_t volume) override { driver().set_layer_volume(layer, volume); }
    void stop_music() override { driver().call(commands().fade_out_music); }
    void restart_channels() override { Bgm_ChannelSet(); }
};

gf::MusicMix& mix() {
    static gf::MusicMix m;
    return m;
}

gf::MusicWobble& wobble() {
    static gf::MusicWobble w;
    return w;
}

// Set by Bgm_Update: the map's music logic mixed this frame, so Bgm_TrackUpdate does not add the default mix.
bool g_mixedThisFrame = false;

std::array<std::int16_t, gf::kMusicVolumes> load_volumes() {
    std::array<std::int16_t, gf::kMusicVolumes> v{};
    for (int i = 0; i < gf::kMusicVolumes; ++i) {
        v[i] = sa::music_layer_volume(i);
    }
    return v;
}

void store_volumes(const std::array<std::int16_t, gf::kMusicVolumes>& v) {
    for (int i = 0; i < gf::kMusicVolumes; ++i) {
        sa::set_music_layer_volume(i, v[i]);
    }
}

gf::MusicFrame make_frame(std::int32_t flags, std::int32_t fade_speed, const std::uint8_t* limits) {
    gf::MusicFrame f;
    f.request = static_cast<std::uint32_t>(flags);
    f.fade_speed = fade_speed;
    f.limits = limits;
    f.delta = gs::delta_time_raw();
    f.player_dead_or_game_over = gs::chara_health(0) <= 0 || gs::sys_state() == gs::SysState::GameOver;
    f.radio_noise = sa::radio_noise() && gs::radio_on();
    return f;
}

class EventMusicChange final : public au::MusicChangeHost {
public:
    bool busy() override { return driver().streaming_state() != 0 || !sa::load_queued_chunks(); }
    int step() override { return sa::sys_step(1); }
    void set_step(int step) override { sa::set_sys_step(1, step); }
    void finish() override { sa::set_sys_step(0, sa::sys_step(0) + 1); }
    bool track_differs(int track) override { return Bgm_ActiveBgmTrackCheck(track) != 0; }
    void request_mute() override { sa::set_music_status(sa::music_status() | gf::music_status::kRequestMute); }
    void fade_out_fast() override { driver().call(commands().fade_out_music_fast); }
    bool music_silent() override { return driver().music_channels() == 0; }
    void load_track(int track) override { Bgm_TrackSet(track); }
};

const ld::BootAudio* boot_tables() {
    static bool tried = false;
    static ld::BootAudio audio;
    static bool ok = false;
    if (!tried) {
        tried = true;
        const std::string path = std::string(PcPort_GetGameDataPath()) + "/config/boot_audio.json";
        std::ifstream in(path, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto parsed = ld::BootAudio::parse(text);
        if (parsed) {
            audio = std::move(parsed.value());
            ok = true;
        } else {
            SH_ERR("[BGM] %s: %s - no music or ambience is started", path.c_str(), parsed.error().c_str());
        }
    }
    return ok ? &audio : nullptr;
}

bool queue_busy() { return driver().streaming_state() != 0 || gs::file_queue_length() > 0; }

class MusicHost final : public ld::MusicInitHost {
public:
    bool audio_or_queue_busy() override { return queue_busy(); }
    void restore_kept_track() override {
        if (sa::map_music_track() == kTrackKeep) {
            Bgm_TrackUpdate(1);
        }
    }
    bool track_differs() override { return Bgm_ActiveBgmTrackCheck(sa::map_music_track()) != 0; }
    void fade_out_and_mute() override {
        driver().call(commands().fade_out_music);
        Bgm_AllLayersMute();
    }
    bool sound_idle() override { return driver().music_channels() == 0; }
    void load_map_track() override { Bgm_TrackSet(sa::map_music_track()); }
};

class AmbientHost final : public ld::AmbientInitHost {
public:
    bool audio_or_queue_busy() override { return queue_busy(); }
    bool target_differs() override {
        const char* map = gs::map_name(gs::save_map_index());
        if (map != nullptr) {
            const int bank = au::ambient_override(au::game_sound_rules(), map, [](std::string_view flag) {
                const int bit = sh::ids::flag_number(std::string(flag).c_str());
                return bit >= 0 && gs::event_flag(bit);
            });
            if (bank >= 0) {
                sa::set_map_ambient_bank(bank);
            }
        }
        return sa::ambient_bank() != sa::map_ambient_bank();
    }
    void fade_out_ambient() override { driver().call(commands().fade_out_ambient); }
    void load_target_ambient() override { Sd_AmbientSfxSet(sa::map_ambient_bank()); }
};

}  // namespace

extern "C" {

// --- The music mix ---

void Bgm_TrackUpdate(unsigned int first) {
    g_mixedThisFrame = false;
    if (sa::run_map_music(first != 0) && first == 0 && !g_mixedThisFrame) {
        Bgm_Update(kMixLayer0, kDefaultFadeSpeed, nullptr);
    }
}

void Bgm_AllLayersMute(void) {
    std::array<std::int16_t, gf::kMusicVolumes> v{};
    gf::MusicMix::silence(v);
    store_volumes(v);
}

void Bgm_Update(std::int32_t bgmFlags, std::int32_t fadeSpeed, const std::uint8_t* layerLimits) {
    std::array<std::int16_t, gf::kMusicVolumes> vols = load_volumes();
    std::uint32_t status = sa::music_status();
    GameMusicDriver music;
    mix().update(make_frame(bgmFlags, fadeSpeed, layerLimits), vols, status, music);
    store_volumes(vols);
    sa::set_music_status(status);
    g_mixedThisFrame = true;
}

void Bgm_TrackIdxUpdate(void) {
    sa::clear_radio_noise();
    sa::set_music_status(sa::music_status() | gf::music_status::kDuck);
    Bgm_TrackUpdate(0);
}

void Bgm_TrackChange(std::int32_t bgmIdx) {
    if (bgmIdx != kTrackNone) {
        sa::set_map_music_track(bgmIdx);
    }
}

void Bgm_PlayNewTrack(std::int32_t bgmIdx) {
    if (Bgm_ActiveBgmTrackCheck(bgmIdx)) {
        Bgm_TrackSet(bgmIdx);
    }
}

// An event's change of music: mute the map's mix, fade the song out fast, load the track once quiet.
void Bgm_CrossfadeToTrack(std::int32_t bgmIdx) {
    EventMusicChange host;
    au::music_crossfade_step(bgmIdx, host);
}

std::uint32_t Bgm_CurrentBgmIdxGet(void) { return wobble().next(gs::delta_time_raw()); }

// --- Tracks and ambient banks ---

unsigned int Bgm_ActiveBgmTrackCheck(std::int32_t bgmIdx) {
    return music_is_real(bgmIdx) && sa::music_track() != bgmIdx ? 1 : 0;
}

void Bgm_TrackSet(std::int32_t bgmIdx) {
    if (!music_is_real(bgmIdx)) {
        return;
    }
    const ld::BootAudio* t = boot_tables();
    const std::optional<std::uint16_t> cmd = t != nullptr ? t->bgm_load(bgmIdx) : std::nullopt;
    SH_LOG_INFO(AUDIO, "BGM track %d (was %d), sound command %d", static_cast<int>(bgmIdx), sa::music_track(),
                cmd ? static_cast<int>(*cmd) : -1);
    sa::set_music_track(bgmIdx);
    if (cmd) {
        driver().call(*cmd);
    }
}

void Bgm_ChannelSet(void) {
    const int track = sa::music_track();
    if (!music_is_real(track)) {
        return;
    }
    const ld::BootAudio* t = boot_tables();
    const std::optional<std::uint16_t> cmd = t != nullptr ? t->bgm_channels(track) : std::nullopt;
    SH_DBG("[BGM] ChannelSet bgmIdx=%d cmd=%d", track, cmd ? static_cast<int>(*cmd) : -1);
    if (cmd) {
        driver().call(*cmd);
    }
}

void Sd_AmbientSfxSet(std::int32_t idx) {
    sa::set_ambient_bank(idx);
    const ld::BootAudio* t = boot_tables();
    const std::optional<std::uint16_t> cmd = t != nullptr ? t->ambient_load(idx) : std::nullopt;
    SH_DBG("[SH_AMB] Sd_AmbientSfxSet idx=%d -> loadCmd=%d", static_cast<int>(idx), cmd ? static_cast<int>(*cmd) : -1);
    if (cmd) {
        driver().call(*cmd);
    }
}

// The sound command that loads ambient bank `idx` (guest_sound_banks.cpp), -1 when none.
int Pc_AmbientLoadCommand(std::int32_t idx) {
    const ld::BootAudio* t = boot_tables();
    const std::optional<std::uint16_t> cmd = t != nullptr ? t->ambient_load(idx) : std::nullopt;
    return cmd ? static_cast<int>(*cmd) : -1;
}

// --- Map start ---

unsigned int Bgm_Init(void) {
    MusicHost host;
    int step = sa::game_step(1);
    const ld::BootStep r = ld::music_init_step(step, host);
    if (r != ld::BootStep::Wait) {
        sa::set_game_step(1, step);
    }
    // A busy audio or queue answers "busy" (true), as the original did.
    return r != ld::BootStep::Done ? 1 : 0;
}

unsigned int Sd_AmbientSfxInit(void) {
    AmbientHost host;
    int step = sa::game_step(1);
    const ld::BootStep r = ld::ambient_init_step(step, host);
    if (r != ld::BootStep::Wait) {
        sa::set_game_step(1, step);
    }
    return r == ld::BootStep::Wait ? kBusy : (r == ld::BootStep::Run ? 1 : 0);
}

// --- The player's room ---

void Game_MapRoomIdxUpdate(void) {
    const sa::Point player = sa::player_position();
    sa::set_save_room(sa::map_room_at(player.x, player.z));
    sh::hooks::game_room_update(gs::save_room());
}

}  // extern "C"
