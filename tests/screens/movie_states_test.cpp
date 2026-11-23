// The movie game states (src/engine/screens/movie_states.h): the shipped file, movie
// names from the manifest with the disc file as fallback, the world's sound stopped
// only for the listed movies, the intro fade-in steps and the debug player.

#include "engine/screens/movie_states.h"

#include "framework/sh_test.h"

#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace {

namespace sc = sh::screens;

std::string read(const std::string& rel) {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + rel, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

class FakeHost final : public sc::MovieHost {
public:
    std::int32_t steps[3] = {0, 0, 0};
    sc::Fade fade_{sc::FadeState::None, false};
    std::int32_t fade_step = -1;
    std::int32_t counter = 0;
    bool held = false, enter = false, cancel = false, left = false, right = false, extra = false;
    std::map<std::string, std::string> manifest;
    std::map<std::string, std::int32_t> manifest_ints;
    std::map<std::string, std::int32_t> files = {{"XA/C1_20670", 100}, {"XA/C2_20670", 101}, {"XA/M1_03500", 102},
                                                 {"XA/M4_02490", 103}, {"XA/ZC_14392", 120}, {"XA/OTHER", 105}};
    std::vector<std::string> calls;

    std::int32_t step(int l) override { return steps[l]; }
    void set_step(int l, std::int32_t v) override { steps[l] = v; }
    sc::Fade fade() override { return fade_; }
    void set_fade(sc::Fade f) override { fade_ = f; }
    void set_fade_step(std::int32_t s) override { fade_step = s; }
    void wait_vblanks(std::int32_t n) override { calls.push_back("vsync " + std::to_string(n)); }
    void set_next_state(const std::string& n) override { calls.push_back("next " + n); }
    bool any_button_held() override { return held; }
    bool clicked_enter() override { return enter; }
    bool clicked_cancel() override { return cancel; }
    bool pulsed_left() override { return left; }
    bool pulsed_right() override { return right; }
    std::int32_t frame_counter() override { return counter; }
    bool extra_option(std::int32_t bit) override { return bit == 0 && extra; }
    void start_title_loads() override { calls.push_back("title"); }
    void wait_loads() override { calls.push_back("wait"); }
    void draw_card_message() override { calls.push_back("message"); }
    void set_debug_text_position(std::int32_t x, std::int32_t y) override {
        calls.push_back("text " + std::to_string(x) + "," + std::to_string(y));
    }
    std::string manifest_string(const std::string& k) override {
        auto it = manifest.find(k);
        return it == manifest.end() ? "" : it->second;
    }
    std::int32_t manifest_int(const std::string& k) override {
        auto it = manifest_ints.find(k);
        return it == manifest_ints.end() ? 0 : it->second;
    }
    std::int32_t find_movie(const std::string& n) override {
        auto it = files.find(n);
        return it == files.end() ? -1 : it->second;
    }
    std::string movie_name(std::int32_t id) override {
        for (const auto& [n, i] : files) {
            if (i == id) {
                return n;
            }
        }
        return "";
    }
    void warn(const std::string& m) override { calls.push_back("warn " + m); }
    void silence_world_audio() override { calls.push_back("silence"); }
    void play_movie(std::int32_t id, std::int32_t frames) override {
        calls.push_back("play " + std::to_string(id) + " " + std::to_string(frames));
    }
    void after_movie() override { calls.push_back("after"); }
};

sc::MovieStates shipped() {
    auto spec = sc::parse_movie_states(read("/gamedata/menu/movies.json"));
    SH_CHECK(static_cast<bool>(spec));
    return spec ? spec.value() : sc::MovieStates{};
}

}  // namespace

SH_TEST(movie_states_shipped_file) {
    const sc::MovieStates s = shipped();
    SH_CHECK(s.fade_in_wait_vblanks == 8 && s.fade_in_hold_frames == 300 && s.fade_in_next == "MovieIntro");
    SH_CHECK(s.intro.manifest == "intro_file" && s.intro.file == "XA/C1_20670" && s.intro_extra.file == "XA/C2_20670");
    SH_CHECK(s.intro_fade_step == 4096 && s.intro_next == "MainMenu" && s.alternate_offset == 2060);
    SH_CHECK(s.opening.file == "XA/M1_03500" && s.opening_next == "MainLoadScreen" && s.exit_next == "InGame");
    SH_CHECK(s.debug_last == "XA/ZC_14392" && s.debug_text[0] == 40 && s.debug_text[1] == 40);
    SH_CHECK(s.silence_world_audio.size() == 1 && s.silence_world_audio[0] == "XA/M4_02490");
    SH_CHECK(!sc::parse_movie_states("{}"));
}

SH_TEST(movie_states_intro_and_names) {
    const sc::MovieStates spec = shipped();
    const sc::MovieScreens screens(spec);
    FakeHost host;
    screens.intro(host);
    SH_CHECK((host.calls == std::vector<std::string>{"wait", "play 100 0", "after", "next MainMenu"}));
    SH_CHECK(host.fade_step == 4096);

    // The extra option picks the other cut; the manifest may rename it.
    host.calls.clear();
    host.extra = true;
    host.manifest["intro_file_extra"] = "XA/OTHER";
    screens.intro(host);
    SH_CHECK(host.calls[1] == "play 105 0");

    // A manifest name the disc lacks: a warning, then the disc file.
    host.calls.clear();
    host.manifest["intro_file_extra"] = "XA/NOPE";
    screens.intro(host);
    SH_CHECK(host.calls[0].rfind("warn manifest \"intro_file_extra\"", 0) == 0 && host.calls[2] == "play 101 0");

    // The alternate cut: the manifest offset, 0 keeps the data's.
    host.calls.clear();
    screens.intro_alternate(host);
    SH_CHECK(host.calls[1] == "play 100 2060");
    host.calls.clear();
    host.manifest_ints["intro_frame_offset"] = 77;
    screens.intro_alternate(host);
    SH_CHECK(host.calls[1] == "play 100 77" && host.calls[3] == "next MainMenu");

    host.calls.clear();
    screens.opening(host);
    SH_CHECK(host.calls[1] == "play 102 0" && host.calls[3] == "next MainLoadScreen");
    host.calls.clear();
    screens.exit_movie(host);
    SH_CHECK((host.calls == std::vector<std::string>{"next InGame"}));
}

SH_TEST(movie_states_silence_only_listed) {
    const sc::MovieStates spec = shipped();
    const sc::MovieScreens screens(spec);
    FakeHost host;
    screens.play(host, 103, 55);
    SH_CHECK((host.calls == std::vector<std::string>{"wait", "silence", "play 103 55", "after"}));
    host.calls.clear();
    screens.play(host, 102, 0);
    SH_CHECK((host.calls == std::vector<std::string>{"wait", "play 102 0", "after"}));
}

SH_TEST(movie_states_intro_fade_in) {
    const sc::MovieStates spec = shipped();
    const sc::MovieScreens screens(spec);
    FakeHost host;
    screens.intro_fade_in(host);
    SH_CHECK((host.calls == std::vector<std::string>{"vsync 8", "title", "message"}));
    SH_CHECK(host.fade_.state == sc::FadeState::InStart && host.steps[0] == 1);
    host.counter = 300;
    screens.intro_fade_in(host);
    SH_CHECK(host.steps[0] == 1);
    host.counter = 301;
    screens.intro_fade_in(host);
    SH_CHECK(host.steps[0] == 2 && host.fade_.state == sc::FadeState::OutSteps);
    host.calls.clear();
    screens.intro_fade_in(host);
    SH_CHECK((host.calls == std::vector<std::string>{"message"}));
    host.fade_ = {sc::FadeState::OutComplete, false};
    host.calls.clear();
    screens.intro_fade_in(host);
    SH_CHECK((host.calls == std::vector<std::string>{"wait", "next MovieIntro", "message"}));
}

SH_TEST(movie_states_debug_player) {
    const sc::MovieStates spec = shipped();
    sc::MovieScreens screens(spec);
    FakeHost host;
    host.right = true;
    screens.debug_player(host);
    screens.debug_player(host);
    host.right = false;
    host.left = true;
    screens.debug_player(host);
    SH_CHECK(screens.debug_index() == 1);
    host.left = false;
    host.enter = true;
    host.cancel = true;
    host.calls.clear();
    screens.debug_player(host);
    SH_CHECK(host.calls[0] == "next Unk16" && host.calls[1] == "text 40,40" && host.calls[3] == "play 119 0");
}
