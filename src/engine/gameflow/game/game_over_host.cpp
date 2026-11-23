// The game's game over sequence on the engine (src/engine/gameflow/game_over.h section
// 9): the steps run on the state's step counters, the tips come from gamedata/config/game_over_tips.json.

#include <cstdint>

#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/gameflow/game/step_hosts.h"
#include "engine/gameflow/game_over.h"
#include "engine/text/game_text.h"

extern "C" {
std::int32_t GameFlow_GameOverTipPick(unsigned int darkScene);
std::int32_t GameFlow_GameOverTipFile(std::int32_t tip);
void GameFlow_GameOverTipSeen(std::int32_t tip);
void MainMenu_SelectedOptionIdxReset(void);
void Event_ScreenFadeCmd(std::int32_t cmd, unsigned int fadeOut, std::int32_t fadeType, std::int32_t fadeTimestep,
                         unsigned int incSubStep);
void Game_RadioSoundStop(void);
}

namespace {

namespace gf = sh::gameflow;
namespace link = sh::gameflow::game;
namespace gs = sh::game_state;

// Game data of the sequence.
constexpr std::int32_t kContinueCountMax = 99;
constexpr std::int32_t kTitleHold = 240;  // Q12(1/17): 30 fps frames (8 s)
constexpr std::int32_t kTipHold = 480;    // 30 fps frames (16 s)
constexpr std::int32_t kFadeCmdStart = 0;
constexpr std::int32_t kFadeCmdAuto = 2;
constexpr std::int32_t kFadeBlack = 0;
constexpr int kDifficultyHard = 1;

class GameOverGameHost final : public link::SessionSteps<gf::GameOverHost> {
public:
    void freeze_player() override { link::overlay_freeze_player(); }
    void unfreeze_player() override { link::overlay_unfreeze_player(); }
    void count_continue() override { link::autosave_count_continue(kContinueCountMax); }
    void reset_menu_selection() override { MainMenu_SelectedOptionIdxReset(); }
    int pick_tip() override {
        // Dark scenes favour the tips meant for them.
        return GameFlow_GameOverTipPick((sh::game::session().system.environment.current.preset.flags & 0x3) != 0);
    }
    void load_tip(int tip) override { link::read_image(GameFlow_GameOverTipFile(tip), link::Picture::DeathTip); }
    void tip_seen(int tip) override { GameFlow_GameOverTipSeen(tip); }

    void fade(gf::FadeStep how, bool out, std::int32_t rate) override {
        Event_ScreenFadeCmd(how == gf::FadeStep::Auto ? kFadeCmdAuto : kFadeCmdStart, out, kFadeBlack, rate, false);
    }
    void draw_title() override {
        sh::text::game_text().set_position(link::screen_position_x(32.5f), link::screen_position_y(43.5f));
        sh::text::game_text().draw(const_cast<char*>("\aGAME_OVER"), link::kDefaultMessageLength);
    }
    void draw_tip() override { link::draw_picture(link::Picture::DeathTip); }
    bool confirm_pressed() const override {
        const auto& binds = sh::game::session().game.settings.binds;
        return (sh::game::session().game.pads[0].clicked & (binds.enter | binds.cancel)) != 0;
    }
    std::int32_t delta() const override { return gs::delta_time(); }

    bool tips_skipped() const override { return gs::save_difficulty() == kDifficultyHard; }
    void wait_files() override { link::wait_file_queue(); }
    void stop_radio() override { Game_RadioSoundStop(); }

    void leave() override {
        sh::game::session().set_sys_state_next(sh::game::sys_state::kGameplay);
        link::warm_boot();
    }
};

gf::GameOver& sequence() {
    static gf::GameOver s = [] {
        gf::GameOver made;
        gf::GameOverRules r;
        r.title_hold = kTitleHold;
        r.tip_hold = kTipHold;
        r.fast_fade = 2048;   // Q12 0.5
        r.slow_fade = 8192;   // Q12 2.0
        r.frame_30fps = link::kTimestep30Fps;
        made.set_rules(r);
        return made;
    }();
    return s;
}

}  // namespace

extern "C" void SysState_GameOver_Update(void) {
    GameOverGameHost host;
    sequence().frame(host);
}
