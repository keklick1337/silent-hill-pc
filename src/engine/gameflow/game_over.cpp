#include "engine/gameflow/game_over.h"

namespace sh::gameflow {

std::int32_t GameOver::frames(std::int32_t delta, std::int32_t& carry) const {
    carry += (4096 * delta) / rules_.frame_30fps;
    const std::int32_t whole = carry >> 12;
    carry -= whole << 12;
    return whole;
}

void GameOver::frame(GameOverHost& host) {
    // Steps fall through into the next one on the frame they finish, as in the original.
    switch (host.step(0)) {
    case 0:
        host.freeze_player();
        host.store_hold(0);
        host.count_continue();
        host.reset_menu_selection();
        tip_ = host.pick_tip();
        host.load_tip(tip_);
        steps::advance(host, 0);
        [[fallthrough]];
    case 1:
        host.fade(FadeStep::Auto, true, rules_.fast_fade);
        break;

    case 2:
        host.fade(FadeStep::Start, false, rules_.fast_fade);
        steps::advance(host, 0);
        [[fallthrough]];
    case 3:
        host.draw_title();
        host.store_hold(host.hold() + frames(host.delta(), title_carry_));
        if (host.confirm_pressed() || host.hold() > rules_.title_hold) {
            steps::advance(host, 0);
        }
        break;

    case 4:
        host.draw_title();
        host.fade(FadeStep::Auto, true, rules_.slow_fade);
        break;

    case 5:
        if (host.tips_skipped()) {
            steps::finish(host);
            break;
        }
        host.wait_files();
        host.stop_radio();
        steps::advance(host, 0);
        [[fallthrough]];
    case 6:
        host.fade(FadeStep::Auto, false, rules_.slow_fade);
        host.store_hold(0);
        host.draw_tip();
        break;

    case 7:
        host.store_hold(host.hold() + frames(host.delta(), tip_carry_));
        host.draw_tip();
        if (!host.confirm_pressed() && host.hold() <= rules_.tip_hold) {
            break;
        }
        host.tip_seen(tip_);
        steps::advance(host, 0);
        break;

    case 8:
        host.draw_tip();
        host.fade(FadeStep::Auto, true, rules_.slow_fade);
        break;

    default:
        host.unfreeze_player();
        host.leave();
        break;
    }

    if (host.step(0) >= 2 || !host.in_game()) {
        host.pause_world();
    }
}

}  // namespace sh::gameflow
