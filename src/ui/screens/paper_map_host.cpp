// SPDX-License-Identifier: GPL-3.0-or-later
//
// The paper map screen's host (engine/screens/paper_map_view.h): the game state's steps, the display,
// the fades, the file reads of the page and its markings, the pad, the save's maps and flags and Harry's place,
// over the session and the engine's views. The book (gamedata/menu/map_screen.json) names pages, files and flags.

#include "engine/loader/map_list.h"
#include "engine/screens/paper_map_view.h"

#include <array>
#include <string>
#include <variant>
#include <vector>

#include "engine/app/game_view.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/app/loader/game_files.h"
#include "engine/app/pc_paths.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/ids/game_numbers.h"
#include "engine/memory/work_buffers.h"
#include "engine/log/sh_log.h"
#include "engine/loop/frame_view.h"
#include "engine/screens/paper_map.h"
#include "engine/screens/screens_view.h"
#include "engine/app/loader/picture_places.h"
#include "render/gpu/render_state.h"
#include "engine/mods/data_reload.h"

extern "C" {
const char* PcPort_GetGameDataPath(void);
float GR_Ui2dWideFactor(void);
void Screen_Init(std::int32_t screenWidth, bool isInterlaced);
void Screen_Refresh(std::int32_t screenWidth, bool isInterlaced);
void GameFs_MapItemsTextureLoad(std::int32_t mapId);
// The pictures of the open page and of its markings (their homes in video memory).
}

namespace sh::screens {
namespace {

namespace gs = sh::game_state;
using sh::game::save_record;
using sh::game::session;
using GameState = gs::GameState;

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 240;
constexpr int kSoundMapOpen = 1308;
// Pad bits the screen reads directly: the left stick's directions and the shoulder buttons.
constexpr std::uint32_t kStickUp = 1u << 24;
constexpr std::uint32_t kStickDown = 1u << 26;
constexpr std::uint32_t kShoulders = (1u << 10) | (1u << 11);  // L1, R1
// The fade's states (the low three bits of its status word).
constexpr std::int32_t kFadeIdle = 1;
constexpr std::int32_t kFadeOutComplete = 5;
constexpr std::int32_t kFadeResetTimestep = 4;

// What the page picture covers in texture memory while the map is open.
constexpr int kKeptX = kScreenWidth;
constexpr int kKeptY = 256;
constexpr int kKeptW = 192;
constexpr int kKeptH = kScreenHeight;
std::vector<std::uint16_t> s_kept(static_cast<std::size_t>(kKeptW * kKeptH));

const PaperMapBook& book() {
    static PaperMapBook s_book;
    static std::uint32_t s_book_epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(s_book_epoch)) {
        s_book = [] {
            char mods[1024];
            PcPaths_ExeFile(mods, sizeof(mods), "mods");
            std::string source;
            Result<MapScreenData> data = load_map_screen(PcPort_GetGameDataPath(), mods, &source);
            MapScreenData d;
            if (data) {
                d = std::move(data.value());
                SH_LOG("[MAPSCREEN] %s", source.c_str());
            } else {
                SH_ERR("[MAPSCREEN] %s - built-in zoom and arrows, no pages, links, anchors or marks", data.error().c_str());
            }
            PaperMapNames names;
            names.page = [](const std::string& n) { return sh::ids::paper_map_number(n.c_str()); };
            names.file = [](const std::string& n) { return static_cast<int>(sh::loader::disc_files().find(n.c_str())); };
            names.flag = [](const std::string& n) { return sh::ids::flag_number(n.c_str()); };
            names.report = [](const std::string& what) { SH_ERR("[MAPSCREEN] %s", what.c_str()); };
            return PaperMapBook::build(std::move(d), names);
        }();
    }
    return s_book;
}

std::int32_t previous_state() { return session().game.game_state_previous; }
std::int32_t state(GameState s) { return static_cast<std::int32_t>(s); }

class Host final : public PaperMapHost {
public:
    int step() override { return session().game.game_steps[0]; }
    void set_step(int step) override {
        session().game.game_steps[0] = step;
        session().system.flow.frame_counters[1] = 0;
        session().game.game_steps[1] = 0;
        session().game.game_steps[2] = 0;
    }
    bool opened_from_inventory() override { return previous_state() == state(GameState::InventoryScreen); }
    bool opened_from_game() override {
        return previous_state() == state(GameState::InGame) || previous_state() == state(GameState::LoadMapScreen);
    }
    void set_return_to_game() override { session().game.game_state_previous = state(GameState::InGame); }
    void return_to_previous() override { session().return_to_previous_screen(); }
    void frame_upkeep() override {
        update_music_track();
        update_play_timer();
    }

    void enter_display() override {
        Screen_Refresh(kScreenWidth, true);
        // The fog colour of the room would otherwise show behind the frames that do not cover the screen (the open,
        // the fades, a page turn); the inventory screen zeroes it the same way.
        session().game.clear_color.r = 0;
        session().game.clear_color.g = 0;
        session().game.clear_color.b = 0;
    }
    void leave_display() override { Screen_Init(kScreenWidth, false); }
    void set_vblank_interval(int vblanks) override { sh::loop::frame_interval_vblanks() = vblanks; }
    void store_frame() override { keep_vram(kKeptX, kKeptY, kKeptW, kKeptH, s_kept.data()); }
    void restore_frame() override { restore_vram(kKeptX, kKeptY, kKeptW, kKeptH, s_kept.data()); }
    void flush_frame_lists() override {
        sh::app::view::draw_world_list();
        wait_vblanks(0);
        sh::app::view::draw_world_list();
    }
    void reload_map_item_textures() override { GameFs_MapItemsTextureLoad(save_record().map); }
    void draw_now(const draw2d::Command& command) override { draw_2d_now(command); }
    void wait_vblank() override { wait_vblanks(0); }
    void submit(PaperMapLayer layer, const draw2d::Command& command) override {
        const int slot = layer == PaperMapLayer::Arrows ? 2 : layer == PaperMapLayer::Marks ? 3 : 4;
        Packet2d packet = Packet2d::PolyG3;
        if (std::holds_alternative<draw2d::Line>(command)) {
            packet = Packet2d::LineF4;
        } else if (const auto* p = std::get_if<draw2d::Polygon>(&command); p != nullptr && p->textured) {
            packet = Packet2d::PolyFT4;
        }
        submit_2d_packet(Table2d::World, slot, command, packet);
    }
    float wide_factor() override { return GR_Ui2dWideFactor(); }
    void note_wide_frame() override { sh::render::picture().ui2d_wide_frames = 2; }
    void pause_music() override { session().system.process.music_status |= sh::game::music_status::kPause; }

    int saved_page() override { return save_record().paper_map; }
    void load_page_image(int file) override {
        if (file >= 0) {
            sh::loader::queue_read_image(file, sh::memory::work_buffers().data(sh::memory::WorkBuffer::PaperMapSheet),
                                         &sh::loader::picture_places().paper_map);
        }
    }
    void load_marking_texture(int file) override {
        sh::loader::queue_read_image(file, sh::memory::work_buffers().data(sh::memory::WorkBuffer::FileStaging),
                                     &sh::loader::picture_places().paper_map_markings);
    }
    void seek_file(int file) override {
        if (file >= 0) sh::loader::queue_seek(file);
    }
    void finish_loads() override {
        sh::loader::queue_drain_now();
        sh::loader::queue_wait_empty();
    }
    void pump_file_queue() override { sh::loader::queue_update(); }
    void wait_file_queue() override { sh::loader::queue_wait_empty(); }
    PaperMapImage page_image() override {
        const sh::loader::ImagePlace& page = sh::loader::picture_places().paper_map;
        return {page.page[0], page.page[1], page.v, page.palette_x, page.palette_y};
    }
    std::array<std::int16_t, 2> marking_palette() override {
        const sh::loader::ImagePlace& marks = sh::loader::picture_places().paper_map_markings;
        return {marks.palette_x, marks.palette_y};
    }

    void stop_radio() override { stop_radio_sound(); }
    void play_map_sound() override { gs::sound_command(kSoundMapOpen); }
    void fade_in() override { gs::screen_fade_start(true, true, false); }
    void fade_out() override { gs::screen_fade_start(true, false, false); }
    bool fade_idle() override { return fade_status() == kFadeIdle; }
    bool fade_done() override { return (fade_status() & 0x7) == kFadeOutComplete; }
    void reset_fade_step() override { set_fade_status(kFadeResetTimestep); }

    PaperMapInput input() override {
        const sh::game::PadState& pad = session().game.pads[0];
        const sh::game::PadBinds& keys = session().game.settings.binds;
        PaperMapInput in;
        in.cancel = (pad.clicked & keys.cancel) != 0;
        in.map = (pad.clicked & keys.map) != 0;
        in.enter = (pad.clicked & keys.enter) != 0;
        in.up = (pad.clicked & kStickUp) != 0;
        in.down = (pad.clicked & kStickDown) != 0;
        in.hold_arrow = (pad.held & kShoulders) != 0;
        in.stick_x = pad.sticks_past_dead_zone.left_x;
        in.stick_y = pad.sticks_past_dead_zone.left_y;
        return in;
    }
    // The page bits run on into the words after the first (the save's event flags), as the game reads them.
    bool has_page(int page) override {
        const std::uint32_t* words = &save_record().paper_maps;
        return ((words[page / 32] >> (page % 32)) & 1u) != 0;
    }
    bool flag(int number) override { return save_record().event_flag(number); }
    PaperMapPlayer player() override {
        const sh::entities::CharacterRecord& harry = session().system.characters.player;
        return {harry.position.x, harry.position.z, harry.rotation.y, sh::loader::map_name(save_record().map)};
    }
    std::int32_t sin(std::int32_t angle) override { return gs::sin_q12(angle); }
    std::int32_t cos(std::int32_t angle) override { return gs::cos_q12(angle); }
};

PaperMapScreen s_screen;

}  // namespace

void run_paper_map_screen() {
    Host host;
    s_screen.update(host, book());
}

void draw_paper_map_page(int left, int top, int scale) {
    Host host;
    const PaperMapWideSpan span = paper_map_scene_span(host.wide_factor());
    if (span.on) host.note_wide_frame();
    paper_map_draw_page(host, book(),
                        {static_cast<std::uint16_t>(left), static_cast<std::uint16_t>(top),
                         static_cast<std::uint16_t>(scale)},
                        span.on ? &span : nullptr);
}

bool draw_paper_map_marks(int mode, int page, int pick, int shade, int left, int top, int scale) {
    Host host;
    return paper_map_draw_marks(host, book(), mode, page, pick, shade,
                                {static_cast<std::uint16_t>(left), static_cast<std::uint16_t>(top),
                                 static_cast<std::uint16_t>(scale)});
}

std::int32_t draw_paper_map_player(int page, int left, int top, int scale) {
    Host host;
    return paper_map_draw_player(host, book(), page,
                                 {static_cast<std::uint16_t>(left), static_cast<std::uint16_t>(top),
                                  static_cast<std::uint16_t>(scale)});
}

int paper_map_image_file(int page) {
    const PaperMapPage* p = book().page(page);
    return p != nullptr ? p->image_file : -1;
}

int paper_map_marking_file(int page) {
    const PaperMapPage* p = book().page(page);
    return p != nullptr ? p->marking_file : -1;
}

}  // namespace sh::screens

// The game state table names the screen by this entry point.
extern "C" void GameState_PaperMapScreen_Update(void) { sh::screens::run_paper_map_screen(); }
