// Runtime world pickups (engine/world/runtime_pickups.h): the registry's ids, names, per-map lifetime, what it draws
// and lets the player reach, and the sh.world.pickup_* functions of the script host (owner namespaces of mods).

#include "engine/script/script_host.h"
#include "engine/world/runtime_pickups.h"
#include "framework/sh_test.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "framework/mod_fixture.h"

namespace {

namespace fs = std::filesystem;
namespace sw = sh::world;

constexpr std::int32_t m(double metres) { return static_cast<std::int32_t>(metres * 4096.0); }

class FakeResolver final : public sw::PickupResolver {
public:
    std::map<std::string, int> items{{"Ampoule", 34}, {"FirstAidKit", 32}, {"KitchenKnife", 128}};
    std::map<std::string, int> flags{{"cafe_supplies:ampoule_taken", 1600}, {"cafe_supplies:kit_taken", 1601},
                                     {"other:a", 1602}, {"other:b", 1603}};
    std::map<std::string, std::string> models{{"Ampoule", "AMPULE_N"}, {"FirstAidKit", "AIDKIT_N"}};
    std::set<std::string> common{"Ampoule", "FirstAidKit"};

    sh::Result<void> resolve(sw::PickupSpec& spec, sw::PickupResolved& out) override {
        const auto item = items.find(spec.item);
        if (item == items.end()) {
            return sh::Result<void>::fail("unknown item '" + spec.item + "'");
        }
        const auto flag = flags.find(spec.flag);
        if (flag == flags.end()) {
            return sh::Result<void>::fail("unknown flag '" + spec.flag + "'");
        }
        if (spec.message.empty() && common.count(spec.item) == 0) {
            return sh::Result<void>::fail("no prompt");
        }
        out.item = item->second;
        out.flag = flag->second;
        out.common_kind = common.count(spec.item) != 0 ? 2 : -1;
        if (spec.model.empty() && models.count(spec.item) != 0) {
            spec.model = models[spec.item];
        }
        return sh::Result<void>::ok();
    }
};

sw::PickupSpec spec(const std::string& id, const std::string& item, const std::string& flag, double x, double z,
                    double y = -1.0) {
    sw::PickupSpec s;
    s.id = id;
    s.item = item;
    s.flag = flag;
    s.position = {m(x), m(y), m(z)};
    return s;
}

}  // namespace

SH_TEST(runtime_pickups_empty_until_created) {
    sw::PickupRegistry reg;
    SH_CHECK(reg.empty());
    // Nothing loaded: nothing can be created, and the empty registry selects nothing.
    SH_CHECK(!reg.create(spec("a", "Ampoule", "cafe_supplies:ampoule_taken", 5, 274)));
    reg.begin_map("map0_s01");
    const std::uint64_t rev = reg.revision();
    reg.begin_map("map0_s01");  // nothing to drop: no change
    SH_CHECK(reg.revision() == rev);
    SH_CHECK(reg.visible({0, 0, 0}, [](int) { return false; }).empty());
    SH_CHECK(reg.reachable({0, 0, 0}, [](int) { return false; }, [](const sw::Pickup&) { return true; }) == nullptr);
}

SH_TEST(runtime_pickups_create_ids_and_names) {
    FakeResolver names;
    sw::PickupRegistry reg;
    reg.set_resolver(&names);
    reg.begin_map("map0_s01");

    auto a = reg.create(spec("cafe_supplies:ampoule", "Ampoule", "cafe_supplies:ampoule_taken", 5.3, 274.3));
    SH_CHECK(a && a.value() == "cafe_supplies:ampoule");
    const sw::Pickup* p = reg.find("cafe_supplies:ampoule");
    SH_CHECK(p != nullptr && p->spec.model == "AMPULE_N" && p->resolved.item == 34 && p->resolved.flag == 1600);
    SH_CHECK(p->spec.owner == "silenthill");

    // Same id twice, unknown names, no flag, a non-common item without a prompt, an over-long model.
    SH_CHECK(!reg.create(spec("cafe_supplies:ampoule", "Ampoule", "cafe_supplies:kit_taken", 5, 274)));
    auto bad = reg.create(spec("x", "Crowbar", "cafe_supplies:kit_taken", 5, 274));
    SH_CHECK(!bad && bad.error().find("unknown item") != std::string::npos);
    SH_CHECK(!reg.create(spec("y", "FirstAidKit", "nope:flag", 5, 274)));
    SH_CHECK(!reg.create(spec("z", "FirstAidKit", "", 5, 274)));
    SH_CHECK(!reg.create(spec("k", "KitchenKnife", "other:a", 5, 274)));
    sw::PickupSpec longer = spec("l", "FirstAidKit", "other:a", 5, 274);
    longer.model = "TOO_LONG_NAME";
    SH_CHECK(!reg.create(longer));
    sw::PickupSpec negative = spec("n", "FirstAidKit", "other:a", 5, 274);
    negative.count = -3;
    SH_CHECK(!reg.create(negative));
    SH_CHECK(reg.all().size() == 1);

    // No id: numbered in the owner's namespace.
    sw::PickupSpec anon = spec("", "FirstAidKit", "other:a", 6, 274);
    anon.owner = "other";
    auto made = reg.create(anon);
    SH_CHECK(made && made.value() == "other:pickup1");
    anon.flag = "other:b";
    made = reg.create(anon);
    SH_CHECK(made && made.value() == "other:pickup2");

    // A knife with its own prompt message and model is fine.
    sw::PickupSpec knife = spec("knife2", "KitchenKnife", "cafe_supplies:kit_taken", 5, 274);
    knife.message = "map0_s01.there_is_a_kitchen_knife";
    knife.model = "KNIFE_HI";
    SH_CHECK(reg.create(knife));
    SH_CHECK(reg.all().size() == 4);
}

SH_TEST(runtime_pickups_remove_and_map_lifetime) {
    FakeResolver names;
    sw::PickupRegistry reg;
    reg.set_resolver(&names);
    reg.begin_map("map0_s01");
    sw::PickupSpec a = spec("cafe_supplies:a", "Ampoule", "cafe_supplies:ampoule_taken", 5, 274);
    a.owner = "cafe_supplies";
    sw::PickupSpec b = spec("other:b", "FirstAidKit", "other:b", 5, 275);
    b.owner = "other";
    SH_CHECK(reg.create(a) && reg.create(b));
    std::uint64_t rev = reg.revision();
    SH_CHECK(!reg.remove("cafe_supplies:none"));
    SH_CHECK(reg.revision() == rev);
    SH_CHECK(reg.remove("other:b"));
    SH_CHECK(reg.revision() != rev && reg.all().size() == 1);
    SH_CHECK(reg.create(b));
    SH_CHECK(reg.remove_owner("cafe_supplies") == 1 && reg.find("other:b") != nullptr);
    // Another map (or the same one loaded again): the pickups are gone; the ids are free again.
    reg.begin_map("map2_s00");
    SH_CHECK(reg.empty() && reg.map() == "map2_s00");
    SH_CHECK(reg.create(b));
    reg.begin_map("");
    SH_CHECK(reg.empty());
    SH_CHECK(!reg.create(b));
}

SH_TEST(runtime_pickups_visible_and_reachable) {
    FakeResolver names;
    sw::PickupRegistry reg;
    reg.set_resolver(&names);
    reg.begin_map("map0_s01");
    SH_CHECK(reg.create(spec("near", "Ampoule", "cafe_supplies:ampoule_taken", 5, 274, -1.0)));
    SH_CHECK(reg.create(spec("far", "FirstAidKit", "cafe_supplies:kit_taken", 5, 274 + 40, -1.0)));
    SH_CHECK(reg.create(spec("upstairs", "FirstAidKit", "other:a", 5, 274, -5.5)));
    sw::PickupSpec grounded = spec("ground", "FirstAidKit", "other:b", 6, 274);
    grounded.ground = true;
    SH_CHECK(reg.create(grounded));

    std::set<int> set_flags;
    const auto flag = [&](int f) { return set_flags.count(f) != 0; };
    const std::array<std::int32_t, 3> player{m(4.5), 0, m(273.5)};
    auto shown = reg.visible(player, flag);
    SH_CHECK(shown.size() == 1 && reg.all()[shown[0]].spec.id == "near");

    // The ground height arrives: drawn from then on.
    SH_CHECK(reg.place_on_ground("ground", m(-0.0)));
    shown = reg.visible(player, flag);
    SH_CHECK(shown.size() == 2);

    // Reach: the first accepted in creation order; the far one is on the storey but the predicate refuses it.
    const sw::Pickup* r = reg.reachable(player, flag, [](const sw::Pickup& p) { return p.spec.id != "near"; });
    SH_CHECK(r != nullptr && r->spec.id == "far");
    r = reg.reachable(player, flag, [](const sw::Pickup&) { return true; });
    SH_CHECK(r != nullptr && r->spec.id == "near");

    // Taken (flag set): neither drawn nor reachable.
    set_flags.insert(1600);
    shown = reg.visible(player, flag);
    SH_CHECK(shown.size() == 1 && reg.all()[shown[0]].spec.id == "ground");
    r = reg.reachable(player, flag, [](const sw::Pickup& p) { return p.spec.id == "near"; });
    SH_CHECK(r == nullptr);
}

namespace {

struct ScriptDir {
    fs::path root;
    explicit ScriptDir(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "pickup_script" / name) {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~ScriptDir() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void write(const fs::path& relative, std::string_view text) const {
        fs::create_directories(root / relative.parent_path());
        std::ofstream out(root / relative, std::ios::binary);
        out << text;
        out.close();
        sh::test::mod_file_written(root / relative);
    }
};

}  // namespace

// Two mods place pickups on the same map from on_map_load; bare ids land in each mod's namespace.
SH_TEST(runtime_pickups_script_api_two_mods) {
    ScriptDir dir("two_mods");
    dir.write("mods/cafe_supplies/mod.json", R"({"id": "cafe_supplies", "version": "1.0.0", "scripts": true})");
    dir.write("mods/cafe_supplies/scripts/main.lua", R"lua(
        function on_map_load(map)
            if map ~= "map0_s01" then return end
            local id, err = sh.world.pickup_create{item = "Ampoule", x = 5.3, y = -1.01, z = 274.3, rot = 32,
                                                   id = "ampoule", flag = "cafe_supplies:ampoule_taken"}
            sh.log("ampoule " .. tostring(id) .. " " .. tostring(err))
            local dup, why = sh.world.pickup_create{item = "Ampoule", x = 1, z = 2, id = "ampoule",
                                                    flag = "cafe_supplies:ampoule_taken"}
            sh.log("dup " .. tostring(dup) .. " " .. tostring(why ~= nil))
            local bad, e2 = sh.world.pickup_create{item = "Ampoule", z = 2, flag = "cafe_supplies:ampoule_taken"}
            sh.log("nox " .. tostring(bad) .. " " .. tostring(e2))
        end
    )lua");
    dir.write("mods/other/mod.json",
              R"({"id": "other", "version": "1.0.0", "scripts": true, "load_after": ["cafe_supplies"]})");
    dir.write("mods/other/scripts/main.lua", R"lua(
        function on_map_load(map)
            local id = sh.world.pickup_create{item = "FirstAidKit", x = 4.8, z = 274.0, rot = {0, 90, 0},
                                              id = "ampoule", flag = "other:a"}
            sh.log("other " .. tostring(id))
            for _, p in ipairs(sh.world.pickups("metres")) do
                sh.log(string.format("list %s %s %s %.2f %s %s %s", p.id, p.owner, p.item, p.x, tostring(p.y),
                                     p.model, tostring(p.taken)))
            end
            sh.log("removed " .. tostring(sh.world.pickup_remove("ampoule")) .. " " .. #sh.world.pickups())
        end
    )lua");

    FakeResolver names;
    sw::PickupRegistry& reg = sw::pickups();
    reg.set_resolver(&names);
    reg.begin_map("map0_s01");

    std::vector<std::string> lines;
    sh::script::HostConfig c;
    c.gamedata_dir = dir.root / "base";
    c.mods_dir = dir.root / "mods";
    c.log = [&](int, const std::string& msg) { lines.push_back(msg); };
    sh::script::Host host(c);
    SH_CHECK(host.load().value());
    host.on_map_load("map0_s01");
    const auto has = [&](const std::string& part) {
        for (const std::string& l : lines) {
            if (l.find(part) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    SH_CHECK(has("ampoule cafe_supplies:ampoule nil"));
    SH_CHECK(has("dup nil true"));
    SH_CHECK(has("nox nil x and z are required numbers (Q12 integers)"));
    SH_CHECK(has("other other:ampoule"));
    SH_CHECK(has("list cafe_supplies:ampoule cafe_supplies Ampoule 5.30 -1.0097"));
    SH_CHECK(has("list other:ampoule other FirstAidKit 4.80"));
    SH_CHECK(has("AIDKIT_N false"));
    SH_CHECK(has("removed true 1"));  // "other" removed its own bare id, not the other mod's
    const sw::Pickup* p = reg.find("cafe_supplies:ampoule");
    SH_CHECK(p != nullptr);
    if (p != nullptr) {
        SH_CHECK(p->spec.position[0] == static_cast<std::int32_t>(5.3f * 4096.0f));
        SH_CHECK(p->spec.position[1] == static_cast<std::int32_t>(-1.01f * 4096.0f));
        SH_CHECK(p->spec.rotation[1] == 364);  // Q12_ANGLE(32)
        SH_CHECK(!p->spec.ground);
    }
    reg.begin_map("");
    reg.set_resolver(nullptr);
}

// The pickups take Q12 integers like the rest of the API; the old metres-and-degrees form is still read from
// units = "metres" or from fractional numbers, and sh.world.pickups() answers in Q12 unless asked for metres.
SH_TEST(runtime_pickups_script_api_units) {
    ScriptDir dir("units");
    dir.write("mods/cafe_supplies/mod.json", R"({"id": "cafe_supplies", "version": "1.0.0", "scripts": true})");
    dir.write("mods/cafe_supplies/scripts/main.lua", R"lua(
        function on_map_load(map)
            local a = sh.world.pickup_create{item = "Ampoule", x = 21258, y = -4136, z = 1122795, rot = 364,
                                             id = "q12", flag = "cafe_supplies:ampoule_taken"}
            local b = sh.world.pickup_create{item = "Ampoule", x = 5, y = -1, z = 274, rot = 90, units = "metres",
                                             id = "metres", flag = "cafe_supplies:kit_taken"}
            local c = sh.world.pickup_create{item = "FirstAidKit", x = 5.19, z = 273, rot = {0, 32, 0},
                                             id = "fraction", flag = "other:a"}
            local d, why = sh.world.pickup_create{item = "Ampoule", x = 4096, z = 8192, rot = 1.5, units = "q12",
                                                  id = "mixed", flag = "other:b"}
            local e, why2 = sh.world.pickup_create{item = "Ampoule", x = 1, z = 2, units = "feet", flag = "other:b"}
            sh.log(string.format("made %s %s %s %s %s", tostring(a), tostring(b), tostring(c), tostring(d),
                                 tostring(e)))
            sh.log("why " .. tostring(why) .. " / " .. tostring(why2))
            for _, p in ipairs(sh.world.pickups()) do
                sh.log(string.format("q12 %s %s %s %s %s %s", p.id, math.type(p.x), p.x, p.y, p.z, p.rot))
            end
            for _, p in ipairs(sh.world.pickups("metres")) do
                sh.log(string.format("m %s %.2f %.0f", p.id, p.x, p.rot))
            end
        end
    )lua");

    FakeResolver names;
    sw::PickupRegistry& reg = sw::pickups();
    reg.set_resolver(&names);
    reg.begin_map("map0_s01");
    std::vector<std::string> lines;
    sh::script::HostConfig c;
    c.gamedata_dir = dir.root / "base";
    c.mods_dir = dir.root / "mods";
    c.log = [&](int, const std::string& msg) { lines.push_back(msg); };
    sh::script::Host host(c);
    SH_CHECK(host.load().value());
    host.on_map_load("map0_s01");
    const auto has = [&](const std::string& part) {
        for (const std::string& l : lines) {
            if (l.find(part) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    SH_CHECK(has("made cafe_supplies:q12 cafe_supplies:metres cafe_supplies:fraction nil nil"));
    SH_CHECK(has("why rot must be a Q12 angle (a number) or {x, y, z} / units must be \"q12\" or \"metres\""));
    SH_CHECK(has("q12 cafe_supplies:q12 integer 21258 -4136 1122795 364"));
    SH_CHECK(has("q12 cafe_supplies:metres integer 20480 -4096 1122304 1024"));
    SH_CHECK(has("q12 cafe_supplies:fraction integer 21258 0 1118208 364"));
    SH_CHECK(has("m cafe_supplies:q12 5.19 32"));
    const sw::Pickup* p = reg.find("cafe_supplies:fraction");
    SH_CHECK(p != nullptr && p->spec.ground && p->spec.rotation[1] == 364);
    reg.begin_map("");
    reg.set_resolver(nullptr);
}
