// SPDX-License-Identifier: GPL-3.0-or-later
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
// The generic menu model: row kinds, navigation, native binding, dropdowns, submenus, visibility.

#include <string>
#include <vector>

#include "engine/menu/menu_model.h"
#include "framework/sh_test.h"

using namespace sh::menu;

namespace {

struct Fixture {
    Registry reg;
    bool god = false;
    double vol = 0.5;
    std::int64_t qty = 1;
    std::string mode = "a";
    std::string picked;
    int fired = 0;
    bool hidden = false;
    int setting = 0;

    Fixture() {
        reg.native_value("god", {[this] { return Value{god}; }, [this](const Value& v) { god = as_bool(v); }});
        reg.native_value("vol", {[this] { return Value{vol}; }, [this](const Value& v) { vol = as_double(v); }});
        reg.native_value("qty", {[this] { return Value{qty}; }, [this](const Value& v) { qty = as_int(v); }});
        reg.native_value("mode", {[this] { return Value{mode}; }, [this](const Value& v) { mode = as_string(v); }});
        reg.native_value("pick", {[this] { return Value{picked}; }, [this](const Value& v) { picked = as_string(v); }});
        reg.native_action("fire", [this] { ++fired; });
        reg.native_list("things", [] {
            std::vector<Choice> out;
            for (int i = 0; i < 20; ++i) out.push_back({"t" + std::to_string(i), "Thing " + std::to_string(i)});
            return out;
        });
        reg.native_setting("opt", {[this](int d) { setting += d; }, [this] { return "v" + std::to_string(setting); }});
        reg.native_visible("shown", [this] { return !hidden; });

        MenuDef sub;
        sub.id = "sub";
        sub.title = "ui.menu.sub";
        sub.rows.push_back(RowDef{.kind = RowKind::Action, .id = "sub_fire", .label = "Fire", .native = "fire"});

        MenuDef root;
        root.id = "root";
        root.title = "ui.menu.root";
        root.rows.push_back(RowDef{.kind = RowKind::Info, .id = "note", .label = "Note",
                                   .text = [] { return std::string("hello"); }});
        root.rows.push_back(RowDef{.kind = RowKind::Toggle, .id = "god", .label = "God", .help = "h.god", .native = "god"});
        root.rows.push_back(RowDef{.kind = RowKind::Slider, .id = "vol", .label = "Vol", .native = "vol", .min = 0, .max = 1,
                                   .step = 0.1});
        root.rows.push_back(RowDef{.kind = RowKind::Number, .id = "qty", .label = "Qty", .native = "qty", .min = 1, .max = 99,
                                   .step = 1});
        root.rows.push_back(RowDef{.kind = RowKind::Choice, .id = "mode", .label = "Mode", .native = "mode",
                                   .choices = {{"a", "ui.m.a"}, {"b", "ui.m.b"}, {"c", "ui.m.c"}}});
        root.rows.push_back(RowDef{.kind = RowKind::Dropdown, .id = "pick", .label = "Pick", .native = "things"});
        root.rows.back().get = [this] { return Value{picked}; };
        root.rows.back().set = [this](const Value& v) { picked = as_string(v); };
        root.rows.push_back(RowDef{.kind = RowKind::Setting, .id = "opt", .label = "Opt", .native = "opt"});
        root.rows.push_back(RowDef{.kind = RowKind::Action, .id = "fire", .label = "Fire", .native = "fire"});
        root.rows.push_back(RowDef{.kind = RowKind::Submenu, .id = "go", .label = "Go", .submenu = "sub"});
        root.rows.push_back(RowDef{.kind = RowKind::Action, .id = "maybe", .label = "Maybe", .native = "shown"});
        root.rows.back().visible = [this] { return !hidden; };
        reg.define(root);
        reg.define(sub);
    }
};

MenuInput press_down() { MenuInput i; i.down = true; return i; }
MenuInput press_left() { MenuInput i; i.left = true; return i; }
MenuInput press_right() { MenuInput i; i.right = true; return i; }
MenuInput press_confirm() { MenuInput i; i.confirm = true; return i; }
MenuInput press_cancel() { MenuInput i; i.cancel = true; return i; }

}  // namespace

SH_TEST(menu_opens_on_first_selectable_row) {
    Fixture f;
    Controller c(f.reg);
    SH_CHECK(!c.open("missing"));
    SH_CHECK(c.open("root"));
    SH_CHECK(c.is_open() && c.title() == "ui.menu.root");
    const auto rows = c.rows();
    SH_CHECK(rows.size() == 10);
    SH_CHECK(rows[0].def->kind == RowKind::Info && rows[0].value == "hello" && !rows[0].selected);
    SH_CHECK(rows[1].selected);  // the Info row is skipped
    SH_CHECK(c.help() == "h.god");
}

SH_TEST(menu_toggle_slider_number_choice) {
    Fixture f;
    Controller c(f.reg);
    c.open("root");
    SH_CHECK(c.update(press_confirm()) == Event::Changed && f.god);
    SH_CHECK(c.rows()[1].value == "ui.menu.on" && c.rows()[1].value_is_key);
    c.update(press_down());
    for (int i = 0; i < 3; ++i) c.update(press_right());
    SH_CHECK(f.vol > 0.79 && f.vol < 0.81);  // 0.5 + 3 * 0.1 on the step grid
    for (int i = 0; i < 20; ++i) c.update(press_right());
    SH_CHECK(f.vol == 1.0);  // clamped
    SH_CHECK(c.rows()[2].value == "1");
    c.update(press_down());
    MenuInput big;
    big.page_down = true;
    c.update(big);
    SH_CHECK(f.qty == 11);  // page steps are ten times larger
    for (int i = 0; i < 20; ++i) c.update(big);
    SH_CHECK(f.qty == 99);
    c.update(press_left());
    SH_CHECK(f.qty == 98);
    c.update(press_down());
    c.update(press_right());
    SH_CHECK(f.mode == "b" && c.rows()[4].value == "ui.m.b");
    c.update(press_right());
    c.update(press_right());
    SH_CHECK(f.mode == "a");  // wraps
    c.update(press_left());
    SH_CHECK(f.mode == "c");
}

// A right-to-left language mirrors the layout and with it the Left/Right keys (the physical Right arrives as left):
// a bar still moves the way the key points on screen, a list of values keeps its order with Right as next.
SH_TEST(menu_mirrored_layout_keeps_choice_order) {
    Fixture f;
    Controller c(f.reg);
    c.open("root");
    MenuInput physical_right;
    physical_right.left = true;
    physical_right.mirrored = true;
    c.update(press_down());
    c.update(physical_right);
    SH_CHECK(f.vol > 0.59 && f.vol < 0.61);  // a slider shown as a number: Right is larger
    c.update(press_down());
    c.update(press_down());
    c.update(physical_right);
    SH_CHECK(f.mode == "b");
    c.update(physical_right);
    SH_CHECK(f.mode == "c");
    c.update(press_down());
    c.update(press_down());
    c.update(physical_right);
    SH_CHECK(f.setting == 1);
}

SH_TEST(menu_dropdown_picks_and_cancels) {
    Fixture f;
    Controller c(f.reg);
    c.open("root");
    c.set_visible_rows(5);
    for (int i = 0; i < 4; ++i) c.update(press_down());  // god, vol, qty, mode, pick
    SH_CHECK(c.rows()[c.selection()].def->id == "pick");
    SH_CHECK(c.update(press_confirm()) == Event::Entered && c.dropdown_open());
    SH_CHECK(c.dropdown().items.size() == 20);
    for (int i = 0; i < 7; ++i) c.update(press_down());
    SH_CHECK(c.dropdown().selected == 7 && c.dropdown().top == 3);  // window follows the cursor
    SH_CHECK(c.update(press_cancel()) == Event::Back && !c.dropdown_open() && f.picked.empty());
    c.update(press_confirm());
    c.update(press_down());
    SH_CHECK(c.update(press_confirm()) == Event::Changed && f.picked == "t1" && !c.dropdown_open());
    SH_CHECK(c.rows()[c.selection()].value == "Thing 1");
}

SH_TEST(menu_action_setting_and_submenu) {
    Fixture f;
    Controller c(f.reg);
    c.open("root");
    for (int i = 0; i < 5; ++i) c.update(press_down());
    SH_CHECK(c.rows()[c.selection()].def->id == "opt");
    c.update(press_right());
    c.update(press_right());
    c.update(press_left());
    SH_CHECK(f.setting == 1 && c.rows()[c.selection()].value == "v1");
    c.update(press_down());
    SH_CHECK(c.update(press_confirm()) == Event::Activated && f.fired == 1 && c.last_row_id() == "fire");
    c.update(press_down());
    SH_CHECK(c.update(press_confirm()) == Event::Entered && c.depth() == 2 && c.title() == "ui.menu.sub");
    SH_CHECK(c.update(press_confirm()) == Event::Activated && f.fired == 2);
    SH_CHECK(c.update(press_cancel()) == Event::Back && c.depth() == 1);
    SH_CHECK(c.rows()[c.selection()].def->id == "go");  // the parent keeps its cursor
    SH_CHECK(c.update(press_cancel()) == Event::Closed && !c.is_open());
}

SH_TEST(menu_visibility_and_disabled_rows) {
    Fixture f;
    Controller c(f.reg);
    c.open("root");
    SH_CHECK(c.rows().size() == 10);
    f.hidden = true;
    SH_CHECK(c.rows().size() == 9);
    f.hidden = false;

    MenuDef d;
    d.id = "d";
    d.title = "D";
    bool on = false;
    d.rows.push_back(RowDef{.kind = RowKind::Action, .id = "x", .label = "X", .activate = [&] { ++f.fired; },
                            .enabled = [&] { return on; }});
    f.reg.define(d);
    c.open("d");
    SH_CHECK(c.update(press_confirm()) == Event::Refused && f.fired == 0);
    on = true;
    SH_CHECK(c.update(press_confirm()) == Event::Activated && f.fired == 1);
}

// A header is a caption: never selected, and gone while every row of its group is hidden.
SH_TEST(menu_header_rows) {
    Fixture f;
    MenuDef d;
    d.id = "h";
    d.title = "H";
    bool shown = true;
    d.rows.push_back(RowDef{.kind = RowKind::Header, .id = "g1", .label = "G1"});
    d.rows.push_back(RowDef{.kind = RowKind::Action, .id = "a", .label = "A", .activate = [&] { ++f.fired; }});
    d.rows.push_back(RowDef{.kind = RowKind::Header, .id = "g2", .label = "G2"});
    d.rows.push_back(RowDef{.kind = RowKind::Action, .id = "b", .label = "B", .activate = [&] { ++f.fired; },
                            .visible = [&] { return shown; }});
    f.reg.define(d);
    Controller c(f.reg);
    c.open("h");
    SH_CHECK(c.rows().size() == 4);
    SH_CHECK(c.selection() == 1);
    c.update(press_down());
    SH_CHECK(c.selection() == 3);
    shown = false;
    SH_CHECK(c.rows().size() == 2);
    SH_CHECK(c.rows().back().def->id == "a");
}

SH_TEST(menu_mouse_hover_click_and_wheel) {
    Fixture f;
    Controller c(f.reg);
    c.open("root");
    MenuInput in;
    in.hover_row = 3;
    in.pointer_moved = true;
    SH_CHECK(c.update(in) == Event::Moved && c.rows()[c.selection()].def->id == "qty");
    in = MenuInput{};
    in.hover_row = 1;
    in.click = true;
    SH_CHECK(c.update(in) == Event::Changed && f.god);
    in = MenuInput{};
    in.wheel = -2;
    c.update(in);
    SH_CHECK(c.rows()[c.selection()].def->id == "qty");  // wheel down moves two rows
    in = MenuInput{};
    in.right_click = true;
    SH_CHECK(c.update(in) == Event::Closed);
}

SH_TEST(menu_close_on_activate_and_mod_override) {
    Fixture f;
    MenuDef m;
    m.id = "warp";
    m.title = "W";
    RowDef r{.kind = RowKind::Dropdown, .id = "go", .label = "Go", .native = "things"};
    r.set = [&](const Value& v) { f.picked = as_string(v); };
    r.close_on_activate = true;
    m.rows.push_back(r);
    f.reg.define(m);
    Controller c(f.reg);
    c.open("warp");
    c.update(press_confirm());
    c.update(press_down());
    SH_CHECK(c.update(press_confirm()) == Event::Closed && !c.is_open() && f.picked == "t1");

    // A later define() of the same id replaces it; append_rows extends it.
    MenuDef over;
    over.id = "warp";
    over.title = "W2";
    f.reg.define(over);
    SH_CHECK(f.reg.find("warp")->title == "W2" && f.reg.find("warp")->rows.empty());
    SH_CHECK(f.reg.append_rows("warp", {RowDef{.kind = RowKind::Info, .id = "i"}}));
    SH_CHECK(!f.reg.append_rows("none", {}));
    SH_CHECK(f.reg.find("warp")->rows.size() == 1);
}

SH_TEST(menu_value_helpers) {
    SH_CHECK(as_bool(Value{std::int64_t{2}}) && !as_bool(Value{std::string("0")}) && !as_bool(Value{}));
    SH_CHECK(as_int(Value{3.6}) == 4 && as_double(Value{std::string("1.5")}) == 1.5);
    SH_CHECK(format_number(2.0, false) == "2" && format_number(0.25, false) == "0.25" && format_number(0.5, false) == "0.5");
    SH_CHECK(as_string(Value{std::int64_t{7}}) == "7");
}

// A dropdown row reports the entry under the cursor (a live preview) and "" when the list closes.
SH_TEST(menu_dropdown_highlight) {
    Fixture f;
    std::vector<std::string> seen;
    f.reg.native_highlight("things", [&seen](const std::string& v) { seen.push_back(v); });
    Controller c(f.reg);
    c.open("root");
    for (int i = 0; i < 4; ++i) c.update(press_down());
    c.update(press_confirm());  // opens on the first entry
    c.update(press_down());
    c.update(press_down());
    SH_CHECK((seen == std::vector<std::string>{"t0", "t1", "t2"}));
    c.update(press_cancel());  // closing reports ""
    SH_CHECK(seen.size() == 4 && seen.back().empty());
    c.update(press_confirm());
    c.update(press_confirm());  // pick: "" first, then set
    SH_CHECK(seen.size() == 6 && seen.back().empty() && f.picked == "t0");
    c.update(press_confirm());
    c.close();  // closing the whole menu with the list open
    SH_CHECK(seen.size() == 8 && seen.back().empty() && seen[6] == "t0");
}

// A page added before its parent menu exists (mods.lua sorts before options.lua) is linked when the
// parent is defined, and stays linked when the parent is defined again; Reset stays last.
SH_TEST(menu_add_page_survives_parent_order) {
    Registry reg;
    MenuDef page;
    page.id = "mymod.page";
    page.title = "My Page";
    SH_CHECK(!reg.add_page("options", page));
    SH_CHECK(reg.find("mymod.page") != nullptr);

    MenuDef options;
    options.id = "options";
    RowDef display;
    display.kind = RowKind::Submenu;
    display.id = "display";
    display.submenu = "options.display";
    RowDef reset;
    reset.id = "tail.reset_defaults";
    options.rows = {display, reset};
    reg.define(options);
    const MenuDef* o = reg.find("options");
    SH_CHECK(o != nullptr && o->rows.size() == 3);
    SH_CHECK(o->rows[1].submenu == "mymod.page" && o->rows[1].label == "My Page");
    SH_CHECK(o->rows[2].id == "tail.reset_defaults");

    reg.define(options);  // a later file redefines the menu
    o = reg.find("options");
    SH_CHECK(o->rows.size() == 3 && o->rows[1].submenu == "mymod.page");
    SH_CHECK(reg.add_page("options", page));  // again: still one link
    SH_CHECK(reg.find("options")->rows.size() == 3);
}

// visible_if: the value of a key (read through the registry's readers) picks which rows show, live.
SH_TEST(menu_visible_if_conditions) {
    VisibleIf cam{.key = "control_style", .any = {Value{std::string("tps")}, Value{std::string("ots")}}};
    SH_CHECK(visible_if_holds(cam, Value{std::string("tps")}));
    SH_CHECK(!visible_if_holds(cam, Value{std::string("classic")}));
    SH_CHECK(visible_if_holds(cam, std::nullopt));  // an unknown key leaves the row shown

    VisibleIf on{.key = "crosshair"};  // no list: the value must be truthy
    SH_CHECK(visible_if_holds(on, Value{1.0}) && !visible_if_holds(on, Value{0.0}));
    SH_CHECK(visible_if_holds(on, Value{true}) && !visible_if_holds(on, Value{std::string("0")}));

    VisibleIf not_fps{.key = "control_style", .none = {Value{std::string("fps")}}};
    SH_CHECK(visible_if_holds(not_fps, Value{std::string("ots")}) && !visible_if_holds(not_fps, Value{std::string("fps")}));

    // Numbers match across their spellings: config.cfg text, Lua integers and floats.
    VisibleIf two{.key = "n", .any = {Value{std::int64_t{2}}}};
    SH_CHECK(visible_if_holds(two, Value{2.0}) && visible_if_holds(two, Value{std::string("2")}));
    SH_CHECK(!visible_if_holds(two, Value{std::string("two")}));
    VisibleIf yes{.key = "b", .any = {Value{true}}};
    SH_CHECK(visible_if_holds(yes, Value{std::int64_t{1}}) && !visible_if_holds(yes, Value{0.0}));
}

SH_TEST(menu_visible_if_rows_follow_the_value) {
    Registry reg;
    std::string style = "classic";
    double crosshair = 0.0;
    bool native_shows = true;
    reg.config_reader("first", [&](std::string_view key) -> std::optional<Value> {
        if (key == "control_style") return Value{style};
        return std::nullopt;
    });
    reg.config_reader("second", [&](std::string_view key) -> std::optional<Value> {
        if (key == "control_style") return Value{std::string("never asked")};
        if (key == "crosshair") return Value{crosshair};
        return std::nullopt;
    });
    reg.native_action("act", [] {});
    reg.native_visible("act", [&] { return native_shows; });

    MenuDef cam;
    cam.id = "cam";
    cam.rows.push_back(RowDef{.kind = RowKind::Action, .id = "style", .native = "act"});
    cam.rows.back().visible = [] { return true; };  // its own visible wins over the native's
    RowDef tps{.kind = RowKind::Action, .id = "tps_fov", .native = "act"};
    tps.visible_if = {VisibleIf{.key = "control_style", .any = {Value{std::string("tps")}}}};
    cam.rows.push_back(tps);
    RowDef size{.kind = RowKind::Action, .id = "size", .native = "act"};
    size.visible_if = {VisibleIf{.key = "control_style", .none = {Value{std::string("classic")}}},
                       VisibleIf{.key = "crosshair"}};
    cam.rows.push_back(size);
    RowDef other{.kind = RowKind::Action, .id = "other", .native = "act"};
    other.visible_if = {VisibleIf{.key = "nobody.knows", .any = {Value{std::string("x")}}}};
    cam.rows.push_back(other);
    reg.define(cam);

    Controller c(reg);
    SH_CHECK(c.open("cam"));
    const auto ids = [&] {
        std::string s;
        for (const RowView& r : c.rows()) s += r.def->id + " ";
        return s;
    };
    SH_CHECK(ids() == "style other ");
    style = "tps";
    SH_CHECK(ids() == "style tps_fov other ");  // the rows change as the value does, no reopen
    crosshair = 1.0;
    SH_CHECK(ids() == "style tps_fov size other ");
    style = "fps";
    SH_CHECK(ids() == "style size other ");
    native_shows = false;  // the native's own visibility still applies to rows with conditions
    SH_CHECK(ids() == "style ");
    // A reader added again under its name replaces the old one: now the second one answers.
    reg.config_reader("first", [](std::string_view) -> std::optional<Value> { return std::nullopt; });
    native_shows = true;
    SH_CHECK(ids() == "style size other ");
}

SH_TEST(menu_enter_opens_a_page_over_the_current_menu) {
    Fixture f;
    Controller c(f.reg);
    SH_CHECK(!c.enter("sub"));  // nothing open
    SH_CHECK(c.open("root"));
    SH_CHECK(!c.enter("missing") && c.depth() == 1);
    SH_CHECK(c.enter("sub") && c.depth() == 2 && c.current()->id == "sub");
    SH_CHECK(c.update(press_cancel()) == Event::Back && c.current()->id == "root");
}

SH_TEST(menu_registry_checks_visible_and_runs_actions_by_name) {
    Registry reg;
    bool shown = false;
    int fired = 0;
    reg.native_visible("pending", [&] { return shown; });
    reg.native_action("restart", [&] { ++fired; });
    SH_CHECK(reg.check_visible("pending") == std::optional<bool>(false));
    shown = true;
    SH_CHECK(reg.check_visible("pending") == std::optional<bool>(true));
    SH_CHECK(!reg.check_visible("missing").has_value());
    SH_CHECK(reg.run_action("restart"));
    SH_CHECK(fired == 1);
    SH_CHECK(!reg.run_action("missing"));
}
