-- Setup and per-tick rules of map3_s01: the map effects of each
-- area (with the elevator shaft's light and its doors closed on entering it) and the generator's hum.
-- The elevator panel in events.lua slides the doors through the map variable sliding_doors (bound to the
-- world objects); generator_make_noise is also written by the generator switch there.

local CELL = 163840 -- 40 m
local DOOR_Z = 78233 -- 19.1 m
local DOORS = {"ElevatorDoor1Left", "ElevatorDoor1Right", "ElevatorDoor3Left", "ElevatorDoor3Right",
               "ElevatorDoor2Left", "ElevatorDoor2Right"}
-- The generator's position (140.8, -1.2, -20.0 m).
local HUM_X, HUM_Y, HUM_Z = 576716, -4915, -81920

-- The map's chunk test (PLAYER_IN_MAP_CHUNK): the index truncates toward zero; the offsets differ by sign.
local function in_chunk(p, off_pos, want_pos, off_neg, want_neg)
    local idx = p // CELL
    if p < 0 and idx * CELL ~= p then
        idx = idx + 1
    end
    if p > 0 then
        return idx + off_pos == want_pos
    end
    return idx + off_neg == want_neg
end

local area = 0
sh.map.set("generator_make_noise", 0)

function update(dt)
    local x, _, z = sh.player_pos()

    if in_chunk(x, 1, 2, -1, 2) and in_chunk(z, 1, -1, 0, 0) then
        if area ~= 1 then
            sh.world.effects_update(7, 4)
            sh.light.flashlight_off()
            area = 1
        end
    end
    if (in_chunk(x, 0, 0, -1, 1) and in_chunk(z, 1, -1, 0, 0)) or
       (in_chunk(x, 1, 3, -1, 3) and in_chunk(z, 0, 0, -1, 1)) then
        if area ~= 2 then
            sh.world.effects_update(13, 13)
            sh.light.flashlight_off()
            area = 2
        end
    end
    if in_chunk(x, 0, 0, -1, 1) and in_chunk(z, 0, 0, -1, 1) then
        if area ~= 3 then
            sh.world.effects_update(2, 2)
            area = 3
        end
    end
    -- The elevator shaft: its own light, the water and the doors back at the shaft.
    if in_chunk(x, 1, 2, -1, 2) and in_chunk(z, 0, 0, -1, 1) then
        if area ~= 4 then
            area = 4
            sh.world.effects_update(4, 4)
            sh.light_bones_clear()
            sh.light_pos_set(242073, -12288, DOOR_Z) -- 59.1, -3.0, 19.1 m
            sh.light_rot_set(-1024, 0, 0)            -- -90 degrees
            sh.light_intensity_set(2867)             -- 0.7
            sh.world.water_enable()
            for i = #DOORS, 1, -1 do
                local dx, dy = sh.world_object.position_get(DOORS[i])
                sh.world_object.position_set(DOORS[i], dx, dy, DOOR_Z)
            end
        end
    end
    if in_chunk(x, 1, -1, 0, 0) and in_chunk(z, 0, 0, -1, 1) then
        if area ~= 5 then
            sh.world.effects_update(2, 2)
            area = 5
        end
    end
    if in_chunk(x, 1, -1, 0, 0) and in_chunk(z, 1, -1, 0, 0) then
        if area ~= 6 then
            sh.world.effects_update(2, 2)
            area = 6
        end
    end

    if in_chunk(x, 1, 4, -1, 4) and in_chunk(z, 1, -1, 0, 0) then
        if sh.flag_get("M3S01_GeneratorOn") then
            local noise = sh.map.get("generator_make_noise")
            if noise == 0 then
                sh.sd_call("machine_hum_falloff")
                sh.map.set("generator_make_noise", 1)
            elseif noise == 1 then
                sh.sfx_falloff_pitch_play("generator_start", HUM_X, HUM_Y, HUM_Z, 128, 65536, 0)
                sh.sfx_falloff_pitch_play("machine_hum_falloff", HUM_X, HUM_Y, HUM_Z, 128, 65536, 0)
            end
        end
    elseif sh.map.get("generator_make_noise") == 1 then
        sh.sfx_stop("generator_start")
        sh.sfx_stop("machine_hum_falloff")
        sh.map.set("generator_make_noise", 0)
    end
end
