-- Options > Mod Options > Ambush: dogs — a page of its own (sh.menu.add_page), so it stacks with other mods' pages.
-- Menu files of all mods share one Lua state, so everything stays local to this file.
-- Settings live in mods_config.cfg; scripts/main.lua reads them with sh.config.get (declared here: menu files run
-- before the game scripts).
sh.config.declare{ key = "ambush_dogs.enabled", type = "bool", default = true }
sh.config.declare{ key = "ambush_dogs.extra", type = "number", default = 0, min = 0, max = 3, step = 1 }

sh.menu.add_page{
    parent = "options",
    id = "ambush_dogs:options",
    title = "ambush_dogs:ui.title",
    help = "ambush_dogs:ui.help",
    rows = {
        { kind = "toggle", id = "ambush_dogs:enabled", label = "ambush_dogs:ui.enabled", config = "ambush_dogs.enabled" },
        { kind = "slider", id = "ambush_dogs:extra", label = "ambush_dogs:ui.extra", config = "ambush_dogs.extra" },
        { kind = "info", id = "ambush_dogs:where", label = "ambush_dogs:ui.where" },
        { kind = "info", id = "ambush_dogs:count", label = "ambush_dogs:ui.count", text = "1 / 2 / 3" },
        { kind = "info", id = "ambush_dogs:version", label = "ambush_dogs:ui.version", text = "1.0.0" },
    },
}
