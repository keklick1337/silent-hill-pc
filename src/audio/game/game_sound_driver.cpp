// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game's sound driver on the engine: the host connects sh::audio::SoundDriver
// to the engine mixer (sound_api.h), the disc drive model, the voice-line player and file sounds of the sound
// registry. The sound command entry points of the game (SD_Call, Sd_*) call the driver.

#include "engine/ids/game_numbers.h"
#include "audio/game/game_sound_driver.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "audio/bridge/sound_bridge.h"
#include "audio/voice/voice_line_player.h"
#include "engine/audio/disc_drive.h"
#include "engine/audio/sound_api.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session_audio.h"
#include "engine/gconfig/sound_tables.h"
#include "engine/memory/work_buffers.h"
#include "engine/log/sh_log.h"
#include "engine/loop/vblank_clock.h"
#include "engine/platform/disc.h"
#include "engine/timing/vblank.h"

extern "C" {
const char* PcPort_GetGameDataPath(void);

// File sounds of the sound registry (an own WAV instead of a bank tone).
int Pc_SoundIsFile(int sfxId);
int Pc_SoundFilePlay(int sfxId, int left, int right, int pitchCents);
void Pc_SoundFileStop(int sfxId);
void Pc_SoundFileVolume(int sfxId, int left, int right);

// The behaviour diff of characters records the sounds one character update asks for; a
// non-zero return swallows the sound.
int (*g_PcSfxObserver)(int kind, int sfxId, int balance, int vol, int pitch) = nullptr;
}

namespace sh::audio {
namespace {

// Where a loaded bank's header is kept (one buffer per bank slot); disc reads land in the staging buffer and the
// loaded song in its own.
constexpr sh::memory::WorkBuffer kBankHeader[4] = {sh::memory::WorkBuffer::SoundBankHeader0, sh::memory::WorkBuffer::SoundBankHeader1,
                                               sh::memory::WorkBuffer::SoundBankHeader2, sh::memory::WorkBuffer::SoundBankHeader3};
// Voice files the driver addresses by number (1..9) plus the two ends of the table.
constexpr int kVoiceFiles = 11;
constexpr int kAmbientSlot = 2;

std::uint8_t* memory(sh::memory::WorkBuffer b) { return sh::memory::work_buffers().as<std::uint8_t>(b); }

int blank_rate() { return sh::loop::vblank_clock().rate(); }

DiscDrive& disc_drive() {
    static DiscDrive drive(DiscDriveHost{
        [] { return sh::timing::vblank_count(); },
        [] { return blank_rate(); },
        [] { return sh::disc::has_source(); },
        [] { return sh::disc::sector_count(); },
        [](int lba, void* dst) { return sh::disc::read_sector(lba, sh::disc::SectorKind::Data, dst); },
        [] { return sh::disc::drive_pacing().instant || !sh::disc::drive_pacing().console_speed; },
        [] { sh::timing::virtual_time::device_busy(); }});
    return drive;
}

SoundRules load_rules() {
    const std::string path = std::string(PcPort_GetGameDataPath()) + "/config/sound_driver.json";
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Result<SoundRules> parsed = SoundRules::parse(text);
    if (!parsed) {
        SH_ERR("[SOUND] %s: %s - the sound driver runs without its rules", path.c_str(), parsed.error().c_str());
        return {};
    }
    return std::move(parsed.value());
}

// The game's sound tables (sound_tables.json as the boot loaded them and the disc's file table moved them) and the
// driver rules.
DriverTables game_tables() {
    const sh::gconfig::SoundTables& g = sh::gconfig::active_sound_tables().tables;
    DriverTables t;
    t.effect_base = static_cast<std::uint16_t>(sh::game_state::kSfxBase);
    for (const sh::gconfig::SoundEffect& v : g.effects) {
        t.effects.push_back({v.audio_index, v.bank, v.program, v.note, v.volume});
    }
    for (const sh::gconfig::SoundFile& f : g.files) {
        t.files.push_back({f.bank, f.vag_data_offset, f.size, f.sector});
    }
    t.ambient_reverb.assign(g.reverb_depths.begin(), g.reverb_depths.end());
    for (const auto& row : g.layer_channels) {
        t.layer_channels.push_back(row);
    }
    for (const sh::gconfig::XaLine& x : g.xa_lines) {
        t.lines.push_back({x.xa_file, x.sector, x.group_index, x.length, x.group});
    }
    apply_rules(game_sound_rules(), game_sound_rules_resolved(), t);
    return t;
}

class GameDriverHost final : public DriverHost {
public:
    void set_mono(bool mono) override { ShSound_SetMono(mono ? 1 : 0); }
    void set_music_slot(int slot) override { ShSound_SetMusicSlot(slot); }
    void set_reverb_depth(int depth) override { ShSound_SetReverbDepth(depth, depth); }
    void set_line_mix(bool on) override { ShSound_SetLineMix(on ? 1 : 0); }
    void set_main_volume(int volume) override { ShSound_SetMainVolume(volume, volume); }
    void set_song_volume(int left, int right) override { ShSound_SetSongVolume(left, right); }
    void set_line_volume(int left, int right) override { ShSound_SetLineVolume(left, right); }
    void song_play() override { ShSound_SongPlay(); }
    void song_stop() override { ShSound_SongStop(); }
    void song_close() override { ShSound_SongClose(); }
    void close_bank(int slot) override { ShSound_CloseBank(slot); }
    int key_on_program(int slot, int program, int note, int fine, int left, int right) override {
        return ShSound_KeyOnProgram(slot, program, note, fine, left, right);
    }
    int key_on_tone(int slot, int program, int tone, int note, int fine, int left, int right) override {
        return ShSound_KeyOnTone(slot, program, tone, note, fine, left, right);
    }
    int key_on_voice(int voice, int slot, int program, int tone, int note, int fine, int left, int right) override {
        return ShSound_KeyOnVoice(voice, slot, program, tone, note, fine, left, right);
    }
    void key_off_program(int slot, int program, int note) override { ShSound_KeyOffProgram(slot, program, note, 0); }
    void key_off_voice(int voice, bool cut) override { ShSound_KeyOffVoice(voice, cut ? 1 : 0); }
    int key_status(int voice) override { return ShSound_KeyStatus(voice); }
    int voice_pitch(int voice) override { return ShSound_VoicePitch(voice); }
    void set_voice_pitch(int voice, int pitch) override { ShSound_SetVoicePitch(voice, pitch); }
    void set_voice_volume(int voice, int left, int right) override { ShSound_SetVoiceVolume(voice, left, right); }
    std::uint32_t take_released_voices() override { return ShSound_TakeReleasedVoices(); }
    int channel_volume(int channel) override { return ShSound_ChannelVolume(channel); }
    void set_channel_volume(int channel, int volume) override { ShSound_SetChannelVolume(channel, volume); }

    void load_bank(int slot, std::int32_t sector) override {
        if (slot == kAmbientSlot) {
            Pc_SoundBanks_Reset();
        }
        ShSoundBridge_LoadBankAt(slot, sector);
    }
    bool open_song(std::int32_t sector) override { return ShSoundBridge_OpenSongAt(sector) != 0; }

    void line_play(std::uint16_t line) override { voice_lines().play_line(line); }
    void line_stop() override { voice_lines().stop(); }
    void line_player_volume(int, int) override {}  // the mixer's CD input volume is the driver's own call
    void line_seek_play(std::uint16_t line, int volume) override { voice_lines().play_task_ran(line, volume); }
    void line_seek_stop() override { voice_lines().stop_task_ran(); }
    std::uint32_t voice_file_sector(int file) override {
        return file >= 0 && file < kVoiceFiles ? sh::game_state::audio::voice_file_sector(file) : 0;
    }

    void disc_mix(bool) override {}  // the PC mixer owns the disc audio volume
    bool disc_ready() override { return disc_drive().ready(); }
    bool disc_command(DiscCommand command, const std::uint8_t* params) override {
        return disc_drive().command(command, params);
    }
    void disc_read(int sectors, ReadTarget) override {
        constexpr std::size_t kSectorBytes = 2048;
        const std::size_t room = sh::memory::WorkBuffers::capacity(sh::memory::WorkBuffer::FileStaging) / kSectorBytes;
        if (sectors < 0 || static_cast<std::size_t>(sectors) > room) {
            SH_ERR("[SOUND] a read of %d sectors does not fit the staging buffer (%zu sectors); clipped", sectors, room);
            sectors = sectors < 0 ? 0 : static_cast<int>(room);
        }
        disc_drive().read(sectors, memory(sh::memory::WorkBuffer::FileStaging));
    }
    int disc_read_remaining() override { return disc_drive().read_remaining(); }
    void disc_reset(bool wait) override {
        if (wait) {
            sh::timing::vblank_count();  // the console's wait for blanks only counts here
        }
    }
    void store_read(ReadTarget target, int slot, std::uint32_t bytes) override {
        const sh::memory::WorkBuffer buffer = target == ReadTarget::BankHeader ? kBankHeader[slot] : sh::memory::WorkBuffer::SongFile;
        const std::size_t room = std::min(sh::memory::WorkBuffers::capacity(buffer),
                                          sh::memory::WorkBuffers::capacity(sh::memory::WorkBuffer::FileStaging));
        if (bytes > room) {
            SH_ERR("[SOUND] %u bytes of sound data do not fit their buffer (%zu bytes); clipped", bytes, room);
            bytes = static_cast<std::uint32_t>(room);
        }
        std::memcpy(memory(buffer), memory(sh::memory::WorkBuffer::FileStaging), bytes);
    }
    std::uint32_t vblank_count() override { return static_cast<std::uint32_t>(sh::timing::vblank_count()); }
    bool disc_paced() override { return sh::disc::drive_pacing().console_speed; }

    bool effect_is_file(std::uint16_t id) override { return Pc_SoundIsFile(id) != 0; }
    void effect_file_play(std::uint16_t id, int left, int right, int cents) override {
        Pc_SoundFilePlay(id, left, right, cents);
    }
    void effect_file_volume(std::uint16_t id, int left, int right) override { Pc_SoundFileVolume(id, left, right); }
    void effect_file_stop(std::uint16_t id) override { Pc_SoundFileStop(id); }
    bool effect_swallowed(EffectRequest request, std::uint16_t id, int balance, int volume, int pitch) override {
        return g_PcSfxObserver != nullptr && g_PcSfxObserver(static_cast<int>(request), id, balance, volume, pitch) != 0;
    }

    int effect_slot(std::uint16_t id, int slot) override { return guest_effect_slot(id, slot); }
};

SoundDriver& drv() { return game_sound_driver(); }

}  // namespace

const SoundRules& game_sound_rules() {
    static const SoundRules rules = load_rules();
    return rules;
}

const ResolvedSoundRules& game_sound_rules_resolved() {
    static const ResolvedSoundRules resolved = resolve(game_sound_rules(), [](std::string_view name) {
        return sh::ids::sound_number(std::string(name).c_str());
    });
    return resolved;
}

DiscDrive& game_disc_drive() { return disc_drive(); }

SoundDriver& game_sound_driver() {
    static GameDriverHost host;
    static SoundDriver driver(game_tables(), host);
    return driver;
}

}  // namespace sh::audio

namespace au = sh::audio;

// Boot: gamedata/config/sound_tables.json with the mods' patches. On an error the tables stay zero.
extern "C" void SoundTables_GamedataInit(const char* gamedataDir, const char* modsDir) {
    if (gamedataDir == nullptr) {
        return;
    }
    std::vector<std::string> messages;
    std::string modId;
    const bool ok = sh::gconfig::load_active_sound_tables(gamedataDir, modsDir ? modsDir : "", messages, &modId);
    for (const std::string& m : messages) {
        SH_WARN("[SOUND] %s", m.c_str());
    }
    if (!ok) {
        SH_ERR("[SOUND] gamedata/config/sound_tables.json not loaded (sound tables stay empty)");
        return;
    }
    const sh::gconfig::SoundTables& t = sh::gconfig::active_sound_tables().tables;
    SH_LOG("[SOUND] gamedata/config/sound_tables.json: %d files, %d effects, %d XA lines%s",
           static_cast<int>(t.files.size()), static_cast<int>(t.effects.size()), static_cast<int>(t.xa_lines.size()),
           modId.empty() ? "" : (" (mod " + modId + ")").c_str());
}

extern "C" {

void SD_Call(std::uint32_t cmd) { au::drv().call(cmd); }
std::uint8_t Sd_AudioStreamingCheck(void) { return au::drv().streaming_state(); }
std::uint16_t Sd_UnkU16Get(void) { return au::drv().music_channels(); }
void SD_Init(void) { au::drv().init(); }
void sd_work_init(void) { au::drv().reset(); }

std::uint8_t Sd_PlaySfx(std::uint16_t sfxId, std::int8_t balance, std::uint8_t vol) {
    return static_cast<std::uint8_t>(au::drv().play_effect(sfxId, balance, vol));
}

void Sd_SfxAttributesUpdate(std::uint16_t sfxId, std::int8_t balance, std::uint8_t vol, std::int8_t pitch) {
    au::drv().update_effect(sfxId, balance, vol, pitch);
}

void Sd_SfxStop(std::uint16_t sfxId) { au::drv().stop_effect(sfxId); }
void Sd_AllSfxWithRRStop(void) { au::drv().stop_all_effects(true); }
void Sd_BgmLayerVolumeSet(std::uint8_t layerIdx, std::uint8_t vol) { au::drv().set_layer_volume(layerIdx, vol); }
void Sd_XaPreLoadAudioPreTaskAdd(std::uint16_t xaIdx) { au::drv().preload_line(xaIdx); }

void Sd_SetVolume(std::uint8_t xaVol, std::int16_t bgmVol, std::uint8_t seVol) {
    au::drv().set_option_volumes(xaVol, static_cast<std::uint8_t>(bgmVol), seVol);
}

void Sd_TaskPoolExecute(void) { au::drv().run_tasks(); }
void Sd_TaskPoolDrain(void) { au::drv().drain_tasks(); }
std::uint16_t Sd_GetXaAudioIdx(void) { return au::drv().line_playing(); }

// Watchdog of the loading screens: the line is forgotten as if it had ended.
void Sd_ForceClearXaAudioIdx(void) { au::drv().line_finished(); }

}  // extern "C"
