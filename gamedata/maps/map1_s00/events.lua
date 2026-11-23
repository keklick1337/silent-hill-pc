-- map1_s00: School - 1F, courtyard, basement
-- Event scripts of this area: the engine runs a handler every tick while its event is active.
-- Fixed-point numbers: 4096 = 1.0 (metres, seconds); angles: 4096 = 360 degrees.

function on_boot()
end

-- index 0: jammed door.
function MapEvent_DoorJammed()
    sh.event_msg_with_sfx("common.lock_jammed", "door_jammed") -- message common.lock_jammed: door jammed
end

-- index 1: locked door.
function MapEvent_DoorLocked()
    sh.event_msg_with_sfx("common.locked", "door_locked")
end

-- index 2: unlocked door.
function MapEvent_DoorUnlocked()
    sh.event_msg_with_sfx("common.unlocked", "door_unlocked") -- message common.unlocked: door unlocked
end

-- index 3: common pickups, chosen by the event's point of interest.
function MapEvent_CommonItemTake()
    local poi = sh.event_point()
    local t = {
        [94] = {"HandgunBullets", "M1S00_HandgunBullets0"},
        [95] = {"HandgunBullets", "M1S00_HandgunBullets1"},
        [96] = {"HandgunBullets", "M1S00_HandgunBullets2"},
        [97] = {"HealthDrink",    "M1S00_HealthDrink"},
        [98] = {"FirstAidKit",    "M1S00_FirstAidKit"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point: first aid kit, flag 0
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- index 4: "A golden sun" — the empty receptacle with the GOLD1 texture
-- (GOLD1 picture at 2.5, 2.0, message map1_s00.a_golden_sun).
function MapEvent_GoldenSunEmptyReceptacleInspect()
    sh.delta_time_reset()
    sh.event_msg_with_bg("TIM/GOLD1.TIM", 2.5, 2.0, "map1_s00.a_golden_sun")
end

-- index 5: the receptacle with the golden plate inserted, GOLD2 (msg map1_s00.a_golden_sun).
function MapEvent_GoldenSunFilledReceptacleInspect()
    sh.delta_time_reset()
    sh.event_msg_with_bg("TIM/GOLD2.TIM", 2.5, 2.0, "map1_s00.a_golden_sun")
end

-- index 7: the empty silver receptacle, SILVER1 (msg map1_s00.a_silver_moon).
function MapEvent_SilverMoonEmptyReceptacleInspect()
    sh.delta_time_reset()
    sh.event_msg_with_bg("TIM/SILVER1.TIM", 2.5, 2.0, "map1_s00.a_silver_moon")
end

-- index 8: the receptacle with the silver plate inserted, SILVER2 (msg map1_s00.a_silver_moon).
function MapEvent_SilverMoonFilledReceptacleInspect()
    sh.delta_time_reset()
    sh.event_msg_with_bg("TIM/SILVER2.TIM", 2.5, 2.0, "map1_s00.a_silver_moon")
end

-- index 20: school map pickup (paper map 6, flag "school map picked up", message map1_s00.there_is_a_school_map).
function MapEvent_TownMapTake()
    sh.event_paper_map_take("FogSchool1F", "M1S00_PickupMap", "map1_s00.there_is_a_school_map")
end

-- index 22: empty reset handler: freeze + unfreeze + back to Gameplay.
function MapEvent_PlayerControlReset()
    sh.player_freeze(true)
    sh.player_freeze(false)
    sh.state_set(0)
end

-- Notes near the school (PAPER1-3.TIM, two message indices: title + text).
-- Picture placement: 2.5, 1.5.
function MapEvent_Note1Read()
    sh.event_msg_with_texture("TIM/PAPER1.TIM", 10240, 6144, "map1_s00.its_written_in_blood", "map1_s00.10_00_alchemy_laboratory_gold")  -- PAPER1.TIM
end

function MapEvent_Note2Read()
    sh.event_msg_with_texture("TIM/PAPER2.TIM", 10240, 6144, "map1_s00.its_written_in_blood", "map1_s00.12_00_a_place_with")  -- PAPER2.TIM
end

function MapEvent_Note3Read()
    sh.event_msg_with_texture("TIM/PAPER3.TIM", 10240, 6144, "map1_s00.its_written_in_blood", "map1_s00.5_00_darkness_that_brings")  -- PAPER3.TIM
end

-- index 10: the school clock tower.
function MapEvent_ClockTowerInspect()
    sh.delta_time_reset()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_step_increment_delayed(1228, false)   -- 0.3 s
    elseif step == 2 then
        sh.event_selection(false, "map1_s00.the_tower_door_is_locked")                  -- "The tower door is locked."
    elseif step == 3 then
        -- clock texture by flags: 72 -> 5:00, 71 -> 12:00, otherwise 10:00
        local tex = "TIM/CLOCK.TIM"                                -- CLOCK.TIM
        if sh.flag_get("M1S00_SilverMedallionPlaced") then tex = "TIM/CLOCK5.TIM"              -- CLOCK5.TIM
        elseif sh.flag_get("M1S00_GoldMedallionPlaced") then tex = "TIM/CLOCK12.TIM" end      -- CLOCK12.TIM
        sh.overlay_screen_show(tex, 12288, 8192)
    elseif step == 4 then
        sh.overlay_screen_set(2, 0, false)
        local msg = "map1_s00.the_hands_are_stopped_at"                                  -- "stopped at 10:00."
        if sh.flag_get("M1S00_SilverMedallionPlaced") then msg = "map1_s00.the_hands_are_stopped_at_3"                -- 5:00
        elseif sh.flag_get("M1S00_GoldMedallionPlaced") then msg = "map1_s00.the_hands_are_stopped_at_2" end        -- 12:00
        sh.event_selection(false, msg)
    elseif step == 5 then
        sh.overlay_screen_hide(12288, 8192)
    else
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 11: the tower door (locked, note PAPER1); sound volume 0.5.
function MapEvent_TowerDoorLocked()
    sh.delta_time_reset()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.sfx_3d("door_locked", 128)                            -- door locked sound at the event point
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.event_selection(false, "common.locked")                   -- "It's locked."
    elseif step == 3 then
        sh.overlay_screen_show("TIM/PAPER1.TIM", 12288, 8192)       -- PAPER1.TIM
    elseif step == 4 then
        sh.overlay_screen_set(2, 0, false)
        if sh.input_action("enter") or sh.input_action("cancel") then
            sh.event_step_increment(0)
        end
    elseif step == 5 then
        sh.overlay_screen_hide(12288, 8192)
    else
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 17: the photo at the reception (RECEPPIC.TIM).
function MapEvent_ReceptionPhotoInspect()
    sh.event_msg_with_bg("TIM/RECEPPIC.TIM", 0.0, 0.0, "map1_s00.a_picture_of_a_door")
end

-- index 21: the library book (USCBOOK.TIM) with a dimmed background.
-- Dimmed background (gamma 13/32 = 104), then the book picture with message 38.
function MapEvent_LibraryBookInspect()
    sh.background_gamma(104) -- 13/32
    sh.event_msg_with_bg("TIM/USCBOOK.TIM", 0.0, 0.0, "map1_s00.moore_ranaldo_gordon_this_must")
end

-- index 6: inserting the golden plate (GOLD1 -> GOLD2, cross-fade driven by the state timer).
-- Overlay screen states 7/3/2/8/5/6, sounds 1416/1419, picture cross-fade
-- (timer += 1/16 per tick), message map1_s00.a_golden_sun "A golden sun.",
-- flags 71 + clock tower circle map mark (1120).
function MapEvent_GoldenSunPlateInsert()
    sh.delta_time_reset()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment_after_fade(0, true, 0, 10240, false)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.overlay_screen_set(7, "TIM/GOLD1.TIM", false)     -- show GOLD1
    elseif step == 2 then
        sh.event_step_increment_after_fade(1, true, 0, 0, false)
    elseif step == 3 then
        sh.overlay_screen_set(3, 0, false)
        sh.event_step_increment_after_fade(0, false, 0, 8192, false)
        sh.event_step_increment(0)
        step = 4
    end
    if step == 4 then
        sh.overlay_screen_set(2, 0, false)
        sh.overlay_screen_set(8, "TIM/GOLD2.TIM", false)          -- load GOLD2 (GOLD2.TIM)
    elseif step == 5 then
        sh.overlay_screen_set(2, 0, false)
        sh.event_step_increment_after_fade(1, false, 0, 0, false)
    elseif step == 6 then
        sh.overlay_screen_set(2, 0, false)
        sh.event_step_increment_delayed(2048, false)   -- 0.5 s
    elseif step == 7 then
        sh.sd_call("item_place_receptacle")
        sh.event_step_increment(0)
        step = 8 -- continues into step 8 in the same tick
    end
    if step == 8 then
        sh.bg_img_transition(0, sh.state_timer_get())      -- plate cross-fade
        sh.state_timer_set(sh.state_timer_get() + 256)         -- 1/16
        if sh.state_timer_get() > 4096 then sh.event_step_increment(0) end
    elseif step == 9 then
        sh.sd_call("plate_insert")
        sh.event_step_increment(0)
        step = 10 -- continues into step 10 in the same tick
    end
    if step == 10 then
        sh.overlay_screen_set(5, 0, false)
        sh.event_step_increment_delayed(4096, false)   -- 1.0 s
    elseif step == 11 then
        sh.overlay_screen_set(5, 0, false)
        sh.event_msg(false, "map1_s00.a_golden_sun")                        -- "A golden sun."
    elseif step == 12 then
        sh.overlay_screen_set(5, 0, false)
        sh.event_step_increment_after_fade(2, true, 0, 8192, false)
    elseif step < 0 or step > 12 then -- default
        sh.overlay_screen_set(6, 0, false)
        sh.event_step_increment_after_fade(0, false, 0, 10240, false)
        sh.flag_set("M1S00_GoldMedallionPlaced", true)
        sh.flag_set("MapMark_FogSchool1F_ClockTowerCircle", true)                        -- map mark: clock tower circle
        sh.player_freeze(false)
        sh.state_set(0)
    end
end
-- index 9: inserting the silver plate (SILVER1 -> SILVER2).
-- Mirrors the golden one (index 6): msg map1_s00.a_silver_moon "A silver moon.", flags 72 + 1120.
function MapEvent_SilverMoonPlateInsert()
    sh.delta_time_reset()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment_after_fade(0, true, 0, 10240, false)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.overlay_screen_set(7, "TIM/SILVER1.TIM", false)   -- show SILVER1
    elseif step == 2 then
        sh.event_step_increment_after_fade(1, true, 0, 0, false)
    elseif step == 3 then
        sh.overlay_screen_set(3, 0, false)
        sh.event_step_increment_after_fade(0, false, 0, 8192, false)
        sh.event_step_increment(0)
        step = 4
    end
    if step == 4 then
        sh.overlay_screen_set(2, 0, false)
        sh.overlay_screen_set(8, "TIM/SILVER2.TIM", false)   -- load SILVER2
    elseif step == 5 then
        sh.overlay_screen_set(2, 0, false)
        sh.event_step_increment_after_fade(1, false, 0, 0, false)
    elseif step == 6 then
        sh.overlay_screen_set(2, 0, false)
        sh.event_step_increment_delayed(2048, false)   -- 0.5 s
    elseif step == 7 then
        sh.sd_call("item_place_receptacle")
        sh.event_step_increment(0)
        step = 8 -- continues into step 8 in the same tick
    end
    if step == 8 then
        sh.bg_img_transition(0, sh.state_timer_get())
        sh.state_timer_set(sh.state_timer_get() + 256)         -- 1/16
        if sh.state_timer_get() > 4096 then sh.event_step_increment(0) end
    elseif step == 9 then
        sh.sd_call("plate_insert")
        sh.event_step_increment(0)
        step = 10 -- continues into step 10 in the same tick
    end
    if step == 10 then
        sh.overlay_screen_set(5, 0, false)
        sh.event_step_increment_delayed(4096, false)
    elseif step == 11 then
        sh.overlay_screen_set(5, 0, false)
        sh.event_msg(false, "map1_s00.a_silver_moon")                        -- "A silver moon."
    elseif step == 12 then
        sh.overlay_screen_set(5, 0, false)
        sh.event_step_increment_after_fade(2, true, 0, 8192, false)
    elseif step < 0 or step > 12 then -- default
        sh.overlay_screen_set(6, 0, false)
        sh.event_step_increment_after_fade(0, false, 0, 10240, false)
        sh.flag_set("M1S00_SilverMedallionPlaced", true)
        sh.flag_set("MapMark_FogSchool1F_ClockTowerCircle", true)                        -- map mark: clock tower circle
        sh.player_freeze(false)
        sh.state_set(0)
    end
end
-- index 15: climbing down the hatch (drop into the school basement).
-- Skip on steps 3-4 resets the fade timestep and the step machine; camera at
-- (-56.41, -5.67, -55.39) looking at (-56.4, -1.8699, -54.16), vy = -2.3, heading 0,
-- player animation 88, fall vy += dt * 0.3; exit by flag M1S00_EastHatchDropRoomLoaded
-- (x -60, y 0, z -63.7, 0; clears 184 + 78) or 81 (z -56.3, y 0.6, 180; clears 185 + 81).
function MapEvent_HatchDropToBasement()
    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step >= 3 and step < 5 then
        sh.screen_fade_reset_timestep()
        sh.event_step_reset()
        step = -1 -- reset the step machine: go to the final step
    end
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.camera_pos(-56.41, -5.67, -55.39, 0, 0, 0, 0, 0, 0, 0, true)
        sh.camera_look(-56.4, -1.8699, -54.16, 0, 0, 0, 0, 0, 0, 0, true)
        local px, _, pz = sh.player_pos()               -- only vy changes
        sh.player_set_pos(px, -9420, pz)                -- -2.3 m
        sh.player_rotate(0)                             -- heading 0 deg
        sh.event_step_increment_after_fade(0, false, 0, 6144, false) -- 1.5 s
        sh.event_chara_anim(0, 0, "hatch_drop_down", false)            -- player animation 88
        sh.event_step_increment(0)
        step = 3
    end
    if step == 3 then
        sh.event_step_increment_delayed(14336, false)    -- 3.5 s
        sh.player_fall_tick()                            -- vy += dt * 0.3
    elseif step == 4 then
        sh.player_fall_tick()
        sh.event_step_increment_after_fade(2, true, 0, 6144, false)
    elseif step < 0 or step > 4 then -- default
        if sh.flag_get("M1S00_EastHatchDropRoomLoaded") then
            sh.player_set_pos(-245760, 0, -260915)       -- -60, 0, -63.7 m
            sh.player_rotate(0)
            sh.flag_set("M1S00_EastHatchDropStarted", false)
            sh.flag_set("M1S00_EastHatchDropRoomLoaded", false)
        elseif sh.flag_get("M1S00_WestHatchDropRoomLoaded") then
            sh.player_set_pos(-245760, 2457, -230604)    -- -60, 0.6, -56.3 m
            sh.player_rotate(2048)                       -- 180 deg
            sh.flag_set("M1S00_WestHatchDropStarted", false)
            sh.flag_set("M1S00_WestHatchDropRoomLoaded", false)
        end
        sh.event_step_increment_after_fade(0, false, 0, 6144, false)
        sh.player_freeze(false, true)                    -- unfreeze the player
        sh.state_set(0)
        sh.camera_auto_return(true)                      -- camera returns to automatic control
    end
end
-- index 16: climbing up out of the basement through the hatch (reverse of index 15).
-- Skip on steps 4-5 resets the step machine; freeze + clear flags 82/79/80;
-- camera (-56.74, -1.7698, -55.13) looking at (-55.43, -5.5, -54.56), position
-- (-56.34, -2.276, -55.1), heading 0; animation 87; POI 16 -> flag M1S00_EastHatchClimbStarted, POI 17 -> flag M1S00_WestHatchClimbStarted;
-- rise vy += dt * (-0.3); exit: unfreeze(true) + vy = 0.
function MapEvent_HatchClimbFromBasement()
    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step >= 4 and step < 6 then
        sh.screen_fade_reset_timestep()
        sh.event_step_reset()
        step = -1 -- reset the step machine: go to the final step
    end
    if step == 0 then
        sh.player_freeze(true)
        sh.flag_set("M1S00_HatchClimbRoomLoaded", false)
        sh.flag_set("M1S00_EastHatchClimbStarted", false)
        sh.flag_set("M1S00_WestHatchClimbStarted", false)
        sh.event_step_increment_after_fade(0, true, 0, 6144, false) -- 1.5 s
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.event_step_increment_after_fade(1, true, 0, 0, false)
    elseif step == 3 then
        sh.camera_pos(-56.74, -1.7698, -55.13, 0, 0, 0, 0, 0, 0, 0, true)
        sh.camera_look(-55.43, -5.5, -54.56, 0, 0, 0, 0, 0, 0, 0, true)
        sh.player_set_pos(-230768, -9322, -225689)     -- -56.34, -2.276, -55.1 m
        sh.player_rotate(0)
        sh.event_step_increment_after_fade(0, false, 0, 6144, false)
        sh.event_chara_anim(0, 0, "hatch_climb_up", false)           -- animation 87
        local poi = sh.event_point()
        if poi == 16 then
            sh.flag_set("M1S00_EastHatchClimbStarted", true)
        elseif poi == 17 then
            sh.flag_set("M1S00_WestHatchClimbStarted", true)
        end
        sh.event_step_increment(0)
        step = 4
    end
    if step == 4 then
        sh.event_step_increment_delayed(14336, false)   -- 3.5 s
        sh.player_rotate(0)
        sh.player_fall_tick(-1)                         -- vy += dt * (-0.3): rising
    elseif step == 5 then
        sh.player_fall_tick(-1)
        sh.event_step_increment_after_fade(2, true, 0, 6144, false)
    elseif step < 0 or step > 5 then -- default
        sh.player_freeze(false, true)                   -- unfreeze the player
        sh.state_set(0)
        local px, _, pz = sh.player_pos()               -- only vy is reset
        sh.player_set_pos(px, 0, pz)
    end
end
-- index 18: switching the boiler on (the boiler light pulses, the controller hums).
-- freeze -> wait for the player to stop -> "press the switch?" (29) -> sound boiler_hum at
-- (20.5, -1.2, -17.5), volume 0.5, + flag M1S00_BoilerSwitchPressed -> wait 1.0 s -> the pulse timer runs
-- (the light is visible while t % 0.85 s < 0.45 s; after 3.5 s -> next step) -> sound split_head_vocal at
-- (18.8, 4.0, -17.5) -> flag M1S00_BoilerRunning + wait 1.5 s
-- -> step reset + flag M1S00_BoilerSceneDone; every tick: flag M1S00_BoilerRunning -> pad effect 112, flag M1S00_BoilerSwitchPressed -> pad effect 144.
local boilerPulseT = 0 -- boiler pulse timer, kept across ticks
function MapEvent_Boiler0()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.selection_silent(true)
        sh.event_selection(true, "map1_s00.there_is_a_switch_do", 3, -1, 0)     -- "Do you want to press the switch?"
    elseif step == 3 then
        sh.sfx_3d_at("boiler_hum", 83968, -4915, -71680, 128) -- (20.5, -1.2, -17.5) m, volume 0.5
        sh.flag_set("M1S00_BoilerSwitchPressed", true)
        sh.event_step_increment(0)
        step = 4
    end
    if step == 4 then
        sh.event_step_increment_delayed(4096, false) -- 1.0 s
    elseif step == 5 then
        boilerPulseT = boilerPulseT + sh.delta_time_get()
        if (boilerPulseT % 3481) < 1843 then
            sh.world.object_add("boiler_light", "boiler_light.pos", "svec:0") -- the boiler light pulses
        end
        if boilerPulseT > 14336 then                 -- 3.5 s
            sh.event_step_increment(0)
        end
    elseif step == 6 then
        sh.sfx_3d_at("split_head_vocal", 77004, 16384, -71680, 128) -- (18.8, 4.0, -17.5) m, volume 0.5
        sh.event_step_increment(0)
        step = 7
    end
    if step == 7 then
        sh.flag_set("M1S00_BoilerRunning", true)
        sh.event_step_increment_delayed(6144, false) -- 1.5 s
    elseif step == 8 then
        sh.event_step_reset()
        sh.flag_set("M1S00_BoilerSceneDone", true)
    elseif step < 0 or step > 8 then -- default
        sh.player_freeze(false)
        sh.state_set(0)
    end
    if sh.flag_get("M1S00_BoilerRunning") then
        sh.pad_effect(9, 112)
    elseif sh.flag_get("M1S00_BoilerSwitchPressed") then
        sh.pad_effect(9, 144)
    end
end
-- index 19: the switched-off boiler: "Nothing appears to happen."
-- freeze -> wait for the player to stop -> "press the switch?" (29, default Yes)
-- -> sound boiler_hum at (20.5, -1.2, -17.5), volume 0.5 -> wait 0.5 s -> message 30.
local Boiler1Pos = {83968, -4915, -71680}
function MapEvent_Boiler1()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.selection_silent(true)                      -- the selection defaults to Yes silently
        sh.event_selection(true, "map1_s00.there_is_a_switch_do", 3, -1, 0)         -- "Do you want to press the switch?"
    elseif step == 3 then
        sh.sfx_3d_at("boiler_hum", Boiler1Pos[1], Boiler1Pos[2], Boiler1Pos[3], 128) -- volume 0.5
        sh.event_step_increment(0)
        step = 4 -- continues into step 4 in the same tick
    end
    if step == 4 then
        sh.event_step_increment_delayed(2048, false)   -- 0.5 s
    elseif step == 5 then
        sh.event_msg(false, "map1_s00.nothing_appears_to_happen")                        -- "Nothing appears to happen."
    elseif step < 0 or step > 5 then -- default
        sh.player_freeze(false)
        sh.state_set(0)
    end
end
-- index 23: "the boiler is working" (the controller hums every tick).
-- freeze -> wait for the player to stop -> message map1_s00.the_boiler_is_working -> unfreeze; every tick: the boiler rumble
-- (pad effect 9 at level 96) every tick.
function MapEvent_Boiler2()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.event_msg(false, "map1_s00.the_boiler_is_working") -- "The boiler is working."
    else
        sh.player_freeze(false)
        sh.state_set(0)
    end
    sh.pad_effect(9, 96) -- boiler hum in the controller
end
