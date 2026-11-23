// The session's state machine helpers: what a state or step change starts over and what it keeps.

#include "engine/game_state/session.h"

#include "framework/sh_test.h"

namespace sg = sh::game;

namespace {

// A session that is not the running game's, filled with marks.
sg::Session marked()
{
    sg::Session s{};
    sg::SessionFlow& f = s.system.flow;
    f.sys_state = 3;
    for (int i = 0; i < 3; ++i) {
        f.sys_steps[i] = 10 + i;
        f.frame_counters[i] = 20 + i;
        s.game.game_steps[i] = 30 + i;
    }
    f.sys_step_data[0] = 40;
    f.sys_step_data[1] = 41;
    s.game.game_state = 11;
    s.game.game_state_previous = 7;
    return s;
}

}  // namespace

SH_TEST(session_sys_state_next_starts_over)
{
    sg::Session s = marked();
    s.set_sys_state_next(sg::sys_state::kPaused);
    const sg::SessionFlow& f = s.system.flow;
    SH_CHECK(s.sys_state() == sg::sys_state::kPaused);
    SH_CHECK(f.sys_steps[0] == 0 && f.sys_steps[1] == 0 && f.sys_steps[2] == 0);
    SH_CHECK(f.sys_step_data[0] == 0 && f.sys_step_data[1] == 0);
    SH_CHECK(f.frame_counters[2] == 0);
    // The screen's counters are not the in-game state's.
    SH_CHECK(f.frame_counters[0] == 20 && f.frame_counters[1] == 21);
}

SH_TEST(session_sys_steps_clear_the_levels_under_them)
{
    sg::Session s = marked();
    s.set_sys_step(1, 5);
    SH_CHECK(s.system.flow.sys_steps[0] == 10 && s.system.flow.sys_step_data[0] == 40);
    SH_CHECK(s.system.flow.sys_steps[1] == 5 && s.system.flow.sys_step_data[1] == 0 && s.system.flow.sys_steps[2] == 0);
    s.advance_sys_step(0);
    SH_CHECK(s.system.flow.sys_steps[0] == 11 && s.system.flow.sys_step_data[0] == 0);
    SH_CHECK(s.system.flow.sys_steps[1] == 0 && s.system.flow.sys_step_data[1] == 0);
    s.advance_sys_step(2);
    SH_CHECK(s.system.flow.sys_steps[2] == 1);
}

SH_TEST(session_game_state_next_remembers_the_previous_screen)
{
    sg::Session s = marked();
    s.set_game_state_next(sg::game_state::kMainMenu);
    SH_CHECK(s.game_state() == sg::game_state::kMainMenu);
    SH_CHECK(s.game.game_state_previous == 11);
    SH_CHECK(s.game.game_steps[0] == 0 && s.game.game_steps[1] == 0 && s.game.game_steps[2] == 0);
    SH_CHECK(s.system.flow.frame_counters[0] == 0 && s.system.flow.frame_counters[1] == 0);
    SH_CHECK(s.sys_state() == sg::sys_state::kGameplay && s.system.flow.sys_steps[0] == 0);

    s.set_game_step(0, 4);
    SH_CHECK(s.game.game_steps[0] == 4 && s.system.flow.frame_counters[1] == 0);

    s.system.flow.frame_counters[0] = 5;
    s.return_to_previous_screen();
    SH_CHECK(s.game_state() == 11 && s.game.game_state_previous == sg::game_state::kMainMenu);
    SH_CHECK(s.game.game_steps[0] == 0 && s.system.flow.frame_counters[0] == 0);
}

SH_TEST(session_step_levels_past_two_are_level_two)
{
    sg::Session s = marked();
    s.advance_sys_step(-1);
    SH_CHECK(s.system.flow.sys_steps[0] == 10 && s.system.flow.sys_steps[1] == 11 && s.system.flow.sys_steps[2] == 13);
    s.set_sys_step(7, 2);
    SH_CHECK(s.system.flow.sys_steps[2] == 2 && s.system.flow.sys_step_data[1] == 41);
}

SH_TEST(session_flags_and_clear)
{
    sg::Session s = marked();
    s.set_sys_flag(sg::sys_flag::kCutscene, true);
    SH_CHECK(s.sys_flag(sg::sys_flag::kCutscene) && s.cutscene_live());
    s.set_sys_flag(sg::sys_flag::kCutscene, false);
    s.system.flow.cutscene_border = sg::kCutsceneBorderNone;
    SH_CHECK(!s.cutscene_live());
    s.system.flow.cutscene_border = sg::kCutsceneBorderNone + 1;
    SH_CHECK(s.cutscene_live());
    s.clear_system();
    SH_CHECK(s.sys_state() == 0 && s.system.flow.cutscene_border == 0);
    // The game block is not the system block.
    SH_CHECK(s.game_state() == 11);
}
