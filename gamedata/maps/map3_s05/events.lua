-- map3_s05: Hospital Otherworld - basement
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

-- Kept between ticks: pause counter of the random hisses while the vines are sprayed.
local g_vinesAcc = 0

-- index 0: jammed door / grate: message common.lock_jammed + a 3D sound at the event point
-- (point 25 plays sound grate_door_jammed, the others the jammed door sound).
function MapEvent_GrateDoorJammed()
    local poi = sh.event_point()
    if poi == 25 then
        sh.event_msg_with_sfx("common.lock_jammed", "grate_door_jammed")
    else
        sh.event_msg_with_sfx("common.lock_jammed", "door_jammed") -- door jammed sound
    end
end

-- index 1: locked door.
function MapEvent_DoorLocked()
    sh.event_msg_with_sfx("common.locked", "door_locked")
end

-- index 2: common pickups, chosen by the event's point of interest.
function MapEvent_CommonItemTake()
    local poi = sh.event_point()
    local t = {
        [96] = {"Ampoule","M3S05_Ampoule"},
        [97] = {"HealthDrink","M3S05_HealthDrink"},
        [98] = {"HandgunBullets","M3S05_HandgunBullets"},
        [99] = {"ShotgunShells","M3S05_ShotgunShells"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point gives a first aid kit with flag 0
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- index 3: basement storeroom key opens the door:
-- remove the key, message map3_s05.used_the_basement_storeroom + the key sound, set flags 1537/256.
function MapEvent_BasementStoreroomKeyUse()
    sh.item_remove("BasementStoreroomKey", 1)
    sh.event_msg_with_sfx("map3_s05.used_the_basement_storeroom", "use_key") -- use key sound
    sh.flag_set("MapMark_AltHospitalBF_StoreroomArrows", true) -- map mark: arrows on the Otherworld hospital basement storeroom door
    sh.flag_set("M3S05_BasementStoreroomUnlocked", true)
end

-- index 4: inspect the ivies (IVIES.TIM background).
-- Background brightness 10/32; load the picture -> wait until the player stops ->
-- bend down -> fade -> wait for the load -> fade + show the picture -> sound 1280 ->
-- picture + message map3_s05.looks_like_a_hallway_under -> fade -> straighten up -> unfreeze.
-- Steps 0 -> 1, 6 -> 7 and 9 -> 10 run in the same tick.
function MapEvent_IviesInspect()
    sh.background_gamma(80) -- 10/32
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_bg_texture_cmd(0, "TIM/IVIES.TIM", false) -- QueueRead, IVIES.TIM
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.chara_anim_play_to_end(0, "pickup_crouch")
    elseif step == 3 then
        sh.event_fade_cmd(2, true, 0, 0, false)  -- Auto, out
    elseif step == 4 then
        sh.event_bg_texture_cmd(1, 0, false)     -- AwaitLoad
    elseif step == 5 then
        sh.event_fade_cmd(2, false, 0, 0, false) -- Auto, in
        sh.event_bg_texture_cmd(2, 0, false)     -- Draw
    elseif step == 6 then
        -- The original plays the base sound id here, which makes no sound.
        sh.event_step_increment(0)
        step = 7
    end
    if step == 7 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_msg(false, "map3_s05.looks_like_a_hallway_under")
    elseif step == 8 then
        sh.event_fade_cmd(2, true, 0, 0, false)
        sh.event_bg_texture_cmd(2, 0, false)
    elseif step == 9 then
        sh.event_fade_cmd(0, false, 0, 0, false) -- Start
        sh.event_step_increment(0)
        step = 10
    end
    if step == 10 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")
    elseif step > 10 or step < 0 then            -- finish
        sh.player_freeze(false)
        sh.state_set(0)
    end
end
-- index 5: crawl under the grate (steps 0-12): fade, warp, camera, sound flame_ambient at the
-- flame spot (vines_flame_sfx_pos), line 24, animations 59/60; the lighter at the end.
-- Steps 0->1, 2->3, 5->6, 7->8 and 9->10 run in the same tick.
local function CrawlSfx1532()
    sh.sfx_flags_play("flame_ambient", 0x15199, 0, -0x14000, 128, 0) -- at the flame spot, volume 0.5
end

function MapEvent_CrawlUnderGrate()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_fade_cmd(0, true, 2, 0, false) -- Start, out, type 2, 0
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
        return
    end
    if step == 2 then
        local _, py, _ = sh.player_pos()
        sh.player_set_pos(83558, py, -81920) -- 20.4, -20.0 m
        sh.player_rotate(1024) -- 90 deg
        sh.chara_field_set(0, "collision_state", 5) -- collision state 5
        sh.camera_pos(74915 / 4096, -3768 / 4096, -80732 / 4096, 0, 0, 0, 0, 0, 0, 0, true) -- (18.29, -0.92, -19.71)
        sh.camera_look(91095 / 4096, -3112 / 4096, -83230 / 4096, 0, 0, 0, 0, 0, 0, 0, true) -- (22.24, -0.76, -20.32)
        sh.event_step_increment(0)
        step = 3
    end
    if step == 3 then
        sh.chara_anim_play_to_end(0, "pickup_crouch")
    elseif step == 4 then
        sh.event_wait_timer(2457, false) -- 0.6 s
    elseif step == 5 or step == 7 or step == 9 then
        CrawlSfx1532()
        sh.event_step_increment(0)
        if step == 5 then
            sh.event_wait_timer(3276, false) -- 0.8 s
        elseif step == 7 then
            sh.event_wait_timer(2048, false) -- 0.5 s
        else
            sh.event_wait_timer(4096, false) -- 1.0 s
        end
    elseif step == 6 then
        sh.event_wait_timer(3276, false) -- 0.8 s
    elseif step == 8 then
        sh.event_wait_timer(2048, false) -- 0.5 s
    elseif step == 10 then
        sh.event_wait_timer(4096, false) -- 1.0 s
    elseif step == 11 then
        sh.event_msg(false, "map3_s05.no_the_fire_goes_out", 0, 0, 0, false) -- "No. The fire goes out right away..."
    elseif step == 12 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")
    else
        sh.player_freeze(false)
        sh.state_set(0)
        local _, py, _ = sh.player_pos()
        sh.player_set_pos(84377, py, -81920) -- 20.6, -20.0 m
        sh.player_rotate(1024)
        sh.event_inv_item_cmd(3, "Lighter", 1, false)
        sh.chara_field_set(0, "collision_state", 3) -- collides as an NPC
        sh.event_fade_cmd(0, false, 2, 0, false)
        sh.camera_auto_return(true) -- back to the normal camera
    end
end

-- index 6: spray the vines with disinfectant (steps 0-7): fade, warp,
-- animation 141, a series of random hisses (sounds 1536/1531) with random pauses,
-- line 25, flag M3S05_AlcoholPouredOnVines at the end. Steps 0->1, 2->3 and 6->7 run in the same tick.
function MapEvent_VinesDisinfectantUse()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_fade_cmd(0, true, 2, 0, false)
        g_vinesAcc = -1228 -- -0.3
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
        return
    end
    if step == 2 then
        local _, py, _ = sh.player_pos()
        sh.player_set_pos(83148, py, -81920) -- 20.3, -20.0 m
        sh.player_rotate(1024)
        sh.camera_pos(79872 / 4096, -4464 / 4096, -81920 / 4096, 0, 0, 0, 0, 0, 0, 0, true) -- (19.5, -1.09, -20.0)
        sh.camera_look(96215 / 4096, -3481 / 4096, -80855 / 4096, 0, 0, 0, 0, 0, 0, 0, true) -- (23.49, -0.85, -19.74)
        sh.event_chara_anim(0, 0, "throw_alcohol", false) -- animation 141
        sh.event_step_increment(0)
        step = 3
    end
    if step == 3 then
        sh.event_wait_timer(4300, false) -- 1.05 s
    elseif step == 4 then
        local dt = sh.delta_time_get()
        local t = sh.state_timer_get() + dt
        sh.state_timer_set(t)
        if t > 15564 then -- 3.8
            sh.event_step_increment(0)
        elseif t < 13107 then -- 3.2
            if g_vinesAcc > 0 then
                g_vinesAcc = g_vinesAcc - dt
                if g_vinesAcc < 0 then
                    -- next pause: 0.3 s plus a random 0..0.1 s (kept as a 16-bit value)
                    g_vinesAcc = g_vinesAcc - 1228 - sh.rng_int(0, 408)
                    sh.sfx_flags_play("vines_splash", 88473, 0, -81920, 128, 0) -- (21.6, 0, -20.0) m, 0.5
                end
            else
                g_vinesAcc = g_vinesAcc + dt
                if g_vinesAcc >= 0 then
                    g_vinesAcc = g_vinesAcc + 204 + sh.rng_int(0, 203) -- 0.05 s plus a random 0..0.05 s
                    sh.sfx_flags_play("vines_sizzle", 88473, 0, -81920, 128, 0)
                end
            end
        end
    elseif step == 5 then
        sh.event_chara_anim(2, 0, 0, false) -- AnimLock
        sh.event_msg(false, "map3_s05.poured_disinfecting_alcohol_on", 0, 0, 0, false) -- "Poured Disinfecting alcohol on the vines."
    elseif step == 6 or step == 7 then
        if step == 6 then
            sh.event_chara_anim(3, 0, 0, false) -- AnimUnlock
            sh.event_step_increment(0)
        end
        sh.event_chara_anim(1, 0, 0, false) -- AwaitAnimEnd
    else
        sh.player_freeze(false)
        sh.state_set(0)
        sh.flag_set("M3S05_AlcoholPouredOnVines", true) -- flag: alcohol poured on vines
        sh.event_fade_cmd(0, false, 2, 0, false)
        sh.camera_auto_return(true)
    end
end

-- index 7: "the vines soaked up the alcohol" (steps 0-5): line 26 + animations.
function MapEvent_VinesAbsorbedAlcohol()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1                             -- continues into step 1 in the same tick
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.chara_anim_play_to_end(0, "pickup_crouch")
    elseif step == 3 then
        sh.event_msg(false, "map3_s05.the_vines_absorbed_the_alcohol", 0, 0, 0, false) -- "The vines absorbed the alcohol."
    elseif step == 4 then
        sh.event_wait_timer(1228, false)     -- 0.3 s
    elseif step == 5 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")
    elseif step > 5 then
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 8: set fire to the vines at the grate (fire/smoke, sounds, water; map effects 7/9).
-- Map state (sh.map.get/set): vines_burn_level (effect parameter), vines_fire_fade,
-- vines_flame_sfx_pos (sound position), particle_emitter.* (the fire particle volume) and
-- effect_slot_type[450]; sh.map.particle_emitter_start() starts the emitter.


local function VinesFlameSfx()
    local x = sh.map.get("vines_flame_sfx_pos.x")
    local y = sh.map.get("vines_flame_sfx_pos.y")
    local z = sh.map.get("vines_flame_sfx_pos.z")
    sh.sfx_flags_play("flame_ambient", x, y, z, 128, 0) -- sound flame_ambient, 0.5
end

local function VinesEffectAdd(rate)
    local v = sh.map.get("vines_burn_level") + (sh.delta_time_get() * rate) // 4096
    sh.map.set("vines_burn_level", v)
end

function Cutscene_BurnVines()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.map.set("vines_burn_level", 0)
        sh.map_effects_update(7, 9, 5, "vines_burn_level", 0, 0x64000) -- 32-bit primitives
    elseif step == 5 or step == 7 or step == 9 then
        VinesEffectAdd(20480) -- 5.0
    elseif step >= 11 and step <= 13 then
        VinesEffectAdd(40960) -- 10.0
    elseif step == 14 or step == 15 then
        VinesEffectAdd(-81920) -- -20.0
    end

    local s = sh.event_step_get(0)
    if s == 0 then
        sh.player_freeze(true)
        sh.event_fade_cmd(0, 1, 2, 0, 0) -- Start, out, type 2
        sh.event_step_increment(0)
        s = 1
    end
    if s == 1 then
        sh.event_wait_player_stop()
    elseif s == 2 or s == 3 then
        if s == 2 then
            local _, py, _ = sh.player_pos()
            sh.player_set_pos(84582, py, -81920) -- 20.65, -20.0 m
            sh.player_rotate(1024) -- 90 deg
            sh.chara_field_set(0, "collision_state", 0) -- no collision
            sh.chara_field_set(0, "collision_state", 5) -- collision state 5
            sh.camera_pos(70246 / 4096, -5406 / 4096, -76636 / 4096, 0, 0, 0, 0, 0, 0, 0, true)
            sh.camera_look(85852 / 4096, -4628 / 4096, -81633 / 4096, 0, 0, 0, 0, 0, 0, 0, true)
            sh.held_item_hide()
            sh.player_weapon_stash()
            sh.event_step_increment(0)
        end
        sh.chara_anim_play_to_end(0, "pickup_crouch")
    elseif s >= 4 and s <= 9 then
        -- 4 -> 5, 6 -> 7, 8 -> 9: sound + wait
        if s == 4 or s == 6 or s == 8 then
            VinesFlameSfx()
            sh.event_step_increment(0)
            s = s + 1
        end
        if s == 5 then
            sh.event_wait_timer(3276, 0) -- 0.8 s
        elseif s == 7 then
            sh.event_wait_timer(2048, 0) -- 0.5 s
        else
            sh.event_wait_timer(2662, 0) -- 0.65 s
        end
    elseif s == 10 or s == 11 then
        if s == 10 then
            sh.map.set("particle_emitter.state", 3)
            sh.map.set("particle_emitter.flags", 0x18)
            sh.map.set("particle_emitter.spawn_count", 0x12C)
            sh.map.set("particle_emitter.top_y", -819)    -- -0.2 m
            sh.map.set("particle_emitter.bottom_y", 204)    -- 0.05 m
            sh.map.set("particle_emitter.half_size_x", 2457)   -- 0.6 m
            sh.map.set("particle_emitter.half_size_z", 2457)   -- 0.6 m
            sh.map.set("particle_emitter.unread_d", -4096)   -- -1.0
            sh.map.set("particle_emitter.center_x", 88473)   -- 21.6 m
            sh.map.set("particle_emitter.center_z", -81920)  -- -20.0 m
            sh.map.set("particle_emitter.spread", 0x20)
            sh.map.set("particle_emitter.sway_divisor", 0x40)
            sh.map.set("particle_emitter.unread_c", 0x2D)
            sh.map.set("particle_emitter.unread_b", 0x20)
            sh.map.set("particle_emitter.drift_speed", 0x100)
            sh.map.set("particle_emitter.spin_speed", 4096)    -- 1.0
            sh.map.set("particle_emitter.unread_a", 8)
            sh.map.particle_emitter_start()
            sh.sd_call("fire_roar")
            sh.water_effect_enable()
            sh.event_step_increment(0)
        end
        sh.chara_anim_play_to_end(0, "pickup_stand_up")
    elseif s == 12 then
        sh.chara_anim_play_to_end(0, "burn_vines")
        sh.camera_pos(96706 / 4096, -11386 / 4096, -76554 / 4096, 0, 0, 0, 0, 0, 0, 0, true)
        sh.camera_look(84541 / 4096, -2744 / 4096, -83271 / 4096, 0, 0, 0, 0, 0, 0, 0, true)
    elseif s == 13 then
        sh.event_wait_timer(12288, 0) -- 3.0 s
    elseif s == 14 then
        sh.map.set("particle_emitter.state", 1)
        sh.map.set("particle_emitter.unread_a", 4)
        sh.event_wait_timer(2048, 0)
    elseif s == 15 then
        sh.event_fade_cmd(2, 1, 0, 0, 0) -- Auto, out
    elseif s == 16 or s == 17 then
        if s == 16 then
            sh.sfx_stop("fire_roar")
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(4096, 0) -- 1.0 s
    elseif s == 18 or s == 19 then
        if s == 18 then
            sh.sfx_flags_play("fire_burst", 0x15999, -1228, -81920, 128, 0) -- sound fire_burst at the grate
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(2048, 0)
    else
        sh.player_freeze(false, true)
        sh.state_set(0)
        for i = 0, 449 do
            local a = sh.map.get("effect_slot_type", i)
            if a == 8 or a == 11 then
                sh.map.set("effect_slot_type", 0, i)
            end
        end
        sh.event_fade_cmd(0, 0, 0, 0, 0)
        sh.flag_set("M3S05_VinesBurned", true)
        sh.map_effects_init(7, 4)
        sh.water_effect_disable()
        sh.camera_auto_return(true)
        local px, py, pz = sh.player_pos()
        sh.player_set_pos(px - 4915, py, pz) -- 1.2 m
        sh.event_fade_cmd(0, 0, 2, 0, 0)
        sh.chara_field_set(0, "collision_state", 0) -- no collision
        sh.chara_field_set(0, "collision_state", 3) -- collides as an NPC
        sh.held_item_show()
        sh.player_weapon_restore()
    end

    local st = sh.event_step_get(0)
    if st > 10 and st < 16 then
        local vol = 0x80 - (sh.map.get("vines_fire_fade") * 0x80) // 4096
        sh.sfx_falloff_pitch_play("fire_roar", 0x15999, -2048, -81920, vol, 65536, 0) -- fire sound at the grate, 16.0
    end
end

-- index 9: item pickup.
function MapEvent_VideoTapeTake()
    sh.event_item_take("VideoTape", 1, "M3S05_PickupVideoTape", "map3_s05.there_is_a_video_tape")
end

-- index 10: Alessa vision: background picture ALESSA.TIM (1795)
-- with a fade and line 16 ("Alessa"). Steps 0 -> 1 run in the same tick.
function MapEvent_AlessaVisionShow()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_bg_texture_fade_in("TIM/ALESSA.TIM", 0, 0)
    elseif step == 2 then
        sh.event_bg_texture_cmd(2, 0, false) -- Draw
        sh.event_msg(false, "map3_s05.alessa", 0, 0, 0, false)
    elseif step == 3 then
        sh.event_bg_texture_fade_out(0, 0)
    else
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 11: item pickup.
function MapEvent_ExaminationRoomKeyTake()
    sh.event_item_take("ExaminationRoomKey", 1, "M3S05_PickupExaminationRoomKey", "map3_s05.there_is_an_examination_room")
end

-- index 12: "What is that?.. Alessa?!" cutscene (steps 0-16):
-- skip -> step 15; cutscene border, camera, flashlight, waypoints (animation 53),
-- voiced lines 18/19 (alessa_voice_cmds is map state; the voice index is read only here,
-- so it is a Lua local), 20/16/17, the ALESSA.TIM vision; flag M3S05_AlessaCutsceneDone at the end.
-- Steps 0->1, 4->5, 6->7, 11->12 and 16->finish run in the same tick.
local alessaAudioIdx = 0

local function AlessaVoice(msgIdx)
    alessaAudioIdx = sh.event_msg_audio(msgIdx, alessaAudioIdx, "alessa_voice_cmds")
end

function Cutscene_AlessaConfrontation()
    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step > 0 and step < 11 then
        StepSet0(15)
        step = 15
    end
    if step == 0 then
        sh.player_freeze(true)
        sh.cutscene_border(20) -- force the letterbox borders
        sh.camera_pos(74137 / 4096, -7004 / 4096, -398704 / 4096, 0, 0, 0, 0, 0, 0, 0, true) -- (18.1, -1.71, -97.34)
        sh.camera_look(82083 / 4096, -3031 / 4096, -412467 / 4096, 0, 0, 0, 0, 0, 0, 0, true) -- (20.04, -0.74, -100.7)
        alessaAudioIdx = 0
        sh.held_item_hide()
        sh.player_weapon_stash()
        sh.flashlight(true)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        AlessaVoice("map3_s05.whats_this") -- "What's this?"
    elseif step == 2 then
        sh.chara_anim_play_to_end(0, "scene_alessa_confrontation")
    elseif step == 3 then
        AlessaVoice("map3_s05.looks_like_someones_been_here") -- "Looks like someone's been here..."
    elseif step == 4 or step == 5 then
        if step == 4 then
            sh.event_waypoint_set(true, 0, 0, 0, 88883, -413286) -- 21.7, -100.9
            sh.event_waypoint_set(true, 0, 1, 0, 89702, -410828) -- 21.9, -100.3
            sh.event_waypoint_set(true, 0, 2, 0, 86016, -407142) -- 21.0, -99.4
            sh.event_step_increment(0)
        end
        sh.event_waypoint_player("walk_forward", 3, false)
        sh.event_wait_timer(8192, false) -- 2.0 s
    elseif step == 6 or step == 7 then
        if step == 6 then
            sh.camera_pos(75530 / 4096, -18595 / 4096, -413614 / 4096, 0, 0, 0, 0, 0, 0, 0, true) -- (18.44, -4.54, -100.98)
            sh.camera_look(84131 / 4096, -6471 / 4096, -406732 / 4096, 0, 0, 0, 0, 0, 0, 0, true) -- (20.54, -1.58, -99.3)
            sh.event_step_increment(0)
        end
        sh.event_waypoint_player("walk_forward", 3, false)
    elseif step == 8 then
        sh.event_msg(false, "map3_s05.msg20", 0, 0, 0, false)
    elseif step == 9 then
        sh.event_bg_texture_fade_in("TIM/ALESSA.TIM", 0, 0)
    elseif step == 10 then
        sh.event_bg_texture_cmd(2, 0, false) -- Draw
        sh.event_msg(false, "map3_s05.alessa", 0, 0, 0, false)
    elseif step == 11 or step == 12 then
        if step == 11 then
            sh.camera_auto_return(true) -- back to the normal camera
            sh.event_step_increment(0)
        end
        sh.event_bg_texture_fade_out(0, 0)
    elseif step == 13 then
        sh.event_msg(false, "map3_s05.alessa_2", 0, 0, 0, false) -- "Alessa...?"
    elseif step == 15 then
        sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, type 0
    else
        if step == 16 then
            sh.camera_auto_return(true)
            local _, py, _ = sh.player_pos()
            sh.player_set_pos(86016, py, -407142) -- 21.0, -99.4 m
            sh.player_rotate(0)
            sh.event_fade_cmd(0, false, 0, 0, false) -- Start, in, type 0
        end
        -- finish
        sh.player_freeze(false)
        sh.state_set(0)
        sh.held_item_show()
        sh.player_weapon_restore()
        sh.event_fade_cmd(0, false, 2, 0, false) -- Start, in, type 2
        sh.flag_set("M3S05_AlessaCutsceneDone", true)
    end
end

-- index 13: push the cabinet (TANA_HID world object, map variable cabinet = the Cabinet object of
-- world_objects.json) together with the player.
-- The scraping volume is read only by this handler, so it is a Lua local.
local cabinetVol = 0

local function CabinetScrapeSfx()
    local wx, wy, wz = sh.map.get("cabinet.pos.x"), sh.map.get("cabinet.pos.y"), sh.map.get("cabinet.pos.z")
    sh.sfx_falloff_pitch_play("cabinet_scrape", wx, wy, wz, cabinetVol >> 5, 65536, 0) -- sound cabinet_scrape, 16.0
end

local function CabinetPushStep()
    local distAdd = (sh.delta_time_get() * 942) // 4096 -- dt * 0.23 (the player's default collision radius)
    local px, py, pz = sh.player_pos()
    sh.player_set_pos(px + distAdd, py, pz)
    local wx = sh.map.get("cabinet.pos.x")
    sh.map.set("cabinet.pos.x", wx + distAdd)
    return wx + distAdd
end

function MapEvent_CabinetPush()
    local s = sh.event_step_get(0)
    if s == 0 then
        sh.player_freeze(true)
        cabinetVol = 0
        sh.event_step_increment(0)
        s = 1
    end
    if s == 1 then
        sh.event_wait_player_stop()
    elseif s == 2 then
        sh.event_msg(true, "map3_s05.do_you_want_to_push", 4, 3, 0, false) -- "Do you want to push the cabinet?"
    elseif s == 3 then
        sh.player_freeze(false, false)
        sh.state_set(0)
    elseif s >= 4 and s <= 6 then
        if s == 4 then
            sh.event_step_increment(0)
        end
        if s <= 5 then
            sh.camera_pos(236994 / 4096, -14827 / 4096, -86425 / 4096, 0, 0, 0, 0, 0, 0, 0, true) -- 57.86, -3.62, -21.1 m
            sh.camera_look(239083 / 4096, -1638 / 4096, -95928 / 4096, 0, 0, 0, 0, 0, 0, 0, true) -- 58.37, -0.4, -23.42 m
            sh.event_fade_cmd(0, 1, 2, 0, 0) -- Start, out, type 2
            sh.event_chara_anim(0, 0, "push_object", false) -- SetState
            local _, py, _ = sh.player_pos()
            sh.player_set_pos(234086, py, -93388) -- 57.15, -22.8 m
            sh.player_rotate(1024) -- 90.0 deg
            sh.sd_call("cabinet_scrape")
            sh.event_step_increment(0)
        end
        -- step 6
        local wx = CabinetPushStep()
        if wx > 249856 then -- 61.0
            sh.event_step_increment(0)
        end
        cabinetVol = cabinetVol + (sh.delta_time_get() & 0xFFFF) * 2
        if cabinetVol > 4096 then
            cabinetVol = 4096
        end
        CabinetScrapeSfx()
    elseif s == 7 then
        local wx = CabinetPushStep()
        if wx > 250880 then -- 61.25
            local over = wx - 250880
            local px, py, pz = sh.player_pos()
            sh.player_set_pos(px - over, py, pz)
            sh.map.set("cabinet.pos.x", sh.map.get("cabinet.pos.x") - over)
        end
        cabinetVol = cabinetVol - (sh.delta_time_get() & 0xFFFF) * 2
        if cabinetVol < 0 then
            cabinetVol = 0
        end
        CabinetScrapeSfx()
        sh.event_fade_cmd(2, 1, 0, 0, 0) -- Auto, out
        if sh.event_step_get(0) ~= 7 then
            sh.flag_set("M3S05_CabinetPushed", true) -- flag: cabinet pushed
        end
    else
        sh.player_freeze(false, true)
        sh.state_set(0)
        sh.event_fade_cmd(0, 0, 2, 0, 0)
        sh.camera_auto_return(true)
        sh.event_fade_cmd(0, 0, 0, 0, 0)
        sh.sfx_stop("cabinet_scrape")
    end
end

-- index 14: item pickup.
function MapEvent_HammerTake()
    sh.event_item_take("Hammer", 1, "M3S05_PickupHammer", "map3_s05.there_is_a_hammer_take")
end

-- index 15: "the generator is running": line 29 + controller rumble
-- (pad_effect(9, 96) every tick).
function MapEvent_GeneratorOn()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1 -- continues into step 1 in the same tick
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.event_msg(false, "map3_s05.the_generator_is_on", 0, 0, 0, false) -- "The generator is on."
    else
        sh.player_freeze(false)
        sh.state_set(0)
    end
    sh.pad_effect(9, 96) -- rumble preset 9, level 0x60
end

-- index 16: hospital elevator button panel (this map's variant).
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
