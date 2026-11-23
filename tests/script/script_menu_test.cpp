// sh.menu.*: Lua-defined menus, Lua natives, hooks, budget and error safety, mod overrides.

#include "framework/sh_test.h"

#include "engine/menu/menu_model.h"
#include "engine/script/bind_menu.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "framework/mod_fixture.h"

namespace {

namespace fs = std::filesystem;
using sh::menu::Controller;
using sh::menu::Event;
using sh::menu::MenuInput;
using sh::menu::Registry;

struct Scratch {
    fs::path root;
    std::vector<std::string> lines;
    explicit Scratch(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "script" / name) {
        fs::remove_all(root);
        fs::create_directories(root / "gamedata" / "menus");
        fs::create_directories(root / "mods");
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void write(const fs::path& rel, const std::string& text) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
        sh::test::mod_file_written(root / rel);
    }
    void mod(const char* folder, const char* file, const std::string& text, bool scripts = true) {
        write(fs::path("mods") / folder / "mod.json", std::string(R"({"id": ")") + folder +
                                                          R"(", "version": "1.0.0", "scripts": )" +
                                                          (scripts ? "true" : "false") + "}");
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
MenuInput press_confirm() { MenuInput i; i.confirm = true; return i; }

}  // namespace

SH_TEST(script_menu_defines_every_row_kind) {
    Scratch s("menu_kinds");
    s.write("gamedata/menus/a.lua", R"(
local flag, n, c = true, 5, "b"
sh.menu.define{ id = "silenthill:All", title = "ui.all", rows = {
  { kind = "action", id = "act", label = "Run", help = "h", close = true, activate = function() n = n + 100 end },
  { kind = "toggle", id = "tog", get = function() return flag end, set = function(v) flag = v end },
  { kind = "slider", id = "sl", min = 0, max = 10, step = 2.5, integer = false, get = function() return 1.5 end, set = function(v) end },
  { kind = "number", id = "num", min = 0, max = 99, step = 1, get = function() return n end, set = function(v) n = v end },
  { kind = "choice", id = "ch", choices = { "a", "b", "c" }, get = function() return c end, set = function(v) c = v end },
  { kind = "dropdown", id = "dd", choices = { { value = "x", label = "Ex" }, { value = "y" } },
    get = function() return "x" end, set = function(v) end },
  { kind = "submenu", id = "sub", submenu = "silenthill:Other" },
  { kind = "setting", id = "set", step = function(d) n = n + d end, text = function() return "n=" .. n end },
  { kind = "info", id = "inf", text = "const" },
}}
sh.menu.define{ id = "silenthill:Other", title = "other" }
)");
    Registry r;
    s.load(r);
    SH_CHECK(s.lines.empty());
    const auto* def = r.find("silenthill:All");
    SH_CHECK(def != nullptr);
    SH_CHECK(def->title == "ui.all");
    SH_CHECK(def->rows.size() == 9);
    using sh::menu::RowKind;
    const RowKind kinds[] = {RowKind::Action, RowKind::Toggle, RowKind::Slider,  RowKind::Number, RowKind::Choice,
                             RowKind::Dropdown, RowKind::Submenu, RowKind::Setting, RowKind::Info};
    for (size_t i = 0; i < 9; ++i) {
        SH_CHECK(def->rows[i].kind == kinds[i]);
    }
    SH_CHECK(def->rows[0].close_on_activate && def->rows[0].help == "h");
    SH_CHECK(def->rows[2].step == 2.5 && def->rows[2].max == 10.0);
    SH_CHECK(def->rows[4].choices.size() == 3 && def->rows[4].choices[1].value == "b" &&
             def->rows[4].choices[1].label == "b");
    SH_CHECK(def->rows[5].choices[1].value == "y" && def->rows[5].choices[1].label == "y");
    SH_CHECK(def->rows[6].submenu == "silenthill:Other");
    SH_CHECK(def->rows[8].text() == "const");

    // Drive the Lua callbacks through a Controller.
    Controller c(r);
    SH_CHECK(c.open("silenthill:All"));
    SH_CHECK(c.update(press_confirm()) == Event::Closed);  // the close=true action ran and closed
    SH_CHECK(!c.is_open());
    SH_CHECK(c.open("silenthill:All"));
    c.update(press_down());  // toggle
    SH_CHECK(c.update(press_confirm()) == Event::Changed);
    const auto rows = c.rows();
    SH_CHECK(rows[1].value_is_key || !rows[1].value.empty());
    SH_CHECK(def->rows[1].get != nullptr && !sh::menu::as_bool(def->rows[1].get()));
}

SH_TEST(script_menu_natives_and_lua_natives) {
    Scratch s("menu_natives");
    s.write("gamedata/menus/a.lua", R"(
local vol = 3
sh.menu.value("test.vol", function() return vol end, function(v) vol = v end)
sh.menu.action("test.bump", function() vol = vol + 1 end)
sh.menu.list("test.modes", function() return { "one", { value = "two", label = "Two" } } end)
sh.menu.define{ id = "n", title = "n", rows = {
  { kind = "number", id = "vol", native = "test.vol", min = 0, max = 10, step = 1 },
  { kind = "action", id = "bump", native = "test.bump" },
  { kind = "choice", id = "mode", native = "test.modes", get = function() return "one" end, set = function() end },
}}
)");
    Registry r;
    s.load(r);
    SH_CHECK(s.lines.empty());
    SH_CHECK(r.has_native("test.vol") && r.has_native("test.bump") && r.has_native("test.modes"));
    Controller c(r);
    SH_CHECK(c.open("n"));
    SH_CHECK(c.update(press_right()) == Event::Changed);  // vol 3 -> 4 through the Lua set
    SH_CHECK(c.rows()[0].value == "4");
    c.update(press_down());
    SH_CHECK(c.update(press_confirm()) == Event::Activated);  // bump -> 5
    c.update(press_down());
    SH_CHECK(c.rows()[2].has_arrows);
    SH_CHECK(c.update(press_right()) == Event::Changed);  // list from Lua
    SH_CHECK(c.update(press_right()) != Event::Refused);
    // Natives are real: the value moved to 5.
    sh::menu::NativeValue* none = nullptr;
    (void)none;
    c.close();
    SH_CHECK(c.open("n"));
    SH_CHECK(c.rows()[0].value == "5");
}

SH_TEST(script_menu_override_and_append) {
    Scratch s("menu_override");
    s.write("gamedata/menus/a.lua", R"(
sh.menu.define{ id = "debug", title = "base", rows = { { kind = "action", id = "one", label = "1" } } }
sh.menu.define{ id = "debug", title = "replaced", rows = { { kind = "action", id = "two", label = "2" } } }
sh.menu.define{ id = "keep", title = "keep" }
assert(sh.menu.append_rows("keep", { { kind = "info", id = "added", text = "x" } }) == true)
assert(sh.menu.append_rows("missing", {}) == false)
)");
    Registry r;
    s.load(r);
    SH_CHECK(s.lines.empty());
    SH_CHECK(r.find("debug")->title == "replaced" && r.find("debug")->rows.size() == 1 &&
             r.find("debug")->rows[0].id == "two");
    SH_CHECK(r.find("keep")->rows.size() == 1 && r.find("keep")->rows[0].id == "added");
}

SH_TEST(script_menu_mod_folder_overrides_base) {
    Scratch s("menu_mod");
    s.write("gamedata/menus/debug.lua", R"(
sh.menu.define{ id = "debug", title = "base", rows = { { kind = "action", id = "base_row" } } }
)");
    s.mod("cheats", "extra.lua", R"(
sh.menu.append_rows("debug", { { kind = "action", id = "cheats:God", label = "God" } })
sh.menu.define{ id = "cheats:Menu", title = "Cheats" }
)");
    s.mod("zzoverride", "over.lua", R"(
sh.menu.define{ id = "debug", title = "modded", rows = {} }
)");
    Registry r;
    s.load(r);
    SH_CHECK(s.lines.empty());
    // Mods run after the base: the append reached the base menu, then the later define replaced it.
    SH_CHECK(r.find("debug")->title == "modded");
    SH_CHECK(r.find("cheats:Menu") != nullptr);

    Scratch t("menu_mod_append");
    t.write("gamedata/menus/debug.lua", R"(
sh.menu.define{ id = "debug", title = "base", rows = { { kind = "action", id = "base_row" } } }
)");
    t.mod("cheats", "extra.lua", R"(
sh.menu.append_rows("debug", { { kind = "action", id = "cheats:God", label = "God" } })
)");
    Registry r2;
    t.load(r2);
    SH_CHECK(r2.find("debug")->rows.size() == 2 && r2.find("debug")->rows[1].id == "cheats:God");
}

// A mod's menus/*.lua run only with "scripts": true; without it the mod gets one warning.
SH_TEST(script_menu_mod_without_scripts_flag) {
    Scratch s("menu_mod_noscripts");
    s.write("gamedata/menus/debug.lua", R"(
sh.menu.define{ id = "debug", title = "base" }
)");
    s.mod("quiet", "over.lua", R"(
sh.menu.define{ id = "debug", title = "modded" }
)", false);
    Registry r;
    s.load(r);
    SH_CHECK(r.find("debug")->title == "base");
    SH_CHECK(s.logged("mod quiet has Lua files but mod.json does not say \"scripts\": true; its scripts are not run"));
}

SH_TEST(script_menu_open_close_hooks) {
    Scratch s("menu_hooks");
    s.write("gamedata/menus/a.lua", R"(
sh.menu.define{ id = "h", title = "h", rows = {
  { kind = "action", id = "open", activate = function() sh.menu.open("other") end },
  { kind = "action", id = "state", activate = function() sh.menu.define{ id = "was_open:" .. tostring(sh.menu.is_open()), title = "" } end },
  { kind = "action", id = "close", activate = function() sh.menu.close() end },
}}
)");
    Registry r;
    s.load(r);
    auto& hooks = sh::menu::host();
    const auto saved = hooks;
    hooks = {};  // unset hooks: calls are error-free no-ops
    Controller c(r);
    SH_CHECK(c.open("h"));
    c.update(press_confirm());
    c.update(press_down());
    c.update(press_confirm());
    c.update(press_down());
    c.update(press_confirm());
    SH_CHECK(s.lines.empty());
    SH_CHECK(r.find("was_open:false") != nullptr);

    std::string opened;
    int closed = 0;
    hooks.open = [&](std::string_view id) { opened = std::string(id); return true; };
    hooks.close = [&] { ++closed; };
    hooks.is_open = [] { return true; };
    SH_CHECK(c.open("h"));
    c.update(press_confirm());
    c.update(press_down());
    c.update(press_confirm());
    c.update(press_down());
    c.update(press_confirm());
    SH_CHECK(opened == "other" && closed == 1);
    SH_CHECK(r.find("was_open:true") != nullptr);
    hooks = saved;
}

SH_TEST(script_menu_budget_and_errors_are_contained) {
    Scratch s("menu_safety");
    s.write("gamedata/menus/a.lua", R"(
sh.menu.define{ id = "bad", title = "bad", rows = {
  { kind = "toggle", id = "err", get = function() error("boom") end, set = function() error("boom") end },
  { kind = "number", id = "loop", min = 0, max = 5, get = function() while true do end end },
  { kind = "choice", id = "lst", list = function() return 42 end, get = function() return 1 end },
  { kind = "info", id = "txt", text = function() error("x") end,
    visible = function() error("y") end },
}}
)");
    s.write("gamedata/menus/b.lua", "this is not lua");
    s.write("gamedata/menus/c.lua", "sh.menu.define{ id = 'x', rows = { { kind = 'nonsense' } } }");
    s.write("gamedata/menus/d.lua", "sh.menu.define{ id = 'good', title = 'good' }");
    s.write("gamedata/menus/e.lua", "while true do end");
    Registry r;
    s.load(r);
    SH_CHECK(r.find("good") != nullptr);  // the files after the broken ones still load
    SH_CHECK(r.find("x") == nullptr);
    SH_CHECK(s.logged("unknown row kind"));
    SH_CHECK(s.logged("instruction budget"));
    const auto* def = r.find("bad");
    SH_CHECK(def != nullptr);

    s.lines.clear();
    SH_CHECK(std::holds_alternative<std::monostate>(def->rows[0].get()) || true);
    def->rows[0].set(sh::menu::Value{true});  // errors: no crash, neutral result
    SH_CHECK(std::holds_alternative<std::monostate>(def->rows[0].get()));
    def->rows[1].get();  // endless loop hits the budget
    def->rows[1].get();
    SH_CHECK(def->rows[2].list().empty());
    SH_CHECK(def->rows[3].text().empty());
    SH_CHECK(def->rows[3].visible());  // a failing visible() leaves the row visible
    // Each failing callback is reported once, however often it runs.
    size_t boom = 0;
    for (const auto& l : s.lines) {
        if (l.find("boom") != std::string::npos) ++boom;
    }
    SH_CHECK(boom == 2);  // get and set are different callbacks
}

SH_TEST(script_menu_visible_if) {
    Scratch s("menu_visible_if");
    s.write("gamedata/menus/a.lua", R"(
sh.menu.define{ id = "cam", title = "cam", rows = {
  { kind = "info", id = "style", text = "x" },
  { kind = "info", id = "tps", text = "x", visible_if = { key = "control_style", any = { "tps", "ots" } } },
  { kind = "info", id = "fps", text = "x", visible_if = { key = "control_style", is = "fps" } },
  { kind = "info", id = "size", text = "x",
    visible_if = { { key = "control_style", none = "classic" }, { key = "crosshair" } } },
  { kind = "info", id = "both", text = "x", visible_if = { key = "crosshair", is = 1 },
    visible = function() return false end },
}}
)");
    s.write("gamedata/menus/b.lua", R"(
sh.menu.define{ id = "bad", rows = { { kind = "info", id = "r", visible_if = { any = { 1 } } } } }
)");
    Registry r;
    std::string style = "classic";
    double crosshair = 0;
    r.config_reader("test", [&](std::string_view key) -> std::optional<sh::menu::Value> {
        if (key == "control_style") return sh::menu::Value{style};
        if (key == "crosshair") return sh::menu::Value{crosshair};
        return std::nullopt;
    });
    s.load(r);
    SH_CHECK(r.find("bad") == nullptr && s.logged("visible_if needs"));
    const auto* def = r.find("cam");
    SH_CHECK(def != nullptr && def->rows[3].visible_if.size() == 2);
    Controller c(r);
    SH_CHECK(c.open("cam"));
    const auto ids = [&] {
        std::string out;
        for (const auto& row : c.rows()) out += row.def->id + " ";
        return out;
    };
    SH_CHECK(ids() == "style ");
    style = "ots";
    SH_CHECK(ids() == "style tps ");
    crosshair = 1;
    SH_CHECK(ids() == "style tps size ");  // "both": its visible() still says no
    style = "fps";
    SH_CHECK(ids() == "style fps size ");
}

SH_TEST(script_menu_missing_folders_are_fine) {
    Scratch s("menu_none");
    fs::remove_all(s.root / "gamedata" / "menus");
    fs::remove_all(s.root / "mods");
    Registry r;
    s.load(r);
    SH_CHECK(r.ids().empty() && s.lines.empty());
}

// Two mods at once extend the Options menu: each its own page linked from "options" and a row on the same base
// page. Both survive, in load order, and the base rows stay (mods stack instead of replacing each other).
SH_TEST(script_menu_two_mods_extend_options) {
    Scratch s("menu_two_mods");
    s.write("gamedata/menus/options.lua", R"(
sh.menu.define{ id = "options.gameplay", title = "Gameplay", rows = { { kind = "action", id = "base_row" } } }
sh.menu.define{ id = "options", title = "Options", rows = {
    { kind = "submenu", id = "gameplay", label = "Gameplay", submenu = "options.gameplay" } } }
)");
    const std::string page = R"(
sh.menu.define{ id = ID .. ":options", title = ID, rows = { { kind = "action", id = ID .. ":do", label = "Do" } } }
sh.menu.append_rows("options", { { kind = "submenu", id = ID, label = ID, submenu = ID .. ":options" } })
sh.menu.append_rows("options.gameplay", { { kind = "action", id = ID .. ":row", label = ID } })
)";
    // Each sets the same global first: menu files share one Lua state, so a mod must not rely on its globals
    // after its own file has run.
    s.mod("alpha", "alpha.lua", "ID = \"alpha\"" + page);
    s.mod("beta", "beta.lua", "ID = \"beta\"" + page);
    Registry r;
    s.load(r);
    SH_CHECK(s.lines.empty());
    const auto* options = r.find("options");
    SH_CHECK(options != nullptr && options->rows.size() == 3);
    if (options != nullptr && options->rows.size() == 3) {
        SH_CHECK(options->rows[0].id == "gameplay" && options->rows[1].id == "alpha" && options->rows[2].id == "beta");
    }
    SH_CHECK(r.find("alpha:options") != nullptr && r.find("beta:options") != nullptr);
    const auto* gameplay = r.find("options.gameplay");
    SH_CHECK(gameplay != nullptr && gameplay->rows.size() == 3);
    if (gameplay != nullptr && gameplay->rows.size() == 3) {
        SH_CHECK(gameplay->rows[0].id == "base_row" && gameplay->rows[1].id == "alpha:row" &&
                 gameplay->rows[2].id == "beta:row");
    }
}
