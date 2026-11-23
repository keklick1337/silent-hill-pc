#pragma once

// Soft reset watch of the game loop: decides every frame whether
// the player asked to go back to the title (a held button combination, or one of the press
// combinations) or a running demo has run long enough. The game says when a reset may not happen
// at all (boot, saving) and when only the demo time-out counts (the main menu); the buttons, the
// times and the combinations come from the game (ResetSpec).
//
// Bit-exactness contract: with the same inputs and counters, the answer and the counters equal
// the original's.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sh::loop {

// One press combination: exactly `held` held and any of `pressed` newly pressed.
struct ResetCombo {
    std::uint32_t held = 0;
    std::uint32_t pressed = 0;
    std::string name;
};

struct ResetSpec {
    std::uint32_t hold_buttons = 0;  // held together for longer than hold_vblanks: reset
    std::int32_t hold_vblanks = 0;
    std::string hold_name = "hold";
    std::int32_t demo_vblanks = 0;   // a demo longer than this goes back to the title
    std::vector<ResetCombo> combos;
};

struct ResetInput {
    bool blocked = false;        // no reset in this state (boot, a save being written or read)
    bool demo_playing = false;
    bool at_main_menu = false;   // only the demo time-out counts
    bool requested = false;      // the game itself asked for a reset
    std::uint32_t held = 0;
    std::uint32_t pressed = 0;
};

// Counters the watch reads and clears; they count vertical blanks (VBlankTimers).
struct ResetCounters {
    std::int32_t* demo_vblanks = nullptr;
    std::int32_t* hold_vblanks = nullptr;
};

class ResetWatch {
public:
    explicit ResetWatch(ResetSpec spec) : spec_(std::move(spec)) {}

    // nullptr: no reset; else why (for the journal).
    const char* check(const ResetInput& in, const ResetCounters& counters) const;

private:
    ResetSpec spec_;
};

// Counters that advance by one on every vertical blank, whatever the game is doing.
class VBlankTimers {
public:
    void add(std::int32_t* counter) { counters_.push_back(counter); }
    void tick() const {
        for (std::int32_t* c : counters_) {
            ++*c;
        }
    }

private:
    std::vector<std::int32_t*> counters_;
};

}  // namespace sh::loop
