-- Menu files of every mod share one Lua state with the base menus and only see sh.menu and sh.log,
-- so the toggle keeps its value in this file; it lives until the game exits.
local greeting = true

sh.menu.define{
    id = "example_mod:menu",
    title = "example_mod:ui.title",
    rows = {
        { kind = "info", id = "example_mod:about", label = "example_mod:ui.about",
          text = function() return "1.0.0" end },
        { kind = "toggle", id = "example_mod:greeting", label = "example_mod:ui.greeting",
          get = function() return greeting end,
          set = function(v)
              greeting = v
              sh.log("example_mod: greeting " .. (v and "on" or "off"))
          end },
    },
}

-- The base pause menu is read before any mod's menus, so it exists here.
if not sh.menu.append_rows("pause", {
    { kind = "submenu", id = "example_mod:entry", label = "example_mod:ui.entry", submenu = "example_mod:menu" },
}) then
    sh.log("example_mod: there is no pause menu to extend")
end
