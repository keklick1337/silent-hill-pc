-- The Air Screamer of the cafe scenes is NPC slot 1 (chara slot 1). Its clip changes are the
-- overlay's own: the status is set, then the clip restarts at its first keyframe.
local AIR_SCREAMER = 1
local CAFE2_DMS_BUF = 11

local function S16(v)
    v = v & 0xFFFF
    if v >= 0x8000 then v = v - 0x10000 end
    return v
end

local function AirScreamerClipRestart(status, stateStep)
    sh.npc.set_anim_status(AIR_SCREAMER, status)
    local k = sh.npc.anim_start_keyframe(AIR_SCREAMER)
    sh.npc.set_state_step(AIR_SCREAMER, stateStep)
    sh.npc.set_anim_keyframe(AIR_SCREAMER, k)
    sh.npc.set_anim_time(AIR_SCREAMER, k * 4096)
end

-- Steps 1, 3 and 5 of the bird's attack move on to the next step.
local function AirScreamerAttackStep()
    local s = sh.npc.state_step(AIR_SCREAMER)
    if s == 1 or s == 3 or s == 5 then
        sh.npc.set_state_step(AIR_SCREAMER, s + 1)
    end
end

-- Map_WorldObjectsInit (this chunk runs when the map loads, in place of the overlay's init):
-- the radio's hiss level, and after the cafe scene (until the fight, M0S01_AirScreamerFightStarted) the Air Screamer;
-- once the town map is taken it waits off-screen at (-10, 280), gliding, for the fly-by along
-- the BIRD track of CAFE2.DMS. Q12(1048566.0f) of the original is -10.0 after the PSX wrap.
sh.map.set("radio_static_volume", 224)
if sh.flag_get("M0S01_CafeCutsceneStarted") and not sh.flag_get("M0S01_AirScreamerFightStarted") then
    sh.sys_flags_set(sh.sys_flags_get() | 0x220) -- SysFlag_5 | SysFlag_9
    sh.chara_load(0, "AirScreamer", 255) -- force free all
    if sh.flag_get("M0S01_PickupMap") then
        sh.chara_process_loads()
        sh.chara_spawn("AirScreamer", 0, -40960, 1146880, 0, 12) -- (-10.0, 280.0)
        -- The original's damage reaction: one behaviour update at once, then the glide clip.
        sh.npc.behavior_update(AIR_SCREAMER)
        AirScreamerClipRestart(35, 3) -- Glide, active
        sh.load.file("ANIM/CAFE2.DMS", CAFE2_DMS_BUF)
        sh.load.wait()
        sh.cutscene.prepare(CAFE2_DMS_BUF)
    end
end
if sh.flag_get("M2S00_DarknessFell") and not sh.flag_get("M1S06_WokeUpAfterBoss") then
    sh.map_effects_init(6, 3)
end
sh.map.set("cutscene_timer", 0)
local flybyWait = 0 -- s16, like the overlay's counter

-- The bird's fly-by after the town map is taken: Harry stands where the pickup left him until he
-- moves or 7.5 s pass (M0S01_AirScreamerFlybyStarted), then the bird flies its BIRD track at its clip's speed for up to
-- 25 s; walking 0.7 away or the end of the track ends it (M0S01_AirScreamerFlybyDone) and sends the bird back to
-- (y -10, z 280). While it flies, air_screamer_flyby tells its behaviour to only flap the wings.
-- A bird that is not there (a save loaded after the pickup) skips the scene.
local PICKUP_X, PICKUP_Z = 18784, 1094800 -- (4.586, 267.2852)
local FLYBY_END = 102400 -- 25.0

local function BirdFlyBy()
    if not sh.flag_get("M0S01_PickupMap") or sh.flag_get("M0S01_AirScreamerFlybyDone") then
        return
    end
    if sh.npc.chara_id(AIR_SCREAMER) ~= sh.chara_id("AirScreamer") then
        sh.map.set("air_screamer_flyby", 0)
        sh.flag_set("M0S01_AirScreamerFlybyDone", true)
        return
    end
    if sh.flag_get("M0S01_AirScreamerFlybyStarted") then
        sh.map.set("air_screamer_flyby", 1)
        local t = sh.map.get("cutscene_timer") + (sh.delta_time_get() * sh.npc.anim_duration(AIR_SCREAMER)) // 4096
        t = math.min(t, FLYBY_END)
        sh.map.set("cutscene_timer", t)
        sh.cutscene.chara(AIR_SCREAMER, "BIRD", t, CAFE2_DMS_BUF)
        if t >= FLYBY_END or math.abs(sh.npc.pos_x(0) - PICKUP_X) > 2867 or
           math.abs(sh.npc.pos_z(0) - PICKUP_Z) > 2867 then -- 0.7
            sh.flag_set("M0S01_AirScreamerFlybyDone", true)
            sh.camera_auto_return(true)
            sh.map.set("air_screamer_flyby", 0)
            sh.npc.set_pos_y(AIR_SCREAMER, -40960)  -- -10.0
            sh.npc.set_pos_z(AIR_SCREAMER, 1146880) -- 280.0
        end
    elseif sh.npc.pos_x(0) ~= PICKUP_X or sh.npc.pos_z(0) ~= PICKUP_Z or sh.npc.rot_y(0) ~= 1024 or
           flybyWait > 30720 then -- 90 deg, 7.5 s
        sh.flag_set("M0S01_AirScreamerFlybyStarted", true)
        AirScreamerAttackStep()
    else
        flybyWait = S16(flybyWait + sh.delta_time_get())
    end
end

-- The end of the overlay's Map_WorldObjectsUpdate (after the draw):
--   - the bird's fly-by (above);
--   - M0S01_PickedUpKnifeAndMap follows the two pickups;
--   - the Air Screamer's death (health NO_VALUE) after the fight starts (M0S01_AirScreamerFightStarted) ends the scene;
--   - from M0S01_RadioStaticPlaying on the radio hisses at its place on the table at radio_static_volume, both loops
--     restarted after the radio (bgm flag 6) has cut them.
local RADIO_POS = {9420, -2457, 1121894} -- (2.3, -0.6, 273.9)

function post_draw()
    BirdFlyBy()
    sh.flag_set("M0S01_PickedUpKnifeAndMap",
                sh.flag_get("M0S01_PickupKitchenKnife") and sh.flag_get("M0S01_PickupMap"))
    if sh.flag_get("M0S01_AirScreamerFightStarted") then
        if sh.npc.chara_id(1) == sh.chara_id("AirScreamer") and sh.npc.health(1) == -1 then
            sh.flag_set("M0S01_AirScreamerDied", true)
            sh.flag_set("M0S01_CafeCutsceneDone", false)
            sh.flag_set("M0S01_AirScreamerEncounterActive", false)
        end
    end
    if sh.flag_get("M0S01_RadioStaticPlaying") then
        if sh.sound.bgm_flags() & 0x40 ~= 0 then
            sh.sound.sd_call("radio_interference_loop")
            sh.sound.sd_call("radio_static_loop")
        end
        local vol = sh.map.get("radio_static_volume")
        local p = RADIO_POS
        sh.sound.play_3d_falloff("radio_interference_loop", p[1], p[2], p[3], vol, 65536, 0) -- 16 m
        sh.sound.play_3d_falloff("radio_static_loop", p[1], p[2], p[3], vol, 65536, 0)
        sh.flag_set("M0S01_RadioStaticPlaying", true)
    end
end
