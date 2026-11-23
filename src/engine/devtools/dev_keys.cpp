#include "engine/devtools/dev_keys.h"

namespace sh::devtools {

void DevKeys::bind(std::vector<int> keys, bool repeats, Enabled enabled, Action action) {
    Binding b;
    b.keys = std::move(keys);
    b.repeats = repeats;
    b.enabled = std::move(enabled);
    b.action = std::move(action);
    bindings_.push_back(std::move(b));
}

bool DevKeys::repeat_step(Binding& b, bool down, std::uint64_t now_ms) const {
    if (down && !b.was_down) {
        b.pressed_ms = now_ms;
        b.last_fire_ms = now_ms;
        return true;
    }
    if (!down || !b.was_down) {
        return false;
    }
    const std::uint64_t held = now_ms - b.pressed_ms;
    if (held < repeat_.delay_ms) {
        return false;
    }
    const std::uint64_t speedup = (held - repeat_.delay_ms) / repeat_.interval_step_ms;
    std::uint64_t interval =
        speedup >= repeat_.first_interval_ms ? 0 : repeat_.first_interval_ms - speedup;
    if (interval < repeat_.shortest_interval_ms) {
        interval = repeat_.shortest_interval_ms;
    }
    if (now_ms - b.last_fire_ms >= interval) {
        b.last_fire_ms = now_ms;
        return true;
    }
    return false;
}

void DevKeys::update(const IsDown& is_down, std::uint64_t now_ms) {
    for (Binding& b : bindings_) {
        bool down = false;
        for (int key : b.keys) {
            down = down || is_down(key);
        }
        if (!b.enabled || b.enabled()) {
            KeyEvent ev;
            ev.down = down;
            ev.was_down = b.was_down;
            ev.fire = b.repeats ? repeat_step(b, down, now_ms) : ev.pressed();
            b.action(ev);
        }
        // Edges are tracked whether or not the binding is enabled, so enabling it while a key is
        // held does not count as a press.
        b.was_down = down;
    }
}

}  // namespace sh::devtools
