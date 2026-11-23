// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "input/bind/bind_table.h"

#include "engine/log/sh_log.h"

#include <algorithm>
#include <cctype>

namespace sh::input {
namespace {

bool same_ignoring_case(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// The four walking actions are the stick's on a controller, and the screen names them by direction.
std::string_view row_label(Action action) {
    switch (action) {
        case Action::MoveForward: return "Move Up";
        case Action::MoveBack: return "Move Down";
        default: return info(action).label;
    }
}

bool on_stick(Action action) {
    return action == Action::MoveForward || action == Action::MoveBack || action == Action::TurnLeft ||
           action == Action::TurnRight;
}

std::vector<BindRow> build_rows() {
    std::vector<BindRow> rows;
    rows.reserve(kActionCount);
    for (int i = 0; i < kActionCount; ++i) {
        const Action action = static_cast<Action>(i);
        const ActionInfo& a = info(action);
        BindRow r;
        r.action = action;
        r.label = row_label(action);
        r.slot[0] = "action_" + std::string(a.name);
        if ((a.flags & kFlagSingleSlot) == 0) {
            r.slot[1] = r.slot[0] + "_2";
        }
        r.slot[2] = a.pad_key;
        r.slot[3] = a.pad_key2;
        r.stick = on_stick(action);
        r.alt_camera_only = (a.flags & kFlagAltcamOnly) != 0;
        r.shared = (a.flags & kFlagGlobal) != 0;
        r.mouse = (a.flags & kFlagMouse) != 0;
        r.wheel = (a.flags & kFlagWheel) != 0;
        rows.push_back(std::move(r));
    }
    return rows;
}

const char* scheme_name(BindScheme s) { return s == BindScheme::AltCamera ? "altcam" : "classic"; }

}  // namespace

std::span<const BindRow> BindTable::rows() {
    static const std::vector<BindRow> table = build_rows();
    return table;
}

const BindRow* BindTable::row(int index) {
    const auto all = rows();
    return index >= 0 && index < static_cast<int>(all.size()) ? &all[static_cast<std::size_t>(index)] : nullptr;
}

std::string BindTable::value(int row_index, BindColumn column) const {
    const BindRow* r = row(row_index);
    if (r == nullptr || !r->has(column)) {
        return {};
    }
    return store_.value(r->key(column), scheme_).value_or(std::string());
}

bool BindTable::row_enabled(int row_index) const {
    const BindRow* r = row(row_index);
    // Past the table there is nothing to switch off (the screen's own rows under the binds).
    if (r == nullptr) {
        return row_index >= row_count();
    }
    return !(r->alt_camera_only && scheme_ == BindScheme::Classic);
}

bool BindTable::cell_usable(int row_index, BindColumn column) const {
    const BindRow* r = row(row_index);
    return r != nullptr && r->has(column) && row_enabled(row_index);
}

bool BindTable::is_duplicate(int row_index, BindColumn column) const {
    const std::string mine = value(row_index, column);
    if (is_unbound(mine)) {
        return false;
    }
    const bool pad = is_pad_column(column);
    const std::array<BindColumn, 2> device = pad ? std::array{BindColumn::Pad1, BindColumn::Pad2}
                                                 : std::array{BindColumn::Key1, BindColumn::Key2};
    for (int r = 0; r < row_count(); ++r) {
        if (!row_enabled(r)) {
            continue;
        }
        for (const BindColumn c : device) {
            if (r == row_index && c == column) {
                continue;
            }
            const std::string other = value(r, c);
            if (!is_unbound(other) && same_ignoring_case(other, mine)) {
                return true;
            }
        }
    }
    return false;
}

std::string BindTable::config_key(std::string_view key) const {
    std::string out(key);
    if (scheme_ == BindScheme::AltCamera && store_.per_scheme(key)) {
        out += "_altcam";
    }
    return out;
}

void BindTable::set(int row_index, BindColumn column, std::string_view new_value) {
    const BindRow* r = row(row_index);
    if (r == nullptr || !r->has(column)) {
        return;
    }
    const std::string& key = r->key(column);
    if (!store_.assign(key, scheme_, new_value)) {
        return;
    }
    const std::string stored = store_.value(key, scheme_).value_or(std::string());
    if (store_.save_action(key, scheme_)) {
        SH_DBG("[BINDS] %s (%s) = %s", key.c_str(), scheme_name(scheme_), stored.c_str());
    } else {
        const BindLine line{config_key(key), stored};
        store_.save_line(line);
        SH_DBG("[BINDS] %s = %s", line.key.c_str(), line.value.c_str());
    }
    store_.changed();
}

void BindTable::reset_scheme() {
    std::vector<BindLine> lines;
    for (const BindRow& r : rows()) {
        if (r.shared) {
            continue;
        }
        // All of the row first: an action's line carries both of its slots.
        for (int c = 0; c < kBindColumns; ++c) {
            const BindColumn column = static_cast<BindColumn>(c);
            if (!r.has(column)) {
                continue;
            }
            const std::string def = store_.built_in(r.key(column), scheme_);
            store_.assign(r.key(column), scheme_, is_unbound(def) ? std::string_view("NONE") : std::string_view(def));
        }
        for (int c = 0; c < kBindColumns; ++c) {
            const BindColumn column = static_cast<BindColumn>(c);
            if (!r.has(column) || column == BindColumn::Key2) {
                continue;
            }
            const std::string& key = r.key(column);
            const std::optional<std::string> now = store_.value(key, scheme_);
            if (!now) {
                continue;
            }
            if (std::optional<BindLine> action = store_.action_line(key, scheme_)) {
                lines.push_back(std::move(*action));
            } else {
                lines.push_back({config_key(key), *now});
            }
        }
    }
    store_.save_lines(lines);
    store_.actions_assigned();
    SH_DBG("[BINDS] %s scheme reset to defaults (%d binds)", scheme_name(scheme_), static_cast<int>(lines.size()));
    store_.changed();
}

std::string BindTable::rebind_toast(int row_index, BindColumn column) const {
    const BindRow* r = row(row_index);
    std::string text = "Changing ";
    text += is_secondary_column(column) ? "Secondary" : "Primary";
    text += " input for ";
    if (r != nullptr) {
        text += r->label;
    }
    return text;
}

std::string bind_display_name(std::string_view value) {
    struct Rename {
        std::string_view from, to;
    };
    static constexpr Rename kMouse[] = {
        {"Mouse1", "L-Mouse"},
        {"Mouse2", "R-Mouse"},
        {"Mouse3", "M-Mouse"},
        {"MouseWheelUp", "Wheel Up"},
        {"MouseWheelDown", "Wheel Down"},
    };
    static constexpr Rename kSides[] = {{"Left ", "L-"}, {"Right ", "R-"}, {"Keypad ", "KP "}};

    for (const Rename& m : kMouse) {
        if (same_ignoring_case(value, m.from)) {
            return std::string(m.to);
        }
    }
    for (const Rename& s : kSides) {
        if (value.size() > s.from.size() && value.starts_with(s.from)) {
            return std::string(s.to) + std::string(value.substr(s.from.size()));
        }
    }
    return std::string(value);
}

}  // namespace sh::input
