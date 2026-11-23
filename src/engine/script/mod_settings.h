// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Mod settings store behind sh.config: mods declare
// typed settings with namespaced keys (`mymod.speed`), menus and scripts read and write them,
// and every change is saved to mods_config.cfg next to config.cfg (`key = value` lines).
// Game settings stay in config.cfg: a key without a dot is refused.
#pragma once

#include "engine/menu/menu_model.h"

#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sh::script {

enum class SettingType { Number, Bool, Choice, String };

struct SettingDecl {
    std::string key;
    SettingType type = SettingType::Number;
    sh::menu::Value def;  // empty: 0 / min, false, the first choice, ""
    double min = -std::numeric_limits<double>::infinity();
    double max = std::numeric_limits<double>::infinity();
    double step = 0.0;  // 0: none given (a bound slider uses its own default)
    std::vector<sh::menu::Choice> choices;
    bool restart = false;  // a change takes effect after a restart: needs_restart() reports it
};

class ModSettings {
public:
    using Listener = std::function<void(const sh::menu::Value&)>;

    // `<name>.<key>`: letters, digits and _, one or more dots, no empty part.
    static bool valid_key(std::string_view key);

    // Reads `file` (a missing one is an empty store) and saves there from now on. Values of keys
    // not declared yet are kept as text and applied when declared. An empty path: memory only.
    bool load(const std::filesystem::path& file, std::string& err);
    const std::filesystem::path& path() const { return path_; }

    // Declares (or re-declares) a setting. The value saved in the file wins over the default
    // when it fits the declaration.
    bool declare(SettingDecl decl, std::string& err);
    const SettingDecl* find(std::string_view key) const;

    std::optional<sh::menu::Value> get(std::string_view key) const;
    // Validates against the declaration, stores, saves the file and calls the listeners when the
    // value changed.
    bool set(std::string_view key, const sh::menu::Value& v, std::string& err);
    bool reset(std::string_view key, std::string& err);
    bool on_change(std::string_view key, Listener fn, std::string& err);
    // True when a restart=true setting differs from the value it had when declared.
    bool needs_restart() const;

    bool save(std::string& err) const;
    void clear();  // forgets everything, the path included

private:
    struct Entry {
        SettingDecl decl;
        sh::menu::Value value;
        sh::menu::Value boot;  // the value when first declared (needs_restart)
        std::vector<Listener> listeners;
    };
    bool normalize(const SettingDecl& d, const sh::menu::Value& in, sh::menu::Value& out, std::string& err) const;

    std::filesystem::path path_;
    std::vector<std::string> lines_;                          // the file as read, for comments and order
    std::map<std::string, std::string, std::less<>> stored_;  // key -> text to write
    std::map<std::string, Entry, std::less<>> entries_;
    std::set<std::string, std::less<>> notifying_;            // a listener setting its own key is not re-notified
};

// The process-wide store sh.config uses.
ModSettings& mod_settings();

// Loads the process-wide store from `file` (mods_config.cfg next to config.cfg); call before the
// menu scripts run. Errors go to stderr; the store then starts empty but still saves to `file`.
void set_mod_config_path(const std::filesystem::path& file);

// Value <-> the text in the file.
std::string setting_text(const sh::menu::Value& v);

}  // namespace sh::script
