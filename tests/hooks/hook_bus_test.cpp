#include "engine/hooks/hook_bus.h"
#include "framework/sh_test.h"

#include <stdexcept>
#include <string>
#include <string_view>

using namespace sh::hooks;

SH_TEST(hooks_order_priority_owner_registration) {
    Bus bus;
    bus.set_owner_order([](std::string_view owner) { return owner == "a" ? 0 : owner == "b" ? 1 : -1; });
    std::string trace;
    bus.on("tick", [&](Event&) { trace += "b1,"; }, {"b", "", 0});
    bus.on("tick", [&](Event&) { trace += "a1,"; }, {"a", "", 0});
    bus.on("tick", [&](Event&) { trace += "base,"; }, {"silenthill", "", 0});
    bus.on("tick", [&](Event&) { trace += "b_urgent,"; }, {"b", "", 10});
    bus.on("tick", [&](Event&) { trace += "a2,"; }, {"a", "", 0});
    bus.on("tick", [&](Event&) { trace += "stranger,"; }, {"zz", "", 0});
    Event e("tick");
    SH_CHECK(bus.emit(e));
    SH_CHECK(trace == "b_urgent,base,a1,a2,b1,stranger,");
    SH_CHECK(bus.emitted("tick") == 1);
    SH_CHECK(bus.handlers("tick").size() == 6);
}

SH_TEST(hooks_modify_cancel_and_read_only) {
    Bus bus;
    bus.on("item.pickup", [](Event& e) {
        e.update("count", e.get_int("count") * 2);
        SH_CHECK(!e.update("item", std::string("Other")));
    }, {"a", "", 0});
    bus.on("item.pickup", [](Event& e) { e.cancel("b"); }, {"b", "", 0});
    bus.on("item.pickup", [](Event& e) { e.update("count", std::int64_t{0}); }, {"c", "", 0});
    Event e("item.pickup", true);
    e.set("item", std::string("HandgunBullets"), true).set("count", std::int64_t{15});
    bus.set_owner_order([](std::string_view o) { return o == "a" ? 0 : o == "b" ? 1 : 2; });
    SH_CHECK(!bus.emit(e));
    SH_CHECK(e.get_int("count") == 30);  // c never ran
    SH_CHECK(e.get_string("item") == "HandgunBullets");
    SH_CHECK(e.cancelled_by() == "b");

    Event fixed("map.load", false);
    SH_CHECK(!fixed.cancel());
    SH_CHECK(bus.emit(fixed));
}

SH_TEST(hooks_replace_by_id_off_and_errors) {
    Bus bus;
    std::string log;
    bus.set_logger([&](const std::string& m) { log += m; });
    int calls = 0;
    bus.on("x", [&](Event&) { calls += 1; }, {"m", "one"});
    bus.on("x", [&](Event&) { calls += 10; }, {"m", "one"});  // replaces
    const Token t = bus.on("x", [&](Event&) { throw std::runtime_error("boom"); }, {"bad", "", 0});
    bus.on("x", [&](Event&) { calls += 100; }, {"n", "", 0});
    Event e("x");
    bus.emit(e);
    SH_CHECK(calls == 110);
    SH_CHECK(log.find("hook x of bad: boom") != std::string::npos);
    SH_CHECK(bus.off(t));
    SH_CHECK(bus.off("m", "x", "one"));
    SH_CHECK(bus.off_owner("n") == 1);
    SH_CHECK(!bus.has("x"));
}

SH_TEST(hooks_handler_registering_during_emit) {
    Bus bus;
    int late = 0;
    bus.on("x", [&](Event&) { bus.on("x", [&](Event&) { ++late; }, {"m", "", 0}); }, {"m", "first"});
    Event e("x");
    bus.emit(e);
    SH_CHECK(late == 0);  // added handlers run from the next emit
    Event again("x");
    bus.emit(again);
    SH_CHECK(late == 1);
}

SH_TEST(hooks_groups_close_with_their_state) {
    Bus bus;
    int a = 0;
    int b = 0;
    bus.on("x", [&](Event&) { ++a; }, {"m", "one", 0}, "state1");
    bus.on("x", [&](Event&) { ++b; }, {"m", "one", 0}, "state2");      // same owner and id, another state
    bus.on("x", [&](Event&) { a += 10; }, {"m", "one", 0}, "state1");  // replaces state1's
    SH_CHECK(bus.handlers("x").size() == 2);
    Event e("x");
    bus.emit(e);
    SH_CHECK(a == 10 && b == 1);
    SH_CHECK(bus.off_group("state1") == 1);
    SH_CHECK(bus.handlers("x").size() == 1 && bus.handlers("x")[0].group == "state2");
}
