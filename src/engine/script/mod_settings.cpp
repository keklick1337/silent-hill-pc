// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/script/mod_settings.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace sh::script {

namespace {

namespace fs = std::filesystem;
using sh::menu::Value;

bool key_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return std::string(s.substr(a, b - a));
}

// `key = value` -> key and the raw value text; false for comments, blanks and junk.
bool split_line(const std::string& line, std::string& key, std::string& value) {
    const std::string t = trim(line);
    if (t.empty() || t[0] == '#' || t[0] == ';') return false;
    const auto eq = t.find('=');
    if (eq == std::string::npos) return false;
    key = trim(std::string_view(t).substr(0, eq));
    value = trim(std::string_view(t).substr(eq + 1));
    return !key.empty();
}

std::string unquote(const std::string& s) {
    if (s.size() < 2 || s.front() != '"' || s.back() != '"') return s;
    std::string out;
    for (std::size_t i = 1; i + 1 < s.size(); ++i) {
        if (s[i] == '\\' && i + 2 < s.size()) {
            const char n = s[++i];
            out += n == 'n' ? '\n' : n;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string quote_if_needed(const std::string& s) {
    bool plain = !s.empty() && s.front() != ' ' && s.back() != ' ' && s.front() != '"';
    for (char c : s) {
        if (static_cast<unsigned char>(c) < 0x20) plain = false;
    }
    if (plain) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else {
            out += c;
        }
    }
    return out + "\"";
}

bool parse_text(SettingType type, const std::string& text, Value& out) {
    switch (type) {
        case SettingType::Number: {
            if (text.empty()) return false;
            char* end = nullptr;
            const double d = std::strtod(text.c_str(), &end);
            if (end == nullptr || *end != '\0' || !std::isfinite(d)) return false;
            out = Value{d};
            return true;
        }
        case SettingType::Bool:
            if (text == "1" || text == "true" || text == "yes" || text == "on") {
                out = Value{true};
                return true;
            }
            if (text == "0" || text == "false" || text == "no" || text == "off") {
                out = Value{false};
                return true;
            }
            return false;
        case SettingType::Choice:
        case SettingType::String: out = Value{unquote(text)}; return true;
    }
    return false;
}

const char* type_name(SettingType t) {
    switch (t) {
        case SettingType::Number: return "a number";
        case SettingType::Bool: return "a boolean";
        case SettingType::Choice: return "one of its choices";
        case SettingType::String: return "a string";
    }
    return "?";
}

}  // namespace

std::string setting_text(const Value& v) {
    if (const auto* b = std::get_if<bool>(&v)) return *b ? "true" : "false";
    if (const auto* i = std::get_if<std::int64_t>(&v)) return std::to_string(*i);
    if (const auto* d = std::get_if<double>(&v)) {
        char buf[64];
        const auto r = std::to_chars(buf, buf + sizeof buf, *d);  // shortest text that reads back exactly
        return std::string(buf, r.ptr);
    }
    if (const auto* s = std::get_if<std::string>(&v)) return quote_if_needed(*s);
    return {};
}

bool ModSettings::valid_key(std::string_view key) {
    bool dot = false;
    bool part = false;  // the current part has a character
    for (char c : key) {
        if (c == '.') {
            if (!part) return false;
            dot = true;
            part = false;
        } else if (key_char(c)) {
            part = true;
        } else {
            return false;
        }
    }
    return dot && part;
}

bool ModSettings::load(const fs::path& file, std::string& err) {
    path_ = file;
    lines_.clear();
    stored_.clear();
    if (file.empty()) return true;
    std::error_code ec;
    if (!fs::exists(file, ec)) return true;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        err = file.generic_string() + ": cannot read";
        return false;
    }
    std::string line, key, value;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines_.push_back(line);
        if (split_line(line, key, value)) stored_[key] = value;  // a later duplicate wins
    }
    // A key declared before the file was read takes the saved value now.
    for (auto& [k, e] : entries_) {
        const auto it = stored_.find(k);
        Value parsed, norm;
        std::string ignored;
        if (it != stored_.end() && parse_text(e.decl.type, it->second, parsed) &&
            normalize(e.decl, parsed, norm, ignored)) {
            e.value = e.boot = norm;
        }
    }
    return true;
}

bool ModSettings::normalize(const SettingDecl& d, const Value& in, Value& out, std::string& err) const {
    switch (d.type) {
        case SettingType::Number: {
            double v = 0.0;
            if (const auto* i = std::get_if<std::int64_t>(&in)) {
                v = static_cast<double>(*i);
            } else if (const auto* f = std::get_if<double>(&in)) {
                v = *f;
            } else {
                break;
            }
            if (!std::isfinite(v)) break;
            // Slider steps leave float dust (0.5 + 0.1 = 0.6000000000000001); the file should say 0.6.
            if (std::fabs(v) < 1e9) v = std::round(v * 1e9) / 1e9;
            if (v < d.min || v > d.max) {
                err = d.key + ": " + setting_text(Value{v}) + " is outside " + setting_text(Value{d.min}) + ".." +
                      setting_text(Value{d.max});
                return false;
            }
            out = Value{v};
            return true;
        }
        case SettingType::Bool:
            if (const auto* b = std::get_if<bool>(&in)) {
                out = Value{*b};
                return true;
            }
            break;
        case SettingType::Choice: {
            if (std::holds_alternative<std::monostate>(in) || std::holds_alternative<bool>(in)) break;
            const std::string s = sh::menu::as_string(in);
            for (const auto& c : d.choices) {
                if (c.value == s) {
                    out = Value{s};
                    return true;
                }
            }
            err = d.key + ": '" + s + "' is not one of its choices";
            return false;
        }
        case SettingType::String:
            if (const auto* s = std::get_if<std::string>(&in)) {
                out = Value{*s};
                return true;
            }
            if (std::holds_alternative<std::int64_t>(in) || std::holds_alternative<double>(in)) {
                out = Value{sh::menu::as_string(in)};
                return true;
            }
            break;
    }
    err = d.key + ": the value must be " + type_name(d.type);
    return false;
}

bool ModSettings::declare(SettingDecl d, std::string& err) {
    if (!valid_key(d.key)) {
        err = "'" + d.key + "' is not a mod setting key (want <name>.<key>: letters, digits, _ and dots)";
        return false;
    }
    if (d.type == SettingType::Number && !(d.min <= d.max)) {
        err = d.key + ": min is greater than max";
        return false;
    }
    if (d.type == SettingType::Choice && d.choices.empty()) {
        err = d.key + ": a choice setting needs choices";
        return false;
    }
    if (d.step < 0 || !std::isfinite(d.step)) d.step = 0.0;
    if (std::holds_alternative<std::monostate>(d.def)) {
        switch (d.type) {
            case SettingType::Number:
                d.def = Value{std::isfinite(d.min) ? d.min : (0.0 > d.max ? d.max : 0.0)};
                break;
            case SettingType::Bool: d.def = Value{false}; break;
            case SettingType::Choice: d.def = Value{d.choices.front().value}; break;
            case SettingType::String: d.def = Value{std::string()}; break;
        }
    }
    Value def;
    if (!normalize(d, d.def, def, err)) {
        err = "default of " + err;
        return false;
    }
    d.def = def;

    Value value = def;
    const auto prev = entries_.find(d.key);
    const auto saved = stored_.find(d.key);
    Value parsed, norm;
    std::string ignored;
    if (prev != entries_.end() && normalize(d, prev->second.value, norm, ignored)) {
        value = norm;  // a re-declare keeps the current value when it still fits
    } else if (saved != stored_.end() && parse_text(d.type, saved->second, parsed) &&
               normalize(d, parsed, norm, ignored)) {
        value = norm;
    }

    if (prev != entries_.end()) {
        prev->second.decl = std::move(d);
        prev->second.value = value;
        return true;
    }
    Entry e;
    e.value = value;
    e.boot = value;
    const std::string key = d.key;
    e.decl = std::move(d);
    entries_.emplace(key, std::move(e));
    return true;
}

const SettingDecl* ModSettings::find(std::string_view key) const {
    const auto it = entries_.find(key);
    return it == entries_.end() ? nullptr : &it->second.decl;
}

std::optional<Value> ModSettings::get(std::string_view key) const {
    const auto it = entries_.find(key);
    if (it == entries_.end()) return std::nullopt;
    return it->second.value;
}

bool ModSettings::set(std::string_view key, const Value& v, std::string& err) {
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        err = "'" + std::string(key) + "' is not a declared setting";
        return false;
    }
    Entry& e = it->second;
    Value norm;
    if (!normalize(e.decl, v, norm, err)) return false;
    if (norm == e.value) return true;
    e.value = norm;
    stored_[std::string(key)] = setting_text(norm);
    if (!save(err)) {
        err = "cannot save: " + err;  // the value is still applied for this run
    }
    if (notifying_.insert(std::string(key)).second) {
        // Copied: a listener may add listeners.
        const std::vector<Listener> listeners = e.listeners;
        for (const auto& fn : listeners) {
            if (fn) fn(norm);
        }
        notifying_.erase(notifying_.find(key));
    }
    return err.empty();
}

bool ModSettings::reset(std::string_view key, std::string& err) {
    const SettingDecl* d = find(key);
    if (d == nullptr) {
        err = "'" + std::string(key) + "' is not a declared setting";
        return false;
    }
    const Value def = d->def;
    return set(key, def, err);
}

bool ModSettings::on_change(std::string_view key, Listener fn, std::string& err) {
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        err = "'" + std::string(key) + "' is not a declared setting";
        return false;
    }
    it->second.listeners.push_back(std::move(fn));
    return true;
}

bool ModSettings::needs_restart() const {
    for (const auto& [k, e] : entries_) {
        if (e.decl.restart && e.value != e.boot) return true;
    }
    return false;
}

bool ModSettings::save(std::string& err) const {
    if (path_.empty()) return true;
    std::ostringstream out;
    std::set<std::string, std::less<>> written;
    if (lines_.empty()) out << "# Mod settings (sh.config), written by the game. One `key = value` per line.\n";
    std::string key, value;
    for (const std::string& line : lines_) {
        if (!split_line(line, key, value)) {
            out << line << '\n';
            continue;
        }
        const auto it = stored_.find(key);
        if (it == stored_.end() || !written.insert(key).second) continue;  // duplicates collapse into the first
        out << key << " = " << it->second << '\n';
    }
    for (const auto& [k, v] : stored_) {
        if (!written.contains(k)) out << k << " = " << v << '\n';
    }
    std::error_code ec;
    if (path_.has_parent_path()) fs::create_directories(path_.parent_path(), ec);
    fs::path tmp = path_;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            err = tmp.generic_string() + ": cannot write";
            return false;
        }
        f << out.str();
        if (!f.flush()) {
            err = tmp.generic_string() + ": write failed";
            return false;
        }
    }
    // Written aside and renamed so a crash mid-write never leaves a half file.
    fs::rename(tmp, path_, ec);
    if (ec) {
        err = path_.generic_string() + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

void ModSettings::clear() {
    path_.clear();
    lines_.clear();
    stored_.clear();
    entries_.clear();
    notifying_.clear();
}

ModSettings& mod_settings() {
    static ModSettings store;
    return store;
}

void set_mod_config_path(const fs::path& file) {
    std::string err;
    if (!mod_settings().load(file, err)) std::fprintf(stderr, "[config] %s\n", err.c_str());
}

}  // namespace sh::script
