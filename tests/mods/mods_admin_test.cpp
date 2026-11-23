// Registry::reindex, the shared edits (mods_admin.h) and the `mods` console command.

#include "engine/console/registry.h"
#include "engine/mods/mods_admin.h"
#include "engine/mods/mods_console.h"
#include "engine/mods/registry.h"
#include "framework/mod_fixture.h"
#include "framework/sh_test.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace sh::mods;

namespace {

struct Dir {
    fs::path path;
    explicit Dir(const char* name) : path(fs::path(SH_PROJECT_ROOT) / ".tmp" / "mods_admin" / name) {
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path / "mods");
    }
    ~Dir() {
        set_admin_registry(nullptr);
        clear_reloaders();
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    void write(const std::string& rel, std::string_view text) const {
        fs::create_directories((path / rel).parent_path());
        std::ofstream(path / rel, std::ios::binary) << text;
        sh::test::mod_file_written(path / rel);
    }
    RegistryConfig config() const {
        RegistryConfig c;
        c.dirs = {path / "mods"};
        c.base_dir = path / "gamedata";
        return c;
    }
};

std::string manifest(std::string_view id) {
    return std::string(R"({"id": ")") + std::string(id) + R"(", "version": "1.0.0"})";
}

struct Lines : sh::console::Output {
    std::vector<std::string> lines;
    void print(sh::console::Level, std::string_view line) override { lines.emplace_back(line); }
    bool has(std::string_view part) const {
        return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
    }
};

}  // namespace

SH_TEST(mods_reindex_sees_new_and_removed_files) {
    Dir d("reindex");
    d.write("mods/a/mod.json", manifest("a"));
    d.write("mods/a/convert/textures/OLD.png", "1");
    d.write("mods/b/mod.json", manifest("b"));
    Registry reg;
    reg.configure(d.config());
    SH_CHECK(reg.move("b", -1));  // a pending edit that reindex must keep
    SH_CHECK(reg.find("convert/textures/OLD.png").has_value());

    d.write("mods/a/convert/textures/NEW.png", "2");
    d.write("mods/b/convert/textures/OLD.png", "3");
    fs::remove(d.path / "mods/a/convert/textures/OLD.png");
    d.write("mods/c/mod.json", manifest("c"));  // a new mod is not picked up: that needs a restart
    SH_CHECK(!reg.find("convert/textures/NEW.png").has_value());
    reg.reindex();
    SH_CHECK(reg.owner_of("convert/textures/NEW.png") == "a");
    SH_CHECK(reg.owner_of("convert/textures/OLD.png") == "b");
    SH_CHECK(reg.files("a").size() == 1);
    SH_CHECK(reg.plan().find("c") == nullptr);
    SH_CHECK(reg.restart_pending());  // the move is still pending
    SH_CHECK(reg.pending().mods.front().candidate.manifest.id == "b");
}

SH_TEST(mods_hot_reload_runs_reloaders) {
    Dir d("hot");
    d.write("mods/a/mod.json", manifest("a"));
    Registry reg;
    reg.configure(d.config());
    int calls = 0;
    add_reloader("text", [&]() { ++calls; return std::string("reread"); });
    d.write("mods/a/text/en/x.json", "{}");
    const std::vector<std::string> lines = hot_reload(reg);
    SH_CHECK(calls == 1);
    SH_CHECK(lines.size() == 3);
    SH_CHECK(lines[0].find("0 -> 1") != std::string::npos);
    SH_CHECK(lines[1] == "text: reread");
}

SH_TEST(mods_console_command) {
    Dir d("console");
    d.write("mods/a/mod.json", manifest("a"));
    d.write("mods/a/convert/textures/CHARA/HERO.png", "a");
    d.write("mods/a/convert/textures/NEW.png", "a");
    d.write("mods/b/mod.json", manifest("b"));
    d.write("mods/b/convert/textures/CHARA/HERO.png", "b");
    d.write("gamedata/convert/textures/CHARA/HERO.png", "base");
    Registry reg;
    reg.configure(d.config());
    set_admin_registry(&reg);
    sh::console::Registry console;
    register_console_commands(console);

    Lines out;
    SH_CHECK(console.execute("mods", out));
    SH_CHECK(out.has("2 of 2 mods active"));

    out.lines.clear();
    SH_CHECK(console.execute("mods files a", out));
    SH_CHECK(out.has("replace convert/textures/CHARA/HERO.png  (overridden by a later mod; also in b)"));
    SH_CHECK(out.has("add     convert/textures/NEW.png"));
    SH_CHECK(out.has("a: 2 files, 1 replaced, 1 added, 1 overridden"));

    out.lines.clear();
    SH_CHECK(console.execute("mods disable b", out));
    SH_CHECK(out.has("b disabled; applies after restart"));
    SH_CHECK(fs::exists(d.path / "mods" / "load_order.json"));
    SH_CHECK(reg.restart_pending());
    out.lines.clear();
    SH_CHECK(console.execute("mods", out));
    SH_CHECK(out.has("apply after restart"));

    out.lines.clear();
    SH_CHECK(console.execute("mods up b", out));
    SH_CHECK(out.has("b moved up") || out.has("b is already first"));
    SH_CHECK(console.execute("mods enable b", out));
    SH_CHECK(!console.execute("mods enable nope", out));
    SH_CHECK(!console.execute("mods files", out));

    out.lines.clear();
    SH_CHECK(console.execute("mods reload", out));
    SH_CHECK(out.has("indexed again"));
}

SH_TEST(mods_edit_gate_and_restart_handler) {
    SH_CHECK(editable());  // no gate: tools and tests may edit
    bool title = false;
    set_edit_gate([&]() { return title; });
    SH_CHECK(!editable());
    title = true;
    SH_CHECK(editable());
    set_edit_gate(nullptr);

    SH_CHECK(!request_restart());  // no handler: the program cannot restart itself
    int restarts = 0;
    set_restart_handler([&]() {
        ++restarts;
        return sh::Result<void>::ok();
    });
    SH_CHECK(request_restart() && restarts == 1);
    set_restart_handler(nullptr);
}
