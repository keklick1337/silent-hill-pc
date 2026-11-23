-- map2_s01: Church
-- Event scripts of this area: the engine runs a handler every tick while its event is active.
-- Fixed-point numbers: 4096 = 1.0 (metres, seconds); angles: 4096 = 360 degrees.

function on_boot()
    -- per-map script entry point
end

-- index 0: message with a sound played at the event's point of interest.
function MapEvent_DoorJammed()
    sh.event_msg_with_sfx("common.lock_jammed", "door_jammed")
end

-- index 1: locked door at point of interest 2 (message with a sound; checks
-- flag M2S01_ChurchDoorTried and sets it again).
function MapEvent_ChurchDoorLocked()
    local poi = sh.event_point()
    if poi == 2 then
        if sh.flag_get("M2S01_ChurchDoorTried") then
            sh.event_msg_with_sfx("map2_s01.it_wont_open", "church_door_locked") -- "It won't open."
            -- The flag is set only on the tick the message closes and gameplay resumes.
            if sh.event.sys_state() == 0 then
                sh.flag_set("M2S01_ChurchDoorTried", true)
            end
        else
            sh.event_msg_with_sfx("common.locked", "church_door_locked")
        end
    else
        sh.event_msg_with_sfx("common.locked", "door_locked") -- message: door locked / door locked sound
    end
end

-- index 2: common pickups, chosen by the event's point of interest.
function MapEvent_CommonItemTake()
    local poi = sh.event_point()
    local t = {
        [25] = {"HealthDrink","M2S01_HealthDrink"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point: first aid kit with the initial flag
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- Setting a step also clears the state timer, the sub-steps and the step timer
-- (sh.event_step_set alone writes only the step).
local function StepSet0(v)
    sh.event_step_set(0, v)
    sh.state_timer_set(0)
    sh.event_step_set(1, 0)
    sh.sys_timer_set(0)
    sh.event_step_set(2, 0)
end

-- index 3: Dahlia cutscene at the church (CHRC.DMS in file buffer 15, voiced lines 15-34
-- through the map state msg_audio_idx / msg_audio_cmds).
-- Steps 14..15: Dahlia's animation frames 21..30 (time since the animation's first
-- keyframe) -> flag M2S01_FlaurosOnTable.
local DMS_BUF = 15

local function Msg(idx)
    sh.event_msg_audio(idx, "msg_audio_idx", "msg_audio_cmds")
end

-- if t < from then t = from (and return) else t += dt*rate, clamped to `to`.
local function TimerAdvance(from, rateQ12, to)
    local t = sh.cutscene_timer_get()
    if t < from then
        sh.cutscene_timer_set(from)
        return false
    end
    t = t + (sh.delta_time_get() * rateQ12) // 4096
    if t > to then
        sh.cutscene_timer_set(to)
        return true
    end
    sh.cutscene_timer_set(t)
    return false
end

function Cutscene_DahliaAtChurch()
    local SKIP = 31
    local SFX_POS = {49152, -4915, 98304} -- (12.0, -1.2, 24.0) m
    if sh.input_action("skip", 1) and sh.event_step_get(0) > 0 and sh.event_step_get(0) < SKIP then
        StepSet0(SKIP)
    end

    local step = sh.event_step_get(0)
    if step == 0 or step == 1 then
        if step == 0 then
            sh.player_freeze(true)
            sh.fs_read("ANIM/CHRC.DMS", DMS_BUF)                         -- CHRC.DMS
            sh.fs_wait()
            sh.dms_fix(DMS_BUF)
            sh.chara_load(0, "Dahlia", 255)                       -- Dahlia; free all character slots first
            sh.chara_process_loads()
            sh.chara_spawn("Dahlia", 0, 81920, 96256, 0, 3)       -- 20.0, 23.5
            sh.chara_props_reset(1)                         -- reset the first NPC's properties
            sh.cutscene_timer_set(0)
            sh.cutscene_border(20)
            sh.cutscene_flag(true)                          -- cutscene active
            sh.event_chara_anim(0, 0, "cutscene_stand", false)            -- start the animation: player
            sh.event_chara_anim(0, 1, 0, false)             -- start the animation: Dahlia
            sh.held_item_hide()
            sh.flag_set("M2S01_DahliaCutsceneStarted", true)
            sh.map.set("msg_audio_idx", 0)
            sh.event_step_increment(0)
        end
        sh.event_fade_cmd(2, 0, 0, 0, 0)                    -- Auto, in
    elseif step == 2 then
        Msg("map2_s01.were_you_ringing_that_bell")
    elseif step == 3 then
        Msg("map2_s01.i_knew_youd_come")
    elseif step == 4 then
        Msg("map2_s01.you_know_something_tell_me")
        sh.event_wait_timer(4915, 0)                        -- 1.2 s
    elseif step == 5 or step == 6 then
        if step == 5 then
            sh.event_chara_anim(0, 0, "scene_dialogue_1", false)
            sh.event_step_increment(0)
        end
        Msg("map2_s01.you_know_something_tell_me")
        TimerAdvance(4096, 20480, 94208)                    -- [1, 23] @ 5
    elseif step == 7 then
        Msg("map2_s01.stay_back")
        TimerAdvance(4096, 20480, 94208)
    elseif step == 8 then
        sh.event_chara_anim(4, 0, 0, false)                 -- reset the animation: player
        sh.event_wait_timer(3276, 0)                        -- 0.8 s
    elseif step == 9 then
        Msg("map2_s01.nothing_is_to_be_gained")
    elseif step == 10 or step == 11 then
        if step == 10 then
            sh.event_chara_anim(0, 1, "talk_gesture", false)
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(2048, 0)                        -- 0.5 s
    elseif step == 12 then
        sh.event_chara_anim(1, 1, 0, false)                 -- wait for the animation to end: Dahlia
        Msg("map2_s01.here_the_flauros_a_cage")
    elseif step == 13 then
        sh.event_wait_timer(8192, 0)                        -- 2.0 s
        Msg("map2_s01.here_the_flauros_a_cage")
    elseif step == 14 or step == 15 then
        if step == 14 then
            sh.event_chara_anim(0, 1, "talk_gesture_flauros", false)
            sh.event_step_increment(0)
        end
        sh.event_chara_anim(1, 1, 0, false)
        Msg("map2_s01.here_the_flauros_a_cage")
    elseif step == 16 or step == 17 then
        if step == 16 then
            sh.event_step_increment(0)
        end
        Msg("map2_s01.here_the_flauros_a_cage")
    elseif step == 18 or step == 19 then
        if step == 18 then
            sh.event_chara_anim(0, 1, "turn_away", false)
            sh.event_step_increment(0)
        end
        Msg("map2_s01.make_haste_to_the_hospital")
        if sh.cutscene_timer_get() >= 581632 then           -- frame 142.0
            TimerAdvance(581632, 40960, 675840)             -- @ 10, clamp 165
        else
            sh.cutscene_timer_set(581632)
        end
    elseif step == 20 then
        sh.event_wait_timer(1228, 0)                        -- 0.3 s
        TimerAdvance(581632, 40960, 675840)
    elseif step == 21 or step == 22 then
        if step == 21 then
            sh.event_chara_anim(0, 1, "walk", false)
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(3276, 0)                        -- 0.8 s
        if sh.cutscene_timer_get() >= 679936 then           -- frame 166.0
            TimerAdvance(679936, 61440, 737280)             -- @ 15, clamp 180
        else
            sh.cutscene_timer_set(679936)
        end
    elseif step == 23 or step == 24 then
        if step == 23 then
            sh.event_chara_anim(0, 0, "scene_descend", false)
            sh.event_step_increment(0)
        end
        Msg("map2_s01.wait_dont_go_yet")
        TimerAdvance(679936, 61440, 1036288)                -- clamp 253
    elseif step == 25 then
        if TimerAdvance(679936, 61440, 1036288) then
            sh.event_step_increment(0)
        end
    elseif step == 26 then
        sh.event_wait_timer(3276, 0)
    elseif step == 27 or step == 28 then
        if step == 27 then
            sh.sfx_flags_play("fog_door_open", SFX_POS[1], SFX_POS[2], SFX_POS[3], 128, 0)
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(4915, 0)
    elseif step == 29 or step == 30 then
        if step == 29 then
            sh.sfx_flags_play("fog_door_close", SFX_POS[1], SFX_POS[2], SFX_POS[3], 128, 0)
            sh.chara_model_clear(1)
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(2048, 0)
    elseif step == SKIP then
        sh.event_fade_cmd(2, 1, 0, 0, 0)                    -- Auto, out
        if sh.event_step_get(0) ~= SKIP then
            sh.cutscene_timer_set(1036288)
        end
    else
        sh.player_freeze(false)
        sh.state_set(0)
        sh.event_fade_cmd(0, 0, 0, 0, 0)
        sh.camera_auto_return(true)
        sh.event_fade_cmd(0, 0, 2, 0, 0)
        sh.flag_set("M2S01_DahliaCutsceneDone", true)
        sh.flag_set("M2S01_FlaurosOnTable", true)
        sh.flag_set("MapMark_FogCentralTown_HospitalCircle", true)                              -- map mark: Central Silent Hill hospital circle (fog)
        sh.flag_set("MapMark_AltCentralTown_HospitalCircle", true)                              -- map mark: Central Silent Hill hospital circle (Otherworld)
        sh.chara_model_clear(1)
        sh.cutscene_timer_set(-1)                           -- no value
        sh.sd_call("stop_xa_audio")
        sh.held_item_show()
    end

    if step < 15 and step >= 10 then
        TimerAdvance(98304, 40960, 577536)                  -- [24, 141] @ 10
        if step > 13 then
            local animT = sh.chara_field_get(1, "anim_time")
            local start = sh.chara_anim_start_keyframe_get(1) * 4096
            if animT - start <= 122880 and animT - start >= 86016 then -- 30 / 21
                sh.flag_set("M2S01_FlaurosOnTable", true)
            end
        end
    end

    if sh.cutscene_timer_get() >= 0 then
        local t = sh.cutscene_timer_get()
        sh.dms_chara_apply(0, "HERO", t, DMS_BUF)
        sh.dms_chara_apply(1, "DAHLIA", t, DMS_BUF)
        sh.dms_camera_apply(t, DMS_BUF)
    end
end

-- index 4: item pickup.
function MapEvent_FlaurosTake()
    sh.event_item_take("Flauros", 1, "M2S01_PickupFlauros", "map2_s01.there_is_a_flauros_take")
end

-- index 5: item pickup.
function MapEvent_DrawbridgeKeyTake()
    sh.event_item_take("DrawbridgeKey", 1, "M2S01_PickupDrawbridgeKey", "map2_s01.there_is_a_drawbridge_key")
end
