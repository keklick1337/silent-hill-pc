#pragma once

// Developer keys: a table of keyboard bindings for the developer
// tools. Each binding watches one or more keys (any of them counts as the binding being down),
// remembers the previous frame for edge detection, can auto-repeat while held, and calls its
// action on the frames it is enabled. The game registers the bindings and their actions; the
// engine only does the bookkeeping, so new tools are one bind() away.

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace sh::devtools {

// Auto-repeat of a held key: fires on the press, then after a delay repeats, faster the longer the
// key is held, down to a shortest interval.
struct RepeatSpec {
    std::uint64_t delay_ms = 350;
    std::uint64_t first_interval_ms = 250;
    std::uint64_t interval_step_ms = 10;   // the interval shrinks by 1 ms per this many ms held past the delay
    std::uint64_t shortest_interval_ms = 100;
};

struct KeyEvent {
    bool down = false;
    bool was_down = false;
    bool fire = false;  // pressed this frame, or (repeating bindings) a repeat step

    bool pressed() const { return down && !was_down; }
    bool released() const { return !down && was_down; }
};

class DevKeys {
public:
    using IsDown = std::function<bool(int key)>;
    using Enabled = std::function<bool()>;
    using Action = std::function<void(const KeyEvent&)>;

    explicit DevKeys(RepeatSpec repeat = {}) : repeat_(repeat) {}

    // `enabled` may be empty (always). The action runs every frame the binding is enabled.
    void bind(std::vector<int> keys, bool repeats, Enabled enabled, Action action);

    // One frame: reads every bound key once, in binding order.
    void update(const IsDown& is_down, std::uint64_t now_ms);

private:
    struct Binding {
        std::vector<int> keys;
        bool repeats = false;
        Enabled enabled;
        Action action;
        bool was_down = false;
        std::uint64_t pressed_ms = 0;
        std::uint64_t last_fire_ms = 0;
    };

    bool repeat_step(Binding& b, bool down, std::uint64_t now_ms) const;

    RepeatSpec repeat_;
    std::vector<Binding> bindings_;
};

}  // namespace sh::devtools
