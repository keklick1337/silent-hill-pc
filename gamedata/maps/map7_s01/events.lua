-- map7_s01: Nowhere
-- Event scripts of this area: the engine runs a handler every tick while its event is active.
-- Fixed-point numbers: 4096 = 1.0 (metres, seconds); angles: 4096 = 360 degrees.

-- Runs one step of a handler's step table: steps[step], or steps.default for any other step.
local function runStep(steps, step, ...)
    return (steps[step] or steps.default)(...)
end

function on_boot()
    -- per-scenario script entry point
end

-- index 0: message with a sound played at the event's point of interest.
function MapEvent_DoorJammed()
    sh.event_msg_with_sfx("common.lock_jammed", "door_jammed")
end

-- index 1: message with a sound played at the event's point of interest.
function MapEvent_DoorLocked()
    sh.event_msg_with_sfx("common.locked", "door_locked")
end

-- index 2: message with a sound played at the event's point of interest.
function MapEvent_DoorUnlocked()
    sh.event_msg_with_sfx("common.unlocked", "door_unlocked")
end

-- index 3: common pickups, chosen by the event's point of interest.
function MapEvent_CommonItemTake()
    local poi = sh.event_point()
    local t = {
        [170] = {"RifleShells","M7S01_RifleShells"},
        [171] = {"HandgunBullets","M7S01_HandgunBullets0"},
        [172] = {"HealthDrink","M7S01_HealthDrink0"},
        [173] = {"HandgunBullets","M7S01_HandgunBullets1"},
        [174] = {"ShotgunShells","M7S01_ShotgunShells"},
        [175] = {"FirstAidKit","M7S01_FirstAidKit"},
        [176] = {"HandgunBullets","M7S01_HandgunBullets2"},
        [177] = {"HealthDrink","M7S01_HealthDrink1"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point gives a first aid kit and sets no flag
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- index 4: message with a sound played at the event's point of interest.
function MapEvent_DoorLocked81()
    sh.event_msg_with_sfx("map7_s01.its_locked", "door_locked")
end

-- index 5: message with a sound played at the event's point of interest.
function MapEvent_DoorLocked83()
    sh.event_msg_with_sfx("map7_s01.its_locked_2", "door_locked")
end

-- index 6: message with a sound played at the event's point of interest.
function MapEvent_DoorLocked85()
    sh.event_msg_with_sfx("map7_s01.its_locked_3", "door_locked")
end

-- index 7: use the Key of Ophiel: the key leaves the inventory, message map7_s01.used_the_key_of_ophiel with the
-- key sound, flag M7S01_OphielDoorUnlocked.
function MapEvent_KeyOfOphielUse()
    sh.player.item_remove("KeyOfOphiel", 1)
    sh.event_msg_with_sfx("map7_s01.used_the_key_of_ophiel", "use_key")
    sh.flag_set("M7S01_OphielDoorUnlocked", true)
end

-- index 8: use the Key of Hagith: the key leaves the inventory, message map7_s01.used_the_key_of_hagith with the
-- key sound, flag M7S01_HagithDoorUnlocked.
function MapEvent_KeyOfHagithUse()
    sh.player.item_remove("KeyOfHagith", 1)
    sh.event_msg_with_sfx("map7_s01.used_the_key_of_hagith", "use_key")
    sh.flag_set("M7S01_HagithDoorUnlocked", true)
end

-- index 9: use the Key of Phaleg: the key leaves the inventory, message map7_s01.used_the_key_of_phaleg with the
-- key sound, flag M7S01_PhalegDoorUnlocked.
function MapEvent_KeyOfPhalegUse()
    sh.player.item_remove("KeyOfPhaleg", 1)
    sh.event_msg_with_sfx("map7_s01.used_the_key_of_phaleg", "use_key")
    sh.flag_set("M7S01_PhalegDoorUnlocked", true)
end

local voiceMessageShowSteps = {}

voiceMessageShowSteps[0] = function()
    sh.player_freeze(true)
    sh.map.set("voice_msg_audio_idx", 0)
    sh.event_step_increment(0)
    return voiceMessageShowSteps[1]()
end

voiceMessageShowSteps[1] = function()
    sh.msg.show_with_audio("map7_s01.msg15", "voice_msg_audio_idx", "lisa_msg_voice_cmds[0]")
end

voiceMessageShowSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
end

-- index 10: Lisa's voiced line, message 15. The audio index
-- (voice_msg_audio_idx) and the voice commands (lisa_msg_voice_cmds) are map state.
function MapEvent_VoiceMessageShow()
    local s = sh.event_step_get(0)
    runStep(voiceMessageShowSteps, s)
end

-- index 11: message over a background picture.
function MapEvent_BirdCageInspect()
    sh.event_msg_with_bg("TIM/BIRDCAGE.TIM", 0, 0, "map7_s01.the_bird_cage_is_locked")
end

-- index 12: open the bird cage: message map7_s01.used_the_bird_cage_key with sound bird_cage_unlock at a fixed point
-- (-179.64, -1.0, -20.05), remove the key, flag M7S01_BirdCageUnlocked. The sound is not at the event
-- point, so its position goes through the vec:0 scratch slot.
function MapEvent_BirdCageKeyUse()
    sh.vec.set(0, -735805, -4096, -82124) -- (-179.64, -1.0, -20.05) m
    sh.msg.show_with_sfx("map7_s01.used_the_bird_cage_key", "bird_cage_unlock", "vec:0")
    sh.player.item_remove("BirdCageKey", 1)
    sh.flag_set("M7S01_BirdCageUnlocked", true)
end

-- index 13: item pickup.
function MapEvent_KeyOfPhalegTake()
    sh.event_item_take("KeyOfPhaleg", 1, "M7S01_PickupKeyOfPhaleg", "map7_s01.there_is_a_key_of_3")
end

-- index 14: message over a background picture.
function MapEvent_WaterTapInspect()
    sh.event_msg_with_bg("TIM/WATERTAP.TIM", 0, 0, "map7_s01.there_is_a_key_in")
end

local faucetJetsShowSteps = {}

faucetJetsShowSteps[0] = function()
    sh.player_freeze(true)
    sh.map.set("faucet_drip_timers", 7782, 0) -- 1.9 s
    sh.map.set("faucet_drip_timers", 6144, 1) -- 1.5 s
    sh.map.set("faucet_drip_timers", 6144, 2) -- 1.5 s
    sh.map.set("faucet_drip_timers", 1638, 3) -- 0.4 s
    sh.event_fade_cmd(0, true, 2, 0, false) -- fade start
    sh.player.weapon_stash()
    sh.held_item_hide()
    sh.event_step_increment(0)
    return faucetJetsShowSteps[1]()
end

faucetJetsShowSteps[1] = function()
    sh.event.wait_player_stop()
end

faucetJetsShowSteps[2] = function()
    sh.event_fade_cmd(1, true, 2, 0, false) -- wait for the fade
end

faucetJetsShowSteps[3] = function()
    sh.event_chara_anim(0, 0, "scene_faucet_jets", false)
    sh.camera.position(nil, -570900, -3768, -81879, 0, 0, 0, 0, true) -- -139.38, -0.92, -19.99 m
    sh.camera.look_at(nil, -577536, -6717, -96583, 0, 0, 0, 0, true)   -- -141.0, -1.64, -23.58 m
    sh.npc.set_pos_x(0, -573440) -- -140.0 m
    sh.npc.set_pos_z(0, -88883)  -- -21.7 m
    sh.npc.set_rot_y(0, 2048)    -- 180 deg
    sh.event_step_increment(0)
    return faucetJetsShowSteps[4]()
end

faucetJetsShowSteps[4] = function()
    for i = 0, 3 do
        if sh.map.get("faucet_drip_timers", i) > 0 then
            sh.map.set("faucet_drip_timers", sh.map.get("faucet_drip_timers", i) - sh.delta_time_get(), i)
            if sh.map.get("faucet_drip_timers", i) > 0 then
                break
            end
            sh.map.set("faucet_drip_timers", 0, i)
            if i == 3 then
                sh.event_step_increment(0)
                break
            end
            sh.sfx_flags_play("faucet_jet_start", -573644, -5734, -90726, 128, 0) -- at (-140.05, -1.4, -22.15) m, volume 0.5
            break
        end
    end
end

faucetJetsShowSteps[5] = function()
    sh.sfx_flags_play("faucet_jet_loop", -573644, 0, -90726, 128, 0) -- at (-140.05, 0.0, -22.15) m
    sh.event_step_increment(0)
    return faucetJetsShowSteps[6]()
end

faucetJetsShowSteps[6] = function()
    sh.event_chara_anim(1, 0, 0, false) -- wait for the animation to end
end

faucetJetsShowSteps[7] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- fade out
end

faucetJetsShowSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.player.weapon_restore()
    sh.held_item_show()
    sh.camera_auto_return(true)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.flag_set("M7S01_FaucetOpenedWithPliers", true)
end

-- index 15: the water tap: four drips timed by faucet_drip_timers[0..3] (map
-- state), camera and player pose, sounds 1642/1643.
function MapEvent_FaucetJetsShow()
    local s = sh.event_step_get(0)
    runStep(faucetJetsShowSteps, s)
end

-- index 16: take the Key of Ophiel with the inspection animation
-- (the same sequence as the steel pipe pickup).
function MapEvent_KeyOfOphielTake()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_inv_item_cmd(0, "KeyOfOphiel", 0, false) -- start loading
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
        return
    end
    if step == 2 then
        sh.event_chara_anim(0, 0, "pickup_crouch", false) -- start the animation, bend down
        sh.event_step_increment(0)
        step = 3 -- and continue into step 3 in the same tick
    end
    if step == 3 then
        sh.event_inv_item_cmd(1, "KeyOfOphiel", 0, false) -- wait for loading
    elseif step == 4 then
        sh.event_chara_anim(1, 0, 0, false) -- wait for the animation to end
    elseif step == 5 then
        if sh.pickup_animate("KeyOfOphiel") then
            sh.event_msg(true, "map7_s01.there_is_a_key_of", 6, 7, 0, false) -- take / leave
        end
        sh.flag_set("M7S01_PickupKeyOfOphiel", true)
    elseif step == 6 then
        sh.event_inv_item_cmd(3, "KeyOfOphiel", 1, false) -- add to the inventory
        sh.event.step_set(0, 8) -- jump to the step
    elseif step == 7 or step == 8 then
        if step == 7 then
            sh.flag_set("M7S01_PickupKeyOfOphiel", false)
            sh.event_step_increment(0) -- and continue into step 8 in the same tick
        end
        sh.chara_anim_play_to_end(0, "pickup_stand_up") -- straighten up
    elseif step > 8 then
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 17: message over a background picture.
function MapEvent_ClockKeyInspect()
    sh.event_msg_with_bg("TIM/CLOCKKEY.TIM", 0, 0, "map7_s01.old_clock_there_is_a")
end

-- index 18: the Stone of Time put into the old clock: the clock chimes five times (one per
-- 1.4 s), its glass face bursts into shards (GLASS.TIM in the effect picture), Harry flinches
-- (scene_nowhere_a); M7S01_StoneOfTimeInClock at the start, M7S01_ClockOpened (the clock open, its key in reach) at the end.
local ClockGlassCornerA = {-68264, -5054, -251894} -- (-16.6661, -1.234, -61.49756)
local ClockGlassCornerB = {-68264, -5054, -252713} -- (-16.6661, -1.234, -61.69751)

function MapEvent_StoneOfTimeUse()
    local s = sh.event_step_get(0)
    if s == 0 then
        sh.player_freeze(true)
        sh.screen_fade_reset_timestep()
        sh.cutscene.border_show()

        sh.load.tim("TIM/GLASS.TIM", 1, "effect_tex_img")
        sh.flag_set("M7S01_StoneOfTimeInClock", true)
        sh.sound.play_with_flags("clock_stone_insert", "clock_sfx_pos", 128, 0) -- volume 0.5

        sh.map.set("clock_chime_count", 0)

        sh.camera.position(nil, -63037, -14745, -251781, 0, 0, 0, 0, true) -- (-15.39, -3.6, -61.47)
        sh.camera.look_at(nil, -71188, -737, -249241, 0, 0, 0, 0, true)    -- (-17.38, -0.18, -60.85)

        sh.npc.set_pos_x(0, -68403)  -- -16.7
        sh.npc.set_pos_z(0, -250470) -- -61.15
        sh.npc.set_rot_y(0, 2048)    -- 180 deg

        sh.event_step_increment(0)
        s = 1 -- continues into step 1 in the same tick
    end

    if s == 1 then
        sh.event_fade_cmd(2, false, 0, 0, false) -- the fade command of sub-step 2
    elseif s == 3 then
        if sh.load.chunks() ~= 0 then
            sh.event_step_increment(0)
        end
    elseif s == 4 then
        -- The state timer is the chime clock: at 0 it chimes, then it runs to 1.4 s and wraps to 0.
        if sh.event.state_timer() == 0 then
            sh.sound.play_with_flags("nowhere_clock_chime", "clock_sfx_pos", 128, 0)

            sh.map.set("clock_chime_count", sh.map.get("clock_chime_count") + 1)
            if sh.map.get("clock_chime_count") == 5 then
                sh.event_step_increment(0)
            else
                sh.event.set_state_timer(sh.event.state_timer() + 1)
            end
            return
        end

        sh.event.set_state_timer(sh.event.state_timer() + sh.game.delta_time())
        if sh.event.state_timer() > 5734 then -- 1.4 s
            sh.event.set_state_timer(0)
        end
    elseif s == 5 then
        sh.event_wait_timer(10240, false) -- 2.5 s
    elseif s == 6 or s == 7 then
        if s == 6 then
            sh.vec.set(0, ClockGlassCornerA[1], ClockGlassCornerA[2], ClockGlassCornerA[3])
            sh.vec.set(1, ClockGlassCornerB[1], ClockGlassCornerB[2], ClockGlassCornerB[3])
            sh.map.glass_shatter("vec:0", "vec:1", 9830, 90, 737, 819, 204, 0) -- 2.4, 0.022, 0.18, 0.2, 0.05
            sh.sfx_flags_play("clock_glass_shatter", -64716, -5038, -246661, 128, 0) -- at (-15.8, -1.23, -60.22)
            sh.event_chara_anim(0, 0, "scene_nowhere_a", false)
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(819, false) -- 0.2 s
    elseif s == 8 or s == 9 or s == 2 then
        if s == 8 then
            sh.sfx_flags_play("harry_damage_type_2", -68403, -6144, -249036, 128, 0) -- at (-16.7, -1.5, -60.8)
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(4096, false) -- 1.0 s
    else
        sh.player_freeze(false)
        sh.state_set(0) -- gameplay
        sh.camera.auto_return(true)

        sh.npc.set_pos_z(0, -248627) -- -60.7

        sh.event_fade_cmd(0, false, 2, 0, false)
        sh.flag_set("M7S01_ClockOpened", true)
    end
end

-- index 19: item pickup.
function MapEvent_KeyOfHagithTake()
    sh.event_item_take("KeyOfHagith", 1, "M7S01_PickupKeyOfHagith", "map7_s01.there_is_a_key_of_2")
end

-- index 20: message over a background picture.
function MapEvent_AbusedDeskInspect()
    sh.event_msg_with_bg("TIM/ABUSEDSK.TIM", 0, 0, "map7_s01.go_home")
end

-- index 21: item pickup.
function MapEvent_PliersTake()
    sh.event_item_take("Pliers", 1, "M7S01_PickupPliers", "map7_s01.there_are_pliers_take_them")
end

-- index 22: item pickup.
function MapEvent_ScrewdriverTake()
    sh.event_item_take("Screwdriver", 1, "M7S01_PickupScrewdriver", "map7_s01.there_is_a_screwdriver_take")
end

-- index 23: lithograph plaque: message over a background picture, picture
-- brightness 13/32.
function MapEvent_LithographRead()
    sh.background_gamma(104) -- 13/32
    sh.event_msg_with_bg("TIM/LITHGRPH.TIM", 0, 0, "map7_s01.there_is_a_slate")
end

-- index 24: second lithograph plaque: message over a background picture, picture
-- brightness 13/32.
function MapEvent_Lithograph3Read()
    sh.background_gamma(104) -- 13/32
    sh.event_msg_with_bg("TIM/LITHGR_3.TIM", 0, 0, "map7_s01.names_engraved_on_a_lithograph")
end

-- index 25: ALERTDOR.TIM door keypad: a cursor over 26 letter buttons; the last five
-- presses are compared with the solution (flag M7S01_KeypadDoorUnlocked when they match).
-- keypad_entered_letters starts as five 26s (no letter); keypad_buttons.x/y are the
-- button centres in screen pixels.
local function idiv(a, b)
    local q = a // b
    if q < 0 and q * b ~= a then q = q + 1 end
    return q
end

-- puzzle_cursor_x/y: the cursor position; the stick moves it at a rate tuned for 30 fps (136 = 1/30 s).
local function CursorAxisUpdate(var, axis)
    local v = sh.map.get(var)
    v = v + idiv(idiv(sh.input_axis(1, axis) * 16384, 75) * sh.delta_time_get(), 136)
    v = math.max(math.min(v, 491520), -491520) -- -120..120
    sh.map.set(var, v)
end

local function AlertDoorKeypadPress()
    for i = 0, 25 do
        local bx = sh.map.get("keypad_buttons.x", i)
        local by = sh.map.get("keypad_buttons.y", i)
        local cx = sh.map.get("puzzle_cursor_x") // 4096
        local cy = sh.map.get("puzzle_cursor_y") // 4096
        if not ((bx - 174) > cx or (bx - 146) < cx or (by - 134) > cy or (by - 106) < cy) then
            local dx = bx - 160 - cx
            local dy = by - 120 - cy
            if dx * dx + dy * dy < 197 then
                sh.sd_call("door_buzzer")
                for j = 0, 3 do
                    sh.map.set("keypad_entered_letters", sh.map.get("keypad_entered_letters", j + 1), j)
                end
                sh.map.set("keypad_entered_letters", i, 4)

                local j = 0
                while j < 5 do
                    if sh.map.get("keypad_entered_letters", j) ~= sh.map.get("keypad_solution_letters", j) then break end
                    j = j + 1
                end
                if j == 5 then
                    sh.flag_set("M7S01_KeypadDoorUnlocked", true)
                    sh.event.step_set(0, 5)
                end
            end
        end
    end
end

function MapEvent_AlertDoorKeypad()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_fade_cmd(0, true, 0, 0, false) -- Start
        sh.map.set("puzzle_cursor_y", 0)
        sh.map.set("puzzle_cursor_x", 0)
        for i = 0, 4 do
            sh.map.set("keypad_entered_letters", 26, i)
        end
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_bg_texture_cmd(7, "TIM/ALERTDOR.TIM", false) -- Auto
    elseif step == 2 then
        sh.event_fade_cmd(1, true, 0, 0, false) -- Wait
    elseif step == 3 then
        sh.event_fade_cmd(2, false, 0, 0, false) -- Auto
        sh.event_bg_texture_cmd(2, 0, false)
    elseif step == 4 then
        sh.event_bg_texture_cmd(2, 0, false)

        sh.screen.puzzle_cursor("puzzle_cursor_x", "puzzle_cursor_y")
        CursorAxisUpdate("puzzle_cursor_x", 0)
        CursorAxisUpdate("puzzle_cursor_y", 1)

        sh.game.timer_update()

        local cx = sh.map.get("puzzle_cursor_x") // 4096 -- whole pixels
        local cy = sh.map.get("puzzle_cursor_y") // 4096
        sh.screen.cursor_draw(cx + 8, cy + 8, 8, 8, 0, 64, 32, 32, 128, 192, 0, 12)

        if sh.input_action("cancel", 1) then
            sh.event.step_set(0, 8)
            return
        end
        if sh.input_action("enter", 1) then
            AlertDoorKeypadPress()
        end
    elseif step >= 5 and step <= 7 then
        if step == 6 then
            sh.sd_call("door_seal_mech")
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(2457, false) -- 0.6 s
        sh.event_bg_texture_cmd(2, 0, false)
    elseif step == 8 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_fade_cmd(2, true, 0, 0, false) -- Auto
    else
        sh.player_freeze(false, false)
        sh.state_set(0)
        sh.event_fade_cmd(0, false, 0, 0, false) -- Start
    end
end

-- index 26: item pickup.
function MapEvent_AmuletOfSolomonTake()
    sh.event_item_take("AmuletOfSolomon", 1, "M7S01_PickupAmuletOfSolomon", "map7_s01.there_is_an_amulet_of")
end

-- index 27: NURSE1.DMS cutscene: Lisa in the ward.
local function NurseDmsTail()
    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, 11)
        sh.cutscene.chara(1, "LISA", t, 11)
        sh.cutscene.camera(t, 11, "camera_target_pos", "camera_target_look")
    end
end

local function LisaBloodyLoad()
    sh.npc.image_calc("img:0", 33, 2) -- Bloody Lisa
    sh.npc.load(1, "BloodyLisa", "npc_bone:30", 0, "lm:2", "img:0")
end

local lisaNurseRoomSteps = {}

lisaNurseRoomSteps[0] = function()
    sh.player_freeze(true)
    sh.map.set("voice_msg_audio_idx", 0)
    sh.load.file("ANIM/HBM7_SLS.ANM", 4)   -- HBM7_SLS.ANM
    sh.load.file("ANIM/NURSE1.DMS", 11) -- NURSE1.DMS
    sh.load.wait()
    sh.cutscene.prepare(11)
    sh.map.set("cutscene_timer", 0)
    sh.npc.anim_info_clear()
    sh.npc.load(0, "Lisa", "npc_bone:0", 255, nil, nil) -- Lisa, free all character slots
    sh.screen_fade_reset_timestep()
    sh.cutscene.border_show()
    sh.light.set_intensity(3276) -- 0.8
    sh.cutscene_flag(true)
    sh.light.flashlight_on()
    sh.map.set("nurse_scene_flag", 0)
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.player.weapon_stash()
    sh.held_item_hide()
    sh.sound.bgm_change(4) -- music track 4
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[1]()
end

lisaNurseRoomSteps[1] = function()
    sh.sound.bgm_crossfade(4)
end

lisaNurseRoomSteps[2] = function()
    sh.event_fade_cmd(2, false, 0, 4096, false)
end

lisaNurseRoomSteps[3] = function()
    sh.npc.process_loads()
    sh.npc.spawn("Lisa", 0, 532480, -286720, 0, 3) -- 130.0, -70.0
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[4]()
end

lisaNurseRoomSteps[4] = function()
    sh.msg.show_with_audio("map7_s01.harry", "voice_msg_audio_idx", "nurse_room_voice_cmds[0]")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 0, 81920, true, false)
    sh.event_wait_timer(3276, false)
end

lisaNurseRoomSteps[5] = function()
    sh.event_chara_anim(0, 0, "cutscene_react", false)
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[6]()
end

lisaNurseRoomSteps[6] = function()
    sh.msg.show_with_audio("map7_s01.harry", "voice_msg_audio_idx", "nurse_room_voice_cmds[0]")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 0, 81920, true, false)
end

lisaNurseRoomSteps[7] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 0, 81920, true, true)
end

lisaNurseRoomSteps[8] = function()
    sh.event_chara_anim(0, 1, "stand_talk", false)
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.map.set("cutscene_timer", 86016) -- frame 21.0
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[9]()
end

lisaNurseRoomSteps[9] = function()
    sh.msg.show_with_audio("map7_s01.lisa_whats_the_matter_with", "voice_msg_audio_idx", "nurse_room_voice_cmds[0]")
end

lisaNurseRoomSteps[10] = function()
    sh.msg.show_with_audio("map7_s01.lisa", "voice_msg_audio_idx", "nurse_room_voice_cmds[0]")
end

lisaNurseRoomSteps[11] = function()
    sh.event_chara_anim(0, 1, "lean_back", false)
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[12]()
end

lisaNurseRoomSteps[12] = function()
    sh.msg.show_with_audio("map7_s01.save_me_from_them", "voice_msg_audio_idx", "nurse_room_voice_cmds[0]")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 90112, 397312, false, false)
end

lisaNurseRoomSteps[13] = function()
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[14]()
end

lisaNurseRoomSteps[14] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 90112, 397312, false, true)
end

lisaNurseRoomSteps[15] = function()
    sh.event_chara_anim(0, 0, "scene_lisa_room", false)
    sh.sd_call("lisa_nurse_room_voice") -- xa audio 568 sound
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[16]()
end

lisaNurseRoomSteps[16] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 90112, 540672, true, true)
end

lisaNurseRoomSteps[17] = function()
    sh.event_chara_anim(0, 1, "turn_away", false)
    sh.flag_set("M7S01_LisaWardSceneTurnedAway", true)
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[18]()
end

lisaNurseRoomSteps[18] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 544768, 643072, true, true)
end

lisaNurseRoomSteps[19] = function()
    LisaBloodyLoad()
    sh.npc.set_pos_x(0, 737280)  -- 180.0 m
    sh.npc.set_pos_z(0, -409600) -- -100.0 m
    sh.load.close_range_chunks()
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[20]()
end

lisaNurseRoomSteps[20] = function()
    sh.event_fade_cmd(2, true, 0, 2048, false)
end

lisaNurseRoomSteps[21] = function()
    sh.event_step_reset()
end

lisaNurseRoomSteps[22] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

lisaNurseRoomSteps[23] = function()
    sh.flag_set("M7S01_LisaWardSceneFadedOut", true)
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[24]()
end

lisaNurseRoomSteps[24] = function()
    sh.npc.set_pos_x(0, 737280)
    sh.npc.set_pos_z(0, -409600)
    sh.load.close_range_chunks()
    LisaBloodyLoad()
    sh.npc.process_loads()
    sh.event_step_increment(0)
    return lisaNurseRoomSteps.default()
end

lisaNurseRoomSteps.default = function()
    sh.state_set(0)
    sh.flag_set("M7S01_CutsceneLisaWardDone", true)
    sh.npc.clear(1, 0, 0)
    sh.npc.process_loads()
    sh.sd_call("stop_xa_audio")
end

function Cutscene_LisaNurseRoom()
    if sh.input_action("skip", 1) and sh.event_step_get(0) >= 4 and sh.event_step_get(0) <= 20 then
        sh.event.step_set(0, 22)
    end

    local s = sh.event_step_get(0)
    runStep(lisaNurseRoomSteps, s)
    NurseDmsTail()
end

-- index 28: NURSE2.DMS cutscene: bloody Lisa at the door.
-- lisa_door — the door's world object pose (map state: "lisa_door.pos", "lisa_door.rot").
local function Nurse2DmsTail()
    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, 11)
        sh.cutscene.chara(1, "BLS", t, 11)
        if sh.map.get("door_follows_dms") ~= 0 then
            sh.cutscene.transform_to("lisa_door.pos", "lisa_door.rot", "ADOOR", t, 11)
        end
        sh.cutscene.camera(t, 11, "camera_target_pos", "camera_target_look")
    end
end

local function Adv(startQ12, endQ12, clamp, auto)
    sh.cutscene.timer_advance("cutscene_timer", 40960, startQ12, endQ12, clamp, auto) -- rate 10.0
end

local bloodyLisaDoorSteps = {}

bloodyLisaDoorSteps[0] = function()
    sh.load.file("ANIM/NURSE2.DMS", 11) -- NURSE2.DMS
    sh.load.wait()
    sh.cutscene.prepare(11)
    sh.npc.spawn("BloodyLisa", 0, sh.npc.pos_x(0), sh.npc.pos_z(0) + 4096, 0, 3) -- Bloody Lisa
    sh.map.set("cutscene_timer", 0)
    sh.map.set("voice_msg_audio_idx", 0)
    sh.map.set("door_follows_dms", 0)
    sh.event_step_increment(0)
end

bloodyLisaDoorSteps[1] = function()
    sh.event_chara_anim(0, 0, "scene_bloody_lisa_1", false)
    sh.event_chara_anim(0, 1, "appear", false)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_step_increment(0)
    return bloodyLisaDoorSteps[2]()
end

bloodyLisaDoorSteps[2] = function()
    Adv(0, 176128, true, true)        -- [0, 43]
end

bloodyLisaDoorSteps[3] = function()
    Adv(180224, 241664, true, true)   -- [44, 59]
end

bloodyLisaDoorSteps[4] = function()
    sh.event_chara_anim(0, 0, "scene_bloody_lisa_2", false)
    sh.light.bone_detach()
    sh.light.lens_flare_detach()
    sh.light.set_intensity(4096)
    sh.water_effect_enable()
    sh.event_step_increment(0)
    return bloodyLisaDoorSteps[5]()
end

bloodyLisaDoorSteps[5] = function()
    Adv(245760, 311296, true, true)   -- [60, 76]
    sh.cutscene.light(sh.map.get("cutscene_timer"), 11)
end

bloodyLisaDoorSteps[6] = function()
    sh.map.set("door_follows_dms", 1)
    sh.water_effect_disable()
    sh.light.flashlight_fix()
    sh.light.set_intensity(3276) -- 0.8
    sh.sound.play_with_flags("otherworld_door_open", "lisa_door.pos", 128, 0)
    sh.event_step_increment(0)
    return bloodyLisaDoorSteps[7]()
end

bloodyLisaDoorSteps[7] = function()
    Adv(315392, 409600, true, true)   -- [77, 100]
end

bloodyLisaDoorSteps[8] = function()
    sh.event_chara_anim(0, 0, "scene_bloody_lisa_3", false)
    sh.npc.clear(1, 0, 0)
    sh.sound.play_with_flags("otherworld_door_close", "lisa_door.pos", 128, 0)
    sh.event_step_increment(0)
    return bloodyLisaDoorSteps[9]()
end

bloodyLisaDoorSteps[9] = function()
    Adv(413696, 622592, true, false)  -- [101, 152]
    sh.event_wait_timer(1228, false)  -- 0.3 s
end

bloodyLisaDoorSteps[10] = function()
    sh.sd_call("bloody_lisa_door_voice")
    sh.event_step_increment(0)
    return bloodyLisaDoorSteps[11]()
end

bloodyLisaDoorSteps[11] = function()
    Adv(413696, 581632, false, true)  -- [101, 142]
end

bloodyLisaDoorSteps[12] = function()
    sh.event_step_increment(0)
    return bloodyLisaDoorSteps[13]()
end

bloodyLisaDoorSteps[13] = function()
    Adv(413696, 622592, true, false)
    sh.event_fade_cmd(2, true, 0, 1024, false) -- 0.25
end

bloodyLisaDoorSteps[14] = function()
    if sh.sound.streaming() ~= 0 then
        return
    end
    sh.event_step_increment(0)
    return bloodyLisaDoorSteps[15]()
end

bloodyLisaDoorSteps[15] = function()
    sh.event_wait_timer(8192, false) -- 2.0 s
end

bloodyLisaDoorSteps[16] = function()
    sh.event.step_set(0, 19)
end

bloodyLisaDoorSteps[17] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

bloodyLisaDoorSteps[18] = function()
    sh.sd_call("stop_xa_audio")
    sh.npc.clear(1, 0, 0)
    sh.event_step_increment(0)
end

bloodyLisaDoorSteps[19] = function()
    sh.player.map_anim_load(sh.save.map_idx())
    sh.load.wait()
    sh.event_step_increment(0)
    return bloodyLisaDoorSteps.default()
end

bloodyLisaDoorSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.map.set("cutscene_timer", -1) -- not running
    sh.camera_auto_return(true)
    sh.flag_set("M7S01_CutsceneBloodyLisaDone", true)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.water_effect_disable()
    sh.light.flashlight_fix()
    sh.light.set_intensity(4096)
    sh.light.flashlight_on()
    sh.player.weapon_restore()
    sh.held_item_show()
    sh.npc.anim_info_clear()
    sh.event_fade_cmd(0, false, 0, 4096, false)
end

function Cutscene_BloodyLisaDoor()
    if sh.input_action("skip", 1) and sh.event_step_get(0) >= 2 and sh.event_step_get(0) <= 15 then
        sh.event.step_set(0, 17)
    end

    local s = sh.event_step_get(0)
    runStep(bloodyLisaDoorSteps, s)
    Nurse2DmsTail()
end

-- index 29: read Lisa's diary (8 pages, LISDIARY.TIM); LisdiaryMsgs is the page
-- message order, s_diaryPage the current page.
local LisdiaryMsgs = {
    "map7_s01.ask_doctor_to_let_me",
    "map7_s01.still_alive_but_with_wounds",
    "map7_s01.told_the_doctor_i_quit",
    "map7_s01.the_room_is_filled_with",
    "map7_s01.feeling_bad_need_to_throw",
    "map7_s01.blood_and_pus_flow_from",
    "map7_s01.need_drug",
    "map7_s01.help_me",
}
local s_diaryPage = 0

function MapEvent_LisaDiaryRead()
    sh.background_gamma(72) -- 9/32
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_bg_texture_cmd(0, "TIM/LISDIARY.TIM", false) -- start loading, LISDIARY.TIM
        s_diaryPage = 0
        sh.event_step_increment(0)
        step = 1 -- and continue into step 1 in the same tick
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.chara_anim_play_to_end(0, "pickup_crouch")
    elseif step == 3 then
        sh.event_msg(false, "map7_s01.there_is_a_diary_on")
    elseif step == 4 then
        sh.event_bg_texture_cmd(1, 0, false) -- wait for loading
    elseif step == 5 then
        sh.event_fade_cmd(2, true, 0, 0, false)  -- Auto, out
    elseif step == 6 then
        sh.event_fade_cmd(2, false, 0, 0, false) -- Auto, in
        sh.event_bg_texture_cmd(2, 0, false)     -- draw
    elseif step == 7 then
        sh.sfx(28) -- menu map sound = base sound + 0x1C
        sh.event_step_increment(0)
        step = 8 -- and continue into step 8 in the same tick
    end
    if step == 8 then
        sh.event_wait_timer(1228, false) -- 0.3 s
        sh.event_bg_texture_cmd(2, 0, false)
    elseif step == 9 then
        sh.event_msg(false, LisdiaryMsgs[s_diaryPage + 1])
        sh.event_bg_texture_cmd(2, 0, false)
    elseif step == 10 then
        sh.event_bg_texture_cmd(2, 0, false)
        s_diaryPage = s_diaryPage + 1
        if s_diaryPage >= 8 then
            sh.event_step_increment(0)
        else
            sh.event.step_set(0, 7) -- jump to the step
        end
    elseif step == 11 then
        sh.event_fade_cmd(2, true, 0, 0, false)
        sh.event_bg_texture_cmd(2, 0, false)
    elseif step == 12 then
        sh.event_fade_cmd(2, false, 0, 0, false)
    elseif step == 13 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")
    elseif step < 0 or step > 13 then -- leave the diary
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- Integer helpers of the panel puzzles (C semantics: division truncates toward zero,
-- 16-bit fields wrap).
local function PanelTDiv(a, b)
    local q = a // b
    if q < 0 and q * b ~= a then q = q + 1 end
    return q
end

local function PanelS16(v)
    v = v & 0xFFFF
    if v >= 0x8000 then v = v - 0x10000 end
    return v
end

-- Q12 product with a 64-bit intermediate (Q12_MULT_PRECISE).
local function PanelMulQ12(a, b) return (a * b) // 4096 end

-- puzzle_cursor_x/y += stick * scale / 75 per 1/30 s (136 = 1/30 s), clamped to +-limit.
local function PanelCursorAxis(var, axis, scale, limit)
    local v = sh.map.get(var) + PanelTDiv(PanelTDiv(sh.input_axis(1, axis) * scale, 75) * sh.delta_time_get(), 136)
    v = math.max(math.min(v, limit), -limit)
    sh.map.set(var, v)
end

local function PanelCursorDraw()
    local cx = sh.map.get("puzzle_cursor_x") // 4096 -- whole pixels
    local cy = sh.map.get("puzzle_cursor_y") // 4096
    sh.screen.cursor_draw(PanelS16(cx + 8), cy + 8, 8, 8, 0, 64, 32, 32, 128, 192, 0, 12)
end

-- index 30: the stone panels. The event sets one of M7S01_StonePanel0Viewing..M7S01_StonePanel6Viewing when it
-- starts; that picks the picture (stone_panel_pictures). Pictures 1, 5 and 6 carry a 3x3
-- dial whose chosen cell (1..9) is kept in four flags each (M7S01_StonePanel1DialBit0.., M7S01_StonePanel5DialBit0..,
-- M7S01_StonePanel6DialBit0..); when the twelve flags spell stone_panel_solution the Stone of Time
-- (time_stone) drops into the water and M7S01_StoneOfTimeReleased lets it be taken.
local stonePanelCell = 0 -- the chosen cell of the open panel, 0 = none
local stonePanelIdx = 0 -- which of the seven pictures is open
local stoneFallSpeed = 0 -- unsigned 16-bit, like the original variable

-- The viewing flag of each panel picture 0..6, and the twelve dial flags in the order the
-- solution counts them: four bits each of panels 1, 5 and 6.
local StonePanelViewingFlags = {[0] = "M7S01_StonePanel0Viewing", "M7S01_StonePanel1Viewing", "M7S01_StonePanel2Viewing", "M7S01_StonePanel3Viewing", "M7S01_StonePanel4Viewing", "M7S01_StonePanel5Viewing", "M7S01_StonePanel6Viewing"}
local StonePanelDialBits = {"M7S01_StonePanel1DialBit0", "M7S01_StonePanel1DialBit1", "M7S01_StonePanel1DialBit2", "M7S01_StonePanel1DialBit3", "M7S01_StonePanel5DialBit0", "M7S01_StonePanel5DialBit1", "M7S01_StonePanel5DialBit2", "M7S01_StonePanel5DialBit3", "M7S01_StonePanel6DialBit0", "M7S01_StonePanel6DialBit1", "M7S01_StonePanel6DialBit2", "M7S01_StonePanel6DialBit3"}

local StonePanelDialOf = {
    [1] = {"M7S01_StonePanel1DialBit0", "M7S01_StonePanel1DialBit1", "M7S01_StonePanel1DialBit2", "M7S01_StonePanel1DialBit3"},
    [5] = {"M7S01_StonePanel5DialBit0", "M7S01_StonePanel5DialBit1", "M7S01_StonePanel5DialBit2", "M7S01_StonePanel5DialBit3"},
    [6] = {"M7S01_StonePanel6DialBit0", "M7S01_StonePanel6DialBit1", "M7S01_StonePanel6DialBit2", "M7S01_StonePanel6DialBit3"},
}

-- The four dial flags of the open panel: those of panel 1 or 5, for any other panel those of panel 6.
local function StonePanelDialFlags()
    return StonePanelDialOf[stonePanelIdx] or StonePanelDialOf[6]
end

-- The chosen cell marker: a 14x14 piece of the panel picture (texels 320, 0) over its cell.
local function StonePanelMarkDraw()
    if stonePanelCell == 0 then return end
    local s = sh.event_step_get(0)
    if (s >= 8 and s <= 10) or s == 15 then
        local n = stonePanelCell - 1
        sh.render.sprite{texture = "background", layer = "picture",
            rect = {(n % 3) * 35 - 42, (n // 3) * 35 - 32, 14, 14}, uv = {320, 0}}
    end
end

-- Enter over a cell: it becomes the chosen one; its number goes to the panel's flags. The
-- original's flag loop reuses the row counter, so the scan ends after the first hit.
local function StonePanelPress()
    local cx = sh.map.get("puzzle_cursor_x") // 4096
    local cy = sh.map.get("puzzle_cursor_y") // 4096
    for i = 0, 2 do
        for j = 0, 2 do
            local col = j * 35
            local row = i * 35
            if not (cx < col - 46 or cx > col - 24) and not (cy < row - 37 or cy > row - 15) then
                local dx = col - (cx + 35)
                local dy = row - (cy + 26)
                if dx * dx + dy * dy < 122 then
                    stonePanelCell = j + i * 3 + 1
                    sh.sd_call("stone_panel_dial_press")
                    local dial = StonePanelDialFlags()
                    for b = 0, 3 do
                        sh.flag_set(dial[b + 1], ((stonePanelCell >> b) & 1) ~= 0)
                    end
                    local code = 0
                    for b = 0, 11 do
                        if sh.flag_get(StonePanelDialBits[b + 1]) then code = code + (1 << b) end
                    end
                    if sh.map.get("stone_panel_solution") == code then
                        sh.event.step_set(0, 10)
                    end
                    return
                end
            end
        end
    end
end

-- The stone slides out (2 s), falls onto the ledge, tips over and drops into the water.
local function StoneFall()
    local sub_step = sh.event_step_get(1)
    local dt = sh.delta_time_get()
    if sub_step == 0 then
        sh.sound.play_with_flags("time_stone_slide_loop", "time_stone.pos", 128, 0) -- 0.5, no flags
        sh.event_step_increment(1)
        sub_step = 1
    end
    if sub_step == 1 then
        if sh.state_timer_get() + dt > 8192 then -- 2.0 s
            sh.state_timer_set(8192)
            sh.world.object_model_name_set("time_stone", "STONE3_H")
            sh.event_step_increment(1)
            sh.sfx_stop("time_stone_slide_loop")
        else
            sh.state_timer_set(sh.state_timer_get() + dt)
        end
        sh.map.set("time_stone.pos.z", PanelTDiv(sh.state_timer_get() * 409, 8192) - 575897) -- 0.1 per 2 s from -140.6
    elseif sub_step == 2 then
        stoneFallSpeed = (stoneFallSpeed + sh.game.gravity_speed() // 2) & 0xFFFF
        local y = sh.map.get("time_stone.pos.y")
        if y < -4300 then -- -1.05
            y = y + PanelMulQ12(dt, stoneFallSpeed)
            sh.map.set("time_stone.pos.y", y)
            if y > -4300 then
                sh.map.set("time_stone.pos.y", -4300)
                sh.event_step_increment(1)
                sh.state_timer_set(0)
                stoneFallSpeed = 0
                sh.sound.play_with_flags("time_stone_land", "time_stone.pos", 128, 0)
            end
        end
    elseif sub_step == 3 then
        local rz = PanelS16(sh.map.get("time_stone.rot.z") + PanelMulQ12(dt, -284)) -- -25 deg/s
        sh.map.set("time_stone.rot.z", rz)
        sh.map.set("time_stone.pos.y", (sh.math_cos(rz) * 614) // 4096 - 4914) -- 0.15, 1.2
        sh.map.set("time_stone.pos.z", (sh.math_sin(rz) * -614) // 4096 - 575488) -- -0.15, 140.5
        sh.state_timer_set(sh.state_timer_get() + dt)
        if sh.state_timer_get() > 2048 then -- 0.5 s
            sh.event_step_increment(1)
            sh.state_timer_set(0)
        end
    elseif sub_step == 4 then
        stoneFallSpeed = (stoneFallSpeed + sh.game.gravity_speed()) & 0xFFFF
        sh.map.set("time_stone.rot.z", PanelS16(sh.map.get("time_stone.rot.z") + PanelMulQ12(dt, -1365))) -- -120 deg/s
        sh.map.set("time_stone.pos.y", sh.map.get("time_stone.pos.y") + PanelMulQ12(stoneFallSpeed, dt))
        sh.map.set("time_stone.pos.z", sh.map.get("time_stone.pos.z") + PanelMulQ12(dt, 1228)) -- 0.3
        if sh.map.get("time_stone.pos.y") > 0 then
            sh.event_step_increment(1)
        end
    elseif sub_step == 5 then
        sh.map.set("time_stone.pos.x", -571392) -- -139.5
        sh.map.set("time_stone.pos.y", -204) -- -0.05
        sh.map.set("time_stone.pos.z", -573440) -- -140.0
        sh.map.set("time_stone.rot.z", -1024) -- -0.25
        sh.sound.play_with_flags("time_stone_set", "time_stone.pos", 128, 0)
        sh.event_step_increment(0)
    end
end

local stonePanelPuzzleSteps = {}

stonePanelPuzzleSteps[0] = function()
    sh.player_freeze(true)
    stonePanelCell = 0
    stonePanelIdx = 0
    while not sh.flag_get(StonePanelViewingFlags[stonePanelIdx]) do
        stonePanelIdx = stonePanelIdx + 1
    end
    sh.event_bg_texture_cmd(0, sh.map.get("stone_panel_pictures", stonePanelIdx), false) -- start loading
    sh.event_step_increment(0)
    return stonePanelPuzzleSteps[1]()
end

stonePanelPuzzleSteps[1] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto
    return stonePanelPuzzleSteps[2]()
end

stonePanelPuzzleSteps[2] = function()
    sh.event_bg_texture_cmd(1, 0, false) -- wait for loading
end

stonePanelPuzzleSteps[3] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
    sh.event_bg_texture_cmd(2, 0, false) -- draw
end

stonePanelPuzzleSteps[4] = function()
    if sh.input_action("enter", 1) then
        if not sh.flag_get("M7S01_StoneOfTimeReleased") and (stonePanelIdx == 1 or stonePanelIdx == 5 or stonePanelIdx == 6) then
            sh.event.step_set(0, 5)
        else
            sh.event.step_set(0, 15)
        end
    elseif sh.input_action("cancel", 1) then
        sh.event.step_set(0, 15)
    end
    sh.event_bg_texture_cmd(2, 0, false)
end

stonePanelPuzzleSteps[6] = function()
    sh.render.texture_load{file = "TIM/HS_PANEL.TIM", texture = "background", buffer = "image"}
    sh.load.wait()
    sh.event_step_increment(0)
    return stonePanelPuzzleSteps[7]()
end

stonePanelPuzzleSteps[7] = function()
    do
        local dial = StonePanelDialFlags()
        for b = 0, 3 do
            if sh.flag_get(dial[b + 1]) then
                stonePanelCell = (stonePanelCell + (1 << b)) & 0xFF
            end
        end
    end
    sh.map.set("puzzle_cursor_x", 0)
    sh.map.set("puzzle_cursor_y", 0)
    sh.event_step_increment(0)
    return stonePanelPuzzleSteps[8]()
end

stonePanelPuzzleSteps[8] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.event_fade_cmd(2, false, 0, 0, false)
end

stonePanelPuzzleSteps[9] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.screen.puzzle_cursor("puzzle_cursor_x", "puzzle_cursor_y")
    PanelCursorAxis("puzzle_cursor_x", 0, 16384, 327680) -- +-80
    PanelCursorAxis("puzzle_cursor_y", 1, 16384, 327680)
    sh.game.timer_update()
    PanelCursorDraw()
    if sh.input_action("cancel", 1) then
        sh.event.step_set(0, 15)
        return
    end
    if sh.input_action("enter", 1) then
        StonePanelPress()
    end
end

stonePanelPuzzleSteps[11] = function()
    sh.cutscene.border_show()
    stoneFallSpeed = 0
    sh.npc.anim_flags_clear(0, 2) -- hide the player model
    sh.water_effect_enable()
    sh.camera.position(nil, -565862, -7004, -568401, 0, 0, 0, 0, true) -- (-138.15, -1.71, -138.77)
    sh.camera.look_at(nil, -575733, -4218, -581140, 0, 0, 0, 0, true) -- (-140.56, -1.03, -141.88)
    sh.world.object_model_name_set("time_stone", "STONE2_H")
    sh.event_step_increment(0)
    return stonePanelPuzzleSteps[12]()
end

stonePanelPuzzleSteps[12] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
end

stonePanelPuzzleSteps[13] = function()
    StoneFall()
end

stonePanelPuzzleSteps[14] = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.camera_auto_return(true)
    sh.water_effect_disable()
    sh.npc.anim_flags_set(0, 2) -- show the player model
    sh.flag_set(StonePanelViewingFlags[stonePanelIdx], false)
    sh.flag_set("M7S01_StoneOfTimeReleased", true)
    sh.event_fade_cmd(0, false, 2, 0, false)
end

stonePanelPuzzleSteps[15] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.event_fade_cmd(2, true, 0, 0, false)
end

stonePanelPuzzleSteps[5] = stonePanelPuzzleSteps[15]
stonePanelPuzzleSteps[10] = stonePanelPuzzleSteps[15]

stonePanelPuzzleSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.flag_set(StonePanelViewingFlags[stonePanelIdx], false)
end

function MapEvent_StonePanelPuzzle()
    StonePanelMarkDraw()

    local s = sh.event_step_get(0)
    runStep(stonePanelPuzzleSteps, s)
end

-- index 31: take the Stone of Time with the inspection animation
-- (the same sequence as the steel pipe pickup).
function MapEvent_StoneOfTimeTake()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_inv_item_cmd(0, "StoneOfTime", 0, false) -- start loading
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
        return
    end
    if step == 2 then
        sh.event_chara_anim(0, 0, "pickup_crouch", false) -- start the animation, bend down
        sh.event_step_increment(0)
        step = 3 -- and continue into step 3 in the same tick
    end
    if step == 3 then
        sh.event_inv_item_cmd(1, "StoneOfTime", 0, false) -- wait for loading
    elseif step == 4 then
        sh.event_chara_anim(1, 0, 0, false) -- wait for the animation to end
    elseif step == 5 then
        if sh.pickup_animate("StoneOfTime") then
            sh.event_msg(true, "map7_s01.there_is_a_stone_of", 6, 7, 0, false) -- take / leave
        end
        sh.flag_set("M7S01_PickupStoneOfTime", true) -- flag: pickup stone of time
    elseif step == 6 then
        sh.event_inv_item_cmd(3, "StoneOfTime", 1, false) -- add to the inventory
        sh.event.step_set(0, 8) -- jump to the step
    elseif step == 7 or step == 8 then
        if step == 7 then
            sh.flag_set("M7S01_PickupStoneOfTime", false)
            sh.event_step_increment(0) -- and continue into step 8 in the same tick
        end
        sh.chara_anim_play_to_end(0, "pickup_stand_up") -- straighten up
    elseif step > 8 then
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 32: item pickup.
function MapEvent_CrestOfMercuryTake()
    sh.event_item_take("CrestOfMercury", 1, "M7S01_PickupCrestOfMercury", "map7_s01.there_is_a_crest_of")
end

-- index 33: item pickup.
function MapEvent_RingOfContractTake()
    sh.event_item_take("RingOfContract", 1, "M7S01_PickupRingOfContract", "map7_s01.there_is_a_ring_of")
end

-- index 34: item pickup.
function MapEvent_CutsceneCherylRedirect3()
    sh.event_item_take("Camera", 1, "M7S01_PickupCamera", "map7_s01.there_is_a_camera_take")
end

-- index 35: DRIVR.DMS cutscene, arriving at the hospital. screwdriver_sfx_timer
-- (map state) times the repeating engine sound engine_idle.
local function ArriveEngineSfx()
    local v = sh.map.get("screwdriver_sfx_timer")
    if v == 0 then
        sh.sfx_flags_play("engine_idle", -258867, -5939, -569548, 128, 0) -- at (-63.2, -1.45, -139.05) m, volume 0.5
        sh.map.set("screwdriver_sfx_timer", 4915) -- 1.2 s
    else
        sh.map.set("screwdriver_sfx_timer", v - sh.delta_time_get())
        if sh.map.get("screwdriver_sfx_timer") < 0 then
            sh.map.set("screwdriver_sfx_timer", 0)
        end
    end
end

local arriveAtHospitalSteps = {}

arriveAtHospitalSteps[0] = function()
    sh.player_freeze(true)
    sh.cutscene.border_show()
    sh.cutscene_flag(true)
    sh.load.file("ANIM/DRIVR.DMS", 11) -- DRIVR.DMS
    sh.load.wait()
    sh.cutscene.prepare(11)
    sh.event_chara_anim(0, 0, "scene_arrive_hospital", false)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.map.set("screwdriver_sfx_timer", 1228) -- 0.3 s
    sh.map.set("cutscene_timer", 0)
    sh.event_step_increment(0)
    return arriveAtHospitalSteps[1]()
end

arriveAtHospitalSteps[1] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 0, 126976, false, true) -- 10, 0, 31
    ArriveEngineSfx()
end

arriveAtHospitalSteps[2] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
    sh.cutscene.timer_advance("cutscene_timer", 40960, 0, 131072, true, false) -- 10, 0, 32
    ArriveEngineSfx()
end

arriveAtHospitalSteps[3] = function()
    sh.sfx_flags_play("fuse_box_door", -257638, 0, -569753, 128, 0) -- at (-62.9, 0.0, -139.1) m
    sh.event_step_increment(0)
    return arriveAtHospitalSteps[4]()
end

arriveAtHospitalSteps[4] = function()
    sh.event_wait_timer(2048, false) -- 0.5 s
end

arriveAtHospitalSteps.default = function()
    sh.player_freeze(false, true)
    sh.state_set(0)
    sh.camera_auto_return(true)
    sh.map.set("cutscene_timer", -1) -- cutscene stopped
    sh.flag_set("M7S01_FuseBoxPlateRemoved", true)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_fade_cmd(0, false, 2, 0, false)
end

function Cutscene_ArriveAtHospital()
    local s = sh.event_step_get(0)
    runStep(arriveAtHospitalSteps, s)
    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, 11)
        sh.cutscene.camera(t, 11, "camera_target_pos", "camera_target_look")
    end
end

-- index 36: the electric wire: message over a background picture, plus a crackle
-- (sound electric_spark) at random intervals of 0.2..2.2 s during sub-steps 3..5.
-- The timer is map state: the map's world update drives the same crackle.
function MapEvent_ElectricWireInspect()
    sh.event_msg_with_bg("TIM/ELCWIRE1.TIM", 0, 0, "map7_s01.there_is_a_key")
    local step1 = sh.event_step_get(1)
    if step1 >= 3 and step1 <= 5 then
        local timer = sh.map.get("wire_spark_sfx_timer")
        if timer ~= 0 then
            timer = timer - sh.delta_time_get()
            if timer < 0 then
                timer = 0
            end
            sh.map.set("wire_spark_sfx_timer", timer)
        else
            sh.sound.sd_call("electric_spark")
            sh.map.set("wire_spark_sfx_timer", sh.rng.int(819, 9010)) -- 0.2..2.2 s
        end
    end
end

-- index 37: spawn the ghost of Alessa, then
-- animate npc[0] and set flags 562/223.
function MapEvent_GhostAlessaSpawn()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.screen_fade_reset_timestep()
        sh.chara_load(0, "GhostChildAlessa", 1) -- ghost child Alessa, free all character slots
        sh.chara_process_loads()
        sh.chara_spawn("GhostChildAlessa", 0, 234291, -83558, -1024, 3) -- (57.2, -20.4) m, facing -90 deg
        sh.event_step_increment(0)
    else
        sh.event_chara_anim(0, 1, "walk_off", false) -- start the ghost's animation
        sh.event_fade_cmd(0, false, 0, 0, false) -- Start
        sh.player_freeze(false)
        sh.state_set(0)
        sh.flag_set("M7S01_GhostAlessaAtElevatorSeen", true)
        sh.flag_set("HospitalElevatorArrivedAt3F", true)
    end
end

-- index 38: 3x3 plaques: texture/message picked by flags 496/498/499
-- (message over a background picture), then flags 496/497 are cleared.
function MapEvent_Puzzle3x3HintView()
    if sh.flag_get("M7S01_ExaminingDarknessPicture") then
        if sh.flag_get("M7S01_DarknessPicturePhotographed") then
            sh.event_msg_with_bg("TIM/3X3HINT2.TIM", 0, 0, "map7_s01.the_light_illuminating_the")
        else
            sh.event_msg_with_bg("TIM/3X3PICT2.TIM", 0, 0, "map7_s01.the_light_illuminating_the")
        end
    else
        if sh.flag_get("M7S01_FuturePicturePhotographed") then
            sh.event_msg_with_bg("TIM/3X3HINT1.TIM", 0, 0, "map7_s01.the_light_to_the_future")
        else
            sh.event_msg_with_bg("TIM/3X3PICT1.TIM", 0, 0, "map7_s01.the_light_to_the_future")
        end
    end
    sh.flag_set("M7S01_ExaminingDarknessPicture", false)
    sh.flag_set("M7S01_ExaminingFuturePicture", false)
end

local puzzle3x3PhotoTakeSteps = {}

puzzle3x3PhotoTakeSteps[0] = function()
    sh.player_freeze(true)
    if sh.flag_get("M7S01_ExaminingDarknessPicture") then
        sh.event_bg_texture_cmd(0, "TIM/3X3HINT2.TIM", false) -- start loading
        sh.flag_set("M7S01_DarknessPicturePhotographed", true)
    else
        sh.event_bg_texture_cmd(0, "TIM/3X3HINT1.TIM", false)
        sh.flag_set("M7S01_FuturePicturePhotographed", true)
    end
    sh.screen.set_fade_status(0) -- reset the fade
    sh.sound.set_bgm_flags(sh.sound.bgm_flags() | 1) -- pause the music
    sh.sd_call("photo_pick_up")
    sh.event_step_increment(0)
end

puzzle3x3PhotoTakeSteps[1] = function()
    sh.event_fade_cmd(2, true, 1, 10240, false) -- Auto, type 1, 2.5
    sh.sound.set_bgm_flags(sh.sound.bgm_flags() | 1)
end

puzzle3x3PhotoTakeSteps[2] = function()
    sh.event_bg_texture_cmd(1, 0, false) -- wait for loading
end

puzzle3x3PhotoTakeSteps[3] = function()
    sh.event_fade_cmd(2, false, 1, 1638, false) -- Auto, type 1, 0.4
    sh.event_bg_texture_cmd(2, 0, false) -- draw
end

puzzle3x3PhotoTakeSteps[4] = function()
    if sh.flag_get("M7S01_ExaminingDarknessPicture") then
        sh.msg.show(false, "map7_s01.the_light_illuminating_the", 0, 0, 0, false)
    else
        sh.msg.show(false, "map7_s01.the_light_to_the_future", 0, 0, 0, false)
    end
    sh.event_bg_texture_cmd(2, 0, false)
end

puzzle3x3PhotoTakeSteps[5] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
    sh.event_bg_texture_cmd(2, 0, false)
end

puzzle3x3PhotoTakeSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.flag_set("M7S01_ExaminingDarknessPicture", false)
    sh.flag_set("M7S01_ExaminingFuturePicture", false)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_inv_item_cmd(3, "Camera", 1, false) -- add to the inventory
end

-- index 39: take the 3x3 hint photo (and the camera into the inventory).
function MapEvent_Puzzle3x3PhotoTake()
    local s = sh.event_step_get(0)
    runStep(puzzle3x3PhotoTakeSteps, s)
end

-- index 40: the 3x3 door (3X3DOR.TIM, the same puzzle as map7_s02): three 3x3 grids of
-- lamps (door_3x3_cells: screen pixel centres, cell [grid][column][row]); Enter over a lamp
-- toggles it (bit grid * 9 + row * 3 + column). The pattern door_3x3_solution_a opens the door
-- of M7S01_Door3x3AExamining, door_3x3_solution_b the one of M7S01_Door3x3BExamining; M7S01_Door3x3AUnlocked / M7S01_Door3x3BUnlocked records which.
local door3x3Lit = 0

local function Door3x3Cell(i, j, k)
    local n = i * 9 + j * 3 + k
    return sh.map.get("door_3x3_cells.x", n), sh.map.get("door_3x3_cells.y", n)
end

-- The lit lamps: 9x9 pieces of the door picture (texels 320, 0) over their cells.
local function Door3x3LampsDraw()
    local lamps = {}
    for i = 0, 2 do
        for j = 0, 2 do
            for k = 0, 2 do
                if (door3x3Lit & (1 << (i * 9 + k * 3 + j))) ~= 0 then
                    local x, y = Door3x3Cell(i, j, k)
                    lamps[#lamps + 1] = {rect = {x - 164, y - 124, 9, 9}}
                end
            end
        end
    end
    sh.render.sprite{texture = "background", layer = "picture", uv = {320, 0}, sprites = lamps}
end

local function Door3x3Press()
    for i = 0, 2 do
        for j = 0, 2 do
            for k = 0, 2 do
                local x, y = Door3x3Cell(i, j, k)
                local cx = sh.map.get("puzzle_cursor_x") // 4096
                local cy = sh.map.get("puzzle_cursor_y") // 4096
                if not ((x - 169) > cx or (x - 151) < cx) and not ((y - 129) > cy or (y - 111) < cy) then
                    local dx = x - 160 - cx
                    local dy = y - 120 - cy
                    if dx * dx + dy * dy <= 81 then
                        local bit = 1 << (i * 9 + k * 3 + j)
                        if (door3x3Lit & bit) == 0 then
                            door3x3Lit = door3x3Lit + bit
                        else
                            door3x3Lit = door3x3Lit - bit
                        end
                        sh.sd_call("lamp_panel_toggle")
                        if (door3x3Lit == sh.map.get("door_3x3_solution_a") and sh.flag_get("M7S01_Door3x3AExamining")) or
                            (door3x3Lit == sh.map.get("door_3x3_solution_b") and sh.flag_get("M7S01_Door3x3BExamining")) then
                            sh.event.step_set(0, 5)
                        end
                    end
                end
            end
        end
    end
end

local puzzle3x3DoorSteps = {}

puzzle3x3DoorSteps[0] = function()
    sh.player_freeze(true)
    sh.event_step_increment(0)
    return puzzle3x3DoorSteps[1]()
end

puzzle3x3DoorSteps[1] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto
end

puzzle3x3DoorSteps[2] = function()
    sh.sound.set_bgm_flags(sh.sound.bgm_flags() | 1) -- pause the music
    sh.render.texture_load{file = "TIM/3X3DOR.TIM", texture = "background", buffer = "image"}
    sh.load.wait()
    sh.map.set("puzzle_cursor_x", 0)
    sh.map.set("puzzle_cursor_y", 0)
    door3x3Lit = 0
    sh.event_step_increment(0)
    return puzzle3x3DoorSteps[3]()
end

puzzle3x3DoorSteps[3] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
    sh.event_bg_texture_cmd(2, 0, false) -- draw
end

puzzle3x3DoorSteps[4] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.screen.puzzle_cursor("puzzle_cursor_x", "puzzle_cursor_y")
    PanelCursorAxis("puzzle_cursor_x", 0, 8192, 471040) -- +-115
    PanelCursorAxis("puzzle_cursor_y", 1, 8192, 430080) -- +-105
    sh.game.timer_update()
    PanelCursorDraw()
    if sh.input_action("cancel", 1) then
        sh.sd_call("lamp_panel_toggle")
        if door3x3Lit ~= 0 then
            door3x3Lit = 0
        else
            sh.event.step_set(0, 8)
        end
        return
    end
    if sh.input_action("enter", 1) then
        Door3x3Press()
    end
end

puzzle3x3DoorSteps[5] = function()
    sh.event_wait_timer(2457, false) -- 0.6 s
    sh.event_bg_texture_cmd(2, 0, false)
end

puzzle3x3DoorSteps[6] = function()
    sh.sd_call("door_seal_mech")
    if not sh.flag_get("M7S01_Door3x3AExamining") then
        sh.flag_set("M7S01_Door3x3BUnlocked", true)
    else
        sh.flag_set("M7S01_Door3x3AUnlocked", true)
    end
    sh.event_step_increment(0)
    return puzzle3x3DoorSteps[7]()
end

puzzle3x3DoorSteps[7] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.event_wait_timer(2048, false) -- 0.5 s
end

puzzle3x3DoorSteps[8] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.event_fade_cmd(2, true, 0, 0, false)
end

puzzle3x3DoorSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.flag_set("M7S01_Door3x3AExamining", false)
    sh.flag_set("M7S01_Door3x3BExamining", false)
end

function MapEvent_Puzzle3x3Door()
    local s = sh.event_step_get(0)
    if s >= 4 and s <= 8 then
        Door3x3LampsDraw()
    end

    runStep(puzzle3x3DoorSteps, s)
end

local newspaperReadSteps = {}

newspaperReadSteps[0] = function()
    sh.player_freeze(true)
    sh.map.set("voice_msg_audio_idx", 0)
    sh.event_bg_texture_cmd(0, "TIM/NEWSP3.TIM", false) -- start loading, NEWSP3.TIM
    sh.sfx_flags_play("newspaper_rustle", 412876, -4096, -81920, 128, 0) -- sound newspaper_rustle, 100.8, -1.0, -20.0, 0.5
    sh.event_step_increment(0)
    return newspaperReadSteps[1]()
end

newspaperReadSteps[1] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

newspaperReadSteps[2] = function()
    sh.event_bg_texture_cmd(1, 0, false)
end

newspaperReadSteps[3] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
    sh.event_bg_texture_cmd(2, 0, false)
end

newspaperReadSteps[4] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.msg.show(false, "map7_s01.same_thing", 0, 0, 0, false)
end

newspaperReadSteps[5] = function()
    sh.screen.set_bg_gamma(48)
    sh.event_bg_texture_cmd(2, 0, false)
    sh.msg.show(false, "map7_s01.investigation_stalled_ptv", 0, 0, 0, false)
end

newspaperReadSteps[6] = function()
    sh.sd_call("newspaper_rustle")
    sh.event_step_increment(0)
    return newspaperReadSteps[7]()
end

newspaperReadSteps[7] = function()
    sh.screen.set_bg_gamma(48)
    sh.event_bg_texture_cmd(2, 0, false)
    sh.msg.show(false, "map7_s01.are_there_others_its_pretty", 0, 0, 0, false)
end

newspaperReadSteps[8] = function()
    sh.screen.set_bg_gamma(48)
    sh.event_bg_texture_cmd(2, 0, false)
    sh.msg.show(false, "map7_s01.the_date_of_the_blaze", 0, 0, 0, false)
end

newspaperReadSteps[9] = function()
    sh.screen.set_bg_gamma(48)
    sh.event_bg_texture_cmd(2, 0, false)
    sh.event_fade_cmd(2, true, 0, 0, false)
end

newspaperReadSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.event_fade_cmd(0, false, 0, 0, false)
end

-- index 41: the NEWSP3.TIM newspaper: pages with messages 30/31/32/35, picture
-- brightness 6/32.
function MapEvent_NewspaperRead()
    local s = sh.event_step_get(0)
    runStep(newspaperReadSteps, s)
end

-- index 42: book by the plant (PLANTBK.TIM): the page sound once, then two messages
-- over the dimmed picture.
function MapEvent_PlantBookRead()
    if sh.event_step_get(1) == 0 then
        sh.sfx_flags_play("plant_book_page", 400588, -4096, -78643, 128, 0) -- (97.8, -1.0, -19.2) m, 0.5
    end
    sh.event_msg_with_texture("TIM/PLANTBK.TIM", 0, 0, "map7_s01.theres_a_place_mark_in", "map7_s01.white_claudia_perennial_herb")
end

-- index 43: item pickup.
function MapEvent_BirdCageKeyTake()
    sh.event_item_take("BirdCageKey", 1, "M7S01_PickupBirdCageKey", "map7_s01.there_is_a_bird_cage")
end

-- index 44: the hospital elevator button panel, this map's variant (ELEVAT_3.TIM, five buttons,
-- the flags one higher than the hospital's). Cursor and elevator state live in the map
-- state (elevator_*); the mouse cursor writes them directly. The six shaft doors are
-- world objects.
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
    sh.sfx_falloff_pitch_play("elevator_hum_loop", x, y, z, vol, 65536, 0) -- 16.0
end

local function ElevFlagsSfx(sfxId, sym)
    local x, y, z = ElevPos(sym)
    sh.sfx_flags_play(sfxId, x, y, z, 128, 0) -- volume 0.5, no flags
end

local function ElevDoorsMove(first, last, speed)
    local dt = sh.delta_time_get()
    for i = first, last do
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

-- The hospital elevator flags as in map3: ElevatorFlags[1 + f] Harry boarded at floor f
-- (basement, 1F, 2F, 3F), [5 + f] the pressed button of floor f, [9 + f] arrived at floor f.
local ElevatorFlags = {"HospitalElevatorBoardedAtBasement", "HospitalElevatorBoardedAt1F", "HospitalElevatorBoardedAt2F", "HospitalElevatorBoardedAt3F", "HospitalElevatorGoingToBasement", "HospitalElevatorGoingTo1F", "HospitalElevatorGoingTo2F", "HospitalElevatorGoingTo3F", "HospitalElevatorArrivedAtBasement", "HospitalElevatorArrivedAt1F", "HospitalElevatorArrivedAt2F", "HospitalElevatorArrivedAt3F"}

function ElevatorPanel()
    local step0 = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step0 >= 9 and step0 < 17 then
        sh.event.step_set(0, 17)
    end

    local s = sh.event_step_get(0)
    if s == 0 then
        sh.player_freeze(true)
        sh.event_fade_cmd(0, true, 0, 0, false) -- Start
        ElevSet(ELEV_Y, 0)
        ElevSet(ELEV_X, 0)
        for i = 0, 7 do
            sh.flag_set(ElevatorFlags[5 + i], false)
        end
        for i = 0, 3 do
            if sh.flag_get(ElevatorFlags[2 + i]) then -- the lit floor button
                ElevSet(ELEV_Y, (ElevButton(i + 1, 1) - 110) * 4096)
                ElevSet(ELEV_IDX, i)
            end
        end
        sh.event_step_increment(0)
        s = 1
    end
    if s == 1 then
        sh.event_bg_texture_cmd(7, "TIM/ELEVAT_3.TIM", false) -- Auto
        return
    end
    if s == 2 then
        sh.event_fade_cmd(1, true, 0, 0, false) -- Wait
        return
    end
    if s == 3 then
        sh.event_bg_texture_cmd(2, 0, false) -- Draw
        sh.event_fade_cmd(2, false, 0, 0, false) -- Auto
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
            sh.event.step_set(0, 6)
            return
        end
        if not sh.input_action("enter", 1) then
            return
        end

        for i = 0, 5 - 1 do
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
                if i == 4 then -- the "check" button does nothing on this panel
                elseif i <= 0 or not sh.flag_get(ElevatorFlags[1 + i]) then
                    if i ~= 0 then
                        sh.flag_set(ElevatorFlags[5 + i], true) -- the pressed floor button
                        ElevSet(ELEV_X, 1)
                        ElevSet(ELEV_IDX, ElevS8(math.abs((ElevGet(ELEV_IDX) + 1) - i)))
                        sh.event.step_set(0, 5)
                    else
                        for j = 0, 3 do
                            if sh.flag_get(ElevatorFlags[1 + j]) then
                                sh.flag_set(ElevatorFlags[5 + j], true)
                                break
                            end
                        end
                        ElevSet(ELEV_X, 0)
                        sh.event.step_set(0, 5)
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
        sh.player_rotate(-1024) -- -90 deg
        local _, py, _ = sh.player_pos()
        sh.player_set_pos(-417382, py, -246988) -- (-101.9, -60.3) m
        sh.event.step_set(0, 8)
        return
    end
    if s == 6 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_fade_cmd(2, true, 0, 0, false)
        return
    end
    if s == 7 then
        sh.player_freeze(false, false)
        sh.state_set(0)
        sh.event_fade_cmd(0, false, 0, 0, false)
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
            ElevHumSfx(f >> 5)
            sh.event_fade_cmd(2, true, 0, 4096, false) -- 1.0
        else
            sh.event_fade_cmd(2, true, 0, 8192, false) -- 2.0
        end
        return
    end
    if s == 10 then
        if ElevGet(ELEV_X) ~= 0 then
            sh.event_fade_cmd(2, false, 0, 4096, false)
            ElevHumSfx(128)
        else
            sh.event_fade_cmd(2, false, 0, 8192, false)
            if sh.event_step_get(0) ~= 10 then
                ElevFlagsSfx("load_screen_open", "elevator_door_sfx_pos")
                sh.event.step_set(0, 14)
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
        ElevHumSfx(128 - (sh.state_timer_get() >> 5))
        sh.state_timer_set(sh.state_timer_get() + sh.delta_time_get())
        if sh.state_timer_get() > 2048 then -- 0.5
            ElevFlagsSfx("elevator_door", "elevator_door_sfx_pos")
            sh.event_step_increment(0)
        end
        return
    end
    if s == 13 then
        ElevHumSfx(64 - (sh.state_timer_get() >> 5)) -- 0.25
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
        sh.event_wait_timer(1228, false) -- 0.3 s
        return
    end
    if s == 15 or s == 16 then
        local vx = ElevDoorSpeed(0)
        local vy = ElevDoorSpeed(1)
        ElevDoorsMove(0, 3, vx)
        ElevDoorsMove(4, 5, vy)
        if s == 15 then
            sh.event_wait_timer(1228, false)
        else
            sh.event_fade_cmd(2, true, 0, 6144, false) -- 1.5
        end
        return
    end
    if s == 17 then
        sh.event_fade_cmd(2, true, 0, 0, false)
        return
    end
    -- finish
    sh.player_freeze(false, false)
    sh.state_set(0)

    sh.sfx_stop("elevator_hum_loop")
    sh.sfx_stop("load_screen_open")
    sh.sfx_stop("elevator_door")

    for i = 5, 0, -1 do
        sh.map.set("sliding_doors.pos.z", -249446, i) -- -60.9 m
    end
end

-- index 45: the sliding door leaves part, Harry walks off along a waypoint, time
-- runs at half speed.
-- sliding_doors — the six door leaves' world object placements (map state,
-- "sliding_doors.pos.z" element i). sliding_door_speeds — two speeds.
local function dr_move_z(i, speed)
    local v = (i & 1) ~= 0 and speed or -speed
    sh.map.set("sliding_doors.pos.z", sh.map.get("sliding_doors.pos.z", i) +
               (sh.delta_time_get() * v) // 4096, i)
end

local function door_speed_step(j)
    -- accelerate by 0.25 m/s per second up to 0.1 m/s
    sh.map.set("sliding_door_speeds", math.min(sh.map.get("sliding_door_speeds", j) + (sh.delta_time_get() * 1024) // 4096, 409), j)
end

local slidingDoorsOpenSteps = {}

slidingDoorsOpenSteps[0] = function()
    sh.player_freeze(true)
    sh.screen_fade_reset_timestep()
    sh.cutscene.border_show()
    sh.event_step_increment(0)
end

slidingDoorsOpenSteps[1] = function()
    sh.event_fade_cmd(0, false, 0, 2457, false) -- 0.6
    sh.camera.position(nil, -79749, -8929, -94863, 0, 0, 0, 0, true) -- (-19.47, -2.18, -23.16)
    sh.camera.look_at(nil, -91422, -3891, -84541, 0, 0, 0, 0, true) -- (-22.32, -0.95, -20.64)
    sh.event.path_point_set(true, 0, 0, -1024, -106496, -85606) -- -90 deg, -26.0, -20.9
    sh.event_step_increment(0)
    return slidingDoorsOpenSteps[2]()
end

slidingDoorsOpenSteps[2] = function()
    door_speed_step(0)
    local d0 = sh.map.get("sliding_door_speeds", 0)
    for i = 0, 3 do
        dr_move_z(i, d0)
    end
    sh.event_wait_timer(3276, false) -- 0.8 s
end

slidingDoorsOpenSteps[5] = function()
    sh.event_fade_cmd(2, true, 0, 6144, false) -- 1.5
    return slidingDoorsOpenSteps[4]()
end

slidingDoorsOpenSteps[4] = function()
    sh.event.path_walk_player(53, 1, false)
    return slidingDoorsOpenSteps[3]()
end

slidingDoorsOpenSteps[3] = function()
    door_speed_step(0)
    door_speed_step(1)
    for i = 0, 5 do
        dr_move_z(i, sh.map.get("sliding_door_speeds", (i >= 4 and 1 or 0)))
    end
    if sh.event_step_get(0) == 3 then
        sh.event_wait_timer(4 * 4096, false)
    end
    if sh.event_step_get(0) == 4 then
        sh.event_wait_timer(2 * 4096, false)
    end
    sh.delta_time_set(sh.delta_time_get() // 2) -- time runs at half speed
end

slidingDoorsOpenSteps[6] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
    sh.delta_time_set(sh.delta_time_get() // 2)
end

slidingDoorsOpenSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.flag_set("M7S01_SlidingDoorsOpened", true)
    for i = 0, 5 do
        sh.map.set("sliding_doors.pos.x", -413286, i) -- -100.9 m
        sh.map.set("sliding_doors.pos.y", 0, i)
        sh.map.set("sliding_doors.pos.z", -249446, i) -- -60.9 m
    end
end

function Cutscene_SlidingDoorsOpen()
    local s = sh.event_step_get(0)
    if sh.input_action("skip", 1) and s > 0 and s < 6 then
        sh.event.step_set(0, 6)
        s = 6
    end

    runStep(slidingDoorsOpenSteps, s)
end

-- index 46: message over a background picture.
function MapEvent_EmptyCageInspect()
    sh.event_msg_with_bg("TIM/CAGEEMPT.TIM", 0, 0, "map7_s01.bird_cage_its_empty")
end
