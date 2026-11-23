// The editor's mod authoring files (engine/devtools/mod_authoring.h, docs/modding/editor.md): the objects file reads
// back to the same objects, the generated files are valid, and the main.lua region keeps the author's own code.

#include "engine/devtools/mod_authoring.h"
#include "formats/common/json.h"

#include "framework/sh_test.h"

#include <string>

namespace {

namespace dt = sh::devtools;

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

dt::ModMapObjects sample() {
    dt::ModMapObjects m;
    m.map = "map0_s01";
    dt::ModObject a;
    a.id = "note";
    a.kind = dt::ModObjectKind::Message;
    a.x = 20848;
    a.y = -4136;
    a.z = 1122795;
    a.yaw = 364;
    a.radius = 6144;
    a.text = "A \"quoted\" note.\nSecond line.";
    a.once = true;
    dt::ModObject b;
    b.id = "lever";
    b.kind = dt::ModObjectKind::Script;
    b.activation = dt::ModActivation::Action;
    b.flag = "M0S01_KitchenKnife";
    b.flag_value = false;
    dt::ModObject c;
    c.id = "dog";
    c.kind = dt::ModObjectKind::Spawn;
    c.activation = dt::ModActivation::Load;
    c.chara = "Groaner";
    c.ai = "full";
    c.x = -40960;
    dt::ModObject d;
    d.id = "kit";
    d.kind = dt::ModObjectKind::Pickup;
    d.item = "FirstAidKit";
    d.count = 2;
    m.objects = {a, b, c, d};
    return m;
}

bool same(const dt::ModObject& a, const dt::ModObject& b) {
    const bool trigger = a.kind != dt::ModObjectKind::Pickup;
    return a.id == b.id && a.kind == b.kind && a.x == b.x && a.y == b.y && a.z == b.z && a.yaw == b.yaw &&
           (!trigger ||
            (a.radius == b.radius && a.height == b.height && a.activation == b.activation && a.once == b.once)) &&
           a.flag == b.flag && (a.flag.empty() || a.flag_value == b.flag_value) &&
           (a.kind != dt::ModObjectKind::Message || a.text == b.text) &&
           (a.kind != dt::ModObjectKind::Spawn || (a.chara == b.chara && a.ai == b.ai)) &&
           (a.kind != dt::ModObjectKind::Pickup || (a.item == b.item && a.count == b.count));
}

}  // namespace

SH_TEST(devtools_mod_authoring_objects_round_trip) {
    const dt::ModMapObjects m = sample();
    const std::string text = dt::write_map_objects(m);
    auto back = dt::parse_map_objects(text);
    SH_CHECK(static_cast<bool>(back));
    if (!back) return;
    SH_CHECK(back.value().map == "map0_s01");
    SH_CHECK(back.value().objects.size() == m.objects.size());
    for (std::size_t i = 0; i < m.objects.size() && i < back.value().objects.size(); ++i)
        SH_CHECK(same(m.objects[i], back.value().objects[i]));
    SH_CHECK(dt::write_map_objects(back.value()) == text);
}

SH_TEST(devtools_mod_authoring_parse_refuses) {
    SH_CHECK(!dt::parse_map_objects("{\"format\": \"sh-text\"}"));
    SH_CHECK(!dt::parse_map_objects("{\"format\": \"sh-editor-objects\", \"objects\": [{\"id\": \"a b\"}]}"));
    SH_CHECK(
        !dt::parse_map_objects("{\"format\": \"sh-editor-objects\", \"objects\": [{\"id\": \"a\"}, {\"id\": \"a\"}]}"));
    SH_CHECK(!dt::parse_map_objects("{\"format\": \"sh-editor-objects\", \"objects\": [{\"id\": \"a\", \"x\": 1.5}]}"));
    SH_CHECK(!dt::parse_map_objects(
        "{\"format\": \"sh-editor-objects\", \"objects\": [{\"id\": \"a\", \"kind\": \"door\"}]}"));
    SH_CHECK(dt::valid_object_id("kit_2") && !dt::valid_object_id("2kit") && !dt::valid_object_id(""));
}

SH_TEST(devtools_mod_authoring_generated_files) {
    const dt::ModMapObjects m = sample();
    const std::string text = dt::write_text_doc("mymod", m);
    auto tv = sh::json::parse(text);
    SH_CHECK(static_cast<bool>(tv));
    if (static_cast<bool>(tv)) {
        const auto* strings = tv.value().find("strings");
        SH_CHECK(strings != nullptr && strings->find("mymod:map0_s01.note") != nullptr);
        SH_CHECK(tv.value().find("map") != nullptr && tv.value().find("map")->string() == "map0_s01");
    }
    const std::string flags = dt::write_flags_doc("mymod", {m});
    auto fv = sh::json::parse(flags);
    SH_CHECK(static_cast<bool>(fv));
    if (static_cast<bool>(fv)) {
        const auto* f = fv.value().find("flags");
        SH_CHECK(f != nullptr && f->find("note_done") != nullptr && f->find("kit_taken") != nullptr);
        SH_CHECK(f != nullptr && f->find("lever_done") == nullptr);
    }
    const std::string region = dt::runtime_region("mymod", {m});
    SH_CHECK(region.starts_with("-- editor:begin\n") && region.ends_with("-- editor:end\n"));
    SH_CHECK(contains(region, "text = \"mymod:map0_s01.note\""));
    SH_CHECK(contains(region, "event = \"mymod:lever\""));
    SH_CHECK(contains(region, "done = \"mymod:kit_taken\""));
    SH_CHECK(contains(region, "x = -40960"));

    const std::string own = "-- my code\nlocal a = 1\n";
    const std::string first = dt::merge_main_lua(own, region);
    SH_CHECK(first.starts_with(own) && contains(first, region));
    const std::string other = dt::runtime_region("mymod", {});
    const std::string second = dt::merge_main_lua(first + "-- after\n", other);
    SH_CHECK(second.starts_with(own) && contains(second, other) && !contains(second, "mymod:lever") &&
             second.ends_with("-- after\n"));
    SH_CHECK(dt::merge_main_lua("", region) == region);
}

SH_TEST(devtools_mod_authoring_script_stub_and_manifest) {
    const std::string once = dt::add_script_stub("", "mymod", "map0_s01", "lever");
    SH_CHECK(contains(once, "sh.hooks.on(\"mymod:lever\""));
    SH_CHECK(dt::add_script_stub(once, "mymod", "map0_s01", "lever") == once);
    const std::string two = dt::add_script_stub(once, "mymod", "map0_s01", "bell");
    SH_CHECK(two.starts_with(once) && contains(two, "\"mymod:bell\""));

    auto mv = sh::json::parse(dt::new_manifest("mymod", "My mod", "1.0.0"));
    SH_CHECK(static_cast<bool>(mv));
    if (static_cast<bool>(mv)) {
        SH_CHECK(mv.value().find("id")->string() == "mymod");
        SH_CHECK(mv.value().find("engine")->string() == ">=1.0.0 <2.0.0");
        SH_CHECK(mv.value().find("scripts")->boolean());
    }
}
