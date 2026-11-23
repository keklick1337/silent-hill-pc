-- Two extra pickups on the cafe bar counter (map0_s01): an Ampoule next to the Kitchen Knife and a First Aid Kit at
-- the south end of the counter. Runtime pickups live only while their map is loaded, so they are created again on
-- every load of the map; once taken, their flag (saved by name) keeps them hidden.

-- Q12 units, as everywhere in the API: 4096 = 1 m (y is negative upwards), angles 4096 = a full turn; a metre
-- value converts as math.floor(m * 4096) for positive m.

-- The base Kitchen Knife on the counter: (5.09, -1.01, 274.12) m, 32 degrees.
local KNIFE = { x = 20848, y = -4136, z = 1122795, rot = 364 }

-- Offsets from the knife. Only the front half of the counter top (x <= ~5.25 m) is within Harry's reach, and he
-- reaches the counter only at its north and south ends, so the kit goes to the south end, 6.27 m from the knife.
-- The short item and flag names are this mod's first, then the base game's.
local PICKUPS = {
    { id = "ampoule", item = "Ampoule", dx = 409, dy = 0, dz = -819, drot = 0, flag = "ampoule_taken" },
    { id = "kit", item = "FirstAidKit", dx = 40, dy = 40, dz = -25681, drot = 1024, flag = "kit_taken" },
}

sh.hooks.on("map.load", function(ev)
    if ev.map ~= "map0_s01" then
        return
    end
    for _, p in ipairs(PICKUPS) do
        local id, err = sh.world.pickup_create{
            item = p.item, id = p.id, flag = p.flag, count = 1,
            x = KNIFE.x + p.dx, y = KNIFE.y + p.dy, z = KNIFE.z + p.dz, rot = KNIFE.rot + p.drot,
        }
        if id then
            sh.log("cafe_supplies: " .. id .. (sh.flag.get(p.flag) and " (already taken)" or ""))
        else
            sh.log(1, "cafe_supplies: " .. p.item .. ": " .. tostring(err))
        end
    end
end, { id = "counter" })
