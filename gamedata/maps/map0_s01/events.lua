-- map0_s01: Old Silent Hill - cafe
-- Event scripts of this area: the engine runs a handler every tick while its event is active.

function on_boot()
end

-- index 2: common pickups, chosen by the event's point of interest.
function MapEvent_CommonItemTake()
    local poi = sh.event_point()
    local t = {
        [13] = {"HealthDrink","M0S01_HealthDrink0"},
        [14] = {"HealthDrink","M0S01_HealthDrink1"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point gives a first aid kit with flag 0
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- index 3: cafe cutscene with Cybil (CAFE.DMS in file buffer 11): Harry wakes up, Cybil
-- questions him and hands over the handgun; skippable on steps 6-46 (to step 52). The Air
-- Screamer model is loaded for the next scene, M0S01_CafeCutsceneDone at the end.
local CafeDmsBuf = 11
local cafeCybilShown = 0   -- an overlay u8 in the original: Cybil follows the DMS while set
local cafeCybilStandUp = 0 -- an overlay u8 in the original: 0, 1 stand_up started, 2 stand_ready

local function CafeTimer(rate, startTime, endTime, clampToEnd, incStep)
    sh.cutscene.timer_advance("cutscene_timer", rate, startTime, endTime, clampToEnd, incStep)
end

local function CafeMsg(key)
    sh.msg.play_with_audio(key, "msg_audio_idx", "msg_audio_cmds")
end

function MapEvent_CafeCutscene()
    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step > 5 and step < 47 then
        sh.event.step_set(0, 52)
        step = 52
    end

    if step == 0 or step == 1 then
        if step == 0 then
            sh.player_freeze(true)
            sh.cutscene_border(20)
            sh.screen_fade_reset_timestep()
            sh.sys_flags_set(sh.sys_flags_get() | 8) -- SysFlag_CutsceneActive
            sh.savegame_set("item_toggle_flags", sh.savegame_get("item_toggle_flags") & ~1) -- radio off
            sh.sys_flags_set(sh.sys_flags_get() | 0x220) -- SysFlag_5 | SysFlag_9
            sh.map.set("msg_audio_idx", 0)
            cafeCybilShown = 0
            cafeCybilStandUp = 0
            sh.load.file("ANIM/CAFE.DMS", CafeDmsBuf)
            sh.load.wait()
            sh.cutscene.prepare(CafeDmsBuf)
            sh.chara_load(0, "Cybil", 255) -- force free all
            sh.chara_process_loads()
            sh.chara_spawn("Cybil", 0, 18022, 1105510, 0, 2) -- (4.4, 269.9)
            sh.map.set("cutscene_timer", 0)
            sh.event_step_increment(0)
        end
        sh.sound.bgm_crossfade(9) -- music track 9
    elseif step == 2 then
        sh.event_chara_anim(0, 0, "cafe_wake_lie", false)
        sh.event_step_increment_after_fade(0, false, 0, 0, false)
        sh.map.player_weapon_hide()
        sh.event_step_increment(0)
    elseif step == 3 or step == 4 then
        if step == 3 then
            sh.event_chara_anim(2, 0, 0, false) -- lock
            sh.event_step_increment(0)
        end
        sh.event_step_increment_after_fade(1, false, 0, 0, false)
    elseif step == 5 or step == 6 then
        if step == 5 then
            sh.event_chara_anim(3, 0, 0, false) -- unlock
            sh.event_step_increment(0)
        end
        CafeTimer(31948, 0, 20480, false, true) -- 7.8/s, 0 .. 5.0
    elseif step == 7 then
        CafeTimer(31948, 0, 106496, true, false) -- 0 .. 26.0
        CafeMsg("map0_s01.was_i_dreaming") -- "Was I dreaming?"
    elseif step == 8 then
        CafeTimer(31948, 0, 106496, true, true)
    elseif step >= 9 and step <= 13 then
        if step == 9 then
            sh.event_chara_anim(0, 0, "cafe_sit_up", false)
            cafeCybilShown = 1
            sh.event_step_increment(0)
        end
        local s = sh.event_step_get(0)
        if s == 10 then
            CafeMsg("map0_s01.how_do_you_feel") -- "How do you feel?"
        elseif s == 11 then
            CafeMsg("map0_s01.like_ive_been_run_over") -- "Like I've been run over by a truck..."
        elseif s == 12 then
            if sh.map.get("cutscene_timer") == 196608 and cafeCybilStandUp == 2 then -- 48.0
                sh.event.step_set(0, 13)
            end
        elseif s == 13 then
            CafeMsg("map0_s01.glad_to_hear_it") -- "Glad to hear it."
        end

        CafeTimer(16384, 110592, 196608, true, false) -- 4.0/s, 27.0 .. 48.0

        if cafeCybilStandUp == 0 and sh.map.get("cutscene_timer") > 155648 then -- 38.0
            sh.event_chara_anim(0, 1, "stand_up", false)
            sh.event_chara_anim(0, 0, "cafe_sit_forward", false)
            cafeCybilStandUp = cafeCybilStandUp + 1
        end
        local stage = cafeCybilStandUp
        -- Once stand_up has played to its end (playback state 1) Cybil keeps standing ready.
        if cafeCybilStandUp == 1 and sh.npc.anim_playback_state(1) == stage then
            sh.event_chara_anim(0, 1, "stand_ready", false)
            cafeCybilStandUp = cafeCybilStandUp + 1
        end
    elseif step == 14 then
        CafeTimer(12288, 200704, 569344, true, false) -- 3.0/s, 49.0 .. 139.0
        CafeMsg("map0_s01.have_you_seen_a_little") -- "Have you seen a little girl?"
    elseif step == 15 or step == 16 then
        if step == 15 then
            sh.map.set("cutscene_timer", 573440) -- 140.0
            sh.event_step_increment(0)
        end
        CafeMsg("map0_s01.whats_your_name") -- "What's your name?"
    elseif step == 17 then
        sh.event_step_increment_delayed(4915, false) -- 1.2 s
    elseif step == 18 or step == 19 then
        if step == 18 then
            sh.event_chara_anim(0, 0, "cafe_lean_in", false)
            sh.event_step_increment(0)
        end
        CafeTimer(40960, 577536, 622592, false, true) -- 10.0/s, 141.0 .. 152.0
    elseif step == 20 or step == 21 then
        if step == 20 then
            sh.event_chara_anim(0, 0, "walk_forward", false)
            sh.event_step_increment(0)
        end
        CafeTimer(40960, 577536, 651264, true, true) -- 141.0 .. 159.0
        sh.xa_preload("intro_cafe_voice")
    elseif step == 22 or step == 23 then
        if step == 22 then
            sh.event_chara_anim(0, 1, "talk_pose", false)
            sh.sd_call("intro_cafe_voice")
            sh.event_step_increment(0)
        end
        CafeTimer(40960, 655360, 708608, true, false) -- 160.0 .. 173.0
        sh.event_selection(false, "map0_s01.hold_it_where_do_you", 0, 0, 0, false) -- "Hold it! Where do you think you're going?"
        sh.screen.debug_text_position(30, 30)
    elseif step == 24 or step == 25 then
        if step == 24 then
            sh.event_chara_anim(0, 0, "scene_lighthouse_1", false)
            sh.event_step_increment(0)
        end
        CafeTimer(40960, 712704, 753664, true, false) -- 174.0 .. 184.0
        CafeMsg("map0_s01.my_daughter_ive_gotta_find") -- "My daughter... I've gotta find her!"
    elseif step == 26 or step == 27 then
        if step == 26 then
            sh.event_chara_anim(0, 0, "cutscene_stand", false)
            sh.event_chara_anim(0, 1, "talk_gesture", false)
            sh.event_step_increment(0)
        end
        CafeTimer(40960, 757760, 794624, true, false) -- 185.0 .. 194.0
        CafeMsg("map0_s01.no_way") -- "No way!"
    elseif step == 28 or step == 29 then
        if step == 28 then
            sh.map.set("cutscene_timer", 798720) -- 195.0
            sh.event_chara_anim(0, 1, "stand_talk", false)
            sh.event_step_increment(0)
        end
        CafeMsg("map0_s01.its_dangerous_out_there") -- "It's dangerous out there."
    elseif step == 30 then
        sh.event_step_increment_delayed(6144, false) -- 1.5 s
    elseif step == 31 then
        CafeMsg("map0_s01.have_you_got_a_gun") -- "Have you got a gun?"
    elseif step == 32 or step == 33 then
        if step == 32 then
            sh.event_chara_anim(0, 1, "walk", false)
            sh.event_step_increment(0)
        end
        CafeTimer(81920, 847872, 909312, true, true) -- 20.0/s, 207.0 .. 222.0
    elseif step == 34 or step == 35 then
        if step == 34 then
            sh.event_chara_anim(0, 1, "hand_over_gun", false)
            sh.event_step_increment(0)
        end
        CafeTimer(20480, 913408, 1028096, true, false) -- 5.0/s, 223.0 .. 251.0
        CafeMsg("map0_s01.take_this_and_hope_you") -- "Take this. And hope you don't have to use it."
    elseif step == 36 then
        CafeTimer(20480, 913408, 1028096, true, false)
        sh.event_step_increment_delayed(2457, false) -- 0.6 s
    elseif step == 37 then
        CafeTimer(20480, 913408, 1028096, true, false)
        CafeMsg("map0_s01.and_dont_go_blasting_me") -- "And don't go blasting me by mistake."
    elseif step == 38 or step == 39 then
        if step == 38 then
            sh.event_chara_anim(0, 1, "stand_with_gun", false)
            sh.event_chara_anim(0, 0, "cafe_react", false)
            sh.event_step_increment(0)
        end
        CafeTimer(40960, 1032192, 1146880, true, false) -- 10.0/s, 252.0 .. 280.0
        CafeMsg("map0_s01.yeah_thanks") -- "Yeah. Thanks."
    elseif step == 40 then
        CafeTimer(40960, 1032192, 1146880, true, true)
    elseif step == 41 or step == 42 then
        if step == 41 then
            sh.event_chara_anim(0, 1, "stand_talk", false)
            sh.event_step_increment(0)
        end
        CafeTimer(40960, 1150976, 1212416, true, true) -- 281.0 .. 296.0
    elseif step == 43 or step == 44 then
        if step == 43 then
            sh.event_chara_anim(0, 0, "cutscene_stand", false)
            sh.map.set("cutscene_timer", 1216512) -- 297.0
            sh.event_step_increment(0)
        end
        CafeMsg("map0_s01.youd_do_best_to_stay") -- "You'd do best to stay nearby..."
    elseif step == 45 or step == 46 then
        if step == 45 then
            sh.event_chara_anim(0, 0, "cafe_watch_leave", false)
            sh.event_chara_anim(0, 1, "walk", false)
            sh.event_step_increment(0)
        end
        CafeTimer(40960, 1220608, 1261568, true, true) -- 298.0 .. 308.0
    elseif step >= 47 and step <= 49 then
        if step == 47 then
            cafeCybilShown = 0
            sh.sfx_flags_play("cafe_door_cybil_leaves", 3276, -4915, 1103872, 128, 0) -- (0.8, -1.2, 269.5), 0.5
            sh.event_step_increment(0)
            step = 48
        end
        if step == 48 then
            sh.chara_model_clear(1)
            sh.sd_call("stop_xa_audio")
            sh.chara_load(0, "AirScreamer", 255) -- force free all
            sh.event_inv_item_cmd(3, "Handgun", 15, false) -- AddItem, 15 bullets
            sh.event_step_increment(0)
        end
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
    elseif step == 50 then
        sh.map.set("cutscene_timer", 1261568) -- 308.0
        sh.event_step_increment_delayed(2048, false) -- 0.5 s
    elseif step == 51 then
        sh.event.step_reset()
    elseif step == 52 then -- skipped
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
    elseif step == 53 then
        sh.event.step_set(0, 48)
    else
        sh.player_freeze(false)
        sh.state_set(0) -- gameplay
        sh.camera_auto_return(true)
        sh.event_step_increment_after_fade(0, false, 2, 0, false)
        sh.event_step_increment_after_fade(0, false, 0, 0, false)
        sh.sd_call("bgm_fade_2")
        sh.flag_set("M0S01_CafeCutsceneDone", true)
        sh.map.set("cutscene_timer", -1)
    end

    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.camera(t, CafeDmsBuf, "camera_target_pos", "camera_target_look")
        sh.cutscene.chara(0, "HERO", t, CafeDmsBuf)
        if cafeCybilShown ~= 0 then
            sh.npc.anim_flags_set(1, 2) -- Visible
            sh.cutscene.chara(1, "SIBYL", t, CafeDmsBuf)
            return
        end
        sh.npc.anim_flags_clear(1, 2)
    end
end

-- index 4: "To be continued" (unused demo event): CONTINUE.TIM, then the "coming soon"
-- picture, each held 4 s or until enter/cancel; then a warm boot with the music paused.
-- The C switch falls through after every step increment; `step` keeps the case being run.
local function ClickedEnterOrCancel()
    return sh.input_action("enter", 1) or sh.input_action("cancel", 1)
end

function MapEvent_ToBeContinued()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_step_increment_after_fade(0, true, 0, 0, false)
        sh.overlay_screen_set(0, "TIM/CONTINUE.TIM", false)
        sh.event_step_increment(0)
        step = 2
    end
    if step == 2 then
        sh.event_step_increment_after_fade(1, true, 0, 0, false)
        return
    elseif step == 3 then
        sh.overlay_screen_set(1, "1ST/2ZANKO80.TIM", false)
        return
    end
    if step == 4 then
        sh.event_step_increment_after_fade(0, false, 0, 8192, false) -- 2.0 s
        sh.event_step_increment(0)
        step = 5
    end
    if step == 5 then
        sh.overlay_screen_set(2, "1ST/2ZANKO80.TIM", false)
        if ClickedEnterOrCancel() then
            sh.event_step_increment(0)
        end
        sh.event_step_increment_delayed(16384, false) -- 4.0 s
        if sh.event_step_get(0) == 7 then
            sh.event_step_set(0, 6)
        end
        return
    elseif step == 7 then
        sh.overlay_screen_set(7, "TIM/COMESOON.TIM", false)
        return
    end
    if step == 8 then
        sh.event_step_increment_after_fade(0, false, 0, 6144, false) -- 1.5 s
        sh.sfx_stop("radio_interference_loop")
        sh.sfx_stop("radio_static_loop")
        sh.event_step_increment(0)
        step = 9
    end
    if step == 9 then
        sh.overlay_screen_set(2, "1ST/2ZANKO80.TIM", false)
        if ClickedEnterOrCancel() then
            sh.event_step_increment(0)
        end
        sh.event_step_increment_delayed(16384, false) -- 4.0 s
        if sh.event_step_get(0) == 11 then
            sh.event_step_set(0, 10)
        end
    elseif step == 6 or step == 10 then
        sh.overlay_screen_set(2, "1ST/2ZANKO80.TIM", false)
        sh.event_step_increment_after_fade(2, true, 0, 6144, false) -- 1.5 s
    else
        sh.player_freeze(false, false)
        sh.state_set(0)
        sh.game_warm_boot()
        sh.bgm_status_flags_set(sh.bgm_status_flags_get() | 1) -- BgmStatusFlag_Pause
    end
end

-- index 5: the Air Screamer intro (CAFE.DMS in file buffer 11, frames 26..154): the radio
-- crackles, Harry picks it up, the creature crashes through the cafe window and Harry falls
-- back as the fight starts. The radio static grows louder over the scene (the map's world
-- update plays the loops at radio_static_volume). Flags 50/52, 45, 46, 47.
local SYSFLAG_5_AND_9 = 0x220 -- the cafe scene's system flags (bits 5 and 9)

-- The timer runs at `rate` frames per second up to `endTime`, then the step advances.
-- The bird (chara slot 1): a clip change restarts the clip at its first keyframe; steps 1, 3 and 5
-- of its attack move on to the next step.
local function AirScreamerClipRestart(status, stateStep)
    sh.npc.set_anim_status(1, status)
    local k = sh.npc.anim_start_keyframe(1)
    sh.npc.set_state_step(1, stateStep)
    sh.npc.set_anim_keyframe(1, k)
    sh.npc.set_anim_time(1, k * 4096)
end

local function AirScreamerAttackStep()
    local s = sh.npc.state_step(1)
    if s == 1 or s == 3 or s == 5 then
        sh.npc.set_state_step(1, s + 1)
    end
end

local function AirScreamerIntroTimer(rate, endTime)
    local t = sh.map.get("cutscene_timer") + (sh.delta_time_get() * rate) // 4096
    t = math.min(t, endTime)
    sh.map.set("cutscene_timer", t)
    if t >= endTime then
        sh.event_step_increment(0)
    end
end

local function AirScreamerIntroMsg(key)
    sh.msg.play_with_audio(key, "msg_audio_idx", "radio_voice_cmds")
end

-- Window shards: the pane's two corners (Q12 world points) and the burst's parameters.
local function AirScreamerGlassShatter(fromX, fromY, fromZ, toX, toY, toZ, a, b, c, d, e)
    sh.vec.set(0, fromX, fromY, fromZ)
    sh.vec.set(1, toX, toY, toZ)
    sh.map.glass_shatter("vec:0", "vec:1", a, b, c, d, e, 0)
end

function MapEvent_AirScreamerIntroCutscene()
    local step = sh.event_step_get(0)
    if step == 0 or step == 1 then
        if step == 0 then
            sh.player_freeze(true)
            sh.game.state_update()

            sh.flag_set("M0S01_AirScreamerEncounterActive", true)

            sh.sd_call("radio_interference_loop")
            sh.sd_call("radio_static_loop")

            sh.flag_set("M0S01_RadioStaticPlaying", true)

            sh.npc.set_pos_x(0, 5324)    -- 1.3
            sh.npc.set_pos_z(0, 1104691) -- 269.7
            sh.npc.set_rot_y(0, 56)      -- 5 deg

            sh.map.set("cutscene_timer", 106496) -- 26.0

            sh.event_step_increment_after_fade(0, true, 3, 0, false)

            sh.map.set("msg_audio_idx", 0)
            sh.event_step_increment(0)
        end
        sh.sound.bgm_crossfade(30) -- music track 30
    elseif step == 2 then
        sh.event_wait_player_stop()
    elseif step == 3 or step == 4 then
        if step == 3 then
            sh.event_chara_anim(0, 0, "scene_pose_d", false)
            sh.event_step_increment(0)
        end
        AirScreamerIntroTimer(61440, 151552) -- 15.0, .. 37.0
    elseif step == 5 then
        AirScreamerIntroMsg("map0_s01.whats_that") -- "What's that?"
    elseif step == 6 then
        sh.event_step_increment_delayed(2048, false) -- 0.5 s
    elseif step == 7 then
        AirScreamerIntroMsg("map0_s01.huh_radio_whats_going_on") -- "Huh? Radio?"
    elseif step == 8 or step == 9 then
        if step == 8 then
            sh.event_chara_anim(0, 0, "scene_pose_e", false)
            sh.event_step_increment(0)
        end
        AirScreamerIntroTimer(61440, 212992) -- .. 52.0
    elseif step == 10 then
        sh.map.set("cutscene_timer", math.max(sh.map.get("cutscene_timer"), 217088)) -- 53.0
        sh.map.set("radio_static_volume", 128)
        sh.flag_set("M0S01_AirScreamerIntroCutsceneStarted", true)

        sh.event_step_increment_delayed(6144, false) -- 1.5 s
    elseif step == 11 or step == 12 then
        if step == 11 then
            sh.event_chara_anim(0, 0, "walk_forward", false)
            sh.event_step_increment(0)
        end
        AirScreamerIntroTimer(61440, 307200) -- .. 75.0
    elseif step >= 13 and step <= 20 then
        if step == 13 then
            sh.event_chara_anim(0, 0, "scene_pose_c", false)

            sh.map.set("radio_static_volume", 224)
            sh.flag_set("M0S01_AirScreamerIntroOutsideShotDone", true)
            sh.map.set("cutscene_timer", 311296) -- 76.0

            sh.event_step_increment(0)
            step = 14
        end
        if step == 14 then
            AirScreamerIntroTimer(73728, 471040) -- 18.0, .. 115.0
        elseif step == 15 or step == 16 then
            if step == 15 then
                -- (0.8, -1.4, 267.7) .. (0.3, -1.4, 267.7); 2.5, 0.05, 0.6, 0.4, 0.1
                AirScreamerGlassShatter(3276, -5734, 1096499, 1228, -5734, 1096499, 10240, 204, 2457, 1638, 409)
                sh.sfx_3d_at("cafe_first_window_break", 3276, -7372, 1096499, 128) -- (0.8, -1.8, 267.7), 0.5
                sh.event_step_increment(0)
            end
            AirScreamerIntroTimer(73728, 598016) -- .. 146.0
        elseif step == 17 or step == 18 then
            if step == 17 then
                -- (0.8, -1.4, 273.9) .. (0.3, -1.4, 273.9); 3.0, 0.08, 0.4, 0.4, 0.058
                AirScreamerGlassShatter(3276, -5734, 1121894, 1228, -5734, 1121894, 12288, 327, 1638, 1638, 237)
                sh.sfx_3d_at("cafe_second_window_break", 3276, -7372, 1081753, 128) -- (0.8, -1.8, 264.1), 0.5
                sh.sfx_3d_at("cafe_second_window_break_layer", 3276, -7372, 1081753, 128)
                sh.event_step_increment(0)
            end
            AirScreamerIntroTimer(73728, 602112) -- .. 147.0
        else
            if step == 19 then
                AirScreamerAttackStep()
                sh.event_step_increment(0)
            end
            AirScreamerIntroTimer(73728, 630784) -- .. 154.0
        end
    else
        if step == 21 then
            sh.flag_set("M0S01_AirScreamerFightStarted", true)
            AirScreamerClipRestart(39, 7) -- HoverVariable, active
            sh.event_step_increment_after_fade(0, false, 2, 0, false)

            sh.map.set("cutscene_timer", -1) -- NO_VALUE

            sh.camera_auto_return(true)
        end
        sh.player_freeze(false, false)
        sh.state_set(0) -- gameplay
        sh.map.player_fall_backward()

        sh.sys_flags_set(sh.sys_flags_get() & ~SYSFLAG_5_AND_9)
    end

    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, CafeDmsBuf)

        if sh.event_step_get(0) >= 20 then
            sh.cutscene.chara(1, "BIRD", t, CafeDmsBuf)
            -- The bird 0.2 m ahead of its DMS track along its heading.
            local rotY = sh.npc.rot_y(1)
            sh.npc.set_pos_x(1, sh.npc.pos_x(1) + (sh.math.sin(rotY) * 819) // 4096)
            sh.npc.set_pos_z(1, sh.npc.pos_z(1) + (sh.math.cos(rotY) * 819) // 4096)
        end

        sh.cutscene.camera(t, CafeDmsBuf, "camera_target_pos", "camera_target_look")
    end
end

-- index 6: item pickup.
function MapEvent_KitchenKnifeItemTake()
    sh.event_item_take("KitchenKnife", 1, "M0S01_PickupKitchenKnife", "map0_s01.there_is_a_kitchen_knife")
end

-- index 7: item pickup, then the flashlight is switched off.
function MapEvent_FlashlightItemTake()
    sh.event_item_take("Flashlight", 1, "M0S01_PickupFlashlight", "map0_s01.there_is_a_flashlight_take")
    sh.flashlight(false)
end

-- index 8: the town map on the cafe counter. Its own form of the paper map pickup: the
-- picture covers the screen with the "Take it?" choice; on yes the page is found, CAFE2.DMS
-- is read and the Air Screamer is placed on its BIRD track for the fly-by that follows
-- (the map's world update plays it), with the camera on the DMS camera at time 0.
-- The C switch falls through after every step increment; `step` keeps the case being run.
local PaperMapScreenRect = {320, 256, 160, 240} -- VRAM piece the picture screen overwrites
local Cafe2DmsBuf = 11

local function PaperMapTexFile()
    return sh.file_number("TIM/MP_0TOWN.TIM") + sh.map.get("paper_map_tex_offset")
end

local function PaperMapDraw()
    sh.background_gamma(88) -- 11/32
    sh.render.background{texture = "paper_map"}
end

local function PaperMapScreenClose()
    sh.render.vram_restore{rect = PaperMapScreenRect, buffer = "screen_backup"}
    sh.render.display_init{width = 320, interlaced = false}
    sh.render.picture_screen(false)
    sh.event_step_increment_after_fade(0, false, 0, 0, false)
end

function MapEvent_MapItemTake()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.fs_seek(PaperMapTexFile())
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
        return
    end
    if step == 2 then
        sh.render.vram_save{rect = PaperMapScreenRect, buffer = "screen_backup"}
        sh.render.texture_load{file = PaperMapTexFile(), texture = "paper_map", buffer = "paper_map"}
        sh.render.display_init{width = 320, interlaced = true}
        sh.render.picture_screen(true)
        sh.render.vblank_interval(1)

        sh.render.display_swap()
        sh.event_step_increment_after_fade(0, false, 0, 0, false)
        sh.load.wait()
        sh.event_step_increment(0)
        step = 3
    end
    if step == 3 then
        PaperMapDraw()
        sh.event_selection(true, "map0_s01.there_is_a_residential_area", 6, 4, 0, false) -- yes 6, no 4
        return
    end
    if step == 5 then
        PaperMapScreenClose()
        sh.event.step_reset()
        return
    end
    if step == 6 then
        sh.paper_map_grant("OldTown")
        sh.load.file("ANIM/CAFE2.DMS", Cafe2DmsBuf)
        sh.event_step_increment(0)
        step = 7
    end
    if step == 4 or step == 7 then
        PaperMapDraw()
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
        return
    end
    if step == 8 then
        PaperMapScreenClose()
        sh.event_step_increment(0)
        step = 9
    end
    if step == 9 then
        sh.chara_process_loads()
        sh.event_step_increment(0)
        step = 10
    end
    if step == 10 then
        -- The cutscene's bird in NPC slot 0 (chara slot 1).
        sh.chara_spawn("AirScreamer", 0, 0, 0, 0, 12)
        sh.cutscene.prepare(Cafe2DmsBuf)
        sh.cutscene.chara(1, "BIRD", 0, Cafe2DmsBuf)

        sh.cutscene.camera(0, Cafe2DmsBuf, "camera_target_pos", "camera_target_look")

        sh.npc.set_pos_x(0, 18784)   -- 4.585938
        sh.npc.set_pos_z(0, 1094800) -- 267.285156
        sh.npc.set_rot_y(0, 1024)    -- 90 deg

        sh.game.set_npc_flags(sh.game.npc_flags() | 1) -- NPC slot 0 in use
        sh.chara_spawn_entry_set(0, true)
        sh.flag_set("M0S01_PickupMap", true)

        sh.event_step_increment(0)
        step = 11
    end
    if step == 11 then
        sh.event_step_increment_after_fade(1, false, 0, 0, false)
        return
    end
    -- step 12 and past
    sh.player_freeze(false, false)
    sh.state_set(0) -- gameplay
end

-- index 9: item pickup.
function MapEvent_PocketRadioItemTake()
    sh.event_item_take("PocketRadio", 1, "M0S01_PickupPocketRadio", "map0_s01.there_is_a_pocket_radio")
    sh.savegame_set("item_toggle_flags", sh.savegame_get("item_toggle_flags") | 1) -- radio on
end

-- index 10: no action.
function MapEvent_EmptyFunction()
end

-- index 11: the Air Screamer attack scream: freeze, stop the radio
-- loops, clear flag M0S01_RadioStaticPlaying, voiced message map0_s01.this_is_not_a_dream "This is not a dream!" (voice command 0x1005),
-- back to gameplay + flag M0S01_AirScreamerDeathPlayed.
-- sh.event_msg_with_audio advances the event step by itself on the first selection entry;
-- when the message finishes (state -1) the next voice command plays. Lua keeps the
-- command list and the audio index and bumps the index on every finish.
local AirScreamerDeathVoiceCmds = { 0x1005 } -- the map's voice command list for message map0_s01.this_is_not_a_dream
local airScreamerDeathAudioIdx = 1           -- current voice line (1-based)

function MapEvent_AirScreamerDeath()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.sfx_stop("radio_interference_loop") -- radio interference loop sound
        sh.sfx_stop("radio_static_loop") -- radio static loop sound
        sh.flag_set("M0S01_RadioStaticPlaying", false)
        airScreamerDeathAudioIdx = 1
        sh.event_step_increment(0)
        step = 1 -- continues into step 1 in the same tick
    end
    if step == 1 then
        local st = sh.event_msg_with_audio("map0_s01.this_is_not_a_dream", AirScreamerDeathVoiceCmds[airScreamerDeathAudioIdx] or 0)
        if st == -1 then -- message finished
            airScreamerDeathAudioIdx = airScreamerDeathAudioIdx + 1
        end
    else
        sh.player_freeze(false)
        sh.state_set(0) -- back to gameplay
        sh.flag_set("M0S01_AirScreamerDeathPlayed", true) -- flag: air screamer death played
    end
end
