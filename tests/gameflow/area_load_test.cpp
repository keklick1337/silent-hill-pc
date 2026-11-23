// Room and map loads against their specification (the original's load state): the out-fade gate with its
// real-time budget, then the load in order, as call sequences on a fake host.

#include <string>
#include <vector>

#include "engine/gameflow/area_load.h"

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {

class Host final : public gf::AreaLoadHost {
public:
    int s0 = 0;
    std::int32_t s1 = 0;
    bool can = true, is_demo = false, done = false;
    std::int32_t raw = 0;
    gf::AreaTarget t{};
    std::vector<std::string> log;

    bool can_load() const override { return can; }
    int step() const override { return s0; }
    void set_step(int v) override { s0 = v; }
    std::int32_t waited() const override { return s1; }
    void set_waited(std::int32_t v) override { s1 = v; }
    void door_opened() override { log.push_back("door"); }
    bool demo() const override { return is_demo; }
    bool fade_done() const override { return done; }
    void start_out_fade() override { log.push_back("fade"); }
    void fade_progress() override { log.push_back("progress"); }
    std::int32_t raw_delta() const override { return raw; }
    void set_delta(std::int32_t d) override { log.push_back("delta " + std::to_string(d)); }
    void begin_load() override { log.push_back("begin"); }
    gf::ArrivalPoint arrival_target() const override {
        gf::ArrivalPoint p;
        p.x = 1000;
        p.z = 2000;
        p.half_width_q4 = 1;
        return p;
    }
    gf::ArrivalPoint door_point() const override {
        gf::ArrivalPoint p;
        p.x = 100;
        p.z = 100;
        return p;
    }
    std::int32_t player_x() const override { return 150; }
    std::int32_t player_z() const override { return 90; }
    void set_arrival(const gf::ArrivalPoint& p) override {
        log.push_back("arrive " + std::to_string(p.x) + " " + std::to_string(p.z));
    }
    gf::AreaTarget target() const override { return t; }
    void load_map(std::int32_t m) override { log.push_back("map " + std::to_string(m)); }
    void change_room(std::int32_t m, int p) override { log.push_back("room " + std::to_string(m) + " " + std::to_string(p)); }
    void set_flag(std::int16_t f) override { log.push_back("flag " + std::to_string(f)); }
    void set_camera_rail(bool on) override { log.push_back(on ? "rail on" : "rail off"); }
    void start_load_screen() override { log.push_back("screen"); }
};

gf::AreaLoad make() {
    gf::AreaLoad a;
    gf::AreaLoadRules r;
    r.fade_budget = 12288;
    r.min_step = 68;
    a.set_rules(r);
    return a;
}

}  // namespace

SH_TEST(area_load_fades_then_loads) {
    const gf::AreaLoad a = make();
    Host h;
    h.raw = 0;
    a.frame(h);
    SH_CHECK((h.log == std::vector<std::string>{"door", "fade", "progress", "delta 0"}));
    SH_CHECK(h.s0 == 1 && h.s1 == 68);  // a stopped clock still spends the budget

    h.log.clear();
    h.done = true;
    h.t.map = 7;
    h.t.point = 12;
    h.t.done_flag = 300;
    a.frame(h);
    // Walk-through arrival keeps the offset from the door: (1000 + 50, 2000 - 10).
    SH_CHECK((h.log == std::vector<std::string>{"begin", "arrive 1050 1990", "room 7 12", "flag 300", "rail off", "screen"}));
}

SH_TEST(area_load_budget_and_demo) {
    const gf::AreaLoad a = make();
    Host h;
    h.s0 = 1;
    h.s1 = 12288;  // budget spent: load even though the fade has not finished
    h.t.load_map = true;
    h.t.map = 3;
    h.t.camera_rail = true;
    a.frame(h);
    SH_CHECK(h.log.front() == "begin");
    SH_CHECK(h.log[2] == "map 3" && h.log[4] == "rail on");

    Host d;
    d.is_demo = true;
    a.frame(d);
    SH_CHECK(d.log[0] == "door" && d.log[1] == "begin");

    Host n;
    n.can = false;
    a.frame(n);
    SH_CHECK(n.log.empty());
}
