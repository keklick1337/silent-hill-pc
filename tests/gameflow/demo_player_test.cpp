// Attract-mode demo playback (engine/gameflow/demo_player): the rotation, the settings and seed swap,
// stepping the recording, leaving the demo, and demos.json.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/game_state/game_state.h"
#include "engine/gameflow/demo_player.h"

#include "framework/sh_test.h"

namespace gf = sh::gameflow;
namespace gs = sh::game_state;

namespace {

class Host final : public gf::DemoHost {
public:
    bool flag = false, reset = false;
    std::int32_t timer = -1, fade = 0, state = static_cast<int>(gs::GameState::InGame), prev = 0, sys = 0;
    std::uint16_t buttons = 0xFFFF;
    std::uint8_t pad[8] = {};
    std::uint32_t seed = 77;
    std::vector<std::string> log;
    std::int32_t play_bytes = 100;
    std::uint8_t memory[8192] = {};

    bool demo_flag() const override { return flag; }
    void set_demo_flag(bool on) override { flag = on; }
    void request_warm_reset() override { reset = true; }
    void set_demo_timer(std::int32_t v) override { timer = v; }
    std::int32_t fade_status() const override { return fade; }
    std::int32_t game_state() const override { return state; }
    std::int32_t previous_game_state() const override { return prev; }
    std::int32_t sys_state() const override { return sys; }
    std::uint16_t pad_buttons() const override { return buttons; }
    void pad_from_recording(const std::uint8_t* r) override { std::memcpy(pad, r, 8); log.push_back("pad_recorded"); }
    void pad_neutral(std::uint16_t) override { log.push_back("pad_neutral"); }
    void step_error(std::int32_t, std::int32_t) override { log.push_back("step_error"); }
    void apply_demo_settings() override { log.push_back("settings_demo"); }
    void restore_settings() override { log.push_back("settings_restore"); }
    void mark_started() override { log.push_back("started"); }
    std::uint32_t random_seed() const override { return seed; }
    void set_random_seed(std::uint32_t s) override { seed = s; }
    std::uint8_t* setup_buffer() override { return memory + 4096; }
    std::int32_t file_size(const std::string&) const override { return play_bytes; }
    void queue_read(const std::string& path, void*) override { log.push_back("read " + path); }
    std::uintptr_t map_overlay_end() const override { return reinterpret_cast<std::uintptr_t>(memory) + 1000; }
    bool near_active_cell(std::int32_t, std::int32_t) const override { return false; }

    void set_setup(std::uint32_t frames, std::uint16_t start_seed) {
        std::memcpy(setup_buffer() + 0x7F8, &frames, 4);
        std::memcpy(setup_buffer() + 0x7FC, &start_seed, 2);
    }
};

}  // namespace

SH_TEST(demo_player_rotation_wraps_both_ways) {
    gf::DemoPlayer p;
    p.advance(0);
    SH_CHECK(p.setup_file() == "MISC/DEMO0009.DAT" && p.play_file() == "MISC/PLAY0009.DAT");
    p.advance(-1);
    SH_CHECK(p.index() == 4 && p.setup_file() == "MISC/DEMO0005.DAT");
    p.advance(7);
    SH_CHECK(p.index() == 1 && p.play_file() == "MISC/PLAY000A.DAT");
}

// A read sent to a place of no size would land on the setup buffer and run past it.
SH_TEST(demo_player_places_no_buffer_without_a_recording) {
    Host h;
    gf::DemoPlayer p;
    std::uint8_t here = 0;
    p.set_play_buffer(&here);
    SH_CHECK(!p.place_play_buffer(h));  // no demo selected yet
    SH_CHECK(p.play_buffer() == &here);
    p.advance(0);
    h.play_bytes = 0;  // the disc has no such file
    SH_CHECK(!p.place_play_buffer(h));
    SH_CHECK(p.play_buffer() == &here);
    h.play_bytes = 100;
    SH_CHECK(p.place_play_buffer(h));
}

SH_TEST(demo_player_buffer_and_reads) {
    Host h;
    gf::DemoPlayer p;
    p.advance(0);
    SH_CHECK(p.place_play_buffer(h));  // 100 bytes round up to one sector under the setup buffer
    SH_CHECK(p.play_buffer() == h.memory + 4096 - 2048);
    p.read_setup(h);
    p.read_play(h);
    SH_CHECK(h.log.size() == 2 && h.log[0] == "read MISC/DEMO0009.DAT" && h.log[1] == "read MISC/PLAY0009.DAT");
}

SH_TEST(demo_player_start_stop_swaps_seed_and_settings) {
    Host h;
    h.set_setup(10, 0x1234);
    gf::DemoPlayer p;
    p.start(h);
    SH_CHECK(h.flag && p.playing() && h.seed == 0x1234u);
    p.stop(h);
    SH_CHECK(!h.flag && !p.playing() && h.seed == 77u);
    SH_CHECK(h.log == (std::vector<std::string>{"settings_demo", "started", "settings_restore"}));
}

SH_TEST(demo_player_steps_and_exits) {
    Host h;
    h.set_setup(2, 5);
    gf::DemoPlayer p;
    p.advance(0);
    p.place_play_buffer(h);
    auto* frames = static_cast<std::uint8_t*>(p.play_buffer());
    for (int i = 0; i < 2; i++) {
        frames[i * 16 + 8] = static_cast<std::uint8_t>(h.state);
        frames[i * 16 + 9] = 2;
        const std::uint32_t s = 1000u + static_cast<std::uint32_t>(i);
        std::memcpy(frames + i * 16 + 12, &s, 4);
    }
    h.flag = true;

    SH_CHECK(p.update(h) && p.has_frame() && p.step() == 1);
    SH_CHECK(p.feed_pad(h) && h.timer == 0);
    SH_CHECK(p.apply_frame_seed(h) && h.seed == 1000u);
    SH_CHECK(p.update_present_interval() && p.present_interval() == 2);

    h.fade = 3;  // fading out: the recording waits
    SH_CHECK(p.update(h) && !p.has_frame() && p.step() == 1);
    h.fade = 0;
    SH_CHECK(p.update(h));  // the fade of the previous frame still blocks
    SH_CHECK(!p.has_frame());
    SH_CHECK(p.update(h) && p.step() == 2);
    SH_CHECK(!p.update(h) && h.reset && h.timer == 999 * 60 && p.step() == 0);  // the recording ran out
}

SH_TEST(demo_player_exit_rules_and_buttons) {
    Host h;
    h.set_setup(100, 0);
    gf::DemoPlayer p;
    p.advance(0);
    p.place_play_buffer(h);
    h.flag = true;
    h.sys = static_cast<int>(gs::SysState::GameOver);
    SH_CHECK(!p.update(h) && h.reset);

    h.reset = false;
    h.sys = 0;
    h.state = static_cast<int>(gs::GameState::MainMenu);  // not a recorded state: nothing happens
    SH_CHECK(p.update(h) && !p.has_frame() && p.step() == 0);

    h.buttons = 0xFFFE;  // a press leaves the demo
    SH_CHECK(p.feed_pad(h) && h.reset);
    h.flag = false;
    SH_CHECK(!p.feed_pad(h));
    SH_CHECK(p.spawn_allowed(h, 1, 2));
}

SH_TEST(demo_player_seed_passes) {
    Host h;
    gf::DemoPlayer p;
    h.flag = true;
    h.seed = 10;
    p.back_up_seed(h);
    h.seed = 99;
    p.restore_seed(h);
    SH_CHECK(h.seed == 10u);
    p.advance_seed(h);
    SH_CHECK(h.seed == 10u + 0x3C6EF35Fu);
}

SH_TEST(demo_rules_parse) {
    auto ok = gf::DemoRules::parse(R"({"rotation": [{"setup": "MISC/A.DAT", "play": "MISC/B.DAT"}],
        "step_states": ["InGame"], "exit": {"in_state": "InGame", "on_sys_state": "GameOver", "after_state": "SaveScreen"},
        "exit_timer_vblanks": 5, "seed_advance": 7})");
    SH_CHECK(ok && ok.value().rotation.size() == 1 && ok.value().exit_timer == 5 && ok.value().seed_advance == 7u);
    SH_CHECK(!gf::DemoRules::parse(R"({"rotation": []})"));
    SH_CHECK(!gf::DemoRules::parse(R"({"step_states": ["Nowhere"]})"));
    SH_CHECK(!gf::DemoRules::parse(R"({"exit": {"in_state": "InGame"}})"));
    const gf::DemoRules b = gf::DemoRules::builtin();
    SH_CHECK(b.rotation.size() == 5 && b.step_states.size() == 6);
}

SH_TEST(demo_rules_gamedata_matches_builtin) {
    std::FILE* f = std::fopen(SH_PROJECT_ROOT "/gamedata/config/demos.json", "rb");
    SH_CHECK(f != nullptr);
    if (f == nullptr) return;
    std::string text;
    char buf[4096];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof(buf), f)) > 0;) text.append(buf, n);
    std::fclose(f);
    auto parsed = gf::DemoRules::parse(text);
    SH_CHECK(parsed);
    if (!parsed) return;
    const gf::DemoRules& a = parsed.value();
    const gf::DemoRules b = gf::DemoRules::builtin();
    SH_CHECK(a.rotation.size() == b.rotation.size());
    for (std::size_t i = 0; i < a.rotation.size() && i < b.rotation.size(); i++) {
        SH_CHECK(a.rotation[i].setup == b.rotation[i].setup && a.rotation[i].play == b.rotation[i].play);
    }
    SH_CHECK(a.step_states == b.step_states && a.exit_from_state == b.exit_from_state &&
             a.exit_on_sys_state == b.exit_on_sys_state && a.exit_after_state == b.exit_after_state &&
             a.exit_timer == b.exit_timer && a.seed_advance == b.seed_advance);
}
