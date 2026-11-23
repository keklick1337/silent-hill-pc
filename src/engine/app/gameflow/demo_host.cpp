// SPDX-License-Identifier: GPL-3.0-or-later
//
// The demo player's host on the session (demo_host.h): settings, the first pad, the flags, the fade, the file queue
// by disc path and the game's random numbers.

#include "engine/app/gameflow/demo_host.h"

#include <cstring>
#include <string>

#include "engine/app/game_view.h"
#include "engine/app/loader/game_files.h"
#include "engine/app/pc_paths.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/flow_data.h"
#include "engine/memory/work_buffers.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/log/sh_log.h"
#include "engine/save/console_records.h"
#include "engine/loop/frame_view.h"
#include "engine/loop/game_random.h"
#include "engine/screens/screens_view.h"
#include "engine/text/game_text.h"

extern "C" const char* PcPort_GetGameDataPath(void);

namespace sh::gameflow {
namespace {

using sh::game::session;

// The setup file holds the settings, then at this offset the save the demo starts from, both in the console's byte
// form (engine/save/console_records.h).
constexpr std::size_t kSetupSaveOffset = 0x100;
constexpr int kVolumeMin = 0;
constexpr int kVolumeMax = 128;

std::uint8_t* setup() { return sh::memory::work_buffers().as<std::uint8_t>(sh::memory::WorkBuffer::DemoSetup); }

sh::game::SaveRecord setup_save()
{
    return sh::save::console::decode_save_record({setup() + kSetupSaveOffset, sh::save::console::kSaveRecordBytes});
}

class SessionDemoHost final : public DemoHost {
public:
    bool demo_flag() const override { return session().sys_flag(sh::game::sys_flag::kDemo); }
    void set_demo_flag(bool on) override { session().set_sys_flag(sh::game::sys_flag::kDemo, on); }
    void request_warm_reset() override { session().set_sys_flag(sh::game::sys_flag::kWarmReset, true); }
    void set_demo_timer(std::int32_t vblanks) override { sh::loop::demo_vblanks() = vblanks; }

    std::int32_t fade_status() const override { return sh::screens::fade_status(); }
    std::int32_t game_state() const override { return session().game.game_state; }
    std::int32_t previous_game_state() const override { return session().game.game_state_previous; }
    std::int32_t sys_state() const override { return session().system.flow.sys_state; }

    std::uint16_t pad_buttons() const override { return session().game.pads[0].raw.buttons; }
    // A recorded report is the device's 8 bytes: status, kind, the button word (little endian), the four axes.
    void pad_from_recording(const std::uint8_t* report) override
    {
        sh::game::RawPad& pad = session().game.pads[0].raw;
        pad.status = report[0];
        pad.kind = report[1];
        pad.buttons = static_cast<std::uint16_t>(report[2] | report[3] << 8);
        pad.right_x = report[4];
        pad.right_y = report[5];
        pad.left_x = report[6];
        pad.left_y = report[7];
    }
    void pad_neutral(std::uint16_t buttons) override
    {
        sh::game::RawPad& raw = session().game.pads[0].raw;
        raw.status = 0x00;
        raw.kind = 0x73;  // analog, 3 words
        raw.buttons = buttons;
        raw.right_x = raw.right_y = raw.left_x = raw.left_y = 0x80;
    }
    void step_error(std::int32_t expected, std::int32_t actual) override
    {
        sh::text::GameText& text = sh::text::game_text();
        text.set_debug_position(8, 80);
        text.draw_debug("STEP ERROR:[H:");
        text.draw_debug(text.debug_number(2, expected));
        text.draw_debug("]/[M:");
        text.draw_debug(text.debug_number(2, actual));
        text.draw_debug("]");
    }

    void apply_demo_settings() override
    {
        sh::game::Settings& s = session().game.settings;
        user_ = s;
        s = sh::save::console::decode_settings({setup(), sh::save::console::kSettingsBytes});
        s.screen_x = user_.screen_x;
        s.screen_y = user_.screen_y;
        s.mono = user_.mono;
        s.music_volume = kVolumeMin;
        s.sfx_volume = user_.sfx_volume;
        s.vibration = 0;
        s.brightness = user_.brightness;
        sh::screens::set_sound_volumes(kVolumeMin, kVolumeMin, s.sfx_volume);
    }
    void restore_settings() override
    {
        session().game.settings = user_;
        sh::screens::set_sound_volumes(kVolumeMax, user_.music_volume, user_.sfx_volume);
    }
    void mark_started() override
    {
        session().game.demo_marks[0] = 1;
        session().game.demo_marks[1] = 1;
    }

    std::uint32_t random_seed() const override { return sh::loop::game_random().seed(); }
    void set_random_seed(std::uint32_t seed) override { sh::loop::game_random().set_seed(seed); }

    std::uint8_t* setup_buffer() override { return setup(); }
    std::int32_t file_size(const std::string& path) const override
    {
        return static_cast<std::int32_t>(sh::loader::disc_files().bytes(sh::loader::disc_files().find(path)));
    }
    void queue_read(const std::string& path, void* dest) override
    {
        const std::int32_t file = sh::loader::disc_files().find(path);
        if (file == sh::loader::DiscFiles::kNone) {
            SH_ERR("[DEMO] no file %s on the disc", path.c_str());
            return;
        }
        sh::loader::queue_read(file, dest);
    }
    std::uintptr_t map_overlay_end() const override
    {
        const std::int32_t first = sh::loader::disc_files().find("VIN/MAP0_S00.BIN");
        const std::int32_t map = setup_save().map;
        const std::int32_t overlay = static_cast<std::int32_t>(sh::loader::disc_files().bytes(first + map));
        return sh::app::view::map_overlay_base() + static_cast<std::uintptr_t>(overlay);
    }

    bool near_active_cell(std::int32_t x, std::int32_t z) const override { return sh::app::view::near_active_cell(x, z); }

private:
    sh::game::Settings user_{};
};

}  // namespace

DemoPlayer& demo_player()
{
    static DemoPlayer p = [] {
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        std::string err;
        DemoRules rules = load_config<DemoRules>(PcPort_GetGameDataPath(), mods, "demos.json", &err);
        if (!err.empty()) {
            SH_ERR("[DEMO] config/demos.json: %s (built-in rules)", err.c_str());
        }
        return DemoPlayer(std::move(rules));
    }();
    return p;
}

DemoHost& demo_host()
{
    static SessionDemoHost host;
    return host;
}

void demo_save_apply()
{
    session().game.save = setup_save();
}

}  // namespace sh::gameflow
