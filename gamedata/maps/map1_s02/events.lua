-- map1_s02: School Otherworld - 1F and courtyard
-- Event scripts of this area: the engine runs a handler every tick while its event is active.
-- Fixed-point numbers: 4096 = 1.0 (metres, seconds); angles: 4096 = 360 degrees.

function on_boot()
    -- per-map script entry point
end

-- Helpers shared by the handlers below.
-- Setting a step also clears the state timer, the sub-steps and the step timer
-- (sh.event_step_set alone writes only the step).
local function StepSet(v)
    sh.event_step_set(0, v)
    sh.state_timer_set(0)
    sh.event_step_set(1, 0)
    sh.sys_timer_set(0)
    sh.event_step_set(2, 0)
end

-- Camera position / look-at in absolute fixed-point world units.
local function CamPos(x, y, z, accel_xz, accel_y, max_speed_xz, max_speed_y, warp)
    sh.camera_pos(0, 0, 0, x, y, z, accel_xz, accel_y, max_speed_xz, max_speed_y, warp)
end

local function CamLook(x, y, z, accel_x, accel_y, max_speed_x, max_speed_y, warp)
    sh.camera_look(0, 0, 0, x, y, z, accel_x, accel_y, max_speed_x, max_speed_y, warp)
end

-- Y angle of a world object of world_objects.json (BathroomDoor, BossDoor1/2): read, and written
-- keeping its x and z (stored as 16-bit, so a turn wraps).
local function RotY(name)
    local _, y = sh.world_object.rotation_get(name)
    return y
end

local function RotYSet(name, y)
    local x, _, z = sh.world_object.rotation_get(name)
    sh.world_object.rotation_set(name, x, y, z)
end

-- index 0: jammed door.
function MapEvent_DoorJammed()
    sh.event_msg_with_sfx("common.lock_jammed", "door_jammed")
end

-- index 1: locked door (some points of interest of this area play a different sound).
function MapEvent_DoorLocked()
    local poi = sh.event_point()
    if poi == 27 then
        sh.event_msg_with_sfx("common.locked", "door_locked_alt")
    else
        sh.event_msg_with_sfx("common.locked", "door_locked") -- message: door locked, with the door locked sound
    end
end

-- index 2: unlocked door.
function MapEvent_DoorUnlocked()
    sh.event_msg_with_sfx("common.unlocked", "door_unlocked")
end

-- index 3: common pickups, chosen by the event's point of interest.
function TakeCommonPickup()
    local poi = sh.event_point()
    local t = {
        [107] = {"Ampoule","M1S02_Ampoule0"},
        [108] = {"ShotgunShells","M1S02_ShotgunShells0"},
        [109] = {"ShotgunShells","M1S02_ShotgunShells1"},
        [110] = {"FirstAidKit","M1S02_FirstAidKit0"},
        [111] = {"HandgunBullets","M1S02_HandgunBullets0"},
        [112] = {"HandgunBullets","M1S02_HandgunBullets1"},
        [113] = {"HandgunBullets","M1S02_HandgunBullets2"},
        [114] = {"Ampoule","M1S02_Ampoule1"},
        [115] = {"HealthDrink","M1S02_HealthDrink0"},
        [116] = {"FirstAidKit","M1S02_FirstAidKit1"},
        [117] = {"HealthDrink","M1S02_HealthDrink1"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point: first aid kit with the initial flag
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- index 4: the lobby key is used.
function MapEvent_LobbyKeyUse()
    sh.item_remove("LobbyKey", 1)
    sh.event_msg_with_sfx("map1_s02.msg19", "door_unlocked")
end

-- index 5: intro cutscene of the otherworld school courtyard.
-- Two step machines share one step: the camera, then the actions. courtyard_camera_moved and
-- courtyard_msg_audio_idx live in the map state; courtyard_voice_cmds is the voice list.
function CutsceneCourtyardIntro()
    local skipped = false
    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step > 0 and step < 10 then
        skipped = true
        sh.event_step_reset()
        step = sh.event_step_get(0)
    end

    -- Camera.
    if step == 0 then
        CamPos(-81756, -5734, -251699, 0, 0, 0, 0, true)          -- (-19.96, -1.4, -61.45)
        CamLook(-70205, -6103, -263249, 0, 0, 0, 0, true)         -- (-17.14, -1.49, -64.27)
    elseif step >= 1 and step <= 6 then
        -- nothing
    elseif step >= 7 and step <= 10 then
        local spdXz, spdY, angX, angY = sh.camera_default_speeds()
        if sh.map.get("courtyard_camera_moved") == 0 then
            CamPos(-86835, -9338, -242606, 0, 0, spdXz // 8, spdY // 8, false)   -- (-21.2, -2.28, -59.23)
            CamLook(-53657, -4915, -279756, 0, 0, angX // 8, angY // 8, false)   -- (-13.1, -1.2, -68.3)
            local vx, _, vz = sh.vc_view_position()
            if math.abs(vx - -86835) < 1228 and math.abs(vz - -242606) < 1228 then -- 0.3
                sh.map.set("courtyard_camera_moved", 1)
            end
        else
            CamPos(-72867, -20602, -221552, 0, 0, spdXz // 8, spdY // 8, false)  -- (-17.79, -5.03, -54.09)
            CamLook(-80158, 0, -250716, 0, 0, angX // 8, angY // 8, false)       -- (-19.57, 0.0, -61.21)
        end
    else
        sh.flag_set("MapMark_AltSchool1F_CourtyardQuestion", true)                                   -- map mark: Otherworld school 1F courtyard question mark
    end

    -- Actions.
    step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.flashlight(true)
        sh.map.weather_set(6, 127)
        sh.map.particles_update(0, sh.savegame_get("map_idx"), 0)
        local _, py = sh.player_pos()
        sh.player_set_pos(-54476, py, -278937)                    -- -13.3, -68.1 m
        sh.event_waypoint_set(true, 0, 0, -512, -74465, -254935)  -- -45 deg, (-18.18, -62.24)
        sh.flag_set("M1S02_CourtyardIntroWalkStarted", false)
        sh.map.set("courtyard_msg_audio_idx", 0)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_fade_cmd(2, 1, 2, 0, 0)                          -- Auto, out, type 2
    elseif step == 2 then
        sh.event_wait_timer(6144, 0)                              -- 1.5 s
    elseif step == 3 then
        sh.event_wait_player_stop()
    elseif step == 4 then
        sh.event_msg_audio("map1_s02.where_am_i", "courtyard_msg_audio_idx", "courtyard_voice_cmds")        -- "Where am I?"
    elseif step == 5 then
        sh.chara_anim_play_to_end(0, "scene_courtyard_intro")
    elseif step == 6 then
        sh.event_msg_audio("map1_s02.have_i_been_here_before", "courtyard_msg_audio_idx", "courtyard_voice_cmds")        -- "Have I been here before?"
    elseif step == 7 then
        sh.flag_set("M1S02_CourtyardIntroWalkStarted", true)
        sh.event_waypoint_player("walk_forward", 1, false)
    elseif step == 8 then
        sh.event_wait_timer(9011, 0)                              -- 2.2 s
    elseif step == 9 then
        sh.event_msg_audio("map1_s02.i_dont_remember_this_being", "courtyard_msg_audio_idx", "courtyard_voice_cmds")
    elseif step == 10 then
        sh.event_wait_timer(4096, 0)                              -- 1.0 s
    else
        if skipped then
            sh.player_set_pos(-74465, 0, -254935)                 -- (-18.18, 0.0, -62.24)
            sh.player_rotate(-512)                                -- -45 deg
            sh.player_freeze(false, true)
            sh.state_set(0)
            sh.camera_auto_return(true)
        else
            sh.player_freeze(false, false)
            sh.state_set(0)
            sh.camera_auto_return(false)
        end
        sh.event_fade_cmd(0, 0, 2, 0, 0)                          -- Start, in, type 2
        sh.flag_set("M1S02_CourtyardAutosaveMarker", true)                                    -- flag M1S02_CourtyardAutosaveMarker is set while the call runs
        sh.game_state_update()
        sh.flag_set("M1S02_CourtyardAutosaveMarker", false)
    end
end

-- index 6: message over a background picture.
function InspectGoldPlaque()
    sh.event_msg_with_bg("TIM/GOLD2.TIM", 12288, 10240, "map1_s02.a_golden_sun") -- 3.0, 2.5
end

-- index 7: message over a background picture.
function InspectSilverPlaque()
    sh.event_msg_with_bg("TIM/SILVER2.TIM", 12288, 10240, "map1_s02.a_silver_moon")
end

-- index 8: freeze, wait for the player to stop, message.
function MapEvent_PlayerStopMessage()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)                    -- continues into the next step in the same tick
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.event_msg(false, "map1_s02.the_tower_door_is_locked", 0, 0, 0, false)
    else
        sh.player_freeze(false)
        sh.state_set(0)                               -- back to gameplay
    end
end

-- index 9: "CONTINUE"/"COMING SOON" screens after the trial version ends.
local function ClickedEnterOrCancel()
    return sh.input_action("enter", 1) or sh.input_action("cancel", 1)
end

function MapEvent_GameTrialOver()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_fade_cmd(0, 1, 1, 4096, 0)                       -- Start, out, type 1, 1.0
        sh.event_bg_texture_cmd(0, "TIM/CONTINUE.TIM", false)  -- queue the picture load
        sh.event_step_increment(0)
        step = 2
    end
    if step == 2 then
        sh.event_fade_cmd(1, 1, 0, 0, 0)                          -- Wait, out
        return
    elseif step == 3 then
        sh.event_bg_texture_cmd(1, 0, false)                      -- wait for the load
        return
    end
    if step == 4 then
        sh.event_fade_cmd(0, 0, 1, 4096, 0)                       -- Start, in, type 1
        sh.event_step_increment(0)
        step = 5
    end
    if step == 5 then
        sh.event_bg_texture_cmd(2, 0, false)                      -- draw
        if ClickedEnterOrCancel() then
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(16384, 0)                             -- 4.0 s
        if sh.event_step_get(0) == 7 then
            sh.event_step_set(0, 6)                               -- writes only the step
        end
        return
    elseif step == 6 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_fade_cmd(2, 1, 0, 6144, 0)                       -- Auto, out, 1.5
        return
    elseif step == 7 then
        sh.event_bg_texture_cmd(7, "TIM/COMING2.TIM", false)   -- automatic
        return
    end
    if step == 8 then
        sh.event_fade_cmd(0, 0, 0, 6144, 0)                       -- Start, in, 1.5
        sh.event_step_increment(0)
        step = 9
    end
    if step == 9 then
        sh.event_bg_texture_cmd(2, 0, false)
        if ClickedEnterOrCancel() then
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(16384, 0)
        if sh.event_step_get(0) == 11 then
            sh.event_step_set(0, 10)
        end
    elseif step == 10 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_fade_cmd(2, 1, 0, 6144, 0)
    else
        sh.player_freeze(false, false)
        sh.state_set(0)
        sh.game_warm_boot()
        sh.bgm_status_flags_set(sh.bgm_status_flags_get() | 1)  -- pause the music
    end
end

-- index 10: pan over the reception plaques (two TIM textures).
-- Background picture commands + fade + picture cross-fade with a local alpha counter
-- (step 0.0313 per tick, clamped to 1.0).
local recepAlpha = 0

function InspectReceptionPlaques()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_fade_cmd(0, true, 0, 0, false)               -- Start, out
        sh.event_bg_texture_cmd(0, "TIM/RECEPDR1.TIM", false) -- queue the picture load
        recepAlpha = 0
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_bg_texture_cmd(1, 0, false)                  -- wait for the load
    elseif step == 2 then
        sh.event_fade_cmd(1, true, 0, 0, false)               -- Wait
    elseif step == 3 then
        sh.event_bg_texture_cmd(3, 0, false)                  -- keep the first picture in video memory
        sh.event_bg_texture_cmd(4, "TIM/RECEPDR2.TIM", false) -- queue the second picture
        sh.event_step_increment(0)
        step = 4
    end
    if step == 4 then
        sh.event_bg_texture_cmd(2, 0, false)                  -- draw
        sh.event_fade_cmd(2, false, 0, 0, false)              -- Auto, in
    elseif step == 5 then
        sh.event_bg_texture_cmd(2, 0, false)
        if sh.flag_get("M1S02_SeenDoorWithHorizontalSlot") then
            sh.event_msg(false, "map1_s02.if_i_push_it_it", 0, 0, 0, false)           -- "If I push it, it moves a bit."
        else
            sh.event_msg(false, "map1_s02.what_is_this_a_door", 0, 0, 0, false)           -- "What? Is this a door?"
        end
    elseif step == 6 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_bg_texture_cmd(1, 0, false)                  -- wait for the load
    elseif step == 7 then
        if sh.flag_get("M1S02_SeenDoorWithHorizontalSlot") then
            sh.event_step_increment(0)
        else
            sh.event_msg(false, "map1_s02.whats_this", 0, 0, 0, false)           -- "What's this?"
        end
        step = 8 -- continues into step 8 in the same tick
    end
    if step == 8 then
        sh.bg_img_transition(0, recepAlpha)
        recepAlpha = recepAlpha + 128                         -- 0.0313
        if recepAlpha > 4096 then                             -- 1.0
            if sh.event_step_get(0) == 8 then
                StepSet(9)                                    -- also clears the step timers
            else
                recepAlpha = 4096                             -- arrived from step 7 in the same tick: clamp
            end
        end
    elseif step == 9 then
        sh.event_bg_texture_cmd(5, 0, false)                  -- draw the second picture
        sh.event_msg(false, "map1_s02.in_the_center_of_the", 0, 0, 0, false)               -- "horizontal slot"
    elseif step == 10 then
        sh.event_bg_texture_cmd(5, 0, false)
        sh.event_fade_cmd(2, true, 0, 0, false)               -- Auto, out
    elseif step < 0 or step > 10 then -- default
        sh.event_bg_texture_cmd(6, 0, false)                  -- restore video memory
        sh.event_fade_cmd(0, false, 0, 0, false)              -- Start, in
        sh.flag_set("M1S02_SeenDoorWithHorizontalSlot", true)
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 11: inserting the Picture Card into the slot (secret door).
-- The held item is hidden and the weapon put away for the animation, then restored;
-- animations 94/95, a positioned sound, the camera returns to automatic control.
function UsePictureCard()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.player_move_speed_clear()
        sh.cutscene_border(20)                             -- force the cutscene borders on
        local px, py, pz = sh.player_pos()
        sh.player_set_pos(231424, py, 79052)                -- 56.5, 19.3 m
        sh.player_rotate(-1024)                             -- -90 deg
        sh.camera_pos(60.59, -0.83, 18.34, 0, 0, 0, 0, 0, 0, 0, true)
        sh.camera_look(56.7698, -1.45, 19.34, 0, 0, 0, 0, 0, 0, 0, true)
        sh.held_item_hide()
        sh.player_weapon_stash()
        sh.event_step_increment(0)
        step = 1 -- continues into step 1 in the same tick
    end
    if step == 1 then
        sh.event_fade_cmd(1, false, 0, 0, false)             -- Wait, in
    elseif step == 2 then
        sh.event_wait_timer(819, false)                      -- 0.2 s
    elseif step == 3 then
        sh.chara_anim_play_to_end(0, "scene_picture_card_1")
    elseif step == 4 then
        sh.sfx_flags_play("card_reader", 228761, -4505, 79052, 128, 0) -- (55.85, -1.1, 19.3) m, volume 0.5
        sh.event_step_increment(0)
        step = 5 -- continues into step 5 in the same tick
    end
    if step == 5 then
        sh.event_msg(false, "map1_s02.slid_the_picture_card_into", 0, 0, 0, false)              -- "Slid the Picture card into the slot."
    elseif step == 6 then
        sh.sfx_flags_play("door_seal_mech", 229376, -4915, 79052, 128, 0) -- (56.0, -1.2, 19.3) m
        sh.event_step_increment(0)
        step = 7 -- continues into step 7 in the same tick
    end
    if step == 7 then
        sh.chara_anim_play_to_end(0, "scene_picture_card_2")
    elseif step == 8 then
        sh.event_fade_cmd(2, true, 0, 0, false)              -- Auto, out
    elseif step < 0 or step > 8 then -- default
        sh.player_freeze(false, true)                        -- unfreeze the player
        sh.state_set(0)
        sh.event_fade_cmd(0, false, 2, 0, false)             -- Start, in, type 2
        sh.event_fade_cmd(0, false, 0, 0, false)             -- Start, in, type 0
        sh.camera_auto_return(true)                          -- camera returns to automatic control
        sh.flag_set("MapMark_AltSchool1F_SecretDoorArrows", true)                               -- map mark: Otherworld school 1F secret door arrows
        sh.held_item_show()
        sh.player_weapon_restore()
    end
end

-- index 12: item pickup.
function PickupPictureCard()
    sh.event_item_take("PictureCard", 1, "M1S02_PickupPictureCard", "map1_s02.there_is_a_picture_card")
end

-- index 13: item pickup.
function PickupRubberBall()
    sh.event_item_take("RubberBall", 1, "M1S02_PickupRubberBall", "map1_s02.there_is_a_rubber_ball")
end

-- index 14: phone call cutscene by the clock (voice lines 53/54).
-- The held-item queue index and the "skipped before step 5" flag are used only here,
-- so they are Lua locals.
local phoneQueueIdx = 0
local phoneSkippedEarly = false

function CutscenePhoneCall()
    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step >= 2 and step < 26 then
        if step < 5 then
            phoneSkippedEarly = true
        end
        StepSet(27)
        step = 27
    end

    if step == 0 then
        sh.player_freeze(true)
        sh.flag_set("M1S02_PhoneCallStarted", true)
        sh.sd_call("phone_call_start_voice")                                          -- voice line 53
        sh.event_waypoint_set(true, 0, 0, -1024, 83148, 587776)   -- -90 deg, (20.3, 143.5)
        sh.event_tween_reset(0)
        sh.event_tween_reset(3)
        phoneSkippedEarly = false
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
        return
    elseif step == 2 then
        sh.event_wait_timer(6144, 0)                              -- 1.5 s
        return
    elseif step == 3 then
        sh.event_fade_cmd(2, 1, 2, 0, 0)                          -- Auto, out, type 2
        return
    elseif step == 4 then
        sh.event_waypoint_player("walk_forward", 1, false)
        sh.event_wait_timer(8192, 0)                              -- 2.0 s
        return
    end
    if step == 5 then
        sh.held_item_hide()
        sh.player_weapon_stash()
        sh.flashlight(true)
        CamPos(73973, -8151, 584785, 0, 0, 0, 0, true)            -- (18.06, -1.99, 142.77)
        CamLook(85811, 1105, 591339, 0, 0, 0, 0, true)            -- (20.95, 0.27, 144.37)
        sh.event_step_increment(0)
        step = 6
    end
    if step == 6 then
        sh.event_waypoint_player("walk_forward", 1, false)
        return
    end
    if step == 7 then
        sh.event_chara_anim(0, 0, "scene_phone_call_1", false)                      -- start the animation
        sh.event_step_increment(0)
        step = 8
    end
    if step == 8 then
        sh.event_wait_timer(2048, 0)                              -- 0.5 s
        return
    end
    if step == 9 then
        local px, _, pz = sh.player_pos()
        CamPos(px + 16384, -6144, pz, 0, 0, 0, 0, true)           -- +4.0, -1.5
        CamLook(px, -4096, pz, 0, 0, 0, 0, true)                  -- -1.0 m
        sh.event_step_increment(0)
        step = 10
    end
    if step == 10 then
        sh.event_wait_timer(14336, 0)                             -- 3.5 s
        return
    end
    if step == 11 then
        sh.sd_call("phone_ring")
        sh.sd_call("stop_xa_audio")
        phoneQueueIdx = sh.held_item_set("CutscenePhone")
        sh.event_step_increment(0)
        step = 12
    end
    if step == 12 then
        if sh.fs_entry_loaded(phoneQueueIdx) then
            sh.held_item_show()
            sh.xa_preload("phone_call_voice")                                   -- voice line 54
            sh.event_step_increment(0)
        end
        return
    elseif step == 13 then
        sh.event_chara_anim(1, 0, 0, false)                       -- wait for the animation to end
        sh.xa_preload("phone_call_voice")
        return
    end
    if step == 14 then
        sh.sd_call("phone_call_voice")
        sh.event_step_increment(0)
        step = 15
    end
    if step == 15 then
        sh.event_msg(false, "map1_s02.msg31", 0, 0, 0, false)
        local px, _, pz = sh.player_pos()
        CamPos(px + 16384 + sh.event_tween_linear(-11468, 26624, 0), -6144, pz, 0, 0, 0, 0, false) -- -2.8, 6.5 m
        CamLook(px + sh.event_tween_linear(-11468, 26624, 3), -4096, pz, 0, 0, 0, 0, false)
        return
    end
    if step == 16 then
        CamPos(71720, -13025, 590028, 0, 0, 0, 0, true)           -- (17.51, -3.18, 144.05)
        CamLook(83066, -1720, 586629, 0, 0, 0, 0, true)           -- (20.28, -0.42, 143.22)
        sh.event_step_increment(0)
        step = 17
    end
    if step == 17 then
        sh.event_wait_timer(7372, 0)                              -- 1.8 s
        return
    end
    if step == 18 then
        sh.event_chara_anim(0, 0, "scene_phone_call_2", false)
        sh.event_step_increment(0)
        step = 19
    end
    if step == 19 then
        sh.event_wait_timer(4096, 0)
        return
    elseif step == 20 then
        sh.event_chara_anim(2, 0, 0, false)                       -- lock the animation
        sh.event_msg(false, "map1_s02.that_was_cheryls_voice_i", 0, 0, 0, false)
        return
    end
    if step == 21 then
        sh.event_chara_anim(3, 0, 0, false)                       -- unlock the animation
        sh.event_step_increment(0)
        step = 22
    end
    if step == 22 then
        sh.event_wait_timer(6144, 0)
        return
    elseif step == 23 then
        sh.event_fade_cmd(2, 1, 0, 0, 0)                          -- Auto, out
        return
    end
    if step == 24 then
        sh.player_weapon_restore()
        sh.sd_call("stop_xa_audio")
        phoneQueueIdx = sh.held_item_prev()
        sh.event_fade_cmd(0, 0, 2, 0, 0)                          -- Start, in, type 2
        sh.sd_call("phone_voice_end")
        sh.event_step_increment(0)
        step = 25
    end
    if step == 25 then
        sh.event_wait_timer(4096, 0)
    elseif step == 26 then
        if sh.fs_entry_loaded(phoneQueueIdx) then
            sh.event_step_reset()
        end
    elseif step == 27 then
        sh.event_fade_cmd(2, 1, 0, 0, 0)
    elseif step == 28 then
        sh.sd_call("stop_xa_audio")
        sh.event_chara_anim(3, 0, 0, false)                       -- unlock the animation
        if not phoneSkippedEarly then
            sh.player_weapon_restore()
        end
        sh.held_item_prev()
        sh.held_item_show()
        CamPos(71720, -13025, 590028, 0, 0, 0, 0, true)
        CamLook(83066, -1720, 586629, 0, 0, 0, 0, true)
        local _, py = sh.player_pos()
        sh.player_set_pos(83148, py, 587776)                      -- (20.3, 143.5)
        sh.player_rotate(-1024)                                   -- -90 deg
        sh.fs_wait()
        sh.cutscene_border(0)                                     -- cutscene borders off
        sh.event_step_increment(0)
    else
        sh.event_fade_cmd(0, 0, 0, 0, 0)
        sh.player_freeze(false, false)
        sh.state_set(0)
        sh.flag_set("M1S02_PhoneCallDone", true)
        sh.flag_set("M1S02_PhoneReceiverInHand", false)
        sh.camera_auto_return(true)
    end
end

-- index 15: tilting the board on legs in the pool (voice line 56).
-- pool_board_timer (never reset) and the board's world object live in the map state,
-- so the board keeps its tilt when the player comes back.
function PoolBoardTilt()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.flag_set("M1S02_BathroomDoorSceneStarted", true)
        sh.sd_call("pool_board_voice")                                          -- voice line 56
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.chara_slot_anim_flags_clear(0, 2)                      -- clear bit 2 of the player's animation flags
        sh.water_effect_enable()
        sh.event_tween_reset(0)
        sh.event_tween_reset(1)
        CamPos(86016, -2048, 245760, 0, 0, 0, 0, true)            -- (21.0, -0.5, 60.0)
        CamLook(69632, -2048, 245760, 0, 0, 0, 0, true)           -- (17.0, -0.5, 60.0)
        sh.event_fade_cmd(0, 1, 2, 0, 0)                          -- Start, out, type 2
        sh.event_step_increment(0)
    elseif step == 3 then
        local t = sh.map.get("pool_board_timer")
        if t > 4096 then                                          -- 1.0
            CamPos(sh.event_tween_linear(-6144, 20480, 0) + 86016, -2048, 245760, 0, 0, 0, 0, false)
            local lx = sh.event_tween_linear(-6144, 20480, 0) + 69632
            local ly = sh.event_tween_linear(-6144, 20480, 1) - 2048
            CamLook(lx, ly, 245760, 12288, 0, 1228, 0, false)     -- 3.0, 0.3 m
        end

        t = (t + sh.delta_time_get()) & 0xFFFF
        sh.map.set("pool_board_timer", t)
        local angleAdd
        if t < 1228 then                                          -- 0.3
            angleAdd = 0
        elseif t < 2048 then                                      -- 0.5
            angleAdd = -512                                       -- -45 deg
        elseif t < 4096 then
            angleAdd = 56                                         -- 5 deg
        elseif t < 34816 then                                     -- 8.5
            if t - 4096 <= 24576 then                             -- 6.0
                angleAdd = ((t - 4096) * 56) // 4096 - 398        -- 5 deg, 35 deg
            else
                angleAdd = -62                                    -- -5.5 deg
            end
        else
            angleAdd = 0
            sh.event_step_increment(0)
        end

        local ry = RotY("BathroomDoor") + (sh.delta_time_get() * angleAdd) // 4096
        ry = ((ry + 32768) % 65536) - 32768                       -- wrap to a signed 16-bit angle
        if ry < -1280 then                                        -- -112.5 deg
            ry = -1280
        end
        RotYSet("BathroomDoor", ry)
    elseif step > 3 or step < 0 then
        sh.player_freeze(false, false)
        sh.state_set(0)
        sh.chara_slot_anim_flags_set(0, 2)
        sh.water_effect_disable()
        sh.event_fade_cmd(0, 0, 2, 0, 0)
        sh.camera_auto_return(true)
        sh.flag_set("M1S02_BathroomDoorPushedOpen", true)
    end
end

-- index 16: message over a background picture, then flag M1S02_MonsterLurksMessageRead is set.
function InspectLotteryMessage()
    sh.flag_set("M1S02_MonsterLurksMessageRead", true)
    sh.event_msg_with_bg("TIM/LVTRYMSG.TIM", 0, 0, "map1_s02.leonard_rhine_the_monster")
end

-- index 17: shotgun pickup with the inspect animation
-- (same steps as the steel pipe pickup, map2_s00 index 7).
function PickupShotgun()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_inv_item_cmd(0, "Shotgun", 0, false) -- queue the item model load
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 or step == 3 then
        if step == 2 then                                     -- continues into step 3 in the same tick
            sh.event_chara_anim(0, 0, "pickup_crouch", false)              -- start the animation: bend down
            sh.event_step_increment(0)
        end
        sh.event_inv_item_cmd(1, "Shotgun", 0, false) -- wait for the model load
    elseif step == 4 then
        sh.event_chara_anim(1, 0, 0, false)                   -- wait for the animation to end
    elseif step == 5 then
        if sh.pickup_animate("Shotgun") then
            sh.event_msg(true, "map1_s02.there_is_a_shotgun_take", 6, 7, 0, false)            -- take / leave
        end
        sh.flag_set("M1S02_PickupShotgun", true)
    elseif step == 6 then
        sh.event_inv_item_cmd(3, "Shotgun", 6, false) -- add the item: shotgun shells pickup count
        StepSet(8)                                            -- step 8 from the next tick
    elseif step == 7 then
        sh.flag_set("M1S02_PickupShotgun", false)
        sh.event_step_increment(0)
        step = 8                                              -- continues into step 8 in the same tick
    end
    if step == 8 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")                      -- straighten up
    elseif step > 8 then
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 18: two rotating gate leaves with a choice panel.
-- The camera-done / reverse / speed / rotation state is used and reset only by this
-- handler, so it is kept in Lua locals; the leaves are world objects 6 and 7.
local gateCamDone = 0
local gateReverse = 0
local gateSpeed = 0
local gateRotX, gateRotY = 0, 0 -- rotation x / y

local function GateStep(cur, delta, limit)
    -- (cur + delta > limit) ? (limit - cur, limit) : (delta, cur + delta)
    if cur + delta > limit then
        return limit - cur, limit
    end
    return delta, cur + delta
end

local function GateStepBack(cur, delta, limit)
    -- (cur - delta > limit) ? (cur - limit, limit) : (delta, cur - delta)
    if cur - delta > limit then
        return cur - limit, limit
    end
    return delta, cur - delta
end

function RotatingGates()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        gateCamDone = 0
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_msg(true, "map1_s02.msg41", 2, 4, -1, false)
        gateReverse = 0
        gateSpeed = 0
        gateRotY = 0
        gateRotX = 0
        return
    end
    if step == 2 or step == 4 then
        if step == 2 then
            gateReverse = 1                                       -- continues into step 4 in the same tick
        end
        if gateCamDone == 0 then
            CamPos(249856, -4915, -245760, 10240, 3072, 6144, 2048, false) -- (61, -1.2, -60); speeds 2.5, 0.75, 1.5, 0.5
            sh.event_wait_timer(8192, 0)                          -- 2.0 s
            local now = sh.event_step_get(0)
            if now ~= 2 and now ~= 4 then
                StepSet(5)
                gateCamDone = (gateCamDone + 1) & 0xFF
            end
        else
            StepSet(5)
        end
        return
    end
    if step == 5 then
        if sh.flag_get("M1S02_BossGateTurnedFromSouth") then
            sh.sfx_flags_play("gate_squeal", 247808, -2867, -249856, 128, 0) -- first sound position
        else
            sh.sfx_flags_play("gate_squeal", 247808, -2867, -241664, 128, 0) -- second sound position
        end
        sh.event_step_increment(0)
        step = 6
    end
    if step == 6 then
        sh.event_wait_timer(4096, 0)                              -- 1.0 s
    elseif step == 7 then
        local dt = sh.delta_time_get()
        if gateRotX == 0 then
            sh.sd_call("gate_hum")
        end
        if gateRotX < 1024 then                                   -- 0.25
            gateSpeed = gateSpeed + (dt * 682) // 4096            -- 0.1666
        else
            gateSpeed = gateSpeed - (dt * 682) // 4096
            gateSpeed = math.max(gateSpeed, 170)
        end

        local full = (dt * math.min(gateSpeed, 682)) // 4096      -- 60 deg
        local rotY0, rotY1
        if gateReverse ~= 0 then
            if sh.flag_get("M1S02_BossGateTurnedFromSouth") then
                rotY0, gateRotX = GateStepBack(gateRotX, -full, 2048)
                rotY1, gateRotY = GateStepBack(gateRotY, (-full) // 2, 1024)
            else
                rotY1, gateRotX = GateStepBack(gateRotX, -full, 2048)
                rotY0, gateRotY = GateStepBack(gateRotY, (-full) // 2, 1024)
            end
        elseif sh.flag_get("M1S02_BossGateTurnedFromSouth") then
            rotY0, gateRotX = GateStep(gateRotX, full, 2048)
            rotY1, gateRotY = GateStep(gateRotY, full // 2, 1024)
        else
            rotY1, gateRotX = GateStep(gateRotX, full, 2048)
            rotY0, gateRotY = GateStep(gateRotY, full // 2, 1024)
        end

        RotYSet("BossDoor2", RotY("BossDoor2") + rotY0)
        RotYSet("BossDoor1", RotY("BossDoor1") + rotY1)

        sh.sfx_falloff_pitch_play("gate_hum", 239206, -4096, -245760, gateSpeed >> 3, 73728, 0) -- gate hum position, distance 18

        if gateRotX == 2048 and gateRotY == 1024 then
            RotYSet("BossDoor2", (RotY("BossDoor2") + 0x1040) & 0xC00)
            RotYSet("BossDoor1", (RotY("BossDoor1") + 0x1040) & 0xC00)
            sh.sfx_stop("gate_hum")
            StepSet(1)
        end
    else
        sh.player_freeze(false, false)
        sh.state_set(0)
        sh.flag_set("M1S02_BossGateTurnedFromNorth", false)
        sh.flag_set("M1S02_BossGateTurnedFromSouth", false)
        if gateCamDone ~= 0 then
            sh.camera_auto_return(false)
        end
    end
end

-- index 19: raising/lowering the platform (lift) with its hum (sound lift_platform).
-- The fade timestep, a falling-off pitched sound and the lift object position
-- move together with the player's height (lift physics).
local liftHumAccum = 0 -- hum volume, kept across ticks, reset on step 0

function MapEvent_LiftPlatform()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_fade_cmd(0, true, 2, 0, false)            -- Start, out, type 2
        liftHumAccum = 0
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.event_wait_timer(1228, false)                    -- 0.3 s
    elseif step >= 3 and step <= 5 then
        -- steps 3 -> 4 -> 5 continue into each other in the same tick.
        if step == 3 then
            sh.event_chara_anim(0, 0, "scene_lift_platform", false)           -- start the animation
            sh.event_step_increment(0)
        end
        if step <= 4 then
            sh.event_fade_cmd(0, true, 0, 122, false)       -- Start, out, 0.03
            sh.event_step_increment(0)
        end
        sh.event_fade_cmd(1, true, 0, 0, false)             -- Wait, out
        local dt = sh.delta_time_get()
        sh.screen_fade_timestep_set(math.min(sh.screen_fade_timestep_get() + (dt * 409) // 4096, 12288))
    else
        sh.player_freeze(false)
        sh.state_set(0)
        sh.flag_set("M1S02_BossLiftRidden", true)
        sh.sfx_stop("lift_platform")
    end
    -- Hum and lift physics while it moves (steps 2..5).
    step = sh.event_step_get(0)
    if step >= 2 and step < 6 then
        if liftHumAccum == 0 then
            sh.sd_call("lift_platform")
        end
        local wx, wy, wz = sh.world_object.position_get("Box")
        sh.sfx_falloff_pitch_play("lift_platform", wx, wy, wz, (liftHumAccum + 409) >> 4, 131072, 0) -- volume offset 0.1, distance 32.0
        local dt = sh.delta_time_get()
        liftHumAccum = math.min(liftHumAccum + dt, 1638)    -- clamp 0.4
        local rise = (dt * liftHumAccum) // 4096            -- dt * accumulated hum
        local px, py, pz = sh.player_pos()
        sh.player_set_pos(px, py + rise, pz)
        sh.world_object.position_set("Box", wx, wy + rise, wz)
    end
end

-- index 20: classroom key pickup with the inspect animation
-- (same steps as the steel pipe pickup, map2_s00 index 7).
function PickupClassroomKey()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_inv_item_cmd(0, "ClassroomKey", 0, false) -- queue the item model load
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 or step == 3 then
        if step == 2 then                                     -- continues into step 3 in the same tick
            sh.event_chara_anim(0, 0, "pickup_crouch", false)              -- start the animation: bend down
            sh.event_step_increment(0)
        end
        sh.event_inv_item_cmd(1, "ClassroomKey", 0, false) -- wait for the model load
    elseif step == 4 then
        sh.event_chara_anim(1, 0, 0, false)                   -- wait for the animation to end
    elseif step == 5 then
        if sh.pickup_animate("ClassroomKey") then
            sh.event_msg(true, "map1_s02.there_is_a_classroom_key", 6, 7, 0, false)            -- take / leave
        end
        sh.flag_set("M1S02_PickupClassroomKey", true)
    elseif step == 6 then
        sh.event_inv_item_cmd(3, "ClassroomKey", 1, false) -- add the item
        StepSet(8)                                            -- step 8 from the next tick
    elseif step == 7 then
        sh.flag_set("M1S02_PickupClassroomKey", false)
        sh.event_step_increment(0)
        step = 8                                              -- continues into step 8 in the same tick
    end
    if step == 8 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")                      -- straighten up
    elseif step > 8 then
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 21: end of the courtyard cutscene (back to gameplay).
function CourtyardCutsceneEnd()
    sh.map.weather_set(6, 127)
    sh.map.particles_update(0, sh.savegame_get("map_idx"), 0)
    sh.flag_set("M1S02_CourtyardAutosaveMarker", false)
    sh.state_set(0)
end
