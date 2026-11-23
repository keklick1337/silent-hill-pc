-- map5_s02: Annie's Bar and Indian Runner (Resort Area)
-- Event scripts of this area: the engine runs a handler every tick while its event is active.
-- Fixed-point numbers: 4096 = 1.0 (metres, seconds); angles: 4096 = 360 degrees.

-- Runs one step of a handler's step table: steps[step], or steps.default for any other step.
local function runStep(steps, step, ...)
    return (steps[step] or steps.default)(...)
end

function on_boot()
    -- per-map script entry point
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
        [15] = {"HealthDrink","M5S02_HealthDrink0"},
        [16] = {"HealthDrink","M5S02_HealthDrink1"},
        [17] = {"RifleShells","M5S02_RifleShells"},
        [18] = {"HealthDrink","M5S02_HealthDrink2"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point gives a first aid kit and sets no flag
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

local pushBoxSteps = {}

pushBoxSteps[0] = function()
    sh.player_freeze(true)
    sh.event_step_increment(0)
    return pushBoxSteps[1]()
end

pushBoxSteps[1] = function()
    sh.event.wait_player_stop()
end

pushBoxSteps[2] = function()
    sh.msg.show(true, "map5_s02.there_is_a_drawer_do", 3, -1, 0, false) -- none; "There is a drawer. Do you want to open it?"
end

pushBoxSteps[3] = function()
    sh.event_chara_anim(0, 0, "push_box", false)
    sh.cutscene.border_show()
    -- Warp camera.
    sh.camera.position(nil, 553656, -13516, 250224, 0, 0, 0, 0, true) -- 135.17, -3.3, 61.09 m
    sh.camera.look_at(nil, 550051, -860, 258785, 0, 0, 0, 0, true)     -- 134.29, -0.21, 63.18 m
    -- Warp player.
    sh.npc.set_pos_x(0, 552550) -- 134.9 m
    sh.npc.set_pos_z(0, 255795) -- 62.45 m
    sh.npc.set_rot_y(0, -1024)  -- -90.0 deg
    sh.event_step_increment(0)
    return pushBoxSteps[4]()
end

pushBoxSteps[4] = function()
    sh.event_wait_timer(6144, false) -- 1.5 s
end

pushBoxSteps[5] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto
end

pushBoxSteps[6] = function()
    sh.sfx_flags_play("box_scrape", 551321, -6144, 255795, 128, 0) -- sound box_scrape at 134.6, -1.5, 62.45, 0.5
    sh.event_step_increment(0)
    return pushBoxSteps[7]()
end

pushBoxSteps[7] = function()
    sh.event_wait_timer(4096, false) -- 1.0 s
end

pushBoxSteps[8] = function()
    sh.camera_auto_return(true)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.flag_set("M5S02_BoxPushed", true)
    sh.npc.set_pos_x(0, 553574) -- 135.15 m
    sh.event_step_increment(0)
    return pushBoxSteps.default()
end

pushBoxSteps.default = function()
    -- Return to gameplay.
    sh.player_freeze(false, false)
    sh.state_set(0)
end

-- index 3: desk drawer (push box), steps 0-8 + default.
function PushBox()
    local s = sh.event_step_get(0)
    runStep(pushBoxSteps, s)
end

-- index 4: item pickup.
function MapEvent_SafeKeyTake()
    sh.event_item_take("SafeKey", 1, "M5S02_PickupSafeKey", "map5_s02.there_is_a_safe_key")
end

-- index 5: the diary over a background picture; the flag is set before the message
-- is shown.
function MapEvent_DiaryInspect()
    sh.flag_set("M5S02_DiaryRead", true)
    sh.event_msg_with_bg("TIM/DIARYINR.TIM", 0, 0, "map5_s02.august_20")
end

-- index 6: opening the safe with the key, steps 0-14 + default.
-- "safe_door" — the safe door's world object pose (map state; rotation.vy = "safe_door.rot.y").
local function SafeDoorRotY(angle)
    sh.map.set("safe_door.rot.y", angle)
end

local function SafeCamera()
    sh.camera.position(nil, 554024, -6594, 246661, 0, 0, 0, 0, true) -- 135.26, -1.61, 60.22 m
    sh.camera.look_at(nil, 546242, 4587, 237608, 0, 0, 0, 0, true)    -- 133.36, 1.12, 58.01 m
end

local useSafeKeySteps = {}

useSafeKeySteps[0] = function()
    sh.player_freeze(true)
    sh.flag_set("M5S02_SafeOpened", true)
    -- Warp player.
    sh.npc.set_pos_x(0, 553164) -- 135.05 m
    sh.npc.set_pos_z(0, 244736) -- 59.75 m
    sh.npc.set_rot_y(0, 2048)   -- 180.0 deg
    sh.event_fade_cmd(0, true, 2, 0, false)
    sh.event_step_increment(0)
    return useSafeKeySteps[1]()
end

useSafeKeySteps[1] = function()
    sh.event.wait_player_stop()
end

useSafeKeySteps[2] = function()
    sh.chara_anim_play_to_end(0, "pickup_crouch")
end

useSafeKeySteps[3] = function()
    sh.sfx_flags_play("safe_open", 552550, -2048, 243712, 128, 0) -- sound safe_open at 134.9, -0.5, 59.5
    sh.event_step_increment(0)
    return useSafeKeySteps[4]()
end

useSafeKeySteps[4] = function()
    sh.msg.show(false, "map5_s02.used_the_safe_key", 0, 0, 0, false) -- "Used the Safe Key."
end

useSafeKeySteps[5] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

useSafeKeySteps[6] = function()
    sh.flag_set("M5S02_UsedSafeKey", true) -- flag: used safe key
    sh.sfx_flags_play("safe_unlock", 551731, -2048, 243712, 128, 0) -- sound safe_unlock at 134.7, -0.5, 59.5
    -- Warp camera.
    SafeCamera()
    SafeDoorRotY(-1618) -- -142.21 deg
    sh.event_step_increment(0)
    return useSafeKeySteps[7]()
end

useSafeKeySteps[7] = function()
    sh.event_wait_timer(2048, false) -- 0.5 s
end

useSafeKeySteps[8] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
end

useSafeKeySteps[9] = function()
    sh.msg.show(false, "map5_s02.what_is_this_drugs", 0, 0, 0, false) -- "What is this? Drugs?"
end

useSafeKeySteps[10] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

useSafeKeySteps[11] = function()
    sh.camera_auto_return(true)
    SafeDoorRotY(-85) -- -7.5 deg
    sh.event_step_increment(0)
    return useSafeKeySteps[12]()
end

useSafeKeySteps[12] = function()
    sh.event_wait_timer(2048, false)
end

useSafeKeySteps[13] = function()
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_step_increment(0)
    return useSafeKeySteps[14]()
end

useSafeKeySteps[14] = function()
    sh.chara_anim_play_to_end(0, "pickup_stand_up")
end

useSafeKeySteps.default = function()
    -- Return to gameplay.
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.camera_auto_return(true)
end

function UseSafeKey()
    local s = sh.event_step_get(0)
    runStep(useSafeKeySteps, s)
end

local safeReopenSteps = {}

safeReopenSteps[0] = function()
    sh.player_freeze(true)
    sh.event_fade_cmd(0, true, 2, 0, false)
    sh.event_step_increment(0)
    return safeReopenSteps[1]()
end

safeReopenSteps[1] = function()
    sh.event.wait_player_stop()
end

safeReopenSteps[2] = function()
    sh.chara_anim_play_to_end(0, "pickup_crouch")
end

safeReopenSteps[3] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

safeReopenSteps[4] = function()
    sh.flag_set("M5S02_UsedSafeKey", true) -- flag: used safe key
    sh.sfx_flags_play("safe_unlock", 551731, -2048, 243712, 128, 0) -- sound safe_unlock at 134.7, -0.5, 59.5
    SafeCamera()
    SafeDoorRotY(-1618) -- -142.21 deg
    sh.npc.set_pos_x(0, 553164) -- 135.05 m
    sh.npc.set_pos_z(0, 244736) -- 59.75 m
    sh.npc.set_rot_y(0, 2048)   -- 180.0 deg
    sh.event_step_increment(0)
    return safeReopenSteps[5]()
end

safeReopenSteps[5] = function()
    sh.event_wait_timer(2048, false) -- 0.5 s
end

safeReopenSteps[6] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
end

safeReopenSteps[7] = function()
    sh.msg.show(false, "map5_s02.what_is_this_drugs", 0, 0, 0, false) -- "What is this? Drugs?"
end

safeReopenSteps[8] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

safeReopenSteps[9] = function()
    sh.camera_auto_return(true)
    SafeDoorRotY(-85) -- -7.5 deg
    sh.event_step_increment(0)
    return safeReopenSteps[10]()
end

safeReopenSteps[10] = function()
    sh.event_wait_timer(2048, false)
end

safeReopenSteps[11] = function()
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_step_increment(0)
    return safeReopenSteps[12]()
end

safeReopenSteps[12] = function()
    sh.chara_anim_play_to_end(0, "pickup_stand_up")
end

safeReopenSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.camera_auto_return(true)
end

-- index 7: reopening the safe, steps 0-12 + default.
function SafeReopen()
    local s = sh.event_step_get(0)
    runStep(safeReopenSteps, s)
end

-- index 8: message over a background picture.
function MapEvent_PictureInspect()
    sh.event_msg_with_bg("TIM/PICT_INR.TIM", 0, 0, "map5_s02.normans_grand_opening")
end

-- index 9: message over a background picture.
function MapEvent_MemoInspect()
    sh.event_msg_with_bg("TIM/MEMO_INR.TIM", 0, 0, "map5_s02.3_loaves_of_bread_3")
end

-- index 10: Kaufmann and the Mumbler in the bar (POOL1.DMS cutscene).
-- The voice line index and the Mumbler-spawned state are used only by this handler, so
-- they are kept local. bar_msg_voice_cmds (the voice commands) is map state.
local s_barMsgAudioIdx = 0
local s_barMumblerState = 0

local Pool1Buf = 17 -- file buffer 17

local function barTimerAdvance(rate, startT, endT, clamp, auto)
    sh.cutscene.timer_advance("cutscene_timer", rate, startT, endT, clamp, auto)
end

-- Bar message with voice; the line index is kept here. The voice command is read only when
-- a page starts: after the last page the index is one past the table and must not be read.
local function barMsg(msg)
    s_barMsgAudioIdx = sh.event_msg_audio(msg, s_barMsgAudioIdx, "bar_msg_voice_cmds")
end

local function barPointFromAngle()
    sh.vec.set(0, 570572, 0, 96665) -- (139.3, 0.0, 23.6) m
    sh.math.point_from_angle("vec:0", 120, 3, 1)
end

local kaufmannBarFightSteps = {}

kaufmannBarFightSteps[0] = function()
    sh.player_freeze(true)
    s_barMsgAudioIdx = 0
    sh.screen_fade_reset_timestep()
    sh.cutscene.border_show()
    sh.cutscene_flag(true)
    sh.water_effect_enable()
    sh.light.bone_detach()
    sh.light.lens_flare_detach()
    sh.light.set_intensity(4096) -- 1.0
    sh.load.file("ANIM/POOL1.DMS", 17) -- POOL1.DMS
    sh.load.wait()
    sh.cutscene.prepare(17)
    sh.map.set("cutscene_timer", 0)
    sh.npc.set_pos_y(0, 0)
    sh.npc.load(0, "Kaufmann", "npc_bone:0", 255, nil, nil) -- Kaufmann, free all character slots
    sh.npc.load(1, "Mumbler", "npc_bone:30", 0, nil, nil) -- Mumbler
    sh.player.weapon_attack_set(32)
    sh.held_item_hide()
    sh.player.weapon_stash()
    sh.player.held_item_set("Handgun")
    sh.npc.set_collision_state(0, 5) -- collision state 5
    sh.event_chara_anim(0, 0, "scene_sqsu_2", false)
    s_barMumblerState = 0
    sh.event_step_increment(0)
end

kaufmannBarFightSteps[1] = function()
    sh.event_chara_anim(2, 0, 0, false) -- AnimLock
    sh.event_fade_cmd(2, false, 0, 0, false) -- Auto
end

kaufmannBarFightSteps[2] = function()
    sh.event_chara_anim(3, 0, 0, false) -- AnimUnlock
    sh.event_step_increment(0)
    return kaufmannBarFightSteps[3]()
end

kaufmannBarFightSteps[3] = function()
    barTimerAdvance(32768, 0, 81920, true, true) -- 8.0, 0, 20.0
end

kaufmannBarFightSteps[4] = function()
    sh.event_chara_anim(0, 0, "scene_bar_fight_1", false)
    sh.flag_set("M5S02_BarFightInProgress", true)
    sh.event_step_increment(0)
    return kaufmannBarFightSteps[5]()
end

kaufmannBarFightSteps[5] = function()
    barTimerAdvance(32768, 86016, 126976, true, true) -- 21.0, 31.0 s
end

kaufmannBarFightSteps[6] = function()
    sh.npc.process_loads()
    sh.npc.spawn("Kaufmann", 0, 575488, 94208, 0, 3) -- Kaufmann, 140.5, 23.0
    sh.event_step_increment(0)
end

kaufmannBarFightSteps[7] = function()
    sh.npc.spawn("Mumbler", 0, 575488, 94208, 0, 17) -- Mumbler
    s_barMumblerState = 1
    sh.event_chara_anim(0, 1, "wrestle_monster", false)
    sh.event_step_increment(0)
    return kaufmannBarFightSteps[8]()
end

kaufmannBarFightSteps[8] = function()
    barTimerAdvance(40960, 131072, 176128, true, true) -- 10.0, 32.0, 43.0 s
end

kaufmannBarFightSteps[9] = function()
    sh.event_chara_anim(0, 0, "scene_bar_fight_6", false)
    sh.event_step_increment(0)
    return kaufmannBarFightSteps[10]()
end

kaufmannBarFightSteps[10] = function()
    barTimerAdvance(40960, 180224, 229376, true, true) -- 44.0, 56.0 s
end

kaufmannBarFightSteps[11] = function()
    sh.held_item_show()
    sh.event_step_increment(0)
    return kaufmannBarFightSteps[12]()
end

kaufmannBarFightSteps[12] = function()
    sh.event_chara_anim(0, 0, "scene_bar_fight_3", false)
    sh.event_chara_anim(0, 1, "push_free", false)
    sh.npc.set_anim_status(2, sh.npc.anim_status(2) - 1) -- step back the Mumbler's animation status
    sh.event_step_increment(0)
    return kaufmannBarFightSteps[13]()
end

kaufmannBarFightSteps[13] = function()
    barTimerAdvance(40960, 258048, 380928, true, true) -- 63.0, 93.0 s
end

kaufmannBarFightSteps[14] = function()
    sh.chara_anim_play_to_end(0, "scene_bar_fight_4")
    barTimerAdvance(40960, 385024, 614400, false, false) -- 94.0, 150.0 s
end

kaufmannBarFightSteps[15] = function()
    sh.event_chara_anim(0, 0, "scene_bar_fight_5", false)
    sh.event_step_increment(0)
    return kaufmannBarFightSteps[16]()
end

kaufmannBarFightSteps[16] = function()
    barTimerAdvance(40960, 385024, 614400, true, true)
end

kaufmannBarFightSteps[17] = function()
    sh.event_chara_anim(0, 1, "draw_gun", false)
    sh.event_step_increment(0)
    return kaufmannBarFightSteps[18]()
end

kaufmannBarFightSteps[18] = function()
    barTimerAdvance(40960, 618496, 716800, true, true) -- 151.0, 175.0 s
end

kaufmannBarFightSteps[19] = function()
    sh.event_chara_anim(0, 1, "shoot", false)
    sh.held_item_hide()
    sh.flag_set("M5S02_BarFightInProgress", false)
    sh.npc.set_state_step(2, sh.npc.state_step(2) + 1) -- advance the Mumbler's state step
    sh.event_step_increment(0)
    return kaufmannBarFightSteps[20]()
end

kaufmannBarFightSteps[20] = function()
    barTimerAdvance(40960, 720896, 843776, false, true) -- 176.0, 206.0 s
end

kaufmannBarFightSteps[21] = function()
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_chara_anim(0, 1, "stand_talk", false)
    barPointFromAngle()
    s_barMumblerState = 2
    sh.map.set("cutscene_timer", 847872) -- frame 207.0
    sh.event_step_increment(0)
    return kaufmannBarFightSteps[22]()
end

kaufmannBarFightSteps[22] = function()
    barMsg("map5_s02.are_you_ok")
end

kaufmannBarFightSteps[23] = function()
    sh.event_chara_anim(0, 1, "leave", false)
    sh.event_step_increment(0)
    return kaufmannBarFightSteps[24]()
end

kaufmannBarFightSteps[24] = function()
    barMsg("map5_s02.yeah_i_guess_so")
    barTimerAdvance(40960, 847872, 929792, true, false) -- 207.0, 227.0 s
end

kaufmannBarFightSteps[25] = function()
    sh.event_chara_anim(0, 1, "stand_talk", false)
    sh.map.set("cutscene_timer", 933888) -- frame 228.0
    sh.event_step_increment(0)
    return kaufmannBarFightSteps[26]()
end

kaufmannBarFightSteps[26] = function()
    barMsg("map5_s02.so_howd_it_go_did")
end

kaufmannBarFightSteps[27] = function()
    sh.event_chara_anim(0, 1, "talk_gesture", false)
    sh.npc.anim_flags_clear(0, 2)
    sh.event_step_increment(0)
    return kaufmannBarFightSteps[28]()
end

kaufmannBarFightSteps[28] = function()
    sh.event_wait_timer(7372, false) -- 1.8 s
    barTimerAdvance(40960, 937984, 1204224, true, false) -- 229.0, 294.0 s
end

kaufmannBarFightSteps[29] = function()
    barMsg("map5_s02.do_you_know_a_girl")
    barTimerAdvance(40960, 937984, 1204224, true, false)
end

kaufmannBarFightSteps[30] = function()
    sh.event_step_increment(0)
    return kaufmannBarFightSteps[31]()
end

kaufmannBarFightSteps[31] = function()
    sh.event_wait_timer(1638, false) -- 0.4 s
end

kaufmannBarFightSteps[32] = function()
    sh.event_chara_anim(0, 1, "walk", false)
    sh.event_chara_anim(0, 0, "scene_bar_fight_2", false)
    sh.npc.anim_flags_set(0, 2)
    sh.event_step_increment(0)
    return kaufmannBarFightSteps[33]()
end

kaufmannBarFightSteps[33] = function()
    barMsg("map5_s02.no")
    return kaufmannBarFightSteps[34]()
end

kaufmannBarFightSteps[34] = function()
    barTimerAdvance(32768, 1208320, 1290240, true, true) -- 8.0, 295.0, 315.0 s
end

kaufmannBarFightSteps[35] = function()
    sh.npc.clear(1, 0, 0)
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.map.set("cutscene_timer", 1294336) -- frame 316.0
    sh.player.weapon_restore()
    sh.player.held_item_prev()
    sh.player.weapon_stash()
    sh.event_step_increment(0)
    return kaufmannBarFightSteps[36]()
end

kaufmannBarFightSteps[36] = function()
    sh.event_wait_timer(2048, false) -- 0.5 s
end

kaufmannBarFightSteps[37] = function()
    sh.vec.set(1, 550912, -4915, 73318) -- door position (134.5, -1.2, 17.9) m
    sh.sound.play_with_flags("door_open_0", "vec:1", 128, 0) -- door open 0 sound, 0.5, no flags
    sh.event_step_increment(0)
    return kaufmannBarFightSteps[38]()
end

kaufmannBarFightSteps[38] = function()
    sh.event_wait_timer(4096, false) -- 1.0 s
end

kaufmannBarFightSteps[39] = function()
    sh.vec.set(1, 550912, -4915, 73318)
    sh.sound.play_with_flags("door_close_0", "vec:1", 128, 0) -- door close 0 sound
    sh.event_step_increment(0)
    return kaufmannBarFightSteps[40]()
end

kaufmannBarFightSteps[40] = function()
    sh.event_wait_timer(2457, false) -- 0.6 s
end

kaufmannBarFightSteps[41] = function()
    if sh.load.chunks() ~= 0 then
        sh.event_step_increment(0)
    end
end

kaufmannBarFightSteps[42] = function()
    sh.event_step_reset()
end

kaufmannBarFightSteps[43] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

kaufmannBarFightSteps[44] = function()
    if s_barMumblerState == 0 then
        sh.npc.process_loads()
        sh.npc.set_chara_id(1, "Kaufmann") -- Kaufmann
        sh.npc.spawn("Mumbler", 0, 575488, 94208, 0, 17) -- Mumbler
        s_barMumblerState = 1
    end
    sh.npc.clear(1, 0, 0)
    sh.player.weapon_restore()
    sh.player.held_item_prev()
    sh.map.set("cutscene_timer", 1294336) -- frame 316.0
    sh.event_step_increment(0)
end

kaufmannBarFightSteps[45] = function()
    if s_barMumblerState == 1 then
        sh.npc.set_state_step(2, 3)
        barPointFromAngle()
    end
    sh.load.wait()
    sh.event_step_increment(0)
end

kaufmannBarFightSteps[46] = function()
    for i = 0, 199 do -- all 200 map effect slots
        if sh.map.get("effect_slot_type", i) == 1 then
            sh.map.set("effect_slot_type", 0, i)
        end
    end
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_step_increment(0)
    return kaufmannBarFightSteps.default()
end

kaufmannBarFightSteps.default = function()
    sh.player_freeze(false, true)
    sh.state_set(0)
    sh.npc.set_pos_y(0, 0)
    sh.held_item_show()
    sh.player.weapon_restore()
    sh.player.weapon_attack_set(sh.player.weapon_attack() & 0xFF)
    sh.npc.anim_flags_set(0, 2)
    sh.camera.auto_return(true)
    sh.map.set("cutscene_timer", -1) -- none
    sh.flag_set("M5S02_KaufmannBarFightCutsceneDone", true)
    sh.water_effect_disable()
    sh.light.flashlight_fix()
    sh.light.set_intensity(4096)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.npc.set_collision_state(0, 3) -- collision state Npc
    sh.sd_call("stop_xa_audio")
end

function Cutscene_KaufmannBarFight()
    local s = sh.event_step_get(0)
    if sh.input_action("skip", 1) and s >= 3 and s < 41 then
        sh.event.step_set(0, 43)
    end

    s = sh.event_step_get(0)
    runStep(kaufmannBarFightSteps, s)

    if sh.event_step_get(0) < 27 then
        -- Kaufmann's bag: the world object pose (object, position, rotation).
        sh.world.object_add("kaufmann_bag", "kaufmann_bag.pos", "kaufmann_bag.rot")
    end

    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, Pool1Buf)
        sh.cutscene.chara(1, "KAU", t, Pool1Buf)
        sh.cutscene.chara(2, "CLD4", t, Pool1Buf)
        sh.cutscene.camera(t, Pool1Buf, "camera_target_pos", "camera_target_look")
        sh.cutscene.light(t, Pool1Buf)
    end
end

-- index 11: Kaufmann's key and the receipt in the safe: both items are inspected with the
-- pickup animation and added to the inventory.
function KaufmannReceipt()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1 -- continues into step 1 in the same tick
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.chara_anim_play_to_end(0, "pickup_crouch")
    elseif step == 3 then
        sh.event_inv_item_cmd(0, "KaufmannKey", 0, false) -- QueueLoad
        sh.event_step_increment(0)
        step = 4 -- continues into step 4 in the same tick
    end
    if step == 4 then
        sh.event_msg(false, "map5_s02.kaufmann_must_have_dropped", 0, 0, 0, false)         -- "There's something inside."
    elseif step == 5 then
        sh.event_inv_item_cmd(1, "KaufmannKey", 0, false) -- AwaitLoad
    elseif step == 6 then
        sh.flag_set("M5S02_PickupKaufmannKeyAndReceipt", true)
        if sh.pickup_animate("KaufmannKey") then
            sh.event_msg(false, "map5_s02.ive_got_a_kaufmann_key", 0, 0, 0, false)
            if sh.event_step_get(0) ~= 6 then
                sh.sd_call("menu_confirm")                          -- menu confirm sound
            end
        end
    elseif step == 7 then
        sh.event_inv_item_cmd(0, "Receipt", 0, false) -- QueueLoad
        sh.event_step_increment(0)
        step = 8 -- continues into step 8 in the same tick
    end
    if step == 8 then
        sh.event_inv_item_cmd(1, "Receipt", 0, false) -- AwaitLoad
    elseif step == 9 then
        if sh.pickup_animate("Receipt") then
            sh.event_msg(false, "map5_s02.ive_got_a_receipt", 0, 0, 0, false)
            if sh.event_step_get(0) ~= 9 then
                sh.sd_call("menu_confirm")                          -- menu confirm sound
            end
        end
    elseif step == 10 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")
    elseif step < 0 or step > 10 then -- any other step
        sh.player_freeze(false)                           -- unfreeze the player
        sh.state_set(0)                                   -- back to gameplay
        sh.flag_set("M5S02_PickupKaufmannKeyAndReceipt", true)
        sh.event_inv_item_cmd(3, "KaufmannKey", 1, false) -- AddItem
        sh.event_inv_item_cmd(3, "Receipt", 1, false)     -- AddItem
    end
end

MapEvent_KaufmannBarFightCutscene = Cutscene_KaufmannBarFight
