// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/voice/voice_line_player.h"

#include "audio/bridge/sound_bridge.h"
#include "audio/game/game_sound_driver.h"
#include "engine/audio/disc_drive.h"
#include "engine/audio/sound_api.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/game_state_app.h"
#include "engine/game_state/session_audio.h"
#include "engine/gconfig/sound_tables.h"
#include "engine/log/sh_log.h"
#include "engine/loop/vblank_clock.h"
#include "engine/mods/asset_files_c.h"
#include "engine/timing/vblank.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <utility>

namespace sh::audio {

namespace {

// The console's sound tasks give a line up this many blanks after its authored length.
constexpr int kWatchdogVblanks = 32;
// The disc's table keeps a line's sector and length in 24 bits.
constexpr std::uint32_t kTableFieldMask = 0xFFFFFF;
constexpr int kVoiceFiles[2] = {1, 9};

int vblank_now() { return timing::vblank_count(); }

// The voice lines count in the two television rates only.
int voice_blank_rate() { return loop::vblank_clock().rate() == 50 ? 50 : 60; }

LineStartDelay::Moment moment() {
    return {vblank_now(), game_state::frame_interval_vblanks(), loop::vblank_clock().rate()};
}

// A mod's replacement of line `line` of the disc: XA/xa_NNNN.wav.
std::optional<VoiceClip> mod_line(std::uint16_t line) {
    char name[32];
    std::snprintf(name, sizeof name, "XA/xa_%04u.wav", static_cast<unsigned>(line));
    char path[1024];
    if (!ShModAsset_Find(name, path, sizeof path)) return std::nullopt;
    Result<VoiceClip> clip = load_voice_wav(path);
    if (!clip) {
        if (!clip.error().empty()) SH_DBG("[XA] %s is not played: %s", path, clip.error().c_str());
        return std::nullopt;
    }
    return std::move(clip.value());
}

}  // namespace

void VoiceLinePlayer::start_clip(const VoiceClip& clip, bool is_file) {
    ShSound_LineStartPcm(clip.pcm.data(), clip.frames(), clip.channels, clip.rate, 0);
    ShSound_LinePause(1);
    playing_ = true;
    sounding_ = false;
    is_file_ = is_file;
    start_vblank_ = vblank_now();
}

void VoiceLinePlayer::finish() { game_sound_driver().line_finished(); }

void VoiceLinePlayer::play_line(std::uint16_t line) {
    ShSound_LineStop(1);
    const auto& lines = gconfig::active_sound_tables().tables.xa_lines;
    if (line >= lines.size() || lines[line].xa_file < kVoiceFiles[0] || lines[line].xa_file > kVoiceFiles[1]) {
        finish();
        return;
    }
    const gconfig::XaLine& entry = lines[line];
    const std::uint32_t sector = entry.sector & kTableFieldMask;
    const std::uint32_t length = entry.length & kTableFieldMask;
    if (playing_) stop();

    if (const std::optional<VoiceClip> clip = mod_line(line)) {
        start_clip(*clip, false);
        SH_DBG("[XA] line %u from a mod's file, %u Hz", static_cast<unsigned>(line), clip->rate);
    } else {
        char track[256];
        if (!ShSoundBridge_TrackAt(static_cast<int>(game_state::audio::voice_file_sector(entry.xa_file)), track,
                                   sizeof track) ||
            !ShSound_LineStart(track, sector, length)) {
            SH_LOG_WARN(AUDIO, "XA %u: no converted audio (file #%u, sector %u)", static_cast<unsigned>(line),
                        static_cast<unsigned>(entry.xa_file), static_cast<unsigned>(sector));
            finish();
            return;
        }
        ShSound_LinePause(1);
        playing_ = true;
        sounding_ = false;
        is_file_ = false;
        start_vblank_ = vblank_now();
        SH_DBG("[XA] play %u: %s sector %u, %u vblanks", static_cast<unsigned>(line), track,
               static_cast<unsigned>(sector), static_cast<unsigned>(length));
    }
    line_ = line;
    end_vblank_ = start_vblank_ + static_cast<int>(length) + kWatchdogVblanks;
    const int delay = start_delay_.take_delay();
    delay_end_vblank_ = delay != 0 ? start_vblank_ + delay : 0;
}

void VoiceLinePlayer::stop() {
    ShSound_LineStop(0);
    ShSound_LineStop(1);
    playing_ = false;
    sounding_ = false;
    delay_end_vblank_ = 0;
    finish();
}

void VoiceLinePlayer::update() {
    if (held_ || !playing_) return;
    const int now = vblank_now();
    if (delay_end_vblank_ != 0) {
        if (now < delay_end_vblank_) return;
        // The line was timed when it was asked for; its clock moves to where the sound really starts.
        end_vblank_ += now - start_vblank_;
        start_vblank_ = now;
        delay_end_vblank_ = 0;
    }
    if (!sounding_) {
        sounding_ = true;
        ShSound_LinePause(0);
    }
    if (!ShSound_LineDrained(0) || now <= end_vblank_) return;

    if (is_file_) {
        SH_DBG("[XA] finished a file after %d vblanks", now - start_vblank_);
    } else {
        SH_DBG("[XA] finished %u after %d vblanks", static_cast<unsigned>(line_), now - start_vblank_);
    }
    // The CD mix was switched on for the file only; the game's own lines switch it themselves.
    if (is_file_) ShSound_SetLineMix(0);
    playing_ = false;
    sounding_ = false;
    ShSound_LineStop(0);
    finish();
}

bool VoiceLinePlayer::sounding() const {
    if (!playing_) return false;
    // A line waiting out its start delay counts: its sound is still to come.
    return !sounding_ || !ShSound_LineDrained(0);
}

void VoiceLinePlayer::hold(bool held) {
    if (held == held_) return;
    held_ = held;
    if (held) {
        hold_start_vblank_ = vblank_now();
        ShSound_LinePause(1);
        return;
    }
    const int waited = vblank_now() - hold_start_vblank_;
    start_vblank_ += waited;
    end_vblank_ += waited;
    if (delay_end_vblank_ != 0) delay_end_vblank_ += waited;
    if (sounding_) ShSound_LinePause(0);
}

void VoiceLinePlayer::set_volume(float volume) {
    volume_ = std::clamp(volume, 0.0f, 1.0f);
    ShSound_SetLineGain(volume_);
}

bool VoiceLinePlayer::play_file(const char* path) {
    // A file that is not there must change nothing: not even cut the silence that pads the line before.
    const Result<VoiceClip> loaded = load_voice_wav(path);
    if (!loaded) {
        if (!loaded.error().empty()) SH_DBG("[XA] %s is not played: %s", path, loaded.error().c_str());
        return false;
    }
    const VoiceClip& clip = loaded.value();

    if (playing_ && !is_file_ && sounding()) {
        ShSound_LineStartPcm(clip.pcm.data(), clip.frames(), clip.channels, clip.rate, 1);
        return true;
    }
    if (playing_) stop();
    start_clip(clip, true);
    const int length = static_cast<int>(std::uint64_t{clip.frames()} * static_cast<unsigned>(voice_blank_rate()) / clip.rate);
    end_vblank_ = start_vblank_ + length;
    const int gap_ms = g_PcConfig.cutsceneLineGapMs;
    delay_end_vblank_ = gap_ms > 0 ? start_vblank_ + gap_ms * voice_blank_rate() / 1000 : 0;
    // The game's sound task switches the CD mix on for its own lines only.
    ShSound_SetLineMix(1);
    SH_DBG("[XA] file %s %u Hz, %d vblanks", path, clip.rate, length);
    return true;
}

bool VoiceLinePlayer::file_audible() const {
    if (ShSound_LineActive(1) && !ShSound_LineDrained(1)) return true;
    return playing_ && is_file_ && sounding();
}

void VoiceLinePlayer::stop_file() {
    ShSound_LineStop(1);
    if (playing_ && is_file_) {
        ShSound_SetLineMix(0);
        stop();
    }
}

void VoiceLinePlayer::stop_task_ran() { start_delay_.line_stopped(moment(), game_disc_drive().head_epoch()); }

void VoiceLinePlayer::play_task_ran(std::uint16_t line, int volume) {
    int target = 0;
    if (!game_state::xa_line_start_sector(line, &target)) {
        start_delay_.forget_delay();
        return;
    }
    DiscDrive& drive = game_disc_drive();
    const LineStartDelay::Estimate estimate = start_delay_.line_requested(
        moment(), {drive.head_lba(), drive.head_epoch()}, target, volume, g_PcConfig.cutsceneLineGapMs);
    SH_DBG("[XASEEK] xa=%u head=%d target=%d seek=%dvb stopLeft=%dvb frames=%d+%d delay=%dvb",
           static_cast<unsigned>(line), estimate.head_lba, target, estimate.seek_vblanks, estimate.stop_vblanks_left,
           LineStartDelay::kPlayFrames, estimate.seek_wait_frames, estimate.delay_vblanks);
}

VoiceLinePlayer& voice_lines() {
    static VoiceLinePlayer player;
    return player;
}

}  // namespace sh::audio
