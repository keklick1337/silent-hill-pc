// SPDX-License-Identifier: GPL-3.0-or-later
//
// The staff roll against the running game (engine/screens/staff_roll_screen.h): its clock, the display
// mode and the video memory it keeps aside, the music, the fade and the screen steps over the session, and the
// roll's order keys. The script binds and the game state call it by the StaffRoll_* names.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>

#include "engine/app/loader/game_files.h"
#include "engine/app/pc_paths.h"
#include "engine/audio/sound_registry.h"
#include "engine/game_state/session.h"
#include "engine/memory/work_buffers.h"
#include "engine/log/sh_log.h"
#include "engine/loop/frame_view.h"
#include "engine/world/fixed_trig.h"
#include "engine/screens/staff_roll_clock.h"
#include "engine/screens/screens_view.h"
#include "engine/screens/staff_roll_screen.h"
#include "engine/app/loader/picture_places.h"
#include "engine/loader/queue/disc_files.h"

extern "C" {
const char* PcPort_GetGameDataPath(void);
// The sound driver (src/audio/game/game_sound_driver.cpp).
void SD_Call(std::uint32_t cmd);
std::uint8_t Sd_AudioStreamingCheck(void);
void Sd_XaPreLoadAudioPreTaskAdd(std::uint16_t xaIdx);
// The display mode and the interlaced clear.
void Screen_Init(std::int32_t screenWidth, bool isInterlaced);
void Screen_RectInterlacedClear(std::int16_t x, std::int16_t y, std::int16_t w, std::int16_t h, std::uint8_t r,
                                std::uint8_t g, std::uint8_t b);
}

namespace {

namespace sc = sh::screens;

constexpr std::int32_t kRollState = 21;   // the game state the roll runs in
constexpr int kScreenWidth = 320;
constexpr std::uint32_t kSoundStopMusic = 18, kSoundStopEffects = 16, kSoundStopVoice = 19;
constexpr std::uint32_t kOverlaySlot = 6;
constexpr std::uint32_t kWorldBackSlot = 2047;

// The video memory the roll's display mode overwrites, kept aside while it runs (x, y, w, h, where it is kept).
struct KeptArea {
    int x, y, w, h;
    sh::memory::WorkBuffer buffer;
};
constexpr KeptArea kKept[2] = {{320, 256, 160, 240, sh::memory::WorkBuffer::MovieKeep},
                               {480, 16, 32, 480, sh::memory::WorkBuffer::StaffRollKeep}};

std::uint16_t* kept_pixels(const KeptArea& a) { return sh::memory::work_buffers().as<std::uint16_t>(a.buffer); }

class RollHost final : public sc::StaffRollScreenHost {
public:
    std::filesystem::path gamedata_dir() override { return PcPort_GetGameDataPath(); }
    std::filesystem::path mods_dir() override {
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        return mods;
    }
    void log(bool error, const std::string& line) override {
        if (error) {
            SH_ERR("%s", line.c_str());
        } else {
            SH_LOG("%s", line.c_str());
        }
    }
    std::int32_t sound_number(const std::string& name) override {
        return sh::audio::SoundRegistry::shared(gamedata_dir(), mods_dir()).id(name);
    }
    void preload_stream(std::int32_t stream) override { Sd_XaPreLoadAudioPreTaskAdd(static_cast<std::uint16_t>(stream)); }
    void play_stream(std::int32_t stream) override { SD_Call(static_cast<std::uint16_t>(stream)); }
    void submit(sc::RollLayer layer, const sh::draw2d::Command& command) override {
        if (layer == sc::RollLayer::NextFrameBack) {
            sc::submit_2d_packet(sc::Table2d::NextBack, 0, command, packet_of(command));
        } else {
            sc::submit_2d_packet(sc::Table2d::Hud, static_cast<int>(kOverlaySlot), command, packet_of(command));
        }
    }

    // The roll scrolls one row past its last line.
    std::int32_t row_count() override {
        char** lines = sc::staff_roll_screen().lines();
        std::int32_t count = 0;
        while (lines != nullptr && lines[count] != nullptr) {
            ++count;
        }
        return count + 1;
    }
    const std::uint8_t* row(std::int32_t index) override {
        return reinterpret_cast<const std::uint8_t*>(sc::staff_roll_screen().lines()[index]);
    }
    const std::uint8_t* logo() override { return sc::staff_roll_screen().logo(); }

    std::int32_t clock() override { return sc::staff_roll_clock(); }
    void set_clock(std::int32_t value) override { sc::staff_roll_clock() = value; }
    std::int32_t vblanks() override { return sh::loop::frame_vblanks(); }

    sc::EndingTiming ending(std::int32_t index) override { return sc::staff_roll_screen().ending_timing(index); }
    void preload_music(std::int32_t ending) override {
        if (const std::int32_t music = sc::staff_roll_screen().ending_music(ending); music >= 0) {
            preload_stream(music);
        }
    }
    void play_music(std::int32_t ending) override {
        if (const std::int32_t music = sc::staff_roll_screen().ending_music(ending); music >= 0) {
            play_stream(music);
        }
    }
    std::int32_t music_state() override { return Sd_AudioStreamingCheck(); }
    void stop_bgm() override { SD_Call(kSoundStopMusic); }
    void stop_sfx() override { SD_Call(kSoundStopEffects); }
    void stop_voice() override { SD_Call(kSoundStopVoice); }

    bool skip_pressed() override {
        const sh::game::GameBlock& g = sh::game::session().game;
        return (g.pads[0].clicked & g.settings.binds.skip) != 0;
    }

    void set_flat_blend_mode(std::uint32_t mode) override { sc::staff_roll_screen().set_flat_blend_mode(mode); }
    void plane_changed() override { sc::staff_roll_screen().plane_changed(); }

    std::int32_t sin(std::int32_t angle) override { return sh::world::fixed_sin(angle); }
    std::int32_t cos(std::int32_t angle) override { return sh::world::fixed_cos(angle); }

    std::int32_t step(int level) override { return sh::game::session().game.game_steps[level]; }
    void set_step(int level, std::int32_t value) override { sh::game::session().game.game_steps[level] = value; }
    void reset_step_timer() override { sh::game::session().system.flow.frame_counters[1] = 0; }
    bool came_from_gameplay() override {
        return sh::game::session().game.game_state_previous == sh::game::game_state::kInGame;
    }
    void enter_roll_state() override { sh::game::session().set_game_state_next(kRollState); }
    void return_to_previous_state() override { sh::game::session().return_to_previous_screen(); }
    std::int32_t file_queue_length() override { return sh::loader::queue_length(); }

    sc::Fade fade() override {
        const std::int32_t v = sh::screens::fade_control().status;
        if (v < 0 || v > 15) {
            return {sc::FadeState::Other, false};
        }
        return {static_cast<sc::FadeState>(v & 7), (v & 8) != 0};
    }
    void set_fade(sc::Fade f) override { sh::screens::fade_control().status = static_cast<std::int32_t>(f.state) | (f.white ? 8 : 0); }
    void set_fade_step(std::int32_t step_q12) override { sh::screens::fade_control().timestep = step_q12; }
    void set_background(std::uint32_t rgb) override {
        sh::game::Rgbx& c = sh::game::session().game.clear_color;
        c.r = rgb & 0xFF;
        c.g = (rgb >> 8) & 0xFF;
        c.b = (rgb >> 16) & 0xFF;
    }

    void save_screen() override {
        for (const KeptArea& a : kKept) sc::keep_vram(a.x, a.y, a.w, a.h, kept_pixels(a));
    }
    void clear_for_roll(bool white) override {
        const std::uint8_t c = white ? 0xFF : 0;
        Screen_RectInterlacedClear(0, 32, 512, 448, c, c, c);
    }
    void set_roll_display(bool on) override { Screen_Init(on ? 512 : kScreenWidth, on); }
    void set_frame_interval(std::int32_t vblanks) override { sh::loop::frame_interval_vblanks() = vblanks; }
    void restore_screen() override {
        Screen_RectInterlacedClear(0, 32, 320, 448, 0, 0, 0);
        for (const KeptArea& a : kKept) sc::restore_vram(a.x, a.y, a.w, a.h, kept_pixels(a));
        sc::wait_vblanks(2);
    }

private:
    // The packet the game's drawer built for each command (the packet area keeps every later drawer's addresses).
    static sc::Packet2d packet_of(const sh::draw2d::Command& command) {
        if (const auto* s = std::get_if<sh::draw2d::Sprite>(&command)) {
            return s->size == sh::draw2d::RectSize::Free ? sc::Packet2d::Sprite : sc::Packet2d::Sprite16;
        }
        if (std::holds_alternative<sh::draw2d::DrawMode>(command)) {
            return sc::Packet2d::DrawMode;
        }
        if (std::holds_alternative<sh::draw2d::Fill>(command)) {
            return sc::Packet2d::Tile;
        }
        return sc::Packet2d::PolyFT4;
    }
};

RollHost s_rollHost;

}  // namespace

namespace sh::screens {

StaffRollScreen& staff_roll_screen() {
    static StaffRollScreen screen;
    return screen;
}

}  // namespace sh::screens

extern "C" {

void GameFs_StfRollBinLoad(void) {
    sh::loader::queue_read(sh::loader::disc_files().find("VIN/STF_ROLL.BIN"),
                           sh::memory::work_buffers().data(sh::memory::WorkBuffer::FileStaging));

    // The font's page and palette are known before the read lands.
    const std::int32_t font_file = sh::loader::disc_files().find("TIM/FONT24.TIM");
    void* font = sh::memory::work_buffers().data(sh::memory::WorkBuffer::SheetImage);
    const sh::loader::ImagePlace& atlas = sh::loader::picture_places().font24_atlas;
    sh::loader::queue_read_image(font_file, font, &atlas);
    sh::screens::staff_roll_screen().set_font_image(atlas.page[1], atlas.palette_x, atlas.palette_y);
}

void GameState_StaffRoll_Update(void) { sh::screens::staff_roll_screen().update(s_rollHost); }
void StaffRoll_StartFlat(std::int32_t idx) { sh::screens::staff_roll_screen().start_flat(idx, s_rollHost); }
int StaffRoll_MusicStarted(void) { return sh::screens::staff_roll_screen().music_started(s_rollHost) ? 1 : 0; }
int StaffRoll_Finish(void) { return sh::screens::staff_roll_screen().finish(s_rollHost) ? 1 : 0; }
void StaffRoll_StartPerspective(void) { sh::screens::staff_roll_screen().start_perspective(s_rollHost); }
int StaffRoll_UpdatePerspective(void) {
    return sh::screens::staff_roll_screen().update_perspective(s_rollHost) ? 1 : 0;
}

}  // extern "C"
