-- World objects and per-tick rules of map1_s02.
-- The bathroom door, the two boss-room doors and the box are the records BathroomDoor, BossDoor1/2 and
-- Box: events.lua turns and lifts them by name, this script sets them and reads the doors for the
-- collision. The two ventilation fans turn about z while the player is in their cell (x 2, z 3), each by
-- dt * (base - a random 0..511 angle units) per second (base -180 and -205 degrees). The random numbers are
-- the game's (sh.rng_int(0, 0x7FFF) is one Rng_Rand16), drawn in the original's order (fan 0, then fan 1)
-- and last: nothing before them draws any.

local CELL = 163840 -- 40 m
local FANS = {{name = "Fan0", base = -2048}, {name = "Fan1", base = -2332}}
local COLLISION_ALL, COLLISION_1, COLLISION_2 = 0xFFFF, 2, 4

-- The fan hum: Q8 volume 0.766, pitch -0.25, falloff 16 m, at fan 0 (100.0, -1.6, 142.9 m).
local FAN_SFX = "fan_hum"
local FAN_X, FAN_Y, FAN_Z = 409600, -6553, 585318
local FAN_VOL, FAN_FALLOFF, FAN_PITCH = 196, 65536, -64

local ANGLE_90, ANGLE_180, ANGLE_M112_5 = 1024, 2048, -1280

-- The fans' angles start at 0 when the map loads; so do the "water running" and "fan hum on" latches.
local spin = {0, 0}
local water_on = false
local fan_hum_on = false

-- PLAYER_IN_MAP_CHUNK: the C division truncates toward zero.
local function in_chunk(p, x0, x1, x2, x3)
    local c = p // CELL
    if p < 0 and c * CELL ~= p then
        c = c + 1
    end
    return (p > 0 and c + x0 == x1) or (p <= 0 and c + x2 == x3)
end

local function rot_y(name)
    local _, y = sh.world_object.rotation_get(name)
    return y
end

local function rot_y_set(name, y)
    local x, _, z = sh.world_object.rotation_get(name)
    sh.world_object.rotation_set(name, x, y, z)
end

function update(dt)
    local x, _, z = sh.player_pos()
    local coll = 0

    -- The drain's water stream runs while the player is in its cell once the key went down the drain.
    if in_chunk(x, 1, -1, 0, 0) and in_chunk(z, 1, -2, -1, -2) then
        if not water_on and sh.flag_get("M1S03_KeyDownTheDrain") then
            sh.map.water_stream_start()
            water_on = true
        end
    elseif water_on then
        sh.map.water_stream_stop()
        water_on = false
    end

    local in_fans = in_chunk(x, 1, 3, -1, 3) and in_chunk(z, 1, 4, -1, 4)
    if in_fans then
        if not fan_hum_on then
            sh.sd_call(FAN_SFX)
            fan_hum_on = true
        end
        sh.sfx_falloff_pitch_play(FAN_SFX, FAN_X, FAN_Y, FAN_Z, FAN_VOL, FAN_FALLOFF, FAN_PITCH)
    elseif fan_hum_on then
        fan_hum_on = false
        sh.sfx_stop(FAN_SFX)
    end

    if in_chunk(x, 0, 0, -1, 1) and in_chunk(z, 1, 2, -1, 2) then
        if sh.flag_get("M1S02_BathroomDoorPushedOpen") then
            rot_y_set("BathroomDoor", ANGLE_M112_5)
        else
            -- the door blocking the corpse with the shotgun in the bathroom collides
            coll = COLLISION_1
        end
    end

    if in_chunk(x, 1, 2, -1, 2) and in_chunk(z, 1, -2, -1, -2) then
        -- the doors locking the boss entrance collide until both stand at 180 degrees
        local door2 = rot_y("BossDoor2")
        if door2 ~= ANGLE_180 or rot_y("BossDoor1") ~= door2 then
            coll = coll | COLLISION_2
            sh.flag_set("M1S02_BossEntranceDoorsOpen", false)
        else
            sh.flag_set("M1S02_BossEntranceDoorsOpen", true)
        end
        coll = coll | COLLISION_1
    else
        rot_y_set("BossDoor1", sh.flag_get("M1S02_BossEntranceDoorsOpen") and ANGLE_180 or ANGLE_90)
        rot_y_set("BossDoor2", ANGLE_180)
    end

    sh.collision.flag_bits_clear(COLLISION_ALL)
    if coll ~= 0 then
        sh.collision.flag_bits_set(coll)
    end

    if not in_fans then
        return
    end
    for i, fan in ipairs(FANS) do
        -- Q12_MULT_PRECISE: the arithmetic shift right is a floor division.
        spin[i] = spin[i] + (dt * (fan.base - (sh.rng_int(0, 0x7FFF) & 0x1FF))) // 4096
        sh.world_object.rotation_set(fan.name, 0, 0, spin[i])
    end
end

-- After the objects are set up (the end of Map_WorldObjectsInit): the drain's water, two falling streams
-- of 1.6 m at x -16.4 and -14.8 m (z -51.2 m, turned 90 and 270 degrees) and a third that only ripples.
function post_init()
    local streams = {
        {x = -67174, z = -209715, yaw = 1024},
        {x = -60620, z = -209715, yaw = 3072},
    }
    for i, s in ipairs(streams) do
        local n = i - 1
        sh.map.set("water_stream.x", s.x, n)
        sh.map.set("water_stream.y", 409, n)              -- 0.1 m
        sh.map.set("water_stream.z", s.z, n)
        sh.map.set("water_stream.length", 6553, n)        -- 1.6 m
        sh.map.set("water_stream.yaw", s.yaw, n)
        sh.map.set("water_stream.falling", 0, n)
        sh.map.set("water_stream.ribbon_width", 1638, n)  -- 0.4 m
        sh.map.set("water_stream.ripple_length", 2048, n) -- 0.5 m
        sh.map.set("water_stream.ripple_speed", 204, n)   -- 0.05
        sh.map.set("water_stream.spray_width", 1638, n)   -- 0.4 m
        sh.map.set("water_stream.fade_length", 2048, n)   -- 0.5 m
        sh.map.set("water_stream.drop_speed", 204, n)     -- 0.05
    end
    sh.map.set("water_stream.drop_count", 10)
    sh.map.set("water_stream.segment_count", 2)
    sh.map.set("water_stream.length", 0, 2)
    sh.map.set("water_stream.y", 409, 2)
    sh.map.set("water_stream.ribbon_width", 1638, 2)
    sh.map.set("water_stream.spray_width", 1638, 2)
    sh.map.set("water_stream.fade_length", 2048, 2)
end

-- The room's spawn pass (the overlay's npc spawn event): after the reception arrows are marked, the
-- Grey Child of row 12 waits at (134.1, 21.3) m on Hard and stays away otherwise; after M1S02_PhoneCallDone the
-- Creepers of rows 7 and 8 (and 9 on Hard) spawn with flags 2 and 3.
function npc_spawn()
    local hard = sh.save.game_difficulty() == 1
    if sh.flag_get("MapMark_AltSchool1F_ReceptionArrows") then
        if hard then
            sh.npc.spawn_position_set(sh.chara_id("GreyChild"), 12, 549273, 87244)
        else
            sh.chara.spawn_flags_set("GreyChild", 12, 0)
        end
    end
    if sh.flag_get("M1S02_PhoneCallDone") then
        sh.chara.spawn_flags_set("Creeper", 7, 12)
        sh.chara.spawn_flags_set("Creeper", 8, 12)
        if hard then
            sh.chara.spawn_flags_set("Creeper", 9, 12)
        end
    end
end
