// The boot state (engine/gameflow/boot_machine.h) on a recording host: one update from every recorded starting
// point against the recorded calls and results, the boot sequence in words, and the handler table parser.

#include <string>
#include <vector>

#include "engine/gameflow/boot_machine.h"

#include "framework/golden.h"
#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {

constexpr const char* kTable = "gameflow/boot_machine.txt";

struct World {
    std::int32_t steps[3] = {0, 0, 0};
    std::int32_t counters[3] = {0, 0, 0};
    int state = 0, prev = -1;
    bool skip = false, streaming = false, fade = false;
    std::string log;

    void call(const std::string& name) {
        if (!log.empty()) log += ',';
        log += name;
    }
    std::string results() const {
        return std::to_string(steps[0]) + " " + std::to_string(steps[1]) + " " + std::to_string(steps[2]) + " " +
               std::to_string(counters[0]) + " " + std::to_string(counters[1]) + " " + std::to_string(state) + " " +
               std::to_string(prev);
    }
};

class Host final : public gf::BootHost {
public:
    explicit Host(World& w) : w_(w) {}
    gf::LoadView view() override { return {w_.steps, w_.counters}; }
    int game_state() const override { return w_.state; }
    void game_state_set(int s) override {
        w_.state = s;
        w_.call("game_state_set:" + std::to_string(s));
    }
    void game_state_previous_set(int s) override { w_.prev = s; }
    bool skip_intros() const override { return w_.skip; }
    bool audio_streaming() const override { return w_.streaming; }
    bool fade_finished() const override { return w_.fade; }
    void audio_call(int t) override { w_.call("audio_call:" + std::to_string(t)); }
    void pad_hook(int a) override { w_.call("pad_hook:" + std::to_string(a)); }
    void background_black() override { w_.call("background_black"); }
    void screen_init() override { w_.call("screen_init"); }
    void graphics_queue() override { w_.call("graphics_queue"); }
    void intro_loads_run() override { w_.call("intro_loads_run"); }
    void fade_in_start() override { w_.call("fade_in_start"); }
    void queue_wait_empty() override { w_.call("queue_wait_empty"); }
    void play_state_gameplay_next() override { w_.call("play_state_gameplay_next"); }
    void settings_restore_defaults() override { w_.call("settings_restore_defaults"); }

private:
    World& w_;
};

std::string tick(World& w) {
    w.log.clear();
    Host host(w);
    gf::BootMachine(host, gf::BootParams::builtin()).update();
    return w.log;
}

}  // namespace

SH_TEST(boot_machine_every_start_as_recorded) {
    const auto rows = sh::test::golden_rows(kTable, "tick");
    SH_CHECK(rows.size() == 1536);
    int differences = 0;
    for (const auto& r : rows) {
        World w;
        w.steps[0] = static_cast<std::int32_t>(r.num(0));
        w.steps[1] = static_cast<std::int32_t>(r.num(1));
        w.steps[2] = static_cast<std::int32_t>(r.num(2));
        w.counters[0] = static_cast<std::int32_t>(r.num(3));
        w.counters[1] = static_cast<std::int32_t>(r.num(4));
        w.state = static_cast<int>(r.num(5));
        w.skip = r.flag(6);
        w.streaming = r.flag(7);
        w.fade = r.flag(8);
        const std::string calls = tick(w);
        std::string want;
        for (std::size_t i = 10; i < 17; ++i) want += (i > 10 ? " " : "") + r.str(i);
        if (calls != r.str(9) || w.results() != want) {
            if (++differences <= 3) sh::test::golden_mismatch(kTable, r, calls + " " + w.results());
        }
    }
    SH_CHECK(differences == 0);
}

// A boot from the first tick to the next game state: the screen is set up, the base sound tasks start one per tick
// while the driver is not streaming, the graphics are queued and the fade starts, and when the fade is over the
// machine hands over to the next state with its steps cleared. The pad hook closes every tick.
SH_TEST(boot_machine_runs_the_boot_in_order) {
    World w;
    SH_CHECK(tick(w) == "background_black,screen_init,pad_hook:1" && w.steps[0] == 1);

    w.streaming = true;  // the driver is busy: nothing starts
    SH_CHECK(tick(w) == "pad_hook:1" && w.steps[0] == 1 && w.steps[1] == 0);
    w.streaming = false;
    const std::vector<std::int32_t> tasks = gf::BootParams::builtin().base_audio_tasks;
    SH_CHECK(tasks.size() == 2);
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        SH_CHECK(tick(w) == "audio_call:" + std::to_string(tasks[i]) + ",pad_hook:1");
        SH_CHECK(w.steps[0] == 1 && w.steps[1] == static_cast<std::int32_t>(i + 1));
    }
    SH_CHECK(tick(w) == "pad_hook:1" && w.steps[0] == 2 && w.steps[1] == 0);

    SH_CHECK(tick(w) == "graphics_queue,fade_in_start,pad_hook:1" && w.steps[0] == 3);

    SH_CHECK(tick(w) == "pad_hook:1" && w.steps[0] == 3);  // the fade is still running
    w.fade = true;
    w.counters[0] = 5;
    w.counters[1] = 6;
    SH_CHECK(tick(w) == "queue_wait_empty,play_state_gameplay_next,game_state_set:1,pad_hook:1");
    SH_CHECK(w.state == 1 && w.prev == 0 && w.steps[0] == 0 && w.steps[1] == 0 && w.steps[2] == 0);
    SH_CHECK(w.counters[0] == 0 && w.counters[1] == 0);
}

// With the intros skipped the boot runs what the logos would have loaded and goes straight to the main menu with
// the default settings.
SH_TEST(boot_machine_skips_the_intros) {
    World w;
    w.skip = true;
    w.steps[0] = 2;
    SH_CHECK(tick(w) == "graphics_queue,intro_loads_run,fade_in_start,pad_hook:1");
    w.fade = true;
    SH_CHECK(tick(w) ==
             "queue_wait_empty,play_state_gameplay_next,settings_restore_defaults,game_state_set:7,pad_hook:1");
    SH_CHECK(w.state == 7 && w.prev == 0 && w.steps[0] == 0);
    SH_CHECK(gf::state_handler_name(gf::BootParams::builtin(), 7) == "MainMenu");
}

SH_TEST(boot_params_parse) {
    auto p = gf::BootParams::builtin();
    SH_CHECK(p.state_handlers.size() == 22);
    SH_CHECK(gf::state_handler_name(p, 11) == "InGame");
    SH_CHECK(gf::state_handler_name(p, 22).empty());
    auto ok = gf::BootParams::parse(R"({"base_audio_tasks":[5],"state_handlers":["A","B"]})");
    SH_CHECK(ok && ok.value().base_audio_tasks.size() == 1 && ok.value().state_handlers.size() == 2);
    SH_CHECK(!gf::BootParams::parse(R"({"base_audio_tasks":[0]})"));
    SH_CHECK(!gf::BootParams::parse(R"({"state_handlers":[]})"));
}
