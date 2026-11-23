-- map5_s00: Sewers - lower and upper levels
-- Event scripts of this area: the engine runs a handler every tick while its event is active.
-- Fixed-point numbers: 4096 = 1.0 (metres, seconds); angles: 4096 = 360 degrees.

-- Runs one step of a handler's step table: steps[step], or steps.default for any other step.
local function runStep(steps, step, ...)
    return (steps[step] or steps.default)(...)
end

function on_boot()
    -- per-map script entry point
end

-- index 0: message with a sound played at the event's point of interest.
function MapEvent_DoorJammed()
    sh.event_msg_with_sfx("common.lock_jammed", "sewers_door_locked")
end

-- index 1: message with a sound played at the event's point of interest.
function MapEvent_DoorLocked()
    sh.event_msg_with_sfx("common.locked", "sewers_door_locked")
end

-- index 2: using a key on a lock: point 10 is the SewerKey (removed + message map5_s00.used_the_sewer_key),
-- point 14 the SewerExitKey (removed + message map5_s00.used_the_sewer_exit_key), otherwise "door unlocked". The sound
-- plays at the event's point of interest, 1.2 m up;
-- sh.event_msg_with_sfx does the same.
function MapEvent_SewerKeyUse()
    local poi = sh.event_point()
    if poi == 10 then
        sh.player.item_remove("SewerKey", 1)
        sh.event_msg_with_sfx("map5_s00.used_the_sewer_key", "use_key")
    elseif poi == 14 then
        sh.player.item_remove("SewerExitKey", 1)
        sh.event_msg_with_sfx("map5_s00.used_the_sewer_exit_key", "use_key")
    else
        sh.event_msg_with_sfx("common.unlocked", "door_unlocked")
    end
end

-- index 3: common pickups, chosen by the event's point of interest.
function MapEvent_CommonItemTake()
    local poi = sh.event_point()
    local t = {
        [38] = {"ShotgunShells","M5S00_ShotgunShells"},
        [39] = {"HealthDrink","M5S00_HealthDrink0"},
        [40] = {"HandgunBullets","M5S00_HandgunBullets0"},
        [41] = {"RifleShells","M5S00_RifleShells"},
        [42] = {"FirstAidKit","M5S00_FirstAidKit0"},
        [43] = {"FirstAidKit","M5S00_FirstAidKit1"},
        [44] = {"HealthDrink","M5S00_HealthDrink1"},
        [45] = {"HandgunBullets","M5S00_HandgunBullets1"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point gives a first aid kit and sets no flag
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- index 4: item pickup.
function MapEvent_SewerKeyTake()
    sh.event_item_take("SewerKey", 1, "M5S00_PickupSewerKey", "map5_s00.there_is_a_sewer_key")
end

-- index 5: the key in the drain (DRAINKEY.TIM) and the ambush of three Hanged Scratchers.
-- The scene timer is used only by this handler, so it is kept local. ambush_npc_slots
-- (the three spawn slots) is map state.
local s_drainAmbushTimer = 0

local function ambushNpcSlot(i)
    return sh.map.get("ambush_npc_slots", i) + 1 -- NPC slot of ambusher i
end

local function ambushNpcFlag(i)
    local slot = ambushNpcSlot(i)
    sh.npc.set_scratcher_flags_lo(slot, sh.npc.scratcher_flags_lo(slot) | 0x80) -- mark the Scratcher as released
end

local function ambushNpcRelease(i, dx, dz, rotY)
    local slot = ambushNpcSlot(i)
    sh.npc.set_control_state(slot, 0)
    sh.npc.set_state_step(slot, 17)
    sh.npc.set_pos_x(slot, sh.npc.pos_x(slot) + dx)
    sh.npc.set_pos_z(slot, sh.npc.pos_z(slot) + dz)
    sh.npc.set_rot_y(slot, rotY)
end

local drainKeyAmbushSteps = {}

drainKeyAmbushSteps[0] = function()
    sh.player_freeze(true)
    s_drainAmbushTimer = -2048 -- -0.5 s
    sh.event_step_increment(0)
    return drainKeyAmbushSteps[1]()
end

drainKeyAmbushSteps[1] = function()
    sh.event.wait_player_stop()
end

drainKeyAmbushSteps[2] = function()
    sh.npc.anim_play_to_end(0, "pickup_crouch")
end

drainKeyAmbushSteps[3] = function()
    sh.event_fade_cmd(0, true, 0, 0, false) -- Start
    sh.event_step_increment(0)
    return drainKeyAmbushSteps[4]()
end

drainKeyAmbushSteps[4] = function()
    sh.event_fade_cmd(1, true, 0, 0, false) -- Wait
end

drainKeyAmbushSteps[5] = function()
    sh.sound.set_bgm_flags(sh.sound.bgm_flags() | 1) -- pause the music
    sh.event.bg_texture(7, "TIM/DRAINKEY.TIM", false) -- Auto, DRAINKEY.TIM
end

drainKeyAmbushSteps[6] = function()
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_step_increment(0)
    return drainKeyAmbushSteps[7]()
end

drainKeyAmbushSteps[7] = function()
    sh.event_step_increment(0)
    return drainKeyAmbushSteps[8]()
end

drainKeyAmbushSteps[8] = function()
    sh.event.bg_texture(2, 0, false) -- Draw
    sh.event_fade_cmd(1, false, 0, 0, false)
end

drainKeyAmbushSteps[9] = function()
    sh.event.bg_texture(2, 0, false)
    sh.msg.set_silent_yes_selection(1)
    sh.msg.show(true, "map5_s00.there_is_a_sewer_exit", 10, 11, 0, false)
end

drainKeyAmbushSteps[10] = function()
    sh.flag_set("M5S00_DrainKeyAmbushCutsceneDone", true)
    sh.event.inv_item(3, "SewerExitKey", 1, false) -- AddItem, sewer exit key
    sh.npc.set_pos_x(0, -376832) -- -92.0 m
    sh.npc.set_pos_z(0, 31539) -- 7.7 m
    sh.npc.set_rot_y(0, 0)
    for i = 0, 2 do -- three ambushers
        -- Hanged Scratcher at (-93.3, 7.1), facing 180 deg
        sh.map.set("ambush_npc_slots", sh.npc.spawn("HangedScratcher", i, -382156, 29081, 2048, 18 + i), i)
    end
    if sh.save.game_difficulty() ~= -1 then -- not on Easy
        sh.npc.spawn("HangedScratcher", 14, -386252, -22528, 0, 7) -- -94.3, -5.5
    end
    sh.cutscene.border_show()
    sh.sound.play("ambush_scream", 0, 128) -- sound ambush_scream, 0.5
    sh.event_step_increment(0)
    return drainKeyAmbushSteps[11]()
end

drainKeyAmbushSteps[11] = function()
    sh.event.bg_texture_fade_out(0, 0)
end

drainKeyAmbushSteps[12] = function()
    sh.event.branch_on_flag("M5S00_DrainKeyAmbushCutsceneDone", 13, 16, false)
end

drainKeyAmbushSteps[13] = function()
    -- camera at (-88.04, -1.29, 6.26), looking at (-91.77, -0.89, 7.63)
    sh.camera.position(nil, -360611, -5283, 25640, 0, 0, 0, 0, true)
    sh.camera.look_at(nil, -375889, -3645, 31252, 0, 0, 0, 0, true)
    sh.light.set_intensity(8192) -- 2.0
    sh.event_step_increment(0)
    sh.vec.set(0, -373964, 1228, 34816) -- (-91.3, 0.3, 8.5) m
    sh.map.drain_splash_start("vec:0")
    return drainKeyAmbushSteps[14]()
end

drainKeyAmbushSteps[14] = function()
    local dt = sh.delta_time_get()
    if s_drainAmbushTimer < 6144 and (s_drainAmbushTimer + dt) > 6144 then -- 1.5 s
        sh.event_chara_anim(0, 0, "scene_drain_ambush", false)
    end
    s_drainAmbushTimer = s_drainAmbushTimer + (dt * 2457) // 4096 -- dt * 0.6
    if s_drainAmbushTimer > 14540 then -- 3.55 s
        sh.event_step_increment(0)
    elseif s_drainAmbushTimer > 10240 then -- 2.5 s
        ambushNpcFlag(2)
    elseif s_drainAmbushTimer > 5120 then -- 1.25 s
        ambushNpcFlag(1)
    elseif s_drainAmbushTimer > -409 then -- -0.1 s
        ambushNpcFlag(0)
    end
    sh.map.drain_splash_update()
end

drainKeyAmbushSteps[15] = function()
    sh.camera.auto_return(true)
    sh.light.set_intensity(4096)
    sh.event_fade_cmd(0, false, 2, 0, false)
    ambushNpcRelease(0, -769, 1003, 1280) -- -0.1878, 0.245, 112.5 deg
    ambushNpcRelease(1, 7425, 3272, 2048) -- 1.8128, 0.799, 180.0 deg
    ambushNpcRelease(2, 2675, -5117, 0) -- 0.6531, -1.2493, 0.0 deg
    sh.npc.set_pos_z(0, sh.npc.pos_z(0) - 4915) -- -1.2 m
    sh.player_freeze(false, true)
    sh.state_set(0)
end

drainKeyAmbushSteps[16] = function()
    sh.npc.anim_play_to_end(0, "pickup_stand_up")
end

drainKeyAmbushSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
end

function Cutscene_DrainKeyAmbush()
    local s = sh.event_step_get(0)
    runStep(drainKeyAmbushSteps, s)
end

-- index 6: climbing down the sewer ladder (player anim 88).
-- Skip on steps 1-2 -> fade_reset + step_reset; player warp (56.789, -2.02, 60.02, -90 deg),
-- flashlight, camera (58.49,1.18,59.07)/(56.67,-2.3,59.86), descent vy += dt*0.4,
-- exit at (16.7, 0, 52, 90 deg), flag M5S00_SewerEntryLadderDone.
local function ladderClimb(dvy)
    local px, py, pz = sh.player_pos()
    sh.player_set_pos(px, py + (sh.delta_time_get() * dvy) // 4096, pz)
end

function MapEvent_SewerLadderDown1()
    local step = sh.event_step_get(0)

    if sh.input_action("skip", 1) and step > 0 and step < 3 then
        sh.screen_fade_reset_timestep()
        sh.event_step_reset()
        step = 0
    end

    if step == 0 then
        sh.player_freeze(true)
        sh.player_set_pos(232607, -8273, 245841)       -- 56.789, -2.02, 60.02 m
        sh.player_rotate(-1024)                        -- -90° deg
        sh.flashlight(true)
        sh.camera.position(nil, 239575, 4833, 241950, 0, 0, 0, 0, true)   -- 58.49, 1.18, 59.07 m
        sh.camera.look_at(nil, 232120, -9420, 245186, 0, 0, 0, 0, true)   -- 56.67, -2.3, 59.86 m
        sh.event_chara_anim(0, 0, "hatch_drop_down", false)
        sh.event_step_increment_after_fade(0, false, 0, 6144, false) -- Start, 1.5
        sh.event_step_increment(0)
        step = 1
    end

    if step == 1 then
        ladderClimb(1638)                              -- dt * 0.4
        sh.event_wait_timer(15564, false)              -- 3.8 s
    elseif step == 2 then
        sh.event_step_increment_after_fade(2, true, 0, 6144, false) -- Auto, 1.5
        ladderClimb(1638)
    else
        sh.player_freeze(false, true)                  -- unfreeze the player
        sh.state_set(0)
        sh.player_set_pos(68403, 0, 212992)            -- 16.7, 0, 52 m
        sh.player_rotate(1024)                         -- 90° deg
        sh.camera_auto_return(true)
        sh.event_step_increment_after_fade(0, false, 0, 0, false)
        sh.flag_set("M5S00_SewerEntryLadderDone", true)
        sh.game_state_update()
    end
end

-- Ladders 7-10 share one shape: skip on steps 1-5; fade out → WaitPlayerStop →
-- wait for the fade (7, 8: plus the event state timer over 1 s of raw delta) → warp the player and
-- camera, anim, fade in → climb for 3.8 s → fade out while climbing → return.
-- `o` = {wait_extra, pos, cam, look, anim, dvy4, dvy5, unfreeze_idle, finish, mute, reset_only}
local function sewerLadder(o)
    local step = sh.event_step_get(0)

    if sh.input_action("skip", 1) and step > 0 and step < 6 then
        sh.screen_fade_reset_timestep()
        sh.event_step_reset()
        step = 0
    end

    if o.mute then
        sh.sound.set_bgm_flags(sh.sound.bgm_flags() | 2) -- mute the music
    end

    if step == 0 then
        sh.player_freeze(true)
        if o.reset_only then
            sh.screen_fade_reset_timestep()
        else
            sh.event_step_increment_after_fade(0, true, 0, 0, false) -- Start, fade-out
        end
        sh.event_step_increment(0)
        step = 1
    end

    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.event_step_increment_after_fade(1, true, 0, 0, false) -- Wait fade-out
        if o.wait_extra and sh.event_step_get(0) == 2 then
            sh.event.set_state_timer(sh.event.state_timer() + sh.game.delta_time_raw())
            if sh.event.state_timer() > 4096 then        -- 1.0 s
                sh.event.step_set(0, 3)
            end
        end
    elseif step == 3 then
        sh.player_set_pos(o.pos[1], o.pos[2], o.pos[3])
        sh.player_rotate(-1024)                        -- -90° deg
        sh.flashlight(true)
        sh.camera.position(nil, o.cam[1], o.cam[2], o.cam[3], 0, 0, 0, 0, true)
        sh.camera.look_at(nil, o.look[1], o.look[2], o.look[3], 0, 0, 0, 0, true)
        sh.event_chara_anim(0, 0, o.anim, false)       -- SetState
        sh.event_step_increment_after_fade(0, false, 0, 6144, false) -- Start, 1.5
        sh.event_step_increment(0)
        step = 4
    end

    if step == 4 then
        ladderClimb(o.dvy4)
        sh.event_wait_timer(15564, false)              -- 3.8 s
    elseif step == 5 then
        sh.event_step_increment_after_fade(2, true, 0, 6144, false) -- Auto, 1.5
        ladderClimb(o.dvy5)
    elseif step < 0 or step > 5 then -- any other step
        sh.player_freeze(false, o.unfreeze_idle)
        sh.state_set(0)
        o.finish()
    end
end

-- index 7: climbing up the ladder (anim 87); sets flag M5S00_InnerLadderClimbedUp, clears 357.
function MapEvent_SewerLadderUp1()
    sewerLadder({
        wait_extra = true,
        pos  = {232542, -5914, 245907},   -- 56.773, -1.444, 60.036 m
        cam  = {234659, -3522, 243138},   -- 57.29, -0.86, 59.36
        look = {225566, -16384, 246947},  -- 55.07, -4.0, 60.29 m
        anim = 87, dvy4 = -1638, dvy5 = -1638, unfreeze_idle = true,
        finish = function()
            sh.flag_set("M5S00_InnerLadderClimbedUp", true)
            sh.flag_set("M5S00_InnerLadderClimbedDown", false)
        end,
    })
end

-- index 8: climbing down the ladder (anim 88); sets flag M5S00_InnerLadderClimbedDown, clears 356.
function MapEvent_SewerLadderDown2()
    sewerLadder({
        wait_extra = true,
        pos  = {232525, -8478, 245809},   -- 56.769, -2.07, 60.012 m
        cam  = {233840, -23592, 248176},  -- 57.09, -5.76, 60.59
        look = {229212, -8232, 244858},   -- 55.96, -2.01, 59.78 m
        anim = 88, dvy4 = 1638, dvy5 = 1638, unfreeze_idle = true,
        finish = function()
            sh.flag_set("M5S00_InnerLadderClimbedDown", true)
            sh.flag_set("M5S00_InnerLadderClimbedUp", false)
        end,
    })
end

-- index 9: climbing up the ladder (anim 87), BGM muted for the whole event; sets flag M5S00_ExitLadderClimbedUp,
-- clears 359.
function MapEvent_SewerLadderUp2()
    sewerLadder({
        mute = true,
        pos  = {-91070, -7102, 247840},   -- -22.234, -1.734, 60.508 m
        cam  = {-83517, -15728, 262103},  -- -20.39, -3.84, 63.99
        look = {-92405, -11591, 248954},  -- -22.56, -2.83, 60.78 m
        anim = 87, dvy4 = -1638, dvy5 = -1638, unfreeze_idle = false,
        finish = function()
            sh.flag_set("M5S00_ExitLadderClimbedUp", true)
            sh.flag_set("M5S00_ExitLadderClimbedDown", false)
        end,
    })
end

-- index 10: climbing down the ladder (anim 88), no fade-out on step 0; descent
-- vy += dt*0.37, then dt*0.4; clears M5S01_SewerEntryFromResortPending and M5S00_ExitLadderClimbedUp, sets flag M5S00_ExitLadderClimbedDown.
function MapEvent_SewerLadderDown3()
    sewerLadder({
        reset_only = true,
        pos  = {-91115, -9990, 247758},   -- -22.245, -2.439, 60.488 m
        cam  = {-87203, -163, 254484},    -- -21.29, -0.04, 62.13
        look = {-93839, -13230, 247152},  -- -22.91, -3.23, 60.34 m
        anim = 88, dvy4 = 1515, dvy5 = 1638, unfreeze_idle = false,
        finish = function()
            sh.flag_set("M5S01_SewerEntryFromResortPending", false)
            sh.flag_set("M5S00_ExitLadderClimbedUp", false)
            sh.flag_set("M5S00_ExitLadderClimbedDown", true)
        end,
    })
end

-- index 11: paper map pickup.
function MapEvent_PaperMapTake()
    sh.event_paper_map_take("FogSewer1F", "M5S00_PickupMap", "map5_s00.there_is_a_sewer_map")
end
