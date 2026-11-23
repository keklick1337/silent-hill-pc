-- DEBUG OPTIONS (F10): the developer's menu. Every `native` is registered by
-- Pc_DebugBinds_Register (src/features/cheats/pc_debug_binds.cpp) lists them.
-- Rows that touch the player or the loaded map are disabled on the title screen by their native.

sh.menu.define{
    id = "debug",
    title = "ui.debug.title",
    rows = {
        { kind = "submenu", label = "ui.debug.cheats",  help = "ui.debug.cheats.help",  submenu = "debug.cheats" },
        { kind = "submenu", label = "ui.debug.items",   help = "ui.debug.items.help",   submenu = "debug.items" },
        { kind = "submenu", label = "ui.debug.maps",    help = "ui.debug.maps.help",    submenu = "debug.maps" },
        { kind = "submenu", label = "ui.debug.spawn",   help = "ui.debug.spawn.help",   submenu = "debug.spawn" },
        { kind = "submenu", label = "ui.debug.camera",  help = "ui.debug.camera.help",  submenu = "debug.camera" },
        { kind = "submenu", label = "ui.debug.tools",   help = "ui.debug.tools.help",   submenu = "debug.tools" },
    },
}

sh.menu.define{
    id = "debug.cheats",
    title = "ui.debug.cheats",
    rows = {
        { kind = "toggle", label = "ui.debug.god_mode",        help = "ui.debug.god_mode.help",        native = "debug.god_mode" },
        { kind = "toggle", label = "ui.debug.infinite_ammo",   help = "ui.debug.infinite_ammo.help",   native = "debug.infinite_ammo" },
        { kind = "toggle", label = "ui.debug.noclip",          help = "ui.debug.noclip.help",          native = "debug.noclip" },
        { kind = "toggle", label = "ui.debug.enemies_ignore",  help = "ui.debug.enemies_ignore.help",  native = "debug.enemies_ignore" },
        { kind = "action", label = "ui.debug.heal_full",       help = "ui.debug.heal_full.help",       native = "debug.heal_full" },
        { kind = "action", label = "ui.debug.heal_items",      help = "ui.debug.heal_items.help",      native = "debug.heal_items" },
        { kind = "action", label = "ui.debug.kill_nearby",     help = "ui.debug.kill_nearby.help",     native = "debug.kill_nearby" },
    },
}

sh.menu.define{
    id = "debug.items",
    title = "ui.debug.items",
    rows = {
        { kind = "dropdown", label = "ui.debug.give_item",     help = "ui.debug.give_item.help",     native = "debug.give_item" },
        { kind = "number",   label = "ui.debug.give_quantity", help = "ui.debug.give_quantity.help", native = "debug.give_quantity",
          min = 1, max = 99, step = 1, integer = true },
        { kind = "action",   label = "ui.debug.give",          help = "ui.debug.give.help",          native = "debug.give" },
    },
}

sh.menu.define{
    id = "debug.maps",
    title = "ui.debug.maps",
    rows = {
        { kind = "dropdown", label = "ui.debug.goto_map",    help = "ui.debug.goto_map.help",    native = "debug.goto_map" },
        { kind = "action",   label = "ui.debug.restart_map", help = "ui.debug.restart_map.help", native = "debug.restart_map" },
        { kind = "dropdown", label = "ui.debug.start_map",   help = "ui.debug.start_map.help",   native = "debug.start_map" },
    },
}

sh.menu.define{
    id = "debug.spawn",
    title = "ui.debug.spawn",
    rows = {
        { kind = "dropdown", label = "ui.debug.spawn_category",  help = "ui.debug.spawn_category.help",  native = "debug.spawn_category" },
        { kind = "dropdown", label = "ui.debug.spawn_character", help = "ui.debug.spawn_character.help", native = "debug.spawn_character" },
        { kind = "number",   label = "ui.debug.spawn_distance",  help = "ui.debug.spawn_distance.help",  native = "debug.spawn_distance",
          min = 1, max = 20, step = 1, integer = true },
        { kind = "number",   label = "ui.debug.spawn_state",     help = "ui.debug.spawn_state.help",     native = "debug.spawn_state",
          min = -1, max = 40, step = 1, integer = true },
        { kind = "dropdown", label = "ui.debug.spawn_ai",        help = "ui.debug.spawn_ai.help",        native = "debug.spawn_ai" },
        { kind = "toggle",   label = "ui.debug.spawn_preview_on", help = "ui.debug.spawn_preview_on.help", native = "debug.spawn_preview_on" },
        { kind = "action",   label = "ui.debug.spawn_now",       help = "ui.debug.spawn_now.help",       native = "debug.spawn" },
    },
}

sh.menu.define{
    id = "debug.camera",
    title = "ui.debug.camera",
    rows = {
        { kind = "toggle", label = "ui.debug.free_camera", help = "ui.debug.free_camera.help", native = "debug.free_camera" },
        { kind = "action", label = "ui.debug.log_camera",  help = "ui.debug.log_camera.help",  native = "debug.log_camera" },
    },
}

sh.menu.define{
    id = "debug.tools",
    title = "ui.debug.tools",
    rows = {
        { kind = "dropdown", label = "ui.debug.flag",           help = "ui.debug.flag.help",           native = "debug.flag" },
        { kind = "toggle",   label = "ui.debug.flag_value",     help = "ui.debug.flag_value.help",     native = "debug.flag_value" },
        { kind = "choice",   label = "ui.debug.fps_display",    help = "ui.debug.fps_display.help",    native = "debug.fps_display" },
        { kind = "toggle",   label = "ui.debug.profiler",       help = "ui.debug.profiler.help",       native = "debug.profiler" },
        { kind = "toggle",   label = "ui.debug.perf_record",    help = "ui.debug.perf_record.help",    native = "debug.perf_record" },
        { kind = "toggle",   label = "ui.debug.collision_view", help = "ui.debug.collision_view.help", native = "debug.collision_view" },
        { kind = "action",   label = "ui.debug.log_position",   help = "ui.debug.log_position.help",   native = "debug.log_position" },
    },
}
