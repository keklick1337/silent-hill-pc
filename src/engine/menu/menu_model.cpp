// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/menu/menu_model.h"

#include "engine/hooks/hook_bus.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sh::menu {

bool as_bool(const Value& v) {
    if (const auto* b = std::get_if<bool>(&v)) return *b;
    if (const auto* i = std::get_if<std::int64_t>(&v)) return *i != 0;
    if (const auto* d = std::get_if<double>(&v)) return *d != 0.0;
    if (const auto* s = std::get_if<std::string>(&v)) return !s->empty() && *s != "0" && *s != "false";
    return false;
}

std::int64_t as_int(const Value& v) {
    if (const auto* b = std::get_if<bool>(&v)) return *b ? 1 : 0;
    if (const auto* i = std::get_if<std::int64_t>(&v)) return *i;
    if (const auto* d = std::get_if<double>(&v)) return static_cast<std::int64_t>(std::llround(*d));
    if (const auto* s = std::get_if<std::string>(&v)) return std::strtoll(s->c_str(), nullptr, 10);
    return 0;
}

double as_double(const Value& v) {
    if (const auto* d = std::get_if<double>(&v)) return *d;
    if (const auto* s = std::get_if<std::string>(&v)) return std::strtod(s->c_str(), nullptr);
    return static_cast<double>(as_int(v));
}

std::string as_string(const Value& v) {
    if (const auto* s = std::get_if<std::string>(&v)) return *s;
    if (const auto* b = std::get_if<bool>(&v)) return *b ? "1" : "0";
    if (const auto* i = std::get_if<std::int64_t>(&v)) return std::to_string(*i);
    if (const auto* d = std::get_if<double>(&v)) return format_number(*d, false);
    return {};
}

std::string format_number(double v, bool integer) {
    char buf[48];
    if (integer || v == std::floor(v)) {
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(std::llround(v)));
        return buf;
    }
    std::snprintf(buf, sizeof buf, "%.2f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

Cut cut_of(const RowDef& r) {
    if (r.cut) return *r.cut;
    return r.small ? Cut::End : Cut::Pan;
}

// ---- Registry ----------------------------------------------------------------------------

namespace {

// Rows a menu keeps at its end (Reset, Exit): their ids are "tail" or start with "tail.".
bool is_tail(const RowDef& r) { return r.id == "tail" || r.id.starts_with("tail."); }

void insert_before_tail(std::vector<RowDef>& dst, std::vector<RowDef> rows) {
    auto at = std::find_if(dst.begin(), dst.end(), is_tail);
    dst.insert(at, std::make_move_iterator(rows.begin()), std::make_move_iterator(rows.end()));
}

// One link per page: a second add of the same page (a mod reloaded) replaces the first.
void place_link(std::vector<RowDef>& rows, const RowDef& link) {
    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [&](const RowDef& r) { return r.kind == RowKind::Submenu && r.submenu == link.submenu; }),
               rows.end());
    std::vector<RowDef> one{link};
    insert_before_tail(rows, std::move(one));
}

VisibleIf on_mobile(bool mobile) {
    VisibleIf c;
    c.key = std::string(kMobileKey);
    (mobile ? c.any : c.none).push_back(Value{1.0});
    return c;
}

}  // namespace

void Registry::place_advanced(MenuDef& page, const std::vector<RowDef>& rows) {
    if (std::none_of(rows.begin(), rows.end(), [](const RowDef& r) { return r.advanced; })) return;
    const std::string adv_id = page.id + std::string(kAdvancedSuffix);
    MenuDef& adv = menus_[adv_id];
    if (adv.id.empty()) {
        adv.id = adv_id;
        adv.title = std::string(kAdvancedLabel);
    }
    for (const RowDef& r : rows) {
        if (!r.advanced && r.kind != RowKind::Header) continue;
        RowDef copy = r;
        copy.advanced = false;
        adv.rows.push_back(std::move(copy));
    }
    for (RowDef& r : page.rows)
        if (r.advanced) {
            r.visible_if.push_back(on_mobile(false));
            r.advanced = false;
        }
    // A caption without text before the row closes the group above it, so its header does not stay for this row.
    RowDef gap;
    gap.kind = RowKind::Header;
    gap.id = adv_id + ".group";
    gap.visible_if.push_back(on_mobile(true));
    RowDef link;
    link.kind = RowKind::Submenu;
    link.id = adv_id;
    link.label = std::string(kAdvancedLabel);
    link.help = std::string(kAdvancedLabel) + ".help";
    link.submenu = adv_id;
    link.visible_if.push_back(on_mobile(true));
    std::erase_if(page.rows, [&](const RowDef& r) { return r.id == gap.id || r.id == link.id; });
    insert_before_tail(page.rows, {std::move(gap), std::move(link)});
}

void Registry::define(MenuDef def) {
    const std::string id = def.id;
    // Emptied, not erased: an open menu may still point at it.
    if (const auto adv = menus_.find(id + std::string(kAdvancedSuffix)); adv != menus_.end()) adv->second.rows.clear();
    MenuDef& stored = menus_[id] = std::move(def);
    place_advanced(stored, stored.rows);
    // Pages added to this menu stay linked whatever order the files define them in (mods.lua runs
    // before options.lua) and when a later file redefines the menu.
    if (const auto links = page_links_.find(id); links != page_links_.end()) {
        for (const RowDef& link : links->second) place_link(stored.rows, link);
    }
    ++generation_;
}

bool Registry::append_rows(std::string_view menu_id, std::vector<RowDef> rows) {
    const auto it = menus_.find(menu_id);
    if (it == menus_.end()) return false;
    std::vector<RowDef> added = rows;
    insert_before_tail(it->second.rows, std::move(rows));
    place_advanced(it->second, added);
    ++generation_;
    return true;
}

bool Registry::add_page(std::string_view parent, MenuDef page, RowDef link) {
    link.kind = RowKind::Submenu;
    link.submenu = page.id;
    if (link.label.empty()) link.label = page.title.empty() ? page.id : page.title;
    if (link.id.empty()) link.id = page.id;
    define(std::move(page));
    auto& links = page_links_[std::string(parent)];
    std::erase_if(links, [&](const RowDef& r) { return r.submenu == link.submenu; });
    links.push_back(link);
    const auto it = menus_.find(parent);
    if (it == menus_.end()) return false;  // linked when the parent is defined
    place_link(it->second.rows, link);
    ++generation_;
    return true;
}

std::size_t Registry::page_count(std::string_view parent) const {
    const auto it = page_links_.find(parent);
    return it == page_links_.end() ? 0 : it->second.size();
}

void Registry::adopt_orphan_pages(std::string_view from, std::string_view to) {
    const auto it = page_links_.find(from);
    if (it == page_links_.end() || menus_.find(from) != menus_.end()) return;
    std::vector<RowDef> links = std::move(it->second);
    page_links_.erase(it);
    auto& kept = page_links_[std::string(to)];
    const auto parent = menus_.find(to);
    for (RowDef& link : links) {
        if (parent != menus_.end()) place_link(parent->second.rows, link);
        kept.push_back(std::move(link));
    }
    ++generation_;
}

std::vector<std::string> Registry::unlinked_parents() const {
    std::vector<std::string> out;
    for (const auto& [parent, links] : page_links_) {
        if (!links.empty() && menus_.find(parent) == menus_.end()) out.push_back(parent);
    }
    return out;
}

const MenuDef* Registry::find(std::string_view id) const {
    const auto it = menus_.find(id);
    return it == menus_.end() ? nullptr : &it->second;
}

std::vector<std::string> Registry::ids() const {
    std::vector<std::string> out;
    for (const auto& [k, v] : menus_) out.push_back(k);
    return out;
}

void Registry::focus(std::string menu_id, std::string row_id) {
    focus_ = std::make_pair(std::move(menu_id), std::move(row_id));
}

std::optional<std::string> Registry::take_focus(std::string_view menu_id) {
    if (!focus_ || focus_->first != menu_id) return std::nullopt;
    std::string row = std::move(focus_->second);
    focus_.reset();
    return row;
}

void Registry::clear() {
    // An open Controller may still point at a definition (its title, a frame dropped by the clear): kept alive.
    retired_.push_back(std::move(menus_));
    menus_.clear();
    page_links_.clear();
    ++generation_;
}

void Registry::native_value(std::string name, NativeValue v) { values_[std::move(name)] = std::move(v); }
void Registry::native_action(std::string name, std::function<void()> fn) { actions_[std::move(name)] = std::move(fn); }
void Registry::native_list(std::string name, std::function<std::vector<Choice>()> fn) {
    lists_[std::move(name)] = std::move(fn);
}
void Registry::native_highlight(std::string name, std::function<void(const std::string&)> fn) {
    highlights_[std::move(name)] = std::move(fn);
}
void Registry::native_setting(std::string name, NativeSetting s) { settings_[std::move(name)] = std::move(s); }
void Registry::native_visible(std::string name, std::function<bool()> fn) { visible_[std::move(name)] = std::move(fn); }
void Registry::native_enabled(std::string name, std::function<bool()> fn) { enabled_[std::move(name)] = std::move(fn); }

bool Registry::has_native(std::string_view name) const {
    return values_.count(name) || actions_.count(name) || lists_.count(name) || settings_.count(name);
}

std::optional<bool> Registry::check_visible(std::string_view name) const {
    const auto it = visible_.find(name);
    if (it == visible_.end() || !it->second) return std::nullopt;
    return it->second();
}

bool Registry::run_action(std::string_view name) const {
    const auto it = actions_.find(name);
    if (it == actions_.end() || !it->second) return false;
    it->second();
    return true;
}

std::optional<std::string> Registry::step_setting(std::string_view name, int dir) const {
    const auto it = settings_.find(name);
    if (it == settings_.end() || !it->second.step) return std::nullopt;
    if (const auto enabled = enabled_.find(name); enabled != enabled_.end() && !enabled->second()) {
        return it->second.text ? it->second.text() : std::string();
    }
    it->second.step(dir);
    return it->second.text ? it->second.text() : std::string();
}

void Registry::config_binder(std::function<bool(RowDef&)> fn) { config_binder_ = std::move(fn); }

void Registry::config_reader(std::string name, std::function<std::optional<Value>(std::string_view key)> fn) {
    for (auto& [n, f] : config_readers_) {
        if (n == name) {
            f = std::move(fn);
            return;
        }
    }
    config_readers_.emplace_back(std::move(name), std::move(fn));
}

std::optional<Value> Registry::config_value(std::string_view key) const {
    for (const auto& [name, read] : config_readers_) {
        if (std::optional<Value> v = read(key)) return v;
    }
    return std::nullopt;
}

namespace {

bool numeric(const Value& v) {
    return std::holds_alternative<std::int64_t>(v) || std::holds_alternative<double>(v);
}

// config.cfg spells numbers as text ("1", "-2.5").
bool numeric_text(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end != nullptr && *end == '\0';
}

bool same_value(const Value& actual, const Value& want) {
    if (std::holds_alternative<bool>(want)) return as_bool(actual) == std::get<bool>(want);
    if (numeric(want)) {
        if (numeric(actual) || std::holds_alternative<bool>(actual)) return as_double(actual) == as_double(want);
        double d = 0.0;
        if (const auto* s = std::get_if<std::string>(&actual)) return numeric_text(*s, d) && d == as_double(want);
        return false;
    }
    if (const auto* w = std::get_if<std::string>(&want)) {
        if (const auto* s = std::get_if<std::string>(&actual)) return *s == *w;
        double d = 0.0;
        if (numeric(actual)) return numeric_text(*w, d) && as_double(actual) == d;
        return as_string(actual) == *w;
    }
    return false;
}

}  // namespace

bool visible_if_holds(const VisibleIf& cond, const std::optional<Value>& actual) {
    if (!actual) return true;
    const auto in = [&](const std::vector<Value>& list) {
        return std::any_of(list.begin(), list.end(), [&](const Value& w) { return same_value(*actual, w); });
    };
    if (!cond.any.empty() && !in(cond.any)) return false;
    if (!cond.none.empty() && in(cond.none)) return false;
    if (cond.any.empty() && cond.none.empty()) return as_bool(*actual);
    return true;
}

bool Registry::resolve(RowDef& row) const {
    if (!row.visible_if.empty()) {
        // The row's or its native's own visibility still applies on top of the conditions.
        std::function<bool()> base = row.visible;
        if (!base) {
            if (const auto it = visible_.find(row.native); it != visible_.end()) base = it->second;
        }
        row.visible = [this, base = std::move(base), conds = std::move(row.visible_if)]() {
            if (base && !base()) return false;
            for (const VisibleIf& c : conds) {
                if (!visible_if_holds(c, config_value(c.key))) return false;
            }
            return true;
        };
        row.visible_if.clear();
    }
    bool config_ok = true;
    if (!row.config.empty()) config_ok = config_binder_ && config_binder_(row);
    if (row.native.empty()) return config_ok;
    bool found = false;
    if (const auto it = values_.find(row.native); it != values_.end()) {
        if (!row.get) row.get = it->second.get;
        if (!row.set) row.set = it->second.set;
        found = true;
    }
    if (const auto it = actions_.find(row.native); it != actions_.end()) {
        if (!row.activate) row.activate = it->second;
        found = true;
    }
    if (const auto it = lists_.find(row.native); it != lists_.end()) {
        if (!row.list) row.list = it->second;
        found = true;
    }
    if (const auto it = highlights_.find(row.native); it != highlights_.end()) {
        if (!row.on_highlight) row.on_highlight = it->second;
        found = true;
    }
    if (const auto it = settings_.find(row.native); it != settings_.end()) {
        if (!row.step_fn) row.step_fn = it->second.step;
        if (!row.text) row.text = it->second.text;
        if (!row.fraction) row.fraction = it->second.fraction;
        if (!row.texts) row.texts = it->second.texts;
        if (!row.help_fn) row.help_fn = it->second.help;
        found = true;
    }
    if (const auto it = visible_.find(row.native); it != visible_.end() && !row.visible) row.visible = it->second;
    if (const auto it = enabled_.find(row.native); it != enabled_.end() && !row.enabled) row.enabled = it->second;
    return found && config_ok;
}

// ---- Controller --------------------------------------------------------------------------

namespace {

bool selectable(const RowDef& r) { return r.kind != RowKind::Info && r.kind != RowKind::Header; }

const std::string kEmpty;

}  // namespace

bool Controller::push(std::string_view id) {
    const MenuDef* def = registry_->find(id);
    if (def == nullptr) return false;
    Frame f;
    f.def = def;
    f.rows = def->rows;
    for (RowDef& r : f.rows) registry_->resolve(r);
    f.generation = registry_->generation();
    f.id = std::string(id);
    dropdown_.reset();
    confirm_.reset();
    stack_.push_back(std::move(f));
    Frame& top = stack_.back();
    // Land on the first selectable row.
    const auto vis = visible_indices(top);
    for (std::size_t i = 0; i < vis.size(); ++i) {
        if (selectable(top.rows[vis[i]])) {
            top.sel = static_cast<int>(i);
            break;
        }
    }
    clamp(top);
    if (def->on_open) def->on_open();
    if (hooks::bus().has("menu.open")) {
        hooks::Event ev("menu.open");
        ev.set("menu", std::string(id), true);
        hooks::bus().emit(ev);
    }
    return true;
}

void Controller::highlight_dropdown() {
    if (!dropdown_ || dropdown_->sel < 0 || dropdown_->sel >= static_cast<int>(dropdown_->items.size())) return;
    const RowDef& r = stack_.back().rows[dropdown_->row];
    if (r.on_highlight) r.on_highlight(dropdown_->items[dropdown_->sel].value);
}

void Controller::end_dropdown() {
    if (!dropdown_) return;
    std::function<void(const std::string&)> fn;
    if (!stack_.empty() && dropdown_->row < stack_.back().rows.size())
        fn = stack_.back().rows[dropdown_->row].on_highlight;
    dropdown_.reset();
    if (fn) fn(std::string());
}

bool Controller::open(std::string_view id) {
    end_dropdown();
    stack_.clear();
    dropdown_.reset();
    confirm_.reset();
    return push(id);
}

bool Controller::enter(std::string_view id) {
    if (!is_open()) return false;
    end_dropdown();
    return push(id);
}

void Controller::close() {
    end_dropdown();
    stack_.clear();
    dropdown_.reset();
    confirm_.reset();
}

const MenuDef* Controller::current() const { return stack_.empty() ? nullptr : stack_.back().def; }

const std::string& Controller::title() const { return stack_.empty() ? kEmpty : stack_.back().def->title; }

void Controller::set_visible_rows(int n) {
    visible_rows_ = std::max(1, n);
    for (Frame& f : stack_) clamp(f);
}

void Controller::set_row_heights(int pitch, std::function<int(const RowView&)> small_height) {
    row_pitch_ = small_height ? std::max(1, pitch) : 1;
    small_height_ = std::move(small_height);
    for (Frame& f : stack_) clamp(f);
}

std::vector<int> Controller::heights(const Frame& f, const std::vector<std::size_t>& vis) const {
    if (!small_height_) return {};
    std::vector<int> out;
    bool varied = false;
    out.reserve(vis.size());
    for (const std::size_t i : vis) {
        const RowDef& r = f.rows[i];
        if (r.small && r.kind == RowKind::Info) {
            out.push_back(std::max(1, small_height_(view_of(r))));
            varied = true;
        } else {
            out.push_back(row_pitch_);
        }
    }
    if (!varied) out.clear();
    return out;
}

void Controller::apply_focus(Frame& f) {
    const std::optional<std::string> row = registry_->take_focus(f.id);
    if (!row) return;
    const auto vis = visible_indices(f);
    for (std::size_t i = 0; i < vis.size(); ++i) {
        if (f.rows[vis[i]].id != *row) continue;
        for (std::size_t j = i; j < vis.size(); ++j) {
            if (selectable(f.rows[vis[j]])) {
                f.sel = static_cast<int>(j);
                break;
            }
        }
        f.top = static_cast<int>(i);
        f.free_scroll = true;
        clamp(f);
        return;
    }
}

std::vector<std::size_t> Controller::visible_indices(const Frame& f) const {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < f.rows.size(); ++i) {
        if (!f.rows[i].visible || f.rows[i].visible()) out.push_back(i);
    }
    // A header stays only when a row of its group (up to the next header) is shown.
    std::vector<std::size_t> kept;
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (f.rows[out[i]].kind == RowKind::Header &&
            (i + 1 == out.size() || f.rows[out[i + 1]].kind == RowKind::Header)) {
            continue;
        }
        kept.push_back(out[i]);
    }
    return kept;
}

bool Controller::row_enabled(const RowDef& r) const { return !r.enabled || r.enabled(); }

std::vector<Choice> Controller::row_choices(const RowDef& r) const {
    if (r.list) return r.list();
    return r.choices;
}

void Controller::clamp(Frame& f) const {
    const auto vis = visible_indices(f);
    const int n = static_cast<int>(vis.size());
    if (n == 0) {
        f.sel = 0;
        f.top = 0;
        return;
    }
    f.sel = std::clamp(f.sel, 0, n - 1);
    const std::vector<int> h = heights(f, vis);
    if (h.empty()) {
        if (f.free_scroll) {
            f.top = std::clamp(f.top, 0, std::max(0, n - visible_rows_));
            return;
        }
        // The caption of the selected row's group scrolls into view with it.
        const bool headed = f.sel > 0 && f.rows[vis[static_cast<std::size_t>(f.sel - 1)]].kind == RowKind::Header;
        if (f.sel - (headed ? 1 : 0) < f.top) f.top = f.sel - (headed ? 1 : 0);
        if (f.sel >= f.top + visible_rows_) f.top = f.sel - visible_rows_ + 1;
        f.top = std::clamp(f.top, 0, std::max(0, n - visible_rows_));
        return;
    }
    const int window = window_height();
    // The first row of a window that ends with row `last`: rows first..last fit (at least `last` itself).
    const auto first_showing = [&](int last) {
        int first = last;
        int sum = h[static_cast<std::size_t>(last)];
        while (first > 0 && sum + h[static_cast<std::size_t>(first - 1)] <= window) {
            sum += h[static_cast<std::size_t>(--first)];
        }
        return first;
    };
    const int max_top = first_showing(n - 1);
    if (f.free_scroll) {
        f.top = std::clamp(f.top, 0, max_top);
        return;
    }
    const bool headed = f.sel > 0 && f.rows[vis[static_cast<std::size_t>(f.sel - 1)]].kind == RowKind::Header;
    if (f.sel - (headed ? 1 : 0) < f.top) f.top = f.sel - (headed ? 1 : 0);
    // The notes under the selected row come into view with it while they fit.
    int last = f.sel;
    while (last + 1 < n && f.rows[vis[static_cast<std::size_t>(last + 1)]].small &&
           f.rows[vis[static_cast<std::size_t>(last + 1)]].kind == RowKind::Info && first_showing(last + 1) <= f.sel) {
        ++last;
    }
    int sum = 0;
    for (int i = std::max(0, f.top); i <= last; ++i) sum += h[static_cast<std::size_t>(i)];
    if (sum > window) f.top = std::min(f.sel, first_showing(last));
    f.top = std::clamp(f.top, 0, max_top);
}

int Controller::selection() const { return stack_.empty() ? 0 : stack_.back().sel; }
int Controller::scroll_top() const {
    if (dropdown_) return dropdown_->top;
    return stack_.empty() ? 0 : stack_.back().top;
}

void Controller::set_scroll_top(int top) {
    if (dropdown_) {
        const int n = static_cast<int>(dropdown_->items.size());
        dropdown_->top = std::clamp(top, 0, std::max(0, n - visible_rows_));
        dropdown_->free_scroll = true;
        return;
    }
    if (stack_.empty()) return;
    Frame& f = stack_.back();
    f.top = top;
    f.free_scroll = true;
    clamp(f);
}

std::string Controller::value_text(const RowDef& r, bool& is_key) const {
    is_key = false;
    switch (r.kind) {
        case RowKind::Toggle:
            is_key = true;
            return (r.get && as_bool(r.get())) ? "ui.menu.on" : "ui.menu.off";
        case RowKind::Slider:
            if (r.get && r.percent && r.max > r.min) {
                const double f = std::clamp((as_double(r.get()) - r.min) / (r.max - r.min), 0.0, 1.0);
                return format_number(std::round(f * 100.0), true) + "%";
            }
            return r.get ? format_number(as_double(r.get()), r.integer) : std::string();
        case RowKind::Number:
            return r.get ? format_number(static_cast<double>(as_int(r.get())), true) : std::string();
        case RowKind::Choice:
        case RowKind::Dropdown: {
            if (!r.get) return {};
            const std::string cur = as_string(r.get());
            for (const Choice& c : row_choices(r)) {
                if (c.value == cur) {
                    is_key = true;
                    return c.label;
                }
            }
            return cur;
        }
        case RowKind::Setting:
        case RowKind::Info:
            if (r.text) return r.text();
            return r.get ? as_string(r.get()) : std::string();
        default:
            return {};
    }
}

std::vector<RowView> Controller::rows() const {
    std::vector<RowView> out;
    if (stack_.empty()) return out;
    const Frame& f = stack_.back();
    const auto vis = visible_indices(f);
    for (std::size_t i = 0; i < vis.size(); ++i) {
        RowView v = view_of(f.rows[vis[i]]);
        v.selected = static_cast<int>(i) == f.sel;
        out.push_back(std::move(v));
    }
    return out;
}

RowView Controller::view_of(const RowDef& r) const {
    RowView v;
    v.def = &r;
    v.value = value_text(r, v.value_is_key);
    v.enabled = row_enabled(r);
    if (r.kind == RowKind::Slider && r.percent && r.get && r.max > r.min) {
        v.fraction = std::clamp((as_double(r.get()) - r.min) / (r.max - r.min), 0.0, 1.0);
    } else if (r.kind == RowKind::Setting && r.fraction) {
        const double f = r.fraction();
        if (f >= 0.0) v.fraction = std::min(f, 1.0);
    }
    if (r.kind == RowKind::Toggle) {
        v.texts = {"ui.menu.on", "ui.menu.off"};
    } else if (r.kind == RowKind::Choice) {
        for (const Choice& c : row_choices(r)) v.texts.push_back(c.label);
    } else if (r.kind == RowKind::Setting && r.texts) {
        v.texts = r.texts();
    }
    v.has_arrows = v.enabled && (r.kind == RowKind::Toggle || r.kind == RowKind::Slider ||
                                 r.kind == RowKind::Number || r.kind == RowKind::Choice ||
                                 r.kind == RowKind::Setting);
    return v;
}

std::string Controller::help() const {
    if (stack_.empty()) return {};
    const Frame& f = stack_.back();
    const auto vis = visible_indices(f);
    if (f.sel < 0 || f.sel >= static_cast<int>(vis.size())) return {};
    const RowDef& row = f.rows[vis[f.sel]];
    if (row.help_fn) {
        if (std::string text = row.help_fn(); !text.empty()) return text;
    }
    return row.help;
}

DropdownView Controller::dropdown() const {
    DropdownView v;
    if (dropdown_) {
        v.items = dropdown_->items;
        v.selected = dropdown_->sel;
        v.top = dropdown_->top;
        v.current = dropdown_->current;
        if (!stack_.empty() && dropdown_->row < stack_.back().rows.size()) v.label = stack_.back().rows[dropdown_->row].label;
    }
    return v;
}

Event Controller::step_row(Frame& f, std::size_t ri, int dir, bool big) {
    RowDef& r = f.rows[ri];
    if (!row_enabled(r)) return Event::Refused;
    last_row_id_ = r.id;
    switch (r.kind) {
        case RowKind::Toggle: {
            if (!r.get || !r.set) return Event::None;
            r.set(Value{!as_bool(r.get())});
            return Event::Changed;
        }
        case RowKind::Slider: {
            if (!r.get || !r.set) return Event::None;
            const double grid = r.step > 0 ? r.step : 1.0;
            double v = as_double(r.get()) + dir * grid * (big ? 10.0 : 1.0);
            // Snap to the step grid so repeated float steps do not drift (0.1 + 0.1 + 0.1).
            v = std::round(v / grid) * grid;
            v = std::clamp(v, r.min, r.max);
            if (r.integer) r.set(Value{static_cast<std::int64_t>(std::llround(v))});
            else r.set(Value{v});
            return Event::Changed;
        }
        case RowKind::Number: {
            if (!r.get || !r.set) return Event::None;
            const std::int64_t st = std::max<std::int64_t>(1, static_cast<std::int64_t>(r.step)) * (big ? 10 : 1);
            std::int64_t v = as_int(r.get()) + dir * st;
            v = std::clamp<std::int64_t>(v, static_cast<std::int64_t>(r.min), static_cast<std::int64_t>(r.max));
            r.set(Value{v});
            return Event::Changed;
        }
        case RowKind::Choice: {
            if (!r.get || !r.set) return Event::None;
            const auto list = row_choices(r);
            if (list.empty()) return Event::None;
            const std::string cur = as_string(r.get());
            int idx = 0;
            for (std::size_t i = 0; i < list.size(); ++i) {
                if (list[i].value == cur) {
                    idx = static_cast<int>(i);
                    break;
                }
            }
            const int n = static_cast<int>(list.size());
            idx = ((idx + dir) % n + n) % n;
            r.set(Value{list[idx].value});
            return Event::Changed;
        }
        case RowKind::Setting:
            if (!r.step_fn) return Event::None;
            r.step_fn(dir);
            return Event::Changed;
        default:
            return Event::None;
    }
}

Event Controller::run_action(RowDef& r) {
    const bool closes = r.close_on_activate;
    if (r.activate) r.activate();
    if (closes) {
        close();
        return Event::Closed;
    }
    return Event::Activated;
}

const std::string& Controller::confirm_question() const {
    if (!confirm_ || stack_.empty()) return kEmpty;
    if (confirm_->leaving) return confirm_->question;
    return stack_.back().rows[confirm_->row].confirm;
}

Event Controller::leave_frame() {
    if (stack_.size() > 1) {
        stack_.pop_back();
        clamp(stack_.back());
        return Event::Back;
    }
    close();
    return Event::Closed;
}

Event Controller::update_confirm(const MenuInput& in) {
    Confirm& c = *confirm_;
    if (in.cancel || in.right_click) {
        confirm_.reset();
        return Event::Back;
    }
    if (c.leaving && (in.confirm || (in.click && (in.hover_answer == 0 || in.hover_answer == 1)))) {
        const bool yes = in.click ? in.hover_answer == 0 : c.yes;
        confirm_.reset();
        // Copied first: the answer may define the menu again, which replaces the def.
        const std::string id = stack_.back().id;
        const MenuDef* def = registry_->find(id);
        const std::function<void(bool)> answer = def != nullptr ? def->on_leave : nullptr;
        if (answer) answer(yes);
        if (stack_.empty() || stack_.back().id != id) return Event::Back;
        return leave_frame();
    }
    bool moved = false;
    auto pick = [&](bool yes) {
        if (c.yes != yes) {
            c.yes = yes;
            moved = true;
        }
    };
    if (in.left || in.up) pick(true);
    if (in.right || in.down) pick(false);
    const bool answer = in.hover_answer == 0 || in.hover_answer == 1;
    if (answer && (in.pointer_moved || in.click)) pick(in.hover_answer == 0);
    if (in.confirm || (in.click && answer)) {
        const bool yes = c.yes;
        const std::size_t row = c.row;
        confirm_.reset();
        if (!yes) return Event::Back;
        return run_action(stack_.back().rows[row]);
    }
    return moved ? Event::Moved : Event::None;
}

Event Controller::activate_row(Frame& f, std::size_t ri) {
    RowDef& r = f.rows[ri];
    if (!row_enabled(r)) return Event::Refused;
    last_row_id_ = r.id;
    switch (r.kind) {
        case RowKind::Action:
            if (!r.confirm.empty()) {
                confirm_ = Confirm{ri, false, false, {}};
                return Event::Entered;
            }
            return run_action(r);
        case RowKind::Submenu:
            return push(r.submenu) ? Event::Entered : Event::Refused;
        case RowKind::Dropdown: {
            Dropdown d;
            d.items = row_choices(r);
            if (d.items.empty()) return Event::Refused;
            d.row = ri;
            if (r.get) {
                const std::string cur = as_string(r.get());
                for (std::size_t i = 0; i < d.items.size(); ++i) {
                    if (d.items[i].value == cur) {
                        d.sel = static_cast<int>(i);
                        break;
                    }
                }
            }
            d.top = std::max(0, d.sel - visible_rows_ / 2);
            d.top = std::min(d.top, std::max(0, static_cast<int>(d.items.size()) - visible_rows_));
            dropdown_ = std::move(d);
            highlight_dropdown();
            return Event::Entered;
        }
        case RowKind::Choice:
        case RowKind::Setting:
        case RowKind::Toggle:
            if (touch_) {
                // A finger sets a bar where it presses it; a tap beside the bar only selects the row.
                if (r.kind == RowKind::Setting && r.fraction && r.fraction() >= 0.0) return Event::None;
                const Event e = open_picker(f, ri);
                if (e != Event::None) return e;
            }
            return step_row(f, ri, +1, false);
        default:
            return Event::None;
    }
}

Event Controller::open_picker(Frame& f, std::size_t ri) {
    const RowDef& r = f.rows[ri];
    Dropdown d;
    d.row = ri;
    std::string current;
    if (r.kind == RowKind::Toggle) {
        if (!r.get || !r.set) return Event::None;
        d.items = {Choice{"0", "ui.menu.off"}, Choice{"1", "ui.menu.on"}};
        current = as_bool(r.get()) ? "1" : "0";
        d.pick = Dropdown::Pick::Toggle;
    } else if (r.kind == RowKind::Choice) {
        d.items = row_choices(r);
        if (r.get) current = as_string(r.get());
    } else {
        // A bar is set by its position; a setting's texts are the values it can show.
        if (r.fraction && r.fraction() >= 0.0) return Event::None;
        if (!r.texts) return Event::None;
        bool key = false;
        current = value_text(r, key);
        for (const std::string& t : r.texts()) {
            // Custom is what a row reads when no value of its own matches: nothing to pick.
            bool seen = t.empty() || t.ends_with(".value.custom");
            for (const Choice& c : d.items) seen = seen || c.value == t;
            if (!seen) d.items.push_back(Choice{t, t});
        }
        d.pick = Dropdown::Pick::Step;
    }
    // Every row of values opens the list, two values (On / Off) too: a tap never changes a setting unasked.
    if (d.items.size() < 2) return Event::None;
    for (std::size_t i = 0; i < d.items.size(); ++i) {
        if (d.items[i].value == current) d.sel = d.current = static_cast<int>(i);
    }
    d.top = std::max(0, d.sel - visible_rows_ / 2);
    d.top = std::min(d.top, std::max(0, static_cast<int>(d.items.size()) - visible_rows_));
    dropdown_ = std::move(d);
    return Event::Entered;
}

Event Controller::set_bar(Frame& f, std::size_t ri, double at) {
    RowDef& r = f.rows[ri];
    if (!row_enabled(r)) return Event::Refused;
    at = std::clamp(at, 0.0, 1.0);
    if (r.kind == RowKind::Slider && r.get && r.set && r.max > r.min) {
        const double grid = r.step > 0 ? r.step : 1.0;
        double v = std::round((r.min + at * (r.max - r.min)) / grid) * grid;
        v = std::clamp(v, r.min, r.max);
        if (std::fabs(v - as_double(r.get())) < grid * 0.5) return Event::None;
        if (r.integer) r.set(Value{static_cast<std::int64_t>(std::llround(v))});
        else r.set(Value{v});
        return Event::Changed;
    }
    if (r.kind != RowKind::Setting || !r.fraction || r.fraction() < 0.0) return Event::None;
    // A native bar knows only its steps: step towards the point until the next step would pass it.
    bool changed = false;
    for (int i = 0; i < 64; ++i) {
        const double now = r.fraction();
        const int dir = at > now ? +1 : -1;
        step_row(f, ri, dir, false);
        const double next = r.fraction();
        if (next == now) break;
        changed = true;
        if ((dir > 0 && next >= at) || (dir < 0 && next <= at)) {
            // Past the point: keep whichever of the two is nearer.
            if (std::fabs(now - at) < std::fabs(next - at)) step_row(f, ri, -dir, false);
            break;
        }
    }
    return changed ? Event::Changed : Event::None;
}

Event Controller::update_dropdown(const MenuInput& in) {
    Dropdown& d = *dropdown_;
    const int n = static_cast<int>(d.items.size());
    auto move = [&](int delta, bool wrap) {
        int s = d.sel + delta;
        if (wrap) s = ((s % n) + n) % n;
        else s = std::clamp(s, 0, n - 1);
        const bool moved = s != d.sel;
        d.sel = s;
        if (d.sel < d.top) d.top = d.sel;
        if (d.sel >= d.top + visible_rows_) d.top = d.sel - visible_rows_ + 1;
        return moved;
    };
    if (in.cancel || in.right_click || (in.touch && in.click && in.hover_row < 0)) {
        end_dropdown();
        return Event::Back;
    }
    bool moved = false;
    if (in.up) moved |= move(-1, true);
    if (in.down) moved |= move(+1, true);
    if (in.page_up) moved |= move(-visible_rows_, false);
    if (in.page_down) moved |= move(+visible_rows_, false);
    if (in.wheel) moved |= move(-in.wheel, false);
    if (in.hover_row >= 0 && in.pointer_moved && d.top + in.hover_row < n && d.top + in.hover_row != d.sel) {
        d.sel = d.top + in.hover_row;
        moved = true;
    }
    const bool click = in.click && in.hover_row >= 0 && d.top + in.hover_row < n;
    if (click) d.sel = d.top + in.hover_row;
    if (in.confirm || click) {
        Frame& f = stack_.back();
        RowDef& r = f.rows[d.row];
        const std::string picked = d.items[d.sel].value;
        const bool close_all = r.close_on_activate;
        last_row_id_ = r.id;
        const auto highlight = r.on_highlight;
        const Dropdown::Pick pick = d.pick;
        const std::size_t row = d.row;
        dropdown_.reset();
        if (highlight) highlight(std::string());
        if (pick == Dropdown::Pick::Toggle) {
            if (r.set && (r.get ? as_bool(r.get()) : false) != (picked == "1")) r.set(Value{picked == "1"});
            return Event::Changed;
        }
        if (pick == Dropdown::Pick::Step) {
            // Step on until the row shows the picked value; one that cannot be reached (Custom) leaves it where
            // it was once the steps come round.
            bool key = false;
            const std::string start = value_text(r, key);
            for (int i = 0; i < 64 && value_text(r, key) != picked; ++i) {
                step_row(f, row, +1, false);
                if (value_text(r, key) == start) break;
            }
            return Event::Changed;
        }
        if (r.set) r.set(Value{picked});
        if (close_all) {
            close();
            return Event::Closed;
        }
        return Event::Changed;
    }
    if (moved) highlight_dropdown();
    return moved ? Event::Moved : Event::None;
}

void Controller::refresh() {
    // An open list or question points at a row by index: the copy waits until it is answered.
    if (dropdown_ || confirm_) return;
    for (Frame& f : stack_) {
        if (f.generation == registry_->generation()) continue;
        f.generation = registry_->generation();
        const MenuDef* def = registry_->find(f.id);
        if (def == nullptr) continue;  // dropped by a clear(): keep the rows it had
        f.def = def;
        f.rows = def->rows;
        for (RowDef& r : f.rows) registry_->resolve(r);
        clamp(f);
    }
    if (!stack_.empty()) apply_focus(stack_.back());
}

Event Controller::update(const MenuInput& in) {
    touch_ = in.touch;
    refresh();
    if (!stack_.empty() && !dropdown_ && !confirm_) {
        // A copy: the callback may define the menu again, which replaces the stored def.
        if (const MenuDef* def = registry_->find(stack_.back().id); def != nullptr && def->on_update) {
            const std::function<void()> fn = def->on_update;
            fn();
            refresh();
        }
    }
    const Event e = update_frame(in);
    refresh();
    return e;
}

Event Controller::update_frame(const MenuInput& in) {
    if (stack_.empty()) return Event::None;
    if (dropdown_) return update_dropdown(in);
    if (confirm_) return update_confirm(in);

    Frame& f = stack_.back();
    clamp(f);
    const auto vis = visible_indices(f);
    const int n = static_cast<int>(vis.size());

    if (in.cancel || in.right_click) {
        const MenuDef* def = registry_->find(f.id);
        if (def != nullptr && def->leave_confirm) {
            std::string question = def->leave_confirm();
            if (!question.empty()) {
                Confirm c;
                c.leaving = true;
                c.question = std::move(question);
                confirm_ = std::move(c);
                return Event::Entered;
            }
        }
        return leave_frame();
    }
    if (n == 0) return Event::None;

    auto move_to = [&](int target, int dir) {
        // Skip read-only rows in the travel direction; wrap around.
        for (int tries = 0; tries < n; ++tries) {
            target = ((target % n) + n) % n;
            if (selectable(f.rows[vis[target]])) break;
            target += dir;
        }
        const bool moved = target != f.sel;
        f.sel = target;
        clamp(f);
        return moved;
    };

    // The keys and the wheel move the cursor, and the page follows it again.
    if (in.up || in.down || in.wheel != 0 || in.page_up || in.page_down) f.free_scroll = false;
    bool moved = false;
    if (in.up) moved |= move_to(f.sel - 1, -1);
    if (in.down) moved |= move_to(f.sel + 1, +1);
    if (in.wheel) {
        const int target = std::clamp(f.sel - in.wheel, 0, n - 1);
        moved |= move_to(target, in.wheel > 0 ? -1 : 1);
    }
    if (in.hover_row >= 0 && (in.pointer_moved || in.click)) {
        const int idx = f.top + in.hover_row;
        if (idx >= 0 && idx < n && selectable(f.rows[vis[idx]]) && idx != f.sel) {
            f.sel = idx;
            clamp(f);
            moved = true;
        }
    }
    const RowDef& cur = f.rows[vis[f.sel]];
    const bool numeric = cur.kind == RowKind::Slider || cur.kind == RowKind::Number;
    if (in.page_up || in.page_down) {
        if (numeric) {
            const Event e = step_row(f, vis[f.sel], in.page_up ? -1 : +1, true);
            if (e != Event::None) return e;
        } else {
            moved |= move_to(f.sel + (in.page_down ? visible_rows_ : -visible_rows_), in.page_down ? 1 : -1);
        }
    }
    if (in.left || in.right) {
        const RowDef& r = f.rows[vis[f.sel]];
        const bool bar = (r.kind == RowKind::Slider && r.percent) ||
                         (r.kind == RowKind::Setting && r.fraction && r.fraction() >= 0.0);
        const int dir = (in.right ? +1 : -1) * (in.mirrored && !bar ? -1 : 1);
        const Event e = step_row(f, vis[f.sel], dir, false);
        if (e != Event::None) return e;
    }
    if (in.bar_at >= 0.0) return set_bar(f, vis[f.sel], in.bar_at);
    const bool click = in.click && in.hover_row >= 0 && f.top + in.hover_row < n;
    if (in.confirm || click) {
        const Event e = activate_row(f, vis[f.sel]);
        if (e != Event::None) return e;
    }
    return moved ? Event::Moved : Event::None;
}

}  // namespace sh::menu
