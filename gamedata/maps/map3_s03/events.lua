-- map3_s03: Hospital Otherworld - 3F and 2F
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

-- index 2: unlocked door.
function MapEvent_DoorUnlocked()
    sh.event_msg_with_sfx("common.unlocked", "door_unlocked")
end

-- index 3: common pickups, chosen by the event's point of interest.
function MapEvent_CommonItemTake()
    local poi = sh.event_point()
    local t = {
        [126] = {"FirstAidKit","M3S03_FirstAidKit0"},
        [127] = {"HealthDrink","M3S03_HealthDrink"},
        [128] = {"HandgunBullets","M3S03_HandgunBullets"},
        [129] = {"FirstAidKit","M3S03_FirstAidKit1"},
        [130] = {"ShotgunShells","M3S03_ShotgunShells"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point gives a first aid kit with flag 0
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- index 4: item pickup.
function MapEvent_PlateOfHatterTake()
    sh.event_item_take("PlateOfHatter", 1, "M3S03_PickupPlateOfHatter", "map3_s03.there_is_a_plate_of")
end

-- index 5: item pickup.
function MapEvent_PlateOfCatTake()
    sh.event_item_take("PlateOfCat", 1, "M3S03_PickupPlateOfCat", "map3_s03.there_is_a_plate_of_2")
end

-- index 6: item pickup.
function MapEvent_PlateOfTurtleTake()
    sh.event_item_take("PlateOfTurtle", 1, "M3S03_PickupPlateOfTurtle", "map3_s03.there_is_a_plate_of_3")
end

-- index 7: the plate door (COLORS.TIM over the screen, a cursor): four slots for the plates
-- of Turtle, Hatter, Cat and Queen (plate 0..3). With a plate in hand it is set into the slot
-- picked with the cursor; with none, a plate is taken back. Plate p in slot s is the flag
-- PlateSlotFlags[p][s]; MapMark_AltHospital2F_NurseCenterBotArrows once every plate is in its own slot. Without any plate set
-- and nothing in hand the door is only locked.
local PlateItems = {[0] = "PlateOfTurtle", "PlateOfHatter", "PlateOfCat", "PlateOfQueen"}
local PlateEmpty = 8
-- State only this handler reads (overlay globals in the original): the plate in hand, then
-- the slot it slides in or out of; the number of plates set; the cursor; the slide (Q12).
local plateCurrent = PlateEmpty
local plateSetCount = 0
local plateCursorX, plateCursorY = 0, 0
local plateSlide = 0

local PlateSlotFlags = {
    [0] = {[0] = "M3S03_PlateOfTurtleInSlot0", "M3S03_PlateOfTurtleInSlot1", "M3S03_PlateOfTurtleInSlot2",
           "M3S03_PlateOfTurtleInSlot3"},
    {[0] = "M3S03_PlateOfHatterInSlot0", "M3S03_PlateOfHatterInSlot1", "M3S03_PlateOfHatterInSlot2",
     "M3S03_PlateOfHatterInSlot3"},
    {[0] = "M3S03_PlateOfCatInSlot0", "M3S03_PlateOfCatInSlot1", "M3S03_PlateOfCatInSlot2", "M3S03_PlateOfCatInSlot3"},
    {[0] = "M3S03_PlateOfQueenInSlot0", "M3S03_PlateOfQueenInSlot1", "M3S03_PlateOfQueenInSlot2",
     "M3S03_PlateOfQueenInSlot3"},
}

local function PlateFlag(slot, plate)
    return PlateSlotFlags[plate][slot]
end

local function PlateTdiv(a, b) -- integer division truncating toward zero, as in C
    local q = a // b
    if q < 0 and q * b ~= a then q = q + 1 end
    return q
end

-- A 16-bit store of the slide (q3_12 in the original).
local function PlateS16(v)
    v = v & 0xFFFF
    if v >= 0x8000 then v = v - 0x10000 end
    return v
end

-- The stick moves the cursor at a rate tuned for 30 fps (136 = 1/30 s); -100..100 pixels.
local function PlateCursorUpdate()
    sh.vec.set(14, plateCursorX, 0, 0)
    sh.vec.set(15, plateCursorY, 0, 0)
    sh.screen.puzzle_cursor("vec:14", "vec:15")
    plateCursorX = sh.vec.get(14)
    plateCursorY = sh.vec.get(15)

    plateCursorX = plateCursorX + PlateTdiv(PlateTdiv(sh.input_axis(1, 0) * 16384, 75) * sh.delta_time_get(), 136)
    plateCursorX = math.max(math.min(plateCursorX, 409600), -409600)

    plateCursorY = plateCursorY + PlateTdiv(PlateTdiv(sh.input_axis(1, 1) * 16384, 75) * sh.delta_time_get(), 136)
    plateCursorY = math.max(math.min(plateCursorY, 409600), -409600)

    sh.game.timer_update()

    local cx = ((plateCursorX // 4096 + 8 + 0x8000) & 0xFFFF) - 0x8000 -- whole pixels + 8, a signed 16-bit value
    sh.screen.cursor_draw(cx, plateCursorY // 4096 + 8, 8, 8, 0, 64, 32, 32, 128, 192, 0, 12)
end

-- The rectangle (slots 0..3, plates 4..7) under the cursor: 28x28 pixels from its top-left.
local function PlateRectHit(i)
    local x = sh.map.get("plate_door_rects.x", i)
    local y = sh.map.get("plate_door_rects.y", i)
    local cx = plateCursorX // 4096
    local cy = plateCursorY // 4096
    return not ((x - 160) > cx or (x - 132) <= cx or (y - 120) > cy or (y - 92) <= cy)
end

-- The four door slots and, while a plate slides in (slide > 0), the sliding plate: each sprite is
-- its own 28x28 rectangle with its own page and CLUT, drawn over the door picture. A slot holding
-- 8 is empty; the slot being filled fades in with its slide level (Q12, 1.0 = done).
local PlateEmpty8 = 8
local function PlateDoorDraw(slot, slide)
    sh.event_bg_texture_cmd(2, 0, false)
    local sprites = {}
    local last = slide > 0 and 4 or 3
    for i = 0, last do
        if sh.map.get("plate_door_slots", i) ~= PlateEmpty8 then
            local tint, blend = 128, nil
            local draw = true
            if (i == slot or i == 4) and slide < 4096 then
                if slide <= 0 then
                    draw = false
                else
                    local level = slide >> 5
                    tint, blend = {level, level, level}, "half"
                end
            end
            if draw then
                local idx = i < 4 and i or slot
                local rx = sh.map.get("plate_door_rects.x", idx)
                local ry = sh.map.get("plate_door_rects.y", idx)
                local u, v
                if i < 4 then
                    u = 64
                    v = sh.map.get("plate_door_image_v", sh.map.get("plate_door_slots", idx)) + 16
                else
                    u, v = rx, ry + 16
                end
                sprites[#sprites + 1] = {
                    rect = {rx - 160, ry - 120, 28, 28}, uv = {u, v & 0xFF}, tint = tint, blend = blend,
                    clut = i ~= 4 and 0x3CE or 0x38E, tpage = i < 4 and 167 or 197,
                }
            end
        end
    end
    if #sprites > 0 then
        sh.render.sprite{texture = "background", layer = "picture", sprites = sprites}
    end
end

local function PlateSlideStep()
    return PlateTdiv(64 * sh.delta_time_get(), 136) -- 0x40 per frame at 30 fps
end

function MapEvent_PlateDoorPuzzle()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)

        local i = 0
        while i < 16 do
            if sh.flag_get(PlateFlag(i % 4, i // 4)) then
                break
            end
            i = i + 1
        end

        if sh.event.item_used() ~= 0 or i ~= 16 then
            sh.event.step_set(0, 4)
        else
            sh.event_step_increment(0)
        end
    elseif step == 1 or step == 2 then
        if step == 1 then
            local x, z = sh.event.point_position()
            sh.sfx_flags_play("door_locked", x, -4915, z, 128, 0) -- y -1.2, volume 0.5
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(819, false) -- 0.2 s
    elseif step == 3 then
        sh.event_msg(false, "common.locked", 0, 0, 0, false)
    elseif step == 4 then
        sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
    elseif step == 5 or step == 6 then
        if step == 5 then
            sh.bgm_status_flags_set(sh.bgm_status_flags_get() | 1) -- BgmStatusFlag_Pause

            sh.map.plate_door_image_load()

            sh.load.wait()
            plateSetCount = 0
            for slot = 0, 3 do
                local plate = 0
                while plate < 4 do
                    if sh.flag_get(PlateFlag(slot, plate)) then
                        sh.map.set("plate_door_slots", plate, slot)
                        plateSetCount = plateSetCount + 1
                        break
                    end
                    plate = plate + 1
                end

                if plate == 4 then
                    sh.map.set("plate_door_slots", PlateEmpty, slot)
                end
            end

            plateCursorX = 0
            plateCursorY = 0
            plateCurrent = PlateEmpty

            sh.event_step_increment(0)
        end
        sh.event_fade_cmd(2, false, 0, 0, false) -- Auto, in
        PlateDoorDraw(PlateEmpty, 0)

        if sh.event_step_get(0) ~= 6 then
            if sh.event.item_used() ~= 0 then
                sh.event.step_set(0, 7)
                -- The plate in hand: only the four plates start this event as an item.
                for plate = 0, 3 do
                    if sh.event.used_item_is(PlateItems[plate]) ~= 0 then
                        plateCurrent = plate
                    end
                end
            elseif plateSetCount == 0 then
                sh.event.step_set(0, 15)
            else
                sh.event.step_set(0, 11)
            end
        end
    elseif step == 7 then
        PlateDoorDraw(PlateEmpty, 0)
        sh.event_msg(false, "map3_s03.where_do_you_want_to", 0, 0, 0, false)
    elseif step == 8 then
        PlateDoorDraw(PlateEmpty, 0)
        PlateCursorUpdate()

        if sh.input_action("enter", 1) then
            for i = 0, 7 do
                if PlateRectHit(i) then
                    if i >= 4 or sh.map.get("plate_door_slots", i) ~= PlateEmpty then
                        sh.event.step_set(0, 10)
                        break
                    end

                    sh.map.set("plate_door_slots", plateCurrent, i)
                    sh.flag_set(PlateFlag(i, plateCurrent), true)

                    plateCurrent = i
                    plateSlide = 0

                    sh.sd_call("plate_slide")
                    sh.event.step_set(0, 9)

                    local j = 0
                    while j < 4 do
                        if sh.map.get("plate_door_slots", j) ~= j then
                            break
                        end
                        j = j + 1
                    end
                    if j == 4 then
                        sh.flag_set("MapMark_AltHospital2F_NurseCenterBotArrows", true)
                    end
                    break
                end
            end
        elseif sh.input_action("cancel", 1) then
            sh.event_inv_item_cmd(3, PlateItems[plateCurrent], 1, false) -- AddItem: the plate back
            sh.event.step_set(0, 16)
        end
    elseif step == 9 then
        PlateDoorDraw(plateCurrent, plateSlide)

        plateSlide = PlateS16(plateSlide + PlateSlideStep())
        if plateSlide >= 4915 then -- 1.2
            if sh.flag_get("MapMark_AltHospital2F_NurseCenterBotArrows") then
                sh.sd_call("door_seal_mech")
            end
            sh.event.step_set(0, 16)
        end
    elseif step == 10 then
        PlateDoorDraw(PlateEmpty, 0)
        sh.event_msg(false, "map3_s03.cant_set_the_plate_there", 0, 0, 0, false)

        if sh.event_step_get(0) ~= 10 then
            sh.event.step_set(0, 8)
        end
    elseif step == 11 then
        PlateDoorDraw(PlateEmpty, 0)
        sh.event_msg(false, "map3_s03.which_plate_do_you_want", 0, 0, 0, false)
    elseif step == 12 then
        PlateDoorDraw(PlateEmpty, 0)
        PlateCursorUpdate()

        if sh.input_action("enter", 1) then
            for i = 0, 7 do
                if PlateRectHit(i) then
                    if i >= 4 then
                        sh.event.step_set(0, 14)
                    else
                        local plate = sh.map.get("plate_door_slots", i)
                        if plate == PlateEmpty then
                            break
                        end

                        if plate <= 3 then
                            sh.event_inv_item_cmd(3, PlateItems[plate], 1, false) -- AddItem
                        end

                        sh.flag_set(PlateFlag(i, plate), false)

                        plateCurrent = i
                        plateSlide = 4096 -- 1.0

                        sh.event.step_set(0, 13)
                        sh.sd_call("plate_slide")
                    end
                    break
                end
            end
        elseif sh.input_action("cancel", 1) then
            sh.event.step_set(0, 16)
        end
    elseif step == 13 then
        PlateDoorDraw(plateCurrent, plateSlide)

        plateSlide = PlateS16(plateSlide - PlateSlideStep())
        if plateSlide <= -819 then -- -0.2
            sh.map.set("plate_door_slots", PlateEmpty, plateCurrent)
            sh.event.step_set(0, 16)
        end
    elseif step == 14 then
        PlateDoorDraw(PlateEmpty, 0)
        sh.event_msg(false, "map3_s03.cant_remove_that_plate", 0, 0, 0, false)

        if sh.event_step_get(0) ~= 14 then
            sh.event.step_set(0, 12)
        end
    elseif step == 15 then
        sh.event_msg(false, "map3_s03.there_are_4_square", 0, 0, 0, false)
        PlateDoorDraw(PlateEmpty, 0)
    elseif step == 16 then
        PlateDoorDraw(PlateEmpty, 0)
        sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
    else
        sh.player_freeze(false, false)
        sh.state_set(0) -- gameplay
        sh.event_fade_cmd(0, false, 0, 0, false) -- Start
    end
end

-- index 8: plate sign (PLATECLR.TIM).
-- Background brightness 13/32 (104), message 18.
function MapEvent_PlateSignRead()
    sh.background_gamma(104)
    sh.event_msg_with_bg("TIM/PLATECLR.TIM", 0.0, 0.0, "map3_s03.clouds_flowing_over_a_hill")
end

-- index 9: lithograph (LITHGRPH.TIM).
function MapEvent_LithographRead()
    sh.background_gamma(104) -- 13/32
    sh.event_msg_with_bg("TIM/LITHGRPH.TIM", 0.0, 0.0, "map3_s03.msg44")
end

-- index 10: lithograph 2 (LITHGR_2.TIM).
function MapEvent_Lithograph2Read()
    sh.background_gamma(96) -- 3/8
    sh.event_msg_with_bg("TIM/LITHGR_2.TIM", 0.0, 0.0, "map3_s03.slate_nothing_more_than_that")
end

-- index 11: one-step freeze event.
function MapEvent_InstantFreeze()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1
    end
    -- finish
    sh.player_freeze(false)
    sh.state_set(0)                                    -- back to gameplay
end

-- index 12: Lisa's video tape (LISAVTR2.TIM + voiced message map3_s03.msg27).
-- The voice index is read only by this handler, so it is a Lua local.
local lisaAudioIdx = 0

local function LisaTapeSfx()
    sh.sfx_flags_play("vtr_clunk", -256409, -2048, -241664, 128, 0) -- sound vtr_clunk, (-62.6, -0.5, -59.0) m
end

function MapEvent_LisaVideoTape()
    local s = sh.event_step_get(0)
    if s == 0 then
        sh.player_freeze(true)
        sh.event_bg_texture_cmd(0, "TIM/LISAVTR2.TIM", false) -- QueueRead, LISAVTR2.TIM
        lisaAudioIdx = 0
        sh.event_step_increment(0)
        s = 1
    end
    if s == 1 then
        sh.event_wait_player_stop()
    elseif s == 2 or s == 3 then
        if s == 2 then
            sh.event_chara_anim(0, 0, "scene_videotape_1", false) -- SetState
            sh.event_fade_cmd(0, 1, 2, 0, 0) -- Start, out, type 2
            local _, py, _ = sh.player_pos()
            sh.player_set_pos(-255508, py, -244367) -- -62.38, -59.66 m
            sh.player_rotate(3651) -- 320.9 deg
            sh.camera_pos(-249978 / 4096, -8765 / 4096, -255180 / 4096, 0, 0, 0, 0, 0, 0, 0, true)
            sh.camera_look(-257761 / 4096, -3891 / 4096, -241623 / 4096, 0, 0, 0, 0, 0, 0, 0, true)
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(10240, 0) -- 2.5 s
    elseif s == 4 or s == 5 then
        if s == 4 then
            LisaTapeSfx()
            sh.event_step_increment(0)
        end
        sh.event_fade_cmd(2, 1, 0, 0, 0) -- Auto, out
    elseif s == 6 then
        sh.event_bg_texture_cmd(1, 0, false) -- AwaitLoad
    elseif s == 7 then
        sh.event_fade_cmd(2, 0, 0, 0, 0) -- Auto, in
        sh.event_bg_texture_cmd(2, 0, false) -- Draw
    elseif s == 8 or s == 9 then
        if s == 8 then
            sh.event_step_increment(0)
        end
        lisaAudioIdx = sh.event_msg_audio("map3_s03.msg27", lisaAudioIdx, "lisa_voice_cmds")
        sh.event_bg_texture_cmd(2, 0, false)
        if sh.event_step_get(0) == 9 and (sh.input_action("skip", 1) or sh.input_action("cancel", 1)) then
            sh.sd_call("stop_xa_audio")
            sh.event_step_increment(0)
        end
    elseif s == 10 then
        sh.event_fade_cmd(2, 1, 0, 0, 0)
        sh.event_bg_texture_cmd(2, 0, false)
    elseif s == 11 then
        sh.event_chara_anim(0, 0, "scene_videotape_2", false)
        sh.event_step_increment(0)
    elseif s >= 12 and s <= 15 then
        if s == 12 then
            sh.event_chara_anim(2, 0, 0, false) -- AnimLock
            sh.event_fade_cmd(0, 0, 0, 0, 0)
            sh.event_step_increment(0)
        end
        if s <= 13 then
            sh.event_wait_timer(2048, 0) -- 0.5 s
        elseif s == 14 or s == 15 then
            if s == 14 then
                LisaTapeSfx()
                sh.event_step_increment(0)
            end
            sh.chara_anim_unlock_play_to_end(0)
        end
    else
        sh.player_freeze(false, false)
        sh.state_set(0)
        sh.flag_set("M3S03_LisaVideoTapeWatched", true)
        sh.camera_auto_return(false)
        sh.event_fade_cmd(0, 0, 2, 0, 0)
        sh.event_inv_item_cmd(3, "VideoTape", 1, false) -- AddItem, video tape
    end
end

-- index 13: item pickup.
function MapEvent_BloodPackTake()
    sh.event_item_take("BloodPack", 1, "M3S03_PickupBloodPack", "map3_s03.there_is_a_blood_pack")
end

-- index 14: item pickup.
function MapEvent_DisinfectingAlcoholTake()
    sh.event_item_take("DisinfectingAlcohol", 1, "M3S03_PickupDisinfectingAlcohol", "map3_s03.there_is_a_disinfecting")
end

-- index 15: item pickup.
function MapEvent_LighterTake()
    sh.event_item_take("Lighter", 1, "M3S03_PickupLighter", "map3_s03.there_is_a_lighter_take")
end

-- index 16: item pickup.
function MapEvent_BasementStoreroomKeyTake()
    sh.event_item_take("BasementStoreroomKey", 1, "M3S03_PickupBasementStoreroomKey", "map3_s03.there_is_a_basement_storeroom")
end

-- index 17: Harry pours the blood pack (CutsceneBloodPack) for the monsters in the cage.
local function BloodCam(px, py, pz, lx, ly, lz)
    sh.camera_pos(px / 4096, py / 4096, pz / 4096, 0, 0, 0, 0, 0, 0, 0, true)
    sh.camera_look(lx / 4096, ly / 4096, lz / 4096, 0, 0, 0, 0, 0, 0, 0, true)
end

function MapEvent_BloodPackPour()
    local s = sh.event_step_get(0)
    if s == 0 then
        sh.player_freeze(true)
        sh.event_fade_cmd(0, 1, 2, 0, 0) -- Start, out, type 2
        sh.screen_fade_reset_timestep()
        s = 1 -- the next step starts in the same tick without advancing the step counter
    end
    if s == 1 then
        sh.event_wait_player_stop()
    elseif s == 2 then
        sh.player_set_pos(-572211, 0, 251494) -- -139.7, 0.0, 61.4 m
        sh.player_rotate(sh.math_atan2(-8192, -4915)) -- facing atan2(-2.0, -1.2)
        sh.held_item_set("CutsceneBloodPack")
        sh.fs_wait()
        sh.held_item_show()
        sh.event_chara_anim(0, 0, "scene_blood_pack", false)
        BloodCam(-570572, -11878, 267141, -576184, -5160, 253255)
        sh.event_step_increment(0)
    elseif s == 3 or s == 4 then
        if s == 3 then
            sh.event_chara_anim(2, 0, 0, false) -- AnimLock
            sh.event_step_increment(0)
        end
        sh.event_fade_cmd(2, 0, 0, 0, 0) -- Auto, in
    elseif s == 5 or s == 6 then
        if s == 5 then
            sh.event_chara_anim(3, 0, 0, false) -- AnimUnlock
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(6144, 0) -- 1.5 s
    elseif s == 7 or s == 8 then
        if s == 7 then
            for i = 2, 0, -1 do
                sh.chara_field_set(i + 1, "control_state", 4) -- monster control state 4
            end
            sh.event_chara_anim(0, 0, "cutscene_stand", false)
            sh.math_point_from_angle(-580403, 0, 246579, 110, 15, 0) -- (-141.7, 0, 60.2) m
            sh.math_point_from_angle(-580403, 0, 246579, 100, 15, 0)
            sh.held_item_hide()
            sh.held_item_prev() -- back to the previously held item
            BloodCam(-575283, 122, 243507, -586014, -6758, 253829)
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(6144, 0)
    elseif s == 9 or s == 10 then
        if s == 9 then
            sh.flag_set("M3S03_BloodPackPoured", true)
            BloodCam(-573644, -12656, 259932, -581959, -3768, 248995)
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(8192, 0) -- 2.0 s
    else
        sh.fs_wait()
        sh.player_freeze(false, true)
        sh.state_set(0)
        sh.held_item_show()
        sh.camera_auto_return(true)
        sh.event_fade_cmd(0, 0, 2, 0, 0)
    end
end

-- index 18: message over a background picture.
function MapEvent_EmptyCageInspect()
    sh.event_msg_with_bg("TIM/CAGEEMPT.TIM", 0.0, 0.0, "map3_s03.bird_cage_its_empty")
end

-- index 19: live electric wire (ELCWIRE0.TIM).
-- Background brightness 7/16 (112), message 55.
function MapEvent_ElectricWireInspect()
    sh.background_gamma(112) -- 7/16
    sh.event_msg_with_bg("TIM/ELCWIRE0.TIM", 0.0, 0.0, "map3_s03.a_steel_plate_is_screwed")
end

-- index 20: hospital elevator button panel (this map's variant).
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
