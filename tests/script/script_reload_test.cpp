// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// `mods reload` beyond the file index: the game host runs the game script and the mods' scripts/main.lua again in a
// new state (sh::script::reload_host: the old state's hook handlers and console commands go with it), and the menu
// scripts define their menus again without the rows a mod appended piling up (sh::script::reload_menu_scripts).

#include "engine/console/instance.h"
#include "engine/hooks/hook_bus.h"
#include "engine/menu/menu_model.h"
#include "engine/script/bind_menu.h"
#include "engine/script/script_host.h"
#include "framework/mod_fixture.h"
#include "framework/sh_test.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace con = sh::console;

namespace {

struct Scratch {
    fs::path root;
    explicit Scratch(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "script_reload" / name) {
        fs::remove_all(root);
        fs::create_directories(root / "gamedata");
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void write(const fs::path& rel, std::string_view text) const {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
        sh::test::mod_file_written(root / rel);
    }
};

struct Collect final : con::Output {
    std::vector<std::string> lines;
    void print(con::Level, std::string_view line) override { lines.emplace_back(line); }
};

bool contains(const std::vector<std::string>& lines, std::string_view part) {
    for (const std::string& l : lines) {
        if (l.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string main_lua(const char* version) {
    return std::string(R"lua(
        function on_boot()
            sh.log("boot )lua") + version + R"lua(")
            sh.hooks.on("test.reload_ping", function(ev) sh.log("ping )lua" + version + R"lua(") end)
            sh.console.register{name = "reload_probe", run = function() return ")lua" + version + R"lua(" end}
        end
    )lua";
}

}  // namespace

SH_TEST(script_reload_host_runs_mod_scripts_again) {
    Scratch dir("host");
    dir.write("mods/probe/mod.json", R"({"id": "probe", "version": "1.0.0", "scripts": true})");
    dir.write("mods/probe/scripts/main.lua", main_lua("v1"));

    std::vector<std::string> lines;
    sh::script::HostConfig c;
    c.gamedata_dir = dir.root / "gamedata";
    c.mods_dir = dir.root / "mods";
    c.log = [&lines](int, const std::string& m) { lines.push_back(m); };
    auto host = std::make_unique<sh::script::Host>(c);
    SH_CHECK(host->load().value());
    host->on_boot();
    SH_CHECK(contains(lines, "boot v1"));

    dir.write("mods/probe/scripts/main.lua", main_lua("v2"));
    SH_CHECK(sh::script::reload_host(host, c).value());
    SH_CHECK(host != nullptr);
    SH_CHECK(contains(lines, "boot v2"));

    // One handler and one command, both of the new state.
    sh::hooks::Event ev("test.reload_ping");
    sh::hooks::bus().emit(ev);
    SH_CHECK(contains(lines, "ping v2") && !contains(lines, "ping v1"));
    SH_CHECK(sh::hooks::bus().handlers("test.reload_ping").size() == 1);
    Collect out;
    SH_CHECK(con::global_registry().execute("reload_probe", out));
    SH_CHECK(contains(out.lines, "v2") && !contains(out.lines, "v1"));

    // A main.lua that fails now: the mod drops out, the host stays.
    dir.write("mods/probe/scripts/main.lua", "error('broken on purpose')");
    SH_CHECK(sh::script::reload_host(host, c).value());
    SH_CHECK(host != nullptr && host->loaded_mods().empty());
    SH_CHECK(sh::hooks::bus().handlers("test.reload_ping").empty());
    SH_CHECK(!con::global_registry().execute("reload_probe", out));
    host.reset();
}

SH_TEST(script_reload_menus_define_again) {
    Scratch dir("menus");
    dir.write("gamedata/menus/debug.lua", R"(
sh.menu.define{ id = "debug", title = "base", rows = { { kind = "action", id = "base_row" } } }
)");
    dir.write("mods/extra/mod.json", R"({"id": "extra", "version": "1.0.0", "scripts": true})");
    dir.write("mods/extra/menus/extra.lua", R"(
sh.menu.append_rows("debug", { { kind = "action", id = "extra:One", label = "one" } })
)");
    std::vector<std::string> lines;
    const auto log = [&lines](int, const std::string& m) { lines.push_back(m); };
    sh::menu::Registry r;
    sh::script::load_menu_scripts(dir.root / "gamedata", dir.root / "mods", r, log);
    SH_CHECK(r.find("debug") != nullptr && r.find("debug")->rows.size() == 2);

    sh::menu::Controller open(r);
    SH_CHECK(open.open("debug"));
    const std::uint64_t before = r.generation();

    dir.write("mods/extra/menus/extra.lua", R"(
sh.menu.append_rows("debug", { { kind = "action", id = "extra:Two", label = "two" } })
sh.menu.define{ id = "extra:Menu", title = "Extra" }
)");
    sh::script::reload_menu_scripts(dir.root / "gamedata", dir.root / "mods", r, log);
    const sh::menu::MenuDef* debug = r.find("debug");
    SH_CHECK(debug != nullptr && debug->rows.size() == 2 && debug->rows[1].id == "extra:Two");
    SH_CHECK(r.find("extra:Menu") != nullptr);
    SH_CHECK(r.generation() != before);
    // The open menu copies the new rows and its old definition stays readable meanwhile.
    open.update(sh::menu::MenuInput{});
    SH_CHECK(open.title() == "base");
    SH_CHECK(lines.empty());
}
