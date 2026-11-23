-- map3_s01: Hospital - 1F and basement after Kaufmann
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

-- index 2: common pickups, chosen by the event's point of interest.
function MapEvent_CommonItemTake()
    local poi = sh.event_point()
    local t = {
        [101] = {"HealthDrink","M3S01_HealthDrink"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point gives a first aid kit with flag 0
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- index 3: common first aid kit pickup.
function MapEvent_FirstAidKitTake()
    sh.event_common_item_take("FirstAidKit", "M3S00_FirstAidKit")
end

-- index 4: basement door key use: the key leaves the inventory, "Used basement key." with
-- the key sound at the event point, the map mark and the door-open flag are set.
function MapEvent_BasementKeyUse()
    sh.item_remove("BasementKey", 1)
    sh.event_msg_with_sfx("map3_s01.used_the_basement_key", "use_key")        -- "Used basement key."
    sh.flag_set("MapMark_FogHospital1F_StairsArrows", true)          -- map mark: arrows on the fog hospital 1F stairs door
    sh.flag_set("M3S01_BasementDoorOpen", true)
end

-- index 5: hospital elevator button panel (this map's variant).
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
        if sh.flag_get("MapMark_FogHospital2F_NearElevatorBroken") and sh.flag_get("MapMark_FogHospital3F_NearElevatorBroken") then -- the broken doors by the elevator marked on 2F and 3F
            sh.event_bg_texture_cmd(7, "TIM/ELEVAT_2.TIM", false) -- Auto, ELEVAT_2.TIM
        else
            sh.event_bg_texture_cmd(7, "TIM/ELEVATOR.TIM", false) -- Auto, ELEVATOR.TIM
        end
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

        for i = 0, 7 - 1 do
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

                if i == 5 then -- the "check" button
                    if sh.flag_get("MapMark_FogHospital2F_NearElevatorBroken") and sh.flag_get("MapMark_FogHospital3F_NearElevatorBroken") then
                        sh.flag_set("M3S01_ElevatorExtraFloorChosen", true)
                        StepSet0(5)
                        sh.sd_call("elevator_button")
                        ElevSet(ELEV_X, 1)
                    end
                else
                    if (i ~= 6 and (i <= 0 or not sh.flag_get(ElevatorFlags[i]))) then
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
                    sh.sd_call("elevator_button")
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
        sh.player_rotate(-1280)
        local _, py, _ = sh.player_pos()
        sh.player_set_pos(240025, py, 79462)
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
            ElevFlagsSfx("load_screen_open", "elevator_door_sfx_pos2")
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
    sh.water_effect_disable()
    sh.flashlight_attrs_fix()
    sh.light_intensity_set(4096) -- 1.0
    sh.sfx_stop("elevator_hum_loop")
    sh.sfx_stop("load_screen_open")
    sh.sfx_stop("elevator_door")
end

-- index 6: the shattered vial (SHATVIAL.TIM).
-- Load the picture -> wait until the player stops -> bend down -> fade out -> wait for the
-- load -> show the picture and fade in -> message map3_s01.a_glass_vial_lies_shattered -> fade out -> fade in -> straighten up;
-- at the end flag M3S01_SeenShatteredVial is set.
-- Each animation step plays the animation to its end before the event moves on.
function MapEvent_UnknownLiquidInteract()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_bg_texture_cmd(0, "TIM/SHATVIAL.TIM", false) -- QueueRead, SHATVIAL.TIM
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 or step == 10 then
        sh.chara_anim_play_to_end(0, step == 2 and 59 or 60)
    elseif step == 3 then
        sh.event_fade_cmd(0, true, 0, 0, false)  -- Start, out
        sh.event_step_increment(0)
        step = 4
    end
    if step == 4 then
        sh.event_bg_texture_cmd(1, 0, false)     -- AwaitLoad
    elseif step == 5 then
        sh.event_fade_cmd(1, true, 0, 0, false)   -- Wait
    elseif step == 6 then
        sh.event_bg_texture_cmd(2, 0, false)      -- Draw
        sh.event_fade_cmd(2, false, 0, 0, false)  -- Auto, in
    elseif step == 7 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_msg(false, "map3_s01.a_glass_vial_lies_shattered")                   -- "A glass vial lies shattered."
    elseif step == 8 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_fade_cmd(2, true, 0, 0, false)   -- Auto, out
    elseif step == 9 then
        sh.event_fade_cmd(2, false, 0, 0, false)  -- Auto, in
    elseif step > 10 then
        sh.player_freeze(false)
        sh.state_set(0)
        sh.flag_set("M3S01_SeenShatteredVial", true)
    end
end

-- index 7: fill the plastic bottle with the liquid.
-- Freeze -> wait until the player stops -> bend down -> pouring sound at
-- (141.6, -0.0999, 20.75), volume 0.5 -> message map3_s01.the_remaining_liquid_is -> straighten up; at the end:
-- flag M3S01_PickupUnknownLiquid + the bottle (101) is replaced by the Unknown liquid (102).
function MapEvent_UseBottleOnLiquid()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 or step == 5 then
        sh.chara_anim_play_to_end(0, step == 2 and 59 or 60)
    elseif step == 3 then
        sh.sfx_flags_play("bottle_pour", 579993, -409, 84992, 128, 0) -- (141.6, -0.0999, 20.75) m, 0.5
        sh.event_step_increment(0)
        step = 4
    end
    if step == 4 then
        sh.event_msg(false, "map3_s01.the_remaining_liquid_is") -- "The remaining liquid is emptied into the Plastic bottle."
    elseif step > 5 then
        sh.player_freeze(false)
        sh.state_set(0)
        sh.flag_set("M3S01_PickupUnknownLiquid", true)                    -- unknown liquid taken
        sh.item_remove("PlasticBottle", 1)
        sh.event_inv_item_cmd(3, "UnknownLiquid", 1, false)    -- add the unknown liquid
    end
end

-- index 8: item pickup.
function MapEvent_PlasticBottleTake()
    sh.event_item_take("PlasticBottle", 1, "M3S01_PickupPlasticBottle", "map3_s01.there_is_a_plastic_bottle")
end

-- index 9: paper map pickup.
function MapEvent_PaperMapTake0()
    sh.event_paper_map_take("FogHospital1F", "M3S00_PickupMap", "map3_s01.there_is_a_hospital_map")
end

-- index 10: paper map pickup.
function MapEvent_PaperMapTake1()
    sh.event_paper_map_take("FogHospitalBF", "M3S01_PickupMap", "map3_s01.there_is_a_hospital_basement")
end

-- index 11: item pickup.
function MapEvent_BasementKeyTake()
    sh.event_item_take("BasementKey", 1, "M3S01_PickupBasementKey", "map3_s01.there_is_a_basement_key")
end

-- index 12: generator switch (generator_make_noise is map state read by the map's effects).
function MapEvent_Generator0()
    local s = sh.event_step_get(0)
    if s == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        s = 1
    end
    if s == 1 then
        sh.event_wait_player_stop()
    elseif s == 2 then
        sh.selection_silent(true) -- the next yes/no choice is silent
        sh.event_msg(true, "map3_s01.the_in_house_generator_powers", 3, -1, 0, false) -- "Do you want to press the switch?"
    elseif s == 3 or s == 4 then
        if s == 3 then
            sh.sfx_flags_play("generator_hum", 575488, -2048, -84377, 128, 0) -- sound generator_hum, (140.5, -0.5, -20.6) m
            sh.sd_call("generator_start")
            sh.flag_set("M3S01_GeneratorOn", true) -- flag: generator on
            sh.map.set("generator_make_noise", 1)
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(10240, 0) -- 2.5 s
    else
        if s == 5 then
            sh.flag_set("M3S01_GeneratorOn", true)
            sh.map.set("generator_make_noise", 1)
            sh.event_step_increment(0)
        end
        sh.player_freeze(false, false)
        sh.state_set(0)
    end
    if sh.flag_get("M3S01_GeneratorOn") then
        sh.pad_effect(9, 144) -- rumble preset 9, level 144
    end
end

-- index 13: background picture (TIM) with a message.
-- The map menu sound plays at the spot during the first sub-step, then the picture 1916
-- is shown with message map3_s01.there_is_a_newspaper and flag M3S01_SeenFirstNewspaper is set.
function MapEvent_BgPictureMsgShow()
    if sh.event_step_get(1) == 0 then
        sh.sfx_flags_play("menu_map", 252805, -3276, 411688, 128, 0) -- (61.72, -0.8, 100.5098) m, 0.5
    end
    sh.event_msg_with_bg("TIM/NEWSP1.TIM", 0, 0, "map3_s01.there_is_a_newspaper")        -- picture file 1916
    sh.flag_set("M3S01_SeenFirstNewspaper", true)
end

-- index 14: message map3_s01.the_generator_is_on at the running generator, with rumble.
function MapEvent_GeneratorRunningMessage()
    local s = sh.event_step_get(0)
    if s == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        s = 1
    end
    if s == 1 then
        sh.event_wait_player_stop()
    elseif s == 2 then
        sh.event_msg(false, "map3_s01.the_generator_is_on", 0, 0, 0, false)
    else
        sh.player_freeze(false, false)
        sh.state_set(0)
    end
    sh.pad_effect(9, 96) -- rumble preset 9, level 0x60
end
