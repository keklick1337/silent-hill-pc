-- Per-tick rules of map1_s03 (
-- section 10): the locker room's cameras, the locker doors (their idle rattle draws the game's random
-- numbers) and boxes, the third-floor chimes. The draw follows this update. The drain valve
-- is the record DrainValve, which events.lua turns.

local CELL = 163840 -- 40 m

-- Angles (Q3.12, 4096 = 360 degrees) of the locker doors.
local A_M11_3, A_M168_8, A_2_9, A_M157_45, A_M157_4 = -128, -1920, 32, -1791, -1790
local A_M1_8, A_M1_45, A_M2_9, A_1_45, A_M5_7 = -20, -16, -32, 16, -64
local A_M39_4, A_M67_5, A_M157_5 = -448, -768, -1792

-- Sound positions: the locker (-58.81, -1.4, 18.02) m, the side door (-61.0, -1.05, 20.97) m and the
-- chimes (-96.1, -1.2, 141.8) m.
local LOCKER = {-240885, -5734, 73809}
local SIDE_DOOR = {-249856, -4300, 85893}
local CHIMES = {-393625, -4915, 580812}
local NO_DIST_ATTEN = 2
local VOL_FULL = 255 -- Q8_CLAMPED(1.0)
local TIMESTEP_30_FPS = 136

-- The locker's swing speed, timer and step, and the chime count live outside the map's reset data in the
-- original (they keep their values across map loads for the whole process).
local K_SPEED, K_TIMER, K_STEP = "map1_s03.locker_speed", "map1_s03.locker_timer", "map1_s03.locker_step"
local K_CHIMES = "map1_s03.chime_count"

-- Reset with the map: the locker camera's state, the chime clock and the door swing's carry.
local cam_state = 0
local chime_clock = 0
local rot_carry = 0

local function in_chunk(p, x0, x1, x2, x3)
    local c = p // CELL
    if p < 0 and c * CELL ~= p then
        c = c + 1
    end
    return (p > 0 and c + x0 == x1) or (p <= 0 and c + x2 == x3)
end

-- C's truncating division and 16-bit wrap.
local function cdiv(a, b)
    local q = a // b
    if q < 0 and q * b ~= a then
        q = q + 1
    end
    return q
end

local function s16(v)
    return ((v + 32768) & 0xFFFF) - 32768
end

local function rot_y(name)
    local _, y = sh.world_object.rotation_get(name)
    return y
end

local function rot_y_set(name, y)
    local x, _, z = sh.world_object.rotation_get(name)
    sh.world_object.rotation_set(name, x, y, z)
end

local function cam_pos(x, y, z, warp)
    sh.camera_pos(0, 0, 0, x, y, z, 0, 0, 0, 0, warp)
end

local function cam_look(x, y, z, warp)
    sh.camera_look(0, 0, 0, x, y, z, 0, 0, 0, 0, warp)
end

local function play(name, pos, vol, flags)
    sh.sfx_flags_play(name, pos[1], pos[2], pos[3], vol, flags)
end

-- The locker: the side door swings open and bounces (M1S03_LockerBodySceneStarted/106), the locker door rattles until the
-- cat scene (M1S03_LockerDoorPulled/102), then opens by steps; the boxes inside show with them.
local function locker_update(dt)
    local show2, show1 = false, false

    if dt == 0 then
        show2 = sh.flag_get("M1S03_LockerBodySceneStarted")
        show1 = sh.flag_get("M1S03_LockerDoorPulled") and sh.flag_get("M1S03_LockerDoorOpened")
    else
        local speed = sh.session.get(K_SPEED)
        local timer = sh.session.get(K_TIMER)
        local step = sh.session.get(K_STEP)

        if sh.flag_get("M1S03_LockerBodySceneStarted") and not sh.flag_get("M1S03_LockerBodyFellOut") then
            local side = rot_y("LockerDoor2")
            if step == 6 then
                sh.pad.effect(18)
                play("otherworld_locker_door_burst", SIDE_DOOR, VOL_FULL, NO_DIST_ATTEN)
                speed = A_M11_3
                step = 7
                timer = 0
            end
            -- the swing is applied per rendered frame, scaled to the frame time
            side = s16(side + cdiv(dt * speed, TIMESTEP_30_FPS))
            if speed == A_M11_3 then
                if side < A_M168_8 then
                    side = A_M168_8
                    speed = A_2_9
                    play("otherworld_locker_door_swing", SIDE_DOOR, VOL_FULL, NO_DIST_ATTEN)
                end
            elseif (side < A_M168_8 and speed < 0) or (side >= A_M157_45 and speed > 0) then
                speed = (-speed) // 2 -- arithmetic shift right
                if math.abs(speed) == 1 then
                    speed = 0
                end
            end
            if speed == 0 then
                sh.flag_set("M1S03_LockerBodyFellOut", true)
            end
            rot_y_set("LockerDoor2", side)
            show2 = true
        elseif sh.flag_get("M1S03_LockerBodyFellOut") then
            rot_y_set("LockerDoor2", A_M157_4)
            show2 = true
        end

        local door = rot_y("LockerDoor1")
        if not sh.flag_get("M1S03_LockerDoorPulled") or not sh.flag_get("M1S03_LockerDoorOpened") then
            -- the idle rattle: 1 in 32 to start, random bounces back
            if speed == 0 then
                if sh.rng_uint(0, 31) == 0 then
                    speed = -sh.rng_uint(2, 9)
                end
            end
            if speed ~= 0 then
                if math.abs(speed) == 1 then
                    door = s16(door + speed)
                end
                door = s16(door + speed)
                if speed > 0 then
                    if door > 0 then
                        play("locker_door_rattle", LOCKER, 128, 0)
                        door = 0
                        if speed >= 8 and sh.rng_uint(0, 3) == 0 then
                            speed = 0
                        else
                            speed = (sh.rng_uint(0, 3) - speed) + 1
                        end
                        speed = math.min(speed, 0)
                    end
                elseif door < A_M1_8 then
                    play("locker_door_rattle", LOCKER, 128, 0)
                    speed = ~sh.rng_uint(0, 3) - speed
                    speed = math.max(speed, 1)
                end
            end
        elseif not sh.flag_get("M1S03_LockerOpened") or step == 5 then
            if step == 0 then
                speed = A_M1_45
                timer = 0
                step = 1
            end
            if step == 1 then
                door = s16(door + speed)
                if door < A_M2_9 then
                    sh.pad.effect_level(18, 0x60)
                    play("otherworld_locker_door_swing", LOCKER, 196, NO_DIST_ATTEN)
                    door = A_M2_9
                    speed = A_1_45
                    step = 2
                end
            elseif step == 2 then
                speed = speed - 2
                door = s16(door + speed)
                if door > 0 then
                    play("otherworld_locker_door_swing", LOCKER, 196, NO_DIST_ATTEN)
                    speed = -40960 -- -10.0
                    door = 0
                    step = 3
                end
            elseif step == 3 then
                timer = s16(timer + dt)
                if timer > 12288 then -- 3.0 s
                    step = 4
                end
            elseif step == 4 or step == 5 then
                speed = speed + (dt * 16384) // 4096 -- Q12_MULT_PRECISE(dt, 4.0)
                local v = speed
                if v > -8192 then -- -2.0
                    v = -8192
                end
                speed = v
                -- the speed is dt-scaled per rendered frame with a carry of the truncated part
                rot_carry = rot_carry + cdiv(v * dt, TIMESTEP_30_FPS)
                local turn = rot_carry // 4096 -- arithmetic shift right
                rot_carry = rot_carry - (turn << 12)
                door = s16(door + turn)
                show1 = true
                if step == 4 and door < A_M5_7 then
                    play("locker_door_burst_open", LOCKER, 128, 0)
                    step = 5
                end
                if door < A_M39_4 then
                    sh.flag_set("M1S03_LockerOpened", true)
                end
                if sh.flag_get("M1S03_LockerInspectDone") or sh.screen_fade_finished() or door < -0x300 then
                    door = A_M67_5
                    step = 6
                end
            end
        else
            door = A_M157_5
            show1 = true
            if not sh.flag_get("M1S03_LockerBodySceneStarted") then
                step = 6
            end
        end

        rot_y_set("LockerDoor1", door)
        rot_y_set("LockerDoor0", door)
        sh.session.set(K_SPEED, speed)
        sh.session.set(K_TIMER, timer)
        sh.session.set(K_STEP, step)
    end

    -- shown: by the data again (nil); hidden: false
    if show2 then sh.world_object.visible_set("LockerBox2", nil) else sh.world_object.visible_set("LockerBox2", false) end
    if show1 then sh.world_object.visible_set("LockerBox1", nil) else sh.world_object.visible_set("LockerBox1", false) end
end

function update(dt)
    local x, _, z = sh.player_pos()

    if in_chunk(x, 1, -2, -1, -2) and in_chunk(z, 0, 0, -1, 1) then
        locker_update(dt)
        x, _, z = sh.player_pos()

        if x < -235520 and z < 88473 then -- -57.5 m, 21.6 m
            if not sh.flag_get("M1S03_LockerInspectStarted") then
                if x > -245760 then -- -60.0 m
                    if cam_state == 2 then
                        cam_state = 0
                    end
                    cam_pos(-250593, -12288, z + 2007, cam_state == 0)
                    cam_look(-239534, -1269, z - 2949, cam_state == 0)
                    cam_state = 1
                else
                    cam_pos(-239534, -7700, 91340, true)
                    cam_look(-249364, -5038, 78479, true)
                    cam_state = 2
                end
            elseif sh.flag_get("M1S03_LockerBodySceneDone") then
                if z > 76185 then -- 18.6 m
                    cam_pos(-246865, -1679, z - 9216, cam_state == 0)
                    cam_look(-244736, -4464, z + 6758, cam_state == 0)
                    cam_state = 1
                else
                    cam_state = 0
                    cam_pos(-246374, -13516, 84049, true)
                    cam_look(-245678, -3481, 71106, true)
                end
            end
        else
            if cam_state == 2 then
                cam_state = 1
            end
            sh.camera_auto_return(cam_state ~= 0)
            cam_state = 0
        end
    end

    if sh.flag_get("M1S03_LockerInspectDone") and not sh.flag_get("M1S03_LockerBodySceneStarted") then
        local _, _, pz = sh.player_pos()
        cam_pos(-248340, -16957, 71352, true)
        if pz < 75776 then -- 18.5 m
            cam_look(-245760, 0, 75776, false)
        else
            cam_look(-245760, 0, pz, false)
        end
        cam_state = 0
    end

    x, _, z = sh.player_pos()
    if not sh.flag_get("M1S03_ChimesPlayed") and in_chunk(x, 1, -3, -1, -3) and in_chunk(z, 1, 4, -1, 4) then
        if x < -409600 then -- -100.0 m
            chime_clock = chime_clock + dt
        elseif x < -405504 then -- -99.0 m
            chime_clock = chime_clock + (dt * (x + 409600)) // 4096
        end
        local n = sh.session.get(K_CHIMES)
        if (n == 0 and chime_clock > 10240) or (n == 1 and chime_clock > 16384) or (n == 2 and chime_clock > 18841)
            or (n == 3 and chime_clock > 30310) or (n == 4 and chime_clock > 35225) or (n == 5 and chime_clock > 38092) then
            play("school_clock_chime", CHIMES, 255, NO_DIST_ATTEN)
            sh.session.set(K_CHIMES, n + 1)
        end
    elseif sh.session.get(K_CHIMES) ~= 0 then
        sh.flag_set("M1S03_ChimesPlayed", true)
    end
end

-- The room's spawn pass (the overlay's npc spawn event). Grey Children come after the classroom key (rows 9
-- and 10, not on Easy) and the library reserve key (row 12 not on Easy, 13 and 11 on Hard, 14 and 15 always);
-- the Creepers of rows 0..4 only while Harry is healthy enough (the margin is 50 on Easy, 25 on Normal).
function npc_spawn()
    local difficulty = sh.save.game_difficulty() -- -1 Easy, 0 Normal, 1 Hard
    if sh.flag_get("M1S02_PickupClassroomKey") and difficulty ~= -1 then
        sh.chara.spawn_flags_set("GreyChild", 9, 3)
        sh.chara.spawn_flags_set("GreyChild", 10, 3)
    end
    if sh.flag_get("M1S03_PickupLibraryReserveKey") then
        if difficulty ~= -1 then
            sh.chara.spawn_flags_set("GreyChild", 12, 3)
        end
        if difficulty == 1 then
            sh.chara.spawn_flags_set("GreyChild", 13, 3)
            sh.chara.spawn_flags_set("GreyChild", 11, 7)
        end
        sh.chara.spawn_flags_set("GreyChild", 14, 11)
        sh.chara.spawn_flags_set("GreyChild", 15, 3)
    end

    local health = sh.chara_field_get(0, "health")
    if difficulty == -1 then
        health = health - 204800 -- 50.0
    end
    if difficulty == 0 then
        health = health - 102400 -- 25.0
    end
    local function creepers(rows, above)
        for _, row in ipairs(rows) do
            sh.chara.spawn_flags_set("Creeper", row, health > above and 3 or 0)
        end
    end
    creepers({0}, 81920)     -- 20.0
    creepers({1, 2}, 245760) -- 60.0
    creepers({3, 4}, 327680) -- 80.0
end
