-- map3_s04: Hospital Otherworld - 1F
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

-- index 1: locked door; at POI 5 (until flag M3S04_KnockingBehindDoorHeard) something knocks behind it.
-- While any monster is alive the time is stopped for this tick (the scene waits on the door).
-- The door sounds play at the point of interest, 1.2 m up.
function MapEvent_LockedDoorKnocking()
    if not sh.flag_get("M3S04_KnockingBehindDoorHeard") and sh.event_point() == 5 then
        for slot = 1, 32 do -- every character slot
            local id = sh.chara_field_get(slot, "chara_id")
            if id > 0 and id < 25 and sh.chara_field_get(slot, "health") > 0 then
                sh.delta_time_set(0)
                break
            end
        end

        local s = sh.event_step_get(0)
        if s == 0 then
            sh.player_freeze(true)
            sh.sfx_3d("door_locked", 128) -- door locked sound, 0.5
            sh.event_step_increment(0)
            s = 1
        end
        if s == 1 then
            sh.event_wait_timer(819, 0) -- 0.2 s
        elseif s == 2 then
            sh.event_msg(false, "common.locked", 0, 0, 0, false)
        elseif s >= 3 and s <= 6 then
            -- step 3 -> 4 or 6 -> 5
            if s == 3 then
                sh.event_step_increment(0)
            end
            if s ~= 5 then
                sh.sfx_flags_play("lisa_footstep", 0xE800, -4096, 0x8BE66, 128, 0) -- sound lisa_footstep at the knocking spot
                sh.event_step_increment(0)
            end
            sh.event_wait_timer(819, 0)
        elseif s == 7 then
            sh.event_wait_timer(3276, 0) -- 0.8 s
        elseif s == 8 then
            sh.event_msg(false, "map3_s04.a_sound_is_heard_from", 0, 0, 0, false)
        else
            if s == 9 then
                sh.event_step_increment(0)
            end
            sh.player_freeze(false, false)
            sh.state_set(0)
            sh.flag_set("M3S04_KnockingBehindDoorHeard", true)
        end
    else
        sh.event_msg_with_sfx("common.locked", "door_locked")
    end
end

-- index 2: common pickups, chosen by the event's point of interest.
function MapEvent_CommonItemTake()
    local poi = sh.event_point()
    local t = {
        [52] = {"HealthDrink","M3S04_HealthDrink0"},
        [53] = {"HealthDrink","M3S04_HealthDrink1"},
        [54] = {"HealthDrink","M3S04_HealthDrink2"},
        [55] = {"HealthDrink","M3S04_HealthDrink3"},
        [56] = {"HandgunBullets","M3S04_HandgunBullets"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point gives a first aid kit with flag 0
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- index 3: examination room key (ExaminationRoomKey): the bathroom marks the map as broken.
function UseExaminationRoomKey()
    sh.item_remove("ExaminationRoomKey", 1)
    sh.event_msg_with_sfx("map3_s04.used_the_examination_room_key", "use_key")
    sh.flag_set("M3S04_ExaminationRoomUnlocked", true)
    sh.flag_set("MapMark_AltHospital1F_ExaminationMedicineArrows", true) -- map mark: arrows on the Otherworld hospital 1F examination room door
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

local function s16(v)
    v = v & 0xFFFF
    if v >= 0x8000 then v = v - 0x10000 end
    return v
end

-- index 4: HSPTL3.DMS cutscene (Lisa in the hospital, Harry passes out).
-- The heartbeat pauses, the volume of sound darkness_drone and the voice index are read
-- only here, so they are Lua locals.
local lisaBeats = {0, 0, 0}
local lisaHumVol = 0
local lisaAudioIdx = 0
local DMS_BUF = 11 -- file buffer 11

local function LisaVoice(msgIdx)
    lisaAudioIdx = sh.event_msg_audio(msgIdx, lisaAudioIdx, "lisa_voice_cmds")
end

-- Runs one tick of a step table: the step for index s runs, and when it returns another index
-- that step runs too in the same tick (the original switch falls through there); nil ends the
-- tick. An index without a step runs `finish`.
local function RunSteps(steps, s, finish)
    repeat
        s = (steps[s] or finish)()
    until s == nil
end

local function LisaFinish()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.player_weapon_restore()
    sh.chara_model_clear(1)
    sh.cutscene_timer_set(-1) -- no cutscene time
    sh.flag_set("M3S04_LisaCutsceneDone", true)
    sh.flag_set("MapMark_AltHospital1F_ExaminationMedicineArrows", true) -- map mark: arrows on the Otherworld hospital 1F examination room door
    sh.water_effect_disable()
end

-- The cutscene time runs from frame 23 to 139 during Harry's collapse (steps 18-23).
local function LisaCollapseTimer()
    CutsceneTimerAdvance(36864, 94208, 569344, true, false) -- 9.0, 23.0, 139.0
end

local lisaSteps = {
    [0] = function()
        sh.player_freeze(true)
        lisaBeats[1] = 9011  -- 2.2
        lisaBeats[2] = 8601  -- 2.1
        lisaBeats[3] = 10649 -- 2.6
        lisaAudioIdx = 0
        sh.fs_read("ANIM/HSPTL3.DMS", DMS_BUF) -- HSPTL3.DMS
        sh.chara_load(0, "Lisa", -1) -- Lisa
        sh.chara_process_loads()
        sh.chara_spawn("Lisa", 0, 18022, 1105510, 0, 3) -- 4.4, 269.9
        sh.chara_props_reset(1)
        sh.dms_fix(DMS_BUF)
        sh.cutscene_border(20) -- force the letterbox borders
        sh.screen_fade_reset_timestep()
        sh.cutscene_flag(true)
        sh.flashlight(true)
        sh.light_bones_clear()
        sh.light_intensity_set(2867) -- 0.7
        sh.light_pos_set(238960, -7905, 580812) -- 58.34, -1.93, 141.8 m
        sh.light_rot_set(-170, -2013, 0) -- light rotation
        sh.water_effect_enable()
        sh.held_item_hide()
        sh.player_weapon_stash()
        sh.cutscene_timer_set(0)
        lisaHumVol = 0
        sh.event_step_increment(0)
    end,
    [1] = function()
        sh.event_chara_anim(0, 1, "stand_talk", false)
        sh.event_chara_anim(0, 0, "cutscene_stand", false)
        sh.event_step_increment(0)
        return 2
    end,
    [2] = function()
        sh.event_fade_cmd(2, 0, 0, 0, 0) -- Auto, in
    end,
    [3] = function()
        LisaVoice("map3_s04.finally_someone_else_whos_ok")
    end,
    [4] = function()
        sh.event_chara_anim(0, 1, "talk_gesture", false)
        sh.event_step_increment(0)
        return 5
    end,
    [5] = function()
        LisaVoice("map3_s04.harry_tell_me_whats_happening")
        CutsceneTimerAdvance(40960, 4096, 86016, true, false) -- 10.0, 1.0, 21.0
    end,
    [6] = function()
        sh.cutscene_timer_set(90112) -- frame 22.0
        sh.event_chara_anim(0, 1, "lean_forward", false)
        sh.event_step_increment(0)
    end,
    [7] = function()
        sh.event_wait_timer(2048, 0) -- 0.5 s
    end,
    [8] = function()
        sh.event_step_increment(0)
        return 9
    end,
    [9] = function()
        LisaVoice("map3_s04.so_you_dont_know_anything")
    end,
    [10] = function()
        sh.event_wait_timer(2867, 0) -- 0.7 s
    end,
    [11] = function()
        LisaVoice("map3_s04.its_like_this_is_all")
    end,
    [12] = function()
        sh.event_wait_timer(3276, 0) -- 0.8 s
    end,
    [13] = function()
        LisaVoice("map3_s04.let_me_ask_you_have")
    end,
    [14] = function()
        LisaVoice("map3_s04.thats_alright")
        sh.flag_set("M3S04_LisaCutsceneTalkDone", true)
    end,
    [15] = function()
        LisaVoice("map3_s04.what_did_you_say_was")
    end,
    [16] = function()
        sh.event_step_increment(0)
        return 17
    end,
    [17] = function()
        sh.event_chara_anim(0, 0, "scene_hospital_lisa", false)
        sh.event_chara_anim(0, 1, "walk_away", false)
        sh.sd_call("darkness_drone")
        lisaHumVol = s16(lisaHumVol + 1)
        sh.event_step_increment(0)
        return 18
    end,
    [18] = function()
        LisaVoice("map3_s04.msg42")
        LisaCollapseTimer()
    end,
    [19] = function()
        sh.event_wait_timer(2048, 0)
        LisaCollapseTimer()
    end,
    [20] = function()
        LisaVoice("map3_s04.damn_my_head")
        LisaCollapseTimer()
    end,
    [21] = function()
        LisaVoice("map3_s04.harry_let_me_help_you")
        sh.event_wait_timer(9011, 0) -- 2.2 s
        LisaCollapseTimer()
        if sh.event_step_get(0) ~= 21 then
            sh.event_fade_cmd(0, 1, 0, 4096, 0) -- Start, out, 1.0
        end
    end,
    [22] = function()
        LisaVoice("map3_s04.harry_let_me_help_you")
        LisaCollapseTimer()
    end,
    [23] = function()
        sh.event_fade_cmd(1, 1, 0, 0, 0) -- Wait, out
        LisaCollapseTimer()
    end,
    [24] = function()
        LisaVoice("map3_s04.msg46")
        sh.flag_set("M3S04_LisaCutsceneHarryCollapsed", true)
    end,
    [25] = function()
        sh.event_fade_cmd(2, 1, 0, 0, 0) -- Auto, out (skipped cutscene)
    end,
}

-- The heartbeat: while the drone is audible it swells; each pause in lisaBeats counts down in
-- turn and restarts the drone when it runs out.
local function LisaHeartbeat()
    if lisaHumVol == 0 then
        return
    end
    lisaHumVol = s16(lisaHumVol + (sh.delta_time_get() >> 1))
    if lisaHumVol > 4096 then
        lisaHumVol = 4096
    end
    sh.sfx_attributes_update("darkness_drone", 0, -(lisaHumVol >> 4), 0)
    for i = 1, 3 do
        if lisaBeats[i] ~= 0 then
            lisaBeats[i] = s16(lisaBeats[i] - sh.delta_time_get())
            if lisaBeats[i] <= 0 then
                lisaBeats[i] = 0
                sh.sd_call("darkness_drone")
                lisaHumVol = 1
            end
            break
        end
    end
end

function CutsceneHospitalLisa()
    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step >= 2 and step < 23 then
        StepSet0(25)
    end

    RunSteps(lisaSteps, sh.event_step_get(0), LisaFinish)
    LisaHeartbeat()

    local t = sh.cutscene_timer_get()
    if t >= 0 then
        sh.dms_chara_apply(0, "HERO", t, DMS_BUF)
        sh.dms_chara_apply(1, "LISA", t, DMS_BUF)
        sh.dms_camera_apply(t, DMS_BUF)
    end
end

-- index 5: item pickup.
function MapEvent_PlateOfQueenTake()
    sh.event_item_take("PlateOfQueen", 1, "M3S04_PickupPlateOfQueen", "map3_s04.there_is_a_plate_of")
end

-- index 6: hospital elevator button panel (this map's variant).
-- Cursor and elevator state live in the map state (elevator_*); the mouse cursor writes
-- them directly. The six shaft doors are world objects.
local ELEV_X, ELEV_Y, ELEV_IDX, ELEV_DOOR = "elevator_cursor_x", "elevator_cursor_y",
    "elevator_selected_floor", "elevator_door_speed"

local function ElevTDiv(a, b) -- integer division truncating toward zero
    local q = a // b
    if q < 0 and q * b ~= a then
        q = q + 1
    end
    return q
end

local function ElevS8(v)
    v = v & 0xFF
    if v >= 0x80 then v = v - 0x100 end
    return v
end

local function ElevS16(v)
    v = v & 0xFFFF
    if v >= 0x8000 then v = v - 0x10000 end
    return v
end

local function ElevGet(name, i) return sh.map.get(name, i or 0) end
local function ElevSet(name, v, i) sh.map.set(name, v, i or 0) end
local function ElevButton(i, j) return sh.map.get(j == 0 and "elevator_buttons.x" or "elevator_buttons.y", i) end

local function ElevPos(name)
    return sh.map.get(name .. ".x"), sh.map.get(name .. ".y"), sh.map.get(name .. ".z")
end

local function ElevHumSfx(vol)
    local x, y, z = ElevPos("elevator_hum_sfx_pos")
    sh.sfx_falloff_pitch_play("elevator_hum_loop", x, y, z, vol, 65536, 0) -- sound elevator_hum_loop, 16.0
end

local function ElevFlagsSfx(sfxId, sym)
    local x, y, z = ElevPos(sym)
    sh.sfx_flags_play(sfxId, x, y, z, 128, 0) -- volume 0.5, no flags
end

local function ElevDoorsMove(first, last, speed)
    local dt = sh.delta_time_get()
    for i = first, last do
        -- sliding_doors: the six door objects of world_objects.json, in file order.
        local v = (i & 1) ~= 0 and speed or -speed
        sh.map.set("sliding_doors.pos.z", sh.map.get("sliding_doors.pos.z", i) + (dt * v) // 4096, i)
    end
end

-- The shaft doors speed up by 0.8 per second up to 0.35; the speed is kept as a 16-bit value.
local function ElevDoorSpeed(idx)
    local v = math.min((sh.delta_time_get() * 3276) // 4096 + ElevGet(ELEV_DOOR, idx), 1433)
    ElevSet(ELEV_DOOR, ElevS16(v), idx)
    return ElevS16(v)
end

-- The elevator panel's save flags: ElevatorFlags[k] is the k-th flag from the generator one.
local ElevatorFlags = {[0] = "M3S01_GeneratorOn", "HospitalElevatorBoardedAtBasement", "HospitalElevatorBoardedAt1F", "HospitalElevatorBoardedAt2F", "HospitalElevatorBoardedAt3F", "HospitalElevatorGoingToBasement", "HospitalElevatorGoingTo1F", "HospitalElevatorGoingTo2F", "HospitalElevatorGoingTo3F", "HospitalElevatorArrivedAtBasement", "HospitalElevatorArrivedAt1F", "HospitalElevatorArrivedAt2F", "HospitalElevatorArrivedAt3F"}

function ElevatorPanel()
    local step0 = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step0 >= 9 and step0 < 17 then
        StepSet0(17)
    end

    local s = sh.event_step_get(0)
    if s == 0 then
        sh.player_freeze(true)
        sh.event_fade_cmd(0, 1, 0, 0, 0) -- Start, out
        ElevSet(ELEV_Y, 0)
        ElevSet(ELEV_X, 0)
        for i = 0, 7 do
            sh.flag_set(ElevatorFlags[5 + i], false)
        end
        for i = 0, 3 do
            if sh.flag_get(ElevatorFlags[1 + i]) then -- the lit floor button
                ElevSet(ELEV_Y, (ElevButton(i + 1, 1) - 110) * 4096)
                ElevSet(ELEV_IDX, i)
            end
        end
        sh.event_step_increment(0)
        s = 1
    end
    if s == 1 then
        sh.event_bg_texture_cmd(7, "TIM/ELEVAT_4.TIM", false) -- Auto, ELEVAT_4.TIM
        return
    end
    if s == 2 then
        sh.event_fade_cmd(1, 1, 0, 0, 0) -- Wait, out
        return
    end
    if s == 3 then
        sh.event_bg_texture_cmd(2, 0, false) -- Draw
        sh.event_fade_cmd(2, 0, 0, 0, 0) -- Auto, in
        return
    end
    if s == 4 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.mouse_cursor_puzzle_override(ELEV_X, ELEV_Y)
        -- Stick moves the cursor at a 30 fps-based rate (16384 / 75 per tick of 1/30 s).
        local dt = sh.delta_time_get()
        local cx = ElevGet(ELEV_X) + ElevTDiv(ElevTDiv(sh.input_axis(1, 0) * 16384, 75) * dt, 136)
        cx = math.max(math.min(cx, 278528), -286720) -- clamp to -70 .. 68
        ElevSet(ELEV_X, cx)
        local cy = ElevGet(ELEV_Y) + ElevTDiv(ElevTDiv(sh.input_axis(1, 1) * 16384, 75) * dt, 136)
        cy = math.max(math.min(cy, 450560), -450560) -- clamp to -110 .. 110
        ElevSet(ELEV_Y, cy)

        sh.game_timer_update()

        local cursorX = ElevS16(cx // 4096 + 8) -- screen pixels + 8
        sh.cursor_draw(cursorX, cy // 4096 + 8, 8, 8, 0, 64, 32, 32, 128, 192, 0, 12)

        if sh.input_action("cancel", 1) then
            StepSet0(6)
            return
        end
        if not sh.input_action("enter", 1) then
            return
        end

        for i = 0, 6 - 1 do
            local temp = ElevButton(i, 0)
            local fx = ElevGet(ELEV_X) // 4096
            local fy = ElevGet(ELEV_Y) // 4096
            local hit = true
            if (temp - 160) > fx then
                hit = false
            elseif i == 0 then
                if not ((temp - 140) >= fx) then
                    hit = false
                end
            elseif (temp - 141) < fx then
                hit = false
            end
            if hit and ((ElevButton(i, 1) - 120) > fy or (ElevButton(i, 1) - 100) < fy) then
                hit = false
            end
            if hit then
                sh.sd_call("elevator_button")
                if i == 5 then -- the "check" button
                    -- (does nothing on this floor's panel)
                else
                    if (i <= 0 or not sh.flag_get(ElevatorFlags[i])) then
                        if i ~= 0 then
                            sh.flag_set(ElevatorFlags[4 + i], true) -- the pressed floor button
                            ElevSet(ELEV_X, 1)
                            ElevSet(ELEV_IDX, ElevS8(math.abs((ElevGet(ELEV_IDX) + 1) - i)))
                            StepSet0(5)
                        else
                            for j = 0, 3 do
                                if sh.flag_get(ElevatorFlags[1 + j]) then
                                    sh.flag_set(ElevatorFlags[5 + j], true)
                                    break
                                end
                            end
                            ElevSet(ELEV_X, 0)
                            StepSet0(5)
                        end
                    end

                end
            end
        end
        return
    end
    if s == 5 then
        sh.event_bg_texture_cmd(2, 0, false)
        for i = 0, 3 do
            sh.flag_set(ElevatorFlags[1 + i], false)
        end
        ElevSet(ELEV_DOOR, 0, 1)
        ElevSet(ELEV_DOOR, 0, 0)
        sh.player_rotate(-1536)
        local _, py, _ = sh.player_pos()
        sh.player_set_pos(74547, py, 244531)
        StepSet0(8)
        return
    end
    if s == 6 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_fade_cmd(2, 1, 0, 0, 0)
        return
    end
    if s == 7 then
        sh.player_freeze(false, false)
        sh.state_set(0)
        sh.event_fade_cmd(0, 0, 0, 0, 0)
        return
    end
    if s == 8 then
        if ElevGet(ELEV_X) ~= 0 then
            sh.sd_call("elevator_hum_loop")
        end
        sh.event_step_increment(0)
        s = 9
    end
    if s == 9 then
        sh.event_bg_texture_cmd(2, 0, false)
        if ElevGet(ELEV_X) ~= 0 then
            local f = sh.state_timer_get() + sh.delta_time_get()
            if f > 4096 then
                f = 4096
            end
            sh.state_timer_set(f)
            ElevHumSfx(f // 32)
            sh.event_fade_cmd(2, 1, 0, 4096, 0) -- 1.0
        else
            sh.event_fade_cmd(2, 1, 0, 8192, 0) -- 2.0
        end
        return
    end
    if s == 10 then
        if ElevGet(ELEV_X) ~= 0 then
            sh.event_fade_cmd(2, 0, 0, 4096, 0)
            ElevHumSfx(128)
        else
            sh.event_fade_cmd(2, 0, 0, 8192, 0)
            if sh.event_step_get(0) ~= 10 then
                ElevFlagsSfx("load_screen_open", "elevator_door_sfx_pos")
                StepSet0(14)
            end
        end
        return
    end
    if s == 11 then
        if sh.state_timer_get() > (ElevGet(ELEV_IDX) - 1) * 3072 then -- 0.75
            sh.event_step_increment(0)
        else
            sh.state_timer_set(sh.state_timer_get() + sh.delta_time_get())
        end
        return
    end
    if s == 12 then
        ElevHumSfx(128 - sh.state_timer_get() // 32)
        sh.state_timer_set(sh.state_timer_get() + sh.delta_time_get())
        if sh.state_timer_get() > 2048 then -- 0.5
            ElevFlagsSfx("elevator_door", "elevator_hum_sfx_pos")
            sh.event_step_increment(0)
        end
        return
    end
    if s == 13 then
        ElevHumSfx(64 - sh.state_timer_get() // 32) -- 0.25
        sh.state_timer_set(sh.state_timer_get() + sh.delta_time_get())
        if sh.state_timer_get() > 2048 then
            sh.sd_call("load_screen_open_2")
            ElevFlagsSfx("load_screen_open", "elevator_door_sfx_pos")
            sh.sfx_stop("elevator_hum_loop")
            sh.event_step_increment(0)
        end
        return
    end
    if s == 14 then
        local vx = ElevDoorSpeed(0)
        ElevDoorsMove(0, 3, vx)
        sh.event_wait_timer(1228, 0) -- 0.3 s
        return
    end
    if s == 15 or s == 16 then
        local vx = ElevDoorSpeed(0)
        local vy = ElevDoorSpeed(1)
        ElevDoorsMove(0, 3, vx)
        ElevDoorsMove(4, 5, vy)
        if s == 15 then
            sh.event_wait_timer(1228, 0)
        else
            sh.event_fade_cmd(2, 1, 0, 6144, 0) -- 1.5
        end
        return
    end
    if s == 17 then
        sh.event_fade_cmd(2, 1, 0, 0, 0)
        return
    end
    -- finish
    sh.player_freeze(false, false)
    sh.state_set(0)

    sh.sfx_stop("elevator_hum_loop")
    sh.sfx_stop("load_screen_open")
    sh.sfx_stop("elevator_door")
end
