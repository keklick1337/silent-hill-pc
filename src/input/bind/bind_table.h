// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The table the Controls screen edits: one row per input action, four cells per row (two keyboard/mouse slots,
// two controller slots). The rows come from the action registry (engine/input/input_actions.h); the values live
// behind a BindStore, which in the game is config.cfg's bind fields (bind_store_host.cpp), so the table itself
// needs neither SDL nor the game and is tested with a store in memory.
//
// What is written is what a player can type by hand: a key by its scancode name, the mouse as Mouse1..Mouse5 /
// MouseWheelUp / MouseWheelDown, a controller input by its button name, NONE for nothing, and the alternate
// cameras' copy under the same key with "_altcam".

#pragma once

#include "engine/input/input_actions.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::input {

enum class BindColumn : int { Key1 = 0, Key2, Pad1, Pad2 };
inline constexpr int kBindColumns = 4;

constexpr bool is_pad_column(BindColumn c) { return c == BindColumn::Pad1 || c == BindColumn::Pad2; }
constexpr bool is_secondary_column(BindColumn c) { return c == BindColumn::Key2 || c == BindColumn::Pad2; }

// Which set of binds is edited: the original camera's, or the one the alternate cameras share.
enum class BindScheme : int { Classic = 0, AltCamera = 1 };

struct BindRow {
    Action action = Action::Count;
    std::string label;                           // English row label, also the source of its text key
    std::array<std::string, kBindColumns> slot;  // config key of each cell, "" where the row has no such cell
    bool stick = false;            // the controller side is the stick and the d-pad, not a bind
    bool alt_camera_only = false;  // does something only with an alternate camera
    bool shared = false;           // one value for both schemes
    bool mouse = false;            // the keyboard cells take mouse buttons
    bool wheel = false;            // ...and the wheel
    bool chord = false;            // the controller cells take two buttons together ("a+b")

    bool has(BindColumn c) const { return !slot[static_cast<std::size_t>(c)].empty(); }
    const std::string& key(BindColumn c) const { return slot[static_cast<std::size_t>(c)]; }
};

// One `key = value` line of config.cfg.
struct BindLine {
    std::string key;
    std::string value;

    bool operator==(const BindLine&) const = default;
};

// Where the values are kept and written.
class BindStore {
public:
    virtual ~BindStore() = default;
    // The value behind a cell's config key, nullopt when the key names no bind.
    virtual std::optional<std::string> value(std::string_view key, BindScheme scheme) const = 0;
    virtual std::string built_in(std::string_view key, BindScheme scheme) const = 0;
    // True when the alternate cameras keep their own copy of this key.
    virtual bool per_scheme(std::string_view key) const = 0;
    // Changes the value in memory only. False when the key names no bind.
    virtual bool assign(std::string_view key, BindScheme scheme, std::string_view value) = 0;
    // The config line of the action a keyboard cell belongs to (both of its slots); nullopt for other keys.
    virtual std::optional<BindLine> action_line(std::string_view key, BindScheme scheme) const = 0;
    // Writes the line of the action a keyboard cell belongs to. False, and nothing written, for other keys.
    virtual bool save_action(std::string_view key, BindScheme scheme) = 0;
    // Writes one line, or several in one pass over the file.
    virtual void save_line(const BindLine& line) = 0;
    virtual void save_lines(std::span<const BindLine> lines) = 0;
    // The actions' values changed without save_action: whatever mirrors them is brought up to date.
    virtual void actions_assigned() = 0;
    // Called after any change: whoever resolved a bind and kept the result resolves it again.
    virtual void changed() = 0;
};

class BindTable {
public:
    explicit BindTable(BindStore& store) : store_(store) {}

    static std::span<const BindRow> rows();
    static const BindRow* row(int index);
    static int row_count() { return static_cast<int>(rows().size()); }

    void set_scheme(BindScheme scheme) { scheme_ = scheme; }
    BindScheme scheme() const { return scheme_; }

    // "" for a cell the row does not have.
    std::string value(int row, BindColumn column) const;
    bool row_enabled(int row) const;
    bool cell_usable(int row, BindColumn column) const;
    // The same input on another cell of this scheme and device: allowed, but shown.
    bool is_duplicate(int row, BindColumn column) const;

    // Sets one cell and writes its line.
    void set(int row, BindColumn column, std::string_view value);
    // Every row of this scheme back to its built-in binds, written at once; shared rows stay.
    void reset_scheme();

    std::string rebind_toast(int row, BindColumn column) const;

private:
    std::string config_key(std::string_view key) const;

    BindStore& store_;
    BindScheme scheme_ = BindScheme::Classic;
};

// Short on-screen name of a keyboard cell's value ("Left Shift" -> "L-Shift", "Mouse1" -> "L-Mouse").
std::string bind_display_name(std::string_view value);

// The game's table over config.cfg (bind_store_host.cpp).
BindTable& bind_table();

}  // namespace sh::input
