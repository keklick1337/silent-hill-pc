// SPDX-License-Identifier: GPL-3.0-or-later
// sh::menu - the engine's generic menu model. Renderer-free and game-agnostic: a menu is data
// (MenuDef: a title and rows) plus callbacks that read and write whatever the row is bound to
// (a config setting, a native engine value, a Lua function). The Controller walks the
// defs with abstract input (MenuInput) and exposes what a view needs to draw (RowView). The
// views (src/ui/menu) draw it with the game's menu look; the defs come from gamedata/menus/*.lua
// through the sh.menu.* script API or from C++ natives.
//
// Text is never stored here: titles, labels, help and choice labels are keys of
// gamedata/text/<lang>/*.json (a string without a key is shown as it is), resolved by the view.
#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace sh::menu {

// A bound value. Toggles use bool, sliders double, numbers int64, choices and dropdowns the
// Choice::value string of the picked entry.
using Value = std::variant<std::monostate, bool, std::int64_t, double, std::string>;

bool as_bool(const Value& v);
std::int64_t as_int(const Value& v);
double as_double(const Value& v);
std::string as_string(const Value& v);

enum class RowKind {
    Action,    // confirm runs `activate`
    Toggle,    // left/right/confirm flip a bool
    Slider,    // left/right step a number between min and max (a float when `integer` is false)
    Number,    // an integer value with big steps on page up / page down (quantities, ids)
    Choice,    // left/right cycle through `choices` (static or from `list`)
    Dropdown,  // confirm opens a scrolling list of `list` / `choices`; confirm on an entry picks it
    Submenu,   // confirm opens the menu named `submenu`
    Setting,   // game-owned stepping: `step(dir)` changes it, `text` shows it (PC options rows)
    Info,      // read-only line; `text` or `get` is its value
    Header,    // a group caption; never selected, hidden while no row of its group is shown
};

struct Choice {
    std::string value;  // what `get` / `set` exchange
    std::string label;  // text key or literal
};

// A row shown only while a setting has a value (`visible_if` in Lua): the value of `key` (an sh.config
// key or a config.cfg key, through the registry's config readers) must be one of `any` (when given)
// and none of `none`; with neither list it must be truthy. A key no reader knows leaves the row shown.
struct VisibleIf {
    std::string key;
    std::vector<Value> any;
    std::vector<Value> none;
};

// Whether `actual` (nullopt: the key is unknown) satisfies `cond`. Numbers compare as numbers (a
// numeric string counts), booleans by truth, strings exactly.
bool visible_if_holds(const VisibleIf& cond, const std::optional<Value>& actual);

// The `visible_if` key that is 1 on a phone or tablet (a config reader answers it); `mobile = true / false` on a row.
inline constexpr std::string_view kMobileKey = "mobile";
// A page's rows marked `advanced` show on a phone or tablet on the page "<page id>.advanced" instead, opened by a row
// the registry adds to the page (text keys kAdvancedLabel and kAdvancedLabel + ".help"); elsewhere they stay in place.
inline constexpr std::string_view kAdvancedSuffix = ".advanced";
inline constexpr std::string_view kAdvancedLabel = "ui.menu.advanced";

// How a row's text that is too long for its room is shown.
enum class Cut {
    Pan,     // scrolls to its end and back (the default of a full row)
    End,     // ends in "..." (the default of a `small` row)
    Middle,  // keeps its start and its end with "..." between (a path)
    Path,    // only the last component of a path, then as End
    Wrap,    // a `small` row: breaks into as many lines as it needs (other rows: as Pan)
};

struct RowDef {
    RowKind kind = RowKind::Action;
    std::string id;       // unique in its menu; optional
    std::string label;    // text key or literal
    std::string help;     // text key or literal, optional (one short line)
    // The help line when it depends on the state (a row that is off or locked says why); empty: `help`.
    std::function<std::string()> help_fn;
    std::string submenu;  // Submenu: id of the target menu
    std::string native;   // resolved by Registry::resolve: name of a native value / action / list / setting
    double min = 0.0;     // Slider / Number
    double max = 1.0;
    double step = 0.1;
    bool integer = false;  // Slider: whole numbers only
    bool percent = false;  // Slider: shown as a bar and 0..100 (the share of min..max) instead of the number
    std::vector<Choice> choices;

    std::function<Value()> get;
    std::function<void(const Value&)> set;
    std::function<void()> activate;
    // Dropdown: called with the Choice::value under the cursor when the list opens and each time the
    // cursor moves, and with "" when the list closes (picked or cancelled): a live preview.
    std::function<void(const std::string&)> on_highlight;
    std::function<std::vector<Choice>()> list;  // Choice / Dropdown entries computed when needed
    std::function<void(int dir)> step_fn;       // Setting
    std::function<std::string()> text;          // Setting / Info value text (a literal, or a ui.* text key)
    std::function<double()> fraction;           // Setting: 0..1 drawn as a bar next to the text; < 0 or none = no bar
    std::function<std::vector<std::string>()> texts;  // Setting: every text it can show (sizes the value column)
    std::function<bool()> visible;              // hidden while it returns false
    std::vector<VisibleIf> visible_if;          // hidden unless every condition holds (and `visible`)
    bool advanced = false;                      // on a phone or tablet: on the page's Advanced page (kAdvancedSuffix)
    std::function<bool()> enabled;              // drawn dimmed and refused while it returns false
    bool close_on_activate = false;             // an Action that also closes the whole menu stack
    std::string confirm;                        // Action: text key / literal of a Yes/No question asked first (default No)
    // A mod setting key (sh.config, `mymod.speed`): Registry::resolve binds get / set and the range
    // or choices the row leaves out through the registry's config binder.
    std::string config;
    // Info: a note under the row before it, in smaller dimmer text with a tighter pitch, left-aligned with the labels;
    // its label and value read as one line. Other kinds ignore it.
    bool small = false;
    std::optional<Cut> cut;  // unset: Pan, or End on a `small` row
    // Data text put for "{name}" into the label's text (after it is translated), so a translation orders its parts.
    std::vector<std::pair<std::string, std::string>> args;
};

// The row's Cut, its default filled in.
Cut cut_of(const RowDef& r);

struct MenuDef {
    std::string id;     // "debug", "options.graphics", "mymod:cheats"
    std::string title;  // text key or literal
    std::vector<RowDef> rows;
    std::function<void()> on_open;  // called each time the menu becomes the current one
    // Called every frame while the menu is the current one, before input: a page waiting for something the engine
    // delivers later (a file the system's dialog picked) looks here and may define itself again.
    std::function<void()> on_update;
    // Asked when the player backs out of this menu: a non-empty text key / literal is a Yes/No question put first
    // (default No). Yes or No calls `on_leave(yes)`, then the menu is left; cancel stays in the menu.
    std::function<std::string()> leave_confirm;
    std::function<void(bool yes)> on_leave;
};

// Natives are what C++ (and mods through Lua) register by name and rows reference with
// `native = "name"`: they fill the callbacks of a row that does not carry its own.
struct NativeValue {
    std::function<Value()> get;
    std::function<void(const Value&)> set;
};
struct NativeSetting {
    std::function<void(int dir)> step;
    std::function<std::string()> text;
    std::function<double()> fraction;  // optional: the setting is a slider, drawn as a bar
    // Optional: every text the setting can show, in the form of text(); the Options look keeps one value
    // column wide enough for the widest, so the column neither jumps nor overflows as the value changes.
    std::function<std::vector<std::string>()> texts;
    std::function<std::string()> help;  // optional: RowDef::help_fn
};

class Registry {
public:
    // A later define() of the same id replaces the earlier one (a mod overrides the base menu). Rows marked
    // `advanced` also go to the page's Advanced page (kAdvancedSuffix), shown there on a phone or tablet only.
    void define(MenuDef def);
    // Appends rows to an existing menu (a mod extending the base menu), `advanced` ones as define() places them;
    // false when the menu does not exist.
    bool append_rows(std::string_view menu_id, std::vector<RowDef> rows);
    // Defines `page` and links it from `parent` with a submenu row (label = the page's title unless
    // `link` has its own). The row goes before the parent's rows with id "tail" / "tail.*" (Reset
    // stays last), else at the end. False when the parent does not exist yet: the page is defined
    // and the link is made when the parent is (it also survives a later define of the parent).
    bool add_page(std::string_view parent, MenuDef page, RowDef link = {});
    // How many pages add_page linked into `parent`.
    std::size_t page_count(std::string_view parent) const;
    // When no file defined the menu `from`, the pages added to it are linked into `to` instead.
    void adopt_orphan_pages(std::string_view from, std::string_view to);
    // Parents named by add_page that no file defined (their pages have no link).
    std::vector<std::string> unlinked_parents() const;
    const MenuDef* find(std::string_view id) const;
    std::vector<std::string> ids() const;
    void clear();
    // Bumped by every define / append_rows / clear: an open Controller copies its rows again when it changes,
    // so a menu that defines itself again from its own callbacks (the Mods screen) shows the new rows.
    std::uint64_t generation() const { return generation_; }
    // Asks the open controller to bring the row `row_id` of `menu_id` to the window's top (the selection goes to the
    // first row at or after it that can be selected), as soon as that menu is the current one. The latest ask wins.
    void focus(std::string menu_id, std::string row_id);
    // The ask of focus() for `menu_id`, taken (nullopt when there is none).
    std::optional<std::string> take_focus(std::string_view menu_id);

    void native_value(std::string name, NativeValue v);
    void native_action(std::string name, std::function<void()> fn);
    void native_list(std::string name, std::function<std::vector<Choice>()> fn);
    void native_setting(std::string name, NativeSetting s);
    void native_highlight(std::string name, std::function<void(const std::string&)> fn);
    void native_visible(std::string name, std::function<bool()> fn);
    void native_enabled(std::string name, std::function<bool()> fn);
    bool has_native(std::string_view name) const;
    // The answer of the native_visible predicate `name`, or nullopt when there is none.
    std::optional<bool> check_visible(std::string_view name) const;
    // Runs the native action `name`; false when there is none.
    bool run_action(std::string_view name) const;
    // Steps the setting native `name` as Left (-1) / Right (1) on its row do; its text afterwards, or nullopt
    // when there is no such setting.
    std::optional<std::string> step_setting(std::string_view name, int dir) const;
    // Binds rows that carry `config` (set by the script layer, which owns the settings store);
    // it returns false for a key it cannot bind. Without a binder such rows stay inert.
    void config_binder(std::function<bool(RowDef&)> fn);
    // Readers of setting values by key for `visible_if`, asked in the order first added; the first
    // that knows the key answers. A reader added again under the same name replaces the old one.
    void config_reader(std::string name, std::function<std::optional<Value>(std::string_view key)> fn);
    std::optional<Value> config_value(std::string_view key) const;

    // Fills the callbacks of `row` from its `config` key and its `native` name where the row has
    // none of its own. Returns false (and leaves the row inert) when either is unknown.
    bool resolve(RowDef& row) const;

private:
    // Copies `rows` marked advanced (and every header, which hides itself while its group is empty there) onto the
    // Advanced page of `page`, hides the originals on a phone or tablet and links the page from `page` there.
    void place_advanced(MenuDef& page, const std::vector<RowDef>& rows);

    std::map<std::string, MenuDef, std::less<>> menus_;
    std::list<std::map<std::string, MenuDef, std::less<>>> retired_;  // the menus of earlier clear()s
    std::uint64_t generation_ = 0;
    std::optional<std::pair<std::string, std::string>> focus_;  // menu id, row id
    std::map<std::string, std::vector<RowDef>, std::less<>> page_links_;  // add_page links by parent id
    std::map<std::string, NativeValue, std::less<>> values_;
    std::map<std::string, std::function<void()>, std::less<>> actions_;
    std::map<std::string, std::function<std::vector<Choice>()>, std::less<>> lists_;
    std::map<std::string, std::function<void(const std::string&)>, std::less<>> highlights_;
    std::map<std::string, NativeSetting, std::less<>> settings_;
    std::map<std::string, std::function<bool()>, std::less<>> visible_;
    std::map<std::string, std::function<bool()>, std::less<>> enabled_;
    std::function<bool(RowDef&)> config_binder_;
    std::vector<std::pair<std::string, std::function<std::optional<Value>(std::string_view)>>> config_readers_;
};

// One frame of abstract input; the host fills it from the engine's action layer
// (keyboard, mouse, gamepad) with repeat already applied to up/down/left/right.
struct MenuInput {
    bool up = false, down = false, left = false, right = false;
    // left/right are in the space of a mirrored layout (a right-to-left language): a bar still moves the way the
    // key points on screen, a list of values keeps its order with Right as next.
    bool mirrored = false;
    bool confirm = false, cancel = false;
    bool page_up = false, page_down = false;
    int wheel = 0;          // notches, + = up
    int hover_row = -1;     // visible-row index under the pointer, -1 none
    bool pointer_moved = false;
    bool click = false;       // left click (activates the hovered row)
    bool right_click = false;  // = cancel
    int hover_answer = -1;     // while a confirm question is up: 0 = Yes, 1 = No under the pointer, -1 none
    // The pointer is a finger (a touch screen): a tap on a row of values (a toggle's On / Off too) opens the list
    // of them to pick from, a tap beside an open list closes it, a tap on a bar's row beside the bar only selects.
    bool touch = false;
    // The pointer presses the selected row's bar at this share of its length (0..1); < 0: not on a bar.
    double bar_at = -1.0;
};

enum class Event {
    None,
    Moved,      // selection moved
    Changed,    // a value was stepped / toggled / picked
    Activated,  // an action ran
    Entered,    // a submenu was opened (or a dropdown)
    Back,       // went up one level
    Closed,     // the last menu was left (or an action closed the stack)
    Refused,    // the row is disabled
};

struct RowView {
    const RowDef* def = nullptr;
    std::string value;          // text to draw in the value column, empty for none
    bool value_is_key = false;  // the value is a text key to localize (On/Off, choice labels)
    bool enabled = true;
    bool selected = false;
    bool has_arrows = false;    // left/right change the value
    double fraction = -1.0;     // 0..1: draw the value as a bar (sliders, volume settings); < 0 = no bar
    std::vector<std::string> texts;  // every value the row can show (as `value`, keys or literals); may be empty
};

struct DropdownView {
    std::vector<Choice> items;
    int selected = 0;
    int top = 0;
    std::string label;  // the row's label (what the list picks)
    int current = -1;   // the entry the row had when the list opened, -1 none
};

class Controller {
public:
    explicit Controller(Registry& registry) : registry_(&registry) {}

    // Opens `id` as the root (clears the stack). False when there is no such menu.
    bool open(std::string_view id);
    // Opens `id` over the current menu, as its submenu row would (Back returns). False when nothing
    // is open or there is no such menu.
    bool enter(std::string_view id);
    void close();
    bool is_open() const { return !stack_.empty(); }
    int depth() const { return static_cast<int>(stack_.size()); }

    Event update(const MenuInput& in);

    // How many rows the view can show at once; the controller keeps the selection inside the window.
    void set_visible_rows(int n);
    // Rows of a height of their own: a full row is `pitch` tall and the window set_visible_rows(n) * pitch; a `small`
    // row is as tall as `small_height` says (in the same units). Without it every row is one row of the window.
    void set_row_heights(int pitch, std::function<int(const RowView&)> small_height);
    const MenuDef* current() const;
    const std::string& title() const;
    std::vector<RowView> rows() const;  // the visible rows (hidden ones are skipped)
    int selection() const;              // index into rows()
    int scroll_top() const;             // the first row shown (of the open list while dropdown_open())
    // A touch screen's list follows the finger, not the cursor: the view sets the first row shown, and the
    // selection may leave the window until a key moves it (the page, or the open list of values).
    void set_scroll_top(int top);
    std::string help() const;           // help key of the selected row
    bool dropdown_open() const { return dropdown_.has_value(); }
    DropdownView dropdown() const;      // only while dropdown_open()
    // A row with `confirm` asks its question before it runs: left/up pick Yes, right/down No, confirm answers,
    // cancel is No. Entering the question is Event::Entered, answering No is Event::Back, Yes runs the action.
    // A menu's leave_confirm question works the same, except that cancel stays in the menu.
    bool confirm_open() const { return confirm_.has_value(); }
    const std::string& confirm_question() const;
    bool confirm_yes() const { return confirm_ && confirm_->yes; }
    // Id of the last row that ran (for tests and the host's sounds).
    const std::string& last_row_id() const { return last_row_id_; }

private:
    struct Frame {
        const MenuDef* def = nullptr;
        std::vector<RowDef> rows;  // resolved copy: natives bound, callbacks live
        std::uint64_t generation = 0;  // Registry::generation() when `rows` was copied
        std::string id;                // to find the def again (a clear() frees the old one)
        int sel = 0;               // index into the visible rows
        int top = 0;
        bool free_scroll = false;  // a finger scrolled the page: the top no longer follows the selection
    };
    struct Dropdown {
        std::vector<Choice> items;
        int sel = 0;
        int top = 0;
        std::size_t row = 0;  // index into Frame::rows
        int current = -1;     // the row's value when the list opened
        // How a pick reaches the row: its `set` with the entry's value, a toggle's bool ("0" / "1"), or a setting's
        // steps until the row shows the entry (a setting has no setter, only steps and texts).
        enum class Pick { Set, Toggle, Step };
        Pick pick = Pick::Set;
        bool free_scroll = false;  // a finger scrolled the list: the top no longer follows the cursor
    };

    struct Confirm {
        std::size_t row = 0;  // index into Frame::rows
        bool yes = false;
        bool leaving = false;  // MenuDef::leave_confirm: answering leaves the menu instead of running a row
        std::string question;  // leaving only
    };
    Event leave_frame();  // Back, or Closed when it was the last menu

    bool push(std::string_view id);
    Event update_frame(const MenuInput& in);
    void refresh();  // copies the rows of every open menu again when the registry changed
    Event run_action(RowDef& r);
    Event update_confirm(const MenuInput& in);
    std::vector<std::size_t> visible_indices(const Frame& f) const;
    bool row_enabled(const RowDef& r) const;
    std::vector<Choice> row_choices(const RowDef& r) const;
    void clamp(Frame& f) const;
    Event step_row(Frame& f, std::size_t row_index, int dir, bool big);
    Event activate_row(Frame& f, std::size_t row_index);
    Event open_picker(Frame& f, std::size_t row_index);  // the touch screen's list of a row's values
    Event set_bar(Frame& f, std::size_t row_index, double at);
    RowView view_of(const RowDef& r) const;
    // The heights of the visible rows `vis` of `f`; empty when every row is one row of the window.
    std::vector<int> heights(const Frame& f, const std::vector<std::size_t>& vis) const;
    int window_height() const { return visible_rows_ * row_pitch_; }
    void apply_focus(Frame& f);
    Event update_dropdown(const MenuInput& in);
    void highlight_dropdown();  // on_highlight with the entry under the cursor
    void end_dropdown();        // closes the list; on_highlight("")
    std::string value_text(const RowDef& r, bool& is_key) const;

    Registry* registry_;
    std::vector<Frame> stack_;
    std::optional<Dropdown> dropdown_;
    std::optional<Confirm> confirm_;
    int visible_rows_ = 8;
    int row_pitch_ = 1;
    std::function<int(const RowView&)> small_height_;
    bool touch_ = false;  // MenuInput::touch of the update running
    std::string last_row_id_;
};

// The process-wide registry: C++ natives register into it at startup, gamedata/menus/*.lua
// (sh.menu.define) fills it, the views open menus from it.
Registry& registry();

// What the view layer provides to scripts and natives: open a menu as the player-facing screen
// (the pause menu or the title hosts it), close it, ask whether one is up. Set once by the view.
struct HostHooks {
    std::function<bool(std::string_view id)> open;
    std::function<void()> close;
    std::function<bool()> is_open;
};
HostHooks& host();

// Formats a slider / number for display: whole numbers without decimals, else up to 2 decimals trimmed.
std::string format_number(double v, bool integer);

}  // namespace sh::menu
