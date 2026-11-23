-- map3_s02: Hospital - antique shop cutscene
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
    sh.event_msg_with_sfx("map3_s02.its_locked", "door_locked")
end

-- index 2: empty handler.
function MapEvent_Nop()
end

-- index 3: short fade-out with a sound near the elevator.
-- Steps 1 -> 2 and 3 -> 4 -> finish run in the same tick.
function MapEvent_DarknessSfxEvent()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.screen_fade_reset_timestep()
        sh.event_step_increment(0)
        return
    end
    if step == 1 then
        -- start the fade; continues into step 2 in the same tick
        sh.event_fade_cmd(0, false, 0, 0, false)
        sh.event_step_increment(0)
        step = 2
    end
    if step == 2 then
        sh.event_wait_timer(4096, false)
        return
    end
    if step == 3 then
        -- sound darkness_fall at (100.0, -1.2, -127.2), volume 0.5; continues into step 4 in the same tick
        sh.sfx_flags_play("darkness_fall", 409600, -4915, -521011, 128, 0)
        sh.event_step_increment(0)
        step = 4
    end
    if step == 4 then
        sh.event_wait_timer(1228, false)              -- 0.3 s; continues into the finish in the same tick
    end
    -- finish
    sh.player_freeze(false, false)
    sh.state_set(0)                                   -- back to gameplay
end

-- index 4: the lights go out (map effects 7/4, flashlight off/on), flag M3S02_LightsWentOut.
-- fog_effect_level and fog_effect_active are shared map state: the map's world objects read
-- them every frame.
function MapEvent_LightsGoOut()
    local s = sh.event_step_get(0)
    if s == 0 then
        sh.player_freeze(true)
        sh.map.set("fog_effect_level", 409600) -- 100.0
        sh.screen_fade_reset_timestep()
        sh.event_step_increment(0)
    elseif s == 1 then
        sh.map_effects_init(7, 4)
        sh.map.set("fog_effect_active", 0)
        sh.flashlight(false)
        sh.event_fade_cmd(0, 0, 0, 0, 0) -- Start, in
        sh.event_step_increment(0)
    elseif s == 2 then
        sh.event_wait_timer(2457, 0) -- 0.6 s
    else
        if s == 3 then
            sh.flashlight(true)
            sh.event_step_increment(0)
        end
        sh.player_freeze(false, false)
        sh.state_set(0)
        sh.flag_set("M3S02_LightsWentOut", true)
    end
end

-- index 5: DWSTWY.DMS cutscene: Alessa on the stairway, seen double. Every tick the held
-- picture is cleared, the world is drawn into it and comes onto the screen as four shifted
-- copies (the double vision); time runs at 4/5 speed. The scene clock is the map's own
-- (stairway_scene_timer, -1 = not running); flag M3S02_AlessaStairwayCutsceneDone at the end.
local STAIRWAY_DMS_BUF = 24

-- Bottom first: an opaque copy one pixel up, then half blends one pixel down (full) and one
-- right (half bright), an added quarter-bright copy one pixel left on top.
local DOUBLE_VISION = {
    {offset = {0, -1}, tint = 128, blend = "none"},
    {offset = {0, 1}, tint = 128, blend = "half"},
    {offset = {1, 0}, tint = 64, blend = "half"},
    {offset = {-1, 0}, tint = 32, blend = "add"},
}

local function StairwayTimerAdvance(rate, startTime, endTime, clampToEnd, autoAdvance)
    sh.cutscene.timer_advance("stairway_scene_timer", rate, startTime, endTime, clampToEnd, autoAdvance)
end

function Cutscene_AlessaStairway()
    sh.delta_time_set(sh.delta_time_get() * 4 // 5)
    sh.render.held_clear()
    sh.render.world_redirect{layers = DOUBLE_VISION}

    local s = sh.event_step_get(0)
    if sh.input_action("skip", 1) and s > 0 and s < 4 then
        sh.event.step_set(0, 4)
        s = 4
    end

    if s == 0 then
        sh.player_freeze(true)
        sh.game_state_update()
        sh.screen_fade_reset_timestep()
        sh.cutscene.border_show()
        sh.cutscene_flag(true)
        sh.load.file("ANIM/DWSTWY.DMS", STAIRWAY_DMS_BUF)
        sh.load.wait()
        sh.cutscene.prepare(STAIRWAY_DMS_BUF)
        sh.npc.spawn("Alessa", 0, 327680, 409600, 0, 3) -- (80.0, 100.0) m
        sh.light.bone_detach()
        sh.light.lens_flare_detach()
        sh.light.set_intensity(2867) -- 0.7
        sh.npc.anim_flags_clear(0, 2)
        sh.water_effect_enable()
        sh.map_effects_init(16, 16)
        sh.map.set("stairway_scene_timer", 0)
        sh.event_step_increment(0)
    elseif s == 1 or s == 2 then
        if s == 1 then
            sh.event_chara_anim(0, 1, "walk", false)
            sh.event_fade_cmd(0, false, 0, 4096, false) -- Start, in, 1.0 s
            sh.event_step_increment(0)
        end
        StairwayTimerAdvance(49152, 0, 245760, false, true) -- 12.0, 0.0 .. 60.0
    elseif s == 3 then
        sh.event_fade_cmd(2, true, 0, 4096, false) -- Auto, out, 1.0 s
        StairwayTimerAdvance(49152, 0, 307200, true, false) -- 12.0, 0.0 .. 75.0
    elseif s == 4 then
        sh.event_fade_cmd(2, true, 0, 0, false)
    else
        sh.player_freeze(false, false)
        sh.state_set(0) -- back to gameplay
        sh.npc.anim_flags_set(0, 2)
        sh.water_effect_disable()
        sh.light.flashlight_fix()
        sh.light.set_intensity(4096)
        sh.cutscene.border_reset()
        sh.flag_set("M3S02_AlessaStairwayCutsceneDone", true)
        sh.camera_auto_return(true)
        sh.map.set("stairway_scene_timer", -1)
        sh.map_effects_init(2, 2)
        sh.map_effects_update(7, 7, 5, "fog_effect_level", 0, 409600) -- 32-bit level, 100.0
    end

    local t = sh.map.get("stairway_scene_timer")
    if t >= 0 then
        sh.cutscene.chara(1, "ARISA", t, STAIRWAY_DMS_BUF)
        sh.cutscene.camera(t, STAIRWAY_DMS_BUF, "vec:0", "vec:1")
        sh.cutscene.light(t, STAIRWAY_DMS_BUF)
    end
end
