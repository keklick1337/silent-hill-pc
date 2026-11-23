#include "engine/mods/load_order.h"
#include "engine/mods/manifest.h"
#include "engine/mods/mods.h"
#include "engine/mods/mods_admin.h"
#include "engine/mods/registry.h"
#include "engine/mods/semver.h"
#include "framework/mod_fixture.h"
#include "framework/sh_test.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using namespace sh::mods;

namespace {

// Each test works in its own folder under the project's git-ignored .tmp/mods/.
class Scratch {
public:
    explicit Scratch(const std::string& name) : path_(fs::path(SH_PROJECT_ROOT) / ".tmp" / "mods" / name) {
        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_, ec);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    const fs::path& path() const { return path_; }

    void write(const std::string& relative, std::string_view text) const {
        const fs::path file = path_ / relative;
        fs::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary);
        out << text;
        out.close();
        sh::test::mod_file_written(file);
    }

private:
    fs::path path_;
};

bool has(const std::vector<std::string>& lines, std::string_view part) {
    return std::any_of(lines.begin(), lines.end(),
                       [&](const std::string& l) { return l.find(part) != std::string::npos; });
}

bool in_range(std::string_view range, std::string_view version) {
    const auto r = VersionRange::parse(range);
    const auto v = Version::parse(version);
    return r && v && r.value().matches(v.value());
}

Candidate mod(std::string id, std::string version = "1.0.0", int priority = 0) {
    Candidate c;
    c.manifest.id = id;
    c.manifest.name = id;
    c.manifest.version = Version::parse(version).value();
    c.manifest.priority = priority;
    c.folder = id;
    return c;
}

// Every candidate switched on, listed as the Mods screen lists mods a player switches on: priority, then id.
UserState all_on(std::vector<Candidate> list) {
    std::stable_sort(list.begin(), list.end(), [](const Candidate& a, const Candidate& b) {
        return a.manifest.priority != b.manifest.priority ? a.manifest.priority < b.manifest.priority
                                                          : a.manifest.id < b.manifest.id;
    });
    UserState user;
    for (const Candidate& c : list) {
        if (user.find(c.manifest.id) == nullptr) {
            user.mods.push_back({c.manifest.id, true});
        }
    }
    return user;
}

Plan resolve_on(const std::vector<Candidate>& list, const Environment& env = {}) {
    return resolve_load_order(list, all_on(list), env);
}

ModRef ref(std::string id, std::string_view range = "*") {
    return ModRef{std::move(id), VersionRange::parse(range).value()};
}

std::string order_of(const Plan& plan) {
    std::string out;
    for (const ResolvedMod* m : plan.active()) {
        out += (out.empty() ? "" : ",") + m->candidate.manifest.id;
    }
    return out;
}

const ResolvedMod& get(const Plan& plan, std::string_view id) {
    const ResolvedMod* m = plan.find(id);
    if (m == nullptr) {
        throw std::runtime_error("no mod " + std::string(id));
    }
    return *m;
}

std::string json_manifest(std::string_view id, std::string_view extra = "") {
    return std::string(R"({"id": ")") + std::string(id) + R"(", "version": "1.0.0")" +
           (extra.empty() ? "" : ", " + std::string(extra)) + "}";
}

}  // namespace

// ---- versions ----

SH_TEST(mods_version_parse) {
    SH_CHECK(Version::parse("1.2.3").value() == (Version{1, 2, 3, {}}));
    SH_CHECK(Version::parse("1").value() == (Version{1, 0, 0, {}}));
    SH_CHECK(Version::parse("v2.1").value() == (Version{2, 1, 0, {}}));
    SH_CHECK(Version::parse("1.0.0-beta.2+build7").value().prerelease == "beta.2");
    SH_CHECK(!Version::parse(""));
    SH_CHECK(!Version::parse("one"));
    SH_CHECK(!Version::parse("1.2.3.4"));
    SH_CHECK(Version::parse("1.0.0-rc").value() < Version::parse("1.0.0").value());
    SH_CHECK(Version::parse("1.10.0").value() > Version::parse("1.9.9").value());
}

SH_TEST(mods_version_ranges) {
    SH_CHECK(in_range("*", "0.0.1"));
    SH_CHECK(in_range("", "7.0.0"));
    SH_CHECK(in_range("1.2.3", "1.2.3"));
    SH_CHECK(!in_range("1.2.3", "1.2.4"));
    SH_CHECK(in_range("1.2", "1.2.9"));
    SH_CHECK(!in_range("1.2", "1.3.0"));
    SH_CHECK(in_range("1.x", "1.9.0"));
    SH_CHECK(!in_range("1.x", "2.0.0"));
    SH_CHECK(in_range("^1.2", "1.9.0"));
    SH_CHECK(!in_range("^1.2", "1.1.9"));
    SH_CHECK(!in_range("^1.2", "2.0.0"));
    SH_CHECK(in_range("^0.3", "0.3.5"));
    SH_CHECK(!in_range("^0.3", "0.4.0"));
    SH_CHECK(in_range("~1.2", "1.2.7"));
    SH_CHECK(!in_range("~1.2", "1.3.0"));
    SH_CHECK(in_range(">=1.0 <2", "1.5.0"));
    SH_CHECK(!in_range(">=1.0 <2", "2.0.0"));
    SH_CHECK(!in_range(">=1.0 <2", "2.0.0-rc"));
    SH_CHECK(in_range(">= 1.0", "1.0.0"));
    SH_CHECK(in_range(">1.2", "1.3.0"));
    SH_CHECK(!in_range(">1.2", "1.2.9"));
    SH_CHECK(in_range("<=2.1", "2.1.5"));
    SH_CHECK(in_range("^1 || ^3", "3.2.0"));
    SH_CHECK(!in_range("^1 || ^3", "2.0.0"));
    SH_CHECK(!VersionRange::parse(">=banana"));
    SH_CHECK(!VersionRange::parse("1 ||"));
}

// ---- manifest ----

SH_TEST(mods_manifest_json_full) {
    const ManifestResult r = parse_manifest_json(R"({
        "format": 1, "id": "hd_harry", "name": "HD Harry", "version": "1.2.0",
        "description": "Sharper Harry", "authors": ["A", "B"], "engine": ">=1.0", "api": "^1",
        "game": "SLUS-007.07", "dependencies": {"base_pack": "^2.0"}, "optional_dependencies": ["extras"],
        "conflicts": {"old_harry": "*"}, "load_after": ["x"], "load_before": ["y"], "priority": 5,
        "gameplay": false, "scripts": true, "icon": "icon.png", "_comment": "ignored"})",
                                                 "hd_harry");
    SH_CHECK(r.errors.empty());
    SH_CHECK(r.warnings.empty());
    SH_CHECK(r.manifest.has_value());
    const Manifest& m = *r.manifest;
    SH_CHECK(m.id == "hd_harry" && m.name == "HD Harry" && m.version.to_string() == "1.2.0");
    SH_CHECK(m.authors.size() == 2 && m.games.size() == 1);
    SH_CHECK(m.dependencies.size() == 1 && m.dependencies[0].versions.matches(Version{2, 5, 0, {}}));
    SH_CHECK(m.optional_dependencies.size() == 1 && m.optional_dependencies[0].versions.is_any());
    SH_CHECK(m.conflicts.size() == 1 && m.load_after.size() == 1 && m.load_before.size() == 1);
    SH_CHECK(m.priority == 5 && m.scripts && !m.gameplay && m.icon == "icon.png");
    SH_CHECK(m.engine && m.engine->matches(kEngineVersion));
}

SH_TEST(mods_manifest_json_errors) {
    SH_CHECK(!parse_manifest_json("{", "x").manifest);
    SH_CHECK(!parse_manifest_json(R"({"version": "1.0.0"})", "x").manifest);  // no id
    SH_CHECK(!parse_manifest_json(R"({"id": "x"})", "x").manifest);           // no version
    SH_CHECK(!parse_manifest_json(R"({"id": "bad id", "version": "1"})", "x").manifest);
    SH_CHECK(!parse_manifest_json(R"({"id": "silenthill", "version": "1"})", "x").manifest);
    SH_CHECK(!parse_manifest_json(R"({"id": "x", "version": "1", "dependencies": {"y": ">=what"}})", "x").manifest);
    SH_CHECK(!parse_manifest_json(R"({"id": "x", "version": "1", "gameplay": 1})", "x").manifest);

    const ManifestResult unknown = parse_manifest_json(R"({"id": "x", "version": "1", "colour": "red"})", "y");
    SH_CHECK(unknown.manifest.has_value());
    SH_CHECK(has(unknown.warnings, "unknown key 'colour'"));
    SH_CHECK(has(unknown.warnings, "folder is named 'y'"));

    const ManifestResult escape = parse_manifest_json(R"({"id": "x", "version": "1", "icon": "../../etc"})", "x");
    SH_CHECK(escape.manifest && escape.manifest->icon.empty());
}

SH_TEST(mods_manifest_legacy_cfg) {
    const ManifestResult r = parse_legacy_cfg(
        "\xEF\xBB\xBF# HD\r\nid = HD_TEST\r\nname = HD test # note\r\nversion = 1\r\npriority = 10\r\ngameplay = 0\r\n",
        "HD_TEST");
    SH_CHECK(r.manifest.has_value());
    SH_CHECK(r.manifest->legacy);
    SH_CHECK(r.manifest->id == "HD_TEST" && r.manifest->name == "HD test");
    SH_CHECK(r.manifest->version.to_string() == "1.0.0" && r.manifest->priority == 10);
    SH_CHECK(has(r.warnings, "old manifest format"));

    const ManifestResult folder = parse_legacy_cfg("", "plain");
    SH_CHECK(folder.manifest && folder.manifest->id == "plain");
}

// ---- load order ----

SH_TEST(mods_new_mods_are_off) {
    // Nothing chosen yet: a clean install plays like the original, the mods wait in priority order.
    const Plan plan = resolve_load_order({mod("b", "1.0.0", 5), mod("a", "1.0.0", 5), mod("c", "1.0.0", -1)}, {}, {});
    SH_CHECK(order_of(plan).empty());
    SH_CHECK(plan.mods.size() == 3 && plan.mods[0].candidate.manifest.id == "c" &&
             plan.mods[1].candidate.manifest.id == "a" && plan.mods[2].candidate.manifest.id == "b");
    SH_CHECK(get(plan, "a").state == ModState::Disabled && get(plan, "a").problems.empty());
}

SH_TEST(mods_order_priority_then_id) {
    const Plan plan = resolve_on({mod("b", "1.0.0", 5), mod("a", "1.0.0", 5), mod("c", "1.0.0", -1)});
    SH_CHECK(order_of(plan) == "c,a,b");
    SH_CHECK(get(plan, "c").load_index == 0 && get(plan, "b").load_index == 2);
}

SH_TEST(mods_order_dependencies_first) {
    Candidate top = mod("top", "1.0.0", -100);
    top.manifest.dependencies = {ref("base", "^1")};
    Candidate base = mod("base", "1.4.0", 100);
    const Plan plan = resolve_on({top, base});
    SH_CHECK(order_of(plan) == "base,top");
}

SH_TEST(mods_missing_dependency_disables) {
    Candidate a = mod("a");
    a.manifest.dependencies = {ref("ghost", ">=2")};
    Candidate b = mod("b");
    b.manifest.dependencies = {ref("a")};  // follows a
    const Plan plan = resolve_on({a, b, mod("c")});
    SH_CHECK(order_of(plan) == "c");
    SH_CHECK(get(plan, "a").state == ModState::Error);
    SH_CHECK(has(get(plan, "a").problems, "needs mod 'ghost' >=2, which is not installed"));
    SH_CHECK(has(get(plan, "b").problems, "which could not be loaded"));
}

SH_TEST(mods_dependency_version_out_of_range) {
    Candidate a = mod("a");
    a.manifest.dependencies = {ref("lib", "^2.0")};
    const Plan plan = resolve_on({a, mod("lib", "1.5.0")});
    SH_CHECK(order_of(plan) == "lib");
    SH_CHECK(has(get(plan, "a").problems, "installed is 1.5.0"));
}

SH_TEST(mods_dependency_switched_off) {
    Candidate a = mod("a");
    a.manifest.dependencies = {ref("lib")};
    UserState user;
    user.mods = {{"a", true}, {"lib", false}};
    const Plan plan = resolve_load_order({a, mod("lib")}, user, {});
    SH_CHECK(order_of(plan).empty());
    SH_CHECK(get(plan, "lib").state == ModState::Disabled);
    SH_CHECK(has(get(plan, "a").problems, "switched off"));
}

SH_TEST(mods_conflict_disables_the_declaring_mod) {
    Candidate a = mod("a");
    a.manifest.conflicts = {ref("b", "<2")};
    Plan plan = resolve_on({a, mod("b", "1.0.0")});
    SH_CHECK(order_of(plan) == "b");
    SH_CHECK(has(get(plan, "a").problems, "conflicts with mod 'b'"));
    plan = resolve_on({a, mod("b", "2.0.0")});  // out of the conflict range
    SH_CHECK(order_of(plan) == "a,b");
}

SH_TEST(mods_dependency_cycle) {
    Candidate a = mod("a");
    a.manifest.dependencies = {ref("b")};
    Candidate b = mod("b");
    b.manifest.dependencies = {ref("a")};
    Candidate c = mod("c");
    c.manifest.dependencies = {ref("a")};
    const Plan plan = resolve_on({a, b, c, mod("d")});
    SH_CHECK(order_of(plan) == "d");
    SH_CHECK(has(get(plan, "a").problems, "dependency cycle"));
    SH_CHECK(has(get(plan, "b").problems, "dependency cycle"));
    SH_CHECK(get(plan, "c").state == ModState::Error);
}

SH_TEST(mods_hints_and_contradictions) {
    Candidate a = mod("a", "1.0.0", 10);
    a.manifest.load_before = {"b"};
    Candidate b = mod("b");
    Candidate c = mod("c");
    c.manifest.load_after = {"d"};
    Candidate d = mod("d", "1.0.0", 50);
    Plan plan = resolve_on({a, b, c, d});
    SH_CHECK(order_of(plan) == "a,b,d,c");

    // b says "after a", a says "after b": the second hint is dropped with a warning, the order stays defined.
    Candidate x = mod("x");
    x.manifest.load_after = {"y"};
    Candidate y = mod("y");
    y.manifest.load_after = {"x"};
    plan = resolve_on({x, y});
    SH_CHECK(plan.active().size() == 2);
    SH_CHECK(has(get(plan, "x").warnings, "ignored") || has(get(plan, "y").warnings, "ignored"));
}

SH_TEST(mods_user_order_wins_over_priority) {
    UserState user;
    user.mods = {{"high", true}, {"low", true}};
    const Plan plan = resolve_load_order({mod("low", "1.0.0", 0), mod("high", "1.0.0", 99), mod("new", "1.0.0", -5)},
                                         user, {});
    // Listed mods keep the player's order; a new one stays off until switched on.
    SH_CHECK(order_of(plan) == "high,low");
    SH_CHECK(get(plan, "new").state == ModState::Disabled);
}

SH_TEST(mods_engine_and_game_requirements) {
    Candidate future = mod("future");
    future.manifest.engine = VersionRange::parse(">=9").value();
    Candidate pal = mod("pal");
    pal.manifest.games = {"SLES-01514"};
    Environment env;
    env.game = "SLUS-007.07";
    const Plan plan = resolve_on({future, pal, mod("ok")}, env);
    SH_CHECK(order_of(plan) == "ok");
    SH_CHECK(has(get(plan, "future").problems, "needs engine >=9"));
    SH_CHECK(has(get(plan, "pal").problems, "made for SLES-01514"));
}

// ---- the engine version a mod requires ----

SH_TEST(mods_engine_version_is_the_version_file) {
    std::ifstream in(fs::path(SH_PROJECT_ROOT) / "VERSION");
    std::string line;
    SH_CHECK(static_cast<bool>(std::getline(in, line)));
    SH_CHECK(line == sh::version::kText);
    SH_CHECK(kEngineVersion.to_string() == line);
    SH_CHECK(kEngineVersion == (Version{sh::version::kMajor, sh::version::kMinor, sh::version::kPatch, {}}));
    // The build metadata follows '+' and never takes part in a comparison.
    const std::string full = sh::version::full();
    SH_CHECK(full.starts_with(line + "+"));
    SH_CHECK(Version::parse(full).value() == kEngineVersion);
    SH_CHECK(Environment{}.engine == kEngineVersion);
}

SH_TEST(mods_engine_version_order) {
    const auto v = [](std::string_view text) { return Version::parse(text).value(); };
    SH_CHECK(v("1.2.0") < v("1.10.0"));  // numbers, not text
    SH_CHECK(v("1.9.9") < v("2.0.0"));
    SH_CHECK(v("1.0.1") > v("1.0.0"));
    SH_CHECK(v("1.2.0-beta") < v("1.2.0"));
    SH_CHECK(v("1.2.0+7.abc") == v("1.2.0"));
}

SH_TEST(mods_manifest_engine_version_keys) {
    const ManifestResult both = parse_manifest_json(
        json_manifest("x", R"("min_engine_version": "1.2.0", "max_engine_version": "2.0.3")"), "x");
    SH_CHECK(both.errors.empty() && both.warnings.empty());  // known keys: no "unknown key" warning
    SH_CHECK(both.manifest && both.manifest->min_engine_version == (Version{1, 2, 0, {}}));
    SH_CHECK(both.manifest->max_engine_version == (Version{2, 0, 3, {}}));
    const std::string written = write_manifest_json(*both.manifest);
    SH_CHECK(written.find(R"("min_engine_version": "1.2.0")") != std::string::npos);
    SH_CHECK(written.find(R"("max_engine_version": "2.0.3")") != std::string::npos);
    SH_CHECK(parse_manifest_json(written, "x").manifest->min_engine_version == (Version{1, 2, 0, {}}));

    const ManifestResult none = parse_manifest_json(json_manifest("x"), "x");
    SH_CHECK(none.manifest && !none.manifest->min_engine_version && !none.manifest->max_engine_version);

    // A malformed version is a manifest error: the mod is not read as "any engine".
    for (const char* bad : {R"("1.2")", R"("1")", R"(">=1.2.0")", R"("1.x")", R"("v1.2.0")", R"("1.2.0.0")",
                            R"("01.2.0")", R"("one")", R"("")", "1", "1.2", "true", R"(["1.2.0"])"}) {
        for (const char* key : {"min_engine_version", "max_engine_version"}) {
            const ManifestResult r =
                parse_manifest_json(json_manifest("x", "\"" + std::string(key) + "\": " + bad), "x");
            SH_CHECK(!r.manifest);
            SH_CHECK(has(r.errors, std::string(key) + " must be an engine version MAJOR.MINOR.PATCH"));
        }
    }
    const ManifestResult crossed = parse_manifest_json(
        json_manifest("x", R"("min_engine_version": "2.0.0", "max_engine_version": "1.0.0")"), "x");
    SH_CHECK(!crossed.manifest && has(crossed.errors, "is older than min_engine_version"));
}

SH_TEST(mods_engine_version_requirement) {
    const Version engine{1, 4, 2, {}};
    const auto problems = [&](const char* min, const char* max) {
        Manifest m;
        if (min != nullptr) m.min_engine_version = Version::parse(min).value();
        if (max != nullptr) m.max_engine_version = Version::parse(max).value();
        return engine_problems(m, engine);
    };
    SH_CHECK(problems(nullptr, nullptr).empty());
    SH_CHECK(problems("1.4.2", nullptr).empty());  // the limits themselves are allowed
    SH_CHECK(problems(nullptr, "1.4.2").empty());
    SH_CHECK(problems("1.0.0", "1.9.0").empty());
    SH_CHECK(problems("0.9.0", nullptr).empty());
    SH_CHECK(has(problems("1.4.3", nullptr), "needs engine 1.4.3 or newer, this is 1.4.2"));
    SH_CHECK(has(problems("1.10.0", nullptr), "needs engine 1.10.0 or newer"));
    SH_CHECK(has(problems("2.0.0", nullptr), "needs engine 2.0.0 or newer"));
    SH_CHECK(has(problems(nullptr, "1.4.1"), "needs engine 1.4.1 or older, this is 1.4.2"));
    SH_CHECK(has(problems(nullptr, "0.9.9"), "needs engine 0.9.9 or older"));
    SH_CHECK(problems("1.5.0", "1.6.0").size() == 1);
}

SH_TEST(mods_engine_version_load_decision) {
    Environment env;
    env.engine = Version{1, 0, 0, {}};
    Candidate newer = mod("newer");
    newer.manifest.min_engine_version = Version{1, 2, 0, {}};
    Candidate older = mod("older");
    older.manifest.max_engine_version = Version{0, 9, 0, {}};
    Candidate fits = mod("fits");
    fits.manifest.min_engine_version = Version{1, 0, 0, {}};
    fits.manifest.max_engine_version = Version{1, 0, 0, {}};
    Candidate addon = mod("addon");
    addon.manifest.dependencies = {ref("newer")};  // refused with it
    Candidate extra = mod("extra");
    extra.manifest.optional_dependencies = {ref("newer")};  // works without it

    const std::vector<Candidate> all = {newer, older, fits, addon, extra};
    Plan plan = resolve_on(all, env);
    SH_CHECK(order_of(plan) == "extra,fits");
    SH_CHECK(get(plan, "newer").state == ModState::Error && get(plan, "newer").problems.size() == 1);
    SH_CHECK(has(get(plan, "newer").problems, "needs engine 1.2.0 or newer, this is 1.0.0"));
    SH_CHECK(has(get(plan, "older").problems, "needs engine 0.9.0 or older, this is 1.0.0"));
    SH_CHECK(get(plan, "addon").state == ModState::Error);
    SH_CHECK(has(get(plan, "addon").problems, "needs mod 'newer', which could not be loaded"));

    // The same mods on the engine they ask for.
    env.engine = Version{1, 2, 0, {}};
    plan = resolve_on(all, env);
    SH_CHECK(get(plan, "newer").state == ModState::Active && get(plan, "addon").state == ModState::Active);
    SH_CHECK(get(plan, "fits").state == ModState::Error);

    // Never switched on: the Mods screen still lists the mod as one this engine cannot run.
    env.engine = Version{1, 0, 0, {}};
    plan = resolve_load_order({newer, fits}, {}, env);
    SH_CHECK(get(plan, "newer").state == ModState::Error);
    SH_CHECK(has(get(plan, "newer").problems, "needs engine 1.2.0 or newer"));
    SH_CHECK(get(plan, "fits").state == ModState::Disabled && get(plan, "fits").problems.empty());
}

SH_TEST(mods_examples_engine_version) {
    const auto read = [](const char* folder) {
        std::ifstream in(fs::path(SH_PROJECT_ROOT) / "examples/mods" / folder / "mod.json", std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        return parse_manifest_json(text.str(), folder);
    };
    const ManifestResult needs = read("needs_newer_engine");
    SH_CHECK(needs.errors.empty() && needs.warnings.empty() && needs.manifest.has_value());
    const ManifestResult example = read("example_mod");
    SH_CHECK(example.warnings.empty() && example.manifest && example.manifest->min_engine_version.has_value());

    Candidate refused;
    refused.manifest = *needs.manifest;
    refused.folder = "needs_newer_engine";
    Candidate accepted;
    accepted.manifest = *example.manifest;
    accepted.folder = "example_mod";
    const Plan plan = resolve_on({refused, accepted});  // this engine
    SH_CHECK(order_of(plan) == "example_mod");
    SH_CHECK(has(get(plan, "needs_newer_engine").problems,
                 "needs engine 99.0.0 or newer, this is " + kEngineVersion.to_string()));
}

SH_TEST(mods_duplicate_id) {
    Candidate one = mod("same");
    one.folder = "one";
    Candidate two = mod("same");
    two.folder = "two";
    const Plan plan = resolve_on({one, two});
    SH_CHECK(plan.active().size() == 1);
    SH_CHECK(plan.mods.size() == 2);
    SH_CHECK(has(plan.mods[1].problems, "already used by folder 'one'"));
}

// ---- user state ----

SH_TEST(mods_user_state_round_trip) {
    UserState s;
    s.mods = {{"a", true}, {"b", false}};
    s.notices = {"legacy:/x"};
    const auto back = parse_user_state(write_user_state(s));
    SH_CHECK(back);
    SH_CHECK(back.value().mods.size() == 2 && !back.value().mods[1].enabled && back.value().notices.size() == 1);
    SH_CHECK(!parse_user_state("[1]"));
}

// ---- registry: discovery, resolution, overrides, edits, migration ----

SH_TEST(mods_registry_resolution_priority) {
    Scratch dir("registry");
    dir.write("gamedata/convert/textures/CHARA/HERO.png", "base");
    dir.write("mods/low/mod.json", json_manifest("low", R"("priority": 1)"));
    dir.write("mods/low/convert/textures/CHARA/HERO.png", "low");
    dir.write("mods/low/convert/textures/CHARA/NEW.png", "new");
    dir.write("mods/high/mod.json", json_manifest("high", R"("priority": 2)"));
    dir.write("mods/high/convert/textures/chara/hero.png", "high");  // other case: same file
    dir.write("mods/high/README.md", "not data");
    dir.write("mods/broken/mod.json", json_manifest("broken", R"("dependencies": {"missing": "^1"})"));
    dir.write("mods/broken/convert/textures/CHARA/HERO.png", "broken");
    dir.write("mods/nomanifest/convert/x.png", "x");

    Registry reg;
    RegistryConfig config;
    config.dirs = {dir.path() / "mods"};
    config.base_dir = dir.path() / "gamedata";
    config.legacy_dirs = {dir.path() / "gamedata" / "load"};
    reg.configure(config);

    SH_CHECK(reg.active().size() == 2);
    const auto hero = reg.find("convert/textures/CHARA/HERO.png");
    SH_CHECK(hero && hero->parent_path().parent_path().parent_path().parent_path().filename() == "high");
    SH_CHECK(reg.owner_of("convert\\textures\\CHARA\\HERO.png") == "high");
    SH_CHECK(reg.find_all("convert/textures/CHARA/HERO.png").size() == 2);
    SH_CHECK(!reg.find("convert/textures/CHARA/NONE.png"));
    SH_CHECK(!reg.find("README.md"));

    const std::vector<ModFile> low = reg.files("low");
    SH_CHECK(low.size() == 2);
    for (const ModFile& f : low) {
        if (f.path.ends_with("HERO.png")) {
            SH_CHECK(f.replaces && !f.wins && f.also_in.size() == 1);
        } else {
            SH_CHECK(!f.replaces && f.wins);
        }
    }
    const std::vector<std::string> report = reg.report(true);
    SH_CHECK(has(report, "2 of 3 mods active"));
    SH_CHECK(has(report, "broken 1.0.0 \"broken\": error; needs mod 'missing' ^1, which is not installed"));
    SH_CHECK(has(report, "nomanifest: no mod.json"));
    SH_CHECK(has(report, "overridden by later mods"));
    SH_CHECK(reg.notices().empty());

    // discover() of a configured folder returns the running set in load order.
    std::vector<std::string> warnings;
    SH_CHECK(!reg.covers(dir.path()));
    SH_CHECK(reg.covers(dir.path() / "mods"));
    const std::vector<ModInfo> standalone = discover(dir.path() / "mods", warnings);
    SH_CHECK(standalone.size() == 2 && standalone[0].id == "low" && standalone[1].id == "high");
    SH_CHECK(has(warnings, "broken: mod disabled"));
}

// A mod's .lua files count only with "scripts": true: the index leaves them out, the report says why.
SH_TEST(mods_registry_lua_needs_scripts_flag) {
    Scratch dir("registry_lua");
    dir.write("mods/plain/mod.json", json_manifest("plain"));
    dir.write("mods/plain/maps/map1_s02/events.lua", "-- not run");
    dir.write("mods/plain/maps/map1_s02/handlers.json", "{}");
    dir.write("mods/coder/mod.json", json_manifest("coder", R"("scripts": true)"));
    dir.write("mods/coder/maps/map1_s03/EVENTS.LUA", "-- run");

    Registry reg;
    RegistryConfig config;
    config.dirs = {dir.path() / "mods"};
    reg.configure(config);
    SH_CHECK(!reg.find("maps/map1_s02/events.lua"));
    SH_CHECK(reg.find("maps/map1_s02/handlers.json"));  // its data still applies
    SH_CHECK(reg.find("maps/map1_s03/events.lua"));
    SH_CHECK(reg.files("plain").size() == 1);
    const std::string why = "mod plain has Lua files but mod.json does not say \"scripts\": true";
    SH_CHECK(has(reg.report(false), why));
    SH_CHECK(has(reg.pending().find("plain")->warnings, why));

    // The same rule for a folder outside the registry (tests, tools).
    std::vector<std::string> warnings;
    SH_CHECK(!find_in(dir.path() / "mods", "maps/map1_s02/events.lua", warnings));
    SH_CHECK(find_in(dir.path() / "mods", "maps/map1_s02/handlers.json", warnings));
    SH_CHECK(is_lua_path("a/B.Lua") && !is_lua_path("a/b.luac") && !is_lua_path("lua"));
}

SH_TEST(mods_registry_edits_and_save) {
    Scratch dir("edits");
    dir.write("mods/a/mod.json", json_manifest("a"));
    dir.write("mods/b/mod.json", json_manifest("b"));
    dir.write("mods/c/mod.json", json_manifest("c", R"("dependencies": ["a"])"));

    Registry reg;
    RegistryConfig config;
    config.dirs = {dir.path() / "mods"};
    reg.configure(config);
    SH_CHECK(order_of(reg.plan()) == "a,b,c");
    SH_CHECK(!reg.restart_pending());

    SH_CHECK(reg.move("b", -1));
    SH_CHECK(order_of(reg.pending()) == "b,a,c");
    SH_CHECK(order_of(reg.plan()) == "a,b,c");  // the running set does not change
    SH_CHECK(reg.restart_pending());

    SH_CHECK(reg.set_enabled("a", false));
    SH_CHECK(order_of(reg.pending()) == "b");  // c needs a
    SH_CHECK(reg.pending().find("c")->state == ModState::Error);
    SH_CHECK(reg.save());

    Registry next;
    next.configure(config);
    SH_CHECK(order_of(next.plan()) == "b");
    SH_CHECK(next.plan().find("a")->state == ModState::Disabled);
    SH_CHECK(next.set_enabled("a", true));
    SH_CHECK(order_of(next.pending()) == "b,a,c");
    SH_CHECK(!next.set_enabled("nope", true));
}

SH_TEST(mods_registry_migration_notice_once) {
    Scratch dir("migration");
    dir.write("gamedata/load/CHARA/HERO.TIM.png", "old");
    dir.write("mods/old/mod.cfg", "id = old\nversion = 1\n");
    dir.write("mods/skip.disabled/mod.json", json_manifest("skip"));

    Registry reg;
    RegistryConfig config;
    config.dirs = {dir.path() / "mods"};
    config.legacy_dirs = {dir.path() / "gamedata" / "load", dir.path() / "gamedata" / "texturemods"};
    reg.configure(config);
    SH_CHECK(reg.active().size() == 1);  // the mod.cfg one; .disabled is skipped
    SH_CHECK(has(reg.report(false), "folders ending in .disabled are skipped"));
    const std::vector<std::string> first = reg.take_new_notices();
    SH_CHECK(first.size() == 2);
    SH_CHECK(has(first, "gamedata/load is no longer read"));
    SH_CHECK(has(first, "old mod.cfg"));

    Registry again;
    again.configure(config);
    SH_CHECK(again.take_new_notices().empty());
    SH_CHECK(again.notices().size() == 2);  // still listed by the console
}

SH_TEST(mods_discover_unconfigured_folder) {
    Scratch dir("discover");
    dir.write("x/mod.json", json_manifest("x", R"("priority": 3, "gameplay": true, "scripts": true)"));
    dir.write("y/mod.cfg", "priority = 1\n");
    std::vector<std::string> warnings;
    const std::vector<ModInfo> mods = discover(dir.path(), warnings);
    SH_CHECK(mods.size() == 2);
    SH_CHECK(mods[0].id == "y" && mods[1].id == "x");
    SH_CHECK(mods[1].gameplay && mods[1].scripts && mods[1].version == "1.0.0" && mods[1].load_index == 1);
    SH_CHECK(has(warnings, "old manifest format"));
    std::vector<std::string> none;
    SH_CHECK(discover(dir.path() / "absent", none).empty() && none.empty());
}

SH_TEST(mods_manifest_requires_restart) {
    const ManifestResult live = parse_manifest_json(R"({"id": "x", "version": "1", "requires_restart": false})", "x");
    SH_CHECK(live.manifest && !live.manifest->requires_restart && live.warnings.empty());
    SH_CHECK(!parse_manifest_json(write_manifest_json(*live.manifest), "x").manifest->requires_restart);
    SH_CHECK(parse_manifest_json(R"({"id": "x", "version": "1"})", "x").manifest->requires_restart);
    // Scripts load only at start, whatever the manifest says.
    const ManifestResult code =
        parse_manifest_json(R"({"id": "x", "version": "1", "scripts": true, "requires_restart": false})", "x");
    SH_CHECK(code.manifest && code.manifest->requires_restart && has(code.warnings, "requires_restart"));
}

SH_TEST(mods_registry_new_mods_off_live_apply_and_dependencies) {
    Scratch dir("live");
    dir.write("mods/lib/mod.json", json_manifest("lib"));
    dir.write("mods/skin/mod.json", json_manifest("skin", R"("requires_restart": false)"));
    dir.write("mods/skin/convert/textures/CHARA/HERO.png", "skin");
    dir.write("mods/user/mod.json", json_manifest("user", R"("dependencies": {"lib": "*"})"));
    fs::remove(dir.path() / "mods" / "load_order.json");  // a clean install: nothing chosen yet

    Registry reg;
    RegistryConfig config;
    config.dirs = {dir.path() / "mods"};
    reg.configure(config);
    SH_CHECK(reg.active().empty());
    SH_CHECK(!reg.find("convert/textures/CHARA/HERO.png"));
    SH_CHECK(has(reg.report(false), "0 of 3 mods active"));

    // A mod that needs no restart applies at once, after the save.
    SH_CHECK(reg.set_enabled("skin", true));
    SH_CHECK(!reg.restart_pending() && reg.live_pending());
    SH_CHECK(reg.save());
    SH_CHECK(reg.apply_live());
    SH_CHECK(order_of(reg.plan()) == "skin");
    SH_CHECK(reg.owner_of("convert/textures/CHARA/HERO.png") == "skin");
    SH_CHECK(!reg.live_pending() && !reg.apply_live());

    // Switching a mod on brings its required dependency along; those need a restart.
    SH_CHECK(reg.set_enabled("user", true));
    SH_CHECK(reg.pending().find("lib")->state == ModState::Active);
    SH_CHECK(reg.pending().find("user")->state == ModState::Active);
    SH_CHECK(reg.restart_pending() && !reg.live_pending());
    SH_CHECK(order_of(reg.plan()) == "skin");

    // The console's edit says when a change applies.
    const sh::Result<std::string> later = edit_and_save(reg, "user", "disable");
    SH_CHECK(later && later.value().find("after restart") != std::string::npos);
}
