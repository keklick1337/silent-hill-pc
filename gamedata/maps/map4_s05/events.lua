-- map4_s05: Central SH Otherworld - Floatstinger boss
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

-- index 2: no action.
function MapEvent_Nop()
end

-- index 3: "Cybil at the stairs" cutscene (flashlight, waypoints, camera behind
-- Harry, jump; flags 348/349).
local function StairsCamFollow(warpLook)
    sh.camera.position(nil, sh.npc.pos_x(0) + 16384, -4587, 463175, 0, 0, 0, 0, true) -- +4.0, -1.12, 113.08
    sh.camera.look_at("pos:0", 0, -6144, 0, 0, 0, 0, 0, warpLook) -- -1.5 m
end

local cybilStairsSceneSteps = {}

cybilStairsSceneSteps[0] = function()
    sh.player_freeze(true)
    sh.light.flashlight_on()
    -- Warp player.
    sh.npc.set_pos_y(0, 18432)   -- 4.5 m
    sh.npc.set_pos_x(0, -473088) -- -115.5 m
    sh.npc.set_pos_z(0, 436838)  -- 106.65 m
    sh.npc.set_rot_y(0, 0)
    sh.cutscene.border_show()
    sh.event.path_point_set(true, 0, 0, 0, -473088, 450560)     -- -115.5, 110.0
    sh.event.path_point_set(true, 0, 1, 0, -473497, 450969)     -- -115.6, 110.1
    sh.event.path_point_set(true, 0, 2, -1024, -474316, 452608) -- -90.0 deg, -115.8, 110.5
    sh.event.path_point_set(true, 0, 3, -1024, -474726, 453017) -- -115.9, 110.6
    sh.event.path_point_set(true, 0, 4, -1024, -475463, 453427) -- -116.08, 110.7
    sh.camera.position(nil, -422993, 8273, 428441, 0, 0, 0, 0, true) -- -103.27, 2.02, 104.6 m
    sh.camera.look_at(nil, -438517, 8929, 433643, 0, 0, 0, 0, true)   -- -107.06, 2.18, 105.87 m
    sh.event_step_increment(0)
    return cybilStairsSceneSteps[1]()
end

cybilStairsSceneSteps[1] = function()
    sh.event.path_walk_player(53, 5, false)
    sh.event_fade_cmd(2, false, 0, 0, false) -- Auto
end

cybilStairsSceneSteps[2] = function()
    sh.event.path_walk_player(53, 5, false)
    sh.event_wait_timer(7372, false) -- 1.8 s
end

cybilStairsSceneSteps[3] = function()
    StairsCamFollow(true)
    sh.event_step_increment(0)
    return cybilStairsSceneSteps[4]()
end

cybilStairsSceneSteps[4] = function()
    StairsCamFollow(false)
    sh.event.path_walk_player(53, 5, false)
end

cybilStairsSceneSteps[5] = function()
    sh.event_wait_timer(3276, false) -- 0.8 s
    sh.event.path_point_set(true, 0, 0, -1280, -491520, 446464) -- -112.5 deg, -120.0, 109.0
end

cybilStairsSceneSteps[6] = function()
    StairsCamFollow(false)
    sh.event.path_walk_player(53, 1, false)
end

cybilStairsSceneSteps[7] = function()
    sh.event_wait_timer(819, false) -- 0.2 s
end

cybilStairsSceneSteps[8] = function()
    sh.sound.play_with_flags("floatstinger_shriek", "stairs_sfx_pos", 128, 0) -- sound floatstinger_shriek, 0.5, no flags
    sh.event_step_increment(0)
    return cybilStairsSceneSteps[9]()
end

cybilStairsSceneSteps[9] = function()
    sh.chara_anim_play_to_end(0, "scene_cybil_stairs")
end

cybilStairsSceneSteps[10] = function()
    -- Warp player.
    sh.npc.set_rot_y(0, 1024) -- 90.0 deg
    -- Warp NPC.
    sh.npc.set_pos_x(1, -468992) -- -114.5 m
    sh.npc.set_pos_y(1, 12288)   -- 3.0 m
    sh.npc.set_pos_z(1, 442368)  -- 108.0 m
    sh.npc.set_state_step(1, 1)
    sh.flag_set("M4S05_CybilStairsSceneWarpDone", true)
    -- Warp camera.
    sh.camera.position(nil, -502702, -819, 462233, 0, 0, 0, 0, true) -- -122.73, -0.2, 112.85 m
    sh.camera.look_at(nil, -490168, 942, 451829, 0, 0, 0, 0, true)    -- -119.67, 0.23, 110.31 m
    sh.player_freeze(false, true)
    sh.player_freeze(true)
    sh.event_step_increment(0)
    return cybilStairsSceneSteps[11]()
end

cybilStairsSceneSteps[11] = function()
    sh.event_wait_timer(6144, false) -- 1.5 s
end

cybilStairsSceneSteps.default = function()
    -- Return to gameplay.
    sh.player_freeze(false, false)
    sh.state_set(0) -- back to gameplay
    sh.camera_auto_return(false)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.flag_set("M4S05_CybilStairsCutsceneDone", true)
    sh.npc.set_state_step(1, 2)
    sh.game_state_update()
end

function MapEvent_CybilStairsScene()
    if sh.input_action("skip", 1) and sh.event_step_get(0) > 0 and sh.event_step_get(0) < 12 then
        -- Warp player.
        sh.npc.set_pos_x(0, -491520) -- -120.0 m
        sh.npc.set_pos_z(0, 446464)  -- 109.0 m
        sh.npc.set_rot_y(0, 1024)    -- 0.25 m
        -- Warp NPC.
        sh.npc.set_pos_x(1, -468992) -- -114.5 m
        sh.npc.set_pos_y(1, 8192)    -- 2.0 m
        sh.npc.set_pos_z(1, 442368)  -- 108.0 m
        sh.camera_auto_return(true)
        sh.event_step_reset()
        sh.flag_set("M4S05_CybilStairsSceneWarpDone", true)
    end

    local s = sh.event_step_get(0)
    runStep(cybilStairsSceneSteps, s)
end

local cybilAimSceneSteps = {}

cybilAimSceneSteps[0] = function()
    sh.player_freeze(true)
    sh.event_step_increment(0)
    return cybilAimSceneSteps[1]()
end

cybilAimSceneSteps[1] = function()
    sh.event.wait_player_stop()
end

cybilAimSceneSteps[2] = function()
    sh.cutscene.border_show()
    -- Map point 14 follows Cybil: position and facing.
    sh.map.set("map_point.x", sh.npc.pos_x(0), 14)
    sh.map.set("map_point.z", sh.npc.pos_z(0), 14)
    -- Facing angle, signed, shifted by a full turn and stored in 8 bits.
    local a = sh.npc.rot_y(0) & 0xFFF -- angle wrapped to 0..360 deg
    if a > 2048 then a = a - 4096 end          -- signed angle
    sh.map.set("map_point.heading_q8", (((a + 4096) & 0xFFFFFFFF) >> 4) & 0xFF, 14)

    sh.event_chara_anim(0, 0, "reset", false) -- SetState
    -- Warp camera.
    sh.camera.position(nil, -484229, 5980, 432824, 0, 0, 0, 0, true) -- -118.22, 1.46, 105.67 m
    sh.camera.look_at(nil, -491397, -5734, 441753, 0, 0, 0, 0, true)  -- -119.97, -1.4, 107.85 m
    sh.world.effects_reset()
    sh.map.set("aim_sfx_fade", 0)
    sh.map.set("aim_sfx_play_count", 0)
    sh.event_step_increment(0)
    return cybilAimSceneSteps[3]()
end

cybilAimSceneSteps[3] = function()
    sh.event_chara_anim(2, 0, 0, false) -- AnimLock
    sh.event_wait_timer(14336, false)   -- 3.5 s
    -- Warp player.
    sh.npc.set_pos_x(0, -491520) -- -120.0 m
    sh.npc.set_pos_y(0, 8192)    -- 2.0 m
    sh.npc.set_pos_z(0, 425984)  -- 104.0 m
    sh.npc.set_rot_y(0, 0)
    -- Warp NPC.
    sh.npc.set_pos_x(1, -491520) -- -120.0 m
    sh.npc.set_pos_y(1, 8192)    -- 2.0 m
    sh.npc.set_pos_z(1, 442368)  -- 108.0 m
    sh.npc.set_rot_y(1, 2048)    -- 180.0 deg
end

cybilAimSceneSteps[4] = function()
    sh.flag_set("M4S05_FloatstingerLandingTriggered", true)
    sh.event_fade_cmd(0, true, 1, 819, false) -- Start, type 1, 0.2
    sh.event_step_increment(0)
    return cybilAimSceneSteps[5]()
end

cybilAimSceneSteps[5] = function()
    sh.event_wait_timer(8192, false) -- 2.0 s
end

cybilAimSceneSteps[6] = function()
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.event_step_increment(0)
    return cybilAimSceneSteps[7]()
end

cybilAimSceneSteps[7] = function()
    sh.event_fade_cmd(1, true, 0, 0, false) -- Wait
end

cybilAimSceneSteps.default = function()
    sh.event_chara_anim(3, 0, 0, false) -- AnimUnlock
    sh.player_freeze(false, false)
    sh.state_set(0) -- back to gameplay
    sh.flag_set("M4S05_CybilAimCutsceneDone", true)
end

-- index 4: "Cybil aims at Harry": border, AnimLock, warp, white flash, flags 351/352,
-- pulsing hum (sound darkness_drone).
-- aim_sfx_fade is the hum volume (4096 = 1.0), aim_sfx_play_count counts the repeats.
function MapEvent_CybilAimScene()
    local s = sh.event_step_get(0)
    runStep(cybilAimSceneSteps, s)
    if sh.map.get("aim_sfx_fade") == 0 then
        sh.sd_call("darkness_drone")
        sh.map.set("aim_sfx_play_count", sh.map.get("aim_sfx_play_count") + 1)
    end
    -- Fade the hum in over 1.5 s, up to full volume.
    sh.map.set("aim_sfx_fade", math.min(sh.map.get("aim_sfx_fade") + (sh.delta_time_get() * 2) // 3, 4096))
    sh.sfx_attributes_update("darkness_drone", 0, (-(sh.map.get("aim_sfx_fade") // 16)) & 0xFF, 0)
    local n = sh.map.get("aim_sfx_play_count")
    if sh.map.get("aim_sfx_fade") == 4096 and n >= 0 and n < 3 then
        sh.map.set("aim_sfx_fade", 0)
    end
end

-- index 5: UFO over the roof (UFO ending, UFO2.DMS, steps 0-10 +
-- default; hum (sound ufo_hum) with pitch modulation, Channeling Stone).
-- ufo_hum_clock / ufo_hum_volume / ufo_pos / ufo_msg_audio_idx / ufo_rising are map state
-- used only by this scene.
local function Ufo2Wobble(speedQ12, rangeQ12, mode)
    sh.map.ufo_glow_spawn("ufo_pos", speedQ12, rangeQ12, mode)
end

local ufoChannelingStoneSteps = {}

ufoChannelingStoneSteps[0] = function()
    sh.player_freeze(true)
    sh.map.set("ufo_msg_audio_idx", 0)
    sh.load.tim("TIM/ENBAN.TIM", 1, "ufo_glow_tex") -- ENBAN.TIM
    sh.load.file("ANIM/UFO2.DMS", 19) -- UFO2.DMS
    sh.map.set("cutscene_timer", -1) -- none
    sh.screen_fade_reset_timestep()
    sh.sd_call("ufo_hum")
    sh.sfx_attributes_update("ufo_hum", 0, 255, 0) -- 1.0
    sh.map.set("ufo_hum_volume", 0)
    sh.map.set("ufo_hum_clock", 0)
    sh.event_step_increment(0)
    return ufoChannelingStoneSteps[1]()
end

ufoChannelingStoneSteps[1] = function()
    sh.event.wait_player_stop()
end

ufoChannelingStoneSteps[2] = function()
    if sh.load.chunks() ~= 0 then
        sh.event_step_increment(0)
    end
end

ufoChannelingStoneSteps[3] = function()
    sh.map.set("cutscene_timer", 0)
    sh.event_fade_cmd(0, true, 3, 0, false) -- Start, type 3
    sh.cutscene.prepare(19)
    sh.held_item_hide()
    sh.player.weapon_stash()
    sh.light.flashlight_on()
    sh.map.set("ufo_pos.x", 155 * 4096)
    sh.map.set("ufo_pos.y", -16 * 4096)
    sh.map.set("ufo_pos.z", 28 * 4096)
    sh.map.set("ufo_rising", 0)
    sh.event_step_increment(0)
    return ufoChannelingStoneSteps[4]()
end

ufoChannelingStoneSteps[4] = function()
    sh.event_fade_cmd(2, false, 0, 0, false) -- Auto
end

ufoChannelingStoneSteps[5] = function()
    sh.msg.show_with_audio("map4_s05.what", "ufo_msg_audio_idx", "ufo_msg_voice_cmds[0]")
end

ufoChannelingStoneSteps[6] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40 * 4096, 0, 30 * 4096, true, true)
    Ufo2Wobble(2457, 614, 0) -- 0.6, 0.15
end

ufoChannelingStoneSteps[7] = function()
    sh.msg.show(false, "map4_s05.that_light", 0, 0, 0, false)
    Ufo2Wobble(2457, 614, 0)
end

ufoChannelingStoneSteps[8] = function()
    sh.vec.short_set(0, 0, 0, 0) -- zero offset
    sh.vec.set(0, 632053, -63488, 98590) -- (154.31, -15.5, 24.07) m
    sh.camera.look_at_bone("player_bone:2", "svec:0", "vec:0") -- Harry's head
    Ufo2Wobble(4096, 819, 1) -- 1.0, 0.2
    sh.event_wait_timer(10240, false) -- 2.5 s
end

ufoChannelingStoneSteps[9] = function()
    Ufo2Wobble(2457, 614, 0)
    local dt = sh.delta_time_get()
    local timer = sh.event.state_timer() + dt
    sh.event.set_state_timer(timer)
    timer = sh.event.state_timer()
    if timer > 6144 then -- 1.5
        local vy = sh.map.get("ufo_pos.y")
        if sh.map.get("ufo_rising") ~= 0 then
            vy = vy - (dt * 14336) // 4096 -- 3.5
            sh.map.set("ufo_pos.y", vy)
            if vy < -71680 then -- -17.5
                sh.map.set("ufo_rising", 0)
            end
        else
            vy = vy + (dt * 14336) // 4096
            sh.map.set("ufo_pos.y", vy)
            if vy > -59392 then -- -14.5
                sh.map.set("ufo_rising", 1)
            end
        end
    end
    if timer > 2048 then -- 0.5
        sh.map.set("ufo_pos.x", sh.map.get("ufo_pos.x") - (dt * 10240) // 4096) -- 2.5 m
    end
    if timer > 6 * 4096 then
        sh.event_step_increment(0)
    end
end

ufoChannelingStoneSteps[10] = function()
    sh.event_wait_timer(10240, false) -- 2.5 s
end

ufoChannelingStoneSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0) -- back to gameplay
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.camera_auto_return(true)
    sh.flag_set("M4S05_ChannelingStoneUsed", true)
    sh.event.inv_item(3, "ChannelingStone", 1, false) -- AddItem, channeling stone
    sh.held_item_show()
    sh.player.weapon_restore()
    sh.map.set("cutscene_timer", -1) -- none
end

function Cutscene_UfoChannelingStone()
    local clock = sh.map.get("ufo_hum_clock")
    local half_clock = clock // 2
    local sin0 = sh.math_sin(half_clock)
    local cos0 = sh.math_cos(clock // 16)
    local pitch = ((sin0 * 2) + cos0 + sh.math_cos(sh.math_cos(half_clock))) // 512

    sh.map.set("ufo_hum_clock", clock + sh.delta_time_get())

    local s = sh.event_step_get(0)
    if s >= 10 then
        local v = sh.map.get("ufo_hum_volume") - (sh.delta_time_get() * 2048) // 4096 -- 0.5 s
        sh.map.set("ufo_hum_volume", v)
        if sh.map.get("ufo_hum_volume") < 0 then
            sh.map.set("ufo_hum_volume", 0)
        end
        sh.sfx_attributes_update("ufo_hum", 0, ~(sh.map.get("ufo_hum_volume") // 16), pitch)
    elseif s > 0 then
        local v = sh.map.get("ufo_hum_volume") + (sh.delta_time_get() * 1024) // 4096 -- 0.25 s
        sh.map.set("ufo_hum_volume", v)
        if sh.map.get("ufo_hum_volume") > 4080 then -- 0.9961
            sh.map.set("ufo_hum_volume", 4080)
        end
        sh.sfx_attributes_update("ufo_hum", 0, ~(sh.map.get("ufo_hum_volume") // 16), pitch)
    end

    runStep(ufoChannelingStoneSteps, s)
    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, 19)
        sh.cutscene.camera(t, 19, "camera_target_pos", "camera_target_look")
    end
end
