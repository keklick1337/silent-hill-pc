// The options' controller entry (src/engine/screens/controls_screen.h): open frames
// consume input and keep fading in, closing fades out, the leave waits for the fade.

#include "engine/screens/controls_screen.h"

#include "framework/sh_test.h"

#include <string>
#include <vector>

namespace {

namespace sc = sh::screens;

class FakeHost final : public sc::ControlsScreenHost {
public:
    bool open = true;
    std::int32_t steps[3] = {0, 0, 5};
    sc::Fade fade_{sc::FadeState::None, false};
    std::vector<std::string> calls;

    bool update_screen() override { calls.push_back("update"); return open; }
    void draw_screen() override { calls.push_back("draw"); }
    void close_screen() override { calls.push_back("close"); }
    void show_mouse_cursor() override {}
    void consume_menu_input() override { calls.push_back("consume"); }
    std::int32_t step(int l) override { return steps[l]; }
    void set_step(int l, std::int32_t v) override { steps[l] = v; }
    sc::Fade fade() override { return fade_; }
    void set_fade(sc::Fade f) override { fade_ = f; }
    void leave() override { calls.push_back("leave"); }
};

}  // namespace

SH_TEST(controls_screen_open_close_leave) {
    const sc::ControlsScreen screen;
    FakeHost host;
    screen.update(host);
    SH_CHECK(host.calls.size() == 3 && host.calls[2] == "consume" && host.fade_.state == sc::FadeState::InSteps);
    host.open = false;
    host.calls.clear();
    screen.update(host);
    SH_CHECK(host.fade_.state == sc::FadeState::OutSteps && host.steps[1] == -2 && host.steps[2] == 0);
    host.calls.clear();
    screen.update(host);
    SH_CHECK(host.calls.size() == 1 && host.calls[0] == "draw");
    host.fade_ = {sc::FadeState::OutComplete, false};
    host.calls.clear();
    screen.update(host);
    SH_CHECK(host.calls.size() == 3 && host.calls[1] == "close" && host.calls[2] == "leave");
    SH_CHECK(host.fade_.state == sc::FadeState::InStart);
}
