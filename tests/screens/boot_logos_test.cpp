// The boot logo screens (src/engine/screens/boot_logos.h): the shipped file, the plain
// logo's fade-in, hold and fade-out, the card-check screen's outcome and finish, and
// both skip rules.

#include "engine/screens/boot_logos.h"

#include "framework/sh_test.h"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace sc = sh::screens;

std::string read(const std::string& rel) {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + rel, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

class FakeHost final : public sc::BootLogoHost {
public:
    std::int32_t steps[3] = {0, 7, 7};
    sc::Fade fade_{sc::FadeState::None, false};
    std::int32_t fade_step = -1;
    std::int32_t counter = 0;
    bool held = false;
    bool skip = false;
    int frames_left = 1;
    std::vector<sc::CardOutcome> cards;  // check results in order
    std::vector<std::string> calls;

    bool active() override { return frames_left-- > 0; }
    void poll_input() override { calls.push_back("input"); }
    bool skip_pressed() override { return skip; }
    bool any_button_held() override { return held; }
    std::int32_t frame_counter() override { return counter; }
    std::int32_t step(int l) override { return steps[l]; }
    void set_step(int l, std::int32_t v) override { steps[l] = v; }
    void reset_counters() override { calls.push_back("reset"); steps[1] = steps[2] = 0; }
    sc::Fade fade() override { return fade_; }
    void set_fade(sc::Fade f) override { fade_ = f; }
    void set_fade_step(std::int32_t s) override { fade_step = s; }
    void prepare(const std::string& s) override { calls.push_back("prepare " + s); }
    void start_loads(const std::string& s) override { calls.push_back("loads " + s); }
    void wait_loads() override { calls.push_back("wait"); }
    sc::CardOutcome check_cards() override {
        calls.push_back("check");
        const sc::CardOutcome c = cards.front();
        cards.erase(cards.begin());
        return c;
    }
    void wait_vblank() override { calls.push_back("vblank"); }
    void card_outcome(sc::CardOutcome o) override { calls.push_back("outcome " + std::to_string(int(o))); }
    void finish_card_check() override { calls.push_back("finish"); }
    void set_next_state(const std::string& n) override { calls.push_back("next " + n); }
    void draw_segment(const std::string& image, const sc::LogoSegment& s, std::int32_t order) override {
        calls.push_back("seg " + image + " " + std::to_string(s.image_x) + "," + std::to_string(s.image_y) + " " +
                        std::to_string(s.width) + "x" + std::to_string(s.height) + " @" + std::to_string(s.x) + "," +
                        std::to_string(s.y) + " " + std::to_string(order));
    }
    void draw_backdrop(std::uint32_t rgb, const std::int32_t (&r)[4], std::int32_t order) override {
        calls.push_back("backdrop " + std::to_string(rgb) + " " + std::to_string(r[0]) + "," + std::to_string(r[1]) +
                        " " + std::to_string(r[2]) + "x" + std::to_string(r[3]) + " " + std::to_string(order));
    }
    void end_frame() override { calls.push_back("end"); }
};

sc::BootLogos shipped() {
    auto logos = sc::parse_boot_logos(read("/gamedata/menu/boot_logos.json"));
    SH_CHECK(static_cast<bool>(logos));
    return logos ? logos.value() : sc::BootLogos{};
}

}  // namespace

SH_TEST(boot_logos_shipped_file) {
    const sc::BootLogos logos = shipped();
    SH_CHECK(logos.screens.size() == 2);
    const sc::LogoScreen* konami = logos.find("konami");
    const sc::LogoScreen* kcet = logos.find("kcet");
    SH_CHECK(konami != nullptr && kcet != nullptr);
    if (konami == nullptr || kcet == nullptr) {
        return;
    }
    // Q12(0.2f), Q12(0.4f), Q12(8.0f): float products truncated.
    SH_CHECK(konami->fade_in_step == 819 && konami->fade_out_step == 819 && konami->skip_fade_step == 1638);
    SH_CHECK(kcet->skip_fade_step == 32768 && kcet->skip_latched && kcet->card_check);
    SH_CHECK(konami->hold_frames == 180 && konami->next_state == "KcetLogo" && !konami->card_check);
    SH_CHECK(konami->segments.size() == 4 && kcet->segments.size() == 2);
    SH_CHECK(konami->segments[3].image_x == 256 && konami->segments[3].image_y == 256 &&
             konami->segments[3].width == 128 && konami->segments[3].x == 64 && konami->segments[3].y == 64);
    SH_CHECK(kcet->segments[1].width == 160 && kcet->segments[1].x == 48 && kcet->segments[1].y == -80);
    SH_CHECK(logos.backdrop_rgb == 0xF8F8F8u && logos.order == 15);
    SH_CHECK(logos.backdrop[0] == -320 && logos.backdrop[1] == -240 && logos.backdrop[2] == 640 &&
             logos.backdrop[3] == 480);
    SH_CHECK(!sc::parse_boot_logos("{\"screens\": [], \"backdrop_rgb\": \"#000000\", \"backdrop\": [0,0,1,1], "
                                   "\"order\": 1}"));
}

SH_TEST(boot_logos_plain_screen_steps) {
    const sc::BootLogos logos = shipped();
    if (logos.find("konami") == nullptr) {
        return;
    }
    sc::BootLogoScreen screen(logos, *logos.find("konami"));
    FakeHost host;
    screen.frame(host);
    SH_CHECK(host.calls[0] == "input" && host.calls[1] == "prepare konami" && host.calls[2] == "loads konami");
    SH_CHECK(host.fade_.state == sc::FadeState::InStart && host.fade_step == 819 && host.steps[0] == 1);
    // Draw: four segments, then the backdrop, then the frame.
    SH_CHECK(host.calls[3] == "seg konami 0,0 256x256 @-192,-192 15");
    SH_CHECK(host.calls[7] == "backdrop 16316664 -320,-240 640x480 15" && host.calls[8] == "end");

    host.fade_ = {sc::FadeState::InSteps, false};
    screen.frame(host);
    SH_CHECK(host.steps[0] == 1);
    host.fade_ = {sc::FadeState::None, false};
    screen.frame(host);
    SH_CHECK(host.steps[0] == 2);
    host.counter = 180;
    screen.frame(host);
    SH_CHECK(host.steps[0] == 2);
    host.counter = 181;
    screen.frame(host);
    SH_CHECK(host.steps[0] == 3 && host.fade_.state == sc::FadeState::OutSteps);
    host.calls.clear();
    host.fade_ = {sc::FadeState::OutComplete, false};
    screen.frame(host);
    SH_CHECK(host.calls[1] == "wait" && host.calls[2] == "next KcetLogo");

    // run(): frames while the state lasts.
    host.frames_left = 3;
    host.calls.clear();
    screen.run(host);
    int ends = 0;
    for (const std::string& c : host.calls) {
        ends += c == "end";
    }
    SH_CHECK(ends == 3);
}

SH_TEST(boot_logos_plain_skip) {
    const sc::BootLogos logos = shipped();
    if (logos.find("konami") == nullptr) {
        return;
    }
    sc::BootLogoScreen screen(logos, *logos.find("konami"));
    FakeHost host;
    host.skip = true;
    screen.frame(host);  // the init is not skipped
    SH_CHECK(host.steps[0] == 1 && host.fade_.state == sc::FadeState::InStart);
    host.fade_ = {sc::FadeState::InSteps, false};
    screen.frame(host);
    SH_CHECK(host.steps[0] == 3 && host.fade_.state == sc::FadeState::OutSteps && host.fade_step == 1638);
    host.skip = false;
    host.fade_step = 0;
    screen.frame(host);
    SH_CHECK(host.fade_step == 0 && host.steps[0] == 3);
}

SH_TEST(boot_logos_card_screen) {
    const sc::BootLogos logos = shipped();
    if (logos.find("kcet") == nullptr) {
        return;
    }
    sc::BootLogoScreen screen(logos, *logos.find("kcet"));
    SH_CHECK(screen.hold_step() == 6 && screen.finish_step() == 7);
    FakeHost host;
    screen.frame(host);
    SH_CHECK(host.calls[1] == "prepare kcet" && host.calls[2] == "loads kcet" && host.steps[0] == 1);
    // Faded in: wait for the loads, then poll the cards with a vblank per check.
    host.fade_ = {sc::FadeState::None, false};
    host.cards = {sc::CardOutcome::Pending, sc::CardOutcome::Pending, sc::CardOutcome::NoSave};
    host.calls.clear();
    screen.frame(host);
    const std::vector<std::string> expect = {"input", "wait", "check", "vblank", "check", "vblank",
                                             "check", "vblank"};
    SH_CHECK(std::vector<std::string>(host.calls.begin(), host.calls.begin() + 8) == expect);
    SH_CHECK(host.steps[0] == 4);
    host.calls.clear();
    screen.frame(host);
    SH_CHECK(host.calls[1] == "outcome 4" && host.calls[2] == "reset" && host.steps[0] == 6 && host.steps[1] == 0);
    host.held = true;
    screen.frame(host);
    SH_CHECK(host.steps[0] == 7 && host.fade_step == 819);
    host.fade_ = {sc::FadeState::OutComplete, true};
    host.calls.clear();
    screen.frame(host);
    SH_CHECK(host.calls[1] == "finish");
}

SH_TEST(boot_logos_card_screen_skip_latch) {
    const sc::BootLogos logos = shipped();
    if (logos.find("kcet") == nullptr) {
        return;
    }
    sc::BootLogoScreen screen(logos, *logos.find("kcet"));
    FakeHost host;
    screen.frame(host);
    host.fade_ = {sc::FadeState::InSteps, false};
    host.skip = true;  // a tap during the fade-in
    screen.frame(host);
    SH_CHECK(host.fade_step == 32768 && host.steps[0] == 1 && screen.skip_latched());
    host.skip = false;
    host.fade_ = {sc::FadeState::None, false};
    host.fade_step = 0;
    host.cards = {sc::CardOutcome::NoCard};
    screen.frame(host);  // faded in: the fade step stays, the cards are checked
    SH_CHECK(host.fade_step == 0 && host.steps[0] == 2);
    screen.frame(host);
    SH_CHECK(host.steps[0] == 6);
    screen.frame(host);  // the latch cuts the hold
    SH_CHECK(host.steps[0] == 7 && host.fade_.state == sc::FadeState::OutSteps && host.fade_step == 32768);
}
