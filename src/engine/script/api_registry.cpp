// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/script/api_registry.h"

namespace sh::script {

namespace {
constexpr ApiAlias kAliases[] = {
    {"flag", "get", "flag_get"},
    {"flag", "set", "flag_set"},
    {"flag", "set_alt", "flag_set_alt"},
    {"item", "give", "give_item"},
    {"item", "count", "item_count"},
    {"item", "has", "has_item"},
    {"item", "take", "take_item"},
    {"player", "pos", "player_pos"},
    {"player", "set_pos", "player_set_pos"},
    {"player", "freeze", "player_freeze"},
    {"player", "flashlight", "flashlight"},
    {"map", "name", "map_name"},
    {"audio", "sfx", "sfx"},
    {"audio", "sfx_stop", "sfx_stop"},
    {"audio", "bgm", "bgm"},
    {"ui", "msg_draw", "msg_draw"},
    {"ui", "fade", "fade"},
    {"ui", "fade_done", "fade_done"},
    {"input", "action", "input_action"},
    {"input", "axis", "input_axis"},
    {"camera", "pos", "camera_pos"},
    {"camera", "look", "camera_look"},
};
}  // namespace

std::span<const ApiAlias> api_aliases() { return kAliases; }

}  // namespace sh::script
