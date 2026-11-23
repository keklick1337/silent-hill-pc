// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/debug/console_commands.h"

#include "engine/debug/commands/command_groups.h"
#include "engine/hooks/hooks_console.h"
#include "engine/mods/mods_console.h"

namespace sh::debug {

void register_game_commands(sh::console::Registry& registry) {
    sh::mods::register_console_commands(registry);
    sh::hooks::register_console_commands(registry);

    commands::register_system_commands(registry);
    commands::register_capture_commands(registry);
    commands::register_script_commands(registry);
    commands::register_world_commands(registry);
    commands::register_inspect_commands(registry);
    commands::register_flag_commands(registry);
    commands::register_item_commands(registry);
    commands::register_spawn_commands(registry);
    commands::register_selection_commands(registry);
    commands::register_cheat_commands(registry);
    commands::register_camera_commands(registry);
    commands::register_display_commands(registry);
    commands::register_render_commands(registry);
    commands::register_light_commands(registry);
    commands::register_loading_commands(registry);
    commands::register_perf_commands(registry);
}

}  // namespace sh::debug
