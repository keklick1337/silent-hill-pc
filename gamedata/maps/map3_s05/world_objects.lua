-- World objects of map3_s05.
-- Cabinet: pushed aside once (flag M3S05_CabinetPushed) it starts at x = 61.25 m; the push scene in
-- events.lua slides it through the map variable cabinet (bound to the object).
-- ElevatorDoor*: the elevator panel in events.lua slides them open through the map variable sliding_doors;
-- in gameplay, with the player at the shaft, they are closed again (z back to 59.1 m), as the map's update
-- did before drawing them.

local CELL = 163840          -- 40 m
local DOOR_Z = 242073        -- 59.1 m
local CABINET_PUSHED_X = 250880 -- 61.25 m
local DOORS = {"ElevatorDoor1Left", "ElevatorDoor1Right", "ElevatorDoor3Left", "ElevatorDoor3Right",
               "ElevatorDoor2Left", "ElevatorDoor2Right"}

if sh.flag_get("M3S05_CabinetPushed") then
    local _, y, z = sh.world_object.position_get("Cabinet")
    sh.world_object.position_set("Cabinet", CABINET_PUSHED_X, y, z)
end

-- The cell index the C code computes: integer division truncates toward zero.
local function cell(p)
    local c = p // CELL
    if p < 0 and c * CELL ~= p then
        c = c + 1
    end
    return c
end

-- This map's shifted cell: +1 for a positive position, -1 otherwise.
local function shifted(p)
    if p > 0 then
        return cell(p) + 1
    end
    return cell(p) - 1
end

function update(dt)
    local x, _, z = sh.player_pos()
    if shifted(x) ~= 1 or shifted(z) ~= 2 or sh.event.sys_state() ~= 0 then
        return
    end
    local _, _, first_z = sh.world_object.position_get(DOORS[1])
    if first_z == DOOR_Z then
        return
    end
    for _, name in ipairs(DOORS) do
        local dx, dy = sh.world_object.position_get(name)
        sh.world_object.position_set(name, dx, dy, DOOR_Z)
    end
end

-- Per-tick rules of the map: the cabinet's collision bits, the machine
-- hum of the next room and the random thud in the room beyond it.
local COLLISION_ALL, COLLISION_1, COLLISION_2 = 0xFFFF, 2, 4
-- The hum's position (140.8, -1.2, -20.0 m).
local HUM_X, HUM_Y, HUM_Z = 576716, -4915, -81920
local NO_DIST_ATTEN = 2

local hum_on = false
local thud_timer = 0

local update_doors = update
function update(dt)
    update_doors(dt)

    local x, _, z = sh.player_pos()
    local cx, cz = shifted(x), shifted(z)
    local coll = 0
    sh.collision.flag_bits_clear(COLLISION_ALL)
    if cx == 2 and cz == -1 then
        if not sh.flag_get("M3S05_CabinetPushed") then
            coll = coll | COLLISION_1
            sh.flag_set("M3S05_CabinetNotPushedYet", true)
        else
            coll = coll | COLLISION_2
            sh.flag_set("M3S05_CabinetNotPushedYet", false)
        end
    end

    if cx == 4 and cz == -1 then
        if not hum_on then
            sh.sd_call("machine_hum_falloff")
            hum_on = true
        end
        sh.sfx_falloff_pitch_play("machine_hum_falloff", HUM_X, HUM_Y, HUM_Z, 128, 65536, 0)
    elseif hum_on then
        sh.sfx_stop("machine_hum_falloff")
        hum_on = false
    end
    sh.collision.flag_bits_set(coll)

    if cx == 4 and cz == -2 then
        if thud_timer > 0 then
            thud_timer = math.max(thud_timer - sh.game.delta_time(), 0)
        end
        if thud_timer == 0 and sh.rng.uint(0, 63) == 0 then -- 1 in 64 chance
            thud_timer = 3276 -- 0.8 s
            -- The original sets x twice and never z (stale stack value); the rolls keep their order.
            sh.rng.int(563200, 583679)
            local y = -sh.rng.int(0, 8191)
            local sx = -232652 - sh.rng.int(0, 26213)
            sh.sfx_flags_play("otherworld_basement_random_noise", sx, y, 0, sh.rng.uint(192, 255), NO_DIST_ATTEN)
        end
    end
end
