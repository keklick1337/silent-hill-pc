// SPDX-License-Identifier: GPL-3.0-or-later
// The settings model and gamedata/menu/settings.json.

#include "engine/screens/settings_model.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>

#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::screens {

namespace {

using Kind = SettingRow::Kind;
using QKind = QuickSetting::Kind;
using Format = QuickSetting::Format;

template <typename... Args>
std::string format(const char* pattern, Args... args) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), pattern, args...);
    return buf;
}

std::int32_t choice_index(const SettingRow& row, std::int32_t value) {
    for (std::size_t i = 0; i < row.values.size(); i++) {
        if (row.values[i] == value) {
            return static_cast<std::int32_t>(i);
        }
    }
    return 0;
}

float clamp(float v, float lo, float hi) {
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

using Size = std::pair<std::int32_t, std::int32_t>;

// How far a shape may be from a listed ratio and still count as it (1366x768 is 16:9, 3840x1600 is 21:9).
constexpr double kAspectTolerance = 0.035;

bool smaller(const Size& a, const Size& b) {
    const std::int64_t pa = static_cast<std::int64_t>(a.first) * a.second;
    const std::int64_t pb = static_cast<std::int64_t>(b.first) * b.second;
    return pa != pb ? pa < pb : a.first < b.first;
}

}  // namespace

std::int32_t SettingsModel::aspect_of(std::int32_t width, std::int32_t height) const {
    if (width <= 0 || height <= 0) {
        return -1;
    }
    const double shape = static_cast<double>(width) / height;
    std::int32_t best = -1;
    double best_off = kAspectTolerance;
    for (std::size_t i = 0; i < data_.aspects.size(); i++) {
        const double ratio = static_cast<double>(data_.aspects[i].first) / data_.aspects[i].second;
        const double off = shape > ratio ? shape / ratio - 1.0 : ratio / shape - 1.0;
        if (off < best_off) {
            best_off = off;
            best = static_cast<std::int32_t>(i);
        }
    }
    return best;
}

std::vector<Size> SettingsModel::resolutions_of(std::int32_t aspect, SettingsHost& host) const {
    std::vector<Size> out;
    for (const Size& r : data_.resolutions) {
        if (aspect < 0 || aspect_of(r.first, r.second) == aspect) {
            out.push_back(r);
        }
    }
    Size display{0, 0};
    host.display_size(display.first, display.second);
    if (display.first > 0 && display.second > 0 && (aspect < 0 || aspect_of(display.first, display.second) == aspect) &&
        std::find(out.begin(), out.end(), display) == out.end()) {
        out.push_back(display);
    }
    std::sort(out.begin(), out.end(), smaller);
    return out;
}

std::string SettingsModel::aspect_label(SettingsHost& host) const {
    std::int32_t w = 0;
    std::int32_t h = 0;
    host.window_size(w, h);
    const std::int32_t a = aspect_of(w, h);
    return a < 0 ? std::string() : format("%d:%d", data_.aspects[a].first, data_.aspects[a].second);
}

std::vector<std::string> SettingsModel::aspect_labels() const {
    std::vector<std::string> out;
    for (const Size& a : data_.aspects) {
        out.push_back(format("%d:%d", a.first, a.second));
    }
    return out;
}

void SettingsModel::adjust_aspect(std::int32_t dir, SettingsHost& host) const {
    const auto n = static_cast<std::int32_t>(data_.aspects.size());
    if (n == 0) {
        return;
    }
    std::int32_t w = 0;
    std::int32_t h = 0;
    host.window_size(w, h);
    // From a shape of no listed ratio, right takes the first and left the last.
    std::int32_t a = aspect_of(w, h);
    if (a < 0) {
        a = dir > 0 ? -1 : n;
    }
    for (std::int32_t tries = 0; tries < n; tries++) {
        a = (a + dir + n) % n;
        const std::vector<Size> list = resolutions_of(a, host);
        if (list.empty()) {
            continue;
        }
        const Size* pick = &list.front();
        for (const Size& r : list) {
            if (std::abs(r.second - h) < std::abs(pick->second - h)) {
                pick = &r;
            }
        }
        host.set_window_size(pick->first, pick->second);
        host.echo(format("Resolution: %dx%d", pick->first, pick->second));
        return;
    }
}

const SettingRow* SettingsModel::find(std::string_view key) const {
    if (key.empty()) {
        return nullptr;
    }
    for (const SettingRow& row : data_.rows) {
        if (row.setting == key) {
            return &row;
        }
    }
    return nullptr;
}

std::string SettingsModel::value_label(const SettingRow& row, SettingsHost& host) const {
    switch (row.kind) {
        case Kind::Resolution: {
            std::int32_t w = 0;
            std::int32_t h = 0;
            host.window_size(w, h);
            return format("%dx%d", w, h);
        }
        case Kind::Slider:
            return format("%.2f", static_cast<double>(host.float_setting(row.setting)));
        case Kind::Map: {
            const std::string name = host.start_map();
            return name.empty() ? data_.default_map : name;
        }
        case Kind::Choice: {
            const std::int32_t idx = choice_index(row, host.int_setting(row.setting));
            return row.labels.empty() ? format("%d", row.values[idx]) : row.labels[idx];
        }
    }
    return "";
}

void SettingsModel::adjust(const SettingRow& row, std::int32_t dir, SettingsHost& host) const {
    switch (row.kind) {
        case Kind::Resolution: {
            std::int32_t w = 0;
            std::int32_t h = 0;
            host.window_size(w, h);
            const std::vector<Size> list = resolutions_of(aspect_of(w, h), host);
            const auto n = static_cast<std::int32_t>(list.size());
            if (n == 0) {
                break;
            }
            // A size the list lacks (a window resized by hand) steps to its neighbours in the list.
            std::int32_t below = -1;
            std::int32_t at = -1;
            for (std::int32_t i = 0; i < n; i++) {
                if (list[i] == Size{w, h}) {
                    at = i;
                } else if (smaller(list[i], Size{w, h})) {
                    below = i;
                }
            }
            const std::int32_t idx = at >= 0 ? (at + dir + n) % n : dir > 0 ? (below + 1) % n : (below + n) % n;
            host.set_window_size(list[idx].first, list[idx].second);
            host.echo(format("Resolution: %dx%d", list[idx].first, list[idx].second));
            break;
        }
        case Kind::Slider: {
            const float v = clamp(host.float_setting(row.setting) + static_cast<float>(dir) * row.step, row.min, row.max);
            const std::string text = format("%.3f", static_cast<double>(v));
            host.set_float_setting(row.setting, v, &text);
            host.echo(format("%s: %.2f", row.label.c_str(), static_cast<double>(v)));
            break;
        }
        case Kind::Map: {
            const std::int32_t count = host.map_count();
            std::int32_t idx = host.map_index(host.start_map());
            if (idx < 0) {
                idx = 0;
            }
            idx = (idx + dir + count) % count;
            const std::string name = host.map_name(idx);
            host.set_start_map(name);
            const std::string desc = host.map_description(idx);
            host.echo(format("Map: %s (%s)", name.c_str(), desc.empty() ? name.c_str() : desc.c_str()));
            break;
        }
        case Kind::Choice: {
            const auto n = static_cast<std::int32_t>(row.values.size());
            const std::int32_t idx = (choice_index(row, host.int_setting(row.setting)) + dir + n) % n;
            host.set_int_setting(row.setting, row.values[idx]);
            host.echo(format("%s: %s", row.label.c_str(), value_label(row, host).c_str()));
            if (!row.realtime) {
                host.echo(data_.restart_notice);
            }
            break;
        }
    }
}

void SettingsModel::reset_all(SettingsHost& host) const {
    for (const SettingRow& row : data_.rows) {
        if (row.keep_on_reset) {
            continue;
        }
        if (row.kind == Kind::Slider) {
            const float v = host.default_float(row.setting);
            const std::string text = format("%.3f", static_cast<double>(v));
            host.set_float_setting(row.setting, v, &text);
        } else if (row.kind == Kind::Choice) {
            host.set_int_setting(row.setting, host.default_int(row.setting));
        }
    }
    for (const QuickSetting& q : data_.quick) {
        // Volumes belong to the game's own audio config; the rest are integer settings by config key.
        if (q.kind == QuickSetting::Kind::Toggle || q.kind == QuickSetting::Kind::Cycle) {
            host.set_int_setting(q.setting, host.default_int(q.setting));
        }
    }
    host.echo(data_.reset_done);
}

const QuickSetting* SettingsModel::quick(std::string_view name) const {
    for (const QuickSetting& s : data_.quick) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

namespace {

std::int32_t cycle_index(const QuickSetting& s, std::int32_t value) {
    const auto n = static_cast<std::int32_t>(s.labels.empty() ? s.stored.size() : s.labels.size());
    if (!s.thresholds.empty()) {
        for (std::size_t i = 0; i < s.thresholds.size(); i++) {
            if (value <= s.thresholds[i]) {
                return static_cast<std::int32_t>(i);
            }
        }
        return static_cast<std::int32_t>(s.thresholds.size());
    }
    if (!s.stored.empty()) {
        for (std::size_t i = 0; i < s.stored.size(); i++) {
            if (s.stored[i] == value) {
                return static_cast<std::int32_t>(i);
            }
        }
        return s.default_index;
    }
    return s.mask ? (value & (n - 1)) : value;
}

}  // namespace

std::string SettingsModel::quick_label(const QuickSetting& s, SettingsHost& host) const {
    const auto label = [&](const SettingLabel& l) { return host.translate(l.table, l.text); };
    std::string text;
    switch (s.kind) {
        case QKind::Toggle:
            text = label(s.labels[host.int_setting(s.setting) != 0 ? 1 : 0]);
            break;
        case QKind::Cycle: {
            const std::int32_t value = host.int_setting(s.setting);
            if (s.format == Format::SizeSquare) {
                text = format("%d x %d", value, value);
                break;
            }
            std::int32_t idx = cycle_index(s, value);
            if (idx < 0 || idx >= static_cast<std::int32_t>(s.labels.size())) {
                if (s.fallback) {
                    text = label(*s.fallback);
                    break;
                }
                idx = 0;
            }
            text = label(s.labels[idx]);
            break;
        }
        case QKind::Float: {
            const float v = host.float_setting(s.setting);
            switch (s.format) {
                case Format::Fixed0: text = format("%.0f", static_cast<double>(v)); break;
                case Format::Fixed2: text = format("%.2f", static_cast<double>(v)); break;
                case Format::Fixed3: text = format("%.3f", static_cast<double>(v)); break;
                case Format::Percent: text = format("%d%%", static_cast<int>(v * 100.0f + 0.5f)); break;
                case Format::SignedPercent:
                    text = format("%+d%%", static_cast<int>(v + (v < 0.0f ? -0.5f : 0.5f)));
                    break;
                case Format::Rows:
                    text = host.translate_rows(format("%+d", static_cast<int>(v)));
                    break;
                case Format::OffOrFixed2:
                    text = v <= 0.0001f ? host.translate(LabelTable::QuickMenu, "Off")
                                        : format("%.2f", static_cast<double>(v));
                    break;
                default: break;
            }
            if (s.default_marker && v > s.default_marker->first && v < s.default_marker->second) {
                text += " " + host.translate(LabelTable::Quick, "(default)");
            }
            break;
        }
        case QKind::Offset: {
            const std::int32_t v = host.int_setting(s.setting);
            text = format("%+d", s.invert ? -v : v);
            break;
        }
        case QKind::Volume: {
            const auto step = static_cast<std::int32_t>(s.step);
            text = format("%d / %d", host.int_setting(s.setting) / step, static_cast<std::int32_t>(s.max) / step);
            break;
        }
    }
    if (s.suffix) {
        const bool zero = host.int_setting(s.suffix_setting) == 0;
        if (zero == s.suffix_when_zero) {
            text += "  " + label(*s.suffix);
        }
    }
    return text;
}

void SettingsModel::quick_adjust(const QuickSetting& s, std::int32_t dir, SettingsHost& host) const {
    switch (s.kind) {
        case QKind::Toggle:
            host.set_int_setting(s.setting, host.int_setting(s.setting) == 0 ? 1 : 0);
            host.play_sfx(MenuSfx::Move);
            break;

        case QKind::Cycle: {
            const auto n = static_cast<std::int32_t>(s.stored.empty() ? s.labels.size() : s.stored.size());
            const std::int32_t value = host.int_setting(s.setting);
            const std::int32_t idx = cycle_index(s, value);
            std::int32_t next = 0;
            if (s.wrap) {
                next = (idx + dir + n) % n;
            } else {
                next = value + dir;
                if (next < 0 || next >= n) {
                    host.play_sound(MenuSfx::Error);
                    break;
                }
            }
            host.set_int_setting(s.setting, s.stored.empty() ? next : s.stored[next]);
            host.play_sfx(MenuSfx::Move);
            break;
        }

        case QKind::Float: {
            const float old = host.float_setting(s.setting);
            float v = clamp(old + static_cast<float>(dir) * s.step, s.min, s.max);
            if (s.error_if_unchanged && v == old) {
                host.play_sound(MenuSfx::Error);
                break;
            }
            if (s.round) {
                // Steps of 0.01 would otherwise drift into the label.
                v = static_cast<float>(static_cast<int>(v * 1000.0f + (v < 0.0f ? -0.5f : 0.5f))) / 1000.0f;
            }
            const std::string text = format(s.decimals >= 3 ? "%.3f" : "%.2f", static_cast<double>(v));
            host.set_float_setting(s.setting, v, &text);
            if (s.round) {
                host.log_view_change(s.setting, text);
            }
            host.play_sfx(MenuSfx::Move);
            break;
        }

        case QKind::Offset: {
            const auto delta = static_cast<std::int32_t>(s.step) * (s.invert ? -dir : dir);
            std::int32_t v = host.int_setting(s.setting) + delta;
            if (v < static_cast<std::int32_t>(s.min)) v = static_cast<std::int32_t>(s.min);
            if (v > static_cast<std::int32_t>(s.max)) v = static_cast<std::int32_t>(s.max);
            host.set_int_setting(s.setting, v);
            host.play_sfx(MenuSfx::Move);
            break;
        }

        case QKind::Volume: {
            const std::int32_t old = host.int_setting(s.setting);
            std::int32_t v = old + dir * static_cast<std::int32_t>(s.step);
            if (v < 0) v = 0;
            if (v > static_cast<std::int32_t>(s.max)) v = static_cast<std::int32_t>(s.max);
            if (v == old) {
                host.play_sound(MenuSfx::Error);
                break;
            }
            host.set_int_setting(s.setting, v);
            host.play_sound(MenuSfx::Move);
            break;
        }
    }
}

namespace {

using json::Value;

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

std::string at(const Value& v, std::string_view what) {
    return "line " + std::to_string(v.line()) + ", column " + std::to_string(v.column()) + ": " + std::string(what);
}

const Value& member(const Value& parent, std::string_view key) {
    const Value* v = parent.find(key);
    if (v == nullptr) {
        fail(at(parent, "missing \"" + std::string(key) + "\""));
    }
    return *v;
}

void reject_unknown(const Value& object, std::initializer_list<std::string_view> known) {
    if (!object.is_object()) {
        fail(at(object, "an object was expected"));
    }
    for (const auto& [key, value] : object.members()) {
        bool found = key.rfind("_", 0) == 0 || key == "description";
        for (std::string_view k : known) {
            found = found || key == k;
        }
        if (!found) {
            fail(at(value, "unknown key \"" + key + "\""));
        }
    }
}

std::int32_t int_value(const Value& v, std::int32_t lo, std::int32_t hi, std::string_view what) {
    if (!v.is_integer() || v.integer() < lo || v.integer() > hi) {
        fail(at(v, std::string(what) + " must be an integer in " + std::to_string(lo) + ".." + std::to_string(hi)));
    }
    return static_cast<std::int32_t>(v.integer());
}

std::int32_t int_of(const Value& parent, std::string_view key, std::int32_t lo, std::int32_t hi) {
    return int_value(member(parent, key), lo, hi, "\"" + std::string(key) + "\"");
}

float real_of(const Value& parent, std::string_view key) {
    const Value& v = member(parent, key);
    if (!v.is_number()) {
        fail(at(v, "\"" + std::string(key) + "\" must be a number"));
    }
    return static_cast<float>(v.number());
}

std::string string_of(const Value& parent, std::string_view key) {
    const Value& v = member(parent, key);
    if (!v.is_string()) {
        fail(at(v, "\"" + std::string(key) + "\" must be a string"));
    }
    return v.string();
}

bool bool_of(const Value& parent, std::string_view key, bool fallback) {
    const Value* v = parent.find(key);
    if (v == nullptr) {
        return fallback;
    }
    if (!v->is_bool()) {
        fail(at(*v, "\"" + std::string(key) + "\" must be true or false"));
    }
    return v->boolean();
}

// A window size or a ratio: [width, height], both positive.
Size size_of(const Value& p) {
    if (!p.is_array() || p.array().size() != 2) {
        fail(at(p, "a size is [width, height]"));
    }
    return {int_value(p.array()[0], 1, 16384, "width"), int_value(p.array()[1], 1, 16384, "height")};
}

std::vector<std::int32_t> ints_of(const Value& v) {
    if (!v.is_array() || v.array().empty()) {
        fail(at(v, "a list of integers was expected"));
    }
    std::vector<std::int32_t> out;
    for (const Value& x : v.array()) {
        out.push_back(int_value(x, -1000000, 1000000, "a value"));
    }
    return out;
}

LabelTable table_of(const std::string& name, const Value& where) {
    if (name == "none") return LabelTable::None;
    if (name == "quick") return LabelTable::Quick;
    if (name == "quick_menu") return LabelTable::QuickMenu;
    fail(at(where, "a label table is none, quick or quick_menu"));
}

// A label: "text" (in the list's table) or {"text": ..., "table": ...}.
SettingLabel label_of(const Value& v, LabelTable fallback) {
    if (v.is_string()) {
        return {v.string(), fallback};
    }
    reject_unknown(v, {"text", "table"});
    return {string_of(v, "text"), table_of(string_of(v, "table"), v)};
}

SettingRow row_of(const Value& r) {
    reject_unknown(r, {"label", "kind", "setting", "values", "labels", "mobile_values", "min", "max", "step",
                       "realtime", "keep_on_reset"});
    SettingRow row;
    row.label = string_of(r, "label");
    const std::string kind = string_of(r, "kind");
    if (kind == "choice") {
        row.kind = Kind::Choice;
        row.values = ints_of(member(r, "values"));
        if (const Value* labels = r.find("labels")) {
            if (!labels->is_array() || labels->array().size() != row.values.size()) {
                fail(at(*labels, "\"labels\" are parallel to \"values\""));
            }
            for (const Value& l : labels->array()) {
                if (!l.is_string()) {
                    fail(at(l, "a label is a string"));
                }
                row.labels.push_back(l.string());
            }
        }
        if (const Value* mobile = r.find("mobile_values")) {
            row.mobile_values = ints_of(*mobile);
            for (const std::int32_t v : row.mobile_values) {
                if (std::find(row.values.begin(), row.values.end(), v) == row.values.end()) {
                    fail(at(*mobile, "\"mobile_values\" are some of \"values\""));
                }
            }
        }
    } else if (kind == "slider") {
        row.kind = Kind::Slider;
        row.min = real_of(r, "min");
        row.max = real_of(r, "max");
        row.step = real_of(r, "step");
        if (row.max < row.min || row.step <= 0.0f) {
            fail(at(r, "a slider needs min <= max and a positive step"));
        }
    } else if (kind == "resolution") {
        row.kind = Kind::Resolution;
    } else if (kind == "map") {
        row.kind = Kind::Map;
    } else {
        fail(at(member(r, "kind"), "\"kind\" is choice, slider, resolution or map"));
    }
    if (row.kind == Kind::Resolution) {
        row.setting = "resolution";  // the name the engine menu's setting.resolution native finds it by
    } else {
        row.setting = string_of(r, "setting");
    }
    row.realtime = bool_of(r, "realtime", row.kind != Kind::Resolution);
    row.keep_on_reset = bool_of(r, "keep_on_reset", false);
    return row;
}

Format format_of(const std::string& name, const Value& where) {
    static const std::pair<const char*, Format> kFormats[] = {
        {"fixed0", Format::Fixed0}, {"fixed2", Format::Fixed2}, {"fixed3", Format::Fixed3},
        {"percent", Format::Percent}, {"signed_percent", Format::SignedPercent}, {"signed", Format::Signed},
        {"rows", Format::Rows}, {"off_or_fixed2", Format::OffOrFixed2}, {"steps", Format::Steps},
        {"size_square", Format::SizeSquare}, {"labels", Format::Labels},
    };
    for (const auto& [n, f] : kFormats) {
        if (name == n) {
            return f;
        }
    }
    fail(at(where, "unknown format \"" + name + "\""));
}

QuickSetting quick_of(const std::string& name, const Value& q) {
    reject_unknown(q, {"kind", "setting", "labels", "label_table", "fallback", "stored", "thresholds",
                       "default_index", "mask", "wrap", "min", "max", "step", "decimals", "round",
                       "error_if_unchanged", "invert", "format", "default_marker", "suffix", "suffix_setting",
                       "suffix_when_zero"});
    QuickSetting s;
    s.name = name;
    s.setting = string_of(q, "setting");
    const std::string kind = string_of(q, "kind");
    const LabelTable table = q.find("label_table") ? table_of(string_of(q, "label_table"), q) : LabelTable::None;
    if (const Value* labels = q.find("labels")) {
        if (!labels->is_array()) {
            fail(at(*labels, "\"labels\" is a list"));
        }
        for (const Value& l : labels->array()) {
            s.labels.push_back(label_of(l, table));
        }
    }
    if (const Value* f = q.find("fallback")) {
        s.fallback = label_of(*f, table);
    }
    if (const Value* f = q.find("format")) {
        if (!f->is_string()) {
            fail(at(*f, "\"format\" is a string"));
        }
        s.format = format_of(f->string(), *f);
    }
    if (kind == "toggle") {
        s.kind = QKind::Toggle;
        if (s.labels.size() != 2) {
            fail(at(q, "a toggle has two labels"));
        }
    } else if (kind == "cycle") {
        s.kind = QKind::Cycle;
        if (const Value* v = q.find("stored")) s.stored = ints_of(*v);
        if (const Value* v = q.find("thresholds")) s.thresholds = ints_of(*v);
        if (q.find("default_index")) s.default_index = int_of(q, "default_index", 0, 255);
        s.mask = bool_of(q, "mask", false);
        s.wrap = bool_of(q, "wrap", true);
        const std::size_t n = s.stored.empty() ? s.labels.size() : s.stored.size();
        if (n == 0 || (s.format != Format::SizeSquare && s.labels.size() < n) ||
            (!s.thresholds.empty() && s.thresholds.size() + 1 != n) || (s.mask && (n & (n - 1)) != 0) ||
            static_cast<std::size_t>(s.default_index) >= n) {
            fail(at(q, "a cycle needs labels (or stored values) to match its index range"));
        }
    } else if (kind == "float" || kind == "offset" || kind == "volume") {
        s.kind = kind == "float" ? QKind::Float : kind == "offset" ? QKind::Offset : QKind::Volume;
        s.min = q.find("min") ? real_of(q, "min") : 0.0f;
        s.max = real_of(q, "max");
        s.step = real_of(q, "step");
        if (s.max < s.min || s.step <= 0.0f) {
            fail(at(q, "needs min <= max and a positive step"));
        }
        if (q.find("decimals")) s.decimals = int_of(q, "decimals", 2, 3);
        s.round = bool_of(q, "round", false);
        s.error_if_unchanged = bool_of(q, "error_if_unchanged", false);
        s.invert = bool_of(q, "invert", false);
        if (const Value* m = q.find("default_marker")) {
            if (!m->is_array() || m->array().size() != 2 || !m->array()[0].is_number() ||
                !m->array()[1].is_number()) {
                fail(at(*m, "\"default_marker\" is [low, high]"));
            }
            s.default_marker = std::make_pair(static_cast<float>(m->array()[0].number()),
                                              static_cast<float>(m->array()[1].number()));
        }
    } else {
        fail(at(member(q, "kind"), "\"kind\" is toggle, cycle, float, offset or volume"));
    }
    if (const Value* suffix = q.find("suffix")) {
        s.suffix = label_of(*suffix, LabelTable::Quick);
        s.suffix_setting = string_of(q, "suffix_setting");
        s.suffix_when_zero = bool_of(q, "suffix_when_zero", true);
    }
    return s;
}

}  // namespace

void keep_mobile_values(SettingsData& data) {
    for (SettingRow& row : data.rows) {
        if (row.mobile_values.empty()) continue;
        std::vector<std::int32_t> values;
        std::vector<std::string> labels;
        for (std::size_t i = 0; i < row.values.size(); ++i) {
            if (std::find(row.mobile_values.begin(), row.mobile_values.end(), row.values[i]) == row.mobile_values.end())
                continue;
            values.push_back(row.values[i]);
            if (i < row.labels.size()) labels.push_back(row.labels[i]);
        }
        row.values = std::move(values);
        row.labels = std::move(labels);
    }
}

Result<SettingsData> parse_settings(std::string_view json_text) try {
    const Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<SettingsData>::fail(doc.error());
    }
    const Value& root = doc.value();
    reject_unknown(root, {"reset_done", "restart_notice", "default_map", "resolutions", "aspects", "rows", "quick"});
    SettingsData l;
    l.reset_done = string_of(root, "reset_done");
    l.restart_notice = string_of(root, "restart_notice");
    l.default_map = string_of(root, "default_map");

    const Value& res = member(root, "resolutions");
    if (!res.is_array() || res.array().empty()) {
        fail(at(res, "\"resolutions\" lists [width, height] pairs"));
    }
    for (const Value& r : res.array()) {
        l.resolutions.push_back(size_of(r));
    }
    if (const Value* aspects = root.find("aspects")) {
        if (!aspects->is_array()) {
            fail(at(*aspects, "\"aspects\" lists [width, height] ratios"));
        }
        for (const Value& a : aspects->array()) {
            l.aspects.push_back(size_of(a));
        }
    }

    const Value& rows = member(root, "rows");
    if (!rows.is_array() || rows.array().empty()) {
        fail(at(rows, "\"rows\" must list at least one setting"));
    }
    for (const Value& r : rows.array()) {
        l.rows.push_back(row_of(r));
    }

    const Value& quick = member(root, "quick");
    if (!quick.is_object()) {
        fail(at(quick, "\"quick\" maps names to settings"));
    }
    for (const auto& [name, q] : quick.members()) {
        if (name.rfind("_", 0) == 0) {
            continue;
        }
        l.quick.push_back(quick_of(name, q));
    }

    return Result<SettingsData>::ok(std::move(l));
} catch (const std::exception& e) {
    return Result<SettingsData>::fail(e.what());
}

Result<SettingsData> load_settings(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                   std::string* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("menu") / "settings.json";
    // Base + every active mod's patch, in load order.
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!read) {
        return Result<SettingsData>::fail(read.error());
    }
    const std::string& text = read.value();
    Result<SettingsData> parsed = parse_settings(text);
    if (!parsed) {
        return Result<SettingsData>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::screens
