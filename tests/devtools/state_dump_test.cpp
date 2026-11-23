// The state dump document (engine/devtools/state_dump.h): the id, the JSON a
// developer reads and what `dump load` takes back out of it.

#include "engine/devtools/state_dump.h"

#include "formats/common/json.h"
#include "framework/sh_test.h"

namespace {

namespace dt = sh::devtools;

dt::StateDump sample() {
    dt::StateDump d;
    d.time = "2026-11-20T12:00:00";
    d.note = "the \"Key\" floats";
    d.map = "map3_s01";
    d.room = 7;
    d.state_saved = true;
    d.position = {4096 * 10, -4096, 4096 * 20};
    d.yaw = 1024;
    d.health = 4096 * 100;
    d.weapon = "silenthill:Handgun";
    d.flags = {"MetCybil", "unnamed:900"};
    d.inventory = {{"silenthill:HealthDrink", 3}};
    dt::DumpCharacter nurse;
    nurse.slot = 2;
    nurse.name = "silenthill:PuppetNurse";
    nurse.position = {4096 * 13, -4096, 4096 * 24};
    nurse.model_loaded = true;
    nurse.in_camera_frustum = true;
    d.characters.push_back(nurse);
    dt::DumpObject far_object;
    far_object.name = "Far";
    far_object.position = {4096 * 40, 0, 4096 * 20};
    dt::DumpObject near_object;
    near_object.name = "Near";
    near_object.position = {4096 * 11, -4096, 4096 * 20};
    near_object.queued = true;
    near_object.library = "cell 2,3";
    near_object.library_slot = 1;
    d.objects = {far_object, near_object};
    d.log = {"line one", "line two"};

    // Four frames of history: 10, 10, 10 and a 50 ms stall with two logic updates in it.
    dt::PerfMonitor monitor;
    const int update = monitor.section("game_update");
    const int scripts = monitor.section("scripts", "game_update");
    const int updates = monitor.counter("logic_updates");
    for (int frame = 0; frame < 4; ++frame) {
        const bool stall = frame == 3;
        monitor.add(update, stall ? 40.0 : 4.0);
        monitor.add(scripts, 1.0);
        monitor.add(scripts, 1.0);
        monitor.count(updates, stall ? 2.0 : 1.0);
        monitor.end_frame(stall ? 50.0 : 10.0, false);
    }
    d.performance.history = monitor.report();
    d.performance.cpu_percent = 180.5;
    d.performance.fps_cap = 60;
    d.performance.characters = 1;
    return d;
}

}  // namespace

SH_TEST(devtools_state_dump_id) {
    const std::string a = dt::state_dump_id("content", 1);
    SH_CHECK(dt::valid_dump_id(a));
    SH_CHECK(a == dt::state_dump_id("content", 1));
    SH_CHECK(a != dt::state_dump_id("content", 2));
    SH_CHECK(a != dt::state_dump_id("content.", 1));
    SH_CHECK(!dt::valid_dump_id("1234567"));
    SH_CHECK(!dt::valid_dump_id("1234567G"));
    SH_CHECK(!dt::valid_dump_id("../../ab"));
    SH_CHECK(!dt::valid_dump_id("ABCDEF12"));
}

SH_TEST(devtools_state_dump_json) {
    const dt::StateDump d = sample();
    auto doc = sh::json::parse(dt::state_dump_json(d, "0badf00d"));
    SH_CHECK(static_cast<bool>(doc));
    const sh::json::Value& root = doc.value();
    SH_CHECK(root.find("id")->string() == "0badf00d");
    SH_CHECK(root.find("note")->string() == d.note);
    SH_CHECK(root.find("flags")->array().size() == 2);
    SH_CHECK(root.find("inventory")->array()[0].find("count")->integer() == 3);
    SH_CHECK(root.find("log")->array().size() == 2);

    const sh::json::Value* player = root.find("player");
    SH_CHECK(player->find("position")->find("metres")->array()[2].number() == 20.0);
    SH_CHECK(player->find("position")->find("q12")->array()[1].integer() == -4096);
    SH_CHECK(player->find("yaw")->find("degrees")->number() == 90.0);

    // 3 m and 4 m apart on the ground: 5 m.
    const sh::json::Value& nurse = root.find("characters")->array()[0];
    SH_CHECK(nurse.find("distance")->number() == 5.0);
    SH_CHECK(nurse.find("visible")->boolean());

    // Nearest first, whatever the map's order is.
    const auto& objects = root.find("objects")->array();
    SH_CHECK(objects.size() == 2);
    SH_CHECK(objects[0].find("name")->string() == "Near");
    SH_CHECK(objects[0].find("distance")->number() == 1.0);
    SH_CHECK(objects[0].find("library_slot")->integer() == 1);
    SH_CHECK(objects[1].find("name")->string() == "Far");
    SH_CHECK(objects[1].find("library_slot") == nullptr);
}

SH_TEST(devtools_state_dump_restore) {
    const dt::StateDump d = sample();
    auto restore = dt::parse_state_dump(dt::state_dump_json(d, "0badf00d"));
    SH_CHECK(static_cast<bool>(restore));
    SH_CHECK(restore.value().map == "map3_s01");
    SH_CHECK(restore.value().position.x == d.position.x);
    SH_CHECK(restore.value().position.y == d.position.y);
    SH_CHECK(restore.value().position.z == d.position.z);
    SH_CHECK(restore.value().yaw == 1024);
    SH_CHECK(restore.value().state_saved);

    SH_CHECK(!dt::parse_state_dump("{\"map\": \"map0_s00\"}"));
    SH_CHECK(!dt::parse_state_dump("not json"));
}

SH_TEST(devtools_perf_monitor) {
    dt::PerfMonitor m;
    SH_CHECK(m.report().frame_ms.empty());
    const int a = m.section("a");
    SH_CHECK(m.section("a") == a);
    const int n = m.counter("n");

    // More frames than the ring holds: the oldest go, frame i takes i ms.
    for (int i = 1; i <= dt::PerfMonitor::kFrames + 100; ++i) {
        m.add(a, 0.5 * i);
        m.count(n, i);
        m.end_frame(i, false);
    }
    dt::PerfReport r = m.report();
    SH_CHECK(static_cast<int>(r.frame_ms.size()) == dt::PerfMonitor::kFrames);
    SH_CHECK(r.frame_ms.front() == 101.0);
    SH_CHECK(r.frame_ms.back() == 400.0);
    SH_CHECK(r.min_ms == 101.0 && r.max_ms == 400.0);
    SH_CHECK(r.average_ms == 250.5);
    SH_CHECK(r.p99_ms == 397.0);
    SH_CHECK(r.low_1pct_ms == 399.0);  // the worst three of 300
    SH_CHECK(r.stutter_frames == 0);
    SH_CHECK(r.sections[0].last == 200.0 && r.sections[0].max == 200.0);
    SH_CHECK(r.sections[0].in_slowest_frame == 200.0);
    SH_CHECK(r.sections[0].calls_per_frame == 1.0);
    SH_CHECK(r.counters[0].last == 400.0 && r.counters[0].average == 250.5);

    // Frames under the console are counted, not recorded, and what they measured is dropped.
    m.add(a, 1000.0);
    m.end_frame(5000.0, true);
    m.end_frame(5000.0, true);
    r = m.report();
    SH_CHECK(r.held_frames_before_report == 2 && r.held_frames_total == 2);
    SH_CHECK(r.frame_ms.back() == 400.0 && r.max_ms == 400.0);
    m.end_frame(1.0, false);
    r = m.report();
    SH_CHECK(r.held_frames_before_report == 0 && r.held_frames_total == 2);
    SH_CHECK(r.frame_ms.back() == 1.0 && r.sections[0].last == 0.0);

    // A section entered inside itself is timed by the outer entry alone.
    SH_CHECK(m.enter(a));
    SH_CHECK(!m.enter(a));
    m.leave(a);
    m.leave(a);
    SH_CHECK(m.enter(a));
    m.leave(a);
}

SH_TEST(devtools_state_dump_performance) {
    auto doc = sh::json::parse(dt::state_dump_json(sample(), "0badf00d"));
    SH_CHECK(static_cast<bool>(doc));
    const sh::json::Value* perf = doc.value().find("performance");
    SH_CHECK(perf != nullptr);
    SH_CHECK(perf->find("fps")->number() == 50.0);  // 80 ms over four frames
    SH_CHECK(perf->find("low_1pct_fps")->number() == 20.0);
    SH_CHECK(perf->find("frame_ms")->find("average")->number() == 20.0);
    SH_CHECK(perf->find("frame_ms")->find("max")->number() == 50.0);
    SH_CHECK(perf->find("history_frames")->integer() == 4);
    SH_CHECK(perf->find("stutter_frames")->integer() == 1);
    SH_CHECK(perf->find("frame_ms_history")->array().size() == 4);
    SH_CHECK(perf->find("cpu_percent")->number() == 180.5);
    SH_CHECK(perf->find("gpu_percent")->is_null());  // not reported
    SH_CHECK(perf->find("settings")->find("fps_cap")->integer() == 60);
    SH_CHECK(perf->find("counts")->find("characters")->integer() == 1);

    const auto& sections = perf->find("sections_ms")->array();
    SH_CHECK(sections.size() == 2);
    SH_CHECK(sections[0].find("name")->string() == "game_update");
    SH_CHECK(sections[0].find("inside") == nullptr);
    SH_CHECK(sections[0].find("average")->number() == 13.0);
    SH_CHECK(sections[0].find("in_slowest_frame")->number() == 40.0);
    SH_CHECK(sections[1].find("inside")->string() == "game_update");
    SH_CHECK(sections[1].find("calls_per_frame")->number() == 2.0);
    const sh::json::Value& updates = perf->find("per_frame")->array()[0];
    SH_CHECK(updates.find("name")->string() == "logic_updates");
    SH_CHECK(updates.find("in_slowest_frame")->number() == 2.0);
}
