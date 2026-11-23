// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/input/input_actions.h"

#include <cctype>

namespace sh::input {
namespace {

constexpr unsigned kKey = kFlagMouse | kFlagWheel;
constexpr unsigned kPc = kFlagPcOnly | kFlagSingleSlot;

// Order is the bind screen's row order. Defaults: {classic, altcam} x {primary, secondary}.
constexpr std::array<ActionInfo, kActionCount> kActions = {{
    {"move_forward", "Move Forward", kKey, "key_up", "key_up_2", "", "", {{{"Up", "W"}, {"W", "Up"}}}},
    {"move_back", "Move Back", kKey, "key_down", "key_down_2", "", "", {{{"Down", "S"}, {"S", "Down"}}}},
    {"turn_left", "Turn Left", kKey, "key_left", "key_left_2", "", "", {{{"Left", "A"}, {"Left", "NONE"}}}},
    {"turn_right", "Turn Right", kKey, "key_right", "key_right_2", "", "", {{{"Right", "D"}, {"Right", "NONE"}}}},
    {"step_left", "Sidestep Left", kKey, "key_l1", "key_l1_2", "pad_l1", "pad_l1_2", {{{"Q", "NONE"}, {"A", "Q"}}}},
    {"step_right", "Sidestep Right", kKey, "key_r1", "key_r1_2", "pad_r1", "pad_r1_2", {{{"E", "NONE"}, {"D", "E"}}}},
    {"run", "Run", kKey, "key_square", "key_square_2", "pad_square", "pad_square_2",
     {{{"Left Shift", "Right Shift"}, {"Left Shift", "Right Shift"}}}},
    {"interact", "Action", kKey, "key_cross", "key_cross_2", "pad_cross", "pad_cross_2",
     {{{"Space", "NONE"}, {"Space", "NONE"}}}},
    {"attack", "Attack", kKey, "", "", "", "", {{{"Mouse1", "NONE"}, {"Mouse1", "NONE"}}}},
    {"aim", "Aim", kKey, "key_r2", "key_r2_2", "pad_r2", "pad_r2_2", {{{"Mouse2", "Left Ctrl"}, {"Mouse2", "Left Ctrl"}}}},
    {"view", "View", kKey, "key_l2", "key_l2_2", "pad_l2", "pad_l2_2", {{{"V", "NONE"}, {"NONE", "NONE"}}}},
    {"back", "Back / Cancel", kKey, "key_circle", "key_circle_2", "pad_circle", "pad_circle_2",
     {{{"Escape", "Backspace"}, {"Escape", "Backspace"}}}},
    {"confirm", "Confirm / Skip", kKey, "", "", "", "", {{{"Return", "Keypad Enter"}, {"Return", "Keypad Enter"}}}},
    {"inventory", "Inventory", kKey, "key_select", "key_select_2", "pad_select", "pad_select_2",
     {{{"Tab", "I"}, {"Tab", "I"}}}},
    {"map", "Map", kKey, "key_triangle", "key_triangle_2", "pad_triangle", "pad_triangle_2",
     {{{"M", "NONE"}, {"M", "NONE"}}}},
    {"pause", "Pause", kKey, "key_start", "key_start_2", "pad_start", "pad_start_2",
     {{{"Pause", "NONE"}, {"Pause", "NONE"}}}},
    {"flashlight", "Flashlight", kPc, "key_flashlight", "", "pad_flashlight", "", {{{"F", "NONE"}, {"F", "NONE"}}}},
    {"reload", "Reload", kFlagPcOnly, "key_reload", "key_reload_2", "pad_reload", "", {{{"R", "NONE"}, {"R", "NONE"}}}},
    {"quick_turn", "Quick Turn", kPc, "key_quick_turn", "", "pad_quick_turn", "", {{{"NONE", "NONE"}, {"NONE", "NONE"}}}},
    {"rear_look", "Rear Look", kPc | kFlagAltcamOnly, "key_rear_look", "", "pad_rear_look", "",
     {{{"NONE", "NONE"}, {"NONE", "NONE"}}}},
    {"cycle_weapons", "Cycle Weapons", kPc, "key_cycle_weapons", "", "pad_cycle_weapons", "",
     {{{"NONE", "NONE"}, {"NONE", "NONE"}}}},
    {"quick_heal", "Quick Heal", kPc, "key_quick_heal", "", "pad_quick_heal", "", {{{"NONE", "NONE"}, {"NONE", "NONE"}}}},
    {"change_camera", "Change Camera", kPc, "key_change_cam", "", "pad_change_cam", "", {{{"F9", "NONE"}, {"F9", "NONE"}}}},
    {"swap_shoulder", "Swap Shoulder", kPc | kFlagGlobal | kFlagMouse, "key_swap_shoulder", "", "pad_swap_shoulder", "",
     {{{"Mouse3", "NONE"}, {"Mouse3", "NONE"}}}},
    {"debug_menu", "Debug Options", kPc | kFlagGlobal, "key_debug_menu", "", "pad_debug_menu", "",
     {{{"F10", "NONE"}, {"F10", "NONE"}}}},
    {"quick_save", "Quick Save", kPc | kFlagGlobal, "key_quicksave", "", "pad_quicksave", "",
     {{{"F6", "NONE"}, {"F6", "NONE"}}}},
    {"quick_load", "Quick Load", kPc | kFlagGlobal, "key_quickload", "", "pad_quickload", "",
     {{{"F8", "NONE"}, {"F8", "NONE"}}}},
}};

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

std::string normalized(std::string_view name) {
    return is_unbound(name) ? std::string("NONE") : std::string(name);
}

constexpr std::string_view kPrefix = "action_";
constexpr std::string_view kAltcam = "_altcam";

}  // namespace

const ActionInfo& info(Action action) { return kActions[static_cast<std::size_t>(action)]; }

std::optional<Action> find_action(std::string_view name) {
    for (int i = 0; i < kActionCount; ++i) {
        if (kActions[static_cast<std::size_t>(i)].name == name) {
            return static_cast<Action>(i);
        }
    }
    return std::nullopt;
}

bool is_unbound(std::string_view name) { return name.empty() || iequals(name, "NONE"); }

Binding default_binding(Action action, int scheme) {
    const ActionInfo& a = info(action);
    const int s = (a.flags & kFlagGlobal) ? 0 : (scheme != 0 ? 1 : 0);
    return {std::string(a.defaults[static_cast<std::size_t>(s)][0]),
            std::string(a.defaults[static_cast<std::size_t>(s)][1])};
}

Result<Binding> parse_binding(std::string_view value) {
    Binding out{"NONE", "NONE"};
    int slot = 0;
    std::string_view rest = value;
    while (true) {
        const std::size_t comma = rest.find(',');
        const std::string_view part = trim(rest.substr(0, comma));
        if (slot >= kSlots) {
            return Result<Binding>::fail("more than two keys in \"" + std::string(value) + "\"");
        }
        if (part.size() > kNameMax) {
            return Result<Binding>::fail("key name too long: \"" + std::string(part) + "\"");
        }
        out[static_cast<std::size_t>(slot++)] = normalized(part);
        if (comma == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(comma + 1);
    }
    return Result<Binding>::ok(std::move(out));
}

std::string format_binding(const Binding& binding) {
    const std::string first = normalized(binding[0]);
    if (is_unbound(binding[1])) {
        return first;
    }
    return first + "," + binding[1];
}

std::optional<KeyRef> parse_config_key(std::string_view key) {
    if (!key.starts_with(kPrefix)) {
        return std::nullopt;
    }
    key.remove_prefix(kPrefix.size());
    int scheme = 0;
    if (key.ends_with(kAltcam)) {
        key.remove_suffix(kAltcam.size());
        scheme = 1;
    }
    const std::optional<Action> action = find_action(key);
    if (!action || (scheme == 1 && (info(*action).flags & kFlagGlobal))) {
        return std::nullopt;
    }
    return KeyRef{*action, scheme};
}

std::string config_key(Action action, int scheme) {
    std::string key = std::string(kPrefix) + std::string(info(action).name);
    if (scheme != 0 && !(info(action).flags & kFlagGlobal)) {
        key += kAltcam;
    }
    return key;
}

PadConfig default_pad_config() {
    PadConfig c;
    c.enter = 0x0008 | kPadCross;                      // Start | Cross
    c.cancel = kPadTriangle | kPadCircle | kPadSquare;
    c.skip = 0x0008;                                   // Start
    c.action = kPadCross;
    c.aim = 0x0200;                                    // R2
    c.light = kPadCircle;
    c.run = kPadSquare;
    c.view = 0x0100;                                   // L2
    c.step_left = 0x0400;                              // L1
    c.step_right = 0x0800;                             // R1
    c.pause = 0x0008;                                  // Start
    c.item = 0x0001;                                   // Select
    c.map = kPadTriangle;
    return c;
}

std::uint16_t pick_cancel_bit(std::uint16_t cancel_mask) {
    for (const std::uint16_t bit : {kPadCircle, kPadTriangle, kPadSquare}) {
        if (cancel_mask & bit) {
            return bit;
        }
    }
    return static_cast<std::uint16_t>(cancel_mask & static_cast<std::uint16_t>(-cancel_mask));
}

std::uint16_t pad_bits(Action action, Context context, const PadConfig& c) {
    // In a menu the gameplay buttons stay silent: Run (Square) and Map
    // (Triangle) are cancel bits there, and Shift or M must not leave a screen.
    const bool in_world = context == Context::World || context == Context::Gameplay;
    switch (action) {
        case Action::MoveForward: return kPadUp;
        case Action::MoveBack: return kPadDown;
        case Action::TurnLeft: return kPadLeft;
        case Action::TurnRight: return kPadRight;
        case Action::Interact: return c.action;
        case Action::Back:
            return (context == Context::Gameplay || context == Context::Paused) ? c.pause : pick_cancel_bit(c.cancel);
        case Action::Confirm: return context == Context::Gameplay ? c.action : c.skip;
        case Action::StepLeft: return in_world ? c.step_left : 0;
        case Action::StepRight: return in_world ? c.step_right : 0;
        case Action::Run: return in_world ? c.run : 0;
        case Action::Attack: return in_world ? c.action : 0;
        case Action::Aim: return in_world ? c.aim : 0;
        case Action::View: return in_world ? c.view : 0;
        case Action::Pause: return (in_world || context == Context::Paused) ? c.pause : 0;  // it also resumes
        case Action::Inventory: return (in_world || context == Context::Inventory) ? c.item : 0;
        case Action::Map: return (in_world || context == Context::Map) ? c.map : 0;
        default: return 0;
    }
}

std::uint16_t tap_bits(Action action, Context context, const PadConfig& c) {
    return action == Action::Confirm && context != Context::Gameplay ? c.skip : 0;
}

std::optional<Binding> migrate_legacy(Action action, const LegacyReader& current, const LegacyReader& old_default) {
    const ActionInfo& a = info(action);
    if (a.legacy_key.empty()) {
        return std::nullopt;
    }
    const std::array<std::string_view, kSlots> keys = {a.legacy_key, a.legacy_key2};
    Binding legacy{"NONE", "NONE"};
    bool customized = false;
    for (std::size_t s = 0; s < keys.size(); ++s) {
        if (keys[s].empty()) {
            continue;
        }
        const std::string now = current(keys[s]);
        const std::string was = old_default(keys[s]);
        legacy[s] = normalized(now);
        if (!iequals(normalized(now), normalized(was))) {
            customized = true;
        }
    }
    if (!customized) {
        return std::nullopt;
    }
    if (is_unbound(legacy[0]) && !is_unbound(legacy[1])) {
        std::swap(legacy[0], legacy[1]);
    }
    if (iequals(legacy[0], legacy[1])) {
        legacy[1] = "NONE";
    }
    return legacy;
}

}  // namespace sh::input
