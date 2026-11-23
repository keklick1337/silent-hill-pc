-- Ambush: when Harry takes the handgun ammo from the bench outside the cafe (map2_s00), Groaners appear a few
-- metres behind him and hunt him. Easy 1, normal 2, hard 3; once per playthrough (the flag is saved by name).
-- Only the public script API is used: the item.picked hook, sh.game.difficulty, sh.world.walkable and
-- nearest_walkable, sh.entity.spawn.

local MAP = "map2_s00"
local DONE = "ambush_dogs:ambush_done"
local BENCH_AMMO = { M2S00_HandgunBullets11 = true, M2S00_HandgunBullets12 = true }
local COUNT = { easy = 1, normal = 2, hard = 3 }

local Q = 4096         -- world units per metre
local TURN = 4096      -- angle units per full turn; forward = (sin, cos)

-- Where the dogs come from: each dog has its own bearing behind Harry (degrees from his facing, 180 = straight
-- behind), so they arrive spread apart, and tries it from the farthest distance in to the nearest (metres): when a
-- wall or a step is in the way the dog comes from nearer instead of from somewhere off to the side.
local BEARINGS = { 180, 155, 205, 140, 220 }
local DISTANCES = { 4, 3.5, 3, 2.5, 2 }
local MIN_GAP = 1.2 * Q          -- between two dogs
local CLEARANCE = math.floor(0.5 * Q)  -- free floor around a dog, so it does not stand inside a wall
local NUDGE = Q                  -- last resort: the nearest free floor this close to the wanted spot

local spawned = {}     -- slots spawned by the last ambush, for other mods (see mod.export below)

local function angle_to(fx, fz, tx, tz)
    return math.floor(math.atan(tx - fx, tz - fz) / (2 * math.pi) * TURN + 0.5) % TURN
end

-- Floor a dog can stand on: walkable, reachable from Harry in a straight line (no wall or step between), with free
-- floor on every side, and not on top of a dog already placed.
local function good_spot(x, z, px, pz, points)
    if not sh.world.walkable(x, z, px, pz) then
        return false
    end
    for _, o in ipairs({ { CLEARANCE, 0 }, { -CLEARANCE, 0 }, { 0, CLEARANCE }, { 0, -CLEARANCE } }) do
        if not sh.world.walkable(x + o[1], z + o[2], x, z) then
            return false
        end
    end
    for _, p in ipairs(points) do
        if math.abs(p.x - x) + math.abs(p.z - z) < MIN_GAP then
            return false
        end
    end
    return true
end

local function spawn_points(want)
    local px, _, pz = sh.player.pos()
    local facing = sh.player.facing() / TURN * 2 * math.pi
    local points = {}
    for i = 1, math.min(want, #BEARINGS) do
        local t = facing + math.rad(BEARINGS[i])
        local spot
        for _, d in ipairs(DISTANCES) do
            local cx = math.floor(px + math.sin(t) * d * Q)
            local cz = math.floor(pz + math.cos(t) * d * Q)
            if good_spot(cx, cz, px, pz, points) then
                spot = { x = cx, z = cz }
                break
            end
        end
        if not spot then
            -- Nothing free along the bearing: the nearest free floor around its nearest spot.
            local d = DISTANCES[#DISTANCES]
            local nx, nz = sh.world.nearest_walkable(math.floor(px + math.sin(t) * d * Q),
                                                     math.floor(pz + math.cos(t) * d * Q), NUDGE, px, pz)
            if nx and good_spot(nx, nz, px, pz, points) then
                spot = { x = nx, z = nz }
            end
        end
        if spot then
            spot.rot = angle_to(spot.x, spot.z, px, pz)
            points[#points + 1] = spot
        end
    end
    return points
end

local function ambush()
    local difficulty = sh.game.difficulty() or "normal"
    -- Options > Ambush: dogs (menus/ambush_dogs.lua) adds dogs on top of the difficulty's count.
    local want = (COUNT[difficulty] or 2) + math.floor(sh.config.get("ambush_dogs.extra") or 0)
    local points = spawn_points(want)
    spawned = {}
    for _, p in ipairs(points) do
        local slot, err = sh.entity.spawn{ chara = "Groaner", x = p.x, z = p.z, rot = p.rot, ai = "full" }
        if slot then
            spawned[#spawned + 1] = slot
            sh.log(string.format("ambush_dogs: Groaner in slot %d at (%.2f, %.2f)", slot, p.x / Q, p.z / Q))
        else
            sh.log(1, "ambush_dogs: " .. tostring(err))
        end
    end
    sh.log(string.format("ambush_dogs: %s, %d of %d dogs", difficulty, #spawned, want))
    -- Set even if fewer dogs fit: the ambush is a one-time scare, not something to retry on every pickup.
    sh.flag.set(DONE, true)
    sh.hooks.emit("ambush_dogs:ambushed", { map = MAP, count = #spawned, difficulty = difficulty })
end

sh.hooks.on("item.picked", function(ev)
    if ev.map ~= MAP or not BENCH_AMMO[ev.flag] or sh.flag.get(DONE) or sh.config.get("ambush_dogs.enabled") == false then
        return
    end
    ambush()
end, { id = "bench_ammo" })

-- Cross-mod API: sh.mods.get("ambush_dogs").api in another mod's scripts/main.lua.
mod.export{
    -- True once the ambush happened in this playthrough.
    ambushed = function() return sh.flag.get(DONE) end,
    -- Slots of the dogs spawned by the last ambush in this session.
    dogs = function()
        local copy = {}
        for i, s in ipairs(spawned) do copy[i] = s end
        return copy
    end,
    -- Lets another mod reuse the spot search: up to `n` points behind Harry, {x, z, rot} in Q12.
    points_behind = function(n) return spawn_points(n or 1) end,
}
