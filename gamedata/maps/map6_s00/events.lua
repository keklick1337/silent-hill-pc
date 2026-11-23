-- map6_s00: Resort Area Otherworld
-- Event scripts of this area: the engine runs a handler every tick while its event is active.

-- Runs one step of a handler's step table: steps[step], or steps.default for any other step.
local function runStep(steps, step, ...)
    return (steps[step] or steps.default)(...)
end

function on_boot()
    -- per-scenario script entry point
end

-- index 2: common pickups, chosen by the event's point of interest.
function MapEvent_CommonItemTake()
    local poi = sh.event_point()
    local t = {
        [22] = {"HealthDrink","M6S00_HealthDrink"},
        [23] = {"ShotgunShells","M6S00_ShotgunShells"},
        [24] = {"FirstAidKit","M6S00_FirstAidKit"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point gives a first aid kit and sets no flag
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- index 3: the otherworld spreads over the resort (RSU.DMS in file buffer 15). The ground
-- turns into the otherworld one up to a level that rises through the scene (drawn every
-- tick); while steps 5..12 run, every frame is drawn over the next at half blend (the
-- ghosting). Two drones (darkness_drone, otherworld_spread_drone) swell in on a 16-bit fade
-- and die out at the end; the pad rumbles on steps 5..9. Flags 413/414/415 mark the beats,
-- flag M6S00_OtherworldSpreadsCutsceneDone the end (M6S00_OtherworldSpreadsGroundRising is cleared again).
local RsuDmsBuf = 15
local groundLevel = 0 -- how far the ground has turned, 4096 = 1.0
local droneFade = 0   -- 0..4096

local function RsuMsgAudio(msgKey)
    sh.msg.show_with_audio(msgKey, "rsu_msg_audio_idx", "rsu_voice_cmds")
end

local function RsuTimerAdvance(rate, startTime, endTime, autoAdvance)
    sh.cutscene.timer_advance("cutscene_timer", rate, startTime, endTime, true, autoAdvance)
end

local otherworldSpreadsSteps = {}

otherworldSpreadsSteps[0] = function()
    sh.player_freeze(true)
    sh.screen_fade_reset_timestep()
    sh.cutscene.border_show()
    sh.cutscene_flag(true)
    sh.load.file("ANIM/RSU.DMS", RsuDmsBuf)
    sh.load.wait()
    sh.cutscene.prepare(RsuDmsBuf)
    sh.held_item_hide()
    sh.player.weapon_stash()
    sh.map.set("rsu_msg_audio_idx", 0)
    groundLevel = 0
    sh.map.set("cutscene_timer", 0)
    sh.event_chara_anim(0, 0, "walk_forward", false)
    sh.light.flashlight_on()
    sh.map.weather_set(2, 127)
    sh.map.particles_update(0, sh.savegame_get("map_idx"), 0)
    sh.map.weather_density_set(6)
    sh.sd_call("darkness_drone")
    sh.sd_call("otherworld_spread_drone")
    droneFade = 0
    sh.event_step_increment(0)
    return otherworldSpreadsSteps[1]()
end

otherworldSpreadsSteps[1] = function()
    sh.event_fade_cmd(2, false, 0, 0, false) -- Auto, in
    RsuTimerAdvance(51200, 0, 90112, false) -- 12.5, 0.0 .. 22.0
end

otherworldSpreadsSteps[2] = function()
    RsuTimerAdvance(51200, 0, 90112, true)
end

otherworldSpreadsSteps[3] = function()
    sh.map.set("cutscene_timer", 94208) -- frame 23.0
    sh.event_chara_anim(0, 0, "reset", false)
    sh.event_step_increment(0)
    return otherworldSpreadsSteps[4]()
end

otherworldSpreadsSteps[4] = function()
    RsuMsgAudio("map6_s00.whats_this")
end

otherworldSpreadsSteps[5] = function()
    sh.event_chara_anim(0, 0, "scene_otherworld_resort_a", false)
    sh.flag_set("M6S00_OtherworldSpreadsHarryReacted", true)
    sh.event_step_increment(0)
    return otherworldSpreadsSteps[6]()
end

otherworldSpreadsSteps[6] = function()
    RsuMsgAudio("map6_s00.msg16")
    RsuTimerAdvance(73728, 94208, 176128, false) -- 18.0, 23.0 .. 43.0
end

otherworldSpreadsSteps[7] = function()
    RsuTimerAdvance(73728, 94208, 176128, true)
end

otherworldSpreadsSteps[8] = function()
    sh.event_chara_anim(0, 0, "scene_otherworld_resort_c", false)
    sh.event_step_increment(0)
    return otherworldSpreadsSteps[9]()
end

otherworldSpreadsSteps[9] = function()
    RsuTimerAdvance(17203, 180224, 192512, true) -- 4.2, 44.0 .. 47.0
end

otherworldSpreadsSteps[10] = function()
    sh.event_chara_anim(0, 0, "scene_drain_ambush", false)
    sh.flag_set("M6S00_OtherworldSpreadsCutsceneDone", true)
    sh.map.weather_set(6, 127)
    sh.map.particles_update(0, sh.savegame_get("map_idx"), 0)
    sh.flag_set("M6S00_OtherworldSpreadsGroundRising", true)
    groundLevel = 13926 -- 3.4
    sh.event_step_increment(0)
    return otherworldSpreadsSteps[11]()
end

otherworldSpreadsSteps[11] = function()
    RsuMsgAudio("map6_s00.msg17")
    return otherworldSpreadsSteps[12]()
end

otherworldSpreadsSteps[12] = function()
    RsuTimerAdvance(16384, 196608, 233472, true) -- 4.0, 48.0 .. 57.0
    groundLevel = groundLevel + sh.delta_time_get() -- dt * 1.0
end

otherworldSpreadsSteps[13] = function()
    groundLevel = 21299 -- 5.2
    sh.event_step_increment(0)
    return otherworldSpreadsSteps[14]()
end

otherworldSpreadsSteps[14] = function()
    RsuMsgAudio("map6_s00.msg18")
    return otherworldSpreadsSteps[15]()
end

otherworldSpreadsSteps[15] = function()
    RsuTimerAdvance(16384, 237568, 274432, true) -- 4.0, 58.0 .. 67.0
    groundLevel = groundLevel + (sh.delta_time_get() * 2867) // 4096 -- dt * 0.7
end

otherworldSpreadsSteps[16] = function()
    sh.event_wait_timer(9011, false) -- 2.2 s
    groundLevel = groundLevel + (sh.delta_time_get() * 2867) // 4096
end

otherworldSpreadsSteps[17] = function()
    sh.event_chara_anim(0, 0, "scene_otherworld_resort_b", false)
    sh.flag_set("M6S00_OtherworldSpreadsNotAgainLine", true)
    sh.event_step_increment(0)
    return otherworldSpreadsSteps[18]()
end

otherworldSpreadsSteps[18] = function()
    sh.msg.show(false, "map6_s00.not_again_no_this_time", 0, 0, 0, false)
    RsuTimerAdvance(10240, 278528, 585728, false) -- 2.5, 68.0 .. 143.0
    groundLevel = groundLevel + (sh.delta_time_get() * 1638) // 4096 -- dt * 0.4
    if groundLevel > 65536 then -- 16.0
        groundLevel = 65536
    end
end

otherworldSpreadsSteps[19] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out (the skip)
end

otherworldSpreadsSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0) -- gameplay
    sh.camera.auto_return(true)
    sh.event_fade_cmd(0, false, 2, 0, false) -- Start
    sh.map.set("cutscene_timer", -1) -- not running
    sh.flag_set("M6S00_OtherworldSpreadsCutsceneDone", true)
    sh.flag_set("M6S00_OtherworldSpreadsGroundRising", false)
    sh.sd_call("stop_xa_audio")
    sh.held_item_show()
    sh.player.weapon_restore()
    sh.sfx_stop("darkness_drone")
    sh.sfx_stop("otherworld_spread_drone")
    sh.map.weather_set(6, 127)
    sh.map.particles_update(0, sh.savegame_get("map_idx"), 0)
end

function Cutscene_OtherworldSpreads()
    if sh.input_action("skip", 1) and sh.event_step_get(0) >= 2 and sh.event_step_get(0) < 19 then
        sh.event.step_set(0, 19)
    end

    if sh.event_step_get(0) >= 5 and sh.event_step_get(0) < 13 then
        sh.render.frame_hold{source = "previous", tint = 128, blend = "half", layer = "overlay"}
    end

    local s = sh.event_step_get(0)
    runStep(otherworldSpreadsSteps, s)
    sh.map.ground_morph_draw(groundLevel)

    s = sh.event_step_get(0)
    if s >= 1 and s < 17 then
        if s >= 2 then
            droneFade = droneFade + sh.delta_time_get() // 8
        end
        if droneFade > 4096 then
            droneFade = 4096
        end

        local vol = (droneFade + sh.math_sin(droneFade // 4)) // 32
        if vol > 255 then
            vol = 255
        end
        sh.sfx_attributes_update("darkness_drone", 0, ~vol, 0)

        vol = (4096 - sh.math_cos(droneFade // 4)) // 16
        if vol > 255 then
            vol = 255
        end
        sh.sfx_attributes_update("otherworld_spread_drone", 0, ~vol, 0)
    elseif s >= 17 and s < 20 then
        droneFade = droneFade - sh.delta_time_get() // 8
        if droneFade < 0 then
            droneFade = 0
        end

        local vol = droneFade // 16
        if vol > 255 then
            vol = 255
        end
        sh.sfx_attributes_update("darkness_drone", 0, ~vol, 0)

        vol = droneFade // 16
        if vol > 255 then
            vol = 255
        end
        sh.sfx_attributes_update("otherworld_spread_drone", 0, ~vol, 0)
    end

    if s >= 5 and s < 10 then
        sh.pad.effect_level(9, 224)
    end

    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, RsuDmsBuf)
        sh.cutscene.camera(t, RsuDmsBuf, "camera_target_pos", "camera_target_look")
    end
end
