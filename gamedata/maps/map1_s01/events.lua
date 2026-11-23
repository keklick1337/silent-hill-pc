-- map1_s01: School - 2F
-- Event scripts of this area: the engine runs a handler every tick while its event is active.
-- Fixed-point numbers: 4096 = 1.0 (metres, seconds); angles: 4096 = 360 degrees.

function on_boot()
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
        [96] = {"HandgunBullets","M1S01_HandgunBullets0"},
        [97] = {"FirstAidKit","M1S01_FirstAidKit"},
        [98] = {"HealthDrink","M1S01_HealthDrink"},
        [99] = {"HandgunBullets","M1S01_HandgunBullets1"},
        [100] = {"HandgunBullets","M1S01_HandgunBullets2"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point gives a first aid kit and sets no flag
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- index 4: the hand statue: message over a picture.
function MapEvent_HandStatueInspect()
    sh.event_msg_with_bg("TIM/HAND.TIM", 8192, 6144, "map1_s01.statue_of_an_old_mans") -- 2.0, 1.5
end

-- index 5: item pickup.
function MapEvent_ChemicalItemTake()
    sh.event_item_take("Chemical", 1, "M1S01_PickupChemical", "map1_s01.there_is_a_chemical_take")
end

-- Event helpers.
-- Setting the main step also resets the state timer, the sub-steps and the step timer,
-- so the next step starts from a clean state.
local function StepSet(v)
    sh.event_step_set(0, v)
    sh.state_timer_set(0)
    sh.event_step_set(1, 0)
    sh.sys_timer_set(0)
    sh.event_step_set(2, 0)
end

-- Camera position / look-at in absolute world coordinates.
local function CamPos(x, y, z, accel_xz, accel_y, max_speed_xz, max_speed_y, warp)
    sh.camera_pos(0, 0, 0, x, y, z, accel_xz, accel_y, max_speed_xz, max_speed_y, warp)
end

local function CamLook(x, y, z, accel_x, accel_y, max_speed_x, max_speed_y, warp)
    sh.camera_look(0, 0, 0, x, y, z, accel_x, accel_y, max_speed_x, max_speed_y, warp)
end

-- The silver medallion and the locker door are world objects of world_objects.json (SilverMedallion,
-- LockerDoor); world_objects.lua makes the locker's second door follow LockerDoor.
local MEDAL = "SilverMedallion"

-- Sets the medallion's rotation; nil keeps that axis (angles are 16-bit).
local function MedalRotSet(x, y, z)
    local cx, cy, cz = sh.world_object.rotation_get(MEDAL)
    sh.world_object.rotation_set(MEDAL, x or cx, y or cy, z or cz)
end

local function LockerDoorRotYSet(y)
    local cx, _, cz = sh.world_object.rotation_get("LockerDoor")
    sh.world_object.rotation_set("LockerDoor", cx, y, cz)
end

local function Wrap16(v)
    return ((v + 32768) % 65536) - 32768
end

-- index 6: the locker room smoke scene (smoke hiss sound locker_room_bang, flag M1S01_HandStatueMelted).
-- The smoke emitter and the map effect slots live in the map state (sh.map.get/set);
-- the smoke is started with sh.map.smoke_emitter_start().

function CutsceneLockerRoomEffect()
    sh.bgm_status_flags_set(sh.bgm_status_flags_get() | 2) -- mute the music

    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step > 0 and step < 8 then
        StepSet(8)
        step = 8
    end

    if step == 0 then
        sh.player_freeze(true)
        sh.event_fade_cmd(0, 1, 2, 0, 0)                          -- Start, out, type 2
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
        return
    elseif step == 2 then
        sh.event_fade_cmd(1, 1, 2, 0, 0)                          -- Wait, out, type 2
        return
    end
    if step == 3 then
        local _, py = sh.player_pos()
        sh.player_set_pos(-242483, py, 405094)                    -- -59.2, 98.9 m
        sh.player_rotate(0)
        CamPos(-244162, -1679, 399278, 0, 0, 0, 0, true)          -- (-59.61, -0.41, 97.48)
        CamLook(-240025, -9379, 413163, 0, 0, 0, 0, true)         -- (-58.6, -2.29, 100.87)
        sh.event_step_increment(0)
        step = 4
    end
    if step == 4 then
        sh.event_chara_anim(0, 0, "scene_locker_room", false)                      -- SetState
        sh.event_step_increment(0)
        step = 5
    end
    if step == 5 then
        sh.event_wait_timer(10240, 0)                             -- 2.5 s
        return
    end
    if step == 6 then
        sh.sfx_flags_play("locker_room_bang", -242483, -4096, 407552, 128, 0)   -- sound locker_room_bang @ (-59.2, -1.0, 99.5)
        sh.map.set("smoke_emitter.square_area", 0)
        sh.map.set("smoke_emitter.count", 0x96)
        sh.map.set("smoke_emitter.radius", 0x2E1)
        sh.map.set("smoke_emitter.top_y", -6144)                  -- -1.5 m
        sh.map.set("smoke_emitter.bottom_y", -4096)               -- -1.0 m
        sh.map.set("smoke_emitter.rise_speed", -1638)             -- -0.4 m
        sh.map.set("smoke_emitter.unread", 0x51)
        sh.map.set("smoke_emitter.opacity", 0x20)
        sh.map.set("smoke_emitter.kind", 0)
        sh.map.set("smoke_emitter.center_x", -242483)             -- -59.2 m
        sh.map.set("smoke_emitter.center_z", 407552)              -- 99.5 m
        sh.map.set("smoke_emitter.keep_alive", 12288)             -- 3.0
        sh.map.set("smoke_emitter.fade_rate", 0)
        sh.map.smoke_emitter_start()
        sh.event_step_increment(0)
        step = 7
    end
    if step == 7 then
        sh.event_wait_timer(24576, 0)                             -- 6.0 s
        return
    elseif step == 8 then
        sh.sfx_stop("locker_room_bang")
        sh.event_fade_cmd(2, 1, 0, 0, 0)                          -- Auto, out
        return
    end
    if step == 9 then
        for i = 0, 299 do                                         -- all 300 map effect slots
            local v = sh.map.get("effect_slot_type", i)
            if v == 13 or v == 14 then
                sh.map.set("effect_slot_type", 0, i)
            end
        end
        sh.event_step_increment(0)
    end
    -- step 9 and later: hand control back
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.camera_auto_return(true)
    sh.event_fade_cmd(0, 0, 2, 0, 0)
    if sh.screen_fade_finished() then
        sh.event_fade_cmd(0, 0, 0, 0, 0)
    end
    sh.flag_set("M1S01_HandStatueMelted", true)
end

-- index 7: picking up the gold medallion.
function PickupGoldMedallion()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_inv_item_cmd(0, "GoldMedallion", 0, false)                    -- QueueLoad, gold medallion (no step increment)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.event_inv_item_cmd(1, "GoldMedallion", 0, false)                    -- AwaitLoad
    elseif step == 3 then
        if sh.pickup_animate("GoldMedallion") then
            sh.event_msg(true, "map1_s01.there_is_a_gold_medallion", 4, 5, 0, false)                -- take it / leave it
        end
        sh.flag_set("M1S01_PickupGoldMedallion", true)                                     -- flag: pickup gold medallion
    elseif step == 5 then
        sh.flag_set("M1S01_PickupGoldMedallion", false)
        sh.player_freeze(false, false)
        sh.state_set(0)
    else
        if step == 4 then
            sh.event_inv_item_cmd(3, "GoldMedallion", 1, false)                -- AddItem, then the event ends
        end
        sh.player_freeze(false, false)
        sh.state_set(0)
    end
end

-- index 8: the piano score on a dimmed background (SCORE.TIM, messages 31 and 23).
function InspectPianoScore()
    sh.event_msg_dimmed_bg("TIM/SCORE.TIM", 8192, 6144, "map1_s01.its_written_in_blood", "map1_s01.a_tale_of_birds_without") -- 2.0, 1.5
end

-- index 9: the piano: the puzzle and the falling silver medallion.
-- Picture fade-in over a background: its own sub-step machine on step slot 1.
-- The medallion is world object 0; it falls with falling_object_speed (sh.map) and sh.game.gravity_speed().
local function BgTextureLoadFadeIn(fileIdx, fadeTimestep)
    local sub_step = sh.event_step_get(1)
    if sub_step == 0 then
        sh.event_fade_cmd(0, 1, 0, fadeTimestep, 0)               -- Start, out
        sh.event_step_increment(1)
        sub_step = 1
    end
    if sub_step == 1 then
        sh.event_bg_texture_cmd(7, fileIdx, true)                 -- Auto, incSubStep
    else
        sh.event_fade_cmd(1, 1, 0, 0, 0)                          -- Wait, out
    end
end

local function MedallionRestPose()
    MedalRotSet(-1024, nil, nil)                                 -- rotation.vx = -90 deg
    sh.world_object.position_set(MEDAL, -404684, 0, 93388)        -- (-98.8, 0.0, 22.8)
    MedalRotSet(nil, 324, nil)                                    -- 28.5 deg
end

-- The piano keyboard (PIANO2 picture): a cursor in screen pixels from the centre, Q12. Upper
-- row (y < 34): the key under x is the first boundary of PianoKeyEdges greater than x (11 = past
-- the last one). Lower row: only the white keys, by PianoWhiteEdges. Each key plays its note and
-- snaps the cursor to its middle; the black keys (1, 2, 7, 9, 10) carry the tune, and the five of
-- PianoTune pressed in a row (a white key breaks the run) solve the puzzle.
local PianoKeyEdges = {-72, -57, -46, -31, -14, 1, 16, 29, 44, 58, 73}
local PianoWhiteEdges = {{-64, 0}, {-39, 2}, {-14, 4}, {11, 5}, {36, 7}, {61, 9}}
local PianoTune = {[0] = 2, 9, 10, 7, 1}
-- key -> {sub-step to set, note, cursor x in pixels}; sub-step 1 waits 0.3 s, 3 waits 0.2 s and
-- then plays piano_note_alt (the dull blood-stained key).
local PianoKeyPress = {
    [0] = {1, "piano_key_1", -77}, {3, "piano_key_8", -64}, {3, "piano_key_8", -52},
    {1, "piano_key_2", -39}, {1, "piano_key_3", -27}, {1, "piano_key_4", -2}, {1, "piano_key_5", 8},
    {3, "piano_key_8", 23}, {1, "piano_key_6", 36}, {3, "piano_key_8", 48}, {3, "piano_key_8", 64},
    {1, "piano_key_7", 73},
}
local PianoIsWhite = {[0] = true, [3] = true, [4] = true, [5] = true, [6] = true, [8] = true, [11] = true}
local pianoCursorX, pianoCursorY, pianoKeyCounter = 0, 0, 0

local function TDiv(a, b) -- integer division truncating toward zero, like C
    local q = a // b
    if q < 0 and q * b ~= a then
        q = q + 1
    end
    return q
end

local function PianoKeyAt(px, py)
    if py >= 34 then
        for _, e in ipairs(PianoWhiteEdges) do
            if px < e[1] then
                return e[2]
            end
        end
        return #PianoKeyEdges
    end
    for i, edge in ipairs(PianoKeyEdges) do
        if px < edge then
            return i - 1
        end
    end
    return #PianoKeyEdges
end

local function PianoPlay()
    local sub = sh.event_step_get(1)
    if sub & 1 ~= 0 then
        if sub == 1 or sub == 5 then
            sh.event_wait_timer(1228, 1)                          -- 0.3 s
        elseif sub == 3 then
            sh.event_wait_timer(819, 1)                           -- 0.2 s
        end
        return
    end
    if sub == 4 then
        sh.sd_call("piano_note_alt")
        sh.event.step_set(1, 5)
        return
    end
    sh.event_step_set(1, 0)

    if pianoKeyCounter == 5 then
        sh.flag_set("M1S01_PianoPuzzleSolved", true)
        sh.event_step_increment(0)
        return
    end
    if sh.input_action("cancel", 1) then
        sh.event_step_increment(0)
        return
    end

    sh.vec.set(14, pianoCursorX, 0, 0)
    sh.vec.set(15, pianoCursorY, 0, 0)
    sh.screen.puzzle_cursor("vec:14", "vec:15")
    pianoCursorX = sh.vec.get(14)
    pianoCursorY = sh.vec.get(15)
    -- The stick rate is tuned for 30 fps (136 = 1/30 s).
    local dt = sh.delta_time_get()
    pianoCursorX = pianoCursorX + TDiv(TDiv(sh.input_axis(1, 0) * 16384, 75) * dt, 136)
    pianoCursorX = math.max(math.min(pianoCursorX, 348160), -364544)  -- -89 .. 85
    pianoCursorY = pianoCursorY + TDiv(TDiv(sh.input_axis(1, 1) * 16384, 75) * dt, 136)
    pianoCursorY = math.max(math.min(pianoCursorY, 344064), -290816)  -- -71 .. 84

    sh.game.timer_update()
    sh.screen.cursor_draw(pianoCursorX // 4096 + 8, pianoCursorY // 4096 + 8, 8, 8, 0, 64, 32, 32, 128, 192, 0, 12)

    if not sh.input_action("enter", 1) then
        return
    end
    local key = PianoKeyAt(pianoCursorX // 4096, pianoCursorY // 4096)
    local press = PianoKeyPress[key]
    if press ~= nil then
        sh.event.step_set(1, press[1])
        sh.sd_call(press[2])
        pianoCursorX = press[3] * 4096
    end
    if PianoIsWhite[key] then
        pianoKeyCounter = 0
    elseif key == PianoTune[0] then
        pianoKeyCounter = 1
    elseif PianoTune[pianoKeyCounter] == key then
        pianoKeyCounter = pianoKeyCounter + 1
    else
        pianoKeyCounter = 0
    end
end

function PianoPuzzle()
    sh.bgm_status_flags_set(sh.bgm_status_flags_get() | 2)       -- mute the music

    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step >= 10 and step < 19 then
        sh.screen_fade_reset_timestep()
        StepSet(17)
        step = 17
        MedallionRestPose()
    end

    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        BgTextureLoadFadeIn("TIM/PIANO1.TIM", 10240)           -- 2.5
        return
    end
    if step == 2 then
        sh.event_fade_cmd(0, 0, 0, 8192, 0)                       -- Start, in, 2.0
        sh.event_step_increment(0)
        step = 3
    end
    if step == 3 then
        sh.event_bg_texture_cmd(2, 0, false)                      -- Draw
        sh.event_msg(false, "map1_s01.theres_some_blood_on_some", 0, 0, 0, false)                   -- "There's some blood on some of the keys."
        return
    elseif step == 4 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_msg(true, "map1_s01.check_the_piano", 5, 21, 0, false)                   -- "Check the piano?"
        return
    elseif step == 5 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_fade_cmd(2, 1, 0, 6144, 0)                       -- Auto, out, 1.5
        return
    elseif step == 6 then
        sh.event_bg_texture_cmd(7, "TIM/PIANO2.TIM", false)    -- Auto
        return
    end
    if step == 7 then
        sh.event_fade_cmd(0, 0, 0, 6144, 0)                       -- Start, in, 1.5
        pianoCursorX, pianoCursorY, pianoKeyCounter = 0, 0, 0
        sh.event_step_increment(0)
        step = 8
    end
    if step == 8 then
        sh.event_bg_texture_cmd(2, 0, false)
        PianoPlay()
        return
    elseif step == 9 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_sys_state_branch_on_flag("M1S01_PianoPuzzleSolved", 10, 19, false)      -- flag: piano puzzle solved
        return
    end
    if step == 10 then
        sh.event_fade_cmd(0, 1, 2, 0, 0)                          -- Start, out, type 2
        sh.event_step_increment(0)
        step = 11
    end
    if step == 11 then
        local _, py = sh.player_pos()
        sh.player_set_pos(-409600, py, 82329)                     -- -100.0, 20.1 m
        sh.player_rotate(-256)                                    -- -22.5 deg
        sh.event_wait_timer(6144, 0)                              -- 1.5 s
        CamPos(-398540, -9707, 89989, 0, 0, 0, 0, true)           -- (-97.3, -2.3699, 21.97)
        CamLook(-411115, -9011, 100474, 0, 0, 0, 0, true)         -- (-100.37, -2.2, 24.53)
    elseif step == 12 then
        sh.event_wait_timer(2048, 0)                              -- 0.5 s
        local rx = sh.world_object.rotation_get(MEDAL)
        rx = Wrap16(rx + (sh.delta_time_get() * 227) // 4096)     -- 0.0555 s
        MedalRotSet(rx, nil, nil)
        local px = sh.world_object.position_get(MEDAL)
        sh.world_object.position_set(MEDAL, px,
            (sh.math_cos(rx) * -491) // 4096 - 10486,             -- -43.2 deg, 2.5601
            (sh.math_sin(rx) * -491) // 4096 + 97198)             -- 23.73
    elseif step == 13 then
        local fall = sh.map.get("falling_object_speed") + sh.game.gravity_speed()
        sh.map.set("falling_object_speed", fall)
        local dt = sh.delta_time_get()
        local rx = sh.world_object.rotation_get(MEDAL)
        MedalRotSet(Wrap16(rx + (dt * 3072) // 4096), nil, nil)  -- 0.75
        local px, py, pz = sh.world_object.position_get(MEDAL)
        py = py + (dt * fall) // 4096
        pz = pz + (dt * -2048) // 4096                            -- -0.5
        sh.world_object.position_set(MEDAL, px, py, pz)
        if py >= 0 then
            sh.sd_call("piano_note")
            sh.event_step_increment(0)
        end
    elseif step == 14 then
        sh.event_wait_timer(4096, 0)                              -- 1.0 s
    elseif step == 15 then
        sh.event_wait_timer(8192, 0)                              -- 2.0 s
        MedalRotSet(-1024, nil, nil)
        sh.world_object.position_set(MEDAL, -404684, 0, 93388)
        MedalRotSet(nil, 324, nil)
        CamPos(-415293, -10690, 90644, 0, 0, 0, 0, true)          -- (-101.39, -2.6099, 22.13)
        CamLook(-403046, -573, 94617, 0, 0, 0, 0, true)           -- (-98.4, -0.14, 23.1)
    elseif step == 17 then
        sh.event_fade_cmd(2, 0, 2, 0, 0)                          -- Auto, in, type 2
    elseif step == 18 or step == 20 then
        sh.event_step_reset()
    elseif step == 19 or step == 21 or step == 16 then
        if step ~= 16 then
            sh.event_bg_texture_cmd(2, 0, false)                  -- continues into step 16 in the same tick
        end
        sh.event_fade_cmd(2, 1, 0, 8192, 0)                       -- Auto, out, 2.0
    else
        sh.event_fade_cmd(0, 0, 0, 10240, 0)                      -- Start, in, 2.5
        sh.player_freeze(false, false)
        sh.state_set(0)
        sh.camera_auto_return(true)
    end
end

-- index 10: the piano: message over the PIANO1 picture.
function MapEvent_PianoInspect()
    sh.event_msg_with_bg("TIM/PIANO1.TIM", 0, 0, "map1_s01.theres_some_blood_on_some_2")
end

-- index 11: picking up the silver medallion (the player bends down).
function PickupSilverMedallion()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_inv_item_cmd(0, "SilverMedallion", 0, false)                    -- QueueLoad, silver medallion
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
        return
    end
    if step == 2 then
        sh.event_chara_anim(0, 0, "pickup_crouch", false)                      -- SetState, bend down
        sh.event_step_increment(0)
        step = 3
    end
    if step == 3 then
        sh.event_inv_item_cmd(1, "SilverMedallion", 0, false)                    -- AwaitLoad
    elseif step == 4 then
        sh.event_chara_anim(1, 0, 0, false)                       -- AwaitAnimEnd
    elseif step == 5 then
        if sh.pickup_animate("SilverMedallion") then
            sh.event_msg(true, "map1_s01.there_is_a_silver_medallion", 6, 7, 0, false)
        end
        sh.flag_set("M1S01_PickupSilverMedallion", true)                                     -- flag: pickup silver medallion
    elseif step == 6 then
        sh.event_inv_item_cmd(3, "SilverMedallion", 1, false)                    -- AddItem
        StepSet(8)
    elseif step == 7 or step == 8 then
        if step == 7 then
            sh.flag_set("M1S01_PickupSilverMedallion", false)
            sh.event_step_increment(0)
        end
        sh.chara_anim_play_to_end(0, "pickup_stand_up")                          -- stand up
    else
        sh.player_freeze(false, false)
        sh.state_set(0)
    end
end

-- index 12: the locker: message over the LOCKER0 picture.
function MapEvent_LockerInspect()
    sh.event_msg_with_bg("TIM/LOCKER0.TIM", 0, 0, "map1_s01.nothing_inside")
end

-- index 13: the LOCKER.DMS cutscene (the cat in the locker).
-- The "skipped early" state is read only here and reset on step 0, so it is kept local.
local catSkippedEarly = 0

local function CatTimerAdvance(rateQ12, maxQ12)
    local t = math.min(sh.cutscene_timer_get() + (sh.delta_time_get() * rateQ12) // 4096, maxQ12)
    sh.cutscene_timer_set(t)
    return t
end

local function CatNextControlState()
    local controlState = sh.chara_field_get(1, "control_state")
    sh.chara_slot_control_set(1, controlState + 1, 0)             -- next control state, step 0
end

function CutsceneLockerCat()
    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step > 0 and step < 20 then
        if step < 4 then
            catSkippedEarly = 1
        end
        StepSet(20)
        step = 20
    end

    local function Body()
        if step == 0 then
            sh.player_freeze(true)
            sh.event_fade_cmd(0, 1, 3, 0, 0)                      -- Start, out, type 3
            sh.fs_read("ANIM/LOCKER.DMS", 16)                                   -- LOCKER.DMS -> file buffer 16
            catSkippedEarly = 0
            sh.event_step_increment(0)
            step = 1
        end
        if step == 1 then
            sh.event_wait_player_stop()
            return
        elseif step == 2 then
            if sh.fs_queue_done() then
                sh.event_step_increment(0)
            end
            if sh.event_step_get(0) ~= 2 then
                sh.chara_load(2, "Cat", 4)                           -- Cat
            end
            return
        elseif step == 3 then
            sh.event_fade_cmd(1, 1, 2, 0, 0)                      -- Wait, out, type 2
            return
        end
        if step == 4 then
            sh.dms_fix(16)
            sh.cutscene_timer_set(0)
            sh.event_chara_anim(0, 0, "scene_locker_cat_1", false)
            sh.held_item_hide()
            sh.player_weapon_stash()
            sh.event_step_increment(0)
            step = 5
        end
        if step == 5 then
            CatTimerAdvance(49152, 32768)                         -- cutscene runs at 12 frames/s, up to frame 8
            sh.event_step_increment(0)
            step = 6
        end
        if step == 6 then
            sh.chara_process_loads()
            sh.chara_spawn("Cat", 0, -245760, 73728, 0, 3)           -- (-60, 18)
            sh.event_step_increment(0)
            step = 7
        end
        if step == 7 then
            if CatTimerAdvance(49152, 32768) >= 32768 then
                sh.event_step_increment(0)
            end
            return
        end
        if step == 8 then
            CatNextControlState()
            sh.flag_set("M1S01_LockerCatJumpedOut", true)
            sh.event_chara_anim(0, 0, "scene_locker_cat_3", false)
            sh.pad_effect(18, 96)                                 -- pad rumble preset 18 at level 96
            sh.sfx_flags_play("locker_cat_burst", -241664, -2048, 76185, 128, 0) -- (-59.0, -0.5, 18.6)
            sh.sfx_flags_play("locker_door_burst_open", -240844, -4915, 77004, 128, 0) -- (-58.8, -1.2, 18.8)
            sh.event_step_increment(0)
            step = 9
        end
        if step == 9 then
            if CatTimerAdvance(73728, 135168) >= 135168 then      -- 18 frames/s, up to frame 33
                sh.event_step_increment(0)
            end
            sh.cutscene_timer_set(math.max(sh.cutscene_timer_get(), 65536)) -- frame 16
            return
        end
        if step == 10 then
            CatNextControlState()
            sh.event_step_increment(0)
            step = 11
        end
        if step == 11 then
            if CatTimerAdvance(86016, 196608) >= 196608 then      -- 21 frames/s, up to frame 48
                sh.event_step_increment(0)
            end
            sh.cutscene_timer_set(math.max(sh.cutscene_timer_get(), 139264)) -- frame 34
        elseif step == 12 then
            sh.event_msg(false, "map1_s01.just_a_cat", 0, 0, 0, false)               -- "Just a cat..."
            CatTimerAdvance(81920, 495616)                        -- 20 frames/s, up to frame 121
            sh.cutscene_timer_set(math.max(sh.cutscene_timer_get(), 200704)) -- frame 49
        elseif step == 13 then
            if CatTimerAdvance(81920, 495616) >= 495616 then
                sh.event_step_increment(0)
            end
            sh.cutscene_timer_set(math.max(sh.cutscene_timer_get(), 200704))
        elseif step >= 14 and step <= 19 then
            if step == 14 then
                sh.sfx_flags_play("offscreen_cat_scuffle", -231014, -3276, 68812, 128, 0) -- (-56.4, -0.8, 16.8)
                sh.chara_model_clear(1)
                sh.event_step_increment(0)
                step = 15
            end
            if step == 15 then
                sh.event_wait_timer(4096, 0)                      -- 1.0 s
                return
            end
            if step == 16 then
                sh.sfx_flags_play("offscreen_cat_cry", -231014, -3276, 68812, 128, 0)
                sh.event_step_increment(0)
                step = 17
            end
            if step == 17 then
                sh.event_wait_timer(8192, 0)                      -- 2.0 s
                return
            end
            if step == 18 then
                sh.sfx_flags_play("offscreen_cat_killed", -235929, 0, 65536, 128, 0) -- (-57.6, 0.0, 16.0)
                sh.sfx_flags_play("stalker_attack", -235929, 0, 65536, 64, 0)  -- 0.25
                sh.event_step_increment(0)
            end
            sh.event_wait_timer(12288, 0)                         -- 3.0 s
        elseif step == 20 then
            sh.event_fade_cmd(2, 1, 0, 0, 0)                      -- Auto, out
        else
            sh.cutscene_timer_set(-1)                             -- no cutscene
            local px, py, pz = sh.player_pos()
            local ry = sh.player_rot_y_get()
            px = px + (sh.math_sin(ry + 2048) * 4505) // 4096     -- 180 deg, 1.1
            pz = pz + (sh.math_cos(ry + 2048) * 4505) // 4096
            sh.player_set_pos(px, py, pz)
            sh.camera_auto_return(true)
            sh.event_fade_cmd(0, 0, 2, 0, 0)
            sh.event_fade_cmd(0, 0, 0, 0, 0)
            sh.flag_set("M1S01_LockerCatSceneDone", true)
            sh.player_freeze(false, false)
            sh.state_set(0)
            sh.chara_bones_init(0)
            sh.flag_set("M1S01_LockerCatJumpedOut", true)
            sh.chara_model_clear(1)
            sh.held_item_show()
            if catSkippedEarly == 0 then
                sh.player_weapon_restore()
            end
        end
    end
    Body()

    local t = sh.cutscene_timer_get()
    if t >= 0 then
        sh.dms_chara_apply(0, "HERO", t, 16)
        if t >= 65536 then
            sh.dms_chara_apply(1, "CAT", t, 16)
        end
        sh.dms_camera_apply(t, 16)
    end

    if sh.flag_get("M1S01_LockerCatJumpedOut") and not sh.flag_get("M1S01_LockerCatSceneDone") then
        local since_frame_16 = math.min(t - 65536, 8192)                    -- time since frame 16, at most 2 frames
        LockerDoorRotYSet((since_frame_16 * -46) // 4096 - 478)            -- -4.1 deg, 42.05 deg
    end
end
