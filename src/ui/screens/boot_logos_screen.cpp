// SPDX-License-Identifier: GPL-3.0-or-later
//
// The boot logo game states on the engine (src/engine/screens/boot_logos.h; screens, timings and skip rules in
// gamedata/menu/boot_logos.json): the host gives the logos the session's pad and steps, the file queue by disc path,
// the save card check, the fade and its own frame, and draws the logo records as sprites of the 2D draw list.
// GameState_KonamiLogo_Update and GameState_KcetLogo_Update are the game state table's entries.

#include "engine/save/game_saves.h"
#include "input/pad.h"
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include <SDL3/SDL_scancode.h>

#include "engine/app/game_view.h"
#include "engine/app/gameflow/demo_host.h"
#include "engine/app/gameflow/game_flow.h"
#include "engine/app/loader/game_files.h"
#include "engine/app/loop/frame_loop.h"
#include "engine/app/pc_paths.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/state_names.h"
#include "engine/memory/work_buffers.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/log/sh_log.h"
#include "engine/loop/frame_view.h"
#include "engine/platform/window.h"
#include "engine/screens/boot_logos.h"
#include "engine/screens/screens_view.h"
#include "engine/timing/vblank.h"
#include "render/draw2d/draw2d.h"
#include "engine/mods/data_reload.h"

extern "C" {
const char* PcPort_GetGameDataPath(void);
extern const unsigned char* g_sdlKeyboardState;
}

namespace {

namespace sc = sh::screens;
namespace gs = sh::game_state;
namespace view = sh::app::view;
using sh::game::session;

constexpr int kScreenWidth = 320;
constexpr std::int32_t kFrameStep = 4096 / 60;  // Q12 of a 60 Hz frame
// The packet area of the logo frames: two halves of 32 KB, one per display buffer.
constexpr std::size_t kPacketBufferBytes = 0x8000;

void* buffer(sh::memory::WorkBuffer b, std::size_t offset = 0) { return sh::memory::work_buffers().data(b, offset); }
std::int32_t file(const char* path) { return sh::loader::disc_files().find(path); }

std::optional<sc::BootLogos> s_logos;
std::uint32_t s_logos_epoch = sh::mods::kNeverRead;

const sc::BootLogos& logos()
{
    if (sh::mods::data_stale(s_logos_epoch)) {
        s_logos.reset();
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        std::string source;
        sh::Result<sc::BootLogos> loaded = sc::load_boot_logos(PcPort_GetGameDataPath(), mods, &source);
        if (loaded) {
            s_logos.emplace(std::move(loaded.value()));
            SH_LOG("[MENU] boot logos from %s", source.c_str());
        } else {
            // No logos then: the screens still run their loads and the card check.
            SH_ERR("[MENU] %s - the boot logos are not shown", loaded.error().c_str());
            s_logos.emplace();
            sc::LogoScreen konami;
            konami.name = konami.image = "konami";
            konami.next_state = "KcetLogo";
            sc::LogoScreen kcet;
            kcet.name = kcet.image = "kcet";
            kcet.card_check = true;
            s_logos->screens = {konami, kcet};
        }
    }
    return *s_logos;
}

// The game's pad flags are not fed this early in boot, so the logos read the keyboard and the pad's skip buttons
// themselves (any confirm or start key, or the pad's A/Start).
bool boot_skip_pressed()
{
    if (session().game.pads[0].held != 0) return true;
    const unsigned char* ks = g_sdlKeyboardState;
    if (ks != nullptr && (ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER] || ks[SDL_SCANCODE_RETURN2] ||
                          ks[SDL_SCANCODE_SPACE] || ks[SDL_SCANCODE_C] || ks[SDL_SCANCODE_V])) {
        return true;
    }
    return sh::input::skip_held();
}

std::int32_t state_of(gs::GameState s) { return static_cast<std::int32_t>(s); }

// Where the boot goes once the card-check screen has faded out (set by the card outcome; kept across runs of the
// screen).
std::uint8_t s_next_state = 0;

// The texture page word of the GPU: colour depth, x / 64, y / 256 (and bit 9 of y in bit 11).
std::uint16_t texture_page_word(int depth, std::uint32_t x, std::uint32_t y)
{
    return static_cast<std::uint16_t>(((depth & 0x3) << 7) | ((y & 0x100) >> 4) | ((x & 0x3FF) >> 6) | ((y & 0x200) << 2));
}

std::uint16_t palette_word(const sh::loader::ImagePlace& p)
{
    return static_cast<std::uint16_t>((p.palette_y << 6) | ((p.palette_x >> 4) & 0x3F));
}

class Host final : public sc::BootLogoHost {
public:
    explicit Host(gs::GameState state) : state_(state) {}

    bool active() override { return session().game.game_state == state_of(state_); }
    void poll_input() override { sh::loop::pad_update(); }
    bool skip_pressed() override { return boot_skip_pressed(); }
    bool any_button_held() override { return session().game.pads[0].held != 0; }
    std::int32_t frame_counter() override { return session().system.flow.frame_counters[0]; }

    std::int32_t step(int level) override { return session().game.game_steps[level]; }
    void set_step(int level, std::int32_t value) override { session().game.game_steps[level] = value; }
    void reset_counters() override
    {
        session().system.flow.frame_counters[1] = 0;
        session().game.game_steps[1] = 0;
        session().game.game_steps[2] = 0;
    }

    sc::Fade fade() override
    {
        const std::int32_t v = sc::fade_status();
        if (v < 0 || v > 15) return {sc::FadeState::Other, false};
        return {static_cast<sc::FadeState>(v & 7), (v & 8) != 0};
    }
    void set_fade(sc::Fade f) override { sc::set_fade_status(static_cast<std::int32_t>(f.state) | (f.white ? 8 : 0)); }
    void set_fade_step(std::int32_t step_q12) override { sc::set_fade_timestep(step_q12); }

    void prepare(const std::string& screen) override
    {
        if (screen == "konami") {
            view::screen_init_mode(kScreenWidth * 2, true);
        } else if (screen == "kcet") {
            sh::app::settings_restore_defaults();
        }
    }

    void start_loads(const std::string& screen) override
    {
        if (screen == "konami") {
            // The KCET logo of the next screen, Harry, the base animations.
            sh::loader::queue_read_image(file("1ST/KONAMI2.TIM"), buffer(sh::memory::WorkBuffer::FileStaging), view::picture(view::Picture::KcetLogo));
            view::harry_model_load();
            view::item_textures_load();
            view::map_effect_textures_load();
            sh::loader::queue_read(file("ANIM/HB_BASE.ANM"), buffer(sh::memory::WorkBuffer::PlayerBaseAnim));
        } else if (screen == "kcet") {
            view::world_effect_textures_load();
            sh::loader::queue_read(file("BG/HP_SAFE1.BIN"), buffer(sh::memory::WorkBuffer::CloseUpModel));
            sh::loader::queue_read(file("BG/S__SAFE2.BIN"), buffer(sh::memory::WorkBuffer::SaveScreenData));
        }
    }

    void wait_loads() override { sh::loader::queue_wait_empty(); }

    sc::CardOutcome check_cards() override
    {
        switch (sh::save::game_saves().boot_check()) {
            case 1: return sc::CardOutcome::NoSave;
            case 2: return sc::CardOutcome::HasSave;
            default: return sc::CardOutcome::Pending;
        }
    }

    void wait_vblank() override { sh::timing::wait_vblank(); }

    void card_outcome(sc::CardOutcome outcome) override
    {
        switch (outcome) {
            case sc::CardOutcome::NoSave:
                sh::app::stream_overlay_load();
                sh::app::title_graphics_seek();
                s_next_state = state_of(gs::GameState::MovieIntro);
                break;
            case sc::CardOutcome::HasSave:
                load_last_save();
                break;
            default:
                break;
        }
    }

    void finish_card_check() override
    {
        sh::app::settings_apply_screen_and_volume();
        view::screen_init_mode(kScreenWidth, false);

        if (s_next_state == state_of(gs::GameState::AutoLoadSavegame)) {
            sh::loader::queue_read_image(file("TIM/SAVELOAD.TIM"), buffer(sh::memory::WorkBuffer::SaveLoadImage),
                                         view::picture(view::Picture::ItemInspection));
        } else if (s_next_state != state_of(gs::GameState::MovieIntroFadeIn)) {
            sh::app::title_graphics_load();
        }

        sh::gameflow::demo_player().advance(0);
        sh::gameflow::demo_player().read_setup(sh::gameflow::demo_host());
        sh::loader::queue_wait_empty();

        sh::game::Session& s = session();
        s.system.flow.frame_counters[0] = 0;
        s.system.flow.frame_counters[1] = 0;
        s.game.game_steps[1] = 0;
        s.game.game_steps[2] = 0;
        s.set_sys_state_next(sh::game::sys_state::kGameplay);

        const std::int32_t left = s.game.game_state;
        s.game.game_state = s_next_state;
        s.game.game_state_previous = left;
        s.game.game_steps[0] = 0;
    }

    void set_next_state(const std::string& name) override
    {
        const std::int32_t state = sh::gameflow::game_state_by_name(name);
        if (state < 0) {
            SH_ERR("[MENU] boot_logos.json: unknown game state \"%s\"", name.c_str());
            session().set_game_state_next(state_of(gs::GameState::KcetLogo));
            return;
        }
        session().set_game_state_next(state);
    }

    void draw_segment(const std::string& image, const sc::LogoSegment& s, std::int32_t order) override
    {
        const sh::loader::ImagePlace& img = *view::picture(image == "kcet" ? view::Picture::KcetLogo : view::Picture::KonamiLogo);
        // One sprite of a logo image with its texture page in the same slot (the page drawn first).
        const std::uint32_t page = img.page[1] + static_cast<std::uint32_t>(s.image_x >> 8) +
                                   (static_cast<std::uint32_t>(s.image_y >> 8) << 4);
        sh::draw2d::Sprite sprite;
        sprite.w = static_cast<std::uint16_t>(s.width);
        sprite.h = static_cast<std::uint16_t>(s.height);
        sprite.uv = {static_cast<std::uint8_t>(s.image_x & 0xFF), static_cast<std::uint8_t>(s.image_y & 0xFF)};
        sprite.palette = sh::draw2d::Palette::from_word(palette_word(img));
        sprite.at = {static_cast<std::int16_t>(static_cast<std::uint16_t>(s.x)), static_cast<std::int16_t>(s.y)};
        sc::submit_2d(sc::Table2d::Menu, order, sprite);

        sh::draw2d::DrawMode mode;
        mode.page = sh::draw2d::TexturePage::from_word(texture_page_word(img.page[0], page << 6, ((page >> 4) & 1) << 8));
        mode.dither = true;
        sc::submit_2d(sc::Table2d::Menu, order, mode);
    }

    void draw_backdrop(std::uint32_t rgb, const std::int32_t (&r)[4], std::int32_t order) override
    {
        sh::draw2d::Fill fill;
        fill.color = {static_cast<std::uint8_t>(rgb), static_cast<std::uint8_t>(rgb >> 8), static_cast<std::uint8_t>(rgb >> 16)};
        fill.at = {static_cast<std::int16_t>(r[0]), static_cast<std::int16_t>(r[1])};
        fill.w = static_cast<std::uint16_t>(r[2]);
        fill.h = static_cast<std::uint16_t>(r[3]);
        sc::submit_2d(sc::Table2d::Menu, order, fill);
    }

    void end_frame() override
    {
        sc::run_screen_fade();
        sh::loader::queue_update();
        sh::timing::wait_vblank();
        sh::loop::frame_delta() = kFrameStep;
        sh::loop::frame_delta_raw() = kFrameStep;
        view::swap_buffers();
        view::draw_ui_list();
        sh::platform::end_frame();

        const int index = view::frame_buffer_begin();
        view::frame_lists_clear(buffer(sh::memory::WorkBuffer::PacketArea, static_cast<std::uint32_t>(index) * kPacketBufferBytes));
    }

private:
    // The options of the newest save are read; with auto-load on, the boot goes to it.
    static void load_last_save()
    {
        std::int32_t* steps = session().game.game_steps;
        while (steps[1] < 3) {
            switch (steps[1]) {
                case 0:
                    sh::save::game_saves().boot_begin_load();
                    steps[2] = 0;
                    steps[1]++;
                    [[fallthrough]];
                case 1:
                    if (sh::save::game_saves().boot_poll_load()) {
                        steps[2] = 0;
                        steps[1]++;
                    }
                    break;
                case 2:
                    if (session().game.settings.auto_load) {
                        sh::loader::queue_read(file("VIN/SAVELOAD.BIN"), buffer(sh::memory::WorkBuffer::FileStaging));
                        sh::loader::queue_seek(file("TIM/SAVELOAD.TIM"));
                        s_next_state = state_of(gs::GameState::AutoLoadSavegame);
                    } else {
                        sh::app::stream_overlay_load();
                        sh::app::title_graphics_seek();
                        s_next_state = state_of(gs::GameState::MovieIntro);
                    }
                    steps[2] = 0;
                    steps[1]++;
                    break;
            }
            sh::timing::wait_vblank();
        }
    }

    gs::GameState state_;
};

void run_screen(gs::GameState state, const char* name)
{
    static Host s_konami_host(gs::GameState::KonamiLogo);
    static Host s_kcet_host(gs::GameState::KcetLogo);
    const bool kcet = state == gs::GameState::KcetLogo;
    const sc::BootLogos& all = logos();
    const sc::LogoScreen* screen = all.find(name);
    if (screen == nullptr) {
        // A mod's file without this screen: run it without a logo.
        SH_ERR("[MENU] boot_logos.json has no screen \"%s\"", name);
        static sc::LogoScreen s_empty[2];
        sc::LogoScreen& empty = s_empty[kcet ? 1 : 0];
        empty.name = empty.image = name;
        empty.card_check = kcet;
        empty.next_state = "KcetLogo";
        screen = &empty;
    }
    // The screen object lives for the run: the card screen's skip latch is kept.
    static std::optional<sc::BootLogoScreen> s_screens[2];
    std::optional<sc::BootLogoScreen>& slot = s_screens[kcet ? 1 : 0];
    if (!slot) slot.emplace(all, *screen);
    slot->run(kcet ? s_kcet_host : s_konami_host);
}

}  // namespace

extern "C" void GameState_KonamiLogo_Update(void) { run_screen(gs::GameState::KonamiLogo, "konami"); }

extern "C" void GameState_KcetLogo_Update(void) { run_screen(gs::GameState::KcetLogo, "kcet"); }

// The Konami and KCET logos stay point-sampled under the Menus filter: each is several abutting sprites, and a
// bilinear tap at a sprite's edge reads the next one's texel, so the seams and the border showed as a faint outline.
// Keyed on the two logo palettes and only while their screen is up.
extern "C" int BootScreen_IsLogoPrim(int clut)
{
    const int c = clut & 0xFFFF;
    const std::int32_t state = session().game.game_state;
    if (state != state_of(gs::GameState::KonamiLogo) && state != state_of(gs::GameState::KcetLogo)) return 0;
    return c == palette_word(*view::picture(view::Picture::KonamiLogo)) ||
           c == palette_word(*view::picture(view::Picture::KcetLogo));
}
