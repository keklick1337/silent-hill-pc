// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/devtools/lua_ide.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "engine/script/script_host.h"
#include "framework/sh_test.h"

namespace dt = sh::devtools;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> param_names(const std::string& sig) {
    std::vector<std::string> out;
    for (const auto& [b, e] : dt::signature_parts(sig).params) out.push_back(sig.substr(b, e - b));
    return out;
}

// A game script state with nothing loaded but an empty game.lua, as the tests of a mod see it.
struct TestState {
    fs::path root = fs::temp_directory_path() / "sh_lua_ide_test";
    std::vector<std::string> log;
    std::unique_ptr<sh::script::Host> host;
    TestState() {
        fs::create_directories(root / "scripts");
        std::ofstream(root / "scripts" / "game.lua", std::ios::binary) << "shared_value = 42\n";
        sh::script::HostConfig c;
        c.gamedata_dir = root;
        c.log = [this](int, const std::string& m) { log.push_back(m); };
        host = std::make_unique<sh::script::Host>(c);
        SH_CHECK(host->load().value());
    }
    ~TestState() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    dt::LuaTestReport run(const std::string& code) {
        std::string out;
        host->run_test_file(dt::lua_test_harness(), code, "mymod/tests/t.lua", &out);
        return dt::parse_lua_test_output(out);
    }
};

}  // namespace

SH_TEST(lua_ide_signature_parameters) {
    SH_CHECK(param_names("sh.give_item(item[, n]) -> count") == (std::vector<std::string>{"item", "n"}));
    SH_CHECK(param_names("math.random([m[, n]]) -> number") == (std::vector<std::string>{"m", "n"}));
    SH_CHECK(param_names("sh.hooks.on(event, fn[, {priority = 0, id = \"name\"}]) -> handle") ==
             (std::vector<std::string>{"event", "fn", "{priority = 0, id = \"name\"}"}));
    SH_CHECK(param_names("sh.game_state_update()").empty());
    SH_CHECK(param_names("sh.npc").empty());
    const dt::LuaSignatureParts table = dt::signature_parts("sh.render.background{texture}");
    SH_CHECK(table.table_call);
    SH_CHECK(dt::active_parameter(table, 0) == -1);
}

SH_TEST(lua_ide_active_parameter) {
    const dt::LuaSignatureParts p = dt::signature_parts("sh.chara_spawn(chara, spawn_flags, x, z[, rot[, step]])");
    SH_CHECK(p.params.size() == 6);
    SH_CHECK(dt::active_parameter(p, 0) == 0);
    SH_CHECK(dt::active_parameter(p, 5) == 5);
    SH_CHECK(dt::active_parameter(p, 6) == -1);
    const dt::LuaSignatureParts v = dt::signature_parts("sh.log_fmt(fmt, ...)");
    SH_CHECK(v.variadic);
    SH_CHECK(dt::active_parameter(v, 7) == 1);
}

SH_TEST(lua_ide_test_runner_pass_fail_and_order) {
    TestState s;
    const dt::LuaTestReport r = s.run(R"lua(
local helper = shared_value
function test_b_fails() expect_eq(helper + 1, 44, "sum") end
function test_a_passes() expect(helper == 42) end
test("registered first", function() print("hello from a test") end)
test("errors", function() error("boom") end)
)lua");
    SH_CHECK(r.error.empty());
    SH_CHECK(r.tests.size() == 4);
    SH_CHECK(r.tests[0].name == "registered first" && r.tests[0].passed);
    SH_CHECK(r.tests[1].name == "errors" && !r.tests[1].passed);
    SH_CHECK(r.tests[1].message.find("t.lua:6: boom") != std::string::npos);
    SH_CHECK(r.tests[2].name == "test_a_passes" && r.tests[2].passed);
    SH_CHECK(r.tests[3].name == "test_b_fails" && !r.tests[3].passed);
    SH_CHECK(r.tests[3].message.find("sum: expected 44, got 43") != std::string::npos);
    SH_CHECK(r.passed() == 2 && r.failed() == 2);
    SH_CHECK(r.output.size() == 1 && r.output[0].find("hello from a test") != std::string::npos);
}

SH_TEST(lua_ide_test_runner_keeps_globals_and_reports_broken_files) {
    TestState s;
    SH_CHECK(s.run("leaked = 1\nfunction test_x() end").tests.size() == 1);
    std::string out;
    SH_CHECK(s.host->run_snippet("leaked == nil", &out));
    SH_CHECK(out == "true");
    const dt::LuaTestReport bad = s.run("function test_x( end");
    SH_CHECK(!bad.error.empty() && bad.tests.empty());
    const dt::LuaTestReport top = s.run("error('at load')");
    SH_CHECK(top.error.find("at load") != std::string::npos);
}

SH_TEST(lua_ide_map_variables) {
    std::vector<dt::MapVarRow> rows;
    SH_CHECK(dt::read_map_var_rows(R"({"vars": {"timer": {"storage": "m/timer", "type": "s32"},
                                                 "ids": {"symbol": "ids", "type": "u16", "count": 3}}})",
                                   rows));
    SH_CHECK(dt::read_map_var_rows(R"({"vars": {"timer": {"storage": "other"}}})", rows));
    SH_CHECK(rows.size() == 2 && rows[0].where == "storage m/timer");
    SH_CHECK(!dt::read_map_var_rows("[]", rows));
    const std::vector<std::string> keys = dt::map_var_keys(rows, 2);
    SH_CHECK(keys == (std::vector<std::string>{"timer", "ids[0]", "ids[1]"}));
    SH_CHECK(dt::map_var_read_snippet(keys).find("g(\"ids\", 1)") != std::string::npos);
    SH_CHECK(dt::parse_map_var_values("5\n7", 3) == (std::vector<std::string>{"5", "7", "?"}));
    SH_CHECK(dt::map_var_write_snippet("ids[1]", " 12 ") == "sh.map.set(\"ids\", 12, 1)");
    SH_CHECK(dt::map_var_write_snippet("timer", "true") == "sh.map.set(\"timer\", true)");
    SH_CHECK(dt::map_var_write_snippet("timer", "os.exit()").empty());
}
