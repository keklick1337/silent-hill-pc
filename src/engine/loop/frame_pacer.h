#pragma once

// Frame pacing of the game loop: how long the loop waits at the
// end of a frame. The clock is the vertical blank counter of the display (60 a second); a frame
// waits for whole vertical blanks, or, for a frame rate the vertical blank cannot express (above
// 60, or not a divisor of 60), for the high-resolution timer (unless the display paces it: FpsChoice). Returns how
// many vertical blanks the frame took, which the frame clock then credits.
//
// Three kinds of frame:
//   - a demo plays back at the interval it was recorded with;
//   - a fixed frame (scenes, most menus) waits exactly one vertical blank;
//   - a capped frame (gameplay, unlocked menus) waits for the player's frame rate cap.
//
// Bit-exactness contract: with the same host answers, the waits and the counts equal the
// original's.

#include <cstdint>

namespace sh::loop {

// What the pacer waits on, supplied by the platform.
class PacerHost {
public:
    virtual ~PacerHost() = default;
    virtual int vblank_count() = 0;     // vertical blanks so far
    virtual void wait_vblank() = 0;     // returns after the next vertical blank
    virtual std::uint64_t ticks_ms() = 0;
    virtual std::uint64_t perf_counter() = 0;
    virtual std::uint64_t perf_frequency() = 0;
    virtual void sleep_ms(std::uint32_t ms) = 0;
};

// The frame rate the player asked for, and what the frame is.
struct FpsChoice {
    int locked_refresh = 0;  // > 0: vsync with an explicit refresh rate paces the frame at that rate
    bool uncapped = false;   // no cap (config 0 or the debug toggle)
    int cap = 0;             // frames per second
    bool menu = false;       // a menu with nothing simulated under it: never slower than vblank_rate
    // The present waits for the display, which runs at the cap (a phone: FIFO, the display asked for the rate):
    // a cap above vblank_rate is then paced by the present alone, as a timer of its own would beat against it.
    bool display_paced = false;
};

struct PacerSpec {
    int vblank_rate = 0;               // vertical blanks per second
    std::uint64_t fixed_backstop_ms = 0;  // longest wait for the fixed frame's own vertical blank
};

class FramePacer {
public:
    explicit FramePacer(const PacerSpec& spec) : spec_(spec) {}

    // `interval` is the recorded one, `min_interval` the game's own minimum.
    int demo_frame(PacerHost& host, int interval, int min_interval);

    // One vertical blank. Under fast-forward the count runs on its own and nothing more is awaited.
    int fixed_frame(PacerHost& host, bool fast_forward);

    // `fade_wait`: a screen fade is running and gets one vertical blank first. `apply_cap`: the cap
    // replaces the game's own minimum of `game_min` vertical blanks.
    int capped_frame(PacerHost& host, bool fade_wait, bool apply_cap, int game_min, const FpsChoice& fps);

    // The frame rate a choice works out to; 0 = uncapped.
    int target_fps(const FpsChoice& fps) const;

private:
    void wait_on_timer(PacerHost& host, int fps);

    PacerSpec spec_;
    int last_count_ = 0;
    std::uint64_t timer_start_ = 0;
};

}  // namespace sh::loop
