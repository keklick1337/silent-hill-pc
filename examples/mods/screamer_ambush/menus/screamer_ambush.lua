-- Options > Mod Options > Ambush: Air Screamer — its own page (sh.menu.add_page) next to the other mods' pages.
sh.menu.add_page{
    parent = "options",
    id = "screamer_ambush:options",
    title = "screamer_ambush:ui.title",
    help = "screamer_ambush:ui.help",
    rows = {
        { kind = "info", id = "screamer_ambush:where", label = "screamer_ambush:ui.where" },
        { kind = "info", id = "screamer_ambush:with", label = "screamer_ambush:ui.with" },
        { kind = "info", id = "screamer_ambush:version", label = "screamer_ambush:ui.version", text = "1.0.0" },
    },
}
