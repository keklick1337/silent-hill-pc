-- World objects of map4_s02: the sixteen fans turn about x all the
-- time, the first seven one way and the rest the other, at 90 degrees per second. Their start angles are
-- the overlay's table (map variable fan_angles, from the disc); the script keeps its own copy, as the
-- original kept 16-bit angles.

local SPEED = 1024 -- 90 degrees per second (Q3.12 turns)
local FANS = 16

local angles = {}

local function turn_to(i)
    local name = "Fan" .. i
    local _, ry, rz = sh.world_object.rotation_get(name)
    sh.world_object.rotation_set(name, angles[i], ry, rz)
end

for i = 0, FANS - 1 do
    angles[i] = sh.map.get("fan_angles", i)
    turn_to(i)
end

function update(dt)
    -- Floor division is the original's 64-bit multiply and arithmetic shift right by 12.
    local step = (dt * SPEED) // 4096
    for i = 0, FANS - 1 do
        if i < 7 then
            angles[i] = (angles[i] - step) & 0xFFFF
        else
            angles[i] = (angles[i] + step) & 0xFFFF
        end
        turn_to(i)
    end
end

-- Collision triggers and the fan hum. The original's 40 m chunk tests
-- only ever match the cells below (positive cells never match negative targets; x <= 0 truncates toward zero).
local CELL = 163840
local function trunc_cell(v)
    if v >= 0 then return v // CELL end
    return -((-v) // CELL)
end

local HUM = 1542
local hum_on = false -- reset when the map loads
-- Fan hum anchors by fan index (x, y, z in Q12); the others repeat the previous one 5 m further back in z.
local ANCHORS = {
    [0] = { 217952, -9728, 133120 },  -- Q12(53.211, -2.375, 32.5)
    [4] = { 231731, -11776, 112640 }, -- Q12(56.575, -2.875, 27.5)
    [7] = { 268083, -11776, 112640 }, -- Q12(65.45, -2.875, 27.5)
    [11] = { 281804, -9728, 133120 }, -- Q12(68.8, -2.375, 32.5)
}

local fan_update = update
function update(dt)
    fan_update(dt)
    local x, _, z = sh.player_pos()
    sh.collision.flag_bits_clear(0xFFFF)
    local xc, zc = trunc_cell(x), trunc_cell(z)
    local xnp = x <= 0
    if (xnp and xc == -2 and z > 0 and zc == 2) or (xnp and xc == -3 and z > 0 and zc == 2) then
        sh.collision.flag_bits_set(2)
    end
    if xnp and xc == -2 and z <= 0 and zc == -1 then
        if sh.flag_get("TownStairwellEntryTriggered") then
            sh.collision.flag_bits_set(4)
            if z < -278528 then -- Q12(-68)
                sh.flag_set("TownStairwellEntryTriggered", false)
            end
        else
            sh.collision.flag_bits_set(2)
        end
    end

    local balance = 0
    local ax, ay, az = 0, 0, 0
    for i = 0, FANS - 1 do
        local a = ANCHORS[i]
        if a then ax, ay, az = a[1], a[2], a[3] end
        balance = balance + sh.vc_stereo_balance(ax, ay, az)
        az = az - 20480 -- Q12(5)
    end

    local vol
    if math.abs(x - 249856) < 16384 and math.abs(z - 92160) < 20480 then -- Q12(61 +-4), Q12(22.5 +-5)
        vol = 256
    else
        local d0 = sh.math_vec2_mag_q6(x - 249856, z - 112640)
        local d1 = sh.math_vec2_mag_q6(x - 249856, z - 71680)
        local near = (d1 >= d0) and d0 or d1
        if near <= 131072 then -- Q12(32)
            vol = 256 - (near >> 9)
        else
            vol = 0
        end
    end

    if vol ~= 0 then
        if not hum_on then
            sh.sd_call(HUM)
            hum_on = true
        end
        sh.sfx_attributes_update(HUM, balance // 16, -vol, 0)
    elseif hum_on then
        sh.sfx_stop(HUM)
        hum_on = false
    end
end

-- Set-up when the map loads (the end of Map_WorldObjectsInit): the npc flags id by difficulty.
do
    local difficulty = sh.save.game_difficulty()
    sh.game.set_npc_flags_id(difficulty == -1 and 3 or (difficulty == 0 and 4 or 5)) -- Easy 3, Normal 4, Hard 5
end
