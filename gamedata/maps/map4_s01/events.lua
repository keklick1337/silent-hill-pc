-- map4_s01: Green Lion Antiques (normal + Otherworld)
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

-- index 2: no action.
function MapEvent_Nop()
end

-- index 3: AQSH1.DMS cutscene, Harry and Cybil in the antique shop
-- (steps 0-44 + default, DMS HERO/SIBYL/LIGHT, voiced).
local function MsgAudio(msgIdx)
    sh.msg.show_with_audio(msgIdx, "msg_audio_idx", "msg_audio_cmds")
end

local function Timer0Advance(rate, startTime, endTime, clampToEnd, autoAdvance)
    sh.cutscene.timer_advance("cutscene_timer0", rate, startTime, endTime, clampToEnd, autoAdvance)
end

local function Timer0Set(v)
    sh.map.set("cutscene_timer0", v)
end

local antiqueShopCybilSteps = {}

antiqueShopCybilSteps[0] = function()
    sh.map.set("msg_audio_idx", 0)
    sh.map.set("update_sibyl", 0)
    sh.load.file("ANIM/AQSH1.DMS", 11) -- AQSH1.DMS
    sh.load.wait()
    sh.cutscene.prepare(11)
    Timer0Set(0)
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.cutscene.border_show()
    sh.cutscene_flag(true)
    sh.water_effect_enable()
    sh.light.bone_detach()
    sh.light.lens_flare_detach()
    sh.light.set_intensity(4096) -- 1.0
    sh.event_step_increment(0)
    return antiqueShopCybilSteps[1]()
end

antiqueShopCybilSteps[1] = function()
    sh.event_fade_cmd(2, false, 0, 0, false) -- Auto
end

antiqueShopCybilSteps[2] = function()
    sh.npc.process_loads()
    sh.event_step_increment(0)
    return antiqueShopCybilSteps[3]()
end

antiqueShopCybilSteps[3] = function()
    MsgAudio("map4_s01.whats_this")
end

antiqueShopCybilSteps[4] = function()
    sh.sfx_flags_play("door_open_0", 745472, -4915, 241254, 128, 0) -- door open 0 sound at (182.0, -1.2, 58.9), 0.5
    sh.event_step_increment(0)
    return antiqueShopCybilSteps[5]()
end

antiqueShopCybilSteps[5] = function()
    sh.event_wait_timer(2048, false) -- 0.5 s
end

antiqueShopCybilSteps[6] = function()
    sh.event_chara_anim(0, 0, "scene_antique_shop_1", false)
    sh.sfx_flags_play("door_close_0", 745472, -4915, 241254, 128, 0) -- door close 0 sound
    sh.event_step_increment(0)
    return antiqueShopCybilSteps[7]()
end

antiqueShopCybilSteps[7] = function()
    sh.event_wait_timer(4096, false) -- 1.0 s
end

antiqueShopCybilSteps[8] = function()
    sh.npc.spawn("Cybil", 0, 183 * 4096, 61 * 4096, 0, 3) -- Cybil
    sh.map.set("update_sibyl", 1)
    sh.event_chara_anim(0, 1, "stand_talk", false)
    sh.held_item_show()
    sh.event_step_increment(0)
    return antiqueShopCybilSteps[9]()
end

antiqueShopCybilSteps[9] = function()
    sh.event_chara_anim(1, 0, 0, false) -- AwaitAnimEnd
    Timer0Advance(10 * 4096, 0, 28 * 4096, true, false)
end

antiqueShopCybilSteps[10] = function()
    MsgAudio("map4_s01.harry")
    Timer0Advance(10 * 4096, 0, 28 * 4096, true, false)
end

antiqueShopCybilSteps[11] = function()
    sh.chara_anim_play_to_end(0, "scene_antique_shop_2")
end

antiqueShopCybilSteps[12] = function()
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_chara_anim(0, 1, "walk", false)
    sh.held_item_hide()
    sh.flag_set("M4S01_CybilWalksToHarry", true)
    sh.event_step_increment(0)
    return antiqueShopCybilSteps[13]()
end

antiqueShopCybilSteps[13] = function()
    MsgAudio("map4_s01.im_glad_youre_ok")
    Timer0Advance(16 * 4096, 29 * 4096, 56 * 4096, true, true)
end

antiqueShopCybilSteps[14] = function()
    sh.event_chara_anim(0, 1, "stand_talk", false)
    Timer0Set(57 * 4096)
    sh.light.set_intensity(2867) -- 0.7
    sh.event_step_increment(0)
    return antiqueShopCybilSteps[15]()
end

antiqueShopCybilSteps[15] = function()
    MsgAudio("map4_s01.im_glad_youre_ok")
end

antiqueShopCybilSteps[16] = function()
    MsgAudio("map4_s01.what_are_you_doing_here")
end

antiqueShopCybilSteps[17] = function()
    MsgAudio("map4_s01.what_about_my_daughter_did")
end

antiqueShopCybilSteps[18] = function()
    Timer0Set(58 * 4096)
    sh.light.set_intensity(4096)
    MsgAudio("map4_s01.and_you_just_let_her")
end

antiqueShopCybilSteps[19] = function()
    MsgAudio("map4_s01.what_so_then_cheryl")
end

antiqueShopCybilSteps[20] = function()
    sh.event_wait_timer(6144, false) -- 1.5 s
end

antiqueShopCybilSteps[21] = function()
    MsgAudio("map4_s01.what_about_you_anything")
end

antiqueShopCybilSteps[22] = function()
    Timer0Set(59 * 4096)
    sh.light.set_intensity(2867) -- 0.7
    MsgAudio("map4_s01.she_said_something_about_the")
end

antiqueShopCybilSteps[23] = function()
    MsgAudio("map4_s01.darkness_devouring_the_town")
end

antiqueShopCybilSteps[24] = function()
    sh.event_wait_timer(6144, false) -- 1.5 s
end

antiqueShopCybilSteps[25] = function()
    sh.event_chara_anim(0, 0, "cutscene_react", false)
    sh.event_chara_anim(0, 1, "walk", false)
    sh.event_step_increment(0)
    return antiqueShopCybilSteps[26]()
end

antiqueShopCybilSteps[26] = function()
    Timer0Advance(10 * 4096, 59 * 4096, 64 * 4096, true, true)
end

antiqueShopCybilSteps[27] = function()
    Timer0Advance(16 * 4096, 65 * 4096, 83 * 4096, true, true)
    sh.light.set_intensity(4096)
end

antiqueShopCybilSteps[28] = function()
    sh.event_chara_anim(3, 0, 0, false) -- AnimUnlock
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_chara_anim(0, 1, "stand_talk", false)
    Timer0Set(87 * 4096)
    sh.light.set_intensity(2048) -- 0.5
    sh.event_step_increment(0)
    return antiqueShopCybilSteps[29]()
end

antiqueShopCybilSteps[29] = function()
    MsgAudio("map4_s01.whats_this_2") -- 0x3D
end

antiqueShopCybilSteps[30] = function()
    sh.event_chara_anim(0, 0, "scene_antique_shop_5", false)
    sh.light.set_intensity(4096)
    sh.event_step_increment(0)
    return antiqueShopCybilSteps[31]()
end

antiqueShopCybilSteps[31] = function()
    Timer0Set(88 * 4096)
    MsgAudio("map4_s01.okay")
end

antiqueShopCybilSteps[32] = function()
    sh.event_wait_timer(3276, false) -- 0.8 s
end

antiqueShopCybilSteps[33] = function()
    MsgAudio("map4_s01.cybil_2")
    Timer0Advance(10 * 4096, 88 * 4096, 121 * 4096, true, false)
end

antiqueShopCybilSteps[34] = function()
    Timer0Advance(10 * 4096, 88 * 4096, 121 * 4096, true, true)
end

antiqueShopCybilSteps[35] = function()
    Timer0Set(122 * 4096)
    sh.map.set("cube_model_variant", 1)
    sh.light.set_intensity(4915) -- 1.2
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_step_increment(0)
    return antiqueShopCybilSteps[36]()
end

antiqueShopCybilSteps[36] = function()
    MsgAudio("map4_s01.do_you_know_anything_about")
end

antiqueShopCybilSteps[37] = function()
    MsgAudio("map4_s01.oh")
end

antiqueShopCybilSteps[38] = function()
    sh.event_chara_anim(0, 0, "scene_antique_shop_4", false)
    sh.map.set("cube_model_variant", 0)
    sh.light.set_intensity(4096)
    sh.event_step_increment(0)
    return antiqueShopCybilSteps[39]()
end

antiqueShopCybilSteps[39] = function()
    MsgAudio("map4_s01.yeah_maybe")
    Timer0Advance(10 * 4096, 123 * 4096, 139 * 4096, true, false)
end

antiqueShopCybilSteps[40] = function()
    sh.chara_anim_play_to_end(0, "scene_antique_shop_3") -- 0x78
    sh.light.set_intensity(5734) -- 1.4
    sh.flag_set("M4S01_CutsceneWallShown", true)
    Timer0Advance(10 * 4096, 140 * 4096, 165 * 4096, true, false)
end

antiqueShopCybilSteps[41] = function()
    sh.player.weapon_restore()
    sh.player.held_item_prev()
    sh.player.weapon_stash()
    sh.event_step_increment(0)
    return antiqueShopCybilSteps[42]()
end

antiqueShopCybilSteps[42] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto
    if sh.event_step_get(0) ~= 42 then
        sh.player.weapon_restore()
        sh.event_step_reset()
    end
end

antiqueShopCybilSteps[43] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

antiqueShopCybilSteps[44] = function()
    sh.player.weapon_restore()
    sh.player.held_item_prev()
    sh.event_step_increment(0)
    return antiqueShopCybilSteps.default()
end

antiqueShopCybilSteps.default = function()
    sh.player_freeze(false, false)
    Timer0Set(-1) -- none
    sh.state_set(0) -- back to gameplay
    sh.map.set("cube_model_variant", 0)
    sh.npc.clear(1, 0, 0)
    sh.load.wait()
    sh.held_item_show()
    sh.sd_call("stop_xa_audio")
    sh.flag_set("M4S01_AntiqueShopCybilCutsceneDone", true)
    sh.water_effect_disable()
    sh.light.flashlight_fix()
    sh.light.set_intensity(4096)
    sh.flag_set("M4S01_CutsceneWallShown", false)
    sh.cutscene.border_reset()
end

function Cutscene_AntiqueShopCybil()
    -- Skip.
    local s = sh.event_step_get(0)
    if sh.input_action("skip", 1) and s >= 3 and s < 41 then
        sh.event.step_set(0, 43) -- skip
        s = 43
    end

    runStep(antiqueShopCybilSteps, s)
    local t = sh.map.get("cutscene_timer0")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, 11)
        if sh.map.get("update_sibyl") ~= 0 then
            sh.cutscene.chara(1, "SIBYL", t, 11)
        end
        sh.cutscene.camera(t, 11, "camera_pos", "camera_look")
        sh.cutscene.light(t, 11)
    end
end

-- index 4: AQRM.DMS cutscene, Harry climbs down the hole
-- (steps 0-10 + default, DMS HERO + camera).
local function Timer1Advance(rate, startTime, endTime, clampToEnd, autoAdvance)
    sh.cutscene.timer_advance("cutscene_timer1", rate, startTime, endTime, clampToEnd, autoAdvance)
end

local holeRoomDescentSteps = {}

holeRoomDescentSteps[0] = function()
    sh.player_freeze(true)
    sh.load.file("ANIM/AQRM.DMS", 11) -- AQRM.DMS
    sh.load.wait()
    sh.cutscene.prepare(11)
    sh.cutscene.border_show()
    sh.screen_fade_reset_timestep()
    sh.map.set("msg_audio_idx0", 0)
    sh.map.set("cutscene_timer1", 0)
    sh.cutscene_flag(true)
    sh.event_chara_anim(0, 0, "scene_hole_room", false)
    sh.light.flashlight_on()
    sh.player.weapon_stash()
    sh.held_item_hide()
    sh.event_step_increment(0)
    sh.cutscene.border_show()
    sh.cutscene_flag(true)
end

holeRoomDescentSteps[1] = function()
    sh.event_fade_cmd(0, false, 0, 0, false) -- Start
    sh.event_step_increment(0)
    return holeRoomDescentSteps[2]()
end

holeRoomDescentSteps[2] = function()
    sh.event_step_increment(0)
    return holeRoomDescentSteps[3]()
end

holeRoomDescentSteps[3] = function()
    Timer1Advance(10 * 4096, 0, 34 * 4096, true, true)
end

holeRoomDescentSteps[4] = function()
    sh.chara_anim_play_to_end(0, "scene_alessa_confrontation")
    Timer1Advance(10 * 4096, 35 * 4096, 71 * 4096, true, false)
end

holeRoomDescentSteps[5] = function()
    sh.event_chara_anim(0, 0, "walk_forward", false)
    sh.event_step_increment(0)
    return holeRoomDescentSteps[6]()
end

holeRoomDescentSteps[6] = function()
    Timer1Advance(15 * 4096, 72 * 4096, 93 * 4096, true, true)
end

holeRoomDescentSteps[7] = function()
    sh.event_step_increment(0)
end

holeRoomDescentSteps[8] = function()
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.map.set("cutscene_timer1", 95 * 4096)
    sh.event_step_increment(0)
    return holeRoomDescentSteps[9]()
end

holeRoomDescentSteps[9] = function()
    sh.msg.show_with_audio("map4_s01.whats_this_some_kind_of", "msg_audio_idx0", "msg_audio_cmds0") -- 0x5B
end

holeRoomDescentSteps[10] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto
    if sh.event_step_get(0) ~= 10 then
        sh.map.set("cutscene_timer1", 95 * 4096)
    end
end

holeRoomDescentSteps.default = function()
    sh.map.set("cutscene_timer1", -1) -- none
    sh.player_freeze(false, false)
    sh.state_set(0) -- back to gameplay
    sh.flag_set("M4S01_HoleDescentCutsceneDone", true)
    sh.camera_auto_return(true)
    sh.load.wait()
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.sd_call("stop_xa_audio")
    sh.player.weapon_restore()
    sh.held_item_show()
end

function Cutscene_HoleRoomDescent()
    -- Skip.
    local s = sh.event_step_get(0)
    if sh.input_action("skip", 1) and s >= 3 and s < 10 then
        sh.event.step_set(0, 10) -- skip
        s = 10
    end

    runStep(holeRoomDescentSteps, s)
    local t = sh.map.get("cutscene_timer1")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, 11)
        sh.cutscene.camera(t, 11, "camera_pos", "camera_look")
    end
end

-- index 5: Harry blacks out in the hole room and Cybil finds him (AQRM.DMS frames 96-259 in
-- file buffer 11, its own clock cutscene_timer3; steps 0-30 + default, DMS HERO/SIBYL/LIGHT).
-- AQRM.DMS is loaded here too: the scene before (index 4) is not the only way into the room.
-- While Harry passes out the fire particles burn around him, the scene hum plays at the
-- hole (quieter the brighter the fire) and every frame is drawn over the next at half blend
-- (the ghosting); flag M4S01_CybilFindsHarryCutsceneDone on the way out.
local function Timer3Advance(rate, startTime, endTime, clampToEnd, autoAdvance)
    sh.cutscene.timer_advance("cutscene_timer3", rate, startTime, endTime, clampToEnd, autoAdvance)
end

local function MsgAudio2(msgKey)
    sh.msg.show_with_audio(msgKey, "msg_audio_idx2", "msg_audio_cmds2")
end

local holeFrameHold = false -- ghosting on
local holeHumOn = false     -- the scene hum follows the fire
local holeGlow = 0          -- 0..4096; the scene writes it, nothing reads it

local function HoleEnvAdvance()
    sh.map.set("hole_env_level", sh.map.get("hole_env_level") + (sh.delta_time_get() * 49152) // 4096) -- dt * 12.0
end

local function HoleGlowRise()
    holeGlow = holeGlow + (sh.delta_time_get() * 6144) // 4096 -- dt * 1.5
    if holeGlow > 4096 then
        holeGlow = 4096
    end
end

local function EffectSlotsClear()
    for i = 449, 0, -1 do
        sh.map.set("effect_slot_type", 0, i)
    end
end

local cybilFindsHarrySteps = {}

cybilFindsHarrySteps[0] = function()
    sh.player_freeze(true)
    sh.load.file("ANIM/AQRM.DMS", 11)
    sh.load.wait()
    sh.cutscene.prepare(11)
    holeFrameHold = false
    sh.cutscene.border_show()
    sh.map.set("cutscene_timer3", 393216) -- frame 96.0

    sh.map.set("particle_emitter.state", 3)
    sh.map.set("particle_emitter.flags", 0x11)
    sh.map.set("particle_emitter.spawn_count", 0xFA)
    sh.map.set("particle_emitter.top_y", -0x2333)
    sh.map.set("particle_emitter.bottom_y", -0x1333)
    sh.map.set("particle_emitter.radius", 0x400)
    sh.map.set("particle_emitter.unread_d", -0x800)
    sh.map.set("particle_emitter.spread", 0x40)
    sh.map.set("particle_emitter.sway_divisor", 0x80)
    sh.map.set("particle_emitter.unread_b", 0xFF)
    sh.map.set("particle_emitter.drift_speed", 0x80)
    sh.map.set("particle_emitter.center_x", 0xB04B0) -- 176.29 m
    sh.map.set("particle_emitter.center_z", 0x132CE) -- 19.18 m
    sh.map.set("particle_emitter.unread_c", 0x14)
    sh.map.set("particle_emitter.spin_speed", 0x1000)
    sh.map.set("particle_emitter.unread_a", 8)

    sh.cutscene_flag(true)
    sh.map.particle_emitter_start()

    sh.light.bone_detach()
    sh.light.set_pos_x(sh.map.get("particle_emitter.center_x"))
    sh.light.set_pos_y(-8192) -- -2.0 m
    sh.light.set_pos_z(sh.map.get("particle_emitter.center_z") + 6144) -- + 1.5 m
    sh.light.lens_flare_detach()
    sh.light.set_rot_x(-1024) -- -90.0 deg
    sh.light.set_rot_y(0)
    sh.light.set_rot_z(0)

    sh.map_effects_update(11, 11, 5, "hole_env_level", 0, 409600) -- 32-bit value, 100.0
    sh.map.set("hole_env_level", 0)
    sh.water_effect_enable()

    sh.light.set_intensity(10240) -- 2.5
    sh.npc.set_pos_x(0, 737280) -- 180.0 m
    sh.npc.set_pos_z(0, 245760) -- 60.0 m

    sh.load.close_range_chunks()
    sh.sd_call("antique_shop_hole_hum")

    holeGlow = 819 -- 0.2
    holeHumOn = true

    sh.flag_set("M4S01_HoleDescentStarted", true)

    sh.held_item_hide()
    sh.player.weapon_stash()
    sh.event_step_increment(0)
    return cybilFindsHarrySteps[1]()
end

cybilFindsHarrySteps[1] = function()
    sh.event_chara_anim(0, 0, "throw_alcohol_alt", false)
    holeFrameHold = true
    sh.event_step_increment(0)
    return cybilFindsHarrySteps[2]()
end

cybilFindsHarrySteps[2] = function()
    HoleEnvAdvance()
    MsgAudio2("map4_s01.msg95")
    Timer3Advance(20480, 393216, 471040, true, false) -- 5.0, 96.0 .. 115.0
    HoleGlowRise()
end

cybilFindsHarrySteps[3] = function()
    Timer3Advance(20480, 393216, 471040, true, true)
    HoleEnvAdvance()
    HoleGlowRise()
end

cybilFindsHarrySteps[4] = function()
    sh.event_step_increment(0)
    return cybilFindsHarrySteps[5]()
end

cybilFindsHarrySteps[5] = function()
    sh.event_step_increment(0)
    return cybilFindsHarrySteps[6]()
end

cybilFindsHarrySteps[6] = function()
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_step_increment(0)
    return cybilFindsHarrySteps[7]()
end

cybilFindsHarrySteps[7] = function()
    sh.map.set("cutscene_timer3", 569344) -- frame 139.0
    HoleEnvAdvance()
    if sh.map.get("hole_env_level") >= 409600 then -- 100.0
        sh.event_step_increment(0)
    end
end

cybilFindsHarrySteps[8] = function()
    holeGlow = holeGlow - (sh.delta_time_get() * 3276) // 4096 -- dt * 0.8
    if holeGlow < 0 then
        holeGlow = 0
    end
    sh.event_fade_cmd(2, true, 0, 2048, false) -- Auto, out, 0.5 s
end

cybilFindsHarrySteps[9] = function()
    holeFrameHold = false
    EffectSlotsClear()
    holeHumOn = false
    sh.sfx_stop("antique_shop_hole_hum")
    sh.map.set("cutscene_timer3", 573440) -- frame 140.0
    sh.world.effects_update(7, 4)
    sh.light.bone_detach()
    sh.light.lens_flare_detach()
    sh.light.set_intensity(4096) -- 1.0
    sh.light.set_pos_x(732364) -- 178.8 m
    sh.light.set_pos_y(0)
    sh.light.set_pos_z(257228) -- 62.8 m
    sh.light.set_rot_x(341) -- 30.0 deg
    sh.light.set_rot_y(-1024) -- -90.0 deg
    sh.light.set_rot_z(0)
    sh.event_step_increment(0)
end

cybilFindsHarrySteps[10] = function()
    sh.npc.spawn("Cybil", 0, 749568, 249856, 0, 3) -- (183.0, 61.0) m
    sh.npc.anim_flags_clear(0, 2)
    sh.event_step_increment(0)
end

cybilFindsHarrySteps[11] = function()
    sh.event_chara_anim(0, 1, "scene_pose_b", false)
    sh.event_fade_cmd(0, false, 0, 0, false) -- Start, in
    sh.load.wait()
    sh.event_step_increment(0)
end

cybilFindsHarrySteps[12] = function()
    sh.event_chara_anim(2, 1, 0, false) -- AnimLock
    sh.event_wait_timer(4096, false) -- 1.0 s
end

cybilFindsHarrySteps[13] = function()
    sh.event_chara_anim(3, 1, 0, false) -- AnimUnlock
    sh.event_step_increment(0)
    return cybilFindsHarrySteps[14]()
end

cybilFindsHarrySteps[14] = function()
    MsgAudio2("map4_s01.harry_are_you_ok")
    Timer3Advance(40960, 573440, 819200, true, false) -- 10.0, 140.0 .. 200.0
end

cybilFindsHarrySteps[15] = function()
    Timer3Advance(40960, 573440, 819200, true, true)
end

cybilFindsHarrySteps[16] = function()
    sh.event_chara_anim(0, 1, "walk_back", false)
    sh.event_step_increment(0)
    return cybilFindsHarrySteps[17]()
end

cybilFindsHarrySteps[17] = function()
    MsgAudio2("map4_s01.harry_3")
    Timer3Advance(8192, 823296, 884736, true, false) -- 2.0, 201.0 .. 216.0
end

cybilFindsHarrySteps[18] = function()
    Timer3Advance(8192, 823296, 884736, true, true)
end

cybilFindsHarrySteps[19] = function()
    sh.event_chara_anim(1, 1, 0, false) -- AwaitAnimEnd
end

cybilFindsHarrySteps[20] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
end

cybilFindsHarrySteps[21] = function()
    sh.event_chara_anim(0, 1, "walk_out", false)
    sh.light.set_pos_x(720486) -- 175.9 m
    sh.light.set_pos_y(-4096) -- -1.0 m
    sh.light.set_pos_z(93388) -- 22.8 m
    sh.light.set_rot_x(0)
    sh.light.set_rot_y(682) -- 60.0 deg
    sh.light.set_rot_z(0)
    sh.light.set_intensity(4096)
    sh.map.set("cutscene_timer3", 888832) -- frame 217.0
    sh.event_step_increment(0)
end

cybilFindsHarrySteps[22] = function()
    sh.event_chara_anim(2, 1, 0, false) -- AnimLock
    sh.event_fade_cmd(0, false, 0, 0, false) -- Start, in
    sh.load.wait()
    sh.event_step_increment(0)
    return cybilFindsHarrySteps[23]()
end

cybilFindsHarrySteps[23] = function()
    sh.event_wait_timer(1228, false) -- 0.3 s
end

cybilFindsHarrySteps[24] = function()
    sh.event_chara_anim(3, 1, 0, false) -- AnimUnlock
    sh.event_step_increment(0)
    return cybilFindsHarrySteps[25]()
end

cybilFindsHarrySteps[25] = function()
    MsgAudio2("map4_s01.harry_4")
    Timer3Advance(32768, 888832, 1060864, true, false) -- 8.0, 217.0 .. 259.0
end

cybilFindsHarrySteps[26] = function()
    Timer3Advance(32768, 888832, 1060864, true, true)
end

cybilFindsHarrySteps[27] = function()
    sh.event_wait_timer(5734, false) -- 1.4 s
end

cybilFindsHarrySteps[28] = function()
    sh.event_fade_cmd(2, true, 0, 2048, false) -- Auto, out, 0.5 s
end

cybilFindsHarrySteps[29] = function()
    sh.event.step_set(0, -1)
end

cybilFindsHarrySteps[30] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out (the skip)
end

cybilFindsHarrySteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0) -- back to gameplay
    holeFrameHold = false
    EffectSlotsClear()
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.map.set("cutscene_timer3", -1) -- none
    sh.sd_call("stop_xa_audio")
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.flag_set("M4S01_CybilFindsHarryCutsceneDone", true)
    sh.npc.anim_flags_set(0, 2)
    sh.light.set_intensity(4096)
    sh.held_item_show()
    sh.player.weapon_restore()
end

function Cutscene_CybilFindsHarry()
    -- Skip.
    local s = sh.event_step_get(0)
    if sh.input_action("skip", 1) and s >= 2 and s < 29 then
        sh.event.step_set(0, 30)
        s = 30
    end

    runStep(cybilFindsHarrySteps, s)
    if holeHumOn then
        -- 0.5 at no fire, silent at full fire (the emitter's intensity, 4096 = 1.0); falloff 16.0 m.
        local volume = 128 - (sh.map.get("particle_emitter.intensity") * 128) // 4096
        sh.sfx_falloff_pitch_play("antique_shop_hole_hum", 722124, -6144, 78561, volume, 65536, 0) -- at (176.3, -1.5, 19.18) m
    end

    local t = sh.map.get("cutscene_timer3")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, 11)
        if sh.event_step_get(0) >= 10 then
            sh.cutscene.chara(1, "SIBYL", t, 11)
        end
        sh.cutscene.camera(t, 11, "camera_pos", "camera_look")
        if sh.event_step_get(0) >= 21 then
            sh.cutscene.light(t, 11)
        end
    end

    if holeFrameHold then
        sh.render.frame_hold{source = "previous", tint = 128, blend = "half", layer = "overlay"}
    end
end

-- index 6: pushing the cabinet in the antique shop (msg map4_s01.there_is_a_trace_on
-- "Do you want to push the cabinet?", flag M4S01_CabinetPushed).
-- "cabinet" is the pushed cabinet's placement in the map state (sh.map.get("cabinet.pos.z"));
-- cabinet_slide_volume is the slide sound volume (4096 = 1.0).
local function CabinetSfx()
    sh.sound.play_falloff("cabinet_slide", "cabinet.pos",
            sh.map.get("cabinet_slide_volume") // 32, 49152, 0) -- sound cabinet_slide, volume / 32, falloff 12.0
end

local pushCabinetSteps = {}

pushCabinetSteps[0] = function()
    sh.player_freeze(true)
    sh.event_step_increment(0)
    return pushCabinetSteps[1]()
end

pushCabinetSteps[1] = function()
    sh.event.wait_player_stop()
end

pushCabinetSteps[2] = function()
    sh.msg.show(true, "map4_s01.there_is_a_trace_on", 3, -1, 0, false) -- "Do you want to push the cabinet?"
end

pushCabinetSteps[3] = function()
    sh.event_fade_cmd(0, true, 2, 0, false) -- Start, type 2
    sh.event_step_increment(0)
    return pushCabinetSteps[4]()
end

pushCabinetSteps[4] = function()
    sh.event_wait_timer(6144, false) -- 1.5 s
end

pushCabinetSteps[5] = function()
    sh.camera.position(nil, 734617, -6144, 249896, 0, 0, 0, 0, true) -- 179.35, -1.5, 61.0098 m
    sh.camera.look_at(nil, 724910, -2007, 262430, 0, 0, 0, 0, true)   -- 176.98, -0.49, 64.07 m
    sh.sd_call("cabinet_slide")
    sh.event_chara_anim(0, 0, "push_object", false) -- SetState
    sh.npc.set_pos_x(0, 730316) -- 178.3 m
    sh.npc.set_pos_z(0, 253542) -- 61.9 m
    sh.npc.set_rot_y(0, 0)      -- 0.0 deg
    sh.held_item_hide()
    sh.player.weapon_stash()
    sh.player.held_item_set("Handgun")
    sh.npc.load(0, "Cybil", "npc_bone:0", 255, nil, nil) -- Cybil, free all character slots
    sh.flag_set("M4S01_CabinetPushed", true)
    sh.event_step_increment(0)
    return pushCabinetSteps[6]()
end

pushCabinetSteps[6] = function()
    local tmp = (sh.delta_time_get() * 901) // 4096 -- 0.22 m/s
    sh.npc.set_pos_z(0, sh.npc.pos_z(0) + tmp)
    sh.map.set("cabinet.pos.z", sh.map.get("cabinet.pos.z") + tmp)

    local vol = sh.map.get("cabinet_slide_volume") + (sh.delta_time_get() * 8192) // 4096 -- 2.0 s
    sh.map.set("cabinet_slide_volume", vol)
    if sh.map.get("cabinet_slide_volume") > 4096 then
        sh.map.set("cabinet_slide_volume", 4096) -- 1.0
    end

    local z = sh.map.get("cabinet.pos.z")
    if z > 260839 then -- 63.6814 m
        tmp = z - 260839
        sh.npc.set_pos_z(0, sh.npc.pos_z(0) - tmp)
        sh.map.set("cabinet.pos.z", z - tmp)
        sh.event_step_increment(0)
    end

    CabinetSfx()
end

pushCabinetSteps[7] = function()
    local vol = sh.map.get("cabinet_slide_volume") - (sh.delta_time_get() * 8192) // 4096
    sh.map.set("cabinet_slide_volume", vol)
    if sh.map.get("cabinet_slide_volume") < 0 then
        sh.map.set("cabinet_slide_volume", 0)
    end
    sh.event_fade_cmd(2, true, 0, 12288, false) -- Auto, 3.0
    CabinetSfx()
end

pushCabinetSteps.default = function()
    -- Return to gameplay.
    if sh.flag_get("M4S01_CabinetPushed") then
        sh.sfx_stop("cabinet_slide")
        sh.state_set(0) -- back to gameplay
        sh.event_fade_cmd(0, false, 2, 0, false)
        return
    end
    sh.player_freeze(false, false)
    sh.state_set(0)
end

function MapEvent_PushCabinet()
    local s = sh.event_step_get(0)
    runStep(pushCabinetSteps, s)
end

-- index 7: SQSU.DMS cutscene (steps 0-14 + default,
-- DMS HERO + camera; water effect and fixed flashlight).
local function Timer2Advance(rate, startTime, endTime, clampToEnd, autoAdvance)
    sh.cutscene.timer_advance("cutscene_timer2", rate, startTime, endTime, clampToEnd, autoAdvance)
end

local function MsgAudio1(msgIdx)
    sh.msg.show_with_audio(msgIdx, "msg_audio_idx1", "msg_audio_cmds1")
end

local sqsuSceneSteps = {}

sqsuSceneSteps[0] = function()
    sh.player_freeze(true)
    sh.cutscene.border_show()
    sh.screen_fade_reset_timestep()
    sh.cutscene_flag(true)
    sh.load.file("ANIM/SQSU.DMS", 11) -- SQSU.DMS
    sh.load.wait()
    sh.cutscene.prepare(11)
    sh.map.set("cutscene_timer2", 0)
    sh.map.set("msg_audio_idx1", 0)
    sh.event_chara_anim(0, 0, "scene_sqsu_1", false)
    sh.event_chara_anim(2, 0, 0, false) -- AnimLock
    sh.water_effect_enable()
    sh.light.bone_detach()
    sh.light.set_pos_x(80281) -- 19.6 m
    sh.light.set_pos_y(2 * 4096)
    sh.light.set_pos_z(62 * 4096)
    sh.light.lens_flare_detach()
    sh.light.set_rot_x(1024) -- 90.0 deg
    sh.light.set_rot_y(0)
    sh.light.set_rot_z(0)
    sh.light.set_intensity(4096)
    sh.world.effects_update(14, 14)
    sh.held_item_hide()
    sh.player.weapon_stash()
    sh.event_step_increment(0)
    return sqsuSceneSteps[1]()
end

sqsuSceneSteps[1] = function()
    sh.event_fade_cmd(2, false, 0, 2048, false) -- Auto, 0.5
end

sqsuSceneSteps[2] = function()
    sh.event_chara_anim(3, 0, 0, false) -- AnimUnlock
    MsgAudio1("map4_s01.was_that_another_dream")
    Timer2Advance(10 * 4096, 0, 106 * 4096, true, false)
end

sqsuSceneSteps[3] = function()
    sh.event_wait_timer(6144, false) -- 1.5 s
    Timer2Advance(10 * 4096, 0, 106 * 4096, true, false)
end

sqsuSceneSteps[4] = function()
    MsgAudio1("map4_s01.did_i_pass_out_again")
    Timer2Advance(10 * 4096, 0, 106 * 4096, true, false)
end

sqsuSceneSteps[5] = function()
    Timer2Advance(10 * 4096, 0, 106 * 4096, true, true)
end

sqsuSceneSteps[6] = function()
    sh.event_chara_anim(0, 0, "scene_sqsu_2", false)
    sh.event_step_increment(0)
    return sqsuSceneSteps[7]()
end

sqsuSceneSteps[7] = function()
    Timer2Advance(10 * 4096, 107 * 4096, 128 * 4096, true, true)
end

sqsuSceneSteps[8] = function()
    sh.msg.show(false, "map4_s01.i_dont_want_to_think", 0, 0, 0, false)
    Timer2Advance(15564, 129 * 4096, 228 * 4096, true, false) -- 3.8 s
end

sqsuSceneSteps[9] = function()
    sh.event_wait_timer(6144, false) -- 1.5 s
    Timer2Advance(15564, 129 * 4096, 228 * 4096, true, false)
end

sqsuSceneSteps[10] = function()
    sh.msg.show(false, "map4_s01.msg108", 0, 0, 0, false)
    Timer2Advance(15564, 129 * 4096, 228 * 4096, true, false)
end

sqsuSceneSteps[11] = function()
    sh.event_wait_timer(2457, false) -- 0.6 s
    Timer2Advance(15564, 129 * 4096, 228 * 4096, true, false)
end

sqsuSceneSteps[12] = function()
    sh.event_fade_cmd(2, true, 0, 4096, false) -- Auto, 1.0
    Timer2Advance(15564, 129 * 4096, 228 * 4096, true, false)
end

sqsuSceneSteps[13] = function()
    sh.event_step_reset()
end

sqsuSceneSteps[14] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

sqsuSceneSteps.default = function()
    sh.player_freeze(false, true)
    sh.state_set(0) -- back to gameplay
    sh.flag_set("M4S01_AwakeningCutsceneDone", true)
    sh.camera_auto_return(true)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.world.effects_update(7, 4)
    sh.water_effect_disable()
    sh.light.flashlight_fix()
    sh.light.set_intensity(4096)
    sh.light.flashlight_on()
    sh.map.set("cutscene_timer2", -1) -- none
    sh.held_item_show()
    sh.player.weapon_restore()
end

function Cutscene_SqsuScene()
    -- Skip: only the main step is set (the sub-steps are not reset).
    local s = sh.event_step_get(0)
    if sh.input_action("skip", 1) and s >= 2 and s < 13 then
        sh.sd_call("stop_xa_audio")
        sh.event_step_set(0, 14) -- skip
        s = 14
    end

    runStep(sqsuSceneSteps, s)
    local t = sh.map.get("cutscene_timer2")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, 11)
        sh.cutscene.camera(t, 11, "camera_pos", "camera_look")
    end
end

-- index 8: item pickup.
function MapEvent_AxeTake()
    sh.event_item_take("Axe", 1, "M4S01_PickupAxe", "map4_s01.there_is_an_axe_take")
end

-- index 9: message over a background picture.
function MapEvent_WallClockInspect()
    sh.event_msg_with_bg("TIM/CLOCKATQ.TIM", 0, 0, "map4_s01.there_is_a_clock_nothing")
end
