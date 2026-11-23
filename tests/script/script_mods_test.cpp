// sh.mods.* over a scratch mod registry, and the Mods screen (gamedata/menus/mods.lua) built from it.

#include "framework/mod_fixture.h"
#include "framework/sh_test.h"

#include "engine/menu/menu_model.h"
#include "engine/mods/mods_admin.h"
#include "engine/mods/registry.h"
#include "engine/script/bind_menu.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using sh::menu::Controller;
using sh::menu::MenuInput;
using sh::menu::RowKind;

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

struct Scratch {
    fs::path root;
    std::vector<std::string> lines;
    sh::mods::Registry mods;

    explicit Scratch(const char* name) : root(fs::path(SH_PROJECT_ROOT) / ".tmp" / "script_mods" / name) {
        fs::remove_all(root);
        fs::create_directories(root / "gamedata" / "menus");
        fs::create_directories(root / "mods");
    }
    ~Scratch() {
        sh::mods::set_admin_registry(nullptr);
        sh::mods::set_edit_gate(nullptr);
        sh::mods::set_restart_handler(nullptr);
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void write(const fs::path& rel, const std::string& text) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
        sh::test::mod_file_written(root / rel);
    }
    void configure() {
        sh::mods::RegistryConfig config;
        config.dirs = {root / "mods"};
        config.base_dir = root / "gamedata";
        mods.configure(config);
        sh::mods::set_admin_registry(&mods);
    }
    // The shipped Mods screen, plus the shipped options.lua (or a stand-in) it attaches to.
    void copy_mods_screen(bool real_options) {
        const fs::path menus = fs::path(SH_PROJECT_ROOT) / "gamedata" / "menus";
        fs::copy_file(menus / "mods.lua", root / "gamedata" / "menus" / "mods.lua");
        if (real_options) {
            fs::copy_file(menus / "options.lua", root / "gamedata" / "menus" / "options.lua");
        } else {
            write("gamedata/menus/options.lua", "sh.menu.define{ id = 'options', title = 'ui.options.title' }");
        }
    }
    void load(sh::menu::Registry& r) {
        sh::script::load_menu_scripts(root / "gamedata", {}, r,
                                      [this](int, const std::string& m) { lines.push_back(m); });
    }
};

MenuInput press_confirm() { MenuInput i; i.confirm = true; return i; }
MenuInput press_cancel() { MenuInput i; i.cancel = true; return i; }

std::vector<std::string> labels(const sh::menu::MenuDef& def) {
    std::vector<std::string> out;
    for (const auto& r : def.rows) out.push_back(r.label);
    return out;
}

bool has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

const sh::menu::RowDef* row(const sh::menu::MenuDef& def, const std::string& id) {
    for (const auto& r : def.rows) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

void two_mods(Scratch& s) {
    s.write("mods/alpha/mod.json",
            R"({"id": "alpha", "name": "Alpha Pack", "version": "1.2.0", "authors": ["Ann", "Bob"],
                "description": "Sharper textures for Harry and the town.", "gameplay": true})");
    s.write("mods/alpha/convert/textures/CHARA/HERO.png", "a");
    s.write("mods/alpha/convert/textures/NEW.png", "a");
    s.write("mods/beta/mod.json", R"({"id": "beta", "version": "0.1.0", "dependencies": {"gamma": "^1"}})");
    s.write("gamedata/convert/textures/CHARA/HERO.png", "base");
}

}  // namespace

SH_TEST(script_mods_api_lists_and_edits) {
    Scratch s("api");
    two_mods(s);
    s.configure();
    s.write("gamedata/menus/t.lua", R"(
local out = {}
local function put(x) out[#out + 1] = { kind = "info", label = tostring(x) } end
local l = sh.mods.list()
put(#l)
for _, m in ipairs(l) do
  put(m.id .. "|" .. m.name .. "|" .. m.version .. "|" .. m.state .. "|" .. tostring(m.enabled) .. "|" ..
      m.load_index .. "|" .. tostring(m.gameplay) .. "|" .. m.files_replaced .. "|" .. m.files_added .. "|" ..
      #m.authors .. "|" .. #m.problems)
end
put(tostring(sh.mods.restart_pending()))
put(tostring(sh.mods.set_enabled("alpha", false)))
put(tostring(sh.mods.set_enabled("nope", false)))
put(sh.mods.list()[1].state .. "|" .. tostring(sh.mods.list()[1].enabled) .. "|" .. sh.mods.list()[1].load_index)
put(tostring(sh.mods.restart_pending()))
put(tostring(sh.mods.save()))
put(tostring(sh.mods.move("beta", -1)))
put("nope-moved:" .. tostring(sh.mods.move("nope", 1)))
put(type(sh.mods.reload()) .. "|" .. #sh.mods.reload())
put(sh.mods.dir())
sh.menu.define{ id = "r", rows = out }
)");
    sh::menu::Registry r;
    s.load(r);
    SH_CHECK(s.lines.empty());
    const auto* def = r.find("r");
    SH_CHECK(def != nullptr);
    if (def == nullptr) return;
    const std::vector<std::string> got = labels(*def);
    SH_CHECK(got.size() == 13);
    if (got.size() != 13) return;
    SH_CHECK(got[0] == "2");
    SH_CHECK(got[1] == "alpha|Alpha Pack|1.2.0|active|true|1|true|1|1|2|0");
    SH_CHECK(got[2] == "beta|beta|0.1.0|error|true|0|false|0|0|0|1");
    SH_CHECK(got[3] == "false");
    SH_CHECK(got[4] == "true" && got[5] == "false");
    SH_CHECK(got[6] == "disabled|false|1");  // switched off for the next run, still loaded in this one
    SH_CHECK(got[7] == "true");
    SH_CHECK(got[8] == "true");
    SH_CHECK(fs::exists(s.root / "mods" / "load_order.json"));
    SH_CHECK(got[9] == "true" && got[10] == "nope-moved:false");
    SH_CHECK(got[11].rfind("table|", 0) == 0 && got[11] != "table|0");
    SH_CHECK(got[12] == (s.root / "mods").generic_string());
}

SH_TEST(script_mods_screen_without_mods) {
    Scratch s("screen_empty");
    s.configure();
    s.copy_mods_screen(false);
    sh::menu::Registry r;
    s.load(r);
    SH_CHECK(s.lines.empty());
    const auto* list = r.find("options.mods");
    SH_CHECK(list != nullptr);
    if (list == nullptr) return;
    SH_CHECK(list->title == "ui.mods.title");
    // The read-only note (shown only from the pause menu), then "none" and the folder in wrapped lines.
    SH_CHECK(list->rows.size() > 1 && list->rows[0].label == "ui.mods.locked.row" &&
             list->rows[1].kind == RowKind::Info &&
             list->rows[1].label == "ui.mods.none");
    std::string path;
    for (std::size_t i = 2; i < list->rows.size() && list->rows[i].label.rfind("ui.mods.", 0) != 0; ++i) {
        path += list->rows[i].label;
    }
    SH_CHECK(path == (s.root / "mods").generic_string());
    SH_CHECK(has(labels(*list), "ui.mods.restart"));
    // Attached to Options although options.lua runs after mods.lua.
    const auto* options = r.find("options");
    SH_CHECK(options != nullptr && row(*options, "mods") != nullptr && row(*options, "mods")->submenu == "options.mods");
}

SH_TEST(script_mods_screen_with_two_mods) {
    Scratch s("screen_two");
    two_mods(s);
    s.configure();
    s.copy_mods_screen(true);
    sh::menu::Registry r;
    s.load(r);
    SH_CHECK(s.lines.empty());
    const auto* list = r.find("options.mods");
    SH_CHECK(list != nullptr);
    if (list == nullptr) return;
    const auto* a = row(*list, "alpha");
    const auto* b = row(*list, "beta");
    SH_CHECK(a != nullptr && a->kind == RowKind::Submenu && a->submenu == "options.mods.alpha" &&
             a->label == "Alpha Pack" && a->help == "ui.mods.row.active");
    SH_CHECK(b != nullptr && b->label == "- beta" && b->help == "ui.mods.row.error");

    const auto* alpha = r.find("options.mods.alpha");
    SH_CHECK(alpha != nullptr);
    if (alpha == nullptr) return;
    SH_CHECK(alpha->title == "Alpha Pack");
    const std::vector<std::string> al = labels(*alpha);
    for (const char* key : {"ui.mods.enabled", "ui.mods.move_up", "ui.mods.move_down", "ui.mods.restart",
                            "ui.mods.restart_pending", "ui.mods.state.active", "ui.mods.version", "ui.mods.loaded",
                            "ui.mods.gameplay", "ui.mods.files_replaced", "ui.mods.files_added", "ui.mods.authors",
                            "Ann, Bob", "ui.mods.description"}) {
        SH_CHECK(has(al, key));
    }
    SH_CHECK(!has(al, "ui.mods.scripts"));
    SH_CHECK(row(*alpha, "enabled")->kind == RowKind::Toggle);

    const auto* beta = r.find("options.mods.beta");
    SH_CHECK(beta != nullptr && has(labels(*beta), "ui.mods.problems") && has(labels(*beta), "ui.mods.state.error") &&
             has(labels(*beta), "ui.mods.not_loaded"));

    // The real options.lua keeps its rows; Mods comes right before its "tail." rows (add_page).
    const auto* options = r.find("options");
    SH_CHECK(options != nullptr && options->rows.size() > 1);
    if (options != nullptr) {
        std::size_t mods = options->rows.size();
        for (std::size_t i = 0; i < options->rows.size(); ++i) {
            if (options->rows[i].id == "mods") mods = i;
        }
        SH_CHECK(mods + 1 < options->rows.size() && options->rows[mods + 1].id.starts_with("tail."));
        SH_CHECK(mods > 0 && !options->rows[mods - 1].id.starts_with("tail."));
    }

    // Switching alpha off through the screen saves at once and the open screen shows the pending state.
    Controller c(r);
    SH_CHECK(c.open("options.mods"));
    SH_CHECK(c.update(press_confirm()) == sh::menu::Event::Entered);  // into alpha
    SH_CHECK(c.current() != nullptr && c.current()->id == "options.mods.alpha");
    auto restart_note_shown = [&]() {
        for (const auto& v : c.rows()) {
            if (v.def->label == "ui.mods.restart_pending") return true;
        }
        return false;
    };
    SH_CHECK(!restart_note_shown());
    SH_CHECK(c.rows()[0].value == "ui.menu.on");
    SH_CHECK(c.update(press_confirm()) == sh::menu::Event::Changed);
    SH_CHECK(c.rows()[0].value == "ui.menu.off");
    SH_CHECK(restart_note_shown());
    SH_CHECK(has(labels(*c.current()), "ui.mods.state.disabled"));
    SH_CHECK(read_file(s.root / "mods" / "load_order.json").find("\"enabled\": false") != std::string::npos);
    SH_CHECK(c.update(press_cancel()) == sh::menu::Event::Back);
    SH_CHECK(c.current()->id == "options.mods");
    SH_CHECK(row(*c.current(), "alpha")->help == "ui.mods.row.disabled");
}

SH_TEST(script_mods_screen_texts_are_translated) {
    const fs::path root = fs::path(SH_PROJECT_ROOT) / "gamedata";
    const std::string lua = read_file(root / "menus" / "mods.lua");
    std::set<std::string> keys;
    const std::regex key_re("\"(ui\\.mods\\.[A-Za-z0-9_.]+)\"");
    for (auto it = std::sregex_iterator(lua.begin(), lua.end(), key_re); it != std::sregex_iterator(); ++it) {
        keys.insert((*it)[1]);
    }
    for (const char* state : {"active", "disabled", "error"}) {  // built as "ui.mods.state." .. state
        keys.insert(std::string("ui.mods.state.") + state);
        keys.insert(std::string("ui.mods.row.") + state);
    }
    keys.erase("ui.mods.state.");
    keys.erase("ui.mods.row.");
    SH_CHECK(keys.size() > 20);
    keys.insert("ui.mods.restart_now");
    for (const char* lang : {"en", "ru", "pl", "pt", "de", "fr", "es", "it"}) {
        const std::string text = read_file(root / "text" / lang / "mods_menu.json");
        for (const std::string& k : keys) {
            if (text.find("\"" + k + "\":") == std::string::npos) {
                SH_CHECK(false && "mods_menu.json misses a key");
            }
        }
    }
}

// From the pause menu the page only shows the mods; from the title a change that needs a restart offers one on the
// way out of the page.
SH_TEST(script_mods_screen_locked_in_game_and_restart_offer) {
    Scratch s("screen_gate");
    two_mods(s);
    s.configure();
    s.copy_mods_screen(false);
    bool title = false;
    sh::mods::set_edit_gate([&]() { return title; });
    int restarts = 0;
    sh::mods::set_restart_handler([&]() {
        ++restarts;
        return sh::Result<void>::ok();
    });
    sh::menu::Registry r;
    s.load(r);
    SH_CHECK(s.lines.empty());

    Controller c(r);
    SH_CHECK(c.open("options.mods"));
    SH_CHECK(c.rows()[0].def->label == "ui.mods.locked.row");
    SH_CHECK(c.update(press_confirm()) == sh::menu::Event::Entered);  // the details stay readable
    SH_CHECK(c.current()->id == "options.mods.alpha");
    SH_CHECK(!c.rows()[0].enabled && c.help() == "ui.mods.locked");
    SH_CHECK(c.update(press_confirm()) == sh::menu::Event::Refused);
    SH_CHECK(c.rows()[0].value == "ui.menu.on");
    SH_CHECK(c.update(press_cancel()) == sh::menu::Event::Back);
    SH_CHECK(c.update(press_cancel()) == sh::menu::Event::Closed);  // nothing changed: no question
    c.close();

    // Options opened from the title: the page edits, and leaving it asks to restart.
    title = true;
    SH_CHECK(c.open("options.mods"));
    SH_CHECK(c.rows()[0].def->label != "ui.mods.locked.row");  // the note hides
    SH_CHECK(c.update(press_confirm()) == sh::menu::Event::Entered);
    SH_CHECK(c.rows()[0].enabled && c.help() == "ui.mods.enabled.help");
    SH_CHECK(c.update(press_confirm()) == sh::menu::Event::Changed);  // alpha off
    SH_CHECK(c.update(press_cancel()) == sh::menu::Event::Back);       // the mod's page: no question yet
    SH_CHECK(c.update(press_cancel()) == sh::menu::Event::Entered);    // the Mods page asks
    SH_CHECK(c.confirm_open() && c.confirm_question() == "ui.mods.restart_now" && !c.confirm_yes());
    SH_CHECK(c.update(press_cancel()) == sh::menu::Event::Back && !c.confirm_open());  // cancel stays
    SH_CHECK(c.current()->id == "options.mods");
    SH_CHECK(c.update(press_cancel()) == sh::menu::Event::Entered);
    SH_CHECK(c.update(press_confirm()) == sh::menu::Event::Closed);  // No: leave, the change waits for a restart
    SH_CHECK(restarts == 0);
    SH_CHECK(read_file(s.root / "mods" / "load_order.json").find("\"enabled\": false") != std::string::npos);

    // Asked again after another change; Yes restarts.
    SH_CHECK(c.open("options.mods"));
    SH_CHECK(c.update(press_confirm()) == sh::menu::Event::Entered);
    SH_CHECK(c.update(press_confirm()) == sh::menu::Event::Changed);  // alpha on: the running set again
    SH_CHECK(c.update(press_cancel()) == sh::menu::Event::Back);
    SH_CHECK(c.update(press_cancel()) == sh::menu::Event::Closed);  // nothing to restart for
    SH_CHECK(c.open("options.mods"));
    SH_CHECK(c.update(press_confirm()) == sh::menu::Event::Entered);
    SH_CHECK(c.update(press_confirm()) == sh::menu::Event::Changed);  // alpha off again
    SH_CHECK(c.update(press_cancel()) == sh::menu::Event::Back);
    SH_CHECK(c.update(press_cancel()) == sh::menu::Event::Entered);
    MenuInput left;
    left.left = true;
    SH_CHECK(c.update(left) == sh::menu::Event::Moved && c.confirm_yes());
    c.update(press_confirm());
    SH_CHECK(restarts == 1);
}
