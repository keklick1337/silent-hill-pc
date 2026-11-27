// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Semantic input actions. Keyboard and mouse bind to
// actions ("run", "back", "inventory"), not to PS1 buttons; each frame the held
// actions are translated into the PS1 pad bits the game logic reads, through the
// game's own controller configuration and the current screen. The pad word the
// logic sees is the same one replay records and feeds, so this layer is invisible
// to replay.
//
// This module is pure: no SDL, no game state. src/input/pc_actions.cpp is the
// runtime bridge (storage, config.cfg, the pad-word hook).

#pragma once

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace sh::input {

enum class Action : int {
    MoveForward,
    MoveBack,
    TurnLeft,
    TurnRight,
    StepLeft,
    StepRight,
    Run,
    Interact,
    Attack,
    Aim,
    View,
    Back,
    Confirm,
    Inventory,
    Map,
    Pause,
    // PC-only actions: no PS1 bit, read by their own code through the legacy
    // ControlScheme fields the bridge projects them onto.
    Flashlight,
    Reload,
    QuickTurn,
    RearLook,
    CycleWeapons,
    QuickHeal,
    ChangeCamera,
    SwapShoulder,
    DebugMenu,
    QuickSave,
    QuickLoad,
    Count
};

inline constexpr int kActionCount = static_cast<int>(Action::Count);
inline constexpr int kSlots = 2;      // primary + secondary
inline constexpr int kSchemes = 2;    // 0 = classic camera, 1 = alternate cameras (TPS/OTS/FPS)
inline constexpr std::size_t kNameMax = 23;  // bind names live in 24-byte C fields

enum ActionFlags : unsigned {
    kFlagGlobal = 1u << 0,      // one value for both schemes (stored in scheme 0)
    kFlagMouse = 1u << 1,       // the keyboard slots accept mouse buttons
    kFlagWheel = 1u << 2,       // ...and the wheel
    kFlagSingleSlot = 1u << 3,  // no secondary slot
    kFlagPcOnly = 1u << 4,      // no PS1 bit: projected onto a legacy field
    kFlagAltcamOnly = 1u << 5,  // does something only with an alternate camera
    kFlagPadChord = 1u << 6,    // the controller slot also takes two buttons together (input/bind/pad_chord.h)
};

struct ActionInfo {
    std::string_view name;   // config key suffix: action_<name>
    std::string_view label;  // bind screen row label
    unsigned flags;
    // Legacy ControlScheme keyboard fields this action came from ("" = none):
    // the migration source for the PS1 actions, the projection target for PC ones.
    std::string_view legacy_key;
    std::string_view legacy_key2;
    // Legacy controller fields shown in the bind screen's pad columns ("" = none).
    std::string_view pad_key;
    std::string_view pad_key2;
    std::array<std::array<std::string_view, kSlots>, kSchemes> defaults;
};

const ActionInfo& info(Action action);
std::optional<Action> find_action(std::string_view name);

using Binding = std::array<std::string, kSlots>;

// "NONE" (any case) and "" are both unbound; stored as "NONE".
bool is_unbound(std::string_view name);
Binding default_binding(Action action, int scheme);

// config.cfg value: "<key>[,<key2>]". Names are trimmed; empty or NONE slots are
// unbound. More than two names, or a name over kNameMax bytes, is an error.
Result<Binding> parse_binding(std::string_view value);
std::string format_binding(const Binding& binding);

// config.cfg key: action_<name> (classic or global) / action_<name>_altcam.
struct KeyRef {
    Action action;
    int scheme;
};
std::optional<KeyRef> parse_config_key(std::string_view key);
std::string config_key(Action action, int scheme);

// The game's controller configuration (s_ControllerConfig), one PS1 bit mask per
// function. Bits are active-high ControllerFlag_* values.
struct PadConfig {
    std::uint16_t enter = 0, cancel = 0, skip = 0, action = 0, aim = 0, light = 0, run = 0, view = 0;
    std::uint16_t step_left = 0, step_right = 0, pause = 0, item = 0, map = 0;
};
PadConfig default_pad_config();  // USA type A, Settings_RestoreControlDefaults(0)

// Where input goes this frame; decided when an action is pressed and kept until
// it is released, so a key held across a screen change keeps its bit.
enum class Context {
    Menu,       // title, options, save, pause, messages, FMV: navigation only
    World,      // in game but not free movement (events, room loads)
    Gameplay,   // free movement: Back = pause
    Inventory,  // status screen: the Inventory action closes it
    Map,        // paper map: the Map action closes it
    Paused,     // the original PAUSED screen (pause_menu = 0): only the pause button resumes
};

inline constexpr std::uint16_t kPadUp = 0x10, kPadRight = 0x20, kPadDown = 0x40, kPadLeft = 0x80;
inline constexpr std::uint16_t kPadCross = 0x4000, kPadCircle = 0x2000, kPadTriangle = 0x1000, kPadSquare = 0x8000;

// The single cancel bit Back presses outside gameplay: Circle, then Triangle,
// then Square, then the lowest bit of the mask.
std::uint16_t pick_cancel_bit(std::uint16_t cancel_mask);
// PS1 bits one held action presses in a context; 0 for PC-only actions.
std::uint16_t pad_bits(Action action, Context context, const PadConfig& config);

// The part of pad_bits() that stands for one press however long the key stays
// down. Confirm reaches the game as the skip button, which fast-forwards text
// while held on a pad; a keyboard key is a single advance.
std::uint16_t tap_bits(Action action, Context context, const PadConfig& config);

// Migration from the PS1-button keyboard binds (key_cross, key_cross_2, ...):
// `current` and `old_default` read a legacy field by config key ("" = unbound).
// When either slot differs from its old default the player chose it, and those
// keys become the action's binding; otherwise nullopt (the action default wins).
using LegacyReader = std::function<std::string(std::string_view key)>;
std::optional<Binding> migrate_legacy(Action action, const LegacyReader& current, const LegacyReader& old_default);

}  // namespace sh::input
