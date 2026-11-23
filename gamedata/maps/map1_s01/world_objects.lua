-- World objects of map1_s01.
--
-- The locker in the locker room (cell x -2, z 0) rattles until M1S01_LockerCatJumpedOut: at 30 Hz its door turns by a step
-- that bounces between closed (0) and -1.8 degrees, a 1 in 32 chance starting a burst, with the locker sound
-- at each bounce. With M1S01_LockerCatJumpedOut and not M1S01_LockerCatSceneDone the box behind it shows (world_objects.json); with M1S01_LockerCatSceneDone the
-- door rests at -2.9 degrees. The random numbers are the game's (sh.rng_uint), drawn in
-- the original's order: nothing before the locker in the map's update draws random numbers.
--
-- The door's angle is the record LockerDoor (the cat scene of events.lua turns it by name; LockerDoor2
-- follows it here). The silver medallion is the record SilverMedallion: its start pose depends on the piano
-- puzzle (set below when the map loads), the puzzle scene of events.lua moves it.

local CELL = 163840 -- 40 m
local STEP_30_FPS = 136 -- Q12(1 / 30)
local BOUNCE_OPEN = -20 -- Q12_ANGLE(-1.8)
local RESTING = -32 -- Q12_ANGLE(-2.9)
local SOUND_X, SOUND_Y, SOUND_Z = -240885, -5734, 73809 -- (-58.81, -1.4, 18.0198)

-- The burst's step (0 = still) and the 30 Hz gate's carry, both reset when the map loads.
local step = 0
local carry = 0

-- PLAYER_IN_MAP_CHUNK: the C division truncates toward zero.
local function in_chunk(p, x0, x1, x2, x3)
    local c = p // CELL
    if p < 0 and c * CELL ~= p then
        c = c + 1
    end
    return (p > 0 and c + x0 == x1) or (p <= 0 and c + x2 == x3)
end

local function s16(v)
    v = v & 0xFFFF
    return v >= 0x8000 and v - 0x10000 or v
end

-- The 30 Hz state machine runs once per 1/30 s of game time, whatever the frame rate.
local function tick_30_hz(dt)
    carry = carry + dt
    if carry >= STEP_30_FPS then
        carry = carry - STEP_30_FPS
        if carry > STEP_30_FPS then
            carry = 0
        end
        return true
    end
    return false
end

local function rattle(angle)
    if step == 0 and sh.rng_uint(0, 31) == 0 then -- 1 in 32 chance.
        step = -sh.rng_uint(2, 9)
    end
    if step == 0 then
        return angle
    end
    if math.abs(step) == 1 then
        angle = s16(angle + step)
    end
    angle = s16(angle + step)
    if step > 0 then
        if angle > 0 then
            sh.sfx_flags_play("locker_door_rattle", SOUND_X, SOUND_Y, SOUND_Z, 128, 0)
            angle = 0
            if step >= 8 and sh.rng_uint(0, 3) == 0 then -- 1 in 4 chance.
                step = 0
            else
                step = (sh.rng_uint(0, 3) - step) + 1
            end
            step = math.min(step, 0)
        end
    elseif angle < BOUNCE_OPEN then
        sh.sfx_flags_play("locker_door_rattle", SOUND_X, SOUND_Y, SOUND_Z, 128, 0)
        step = ~sh.rng_uint(0, 3) - step
        step = math.max(step, 1)
    end
    return angle
end

local function set_doors(angle)
    sh.world_object.rotation_set("LockerDoor", 0, angle, 0)
    sh.world_object.rotation_set("LockerDoor2", 0, angle, 0)
end

-- After the piano puzzle the medallion lies on the floor: (-98.8, 0.0, 22.8) m, turned (-90, 28.5, 0) degrees.
if sh.flag_get("M1S01_PianoPuzzleSolved") then
    sh.world_object.position_set("SilverMedallion", -404684, 0, 93388)
    sh.world_object.rotation_set("SilverMedallion", -1024, 324, 0)
end

local function door_angle()
    local _, y = sh.world_object.rotation_get("LockerDoor")
    return y
end

set_doors(door_angle())

function update(dt)
    local x, _, z = sh.player_pos()
    if not (in_chunk(x, 1, -2, -1, -2) and in_chunk(z, 0, 0, -1, 1)) then
        return
    end
    local angle = door_angle()
    if not sh.flag_get("M1S01_LockerCatJumpedOut") then
        if tick_30_hz(dt) then
            angle = rattle(angle)
        end
    elseif sh.flag_get("M1S01_LockerCatSceneDone") then
        angle = RESTING
    end
    set_doors(angle)
end

-- Called by the overlay after the objects are drawn: the sound once the player passes the corridor end
-- with the chase camera (M1S01_CorridorEndSoundPlayed makes it one-shot).
local SFX_NO_DIST_ATTEN = 2
local FLAG_X_LIMIT = -567705 -- Q12(-138.5999)
local SND_POS = {584089, -1638, 240844} -- (142.6, -0.4, 58.8)

function post_draw()
    if sh.flag_get("M1S01_CorridorEndSoundPlayed") then
        return
    end
    local x, _, z = sh.player_pos()
    if in_chunk(x, 1, -4, -1, -4) and in_chunk(z, 1, 2, -1, 2) and x < FLAG_X_LIMIT
        and sh.camera.is_chase() == 1 then
        sh.sfx_flags_play("school_corridor_scare", SND_POS[1], SND_POS[2], SND_POS[3], 255, SFX_NO_DIST_ATTEN)
        sh.flag_set("M1S01_CorridorEndSoundPlayed", true)
    end
end

-- The room's spawn pass (the overlay's npc spawn event). Below Hard, the Grey Children of row 0 come after
-- the chemical is taken and of row 1 after the gold medallion (row 2 too on Normal); after M1S01_LockerCatSceneDone row 3 comes,
-- and row 7 stays away on Easy.
function npc_spawn()
    local difficulty = sh.save.game_difficulty() -- -1 Easy, 0 Normal, 1 Hard
    if difficulty ~= 1 then
        if sh.flag_get("M1S01_PickupChemical") then
            sh.chara.spawn_flags_set("GreyChild", 0, 3)
        end
        if sh.flag_get("M1S01_PickupGoldMedallion") then
            sh.chara.spawn_flags_set("GreyChild", 1, 3)
            if difficulty == 0 then
                sh.chara.spawn_flags_set("GreyChild", 2, 7)
            end
        end
    end
    if sh.flag_get("M1S01_LockerCatSceneDone") then
        sh.chara.spawn_flags_set("GreyChild", 3, 3)
        if difficulty == -1 then
            sh.chara.spawn_flags_set("GreyChild", 7, 0)
        end
    end
end
