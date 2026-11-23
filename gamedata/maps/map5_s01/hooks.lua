-- Engine hooks of map5_s01 (loaded with the map, before any of its code runs; see scripts/map_hooks.lua).

-- Music, run by the engine at the music tick (the same on entry): the layer flags depend on the
-- room, layer 4's volume on how far the player is from the room's edge, with a slow swell on top (a
-- sine of the running time).

-- Layer flags per room index (0..11), from the map's data.
local ROOM_FLAGS = { 0, 22, 22, 26, 26, 30, 30, 30, 30, 18, 18, 18 }

-- Limits of the eight layers; layer 4 is rewritten each tick.
local limits = { 0x80, 0x80, 0x80, 0x80, 0x80, 0, 0, 0 }
local timer = 0

local Q = 4096

local function mult(a, b)
    return (a * b) // Q
end

local function bgm_event(room, dt)
    local x, _, z = sh.player_pos()
    local flags = ROOM_FLAGS[room + 1] or 0
    local d
    if room == 10 then
        local px = x + 150 * Q
        local pz = -25 * Q - z
        d = (px < pz and px or pz)
        -- C division truncates; a negative value is clamped to 0 below anyway.
        d = d // 2
    elseif z > -120 * Q then
        local px = x - 16 * Q
        local pz = -48 * Q - z
        d = (px < pz) and px or pz
    else
        local px
        if x > 0 then px = x - 16 * Q else px = -52 * Q - x end
        local pz = -144 * Q - z
        d = (pz < px) and px or pz
    end

    timer = timer + dt
    if d < 0 then
        d = 0
    elseif d >= 32 * Q then
        d = 32 * Q - 1
    end

    local swell = (sh.math_sin(timer // 12) + 2 * Q) // 2
    limits[5] = mult(d, swell) // (Q // 2)
    sh.music.update(flags, 819, table.unpack(limits))
end

function bgm(init)
    bgm_event(sh.save.map_room_idx(), sh.game.delta_time())
end
