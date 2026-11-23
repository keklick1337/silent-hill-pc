-- map6_s02: Lakeside Pier and Lighthouse
-- Event scripts of this area: the engine runs a handler every tick while its event is active.
-- Fixed-point numbers: 4096 = 1.0 (metres, seconds); angles: 4096 = 360 degrees.

-- Runs one step of a handler's step table: steps[step], or steps.default for any other step.
local function runStep(steps, step, ...)
    return (steps[step] or steps.default)(...)
end

function on_boot()
    -- per-scenario script entry point
end

-- index 0: jammed door.
function MapEvent_DoorJammed()
    sh.event_msg_with_sfx("common.lock_jammed", "door_jammed")
end

-- index 1: locked door.
function MapEvent_DoorLocked()
    sh.event_msg_with_sfx("common.locked", "door_locked")
end

-- index 2: no action.
function MapEvent_Nop()
end

-- Helpers of the cutscenes below.
local function cmod(a, b) -- remainder that truncates toward zero
    local q = a // b
    if q < 0 and q * b ~= a then q = q + 1 end
    return a - q * b
end

local function camera_warp(px, py, pz, lx, ly, lz)
    sh.camera.position(nil, px, py, pz, 0, 0, 0, 0, true)
    sh.camera.look_at(nil, lx, ly, lz, 0, 0, 0, 0, true)
end

local lighthouseAlessaSteps = {}

lighthouseAlessaSteps[0] = function()
    sh.player_freeze(true)
    sh.cutscene.border_show()
    sh.screen_fade_reset_timestep()
    sh.map.set("alessa_msg_audio_idx", 0) -- voice line index (kept across map reloads)
    sh.held_item_hide()
    sh.player.weapon_stash()
    sh.flag_set("M6S02_LighthouseStairFarEndReached", false)
    sh.event_step_increment(0)
    camera_warp(233840, -13230, -83599, 246046, -3850, -78069) -- 57.09, -3.23, -20.41 / (60.07, -0.94, -19.06)
    sh.flag_set("M6S02_CameraCutThisFrame", true)
    -- 0.9 m from (60, -20) at 135 degrees
    sh.npc.set_pos_x(0, (sh.math_sin(1536) * 3686) // 4096 + 60 * 4096)
    sh.npc.set_pos_y(0, 6717) -- 1.64 m
    sh.npc.set_pos_z(0, (sh.math_cos(1536) * 3686) // 4096 - 20 * 4096)
    sh.npc.set_rot_y(0, 512) -- 45.0 deg
    sh.map.set("alessa_scene_active", 1)
end

lighthouseAlessaSteps[1] = function()
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_chara_anim(0, 0, "walk_forward", false)
    sh.npc.load(0, "Alessa", "npc_bone:0", 255, nil, nil) -- Alessa, free all character slots
    sh.event_step_increment(0)
    return lighthouseAlessaSteps[2]()
end

lighthouseAlessaSteps[2] = function()
    local timer = sh.event.state_timer() + sh.delta_time_get()
    sh.event.set_state_timer(timer)
    local a = timer // 4
    sh.npc.set_pos_x(0, (sh.math_sin(1536 - a) * 3686) // 4096 + 60 * 4096)
    sh.npc.set_pos_z(0, (sh.math_cos(1536 - a) * 3686) // 4096 - 20 * 4096)
    sh.npc.set_rot_y(0, 512 - a)
    if a > 1600 then -- 140.7 deg
        sh.event_step_increment(0)
    end
end

lighthouseAlessaSteps[3] = function()
    sh.event_chara_anim(0, 0, "reset", false)
    sh.event_step_increment(0)
    return lighthouseAlessaSteps[4]()
end

lighthouseAlessaSteps[4] = function()
    sh.msg.show(false, "map6_s02.msg15", 0, 0, 0, false)
end

lighthouseAlessaSteps[5] = function()
    sh.event_chara_anim(0, 0, "scene_lighthouse_1", false)
    sh.event_step_increment(0)
    return lighthouseAlessaSteps[6]()
end

lighthouseAlessaSteps[6] = function()
    sh.event_wait_timer(6144, false) -- 1.5 s
end

lighthouseAlessaSteps[7] = function()
    sh.npc.process_loads()
    sh.npc.spawn("Alessa", 0, 60 * 4096, -94208, 0, 3) -- 60.0, -23.0
    sh.event_step_increment(0)
end

lighthouseAlessaSteps[8] = function()
    camera_warp(232611, -32358, -111820, 237404, -20602, -101457) -- (56.79, -7.9, -27.3) / (57.96, -5.03, -24.77)
    sh.npc.set_pos_x(0, 244121) -- 59.6 m
    sh.npc.set_pos_z(0, -19 * 4096)
    sh.npc.set_rot_y(0, 2048) -- 180.0 deg
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_chara_anim(0, 1, "stand", false)
    sh.flag_set("M6S02_CameraCutThisFrame", true)
    sh.map.set("alessa_scene_active", 0)
    sh.event_step_increment(0)
    return lighthouseAlessaSteps[9]()
end

lighthouseAlessaSteps[9] = function()
    sh.event_wait_timer(2 * 4096, false)
end

lighthouseAlessaSteps[10] = function()
    camera_warp(235192, -10158, -67911, 242606, -5079, -81633) -- (57.42, -2.48, -16.58) / (59.23, -1.24, -19.93)
    sh.npc.set_pos_x(0, 59 * 4096)
    sh.npc.set_pos_z(0, -79872) -- -19.5 m
    sh.npc.set_rot_y(0, sh.math.atan2(sh.npc.pos_x(1) - sh.npc.pos_x(0), sh.npc.pos_z(1) - sh.npc.pos_z(0)))
    sh.npc.transparent_set(34, true)
    sh.flag_set("M6S02_CameraCutThisFrame", true)
    sh.event_step_increment(0)
    return lighthouseAlessaSteps[11]()
end

lighthouseAlessaSteps[11] = function()
    sh.event_wait_timer(2048, false) -- 0.5 s
end

lighthouseAlessaSteps[12] = function()
    sh.npc.set_aux_timer(1, sh.npc.aux_timer(1) + (sh.delta_time_get() * 1024) // 4096)
    sh.event_wait_timer(1228, false) -- 0.3 s
    sh.msg.show_with_audio("map6_s02.wait", "alessa_msg_audio_idx", "alessa_msg_voice_cmds[0]")
end

lighthouseAlessaSteps[13] = function()
    sh.event_chara_anim(0, 0, "scene_descend", false)
    sh.event_step_increment(0)
    return lighthouseAlessaSteps[14]()
end

lighthouseAlessaSteps[14] = function()
    sh.msg.show_with_audio("map6_s02.wait", "alessa_msg_audio_idx", "alessa_msg_voice_cmds[0]")
    sh.npc.set_aux_timer(1, sh.npc.aux_timer(1) + (sh.delta_time_get() * 1024) // 4096)
    if sh.npc.aux_timer(1) > 4096 then
        sh.npc.set_aux_timer(1, 4096)
    end
end

lighthouseAlessaSteps[15] = function()
    sh.npc.set_aux_timer(1, sh.npc.aux_timer(1) + (sh.delta_time_get() * 1024) // 4096)
    if sh.npc.aux_timer(1) > 4096 then
        sh.npc.set_aux_timer(1, 4096)
        sh.event_step_increment(0)
    end
end

lighthouseAlessaSteps[16] = function()
    sh.npc.clear(1, 0, 0)
    sh.event_step_increment(0)
    return lighthouseAlessaSteps[17]()
end

lighthouseAlessaSteps[17] = function()
    sh.event_chara_anim(1, 0, 0, false) -- wait for the animation to end
end

lighthouseAlessaSteps[18] = function()
    camera_warp(265707, -22118, -60702, 255139, -13762, -70000) -- (64.87, -5.4, -14.82) / (62.29, -3.36, -17.09)
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.held_item_show()
    sh.player.weapon_restore()
    sh.flag_set("M6S02_CameraCutThisFrame", true)
    sh.event_step_increment(0)
    return lighthouseAlessaSteps[19]()
end

lighthouseAlessaSteps[19] = function()
    sh.msg.show_with_audio("map6_s02.damn_i_was_too_late", "alessa_msg_audio_idx", "alessa_msg_voice_cmds[0]")
    if sh.event_step_get(0) ~= 19 then
        sh.event_step_reset()
    end
end

lighthouseAlessaSteps[20] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

lighthouseAlessaSteps[21] = function(hasSkippedEarly)
    sh.load.wait()
    sh.camera_auto_return(true)
    if not hasSkippedEarly then
        sh.held_item_show()
        sh.player.weapon_restore()
    end
    sh.npc.set_pos_x(0, 59 * 4096)
    sh.npc.set_pos_z(0, -79872)
    sh.npc.set_rot_y(0, 1867) -- 164.1 deg
    sh.npc.clear(1, 0, 0)
    sh.player_freeze(false, true)
    sh.state_set(0)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_step_increment(0)
    return lighthouseAlessaSteps.default()
end

lighthouseAlessaSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.flag_set("M6S02_LighthouseAlessaCutsceneDone", true)
    sh.flag_set("M6S02_LighthouseRoomStairEntered", false)
    sh.flag_set("MapMark_ResortTown_LighthouseCheck", true) -- map mark: resort town lighthouse check
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.camera_auto_return(false)
    sh.map.set("alessa_scene_active", 0)
end

-- index 3: lighthouse: Alessa vanishes in front of Harry.
function Cutscene_LighthouseAlessa()
    sh.flag_set("M6S02_CameraCutThisFrame", false)

    local hasSkippedEarly = false
    local s = sh.event_step_get(0)
    if sh.input_action("skip", 1) and s >= 2 and s <= 19 then
        hasSkippedEarly = s < 19
        sh.event.step_set(0, 20)
        s = 20
    end

    runStep(lighthouseAlessaSteps, s, hasSkippedEarly)
end

-- index 4: Channeling Stone: UFO5.DMS, the UFO hum (sound ufo_hum),
-- fifteen lights ufo_light_pos[0..14].
local function ufo5_lights(speed)
    for i = 0, 14 do
        sh.map.ufo_glow_spawn("ufo_light_pos[" .. i .. "]", 2048, speed, 0) -- 0.5
    end
end

local channelingStoneUfoSteps = {}

channelingStoneUfoSteps[0] = function()
    sh.player_freeze(true)
    sh.sfx_stop("lighthouse_hum")
    if sh.flag_get("M1S03_ChannelingStoneUsed") and sh.flag_get("M4S05_ChannelingStoneUsed") and sh.flag_get("M5S01_ChannelingStoneUsed") and sh.flag_get("M6S01_ChannelingStoneUsed") then
        sh.flag_set("M6S02_UfoEndingTriggered", true)
    end
    sh.map.set("ufo_msg_audio_idx", 0)
    sh.load.tim("TIM/ENBAN.TIM", 1, "ufo_glow_tex") -- ENBAN.TIM
    sh.load.file("ANIM/UFO5.DMS", 13) -- UFO5.DMS
    sh.map.set("cutscene_timer", -1) -- cutscene not started
    sh.screen_fade_reset_timestep()
    sh.sd_call("ufo_hum")
    sh.sfx_attributes_update("ufo_hum", 0, 255, 0)
    sh.map.set("ufo_hum_volume", 0)
    sh.map.set("ufo_hum_clock", 0)
    sh.flag_set("M6S02_ChannelingStoneUfoStarted", true)
    sh.map.set("ufo_lights_clock", 0)
    sh.event_step_increment(0)
    return channelingStoneUfoSteps[1]()
end

channelingStoneUfoSteps[1] = function()
    sh.event.wait_player_stop()
end

channelingStoneUfoSteps[2] = function()
    if sh.load.chunks() ~= 0 then
        sh.event_step_increment(0)
    end
end

channelingStoneUfoSteps[3] = function()
    sh.map.set("cutscene_timer", 0)
    sh.event_fade_cmd(0, true, 3, 0, false)
    sh.cutscene.prepare(13)
    sh.held_item_hide()
    sh.player.weapon_stash()
    sh.light.flashlight_on()
    sh.event_step_increment(0)
    return channelingStoneUfoSteps[4]()
end

channelingStoneUfoSteps[4] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
    return channelingStoneUfoSteps[5]()
end

channelingStoneUfoSteps[5] = function()
    sh.msg.show_with_audio("map6_s02.what", "ufo_msg_audio_idx", "ufo_msg_voice_cmds[0]")
end

channelingStoneUfoSteps[6] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40 * 4096, 0, 30 * 4096, true, true)
    ufo5_lights(204) -- 0.05
end

channelingStoneUfoSteps[7] = function()
    if sh.flag_get("M6S02_UfoEndingTriggered") then
        sh.load.tim("TIM/UFO1.TIM", 1, "ufo_ending_images[0]") -- UFO1.TIM -> [0]
        sh.load.tim("TIM/UFO6.TIM", 1, "ufo_ending_images[1]") -- UFO6.TIM -> [1]
    end
    sh.event_step_increment(0)
    return channelingStoneUfoSteps[8]()
end

channelingStoneUfoSteps[8] = function()
    sh.msg.show(false, "map6_s02.that_light", 0, 0, 0, false)
    ufo5_lights(204)
end

channelingStoneUfoSteps[9] = function()
    sh.vec.set(0, 22077, -19 * 4096, -66928) -- 5.39, -19.0, -16.34
    sh.vec.short_set(0, 0, 0, 0)
    sh.camera.look_at_bone("player_bone:2", "svec:0", "vec:0") -- Harry's head
    ufo5_lights(614) -- 0.15
    sh.event_wait_timer(10240, false) -- 2.5 s
    if sh.event_step_get(0) ~= 9 then
        sh.flag_set("M6S02_CameraCutThisFrame", true)
    end
end

channelingStoneUfoSteps[10] = function()
    if sh.map.get("ufo_lights_clock") > 8 * 4096 then
        if sh.flag_get("M6S02_UfoEndingTriggered") then
            sh.event.step_set(0, 12)
        else
            sh.event.step_set(0, 11)
        end
    end
end

channelingStoneUfoSteps[11] = function()
    sh.event_wait_timer(10240, false)
    if sh.event_step_get(0) ~= 11 then
        sh.event_step_reset()
    end
end

channelingStoneUfoSteps[12] = function()
    sh.event_wait_timer(4 * 4096, false)
    if sh.event_step_get(0) ~= 12 then
        sh.event_fade_cmd(0, false, 2, 0, false)
    end
end

channelingStoneUfoSteps[13] = function()
    sh.event_fade_cmd(2, true, 1, 4096, false)
end

channelingStoneUfoSteps.default = function()
    sh.camera_auto_return(true)
    sh.flag_set("M6S02_ChannelingStoneUsed", true)
    sh.event.inv_item(3, "ChannelingStone", 1, false) -- add the Channeling Stone
    sh.held_item_show()
    sh.player.weapon_restore()
    sh.map.set("cutscene_timer", -1)
    if sh.flag_get("M6S02_UfoEndingTriggered") then
        sh.state_set(0)
        sh.map.set("map_effect_flags", sh.map.get("map_effect_flags") & ~(1 << 5))
    else
        sh.event_fade_cmd(0, false, 2, 0, false)
        sh.player_freeze(false, false)
        sh.state_set(0)
        sh.sd_call("lighthouse_hum")
        sh.sfx_stop("ufo_hum")
    end
end

function Cutscene_ChannelingStoneUfo()
    local curTimerVal = sh.map.get("ufo_hum_clock")
    local pitch = sh.math_sin(curTimerVal // 8) * 2 + sh.math_cos(curTimerVal // 16) +
                  sh.math_sin(sh.math_cos(curTimerVal // 16))
    local dt = sh.delta_time_get()
    sh.map.set("ufo_hum_clock", curTimerVal + dt + dt)
    local pitchDiv512 = pitch // 512

    local step = sh.event_step_get(0)
    if step == 11 or step == 13 then
        sh.map.set("ufo_hum_volume", sh.map.get("ufo_hum_volume") - (dt * 2048) // 4096) -- fades at 0.5 per second
        if sh.map.get("ufo_hum_volume") < 0 then
            sh.map.set("ufo_hum_volume", 0)
        end
        sh.sfx_attributes_update("ufo_hum", 0, ~(sh.map.get("ufo_hum_volume") // 16), pitchDiv512)
    elseif step > 0 then
        sh.map.set("ufo_hum_volume", sh.map.get("ufo_hum_volume") + (dt * 1024) // 4096) -- rises at 0.25 per second
        if sh.map.get("ufo_hum_volume") > 4080 then
            sh.map.set("ufo_hum_volume", 4080)
        end
        sh.sfx_attributes_update("ufo_hum", 0, ~(sh.map.get("ufo_hum_volume") // 16), pitchDiv512)
    end

    local s = step
    runStep(channelingStoneUfoSteps, s)
    do
        local step_now = sh.event_step_get(0)
        if step_now == 10 or step_now == 12 or step_now == 13 then
            local t = sh.map.get("ufo_lights_clock") + sh.delta_time_get()
            sh.map.set("ufo_lights_clock", t)
            local d = sh.delta_time_get()
            for i = 0, 14 do
                if sh.flag_get("M6S02_UfoEndingTriggered") or t < sh.map.get("ufo_light_lifetime", i) then
                    sh.map.ufo_glow_spawn("ufo_light_pos[" .. i .. "]", 2048, 204, 0)
                    if t > 6144 then -- after 1.5 s the lights fly off at (3.5, 1.2, -0.2) m/s
                        sh.map.set("ufo_light_pos.x", sh.map.get("ufo_light_pos.x", i) + (d * 14336) // 4096, i)
                        sh.map.set("ufo_light_pos.y", sh.map.get("ufo_light_pos.y", i) + (d * 4915) // 4096, i)
                        sh.map.set("ufo_light_pos.z", sh.map.get("ufo_light_pos.z", i) + (d * -819) // 4096, i)
                    end
                end
            end
        end

        local timer = sh.map.get("cutscene_timer")
        if timer >= 0 then
            sh.cutscene.chara(0, "HERO", timer, 13)
            sh.cutscene.camera(timer, 13, "camera_target_pos", "camera_target_look")
        end
    end
end

-- index 5: UFO ending: images UFO2-7, voice streams 565/562, credits
-- (sh.screen.credits_start/credits_update drive the credits).
local function ufo_jitter_roll()
    if sh.map.get("ending_jitter_x") == 0 and sh.map.get("ending_jitter_y") == 0 then
        if sh.rng.rand16() % 0x100 == 0 then
            sh.map.set("ending_jitter_x", sh.rng_uint(-8, 7))
            sh.map.set("ending_jitter_y", sh.rng_uint(-8, 7))
        end
    elseif sh.rng.rand16() % 0x20 == 0 then
        sh.map.set("ending_jitter_x", 0)
        sh.map.set("ending_jitter_y", 0)
    end
end

local function ufo_blink_draw()
    local t = sh.map.get("ending_phase_timer") + sh.delta_time_get()
    sh.map.set("ending_phase_timer", t)
    -- blink: the second image shows during the second half of every second
    local trunc = (t // 4096) * 4096
    if t < 0 and trunc ~= t then trunc = trunc + 4096 end
    if t - trunc > 2048 then
        sh.screen.bg_draw_alt("ufo_ending_images[1]")
    else
        sh.screen.bg_draw_alt("ufo_ending_images[0]")
    end
end

local ufoEndingSteps = {}

ufoEndingSteps[0] = function()
    sh.map.set("credits_rolling", 0)
    sh.sound.bgm_crossfade(41) -- music track 41
end

ufoEndingSteps[1] = function()
    sh.sfx_stop("ufo_hum")
    sh.load.wait()
    sh.npc.anim_info_clear()
    sh.load.tim("TIM/UFO2.TIM", 1, "ufo_ending_images[2]") -- UFO2.TIM -> [2]
    sh.load.tim("TIM/UFO3.TIM", 1, "ufo_ending_images[3]") -- UFO3.TIM -> [3]
    sh.load.tim("TIM/UFO7.TIM", 1, "ufo_ending_images[4]") -- UFO7.TIM -> [4]
    sh.event_fade_cmd(0, false, 1, 2048, false) -- 0.5
    sh.map.set("ending_sfx_volume", -1) -- not playing
    sh.map.set("ending_jitter_x", 0)
    sh.map.set("ending_jitter_y", 0)
    sh.map.set("ufo_sprite_flicker_timer", 0)
    sh.sd_call("ufo_ending_hum_loop")
    sh.map.set("ending_phase_timer", 0)
    sh.event_step_increment(0)
    return ufoEndingSteps[2]()
end

ufoEndingSteps[2] = function()
    sh.event_wait_timer(5 * 4096, false)
    ufo_blink_draw()
    sh.map.set("ending_sfx_volume", math.max(0, sh.map.get("ending_sfx_volume") - 3))
    sh.sfx_attributes_update("ufo_ending_hum_loop", 0, sh.map.get("ending_sfx_volume"), 0)
end

ufoEndingSteps[3] = function()
    sh.event_fade_cmd(2, true, 0, 2048, false)
    ufo_blink_draw()
    sh.map.set("ending_sfx_volume", math.min(sh.map.get("ending_sfx_volume") + 3, 255))
    sh.sfx_attributes_update("ufo_ending_hum_loop", 0, sh.map.get("ending_sfx_volume"), 0)
end

ufoEndingSteps[4] = function()
    sh.load.tim("TIM/UFO4.TIM", 1, "ufo_ending_images[0]") -- UFO4.TIM -> [0]
    sh.sfx_stop("ufo_ending_hum_loop")
    sh.sd_call("ufo_ending_beam")
    sh.load.wait()
    sh.event_step_increment(0)
    sh.sound.xa_preload("ufo_ending_voice_1") -- xa audio 565 sound
    return ufoEndingSteps[5]()
end

ufoEndingSteps[5] = function()
    sh.event_wait_timer(10240, false) -- 2.5 s
end

ufoEndingSteps[6] = function()
    sh.sd_call("ufo_ending_voice_1")
    sh.event_step_increment(0)
    return ufoEndingSteps[7]()
end

ufoEndingSteps[7] = function()
    if sh.sound.streaming() ~= 1 then
        return
    end
    sh.event_step_increment(0)
    return ufoEndingSteps[8]()
end

ufoEndingSteps[8] = function()
    sh.event_wait_timer(4096, false)
end

ufoEndingSteps[9] = function()
    sh.event_fade_cmd(0, false, 0, 2 * 4096, false)
    sh.flag_set("M6S02_UfoEndingFadeStarted", true)
    sh.event_step_increment(0)
    return ufoEndingSteps[10]()
end

ufoEndingSteps[10] = function()
    if sh.event_step_get(0) == 10 then
        sh.event_wait_timer(26624, false) -- 6.5 s
    else
        sh.event_fade_cmd(2, true, 0, 2 * 4096, false)
    end
    sh.msg.draw("map6_s02.sorry")
    sh.screen.bg_draw_alt("ufo_ending_images[2]")
end

ufoEndingSteps[11] = ufoEndingSteps[10]

ufoEndingSteps[12] = function()
    sh.event_wait_timer(7372, false) -- 1.8 s
end

ufoEndingSteps[13] = function()
    sh.event_fade_cmd(0, false, 0, 2 * 4096, false)
    sh.map.set("ending_phase_timer", 0)
    sh.event_step_increment(0)
    return ufoEndingSteps[14]()
end

ufoEndingSteps[14] = function()
    if sh.event_step_get(0) == 14 then
        sh.event_wait_timer(17203, false) -- 4.2 s
    else
        sh.event_fade_cmd(2, true, 0, 2 * 4096, false)
    end
    sh.screen.bg_draw_alt("ufo_ending_images[3]")
    local t = sh.map.get("ending_phase_timer") + sh.delta_time_get()
    sh.map.set("ending_phase_timer", t)
    if cmod(t, 819) > 409 then -- 0.2, 0.1
        sh.map.ufo_ending_sprite_draw(0)
    else
        sh.map.ufo_ending_sprite_draw(1)
    end
    sh.pad.effect(10)
end

ufoEndingSteps[15] = ufoEndingSteps[14]

ufoEndingSteps[16] = function()
    if sh.sound.streaming() ~= 0 then
        return
    end
    sh.event_step_increment(0)
    return ufoEndingSteps[17]()
end

ufoEndingSteps[17] = function()
    sh.load.tim("TIM/UFO5.TIM", 1, "ufo_ending_images[1]") -- UFO5.TIM -> [1]
    sh.load.wait()
    sh.load.staff_roll()
    sh.load.wait()
    sh.sd_call("ufo_ending_voice_2") -- xa audio 562 sound
    sh.event_step_increment(0)
    return ufoEndingSteps[18]()
end

ufoEndingSteps[18] = function()
    if sh.sound.streaming() ~= 1 then
        return
    end
    sh.event_step_increment(0)
    return ufoEndingSteps[19]()
end

ufoEndingSteps[19] = function()
    sh.event_fade_cmd(0, false, 0, 2 * 4096, false)
    sh.event_step_increment(0)
    return ufoEndingSteps[20]()
end

ufoEndingSteps[20] = function()
    if sh.event_step_get(0) == 20 then
        sh.event_wait_timer(4 * 4096, false)
    else
        sh.event_fade_cmd(2, true, 0, 2 * 4096, false)
    end
    sh.screen.bg_draw_alt("ufo_ending_images[0]")
end

ufoEndingSteps[21] = ufoEndingSteps[20]

ufoEndingSteps[22] = function()
    sh.event_wait_timer(15564, false) -- 3.8 s
end

ufoEndingSteps[23] = function()
    sh.event_fade_cmd(0, false, 0, 2 * 4096, false)
    sh.map.set("ending_phase_timer", 0)
    sh.event_step_increment(0)
    return ufoEndingSteps[24]()
end

ufoEndingSteps[24] = function()
    sh.screen.bg_draw_alt("ufo_ending_images[1]")
    local t = sh.map.get("ending_phase_timer")
    local sec = t // 4096 -- whole seconds
    if sec >= 0 and sec <= 3 then
        sh.map.ufo_ending_slide_in_draw(t // 4)
    elseif sec == 4 or sec == 5 or sec == 6 then
        if sec == 6 then
            sh.flag_set("M6S02_UfoEndingImageShown", true)
        end
        sh.map.ufo_ending_sprite_draw(2)
    else
        if not (sec >= 7 and sec <= 13) then -- after 13 s the credits start, then the same image as 7..13
            sh.screen.credits_start()
            sh.map.set("credits_rolling", 1)
            sh.map.set("ending_phase_timer", 0)
            sh.event_step_increment(0)
        end
        sh.map.ufo_ending_sprite_draw(3)
    end
    sh.map.set("ending_phase_timer", sh.map.get("ending_phase_timer") + sh.delta_time_get())
    ufo_jitter_roll()
end

ufoEndingSteps.default = function()
    sh.screen.bg_draw_alt("ufo_ending_images[1]")
    local v = sh.map.get("ufo_sprite_flicker_timer")
    if v >= 0 then
        sh.map.set("ufo_sprite_flicker_timer", v - sh.delta_time_get())
        if sh.map.get("ufo_sprite_flicker_timer") < 0 then
            sh.map.set("ufo_sprite_flicker_timer", cmod(-sh.rng.rand16(), 1638)) -- 0.4 s
        else
            sh.map.ufo_ending_sprite_draw(3)
        end
    else
        sh.map.set("ufo_sprite_flicker_timer", v + sh.delta_time_get())
        if sh.map.get("ufo_sprite_flicker_timer") >= 0 then
            sh.map.set("ufo_sprite_flicker_timer", sh.rng_int(4096, 13926 - 1)) -- 1.0, 3.4 - 1
        else
            sh.map.ufo_ending_sprite_draw(2)
        end
    end
    ufo_jitter_roll()
end

function Cutscene_UfoEnding()
    sh.sound.set_bgm_flags(sh.sound.bgm_flags() | 1) -- pause the music

    local credits_shown = 0
    local s = sh.event_step_get(0)
    runStep(ufoEndingSteps, s)
    sh.map.set("credits_vblank_clock", sh.map.get("credits_vblank_clock") + sh.game.vblanks())

    if sh.map.get("credits_rolling") == 1 then
        credits_shown = sh.screen.credits_update() -- credits roll, bool
        if credits_shown ~= 0 then
            sh.map.ufo_ending_sprite_draw(4)
            ufo_jitter_roll()
        end
    end

    if sh.event_step_get(0) >= 25 then
        if sh.sound.streaming() ~= 0 or credits_shown == 0 then
            sh.map.set("ending_phase_timer", 0)
            return
        end
        local fade = sh.screen.fade_status()
        if fade == 0 or fade == 1 then -- no fade running
            sh.map.set("ending_phase_timer", sh.map.get("ending_phase_timer") + sh.delta_time_get())
            if sh.map.get("ending_phase_timer") >= 4 * 4096 then
                sh.screen.fade_start(true, false, false)
            end
        elseif fade == 5 then -- faded out
            sh.flag_set("EndingCreditsDone", true)
            sh.state_set(0)
        end
    end
end

-- index 6: after the UFO ending: clear count, Next Fear.
function Cutscene_UfoEndingClear()
    sh.screen_fade_status_set(5) -- keep the screen faded out

    local s = sh.event_step_get(0)
    if s == 0 then
        sh.player_freeze(true)
        sh.event_chara_anim(2, 0, 0, false) -- lock the animation
        sh.event_step_set(0, s + 1) -- next step without resetting the step timer
    elseif s == 1 then
        sh.sound.bgm_crossfade(5) -- music track 5
    elseif s == 2 then
        local n = (sh.save.clear_game_count() + 1) & 0xFF
        sh.save.set_clear_game_count(n)
        sh.save.set_clear_game_count(math.min(math.max(n, 1), 99))
        sh.save.set_current_ending_flags(1 << 4)
        sh.save.set_clear_game_endings(sh.save.clear_game_endings() | (1 << 4))
        sh.game.set_extra_options_enabled(sh.game.extra_options_enabled() | (1 << 4))
        sh.save.set_location_id(24) -- save location: Next Fear
        sh.state_set(2) -- open the status menu
    end
end
