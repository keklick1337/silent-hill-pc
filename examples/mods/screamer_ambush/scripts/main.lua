-- A second mod on the same event as ambush_dogs: taking the bench ammo outside the cafe (map2_s00) also brings an
-- Air Screamer, once per playthrough. Both mods' item.picked handlers run; with equal priority the load order
-- decides, and mod.json says load_after ambush_dogs, so the dogs come first and this handler can ask about them
-- through the API ambush_dogs exports. Works without ambush_dogs too.

local MAP = "map2_s00"
local DONE = "screamer_ambush:screamer_done"
local BENCH_AMMO = { M2S00_HandgunBullets11 = true, M2S00_HandgunBullets12 = true }

local Q = 4096
local TURN = 4096

-- The screamer comes from ahead and to one side, over the street, so the player sees it arrive.
local AHEAD = 8 * Q
local SIDES = { -50, 50, -90, 90 }

local function place()
    local px, py, pz = sh.player.pos()
    local facing = sh.player.facing() / TURN * 2 * math.pi
    for _, side in ipairs(SIDES) do
        local t = facing + math.rad(side)
        local x, z = sh.world.nearest_walkable(math.floor(px + math.sin(t) * AHEAD),
                                               math.floor(pz + math.cos(t) * AHEAD), 3 * Q)
        if x then
            local rot = math.floor(math.atan(px - x, pz - z) / (2 * math.pi) * TURN + 0.5) % TURN
            return x, z, rot
        end
    end
end

sh.hooks.on("item.picked", function(ev)
    if ev.map ~= MAP or not BENCH_AMMO[ev.flag] or sh.flag.get(DONE) then
        return
    end
    local dogs = sh.mods.get("ambush_dogs")
    if dogs and dogs.api then
        sh.log(string.format("screamer_ambush: ambush_dogs %s is here, ambushed=%s, dogs=%d", dogs.version,
            tostring(dogs.api.ambushed()), #dogs.api.dogs()))
    else
        sh.log("screamer_ambush: ambush_dogs is not active, the screamer comes alone")
    end
    local x, z, rot = place()
    if not x then
        sh.log(1, "screamer_ambush: no open ground ahead of Harry")
        return
    end
    local slot, err = sh.entity.spawn{ chara = "AirScreamer", x = x, z = z, rot = rot, ai = "full" }
    if slot then
        sh.log(string.format("screamer_ambush: AirScreamer in slot %d at (%.2f, %.2f)", slot, x / Q, z / Q))
        sh.flag.set(DONE, true)
    else
        sh.log(1, "screamer_ambush: " .. tostring(err))
    end
end, { id = "bench_ammo" })

-- ambush_dogs also announces its ambush as a mod event; any mod can listen to it.
sh.hooks.on("ambush_dogs:ambushed", function(ev)
    sh.log(string.format("screamer_ambush: heard ambush_dogs:ambushed (%d dogs on %s)", ev.count, ev.difficulty))
end)
