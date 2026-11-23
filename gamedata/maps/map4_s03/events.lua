-- map4_s03: Mall and boss fight
-- Event scripts of this area: the engine runs a handler every tick while its event is active.
-- Fixed-point numbers: 4096 = 1.0 (metres, seconds); angles: 4096 = 360 degrees.

-- Runs one step of a handler's step table: steps[step], or steps.default for any other step.
local function runStep(steps, step, ...)
    return (steps[step] or steps.default)(...)
end

-- The scenes hand the Twinfeeler (gamedata/chara/Twinfeeler/behavior.lua) a control state of its own:
-- a boss already past its first states (2 and up) takes it at once from its first step, one that is
-- still starting gets it as the step its start goes on to.
local TWINFEELER_ATTACK = 3     -- seek a cell and burst out
local TWINFEELER_BURST = 12     -- the scene's burst out
local TWINFEELER_DIVE = 13      -- the scene's dive

local function twinfeelerControlSet(slot, control)
    if sh.npc.control_state(slot) >= 2 then
        sh.npc.set_control_state(slot, control)
        sh.npc.set_state_step(slot, 0)
    else
        sh.npc.set_state_step(slot, control)
    end
end

-- The boss reset before the scene's burst: every body bone counts as underground (the next crossing
-- raises dust), its loops stop and its ground effects start over around it.
local function twinfeelerEffectsReset(slot)
    sh.entity.set(slot, "twinfeeler_ground_bits", 0xFFFF)
    sh.sfx_stop("twinfeeler_crawl_loop")
    sh.sfx_stop("twinfeeler_dig_loop")
    sh.sfx_stop("twinfeeler_acid_loop")
    sh.entity.twinfeeler_effects_reset(slot)
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
        [27] = {"FirstAidKit","M4S03_FirstAidKit"},
        [28] = {"RifleShells","M4S03_RifleShells0"},
        [29] = {"RifleShells","M4S03_RifleShells1"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point gives a first aid kit and sets no flag
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- index 3: Hunting Rifle pickup (message map4_s03.there_is_a_hunting_rifle).
-- While a living character from Harry up to Monster Cybil is still in an NPC slot (that is,
-- Twinfeeler is still alive), time stands still for this tick.
-- NPC slots here are 1..6 (0 = player).
function PickupHuntingRifle()
    for slot = 1, 6 do
        local id = sh.chara_slot_chara_id_get(slot)
        if id >= 1 and id <= 24 then -- Harry .. Monster Cybil
            sh.delta_time_set(0)
            break
        end
    end
    sh.event_item_take("HuntingRifle", 6, "M4S03_PickupHuntingRifle", "map4_s03.there_is_a_hunting_rifle") -- hunting rifle with its ammo, flag: pickup hunting rifle
end


-- Shared helpers of the Twinfeeler scenes.
-- Camera position / look-at set at once, without a source position.
local function camPos(x, y, z, accel_xz, accel_y, max_speed_xz, max_speed_y, warp)
    sh.camera.position_now(nil, x, y, z, accel_xz, accel_y, max_speed_xz, max_speed_y, warp)
end
local function camLook(x, y, z, accel_x, accel_y, max_speed_x, max_speed_y, warp)
    sh.camera.look_at_now(nil, x, y, z, accel_x, accel_y, max_speed_x, max_speed_y, warp)
end
-- Remainder that keeps the sign of the dividend (truncates toward zero).
local function cmod(a, b)
    return math.fmod(a, b)
end
-- Integer division that truncates toward zero.
local function idiv(a, b)
    local q = a // b
    if q < 0 and q * b ~= a then q = q + 1 end
    return q
end

-- Particle timer (0..65535), used only by this handler.
local s_twinParticleTimer = 0

local twinfeelerAttackSteps = {}

twinfeelerAttackSteps[0] = function()
    sh.player_freeze(true)
    sh.water_effect_disable()
    sh.light.flashlight_fix()
    sh.light.set_intensity(4096) -- 1.0
    sh.light.flashlight_on()
    sh.world.effects_update(6, 3)
    if sh.flag_get("M4S03_FenceFallAltLanding") then
        sh.npc.anim_cmd(0, 0, "scene_twinfeeler_3", false)
    else
        sh.npc.anim_cmd(0, 0, "scene_twinfeeler_2", false)
    end
    -- Ground surface under the boss; the ground height comes first.
    sh.world.surface_get("vec:1", sh.npc.pos_x(0), sh.npc.pos_z(0))
    sh.cutscene.set_border_state(20)
    sh.screen_fade_reset_timestep()
    sh.npc.set_pos_y(0, (sh.vec.get(1))) -- ground height
    sh.vec.set(0, 489472, 0, 579584) -- (119.5, 0.0, 141.5) m
    sh.sound.play_3d("twinfeeler_roar", "vec:0", 128, 0) -- sound twinfeeler_roar, 0.5
    sh.pad.effect_level(19, 0xA0)
    sh.event_step_increment(0)
end

twinfeelerAttackSteps[1] = function()
    sh.event.step_increment_after_fade(2, false, 0, 0, false)
    sh.npc.anim_cmd(2, 0, 0, false)
end

twinfeelerAttackSteps[2] = function()
    sh.event.step_increment_delayed(18432, false) -- 4.5 s
end

twinfeelerAttackSteps[3] = function()
    twinfeelerControlSet(1, TWINFEELER_BURST)
    sh.flag_set("M4S03_TwinfeelerBurstOut", true)
    sh.event_step_increment(0)
    return twinfeelerAttackSteps[4]()
end

twinfeelerAttackSteps[4] = function()
    sh.event.step_increment_delayed(2867, false) -- 0.7 s
end

twinfeelerAttackSteps[5] = function()
    sh.npc.anim_cmd(3, 0, 0, false)
    sh.event_step_increment(0)
    return twinfeelerAttackSteps[6]()
end

twinfeelerAttackSteps[6] = function()
    sh.npc.anim_cmd(1, 0, 0, false)
end

twinfeelerAttackSteps[7] = function()
    sh.npc.anim_cmd(0, 0, "scene_twinfeeler_1", false)
    sh.event_step_increment(0)
    return twinfeelerAttackSteps[8]()
end

twinfeelerAttackSteps[8] = function()
    sh.event.step_increment_delayed(9830, false) -- 2.4 s
end

twinfeelerAttackSteps[9] = function()
    sh.player_freeze(false, true)
    sh.player_freeze(true)
    sh.npc.anim_cmd(0, 0, "reset", false)
    sh.npc.set_pos_x(0, sh.npc.pos_x(0) + 2048) -- 0.5 m
    sh.npc.set_rot_y(0, sh.npc.rot_y(0) + 512) -- 45.0 deg
    sh.event_step_increment(0)
    return twinfeelerAttackSteps[10]()
end

twinfeelerAttackSteps[10] = function()
    sh.event.step_increment_delayed(14336, false) -- 3.5 s
end

twinfeelerAttackSteps[11] = function()
    sh.camera.auto_return(false)
    sh.event_step_reset()
end

twinfeelerAttackSteps[12] = function()
    sh.event.step_increment_after_fade(2, true, 0, 0, false)
    if sh.event_step_get(0) ~= 12 then
        camPos(490987, -13926, 568156, 0, 0, 0, 0, true)
        camLook(487956, -6103, 582246, 0, 0, 0, 0, true)
        twinfeelerEffectsReset(1)
        twinfeelerControlSet(1, TWINFEELER_ATTACK)
    end
end

twinfeelerAttackSteps[13] = function()
    sh.npc.set_pos_x(0, 491520) -- 120.0 m
    sh.npc.set_pos_z(0, 579584) -- 141.5 m
    sh.npc.set_rot_y(0, -512) -- -45.0 deg
    sh.flag_set("M4S03_TwinfeelerBurstOut", true)
    sh.player_freeze(false, true)
    sh.state_set(0)
    sh.camera.auto_return(true)
    sh.event.step_increment_after_fade(0, false, 0, 0, false)
    sh.event_step_increment(0)
    return twinfeelerAttackSteps.default()
end

twinfeelerAttackSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.event.step_increment_after_fade(0, false, 2, 0, false)
    twinfeelerControlSet(1, TWINFEELER_ATTACK)
    sh.flag_set("M4S03_TwinfeelerAttackCutsceneDone", true)
    sh.game.state_update()
end

-- index 4: Twinfeeler bursts through the floor.
function Cutscene_TwinfeelerAttack()
    if sh.input_action("skip", 1) and sh.event_step_get(0) > 0 and sh.event_step_get(0) < 11 then
        sh.event.step_set(0, 12)
    end

    -- Camera per step.
    local s = sh.event_step_get(0)
    if s == 0 then
        camPos(493568, -38912, 561152, 0, 0, 0, 0, true) -- 120.5, -9.5, 137.0 m
        camLook(sh.npc.pos_x(0), sh.npc.pos_y(0), sh.npc.pos_z(0), 0, 0, 0, 0, true)
        s_twinParticleTimer = 0
    elseif s == 1 or s == 2 then
        -- 120.5, -5.5, 138.0 m; camera speed: horizontal / 8, vertical / 2
        camPos(493568, -22528, 565248, 0, 0,
               sh.camera.user_max_speed_xz() // 8, sh.camera.user_max_speed_y() // 2, false)
        s_twinParticleTimer = (s_twinParticleTimer + sh.delta_time_get()) & 0xFFFF
        if s_twinParticleTimer < 7372 then -- 1.8 s
            local i = cmod(sh.rng.rand16(), idiv(10240 - s_twinParticleTimer, 2048) + 1) -- 2.5, 0.5 s
            while i > 0 do
                local vx = sh.rng_int(483328, 503808 - 1) -- 118.0, 123.0 - 1
                local vz = sh.rng_int(573440, 585728 - 1) -- 140.0, 143.0 - 1
                sh.vec.set(0, vx, 0, vz)
                sh.entity.twinfeeler_dust_still(vx, 0, vz)
                i = i - 1
            end
        end
    elseif s == 3 then
        camPos(489390, -2048, 587161, 0, 0, 0, 0, true) -- 119.48, -0.5, 143.35 m
        camLook(483983, -3522, 571760, 0, 0, 0, 0, true) -- 118.16, -0.86, 139.59 m
    elseif s == 9 or s == 10 then
        camPos(490987, -13926, 568156, 0, 0, 0, 0, true) -- 119.87, -3.4, 138.71 m
        camLook(487956, -6103, 582246, 0, 0, 0, 0, true) -- 119.13, -1.49, 142.15 m
    end

    s = sh.event_step_get(0)
    runStep(twinfeelerAttackSteps, s)
end

local twinfeelerDiveSteps = {}

twinfeelerDiveSteps[0] = function()
    sh.player_freeze(true)
    sh.event.step_increment_after_fade(0, true, 2, 0, false)
    sh.event_step_increment(0)
    return twinfeelerDiveSteps[1]()
end

twinfeelerDiveSteps[1] = function()
    sh.event.wait_player_stop()
end

twinfeelerDiveSteps[2] = function()
    sh.event.step_increment_after_fade(1, true, 2, 0, false)
end

twinfeelerDiveSteps[3] = function()
    sh.npc.set_pos_x(0, 509542) -- 124.4 m
    sh.npc.set_pos_z(0, 577126) -- 140.9 m
    sh.npc.set_rot_y(0, 1024) -- 90.0 deg
    camPos(500121, -17776, 581099, 0, 0, 0, 0, true) -- 122.1, -4.34, 141.87 m
    camLook(514662, -11223, 577331, 0, 0, 0, 0, true) -- 125.65, -2.74, 140.95 m
    sh.event.step_increment_delayed(10240, false) -- 2.5 s
end

twinfeelerDiveSteps[4] = function()
    twinfeelerControlSet(1, TWINFEELER_DIVE)
    sh.event_step_increment(0)
    return twinfeelerDiveSteps[5]()
end

twinfeelerDiveSteps[5] = function()
    sh.event.step_increment_delayed(8192, false) -- 2.0 s
end

twinfeelerDiveSteps[6] = function()
    sh.npc.clear(1, 0, 0)
    sh.flag_set("M4S03_TwinfeelerDiveCutsceneDone", true)
    camPos(527646, -1064, 569753, 0, 0, 0, 0, true) -- 128.82, -0.26, 139.1 m
    camLook(512983, -3276, 576675, 0, 0, 0, 0, true) -- 125.24, -0.8, 140.79 m
    sh.vec.set(0, 540672, -3072, 573440) -- (132.0, -0.75, 140.0) m
    sh.sound.play_3d("twinfeeler_dive", "vec:0", 128, 0) -- sound twinfeeler_dive, 0.5
    sh.event_step_increment(0)
    return twinfeelerDiveSteps[7]()
end

twinfeelerDiveSteps[7] = function()
    sh.event.step_increment_delayed(8192, false)
end

twinfeelerDiveSteps[8] = function()
    sh.npc.set_pos_x(0, 519618) -- 126.86 m
    sh.npc.set_pos_z(0, 574914) -- 140.36 m
    camPos(501964, -8601, 567296, 0, 0, 0, 0, true) -- 122.55, -2.1, 138.5 m
    camLook(517693, -7086, 571637, 0, 0, 0, 0, true) -- 126.39, -1.73, 139.56 m
    sh.event.step_increment_delayed(14336, false) -- 3.5 s
    if sh.event_step_get(0) ~= 8 then
        sh.event_step_reset()
    end
end

twinfeelerDiveSteps[9] = function()
    sh.event.step_increment_after_fade(2, true, 0, 0, false)
end

twinfeelerDiveSteps[10] = function()
    sh.npc.set_pos_x(0, 519618)
    sh.npc.set_pos_z(0, 574914)
    sh.npc.set_rot_y(0, 1024)
    sh.npc.clear(1, 0, 0)
    sh.flag_set("M4S03_TwinfeelerDiveCutsceneDone", true)
    sh.camera.auto_return(true)
    sh.event.step_increment_after_fade(0, false, 0, 0, false)
    sh.event_step_increment(0)
end

twinfeelerDiveSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.camera.auto_return(false)
    sh.event.step_increment_after_fade(0, false, 2, 0, false)
    sh.game.state_update()
end

-- index 5: Twinfeeler dives into the wall.
function Cutscene_TwinfeelerDive()
    if sh.input_action("skip", 1) and sh.event_step_get(0) > 0 and sh.event_step_get(0) < 9 then
        sh.event.step_set(0, 9)
    end

    local s = sh.event_step_get(0)
    runStep(twinfeelerDiveSteps, s)
end

local tvWallAwakensSteps = {}

tvWallAwakensSteps[0] = function()
    sh.player_freeze(true)
    for i = 0, 5 do
        sh.event.ramp_reset(i)
    end
    sh.event.step_increment_after_fade(0, true, 2, 0, false)
    sh.flag_set("M4S03_TvScreensOn", true)
    sh.map.set("tv_wall_request", 2) -- mode 1 (world_objects.lua runs the wall)
    sh.light.flashlight_on()
    sh.event_step_increment(0)
    return tvWallAwakensSteps[1]()
end

tvWallAwakensSteps[1] = function()
    sh.event.wait_player_stop()
end

tvWallAwakensSteps[2] = function()
    sh.event.step_increment_delayed(9011, false) -- 2.2 s
    sh.render.frame_hold{source = "previous", tint = 128, blend = "half", layer = "overlay"}
    camLook(665600, -4710, 206848, 1536, 1945, 153, 358, false) -- 162.5, -1.15, 50.5 m; speeds 0.375, 0.475, 0.0374, 0.0875
end

tvWallAwakensSteps[3] = function()
    sh.fs_seek("1ST/NO_BLOCK.TIM")
    sh.npc.set_pos_x(0, 666705) -- 162.77 m
    sh.npc.set_pos_z(0, 227041) -- 55.43 m
    sh.npc.set_rot_y(0, 2048) -- 180.0 deg
    sh.npc.anim_flags_clear(0, 2)
    sh.water_effect_enable()
    camPos(670842, -9461, 241295, 0, 0, 0, 0, true) -- 163.78, -2.31, 58.91 m
    camLook(665600, -4710, 206848, 0, 0, 0, 0, true)
    sh.event_step_increment(0)
end

tvWallAwakensSteps[4] = function()
    if sh.map.get("tv_wall_settled") ~= 0 then
        sh.event_step_increment(0)
    end
    -- Each axis eases over 6.5 s on its own ramp (0, 1, 2) from the warp point.
    local z = sh.event.ramp_advance(-18063, 26624, 2) + 241295 -- -4.41 m; + 58.91 m
    local y = sh.event.ramp_advance(4751, 26624, 1) - 9461 -- 1.16 m; - 2.31 m
    local x = sh.event.ramp_advance(-5242, 26624, 0) + 670842 -- -1.28 m; + 163.78 m
    camPos(x, y, z, 0, 0, 0, 0, false)
    if sh.event_step_get(0) ~= 4 then
        sh.event_step_reset()
    end
end

tvWallAwakensSteps[5] = function()
    sh.event.step_increment_after_fade(2, true, 0, 0, false)
end

tvWallAwakensSteps.default = function()
    sh.state_set(0) -- back to gameplay
    sh.flag_set("M4S03_TvWallCutsceneDone", true)
    sh.npc.set_pos_x(0, 666705)
    sh.npc.set_pos_z(0, 227041)
    sh.npc.set_rot_y(0, 2048)
end

-- index 6: the TV wall. Harry stops, the screens come on (flag M4S03_TvScreensOn) and for 2.2 s every frame
-- is drawn over the next at half blend (the ghosting) while the camera turns to the wall;
-- then Harry and the camera warp in front of it and the camera eases back until all the
-- screens have settled (flag M4S03_TvWallCutsceneDone on the way out).
function Cutscene_TvWallAwakens()
    if sh.input_action("skip", 1) and sh.event_step_get(0) > 0 and sh.event_step_get(0) < 5 then
        sh.event.step_set(0, 5)
    end

    local s = sh.event_step_get(0)
    runStep(tvWallAwakensSteps, s)
end

-- Voice line index of the TV message (0..255), used only by this handler.
local s_tvMsgAudioIdx = 0

local twinfeelerTvMessageSteps = {}

twinfeelerTvMessageSteps[0] = function()
    camPos(653680, -5120, 243671, 0, 0, 0, 0, true) -- 159.59, -1.25, 59.49 m
    camLook(662282, -6266, 229785, 0, 0, 0, 0, true) -- 161.69, -1.53, 56.1 m
    sh.map.set("tv_wall_request", 3) -- mode 2
    s_tvMsgAudioIdx = 0
    sh.npc.anim_flags_set(0, 2)
    sh.water_effect_disable()
    sh.event_step_increment(0)
    return twinfeelerTvMessageSteps[1]()
end

twinfeelerTvMessageSteps[1] = function()
    sh.event.step_increment_after_fade(2, false, 0, 0, false)
end

twinfeelerTvMessageSteps[2] = function()
    -- TV message with voice: the voice command is the table entry at the current index,
    -- and the index advances when a line finishes.
    local cmd = sh.map.get("tv_msg_voice_cmds", s_tvMsgAudioIdx)
    if sh.event_msg_with_audio("map4_s03.cheryl", cmd) == -1 then -- message finished
        s_tvMsgAudioIdx = (s_tvMsgAudioIdx + 1) & 0xFF
    end
end

twinfeelerTvMessageSteps[3] = function()
    sh.event.step_increment_delayed(2048, false) -- 0.5 s
end

twinfeelerTvMessageSteps.default = function()
    sh.flag_set("M4S03_TvMessageCutsceneDone", true)
    sh.event.step_increment_after_fade(0, false, 2, 0, false)
    sh.camera.auto_return(false)
    sh.player_freeze(false, false)
    sh.state_set(0)
end

-- index 7: line at the TV screens.
function Cutscene_TwinfeelerTvMessage()
    if sh.input_action("skip", 1) and sh.event_step_get(0) >= 2 and sh.event_step_get(0) < 4 then
        sh.event_step_reset()
    end

    local s = sh.event_step_get(0)
    runStep(twinfeelerTvMessageSteps, s)
end

-- index 8: Twinfeeler attacks again.
function Event_TwinfeelerResumeAttack()
    local s = sh.event_step_get(0)
    if s == 0 then
        sh.player_freeze(true)
        sh.screen_fade_reset_timestep()
        sh.event_step_increment(0)
    elseif s == 1 then
        twinfeelerControlSet(1, TWINFEELER_ATTACK)
        sh.event_step_increment(0)
    else
        sh.flag_set("M4S03_TwinfeelerFightResumePending", false)
        sh.player_freeze(false, false)
        sh.state_set(0)
        sh.event.step_increment_after_fade(0, false, 0, 0, false)
    end
end
