-- map3_s00: Hospital - until Kaufmann meeting
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

-- index 3: first aid kit pickup (event_common_item_take runs its own steps).
function MapEvent_FirstAidKitTake()
    sh.event_common_item_take("FirstAidKit", "M3S00_FirstAidKit")
end

-- index 4: Kaufmann cutscene in the hospital (HP1F03.DMS).
-- The voice index is read only here, so it is a Lua local; the voice commands and the door
-- sound position live in the map state (kaufmann_voice_cmds, door_sfx_pos).
local kaufAudioIdx = 0
local KAUF_DMS_BUF = 15 -- file buffer 15

local function KaufVoice(msgIdx)
    kaufAudioIdx = sh.event_msg_audio(msgIdx, kaufAudioIdx, "kaufmann_voice_cmds")
end

-- Advances the cutscene time by dt * rate, clamped to maxT; true when clamped.
local function KaufTimerAdd(rate, maxT)
    local t = sh.cutscene_timer_get() + (sh.delta_time_get() * rate) // 4096
    if t > maxT then
        sh.cutscene_timer_set(maxT)
        return true
    end
    sh.cutscene_timer_set(t)
    return false
end

-- Jumps the cutscene time up to minT first, then advances it like KaufTimerAdd; true when clamped.
local function KaufTimerRange(rate, minT, maxT)
    if sh.cutscene_timer_get() < minT then
        sh.cutscene_timer_set(minT)
        return false
    end
    return KaufTimerAdd(rate, maxT)
end

local function KaufDoorSfx(sfxId)
    local x = sh.map.get("door_sfx_pos.x")
    local y = sh.map.get("door_sfx_pos.y")
    local z = sh.map.get("door_sfx_pos.z")
    sh.sfx_flags_play(sfxId, x, y, z, 128, 0)
end

-- Runs one tick of a step table: the step for index s runs, and when it returns another index
-- that step runs too in the same tick (the original switch falls through there); nil ends the
-- tick. An index without a step runs `finish`.
local function RunSteps(steps, s, finish)
    repeat
        s = (steps[s] or finish)()
    until s == nil
end

local function KaufFinish()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.event_fade_cmd(0, 0, 0, 0, 0)
    sh.event_fade_cmd(0, 0, 2, 0, 0)
    sh.camera_auto_return(true)
    sh.sd_call("stop_xa_audio")
    sh.cutscene_timer_set(-1) -- no cutscene time
    sh.flag_set("M3S00_KaufmannCutsceneDone", true)
    sh.flag_set("M3S00_KaufmannBagTaken", true)
    sh.held_item_show()
    sh.player_weapon_restore()
end

local kaufSteps = {
    [0] = function()
        sh.player_freeze(true)
        sh.sfx_flags_play("door_close_0", 387481, -4915, 415744, 128, 0) -- door close 0 sound, (94.6, -1.2, 101.5) m
        sh.flag_set("MapMark_FogHospital1F_ExaminationRoomLeftArrows", true) -- map mark: arrows on the fog hospital 1F examination room door (left)
        sh.fs_read("ANIM/HP1F03.DMS", KAUF_DMS_BUF) -- HP1F03.DMS
        sh.fs_wait()
        sh.dms_fix(KAUF_DMS_BUF)
        sh.chara_spawn("Kaufmann", 0, 409600, 409600, 0, 3) -- Kaufmann, 100.0, 100.0
        sh.chara_props_reset(1)
        sh.cutscene_border(20)
        sh.cutscene_timer_set(0)
        kaufAudioIdx = 0
        sh.cutscene_flag(true)
        sh.math_point_from_angle(392396, 0, 414801, 0x48, 2, 1) -- (95.8, 0, 101.27) m
        sh.math_point_from_angle(393420, 0, 413982, 0x48, 2, 1) -- (96.05, 0, 101.07) m
        sh.math_point_from_angle(393625, 0, 412385, 0x48, 2, 1) -- (96.1, 0, 100.68) m
        sh.held_item_hide()
        sh.player_weapon_stash()
        sh.event_step_increment(0)
    end,
    [1] = function()
        sh.event_chara_anim(0, 1, "turn_with_gun", false)
        sh.event_step_increment(0)
    end,
    [2] = function()
        sh.event_chara_anim(2, 1, 0, false) -- AnimLock
        sh.event_fade_cmd(2, 0, 0, 0, 0)
    end,
    [3] = function()
        sh.event_chara_anim(3, 1, 0, false) -- AnimUnlock
        sh.event_step_increment(0)
        return 4
    end,
    [4] = function()
        if KaufTimerAdd(49152, 40960) then -- 12.0, 10.0 s
            sh.event_step_increment(0)
        end
    end,
    [5] = function()
        sh.event_chara_anim(1, 1, 0, false) -- AwaitAnimEnd
        KaufTimerRange(40960, 45056, 102400) -- 10.0, 11.0, 25.0
        KaufVoice("map3_s00.hold_it")
    end,
    [6] = function()
        sh.event_chara_anim(0, 1, "point_gun", false)
        sh.event_chara_anim(0, 0, "held_at_gunpoint", false)
        sh.event_step_increment(0)
        return 7
    end,
    [7] = function()
        KaufVoice("map3_s00.hold_it")
        if KaufTimerAdd(40960, 102400) then
            sh.event_step_increment(0)
        end
    end,
    [8] = function()
        sh.event_step_increment(0)
        return 9
    end,
    [9] = function()
        KaufVoice("map3_s00.hold_it")
        KaufTimerAdd(40960, 446464) -- 109.0
    end,
    [10] = function()
        KaufVoice("map3_s00.msg16")
        KaufTimerAdd(40960, 446464)
    end,
    [11] = function()
        KaufVoice("map3_s00.stop_dont_shoot")
        sh.event_wait_timer(0x7000, 0)
        KaufTimerAdd(40960, 446464)
    end,
    [12] = function()
        sh.event_chara_anim(0, 1, "lower_gun", false)
        sh.event_step_increment(0)
        return 13
    end,
    [13] = function()
        KaufVoice("map3_s00.stop_dont_shoot")
        KaufTimerAdd(40960, 446464)
    end,
    [14] = function()
        sh.event_fade_cmd(2, 1, 0, 0, 0)
    end,
    [15] = function()
        sh.cutscene_timer_set(450560) -- frame 110.0
        sh.event_chara_anim(0, 0, "cutscene_stand", false)
        sh.event_chara_anim(0, 1, "stand_talk", false)
        sh.event_step_increment(0)
        return 16
    end,
    [16] = function()
        sh.event_fade_cmd(2, 0, 0, 0, 0)
    end,
    [17] = function()
        KaufVoice("map3_s00.thank_god_another_human_being")
    end,
    [18] = function()
        sh.cutscene_timer_set(454656) -- frame 111.0
        KaufVoice("map3_s00.so_maybe_you_can_tell")
    end,
    [19] = function()
        sh.event_wait_timer(6144, 0) -- 1.5 s
    end,
    [20] = function()
        KaufVoice("map3_s00.have_you_seen_a_little")
    end,
    [21] = function()
        sh.event_chara_anim(0, 1, "talk_gesture", false)
        sh.event_step_increment(0)
        return 22
    end,
    [22] = function()
        KaufTimerRange(40960, 458752, 712704) -- 10.0, 112.0, 174.0
        sh.event_wait_timer(6144, 0)
    end,
    [23] = function()
        KaufVoice("map3_s00.your_wife_shes_here_with")
        KaufTimerAdd(40960, 712704)
    end,
    [24] = function()
        sh.event_chara_anim(0, 1, "walk", false)
        sh.event_step_increment(0)
        return 25
    end,
    [25] = function()
        if sh.cutscene_timer_get() >= 716800 then -- frame 175.0
            if KaufTimerAdd(61440, 745472) then -- 15.0, 182.0 s
                sh.event_step_increment(0)
            end
        else
            sh.cutscene_timer_set(716800)
        end
    end,
    [26] = function()
        sh.event_chara_anim(0, 0, "cutscene_react", false)
        sh.event_step_increment(0)
        return 27
    end,
    [27] = function()
        if KaufTimerRange(61440, 716800, 901120) then -- 15.0, 175.0, 220.0
            sh.event_step_increment(0)
        end
    end,
    [28] = function()
        sh.event_chara_anim(0, 1, "leave", false)
        sh.event_chara_anim(0, 0, "cutscene_stand", false)
        sh.event_step_increment(0)
        return 29
    end,
    [29] = function()
        KaufTimerRange(40960, 905216, 991232) -- 10.0, 221.0, 242.0
        KaufVoice("map3_s00.well_id_better_be_going")
    end,
    [30] = function()
        sh.chara_model_clear(1)
        sh.cutscene_timer_set(995328) -- frame 243.0
        sh.event_step_increment(0)
        return 31
    end,
    [31] = function()
        sh.event_wait_timer(819, 0) -- 0.2 s
    end,
    [32] = function()
        KaufDoorSfx("door_open_0") -- door open 0 sound
        sh.event_step_increment(0)
        return 33
    end,
    [33] = function()
        sh.event_wait_timer(4915, 0) -- 1.2 s
    end,
    [34] = function()
        KaufDoorSfx("door_close_0") -- door close 0 sound
        sh.event_step_increment(0)
        return 35
    end,
    [35] = function()
        sh.event_wait_timer(2867, 0) -- 0.7 s
    end,
    [36] = function()
        sh.event_fade_cmd(2, 1, 0, 0, 0)
    end,
    [37] = function()
        sh.cutscene_timer_set(995328)
        sh.chara_model_clear(1)
        sh.event_step_increment(0)
    end,
}

function CutsceneKaufmannHospital()
    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step >= 4 and step < 32 then
        StepSet0(36)
    end
    step = sh.event_step_get(0)
    if step >= 24 and step < 28 then
        sh.delta_time_set((sh.delta_time_get() * 6144) // 4096) -- time runs 1.5x faster
    end

    RunSteps(kaufSteps, step, KaufFinish)

    local t = sh.cutscene_timer_get()
    if t >= 0 then
        sh.dms_chara_apply(0, "HERO", t, KAUF_DMS_BUF)
        sh.dms_chara_apply(1, "KAU", t, KAUF_DMS_BUF)
        sh.dms_camera_apply(t, KAUF_DMS_BUF)
    end
end

-- index 5: paper map pickup.
function MapEvent_PaperMapTake()
    sh.event_paper_map_take("FogHospital1F", "M3S00_PickupMap", "map3_s00.there_is_a_hospital_map")
end
