-- map7_s00: Nowhere - hospital 1F, Lisa cutscene
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

-- index 3: hospital: the HSPTL6.DMS cutscene with Lisa.
local CUTSCENE_SKIP = 27 -- step the cutscene jumps to when skipped

local function msg_audio(msgIdx)
    sh.msg.show_with_audio(msgIdx, "msg_audio_idx", "msg_audio_cmds")
end

local function timer_advance(rate, startT, endT, clamp, autoAdvance)
    sh.cutscene.timer_advance("cutscene_timer", rate, startT, endT, clamp, autoAdvance)
end

local function door_sfx(sfxId)
    sh.vec.set(0, 59801, -4915, -567296) -- (14.6, -1.2, -138.5) m, the door
    sh.sound.play_with_flags(sfxId, "vec:0", 128, 0) -- volume 0.5
end

local hospitalLisaSteps = {}

hospitalLisaSteps[0] = function()
    sh.player_freeze(true)
    sh.flag_set("M7S00_EntryMovieSeen", false)
    sh.game_state_update()
    sh.map.set("msg_audio_idx", 0)
    sh.map.set("hospital_scene_flag", 0)
    sh.flag_set("M7S00_EntryMovieSeen", true)
    sh.load.file("ANIM/HSPTL6.DMS", 11) -- HSPTL6.DMS
    sh.load.wait()
    sh.cutscene.prepare(11)
    sh.map.set("cutscene_timer", 0)
    sh.light.bone_detach()
    sh.light.lens_flare_detach()
    sh.light.set_intensity(2048) -- 0.5
    sh.light.set_pos_x(76595) -- 18.7 m
    sh.light.set_pos_y(-7782) -- -1.9 m
    sh.light.set_pos_z(-564428) -- -137.8 m
    sh.light.set_rot_x(-386) -- -34.0 deg
    sh.light.set_rot_y(-1137) -- -100.0 deg
    sh.light.set_rot_z(0)
    sh.water_effect_enable()
    sh.cutscene.border_show()
    sh.screen_fade_reset_timestep()
    sh.cutscene_flag(true)
    sh.npc.spawn("Lisa", 0, 71270, 569753, 0, 3) -- Lisa at (17.4, 139.1); the positive z is intentional
    sh.event_step_increment(0)
    sh.sd_call("lisa_cry")
    sh.map.set("room_hum_sfx_volume", 0)
    sh.held_item_hide()
    sh.player.weapon_stash()
end

hospitalLisaSteps[1] = function()
    msg_audio("map7_s00.msg15")
end

hospitalLisaSteps[2] = function()
    sh.event_wait_timer(6144, false) -- 1.5 s
end

hospitalLisaSteps[3] = function()
    sh.event_chara_anim(0, 0, "scene_lisa_hospital_1", false)
    sh.event_chara_anim(0, 1, "worry", false)
    sh.event_step_increment(0)
    return hospitalLisaSteps[4]()
end

hospitalLisaSteps[4] = function()
    sh.event_fade_cmd(2, false, 0, 3276, false) -- 0.8
end

hospitalLisaSteps[5] = function()
    msg_audio("map7_s00.lisa")
end

hospitalLisaSteps[6] = function()
    sh.event_chara_anim(0, 1, "lean_in", false)
    sh.event_step_increment(0)
    return hospitalLisaSteps[7]()
end

hospitalLisaSteps[7] = function()
    msg_audio("map7_s00.harry_help_me")
    timer_advance(10240, 4096, 44 * 4096, false, true) -- 2.5, 1.0, 44.0 s
end

hospitalLisaSteps[8] = function()
    sh.event_chara_anim(0, 0, "scene_lisa_hospital_2", false)
    sh.event_step_increment(0)
    return hospitalLisaSteps[9]()
end

hospitalLisaSteps[9] = function()
    msg_audio("map7_s00.harry_help_me")
    timer_advance(10240, 4096, 62 * 4096, true, false)
end

hospitalLisaSteps[10] = function()
    sh.event_wait_timer(1638, false) -- 0.4 s
    timer_advance(10240, 4096, 62 * 4096, true, false)
end

hospitalLisaSteps[11] = function()
    sh.event_chara_anim(0, 1, "sob", false)
    sh.flag_set("M7S00_LisaSobbingStarted", true)
    sh.event_step_increment(0)
    return hospitalLisaSteps[12]()
end

hospitalLisaSteps[12] = function()
    msg_audio("map7_s00.no")
    sh.event_wait_timer(2048, false) -- 0.5 s
    timer_advance(10240, 4096, 65 * 4096, true, false)
end

hospitalLisaSteps[13] = function()
    sh.event_chara_anim(0, 0, "scene_lisa_hospital_3", false)
    sh.event_step_increment(0)
    return hospitalLisaSteps[14]()
end

hospitalLisaSteps[14] = function()
    msg_audio("map7_s00.no")
    timer_advance(10240, 4096, 65 * 4096, true, false)
end

hospitalLisaSteps[15] = function()
    sh.event_step_increment(0)
    return hospitalLisaSteps[16]()
end

hospitalLisaSteps[16] = function()
    sh.event_chara_anim(0, 0, "scene_lisa_hospital_4", false)
    sh.event_chara_anim(0, 1, "reach_out", false)
    sh.event_step_increment(0)
    return hospitalLisaSteps[17]()
end

hospitalLisaSteps[17] = function()
    timer_advance(30720, 65 * 4096, 81 * 4096, true, true) -- 7.5 s
    msg_audio("map7_s00.wait_where_do_you_think")
end

hospitalLisaSteps[18] = function()
    sh.npc.clear(1, 0, 0)
    sh.event_chara_anim(0, 0, "scene_lisa_hospital_5", false)
    sh.light.set_pos_x(66764) -- 16.3 m
    sh.light.set_pos_y(-6348) -- -1.55 m
    sh.light.set_pos_z(-567705) -- -138.6 m
    -- light rotation written as one packed value: x = 0xFF34 (-204), y = 0x02E3
    sh.light.set_rot_x(-204)
    sh.light.set_rot_y(0x02E3)
    sh.light.set_rot_z(0)
    door_sfx("door_open_0") -- door open 0 sound
    sh.event_step_increment(0)
    return hospitalLisaSteps[19]()
end

hospitalLisaSteps[19] = function()
    msg_audio("map7_s00.wait_where_do_you_think")
    timer_advance(8 * 4096, 82 * 4096, 105 * 4096, true, false)
end

hospitalLisaSteps[20] = function()
    door_sfx("door_close_0") -- door close 0 sound
    sh.event_step_increment(0)
    return hospitalLisaSteps[21]()
end

hospitalLisaSteps[21] = function()
    sh.event_wait_timer(10240, false) -- 2.5 s
end

hospitalLisaSteps[22] = function()
    sh.msg.show(false, "map7_s00.lisa_2", 0, 0, 0, false)
    timer_advance(8 * 4096, 82 * 4096, 105 * 4096, true, false)
end

hospitalLisaSteps[23] = function()
    timer_advance(8 * 4096, 82 * 4096, 105 * 4096, true, true)
end

hospitalLisaSteps[24] = function()
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.light.set_intensity(2457) -- 0.6
    sh.event_step_increment(0)
    return hospitalLisaSteps[25]()
end

hospitalLisaSteps[25] = function()
    sh.msg.show(false, "map7_s00.that_sound", 0, 0, 0, false)
    timer_advance(6 * 4096, 106 * 4096, 150 * 4096, true, false)
end

hospitalLisaSteps[26] = function()
    timer_advance(6 * 4096, 106 * 4096, 150 * 4096, true, true)
end

hospitalLisaSteps[CUTSCENE_SKIP] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

hospitalLisaSteps[28] = function()
    sh.map.set("cutscene_timer", 150 * 4096)
    sh.event_step_increment(0)
end

hospitalLisaSteps.default = function()
    sh.player_freeze(false, true)
    sh.state_set(0)
    sh.camera_auto_return(true)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.water_effect_disable()
    sh.light.flashlight_fix()
    sh.light.set_intensity(4096)
    sh.flag_set("M7S00_CutsceneLisaHospitalDone", true)
    sh.npc.clear(1, 0, 0)
    sh.map.set("cutscene_timer", -1) -- cutscene stopped
    sh.sd_call("stop_xa_audio")
    sh.map.set("room_hum_sfx_volume", 4096)
    sh.held_item_show()
    sh.player.weapon_restore()
    sh.npc.set_pos_x(0, 72908) -- 17.8 m
    sh.npc.set_pos_z(0, -564019) -- -137.7 m
end

function Cutscene_HospitalLisa()
    local s = sh.event_step_get(0)
    if sh.input_action("skip", 1) and s > 0 and s < CUTSCENE_SKIP then
        sh.event.step_set(0, CUTSCENE_SKIP)
        s = CUTSCENE_SKIP
    end

    runStep(hospitalLisaSteps, s)
    do
        local timer = sh.map.get("cutscene_timer")
        if timer >= 0 then
            sh.cutscene.chara(0, "HERO", timer, 11)
            if sh.event_step_get(0) < 18 then
                sh.cutscene.chara(1, "LISA", timer, 11)
            end
            sh.cutscene.camera(timer, 11, "camera_pos", "camera_look")

            local step = sh.event_step_get(0)
            if step == 1 or step == 2 then
                sh.map.set("room_hum_sfx_volume", sh.map.get("room_hum_sfx_volume") + sh.delta_time_get() // 2)
                if sh.map.get("room_hum_sfx_volume") > 1024 then -- 0.25
                    sh.map.set("room_hum_sfx_volume", 1024)
                end
            end
            if step >= 10 then
                -- the hum swells by a random 0.5..1.0 per second, divided by 8
                local r = sh.rng_uint(2048, 4095)
                sh.map.set("room_hum_sfx_volume", sh.map.get("room_hum_sfx_volume") + ((r * sh.delta_time_get()) // 4096) // 8)
                if sh.map.get("room_hum_sfx_volume") > 4096 then
                    sh.map.set("room_hum_sfx_volume", 4096)
                end
            end

            sh.vec.set(1, 72908, 8192, -569753) -- (17.8, 2.0, -139.1) m
            sh.sound.play_falloff("lisa_cry", "vec:1", sh.map.get("room_hum_sfx_volume") // 32, 32 * 4096, sh.rng_uint(-8, 7)) -- heard within 32 m
        end
    end
end

-- index 4: "I'm being summoned": the elevator doors slide open by themselves while the camera
-- pulls back (steps 3-6), then Harry walks out slowly (time at quarter speed, steps 8-11) as
-- the doors drift on. Every frame is drawn over the next at half blend (the ghosting); from
-- step 7 the brightness option's contrast pass (two added and two subtracted grey tiles, as the
-- map6_s02 scene draws it) goes over it. The room hum fades out; flag M7S00_CutsceneElevatorSummonsDone at the end.
local doorSpeeds = {0, 0} -- door speeds of the two door pairs (s16 in the original), Q12 m/s

local function SummonsMsg(msgKey)
    sh.msg.show_with_audio(msgKey, "msg_audio_idx", "summons_voice_cmds[0]")
end

local function MulQ12(a, b) return (a * b) // 4096 end

-- Doors 0/2 slide to +z, 1/3 to -z, each pair at its speed; `count` doors move.
local function DoorsSlide(count, clamp)
    local dt = sh.delta_time_get()
    for j = 0, count - 1 do
        local z = sh.map.get("elevator_door_z", j)
        local step = MulQ12(dt, doorSpeeds[(j >> 1) + 1])
        if (j & 1) ~= 0 then
            z = z - step
            if clamp and z < -595763 then z = -595763 end -- -145.45
        else
            z = z + step
            if clamp and z > -530227 then z = -530227 end -- -129.45
        end
        sh.map.set("elevator_door_z", z, j)
    end
end

-- Steps 9-11: the doors drift the other way, outer doors at the first speed.
local function DoorsDrift(rate, limit)
    doorSpeeds[1] = math.min(doorSpeeds[1] + MulQ12(sh.delta_time_get(), rate), limit)
    local dt = sh.delta_time_get()
    for j = 0, 3 do
        local z = sh.map.get("elevator_door_z", j)
        local step = MulQ12(dt, doorSpeeds[(j >> 1) + 1])
        if (j & 1) ~= 0 then z = z + step else z = z - step end
        sh.map.set("elevator_door_z", z, j)
    end
end

local function SummonsWalk(msgKey)
    sh.event_waypoint_player("walk_forward", 1, false)
    if msgKey then SummonsMsg(msgKey) end
    sh.camera.look_at("pos:0", -6553, -1228, 0, 0, 0, 0, 0, false) -- -1.6, -0.3 m from Harry
    sh.delta_time_set(sh.delta_time_get() // 4)
end

local elevatorSummonsSteps = {}

elevatorSummonsSteps[0] = function()
    sh.player_freeze(true)
    sh.event_fade_cmd(0, true, 2, 0, false) -- Start, out
    sh.map.set("msg_audio_idx", 0)
    doorSpeeds[2] = 0
    doorSpeeds[1] = 0
    for i = 0, 5 do
        sh.event_tween_reset(i)
    end
    sh.camera.position(nil, 238428, -29614, -592363, 0, 0, 0, 0, true) -- (58.21, -7.23, -144.62) m
    sh.camera.look_at(nil, 233144, -18432, -581591, 0, 0, 0, 0, true)   -- (56.92, -4.5, -141.99) m
    sh.npc.set_pos_x(0, 235315)  -- 57.45
    sh.npc.set_pos_z(0, -569548) -- -139.05
    sh.npc.set_rot_y(0, -1024)   -- -90 deg
    sh.light.flashlight_on()
    sh.flag_set("M7S00_CutsceneElevatorSummonsStarted", true)
    sh.event_step_increment(0)
    return elevatorSummonsSteps[1]()
end

elevatorSummonsSteps[1] = function()
    sh.event_wait_player_stop()
end

elevatorSummonsSteps[2] = function()
    sh.event_fade_cmd(1, true, 2, 0, false) -- Wait
end

elevatorSummonsSteps[6] = function(s)
    SummonsMsg("map7_s00.im_being_summoned")
    return elevatorSummonsSteps[5](s)
end

elevatorSummonsSteps[5] = function(s)
    if s == 5 then
        sh.event_wait_timer(10240, false) -- 2.5 s
    end
    doorSpeeds[2] = math.min(doorSpeeds[2] + MulQ12(sh.delta_time_get(), 1310), 573) -- +0.32/s up to 0.14
    return elevatorSummonsSteps[4](s)
end

elevatorSummonsSteps[4] = function(s)
    local count = 4
    if s == 4 then
        sh.event_wait_timer(3686, false) -- 0.9 s
        count = 2
    end
    doorSpeeds[1] = math.min(doorSpeeds[1] + MulQ12(sh.delta_time_get(), 1310), 573)
    DoorsSlide(count, true)
    return elevatorSummonsSteps[3](s)
end

elevatorSummonsSteps[3] = function(s)
    if s == 3 then
        sh.event_wait_timer(0x1333, false) -- 1.2 s
    end
    local x = sh.event_tween_linear(-1516, 24576, 0) + 238428  -- -0.37 over 6 s
    local y = sh.event_tween_linear(17777, 24576, 1) - 29614   -- +4.34
    sh.camera.position(nil, x, y, sh.event_tween_linear(12616, 24576, 2) - 592363, 0, 0, 0, 0, false) -- +3.08
    local lx = sh.event_tween_linear(-9216, 24576, 3) + 233144 -- -2.25
    local ly = sh.event_tween_linear(12125, 24576, 4) - 18432  -- +2.96
    sh.camera.look_at(nil, lx, ly, sh.event_tween_linear(10240, 24576, 5) - 581591, 0, 0, 0, 0, false) -- +2.5
end

elevatorSummonsSteps[7] = function(s)
    sh.npc.set_pos_x(0, 233472)  -- 57.0
    sh.npc.set_pos_z(0, -569548) -- -139.05
    sh.camera.position(nil, 239616, -11878, sh.npc.pos_z(0), 0, 0, 0, 0, true) -- 58.5, -2.9 m
    sh.camera.look_at(nil, sh.npc.pos_x(0) - 6553, -1228, sh.npc.pos_z(0), 0, 0, 0, 0, true) -- -1.6, -0.3 m
    sh.event_waypoint_set(true, 0, 0, -1024, 212172, -569548) -- -90 deg, (51.8, -139.05)
    sh.npc.set_rot_y(0, -1024)
    doorSpeeds[2] = 0
    doorSpeeds[1] = 0
    sh.event_step_increment(0)
    return elevatorSummonsSteps[8](s)
end

elevatorSummonsSteps[8] = function(s)
    SummonsWalk("map7_s00.what_happened_in_this_town")
    sh.event_wait_timer(24576, false) -- 6.0 s
end

elevatorSummonsSteps[9] = function(s)
    SummonsWalk("map7_s00.what_happened_in_this_town")
    DoorsDrift(2252, 1228) -- +0.55/s up to 0.3
end

elevatorSummonsSteps[10] = function(s)
    SummonsWalk(nil)
    DoorsDrift(2252, 1228)
    sh.event_wait_timer(12288, false) -- 3.0 s
end

elevatorSummonsSteps[11] = function(s)
    sh.event_waypoint_player("walk_forward", 1, false)
    sh.event_fade_cmd(2, true, 0, 2457, false) -- Auto, out, 0.6 s
    SummonsMsg("map7_s00.msg46")
    sh.delta_time_set(sh.delta_time_get() // 4)
    DoorsDrift(0x428, 0x1EB)
end

elevatorSummonsSteps[12] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

elevatorSummonsSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0) -- back to gameplay
    sh.camera_auto_return(true)
    sh.flag_set("M7S00_CutsceneElevatorSummonsDone", true)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.map.set("room_hum_sfx_volume", 0)
end

function Cutscene_ElevatorSummons()
    local s = sh.event_step_get(0)
    if s >= 7 then
        local level = MulQ12(sh.game.brightness() * 8 + 4, sh.render.fade_in_level())
        for k = 0, 3 do
            local right = (k & 1) ~= 0
            sh.render.tile{rect = {right and 96 or -160, -112, right and 64 or 256, 224}, colour = level,
                           blend = k >= 2 and "sub" or "add", layer = "screen"}
        end
    end
    sh.render.frame_hold{source = "previous", tint = 128, blend = "half", layer = "overlay"}

    if sh.input_action("skip", 1) and s >= 3 and s < 12 then
        sh.event.step_set(0, 12)
        s = 12
    end

    runStep(elevatorSummonsSteps, s, s)
    if sh.event_step_get(0) >= 3 then
        local v = sh.map.get("room_hum_sfx_volume") - sh.delta_time_get() // 8
        sh.map.set("room_hum_sfx_volume", math.max(v, 0))
    end
end
