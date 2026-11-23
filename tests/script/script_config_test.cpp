// sh.config and the mod settings store, sh.menu.add_page, rows bound with config = "<key>".

#include "framework/sh_test.h"

#include "engine/menu/menu_model.h"
#include "engine/script/bind_menu.h"
#include "engine/script/mod_settings.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "framework/mod_fixture.h"

namespace {

namespace fs = std::filesystem;
using sh::menu::Controller;
using sh::menu::Event;
using sh::menu::MenuInput;
using sh::menu::Registry;
using sh::menu::RowKind;
using sh::menu::Value;
using sh::script::ModSettings;
using sh::script::SettingDecl;
using sh::script::SettingType;

struct Scratch {
    fs::path root;
    std::vector<std::string> lines;
    explicit Scratch(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "script" / name) {
        fs::remove_all(root);
        fs::create_directories(root / "gamedata" / "menus");
        fs::create_directories(root / "mods");
        sh::script::mod_settings().clear();
    }
    ~Scratch() {
        sh::script::mod_settings().clear();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void write(const fs::path& rel, const std::string& text) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
        sh::test::mod_file_written(root / rel);
    }
    std::string read(const fs::path& rel) const {
        std::ifstream in(root / rel, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
    void mod(const char* folder, const char* file, const std::string& text) {
        write(fs::path("mods") / folder / "mod.json",
              std::string(R"({"id": ")") + folder + R"(", "version": "1.0.0", "scripts": true})");
        write(fs::path("mods") / folder / "menus" / file, text);
    }
    void load(Registry& r) {
        sh::script::load_menu_scripts(root / "gamedata", root / "mods", r,
                                      [this](int, const std::string& m) { lines.push_back(m); });
    }
    bool logged(const std::string& needle) const {
        for (const auto& l : lines) {
            if (l.find(needle) != std::string::npos) return true;
        }
        return false;
    }
};

MenuInput press_down() { MenuInput i; i.down = true; return i; }
MenuInput press_right() { MenuInput i; i.right = true; return i; }
MenuInput press_left() { MenuInput i; i.left = true; return i; }

const char* kBaseOptions = R"(
sh.menu.define{ id = "options", title = "Options", rows = {
  { kind = "submenu", id = "display", label = "Display", submenu = "options.display" },
  { kind = "action", id = "tail.reset", label = "Reset", activate = function() end },
  { kind = "action", id = "tail.exit", label = "Exit", activate = function() end },
}}
sh.menu.define{ id = "options.display", title = "Display" }
)";

}  // namespace

SH_TEST(script_config_add_page_goes_before_tail_rows_and_replaces_its_link) {
    Scratch s("config_add_page");
    s.write("gamedata/menus/options.lua", kBaseOptions);
    s.mod("mymod", "options.lua", R"(
local first = sh.menu.add_page{ parent = "options", id = "mymod.options", title = "My Mod",
  rows = { { kind = "info", id = "i", text = "hello" } } }
assert(first == true)
-- A second add (a reload) replaces the link instead of adding another one.
local again = sh.menu.add_page{ parent = "options", id = "mymod.options", title = "My Mod",
  label = "My Mod Options", help = "mymod.help", rows = { { kind = "info", id = "i2", text = "x" } } }
assert(again == true)
local orphan = sh.menu.add_page{ parent = "nothere", id = "mymod.orphan", title = "Orphan" }
assert(orphan == false)
)");
    Registry r;
    s.load(r);
    const auto* opts = r.find("options");
    SH_CHECK(opts != nullptr);
    SH_CHECK(opts->rows.size() == 4);
    SH_CHECK(opts->rows[0].id == "display");
    SH_CHECK(opts->rows[1].kind == RowKind::Submenu && opts->rows[1].submenu == "mymod.options");
    SH_CHECK(opts->rows[1].label == "My Mod Options" && opts->rows[1].help == "mymod.help");
    SH_CHECK(opts->rows[2].id == "tail.reset" && opts->rows[3].id == "tail.exit");
    const auto* page = r.find("mymod.options");
    SH_CHECK(page != nullptr && page->title == "My Mod" && page->rows.size() == 1 && page->rows[0].id == "i2");
    SH_CHECK(r.find("mymod.orphan") != nullptr);  // defined even without a parent
    SH_CHECK(s.logged("nothere"));
    SH_CHECK(!s.logged("file skipped"));
}

SH_TEST(script_config_add_page_label_defaults_to_title) {
    Scratch s("config_add_page_label");
    s.write("gamedata/menus/options.lua", kBaseOptions);
    s.mod("mymod", "options.lua",
          R"(sh.menu.add_page{ parent = "options", id = "mymod.options", title = "My Mod" })");
    Registry r;
    s.load(r);
    const auto* opts = r.find("options");
    SH_CHECK(opts != nullptr && opts->rows.size() == 4 && opts->rows[1].label == "My Mod");
}

// With the game's Mod Options page (gamedata/menus/mods.lua) a mod's Options page is linked there, not on the root.
SH_TEST(script_config_add_page_of_a_mod_goes_to_mod_options) {
    Scratch s("config_add_page_mod_options");
    s.write("gamedata/menus/options.lua", kBaseOptions);
    s.write("gamedata/menus/mods.lua", R"(
sh.menu.after_load(function()
  if sh.menu.page_count("options.mod_options") == 0 then return end
  sh.menu.add_page{ parent = "options", id = "options.mod_options", title = "Mod Options", rows = {} }
end)
)");
    s.mod("mymod", "options.lua",
          R"(assert(sh.menu.add_page{ parent = "options", id = "mymod:options", title = "My Mod" } == true))");
    Registry r;
    s.load(r);
    const auto* opts = r.find("options");
    SH_CHECK(opts != nullptr && opts->rows.size() == 4 && opts->rows[1].submenu == "options.mod_options");
    const auto* mods = r.find("options.mod_options");
    SH_CHECK(mods != nullptr && mods->rows.size() == 1 && mods->rows[0].submenu == "mymod:options");
    SH_CHECK(r.page_count("options.mod_options") == 1 && !s.logged("file skipped"));

    // No mod page: the Options root gets no Mod Options row.
    Scratch bare("config_add_page_mod_options_none");
    bare.write("gamedata/menus/options.lua", kBaseOptions);
    bare.write("gamedata/menus/mods.lua", R"(
sh.menu.after_load(function()
  if sh.menu.page_count("options.mod_options") == 0 then return end
  sh.menu.add_page{ parent = "options", id = "options.mod_options", title = "Mod Options", rows = {} }
end)
)");
    Registry none;
    bare.load(none);
    SH_CHECK(none.find("options")->rows.size() == 3 && none.find("options.mod_options") == nullptr);
}

SH_TEST(script_config_store_declare_set_validate_persist) {
    Scratch s("config_store");
    s.write("mods_config.cfg", "# my comment\nother.key = kept\nmymod.speed = 2.5\nmymod.mode = bogus\n");
    const fs::path file = s.root / "mods_config.cfg";
    {
        ModSettings st;
        std::string err;
        SH_CHECK(st.load(file, err));

        SettingDecl speed;
        speed.key = "mymod.speed";
        speed.min = 0;
        speed.max = 4;
        speed.step = 0.5;
        speed.def = Value{1.0};
        SH_CHECK(st.declare(speed, err));
        SH_CHECK(sh::menu::as_double(*st.get("mymod.speed")) == 2.5);  // the file wins
        SH_CHECK(sh::menu::as_double(st.find("mymod.speed")->def) == 1.0);

        SettingDecl mode;
        mode.key = "mymod.mode";
        mode.type = SettingType::Choice;
        mode.choices = {{"slow", "Slow"}, {"fast", "Fast"}};
        mode.restart = true;
        SH_CHECK(st.declare(mode, err));
        SH_CHECK(sh::menu::as_string(*st.get("mymod.mode")) == "slow");  // "bogus" is not a choice

        SettingDecl flag;
        flag.key = "mymod.sub.flag";
        flag.type = SettingType::Bool;
        SH_CHECK(st.declare(flag, err));

        SettingDecl name;
        name.key = "mymod.name";
        name.type = SettingType::String;
        name.def = Value{std::string("Harry")};
        SH_CHECK(st.declare(name, err));

        // Bad keys and bad declarations.
        for (const char* bad : {"speed", ".speed", "mymod.", "my mod.x", "mymod..x", "mymod:x"}) {
            SettingDecl d;
            d.key = bad;
            err.clear();
            SH_CHECK(!st.declare(d, err) && !err.empty());
        }
        SettingDecl empty_choice;
        empty_choice.key = "mymod.c";
        empty_choice.type = SettingType::Choice;
        SH_CHECK(!st.declare(empty_choice, err));
        SettingDecl bad_default;
        bad_default.key = "mymod.d";
        bad_default.min = 0;
        bad_default.max = 1;
        bad_default.def = Value{5.0};
        SH_CHECK(!st.declare(bad_default, err));

        // Validation.
        err.clear();
        SH_CHECK(!st.set("mymod.speed", Value{9.0}, err) && !err.empty());
        err.clear();
        SH_CHECK(!st.set("mymod.speed", Value{std::string("x")}, err));
        err.clear();
        SH_CHECK(!st.set("mymod.mode", Value{std::string("medium")}, err));
        err.clear();
        SH_CHECK(!st.set("mymod.sub.flag", Value{std::int64_t{1}}, err));
        err.clear();
        SH_CHECK(!st.set("mymod.nope", Value{true}, err));
        SH_CHECK(sh::menu::as_double(*st.get("mymod.speed")) == 2.5);

        // Listeners and restart.
        int calls = 0;
        double seen = 0;
        SH_CHECK(st.on_change("mymod.speed", [&](const Value& v) { ++calls; seen = sh::menu::as_double(v); }, err));
        err.clear();
        SH_CHECK(st.set("mymod.speed", Value{std::int64_t{3}}, err));
        SH_CHECK(calls == 1 && seen == 3.0);
        SH_CHECK(st.set("mymod.speed", Value{3.0}, err));
        SH_CHECK(calls == 1);  // unchanged: no call
        SH_CHECK(!st.needs_restart());
        SH_CHECK(st.set("mymod.mode", Value{std::string("fast")}, err));
        SH_CHECK(st.needs_restart());
        SH_CHECK(st.set("mymod.mode", Value{std::string("slow")}, err));
        SH_CHECK(!st.needs_restart());  // back to the value it started with
        SH_CHECK(st.set("mymod.mode", Value{std::string("fast")}, err));
        SH_CHECK(st.set("mymod.sub.flag", Value{true}, err));
        SH_CHECK(st.set("mymod.name", Value{std::string(" two words ")}, err));
        SH_CHECK(st.reset("mymod.speed", err));
        SH_CHECK(sh::menu::as_double(*st.get("mymod.speed")) == 1.0 && calls == 2);
        SH_CHECK(st.set("mymod.speed", Value{0.5}, err));
    }
    const std::string text = s.read("mods_config.cfg");
    SH_CHECK(text.find("# my comment") == 0);
    SH_CHECK(text.find("other.key = kept") != std::string::npos);
    SH_CHECK(text.find("mymod.speed = 0.5") != std::string::npos);
    SH_CHECK(text.find("mymod.mode = fast") != std::string::npos);
    SH_CHECK(text.find("mymod.sub.flag = true") != std::string::npos);
    SH_CHECK(!fs::exists(s.root / "mods_config.cfg.tmp"));
    {
        ModSettings st;
        std::string err;
        SH_CHECK(st.load(file, err));
        SettingDecl speed;
        speed.key = "mymod.speed";
        speed.min = 0;
        speed.max = 4;
        SH_CHECK(st.declare(speed, err));
        SettingDecl mode;
        mode.key = "mymod.mode";
        mode.type = SettingType::Choice;
        mode.choices = {{"slow", "Slow"}, {"fast", "Fast"}};
        SH_CHECK(st.declare(mode, err));
        SettingDecl flag;
        flag.key = "mymod.sub.flag";
        flag.type = SettingType::Bool;
        SH_CHECK(st.declare(flag, err));
        SettingDecl name;
        name.key = "mymod.name";
        name.type = SettingType::String;
        SH_CHECK(st.declare(name, err));
        SH_CHECK(sh::menu::as_double(*st.get("mymod.speed")) == 0.5);
        SH_CHECK(sh::menu::as_string(*st.get("mymod.mode")) == "fast");
        SH_CHECK(sh::menu::as_bool(*st.get("mymod.sub.flag")));
        SH_CHECK(sh::menu::as_string(*st.get("mymod.name")) == " two words ");
    }
}

SH_TEST(script_config_lua_api_and_bad_keys) {
    Scratch s("config_lua");
    std::string err;
    SH_CHECK(sh::script::mod_settings().load(s.root / "mods_config.cfg", err));
    s.mod("mymod", "settings.lua", R"(
assert(sh.config.declare{ key = "mymod.speed", type = "number", default = 1, min = 0, max = 2, step = 0.25 })
assert(sh.config.declare{ key = "mymod.on", type = "bool", default = true })
local ok, msg = sh.config.declare{ key = "speed", type = "number" }
assert(ok == false and type(msg) == "string")
assert(sh.config.declare{ key = "mymod.x", type = "colour" } == false)
assert(sh.config.get("mymod.speed") == 1)
assert(sh.config.get("mymod.missing") == nil)
local seen
assert(sh.config.on_change("mymod.speed", function(v) seen = v end))
assert(sh.config.set("mymod.speed", 1.5))
assert(seen == 1.5)
assert(sh.config.set("mymod.speed", 7) == false)
assert(sh.config.set("mymod.on", "yes") == false)
assert(sh.config.default("mymod.speed") == 1)
assert(sh.config.reset("mymod.speed"))
assert(sh.config.get("mymod.speed") == 1 and seen == 1)
assert(sh.config.needs_restart() == false)
assert(sh.config.on_change("mymod.none", function() end) == false)
sh.menu.define{ id = "mymod.after", title = "reached" }
)");
    Registry r;
    s.load(r);
    SH_CHECK(r.find("mymod.after") != nullptr);  // the file ran to the end
    SH_CHECK(s.logged("'speed' is not a mod setting key"));
    SH_CHECK(s.logged("unknown type 'colour'"));
    SH_CHECK(s.logged("outside"));
    SH_CHECK(!s.logged("file skipped"));
    // C++ sees what Lua wrote.
    SH_CHECK(sh::script::mod_settings().set("mymod.speed", Value{0.5}, err));
    SH_CHECK(s.read("mods_config.cfg").find("mymod.speed = 0.5") != std::string::npos);
}

SH_TEST(script_config_rows_bind_to_the_store) {
    Scratch s("config_rows");
    std::string err;
    SH_CHECK(sh::script::mod_settings().load(s.root / "mods_config.cfg", err));
    s.write("gamedata/menus/options.lua", kBaseOptions);
    s.mod("mymod", "options.lua", R"(
sh.config.declare{ key = "mymod.enabled", type = "bool", default = false }
sh.config.declare{ key = "mymod.volume", type = "number", default = 0.5, min = 0, max = 1, step = 0.1 }
sh.config.declare{ key = "mymod.mode", type = "choice", default = "b",
  choices = { { value = "a", label = "Mode A" }, { value = "b", label = "Mode B" }, "c" } }
sh.config.declare{ key = "mymod.count", type = "number", default = 2, min = 0, max = 10, step = 1 }
sh.menu.add_page{ parent = "options", id = "mymod.options", title = "My Mod", rows = {
  { kind = "toggle", id = "en", label = "Enabled", config = "mymod.enabled" },
  { kind = "slider", id = "vol", label = "Volume", config = "mymod.volume", percent = true },
  { kind = "choice", id = "mode", label = "Mode", config = "mymod.mode" },
  { kind = "slider", id = "count", label = "Count", config = "mymod.count", max = 5 },
  { kind = "toggle", id = "bad", label = "Bad", config = "mymod.undeclared" },
  { kind = "toggle", id = "wrong", label = "Wrong", config = "mymod.volume" },
}}
)");
    Registry r;
    s.load(r);
    SH_CHECK(!s.logged("file skipped"));
    Controller c(r);
    SH_CHECK(c.open("mymod.options"));
    auto rows = c.rows();
    SH_CHECK(rows.size() == 6);
    SH_CHECK(rows[0].value == "ui.menu.off" && rows[0].has_arrows);
    SH_CHECK(rows[1].value == "50%" && rows[1].fraction > 0.49 && rows[1].fraction < 0.51);
    SH_CHECK(rows[2].value == "Mode B" && rows[2].value_is_key);
    SH_CHECK(rows[3].value == "2");
    SH_CHECK(rows[4].value == "ui.menu.off");  // inert, drawn, no crash
    SH_CHECK(s.logged("mymod.undeclared") && s.logged("cannot show config key 'mymod.volume'"));

    SH_CHECK(c.update(press_right()) == Event::Changed);  // toggle on
    SH_CHECK(sh::menu::as_bool(*sh::script::mod_settings().get("mymod.enabled")));
    SH_CHECK(c.rows()[0].value == "ui.menu.on");

    c.update(press_down());
    SH_CHECK(c.update(press_right()) == Event::Changed);  // 0.5 -> 0.6
    SH_CHECK(c.rows()[1].value == "60%");
    SH_CHECK(sh::menu::as_double(*sh::script::mod_settings().get("mymod.volume")) == 0.6);
    SH_CHECK(s.read("mods_config.cfg").find("mymod.volume = 0.6\n") != std::string::npos);
    for (int i = 0; i < 10; ++i) c.update(press_right());  // clamped at the declared max
    SH_CHECK(c.rows()[1].value == "100%");

    c.update(press_down());
    SH_CHECK(c.update(press_right()) == Event::Changed);  // b -> c
    SH_CHECK(sh::menu::as_string(*sh::script::mod_settings().get("mymod.mode")) == "c");
    SH_CHECK(c.rows()[2].value == "c");

    c.update(press_down());
    for (int i = 0; i < 8; ++i) c.update(press_right());  // the row's own max (5) wins
    SH_CHECK(c.rows()[3].value == "5");
    SH_CHECK(sh::menu::as_double(*sh::script::mod_settings().get("mymod.count")) == 5.0);
    c.update(press_left());
    SH_CHECK(c.rows()[3].value == "4");

    c.update(press_down());
    SH_CHECK(c.update(press_right()) == Event::None);  // the unbound row does nothing

    const std::string text = s.read("mods_config.cfg");
    SH_CHECK(text.find("mymod.enabled = true") != std::string::npos);
    SH_CHECK(text.find("mymod.volume = 1") != std::string::npos);
    SH_CHECK(text.find("mymod.mode = c") != std::string::npos);
    SH_CHECK(text.find("mymod.count = 4") != std::string::npos);
}
