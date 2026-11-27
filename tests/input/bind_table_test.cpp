// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The Controls screen's table (src/input/bind/bind_table.h) over a store in memory that behaves as the game's:
// 31-character fields, the actions' own config lines, separate values for the alternate cameras.

#include "framework/sh_test.h"

#include "input/bind/bind_table.h"

#include <map>
#include <string>
#include <vector>

using namespace sh::input;

namespace {

// Controller binds of the game's configuration that are not per scheme.
bool shared_pad_key(std::string_view key) {
    return key == "pad_swap_shoulder" || key == "pad_debug_menu" || key == "pad_quicksave" || key == "pad_quickload";
}

struct SlotRef {
    Action action;
    int slot;
};

std::optional<SlotRef> action_slot(std::string_view key) {
    if (!key.starts_with("action_")) {
        return std::nullopt;
    }
    key.remove_prefix(7);
    int slot = 0;
    if (key.ends_with("_2")) {
        key.remove_suffix(2);
        slot = 1;
    }
    const auto a = find_action(key);
    if (!a || (slot == 1 && (info(*a).flags & kFlagSingleSlot) != 0)) {
        return std::nullopt;
    }
    return SlotRef{*a, slot};
}

class MemoryStore final : public BindStore {
public:
    MemoryStore() {
        for (const BindRow& r : BindTable::rows()) {
            for (int c = 2; c < kBindColumns; ++c) {
                if (!r.slot[c].empty()) {
                    pad_defaults_[r.slot[c]] = "pad:" + r.slot[c];
                }
            }
        }
    }

    std::optional<std::string> value(std::string_view key, BindScheme scheme) const override {
        if (!known(key)) {
            return std::nullopt;
        }
        const auto it = values_.find(storage_key(key, scheme));
        return it != values_.end() ? it->second : built_in(key, scheme);
    }
    std::string built_in(std::string_view key, BindScheme scheme) const override {
        if (const auto s = action_slot(key)) {
            return default_binding(s->action, static_cast<int>(scheme))[static_cast<std::size_t>(s->slot)];
        }
        const auto it = pad_defaults_.find(std::string(key));
        return it != pad_defaults_.end() ? it->second : std::string();
    }
    bool per_scheme(std::string_view key) const override {
        if (const auto s = action_slot(key)) {
            return (info(s->action).flags & kFlagGlobal) == 0;
        }
        return known(key) && !shared_pad_key(key);
    }
    bool assign(std::string_view key, BindScheme scheme, std::string_view v) override {
        if (!known(key)) {
            return false;
        }
        values_[storage_key(key, scheme)] = std::string(v.substr(0, 31));
        return true;
    }
    std::optional<BindLine> action_line(std::string_view key, BindScheme scheme) const override {
        const auto s = action_slot(key);
        if (!s) {
            return std::nullopt;
        }
        const std::string name = "action_" + std::string(info(s->action).name);
        Binding b;
        b[0] = *value(name, scheme);
        b[1] = (info(s->action).flags & kFlagSingleSlot) != 0 ? "NONE" : *value(name + "_2", scheme);
        return BindLine{config_key(s->action, static_cast<int>(scheme)), format_binding(b)};
    }
    bool save_action(std::string_view key, BindScheme scheme) override {
        const auto line = action_line(key, scheme);
        if (line) {
            written.push_back(*line);
        }
        return line.has_value();
    }
    void save_line(const BindLine& line) override { written.push_back(line); }
    void save_lines(std::span<const BindLine> lines) override {
        ++batches;
        written.insert(written.end(), lines.begin(), lines.end());
    }
    void actions_assigned() override { ++projections; }
    void changed() override { ++changes; }

    std::vector<BindLine> written;
    int batches = 0;
    int projections = 0;
    int changes = 0;

private:
    bool known(std::string_view key) const {
        return action_slot(key).has_value() || pad_defaults_.count(std::string(key)) != 0;
    }
    std::string storage_key(std::string_view key, BindScheme scheme) const {
        return std::string(key) + (scheme == BindScheme::AltCamera && per_scheme(key) ? "@alt" : "");
    }

    std::map<std::string, std::string> values_;
    std::map<std::string, std::string> pad_defaults_;
};

int row_of(std::string_view label) {
    for (int i = 0; i < BindTable::row_count(); ++i) {
        if (BindTable::row(i)->label == label) {
            return i;
        }
    }
    return -1;
}

}  // namespace

SH_TEST(bind_table_rows_are_the_actions_in_order) {
    // label | key 1 | key 2 | pad 1 | pad 2 | stick, alt camera only, shared, mouse, wheel
    struct Want {
        const char* label;
        const char* k1;
        const char* k2;
        const char* p1;
        const char* p2;
        const char* flags;
    };
    const Want want[] = {
        {"Move Up", "action_move_forward", "action_move_forward_2", "", "", "S..MW"},
        {"Move Down", "action_move_back", "action_move_back_2", "", "", "S..MW"},
        {"Turn Left", "action_turn_left", "action_turn_left_2", "", "", "S..MW"},
        {"Turn Right", "action_turn_right", "action_turn_right_2", "", "", "S..MW"},
        {"Sidestep Left", "action_step_left", "action_step_left_2", "pad_l1", "pad_l1_2", "...MW"},
        {"Sidestep Right", "action_step_right", "action_step_right_2", "pad_r1", "pad_r1_2", "...MW"},
        {"Run", "action_run", "action_run_2", "pad_square", "pad_square_2", "...MW"},
        {"Action", "action_interact", "action_interact_2", "pad_cross", "pad_cross_2", "...MW"},
        {"Attack", "action_attack", "action_attack_2", "", "", "...MW"},
        {"Aim", "action_aim", "action_aim_2", "pad_r2", "pad_r2_2", "...MW"},
        {"View", "action_view", "action_view_2", "pad_l2", "pad_l2_2", "...MW"},
        {"Back / Cancel", "action_back", "action_back_2", "pad_circle", "pad_circle_2", "...MW"},
        {"Confirm / Skip", "action_confirm", "action_confirm_2", "", "", "...MW"},
        {"Inventory", "action_inventory", "action_inventory_2", "pad_select", "pad_select_2", "...MW"},
        {"Map", "action_map", "action_map_2", "pad_triangle", "pad_triangle_2", "...MW"},
        {"Pause", "action_pause", "action_pause_2", "pad_start", "pad_start_2", "...MW"},
        {"Flashlight", "action_flashlight", "", "pad_flashlight", "", "....."},
        {"Reload", "action_reload", "action_reload_2", "pad_reload", "", "....."},
        {"Quick Turn", "action_quick_turn", "", "pad_quick_turn", "", "....."},
        {"Rear Look", "action_rear_look", "", "pad_rear_look", "", ".A..."},
        {"Cycle Weapons", "action_cycle_weapons", "", "pad_cycle_weapons", "", "....."},
        {"Quick Heal", "action_quick_heal", "", "pad_quick_heal", "", "....."},
        {"Change Camera", "action_change_camera", "", "pad_change_cam", "", "....."},
        {"Swap Shoulder", "action_swap_shoulder", "", "pad_swap_shoulder", "", "..GM."},
        {"Debug Options", "action_debug_menu", "", "pad_debug_menu", "", "..G.."},
        {"Quick Save", "action_quick_save", "", "pad_quicksave", "", "..G.."},
        {"Quick Load", "action_quick_load", "", "pad_quickload", "", "..G.."},
    };
    SH_CHECK(BindTable::row_count() == static_cast<int>(std::size(want)));
    for (int i = 0; i < BindTable::row_count() && i < static_cast<int>(std::size(want)); ++i) {
        const BindRow& r = *BindTable::row(i);
        const Want& w = want[i];
        SH_CHECK(r.label == w.label);
        SH_CHECK(r.slot[0] == w.k1 && r.slot[1] == w.k2 && r.slot[2] == w.p1 && r.slot[3] == w.p2);
        const std::string flags = {r.stick ? 'S' : '.', r.alt_camera_only ? 'A' : '.', r.shared ? 'G' : '.',
                                   r.mouse ? 'M' : '.', r.wheel ? 'W' : '.'};
        SH_CHECK(flags == w.flags);
        SH_CHECK(static_cast<int>(r.action) == i);
    }
    SH_CHECK(BindTable::row(-1) == nullptr && BindTable::row(BindTable::row_count()) == nullptr);
}

SH_TEST(bind_table_default_values_per_scheme) {
    MemoryStore store;
    BindTable t(store);
    SH_CHECK(t.value(row_of("Move Up"), BindColumn::Key1) == "Up");
    SH_CHECK(t.value(row_of("Move Up"), BindColumn::Key2) == "W");
    SH_CHECK(t.value(row_of("Sidestep Left"), BindColumn::Key1) == "Q");
    SH_CHECK(t.value(row_of("Flashlight"), BindColumn::Key2).empty());  // no such cell
    SH_CHECK(t.value(row_of("Move Up"), BindColumn::Pad1).empty());
    t.set_scheme(BindScheme::AltCamera);
    SH_CHECK(t.value(row_of("Move Up"), BindColumn::Key1) == "W");
    SH_CHECK(t.value(row_of("Sidestep Left"), BindColumn::Key1) == "A");
    SH_CHECK(t.value(row_of("Swap Shoulder"), BindColumn::Key1) == "Mouse3");  // shared
}

SH_TEST(bind_table_rear_look_needs_an_alternate_camera) {
    MemoryStore store;
    BindTable t(store);
    const int rear = row_of("Rear Look");
    SH_CHECK(!t.row_enabled(rear));
    SH_CHECK(!t.cell_usable(rear, BindColumn::Key1));
    t.set_scheme(BindScheme::AltCamera);
    SH_CHECK(t.row_enabled(rear));
    SH_CHECK(t.cell_usable(rear, BindColumn::Key1));
    SH_CHECK(!t.cell_usable(rear, BindColumn::Key2));
    SH_CHECK(!t.cell_usable(-1, BindColumn::Key1));
    // Rows the screen adds under the binds are never disabled by the table.
    SH_CHECK(t.row_enabled(BindTable::row_count()));
}

SH_TEST(bind_table_set_writes_the_action_line_with_both_slots) {
    MemoryStore store;
    BindTable t(store);
    t.set(row_of("Run"), BindColumn::Key2, "Mouse4");
    SH_CHECK(store.written.size() == 1);
    SH_CHECK((store.written[0] == BindLine{"action_run", "Left Shift,Mouse4"}));
    SH_CHECK(store.changes == 1);

    t.set_scheme(BindScheme::AltCamera);
    t.set(row_of("Run"), BindColumn::Key1, "NONE");
    SH_CHECK((store.written[1] == BindLine{"action_run_altcam", "NONE,Right Shift"}));
    // The classic scheme kept its own value.
    t.set_scheme(BindScheme::Classic);
    SH_CHECK(t.value(row_of("Run"), BindColumn::Key1) == "Left Shift");
    SH_CHECK(t.value(row_of("Run"), BindColumn::Key2) == "Mouse4");
}

SH_TEST(bind_table_set_writes_a_pad_line_under_the_scheme_key) {
    MemoryStore store;
    BindTable t(store);
    t.set(row_of("Run"), BindColumn::Pad1, "x");
    SH_CHECK((store.written.back() == BindLine{"pad_square", "x"}));
    t.set_scheme(BindScheme::AltCamera);
    t.set(row_of("Run"), BindColumn::Pad2, "leftshoulder");
    SH_CHECK((store.written.back() == BindLine{"pad_square_2_altcam", "leftshoulder"}));
    // A shared bind has one key whatever the scheme.
    t.set(row_of("Quick Save"), BindColumn::Pad1, "back");
    SH_CHECK((store.written.back() == BindLine{"pad_quicksave", "back"}));
    t.set(row_of("Quick Save"), BindColumn::Key1, "F5");
    SH_CHECK((store.written.back() == BindLine{"action_quick_save", "F5"}));
    SH_CHECK(store.changes == 4);
    // A cell the row does not have changes nothing.
    t.set(row_of("Quick Save"), BindColumn::Key2, "F7");
    t.set(99, BindColumn::Key1, "F7");
    SH_CHECK(store.changes == 4 && store.written.size() == 4);
}

SH_TEST(bind_table_values_are_cut_to_the_field) {
    MemoryStore store;
    BindTable t(store);
    t.set(row_of("Map"), BindColumn::Pad1, "abcdefghijklmnopqrstuvwxyz0123456789");
    SH_CHECK(t.value(row_of("Map"), BindColumn::Pad1) == "abcdefghijklmnopqrstuvwxyz01234");
    SH_CHECK(store.written.back().value == "abcdefghijklmnopqrstuvwxyz01234");
}

SH_TEST(bind_table_duplicates_are_per_device_and_scheme) {
    MemoryStore store;
    BindTable t(store);
    const int run = row_of("Run");
    const int map = row_of("Map");
    SH_CHECK(!t.is_duplicate(run, BindColumn::Key1));
    t.set(map, BindColumn::Key2, "left shift");  // case does not matter
    SH_CHECK(t.is_duplicate(run, BindColumn::Key1));
    SH_CHECK(t.is_duplicate(map, BindColumn::Key2));
    SH_CHECK(!t.is_duplicate(map, BindColumn::Key1));
    // An unbound cell is never a duplicate of another unbound cell.
    SH_CHECK(!t.is_duplicate(row_of("Quick Turn"), BindColumn::Key1));
    // The same name on a pad cell is another device.
    t.set(map, BindColumn::Pad1, "Left Shift");
    SH_CHECK(!t.is_duplicate(map, BindColumn::Pad1));
    // A row that is switched off does not count: Rear Look in the classic scheme.
    t.set_scheme(BindScheme::AltCamera);
    t.set(row_of("Rear Look"), BindColumn::Key1, "M");
    SH_CHECK(t.is_duplicate(map, BindColumn::Key1));
    t.set_scheme(BindScheme::Classic);
    SH_CHECK(!t.is_duplicate(map, BindColumn::Key1));
}

SH_TEST(bind_table_reset_writes_every_line_of_the_scheme_once) {
    MemoryStore store;
    BindTable t(store);
    t.set_scheme(BindScheme::AltCamera);
    t.set(row_of("Run"), BindColumn::Key1, "X");
    t.set(row_of("Run"), BindColumn::Pad1, "y");
    t.set(row_of("Quick Save"), BindColumn::Key1, "F5");
    store.written.clear();
    store.changes = 0;

    t.reset_scheme();
    SH_CHECK(store.batches == 1 && store.projections == 1 && store.changes == 1);
    SH_CHECK(t.value(row_of("Run"), BindColumn::Key1) == "Left Shift");
    SH_CHECK(t.value(row_of("Run"), BindColumn::Pad1) == "pad:pad_square");
    SH_CHECK(t.value(row_of("Quick Save"), BindColumn::Key1) == "F5");  // shared rows are not reset

    // One line per action and one per pad cell, in row order; shared rows have none.
    std::vector<BindLine> want;
    for (const BindRow& r : BindTable::rows()) {
        if (r.shared) {
            continue;
        }
        const ActionInfo& a = info(r.action);
        want.push_back({"action_" + std::string(a.name) + "_altcam", format_binding(default_binding(r.action, 1))});
        for (int c = 2; c < kBindColumns; ++c) {
            if (!r.slot[c].empty()) {
                want.push_back({r.slot[c] + "_altcam", "pad:" + r.slot[c]});
            }
        }
    }
    SH_CHECK(store.written == want);
    SH_CHECK((store.written[0] == BindLine{"action_move_forward_altcam", "W,Up"}));
}

SH_TEST(bind_table_reset_of_the_classic_scheme_uses_plain_keys) {
    MemoryStore store;
    BindTable t(store);
    t.reset_scheme();
    SH_CHECK((store.written[0] == BindLine{"action_move_forward", "Up,W"}));
    bool any_altcam = false;
    for (const BindLine& l : store.written) {
        any_altcam = any_altcam || l.key.ends_with("_altcam");
    }
    SH_CHECK(!any_altcam);
    // An unbound default is written as NONE.
    bool found = false;
    for (const BindLine& l : store.written) {
        if (l.key == "action_quick_turn") {
            found = true;
            SH_CHECK(l.value == "NONE");
        }
    }
    SH_CHECK(found);
}

SH_TEST(bind_table_toast_and_display_names) {
    MemoryStore store;
    BindTable t(store);
    SH_CHECK(t.rebind_toast(row_of("Run"), BindColumn::Key1) == "Changing Primary input for Run");
    SH_CHECK(t.rebind_toast(row_of("Map"), BindColumn::Pad2) == "Changing Secondary input for Map");
    SH_CHECK(t.rebind_toast(-1, BindColumn::Key2) == "Changing Secondary input for ");

    SH_CHECK(bind_display_name("Mouse1") == "L-Mouse");
    SH_CHECK(bind_display_name("mouse2") == "R-Mouse");
    SH_CHECK(bind_display_name("Mouse3") == "M-Mouse");
    SH_CHECK(bind_display_name("Mouse4") == "Mouse4");
    SH_CHECK(bind_display_name("MouseWheelUp") == "Wheel Up");
    SH_CHECK(bind_display_name("MouseWheelDown") == "Wheel Down");
    SH_CHECK(bind_display_name("Left Shift") == "L-Shift");
    SH_CHECK(bind_display_name("Right Ctrl") == "R-Ctrl");
    SH_CHECK(bind_display_name("Keypad 5") == "KP 5");
    SH_CHECK(bind_display_name("Left") == "Left");
    SH_CHECK(bind_display_name("Left ") == "Left ");
    SH_CHECK(bind_display_name("").empty());
}

SH_TEST(bind_table_chords_in_the_quick_action_rows) {
    // The quick actions read their controller binds through the chord resolver; the PS1 button rows do not.
    for (const char* label : {"Flashlight", "Reload", "Quick Turn", "Rear Look", "Cycle Weapons", "Quick Heal"}) {
        SH_CHECK(BindTable::row(row_of(label))->chord);
    }
    for (const char* label : {"Sidestep Left", "Run", "Change Camera", "Quick Save"}) {
        SH_CHECK(!BindTable::row(row_of(label))->chord);
    }

    MemoryStore store;
    BindTable t(store);
    const int turn = row_of("Quick Turn"), heal = row_of("Quick Heal");
    t.set(turn, BindColumn::Pad1, "leftshoulder+rightshoulder");
    SH_CHECK(t.value(turn, BindColumn::Pad1) == "leftshoulder+rightshoulder");
    SH_CHECK((store.written.back() == BindLine{"pad_quick_turn", "leftshoulder+rightshoulder"}));
    // The same two buttons in the other order are the same bind; one of them alone is not.
    t.set(heal, BindColumn::Pad1, "rightshoulder+leftshoulder");
    SH_CHECK(t.is_duplicate(turn, BindColumn::Pad1) && t.is_duplicate(heal, BindColumn::Pad1));
    t.set(heal, BindColumn::Pad1, "leftshoulder");
    SH_CHECK(!t.is_duplicate(turn, BindColumn::Pad1));

    SH_CHECK(bind_display_name("leftshoulder+rightshoulder") == "leftshoulder + rightshoulder");
    SH_CHECK(bind_display_name("Left Shift") == "L-Shift");
}
