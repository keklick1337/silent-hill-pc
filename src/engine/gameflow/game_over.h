#pragma once

// The game over sequence: fade out, "GAME OVER" until a button
// or the hold time, fade to black, then (except on the hardest difficulty) one of the game over tips
// until a button or its hold time, and a warm boot back into the game. Holds count 30 fps frames
// from the frame time, so they last as long at any frame rate.

#include <cstdint>

#include "engine/gameflow/event_steps.h"

#include "engine/game_state/session.h"

namespace sh::gameflow {

struct GameOverRules {
    std::int32_t title_hold = 0;     // 30 fps frames the title stays at most (held while <= this)
    std::int32_t tip_hold = 0;       // 30 fps frames a tip stays at most
    std::int32_t fast_fade = 0;      // fade rates, 1/seconds in Q12
    std::int32_t slow_fade = 0;
    std::int32_t frame_30fps = 0;    // Q12 seconds of one 30 fps frame
};

// How a fade step of the sequence runs (the game's "advance the step after a fade" helper).
enum class FadeStep : std::uint8_t {
    Start,  // start the fade now
    Auto,   // the sub-step counter drives start, wait and advance
};

// The sequence runs on the state's step counters (event_steps.h); the hold counter counts frames.
class GameOverHost : public StepHost {
public:
    virtual void freeze_player() = 0;
    virtual void unfreeze_player() = 0;
    virtual void count_continue() = 0;
    virtual void reset_menu_selection() = 0;
    virtual int pick_tip() = 0;
    virtual void load_tip(int tip) = 0;
    virtual void tip_seen(int tip) = 0;

    virtual void fade(FadeStep how, bool out, std::int32_t rate) = 0;  // out = fade the picture out
    virtual void draw_title() = 0;
    virtual void draw_tip() = 0;
    virtual bool confirm_pressed() const = 0;
    virtual std::int32_t delta() const = 0;

    virtual bool tips_skipped() const = 0;  // the difficulty that shows no tips
    virtual void wait_files() = 0;
    virtual void stop_radio() = 0;

    virtual void leave() = 0;            // back to gameplay and the warm boot
    // the in-game game state is still the active one
    virtual bool in_game() const { return sh::game::session().game_state() == sh::game::game_state::kInGame; }
    virtual void pause_world() { sh::game::session().system.process.music_status |= sh::game::music_status::kPause; }
};

class GameOver {
public:
    void set_rules(const GameOverRules& r) { rules_ = r; }
    void frame(GameOverHost& host);

private:
    // Adds the frame time to `carry` and returns the whole 30 fps frames in it.
    std::int32_t frames(std::int32_t delta, std::int32_t& carry) const;

    GameOverRules rules_{};
    int tip_ = 0;
    std::int32_t title_carry_ = 0;
    std::int32_t tip_carry_ = 0;
};

}  // namespace sh::gameflow
