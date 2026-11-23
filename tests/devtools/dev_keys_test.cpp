// The developer key table (engine/devtools/dev_keys.h): press
// edges, any-of keys, enabling while held, and the accelerating auto-repeat.

#include "engine/devtools/dev_keys.h"

#include "framework/sh_test.h"

#include <set>

namespace {

namespace dt = sh::devtools;

struct Keyboard {
    std::set<int> down;
    dt::DevKeys::IsDown reader() {
        return [this](int k) { return down.count(k) != 0; };
    }
};

}  // namespace

SH_TEST(devtools_keys_edges) {
    dt::DevKeys keys;
    Keyboard kb;
    int fired = 0, released = 0;
    bool enabled = true;
    keys.bind({1, 2}, false, [&] { return enabled; }, [&](const dt::KeyEvent& ev) {
        fired += ev.fire ? 1 : 0;
        released += ev.released() ? 1 : 0;
    });
    keys.update(kb.reader(), 0);
    kb.down = {2};
    keys.update(kb.reader(), 16);
    keys.update(kb.reader(), 32);
    SH_CHECK(fired == 1);
    kb.down = {1, 2};  // still the same binding held
    keys.update(kb.reader(), 48);
    SH_CHECK(fired == 1);
    kb.down = {};
    keys.update(kb.reader(), 64);
    SH_CHECK(released == 1);
    // Pressed while disabled, then enabled while held: no press.
    enabled = false;
    kb.down = {1};
    keys.update(kb.reader(), 80);
    enabled = true;
    keys.update(kb.reader(), 96);
    SH_CHECK(fired == 1);
}

SH_TEST(devtools_keys_repeat) {
    dt::DevKeys keys;
    Keyboard kb;
    std::vector<std::uint64_t> at;
    std::uint64_t now = 0;
    keys.bind({7}, true, {}, [&](const dt::KeyEvent& ev) {
        if (ev.fire) at.push_back(now);
    });
    kb.down = {7};
    for (now = 1000; now <= 4000; now += 10) {
        keys.update(kb.reader(), now);
    }
    // The press, nothing before 350 ms, then 250 ms falling towards 100 ms.
    SH_CHECK(at.size() > 5);
    SH_CHECK(at[0] == 1000);
    SH_CHECK(at[1] == 1350);
    SH_CHECK(at[2] - at[1] < 250 && at[2] - at[1] >= 200);
    SH_CHECK(at.back() - at[at.size() - 2] == 100);
}
