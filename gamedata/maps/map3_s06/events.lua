-- map3_s06: Hospital - 1F after Otherworld
-- Event scripts of this area: the engine runs a handler every tick while its event is active.
-- Fixed-point numbers: 4096 = 1.0 (metres, seconds); angles: 4096 = 360 degrees.

function on_boot()
end

-- Jump to step v of the event: also resets the sub-steps and the state timers, so the
-- new step starts clean (sh.event_step_set alone changes the step only).
local function StepSet0(v)
    sh.event_step_set(0, v)
    sh.state_timer_set(0)
    sh.event_step_set(1, 0)
    sh.sys_timer_set(0)
    sh.event_step_set(2, 0)
end

-- index 0: jammed door.
function MapEvent_DoorJammed()
    sh.event_msg_with_sfx("common.lock_jammed", "door_jammed")
end

-- index 1: locked door.
function MapEvent_DoorLocked()
    sh.event_msg_with_sfx("common.locked", "door_locked")
end

-- index 2: empty handler.
function MapEvent_Nop()
end

-- index 3: first aid kit pickup.
function MapEvent_CommonItemTake()
    sh.event_common_item_take("FirstAidKit", "M3S00_FirstAidKit")
end

-- Advances the cutscene time from startTime towards endTime at dt * rate.
local function CutsceneTimerAdvance(rate, startTime, endTime, clampToEnd, autoAdvance)
    local t = sh.cutscene_timer_get()
    if t < startTime then
        sh.cutscene_timer_set(startTime)
        return
    end
    t = t + (sh.delta_time_get() * rate) // 4096
    if t > endTime then
        if clampToEnd then
            t = endTime
        end
        sh.cutscene_timer_set(t)
        if autoAdvance then
            sh.event_step_increment(0)
        end
        return
    end
    sh.cutscene_timer_set(t)
end

-- The step 8 timer wraps like a 16-bit fixed-point counter.
local function s16(v)
    v = v & 0xFFFF
    if v >= 0x8000 then v = v - 0x10000 end
    return v
end

-- index 4: HSPTL2.DMS cutscene (meeting Dahlia in the hospital).
-- The voice index, the "Dahlia in the DMS" switch, the door sound counter and the step 8 timer
-- are read only by this handler, so they are Lua locals.
local hsptlAudioIdx = 0
local hsptlDahliaOn = false
local hsptlSfxCount = 0
local hsptlTimer = 0
local DMS_BUF = 15 -- file buffer 15

local function HospitalDoorSfx(sfxId)
    local x = sh.map.get("door_sfx_pos.x")
    local y = sh.map.get("door_sfx_pos.y")
    local z = sh.map.get("door_sfx_pos.z")
    sh.sfx_flags_play(sfxId, x, y, z, 128, 0) -- volume 0.5, no flags
end

local function HospitalVoice(msgIdx)
    hsptlAudioIdx = sh.event_msg_audio(msgIdx, hsptlAudioIdx, "dahlia_voice_cmds")
end

-- Runs one tick of a step table: the step for index s runs, and when it returns another index
-- that step runs too in the same tick (the original switch falls through there); nil ends the
-- tick. An index without a step runs `finish`.
local function RunSteps(steps, s, finish)
    repeat
        s = (steps[s] or finish)()
    until s == nil
end

local function DahliaFinish()
    sh.event_chara_anim(3, 0, 0, false) -- AnimUnlock
    sh.player_freeze(false, true)
    sh.state_set(0)
    sh.camera_auto_return(true)
    sh.cutscene_timer_set(-1) -- no cutscene time
    sh.held_item_show()
    sh.player_weapon_restore()
    sh.event_fade_cmd(0, 0, 2, 0, 0)
    sh.event_fade_cmd(0, 0, 0, 0, 0)
    sh.flag_set("M3S06_DahliaCutsceneDone", true)
    sh.sd_call("stop_xa_audio")
    sh.chara_model_clear(1)
    sh.chara_slot_anim_flags_set(0, 2)
    sh.flashlight(false)
end

local dahliaSteps = {
    [0] = function()
        sh.player_freeze(true)
        sh.cutscene_border(20) -- force the letterbox borders
        sh.screen_fade_reset_timestep()
        sh.cutscene_flag(true) -- cutscene mode on
        sh.sfx_stop("darkness_drone")
        sh.fs_read("ANIM/HSPTL2.DMS", DMS_BUF) -- HSPTL2.DMS
        sh.fs_wait()
        sh.dms_fix(DMS_BUF)
        sh.cutscene_timer_set(0)
        hsptlAudioIdx = 0
        hsptlDahliaOn = false
        sh.held_item_hide()
        sh.player_weapon_stash()
        hsptlSfxCount = 0
        hsptlTimer = 0
        sh.event_step_increment(0)
    end,
    [1] = function()
        sh.event_fade_cmd(0, 0, 0, 0, 0) -- Start
        sh.chara_load(0, "Dahlia", 255) -- Dahlia, free all character slots
        sh.event_chara_anim(0, 0, "scene_hospital_dahlia", false) -- SetState
        sh.event_step_increment(0)
        return 2
    end,
    [2] = function()
        sh.event_fade_cmd(1, 0, 0, 0, 0) -- Wait
        sh.event_chara_anim(2, 0, 0, false) -- AnimLock
    end,
    [3] = function()
        sh.chara_process_loads()
        sh.chara_spawn("Dahlia", 0, 401408, 409600, 0, 3) -- 98.0, 100.0
        sh.chara_props_reset(1)
        hsptlDahliaOn = true
        sh.event_step_increment(0)
        return 4
    end,
    [4] = function()
        HospitalVoice("map3_s06.was_i_dreaming")
        CutsceneTimerAdvance(24576, 0, 159744, true, false) -- 6.0, 0, 39.0
    end,
    [5] = function()
        sh.event_chara_anim(3, 0, 0, false) -- AnimUnlock
        hsptlSfxCount = 0
        sh.event_step_increment(0)
        return 6
    end,
    [6] = function()
        HospitalVoice("map3_s06.you_were_too_late")
        CutsceneTimerAdvance(24576, 0, 159744, true, false)
        if hsptlSfxCount ~= 2 then
            sh.state_timer_set(sh.state_timer_get() + sh.delta_time_get())
            local f = sh.state_timer_get()
            if hsptlSfxCount == 0 and not (f <= 1228) then -- 0.3
                HospitalDoorSfx("door_open_0") -- door open 0 sound
                hsptlSfxCount = hsptlSfxCount + 1
            elseif hsptlSfxCount == 1 and not (f <= 6553) then -- 1.6
                HospitalDoorSfx("door_close_0") -- door close 0 sound
                hsptlSfxCount = hsptlSfxCount + 1
            end
        end
    end,
    [7] = function()
        if not sh.ambient_sfx() then
            sh.event_step_increment(0)
        end
        return 8
    end,
    [8] = function()
        hsptlTimer = s16(hsptlTimer + sh.delta_time_get())
        if hsptlTimer > 8192 and sh.event_step_get(0) == 8 then -- 2.0 s
            StepSet0(9)
        end
        CutsceneTimerAdvance(24576, 0, 159744, true, false)
    end,
    [9] = function()
        HospitalVoice("map3_s06.its_you")
    end,
    [10] = function()
        sh.event_wait_timer(6144, 0) -- 1.5 s
    end,
    [11] = function()
        sh.event_chara_anim(0, 1, "stand_pose", false)
        sh.event_step_increment(0)
        return 12
    end,
    [12] = function()
        HospitalVoice("map3_s06.yes_dahlia_gillespie")
        CutsceneTimerAdvance(26624, 163840, 245760, true, false) -- 6.5, 40.0, 60.0
    end,
    [13] = function()
        CutsceneTimerAdvance(26624, 163840, 245760, true, true)
    end,
    [14] = function()
        sh.event_fade_cmd(2, 1, 0, 0, 0) -- Auto, out
    end,
    [15] = function()
        sh.event_chara_anim(0, 0, "cutscene_stand", false)
        sh.event_chara_anim(0, 1, 0, false)
        sh.event_step_increment(0)
        return 16
    end,
    [16] = function()
        sh.event_fade_cmd(2, 0, 0, 0, 0) -- Auto, in
        sh.cutscene_timer_set(249856) -- frame 61.0
    end,
    [17] = function()
        sh.event_wait_timer(8192, 0) -- 2.0 s
    end,
    [18] = function()
        HospitalVoice("map3_s06.tell_me_everything_you_know")
        CutsceneTimerAdvance(8192, 249856, 409600, true, false) -- 2.0, 61.0, 100.0
    end,
    [19] = function()
        sh.event_chara_anim(0, 1, "talk_gesture", false)
        sh.event_step_increment(0)
        return 20
    end,
    [20] = function()
        HospitalVoice("map3_s06.believe_the_evidence_of_your")
        CutsceneTimerAdvance(8192, 249856, 516096, true, false) -- 2.0, 61.0, 126.0
        if sh.cutscene_timer_get() >= 491520 then -- frame 120.0
            sh.chara_slot_anim_flags_clear(0, 2)
        end
    end,
    [21] = function()
        sh.event_chara_anim(0, 1, "talk_gesture_flauros", false)
        sh.chara_slot_anim_flags_set(0, 2)
        sh.event_step_increment(0)
        return 22
    end,
    [22] = function()
        HospitalVoice("map3_s06.have_you_not_seen_the")
        CutsceneTimerAdvance(40960, 520192, 724992, true, false) -- 10.0, 127.0, 177.0
        sh.event_wait_timer(40140, 0) -- 9.8 s
    end,
    [23] = function()
        sh.event_chara_anim(0, 1, 0, false)
        sh.event_step_increment(0)
        return 24
    end,
    [24] = function()
        HospitalVoice("map3_s06.have_you_not_seen_the")
        CutsceneTimerAdvance(40960, 520192, 724992, true, false)
    end,
    [25] = function()
        sh.flag_set("M3S06_DahliaLeftAntiqueShopKey", true)
        sh.event_chara_anim(0, 1, "stand_pose", false)
        sh.event_step_increment(0)
        return 26
    end,
    [26] = function()
        CutsceneTimerAdvance(55296, 729088, 794624, true, true) -- 13.5, 178.0, 194.0
    end,
    [27] = function()
        sh.event_chara_anim(0, 0, "scene_descend", false)
        sh.chara_model_clear(1)
        hsptlSfxCount = 0
        sh.event_step_increment(0)
        return 28
    end,
    [28] = function()
        HospitalVoice("map3_s06.hey_wait")
        if hsptlSfxCount ~= 1 then
            sh.state_timer_set(sh.state_timer_get() + sh.delta_time_get())
            if hsptlSfxCount == 0 and sh.state_timer_get() > 2457 then -- 0.6
                HospitalDoorSfx("door_close_0") -- door close 0 sound
                hsptlSfxCount = hsptlSfxCount + 1
            end
        end
        CutsceneTimerAdvance(40960, 815104, 999424, true, false) -- 10.0, 199.0, 244.0
    end,
    [29] = function()
        CutsceneTimerAdvance(40960, 815104, 999424, true, true)
    end,
    [30] = function()
        sh.event_fade_cmd(2, 1, 0, 0, 0) -- Auto, out
    end,
    [31] = function()
        sh.cutscene_timer_set(999424) -- frame 244.0
        hsptlDahliaOn = false
        sh.event_step_increment(0)
    end,
    [32] = function()
        -- Waits for the ambient sound to end, then finishes in the same tick.
        if sh.ambient_sfx() then
            return
        end
        sh.event_step_increment(0)
        DahliaFinish()
    end,
}

function CutsceneHospitalDahlia()
    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step >= 4 and step < 30 then
        StepSet0(30)
        sh.flag_set("M3S06_DahliaLeftAntiqueShopKey", true)
    end

    RunSteps(dahliaSteps, sh.event_step_get(0), DahliaFinish)

    local t = sh.cutscene_timer_get()
    if t >= 0 then
        sh.dms_chara_apply(0, "HERO", t, DMS_BUF)
        if hsptlDahliaOn then
            sh.dms_chara_apply(1, "DARIA", t, DMS_BUF)
        end
        sh.dms_camera_apply(t, DMS_BUF)
    end
end

-- index 5: paper map pickup.
function MapEvent_PaperMapTake()
    sh.event_paper_map_take("FogHospital1F", "M3S00_PickupMap", "map3_s06.there_is_a_hospital_map")
end

-- index 6: item pickup.
function MapEvent_AntiqueShopKeyTake()
    sh.event_item_take("AntiqueShopKey", 1, "M3S06_PickupAntiqueShopKey", "map3_s06.there_is_an_antique_shop")
end

-- index 7: newspaper (NEWSP1.TIM) with the map menu sound at the reading spot.
function InspectNewspaper()
    if sh.event_step_get(1) == 0 then
        sh.sfx_flags_play("menu_map", 252805, -3276, 411688, 128, 0) -- (61.72, -0.8, 100.51) m
    end
    sh.event_msg_with_bg("TIM/NEWSP1.TIM", 0, 0, "map3_s06.there_is_a_newspaper") -- NEWSP1.TIM
    sh.flag_set("M3S01_SeenFirstNewspaper", true)
end
