#include "engine/hooks/catalog.h"
#include "engine/hooks/hook_bus.h"
#include "engine/hooks/hook_trace.h"
#include "framework/sh_test.h"

#include <set>
#include <string>
#include <vector>

using namespace sh::hooks;

SH_TEST(hooks_catalog_names_unique_and_complete) {
    std::set<std::string_view> names;
    for (const EventInfo& e : catalog()) {
        SH_CHECK(names.insert(e.name).second);
        SH_CHECK(find_event(e.name) == &e);
        for (const FieldInfo& f : e.fields) {
            SH_CHECK(f.type == "string" || f.type == "int" || f.type == "bool");
        }
    }
    for (const char* want : {"game.boot", "game.new_game", "game.load", "game.save", "game.tick", "map.load",
                             "map.unload", "map.room_enter", "item.pickup", "item.picked", "item.use", "player.damage",
                             "player.death", "entity.spawn", "entity.death", "flag.set", "menu.open", "cutscene.start",
                             "cutscene.end", "input.action", "map.event", "item.given", "player.quick_heal",
                             "game.quick_save", "game.quick_load", "radio.static"}) {
        SH_CHECK(find_event(want) != nullptr);
    }
    // The PC's own grants and keys: cancellable, the count and the heal changeable.
    for (const char* cancellable :
         {"item.given", "player.quick_heal", "game.quick_save", "game.quick_load", "radio.static"}) {
        SH_CHECK(find_event(cancellable)->cancellable);
    }
    const auto mutable_field = [](const char* event, std::string_view field) {
        for (const FieldInfo& f : find_event(event)->fields) {
            if (f.key == field) {
                return f.modifiable;
            }
        }
        return false;
    };
    SH_CHECK(mutable_field("item.given", "count") && !mutable_field("item.given", "source"));
    SH_CHECK(mutable_field("player.quick_heal", "amount") && !mutable_field("player.quick_heal", "item"));
    SH_CHECK(find_event("item.pickup")->cancellable);
    SH_CHECK(!find_event("item.picked")->cancellable);
    SH_CHECK(find_event("nope") == nullptr);
}

SH_TEST(hooks_trace_formats_and_switches) {
    Bus bus;
    std::vector<std::string> lines;
    SH_CHECK(!trace_on(bus));
    const std::size_t n = trace_set(bus, true, [&](const std::string& l) { lines.push_back(l); }, 3);
    SH_CHECK(n == catalog().size());
    SH_CHECK(trace_on(bus));
    SH_CHECK(bus.has("flag.set"));

    // A mod's handler runs first and its change shows in the trace.
    bus.on("item.pickup", [](Event& e) { e.update("count", std::int64_t{30}); }, {"mod", "", 0});
    Event e("item.pickup", true);
    e.set("item", std::string("HandgunBullets"), true).set("count", std::int64_t{15}).set("ok", true);
    bus.emit(e);
    SH_CHECK(lines.size() == 1);
    SH_CHECK(lines[0] == "[HOOK] item.pickup {item=HandgunBullets, count=30, ok=true}");

    for (int i = 0; i < 7; ++i) {
        Event t("game.tick");
        bus.emit(t);
    }
    SH_CHECK(lines.size() == 1 + 3);  // ticks 1, 4, 7

    trace_set(bus, false, nullptr);
    SH_CHECK(!trace_on(bus));
    SH_CHECK(!bus.has("flag.set"));
    SH_CHECK(bus.has("item.pickup"));  // the mod's handler stays
    Event empty("game.boot");
    SH_CHECK(format_fields(empty) == "{}");
}
