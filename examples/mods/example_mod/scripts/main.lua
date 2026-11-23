-- Runs only because mod.json says "scripts": true. `mod` is this mod (id, name, version, dir);
-- mod.storage is a table that lasts for the session.

function on_boot()
    mod.storage.hellos = 0
    sh.console.register{
        name = "example_hello",
        group = "mods",
        help = "say hello from " .. mod.id,
        args = {
            { name = "who", type = "string", optional = true },
        },
        run = function(args)
            mod.storage.hellos = mod.storage.hellos + 1
            return "hello, " .. (args.who or "Harry") .. " (" .. mod.storage.hellos .. " so far, " ..
                   mod.name .. " " .. mod.version .. ")"
        end,
    }
end

-- The older way: a lifecycle function the engine calls in every mod. Fine for the mod's own work.
function on_map_load(map)
    sh.log("example_mod: map " .. map)
end

-- The hook bus: any number of mods listen to the same event without overwriting each other; handlers run by
-- priority, then load order. The event list is in docs/modding/events.md.
sh.hooks.on("map.load", function(ev)
    sh.log("example_mod: hook map.load " .. ev.map)
end, { id = "announce" })

-- Events can carry data: this one only watches. A handler may also change ev.count or call ev:cancel()
-- (item.pickup allows both), but that changes gameplay and needs "gameplay": true in mod.json.
sh.hooks.on("item.picked", function(ev)
    sh.log(string.format("example_mod: picked %d x %s on %s", ev.count, ev.item, ev.map))
end, { id = "watch_pickups" })

-- Other mods call this through sh.mods.get("example_mod").api.
mod.export{
    hellos = function() return mod.storage.hellos or 0 end,
}
