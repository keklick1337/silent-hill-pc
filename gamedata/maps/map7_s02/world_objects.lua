-- World objects of map7_s02: the objects are world_objects.json. This
-- script holds the map's per-tick rules after the draw: the dagger
-- flags of the operating room (cell 17, 14), the fuse box's crackle (14, 12), the scream in the corridor
-- (12, 15) and the generator hum of the machine room.

local CELL = 163840 -- 40 m
local SPARK = "electric_spark"
local SPARK_POS = {-259276, -6553, -570163} -- (-63.3, -1.6, -139.2) m
local SCREAM = "distant_scream"
local SCREAM_POS = {573440, -4915, -96256} -- (140.0, -1.2, -23.5) m
local HUM_START = "generator_start"
local HUM = "machine_hum_falloff"
local HUM_POS = {742604, -4915, -245760} -- (181.3, -1.2, -60.0) m
local SFX_NO_DIST_ATTEN = 2
local COLLISION_ALL = 0xFFFF

-- The fuse box's clock lives in the map variable fuse_box_spark_sfx_timer (events.lua drives the same
-- crackle); the set-up clears it when the map loads. machine_hum_active is outside the map's reset data.
sh.map.set("fuse_box_spark_sfx_timer", 0)

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

local function dagger_flags()
    if sh.flag_get("M7S02_PickupDaggerOfMelchior") then
        local late = sh.flag_get("M7S02_RingOfContractOnChain")
        sh.flag_set("M7S02_ChainsBloodsuckerSceneArmed", late)
        sh.flag_set("M7S02_KitchenBloodsuckerTrapArmed", not late)
    else
        sh.flag_set("M7S02_KitchenBloodsuckerTrapArmed", false)
        sh.flag_set("M7S02_ChainsBloodsuckerSceneArmed", false)
    end
end

local function fuse_box(dt)
    if not sh.flag_get("M7S01_FuseBoxPlateRemoved") or sh.flag_get("M7S02_GeneratorStopped") then
        return
    end
    local timer = sh.map.get("fuse_box_spark_sfx_timer")
    if timer ~= 0 then
        sh.map.set("fuse_box_spark_sfx_timer", math.max(0, s16(timer - dt)))
    else
        sh.sfx_flags_play(SPARK, SPARK_POS[1], SPARK_POS[2], SPARK_POS[3], 32, 0) -- volume 0.125
        sh.map.set("fuse_box_spark_sfx_timer", sh.rng_int(819, 9010)) -- 0.2 .. 2.2 s
    end
end

local function scream(z)
    if not sh.flag_get("M7S02_ScreamHeard") and z < -77824 then -- z < -19 m
        sh.flag_set("M7S02_ScreamHeard", true)
        sh.sfx_flags_play(SCREAM, SCREAM_POS[1], SCREAM_POS[2], SCREAM_POS[3], 255, SFX_NO_DIST_ATTEN)
    end
end

function post_draw()
    local dt = sh.game.delta_time()
    local x, _, z = sh.player_pos()
    local cx, cz = cell(x), cell(z)
    if cx == 17 and cz == 14 then
        dagger_flags()
    elseif cx == 14 and cz == 12 then
        fuse_box(dt)
    elseif cx == 12 and cz == 15 then
        scream(z)
    end

    -- The machine room: x in [160, 200) m, z in (-80, -40] m (the original's 40 m cell tests).
    local hum = sh.map.get("machine_hum_active")
    if x >= 4 * CELL and x < 5 * CELL and z <= -CELL and z > -2 * CELL then
        if not sh.flag_get("M7S02_GeneratorStopped") then
            if hum == 0 then
                sh.sd_call(HUM)
                sh.map.set("machine_hum_active", 1)
            elseif hum == 1 then
                sh.sfx_falloff_pitch_play(HUM_START, HUM_POS[1], HUM_POS[2], HUM_POS[3], 128, 65536, 0) -- 16 m
                sh.sfx_falloff_pitch_play(HUM, HUM_POS[1], HUM_POS[2], HUM_POS[3], 128, 65536, 0)
            end
        end
    elseif hum == 1 then
        sh.sfx_stop(HUM_START)
        sh.sfx_stop(HUM)
        sh.map.set("machine_hum_active", 0)
    end

    sh.collision.flag_bits_clear(COLLISION_ALL)
    sh.collision.flag_bits_set(0)
end

-- The scene objects of the set-up (drawn by events.lua, not by the update): the bed cover, and the 16
-- jelly beans of the jar (models BEAN1_HI..BEAN8_HI, two of each) at random places and angles inside
-- it, each with a random scatter heading and speed for their fall.
sh.world.object_model_name_set("bed_cover", "COVER_HI")
sh.map.set("bed_cover.pos.x", 572252) -- 139.71 m
sh.map.set("bed_cover.pos.y", 1228)   -- 0.3 m
sh.map.set("bed_cover.pos.z", -243916) -- -59.55 m

for i = 0, 15 do
    sh.world.object_model_name_set("jelly_beans[" .. i .. "]", "BEAN" .. ((i >> 1) + 1) .. "_HI")
    sh.map.set("jelly_beans.pos.x", 255180, i) -- 62.3 m
    sh.map.set("jelly_beans.pos.y", sh.rng_int(-5939, -5531), i) -- -1.45 .. -1.3501 m
    sh.map.set("jelly_beans.pos.z", sh.rng_int(-250142, -249570), i) -- -61.07 .. -60.93 m
    sh.map.set("jelly_beans.rot.x", sh.rng_uint(0, 4095), i)
    sh.map.set("jelly_beans.rot.y", sh.rng_uint(0, 4095), i)
    sh.map.set("jelly_beans.rot.z", sh.rng_uint(0, 4095), i)
    sh.map.set("jelly_bean_scatter_angles", sh.rng_uint(0, 4095), i)
    -- 2.2 times the sine of 45..90 degrees.
    sh.map.set("jelly_bean_scatter_speeds", (sh.math_sin(sh.rng_uint(0, 511) | 0x200) * 9011) >> 12, i)
end

-- The map's npcSpawnEvent, before the room's spawn pass: the stalker's model goes to chara group 0
-- unless its animation slot already has it; after the Key of Aratron's pickup row 3 spawns (and row 10
-- on Hard); M7S02_ElevatorPowered is the inverse of M7S02_GeneratorStopped.
local STALKER = "Stalker"

function npc_spawn()
    local stalker = sh.chara_id(STALKER)
    if sh.npc.group_anim_chara(0) ~= stalker or sh.npc.model_loaded(stalker) == 0 then
        sh.npc.anim_info_clear()
        sh.npc.load(0, STALKER, "npc_bone:0", -1, nil, nil)
        sh.npc.process_loads()
    end

    if sh.flag_get("M7S02_PickupKeyOfAratron") then
        sh.chara.spawn_flags_set(STALKER, 3, 3) -- spawn 0, 1
        if sh.save.game_difficulty() == 1 then
            sh.chara.spawn_flags_set(STALKER, 10, 11) -- spawn 0, 1, 3
            sh.chara.spawn_flags_set(STALKER, 10, 3)
        end
    end

    sh.flag_set("M7S02_ElevatorPowered", not sh.flag_get("M7S02_GeneratorStopped"))
end
