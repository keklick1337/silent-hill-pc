-- World objects of map7_s01: the load-time choices of the original's
-- set-up. Once the stone of time has dropped into the water (M7S01_StoneOfTimeReleased) it lies there as STONE3_H; once the
-- sliding doors have opened (M7S01_SlidingDoorsOpened) their leaves stand at the far end. Afterwards events.lua moves these
-- objects through the map variables time_stone, lisa_door and sliding_doors.

if sh.flag_get("M7S01_StoneOfTimeReleased") then
    sh.world_object.model_set("TimeStone", "STONE3_H")
    sh.world_object.position_set("TimeStone", -571392, -204, -573440) -- -139.5, -0.05, -140.0
    sh.world_object.rotation_set("TimeStone", 0, 1024, -1024)       -- 0, 90, -90 degrees
end

if sh.flag_get("M7S01_SlidingDoorsOpened") then
    for i = 0, 5 do
        sh.world_object.position_set("SlidingDoor" .. i, -413286, 0, -249446) -- -100.9, 0, -60.9
    end
end

-- The map's set-up and per-tick rules after the draw: the ghost of
-- Alessa in the corridor (cell 14, 15), the hospital ambience by the Key of Phaleg (11, 15), the ghost fading
-- out at the far end (18, 15) and the electric wire's crackle (14, 12).

local CELL = 163840 -- 40 m
local GHOST = "GhostChildAlessa"
local GHOST_SLOT = 1 -- the character slot of npcs[0]
local GHOST_VOICE = "nowhere_hospital_room_voice"
local AMBIENCE = "nowhere_room_ambience"
local AMBIENCE_POS = {-740556, -4096, -81920} -- (-180.8, -1.0, -20.0) m
local SPARK = "electric_spark"
local SPARK_POS = {-259276, -6553, -570163} -- (-63.3, -1.6, -139.2) m
local COLLISION_ALL = 0xFFFF

-- State of the original that the set-up resets: the ambience clock (s32) and the ghost's fade (s16).
-- The wire's clock is the map variable wire_spark_sfx_timer (events.lua drives the same crackle).
sh.map.set("wire_spark_sfx_timer", 0)
local ambience_clock = sh.rng_int(0, 16383) -- 0 .. 4.0 s
local ghost_fade = 0

-- The spawn rows of the hard and easy game: row 0 of the overlay's spawn table, written directly.
do
    local difficulty = sh.save.game_difficulty()
    if difficulty == -1 then -- easy
        sh.map.spawn_info_flags_set(0, 8, 0)
    end
    if difficulty == 1 then -- hard
        sh.map.spawn_info_flags_set(0, 9, 27)  -- spawn 0, 1, 3, 4
        sh.map.spawn_info_flags_set(0, 11, 23) -- spawn 0, 1, 2, 4
    end
end

local function s16(v)
    v = v & 0xFFFF
    return v >= 0x8000 and v - 0x10000 or v
end

local function tdiv(a, b) -- C integer division: truncates toward zero
    local q = a // b
    if q < 0 and q * b ~= a then
        q = q + 1
    end
    return q
end

-- The original's cell index: truncated toward zero, then +17 above zero and +15 at or below.
local function cell(p)
    local c = tdiv(p, CELL)
    return p > 0 and c + 17 or c + 15
end

local function is_ghost()
    return sh.npc.chara_id(GHOST_SLOT) == sh.chara_id(GHOST)
end

local function ghost_corridor(dt)
    if not sh.flag_get("M7S01_GhostAlessaCorridorPassed") then
        if not is_ghost() then
            sh.chara_spawn(GHOST, 0, -245760, -81920, 2048, 3) -- (-60, -20) m, facing 180 deg
            sh.xa_preload(GHOST_VOICE)
        elseif sh.player_pos() > -253952 then -- x > -62 m
            sh.flag_set("M7S01_GhostAlessaCorridorPassed", true)
            sh.chara_props_reset(GHOST_SLOT)
            sh.event_waypoint_set(true, 1, 0, 1024, -245760, -143360) -- 90 deg, (-60, -35) m
            sh.sd_call(GHOST_VOICE)
        end
        return
    end
    if not is_ghost() then
        return
    end
    sh.entity.set(GHOST_SLOT, "npc_move_distance", 5120) -- 1.25 m
    sh.npc.path_walk_no_wait(GHOST_SLOT, 2, 1)

    -- The ghost fades while her voice plays and once it has ended (0.6 per second).
    local t = 0
    local stream = sh.sound.streaming()
    if stream == 1 then
        local z = sh.npc.pos_z(GHOST_SLOT)
        if z < -98304 then -- -24 m
            t = tdiv(-98304 - z, 5)
            ghost_fade = s16(t < ghost_fade and ghost_fade or t)
        else
            ghost_fade = stream
        end
    elseif ghost_fade > 0 then
        ghost_fade = s16(ghost_fade + (dt * 2457) // 4096) -- Q12_MULT_PRECISE(dt, 0.6)
        t = ghost_fade
    end
    t = math.max(0, math.min(t, 4096))
    sh.npc.set_aux_timer(GHOST_SLOT, t)
    if sh.npc.pos_z(GHOST_SLOT) < -122880 or sh.npc.aux_timer(GHOST_SLOT) == 4096 then -- z < -30 m
        sh.npc.clear(GHOST_SLOT, 0, 0)
    end
end

local function ambience(dt)
    if sh.flag_get("M7S01_PickupKeyOfPhaleg") then
        return
    end
    if ambience_clock ~= 0 then
        ambience_clock = math.max(0, ambience_clock - dt)
    else
        sh.sfx_flags_play(AMBIENCE, AMBIENCE_POS[1], AMBIENCE_POS[2], AMBIENCE_POS[3], 128, 0) -- volume 0.5
        ambience_clock = sh.rng_int(8192, 18431) -- 2.0 .. 4.5 s
    end
end

local function ghost_far_end(dt)
    if is_ghost() then
        sh.npc.set_aux_timer(GHOST_SLOT, sh.npc.aux_timer(GHOST_SLOT) + (dt * 819) // 4096) -- 0.2 per second
        if sh.npc.aux_timer(GHOST_SLOT) > 4096 then
            sh.npc.clear(GHOST_SLOT, 0, 0)
        end
    end
end

local function wire(dt)
    if not sh.flag_get("M7S01_FuseBoxPlateRemoved") or sh.flag_get("M7S02_GeneratorStopped") then
        return
    end
    local timer = sh.map.get("wire_spark_sfx_timer")
    if timer ~= 0 then
        sh.map.set("wire_spark_sfx_timer", math.max(0, s16(timer - dt)))
        return
    end
    sh.sfx_flags_play(SPARK, SPARK_POS[1], SPARK_POS[2], SPARK_POS[3], 32, 0) -- volume 0.125
    sh.map.set("wire_spark_sfx_timer", sh.rng_int(819, 9010)) -- 0.2 .. 2.2 s
end

function post_draw()
    local dt = sh.game.delta_time()
    local x, _, z = sh.player_pos()
    local cx, cz = cell(x), cell(z)
    if cz == 15 and cx == 14 then
        ghost_corridor(dt)
    elseif cz == 15 and cx == 11 then
        ambience(dt)
    elseif cz == 15 and cx == 18 then
        ghost_far_end(dt)
    elseif cz == 12 and cx == 14 then
        wire(dt)
    end
    sh.collision.flag_bits_clear(COLLISION_ALL)
    sh.collision.flag_bits_set(0)
end

-- The map's npcSpawnEvent, before the room's spawn pass. Chara group 0 is the ghost of Alessa until
-- M7S01_GhostAlessaCorridorPassed, then the puppet nurse; its model is loaded (freeing every other) unless group 0's animation
-- slot already has it. Then the nurses' rows: 2 (beyond Easy) and 3 after the stone of time's pickup,
-- else 0 and 1 after M7S01_GhostAlessaCorridorPassed; 12..14 after M7S01_PickupKeyOfOphiel beyond Easy.
local NURSE = "PuppetNurse"
local FREE_ALL = 255 -- CHARA_FORCE_FREE_ALL

local function group0_load(name)
    sh.npc.anim_info_clear()
    sh.npc.load(0, name, "npc_bone:0", FREE_ALL, nil, nil)
    sh.npc.process_loads()
end

function npc_spawn()
    local group0 = sh.npc.group_anim_chara(0)
    if not sh.flag_get("M7S01_GhostAlessaCorridorPassed") then
        local ghost = sh.chara_id(GHOST)
        if group0 ~= ghost or sh.npc.model_loaded(ghost) == 0 then
            group0_load(GHOST)
        end
    else
        local nurse = sh.chara_id(NURSE)
        if group0 ~= nurse or sh.npc.model_loaded(nurse) == 0 then
            sh.map.chara_group_set(0, nurse)
            group0_load(NURSE)
        end
    end

    local easy = sh.save.game_difficulty() == -1
    if sh.flag_get("M7S01_PickupStoneOfTime") then
        if not easy then
            sh.chara.spawn_flags_set(NURSE, 2, 27) -- spawn 0, 1, 3, 4
        end
        sh.chara.spawn_flags_set(NURSE, 3, 27)
    elseif sh.flag_get("M7S01_GhostAlessaCorridorPassed") then
        sh.chara.spawn_flags_set(NURSE, 0, 19) -- spawn 0, 1, 4
        sh.chara.spawn_flags_set(NURSE, 1, 19)
    end

    if sh.flag_get("M7S01_PickupKeyOfOphiel") and not easy then
        sh.chara.spawn_flags_set(NURSE, 12, 19) -- spawn 0, 1, 4
        sh.chara.spawn_flags_set(NURSE, 13, 23) -- spawn 0, 1, 2, 4
        sh.chara.spawn_flags_set(NURSE, 14, 27) -- spawn 0, 1, 3, 4
    end
end
