-- map6_s01: Boat at Lakeside Pier
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

-- index 2: common pickups, chosen by the event's point of interest.
function MapEvent_CommonItemTake()
    local poi = sh.event_point()
    local t = {
        [13] = {"RifleShells","M6S01_RifleShells"},
        [14] = {"HandgunBullets","M6S01_HandgunBullets"},
        [15] = {"HealthDrink","M6S01_HealthDrink"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point gives a first aid kit and sets no flag
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- index 3: lighthouse cutscene SIP.DMS: Cybil and Dahlia.
-- Map state: msg_audio_idx0/msg_audio_cmds0 (voice lines), update_sibyl/update_daria
-- (read by the cutscene update below), lighthouse_door_hidden (read by other map code).
local function LhSfx(id, p) -- sound at one of two fixed spots, volume 0.5
    if p == 0 then sh.sfx_flags_play(id, -87244, -4915, 240435, 128, 0) -- pos0 (-21.3, -1.2, 58.7)
    else sh.sfx_flags_play(id, -81715, -4915, 248627, 128, 0) end      -- pos1 (-19.95, -1.2, 60.7)
end
local function LhMsg(m) sh.msg.show_with_audio(m, "msg_audio_idx0", "msg_audio_cmds0") end
local function LhAdv(r, a, b, c, d) sh.cutscene.timer_advance("cutscene_timer", r, a, b, c, d) end
local function LhTimer(v) sh.map.set("cutscene_timer", v) end

local lighthouseCybilDahliaSteps = {}

lighthouseCybilDahliaSteps[0] = function()
    sh.player_freeze(true)
    sh.cutscene.border_show()
    sh.screen_fade_reset_timestep()
    sh.cutscene_flag(true)
    sh.npc.load(0, "Cybil", "npc_bone:0", 255, nil, nil) -- Cybil
    sh.load.file("ANIM/SIP.DMS", 15) -- SIP.DMS
    sh.npc.process_loads()
    sh.npc.spawn("Cybil", 0, -77824, 237568, 0, 3) -- (-19.0, 58.0)
    sh.cutscene.prepare(15)
    LhTimer(0)
    sh.map.set("msg_audio_idx0", 0)
    sh.map.set("update_sibyl", 1)
    sh.map.set("update_daria", 0)
    sh.held_item_hide()
    sh.player.weapon_stash()
    sh.light.bone_detach()
    sh.light.lens_flare_detach()
    sh.light.set_intensity(2457) -- 0.6
    sh.world.effects_update(16, 16)
    sh.water_effect_enable()
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[1]()
end

lighthouseCybilDahliaSteps[1] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
end

lighthouseCybilDahliaSteps[2] = function()
    sh.event_chara_anim(0, 0, "scene_dialogue_1", false)
    sh.event_chara_anim(0, 1, "sit_slump", false)
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[3]()
end

lighthouseCybilDahliaSteps[3] = function()
    LhMsg("map6_s01.cybil")
    LhAdv(38912, 0, 86016, true, false) -- 9.5, 0, 21
end

lighthouseCybilDahliaSteps[4] = function()
    LhAdv(38912, 0, 86016, true, true)
end

lighthouseCybilDahliaSteps[5] = function()
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_chara_anim(0, 1, "stand_talk", false)
    LhTimer(90112) -- 22
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[6]()
end

lighthouseCybilDahliaSteps[6] = function()
    LhMsg("map6_s01.harry")
end

lighthouseCybilDahliaSteps[7] = function()
    LhMsg("map6_s01.you_were_worried_where_did")
end

lighthouseCybilDahliaSteps[8] = function()
    LhTimer(94208) -- 23
    LhMsg("map6_s01.this_may_sound_really_off")
end

lighthouseCybilDahliaSteps[9] = function()
    LhMsg("map6_s01.at_first_i_thought_i")
end

lighthouseCybilDahliaSteps[10] = function()
    LhTimer(98304) -- 24
    LhMsg("map6_s01.harry_hold_on_a_minute")
end

lighthouseCybilDahliaSteps[11] = function()
    sh.npc.image_calc("img:0", 30, 2) -- Dahlia
    sh.npc.load(1, "Dahlia", "npc_bone:30", 0, "lm:2", "img:0")
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[12]()
end

lighthouseCybilDahliaSteps[12] = function()
    if sh.load.chunks() ~= 0 then
        sh.event_step_increment(0)
    end
end

lighthouseCybilDahliaSteps[13] = function()
    sh.npc.process_loads()
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[14]()
end

lighthouseCybilDahliaSteps[14] = function()
    LhMsg("map6_s01.harry_2")
end

lighthouseCybilDahliaSteps[15] = function()
    LhSfx("lake_door_open", 0)
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[16]()
end

lighthouseCybilDahliaSteps[16] = function()
    sh.event_wait_timer(2048, false) -- 0.5
end

lighthouseCybilDahliaSteps[17] = function()
    sh.event_chara_anim(0, 0, "scene_bar_fight_1", false)
    sh.event_chara_anim(0, 1, "twitch", false)
    LhSfx("lake_door_close", 0)
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[18]()
end

lighthouseCybilDahliaSteps[18] = function()
    LhAdv(40960, 102400, 139264, true, true) -- 10, 25, 34
end

lighthouseCybilDahliaSteps[19] = function()
    sh.npc.spawn("Dahlia", 0, -77824, 237568, 0, 3)
    sh.map.set("update_daria", 1)
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_chara_anim(0, 1, "stand_talk", false)
    sh.event_chara_anim(0, 2, "stand_talk", false)
    sh.flag_set("M6S01_DahliaAppearedOnBoat", true)
    LhTimer(143360) -- 35
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[20]()
end

lighthouseCybilDahliaSteps[20] = function()
    LhMsg("map6_s01.the_demon_is_awakening")
end

lighthouseCybilDahliaSteps[21] = function()
    LhTimer(147456) -- 36
    LhMsg("map6_s01.was_it_not_as_i")
end

lighthouseCybilDahliaSteps[22] = function()
    sh.event_chara_anim(0, 0, "scene_dialogue_1", false)
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[23]()
end

lighthouseCybilDahliaSteps[23] = function()
    LhMsg("map6_s01.so_what_am_i_supposed")
    LhAdv(40960, 151552, 200704, true, false) -- 10, 37, 49
    sh.event_wait_timer(4096, false)
end

lighthouseCybilDahliaSteps[24] = function()
    sh.event_chara_anim(0, 0, "scene_lighthouse_2", false)
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[25]()
end

lighthouseCybilDahliaSteps[25] = function()
    LhMsg("map6_s01.so_what_am_i_supposed")
    LhAdv(40960, 151552, 200704, true, false)
end

lighthouseCybilDahliaSteps[26] = function()
    sh.event_chara_anim(0, 1, "head_loll", false)
    LhTimer(204800) -- 50
    sh.map.set("lighthouse_door_hidden", 1)
    sh.npc.anim_flags_clear(2, 2) -- hide the model
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[27]()
end

lighthouseCybilDahliaSteps[27] = function()
    LhMsg("map6_s01.look_harry")
    sh.event_wait_timer(6144, false) -- 1.5
end

lighthouseCybilDahliaSteps[28] = function()
    sh.event_chara_anim(0, 0, "scene_lighthouse_1", false)
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[29]()
end

lighthouseCybilDahliaSteps[29] = function()
    LhMsg("map6_s01.look_harry")
end

lighthouseCybilDahliaSteps[30] = function()
    sh.event_chara_anim(0, 1, "collapse", false)
    sh.map.set("lighthouse_door_hidden", 0)
    sh.npc.anim_flags_set(2, 2)
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[31]()
end

lighthouseCybilDahliaSteps[31] = function()
    LhAdv(24576, 208896, 245760, true, true) -- 6, 51, 60
end

lighthouseCybilDahliaSteps[32] = function()
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[33]()
end

lighthouseCybilDahliaSteps[33] = function()
    LhMsg("map6_s01.cybil_thanks")
    LhAdv(24576, 249856, 397312, true, false) -- 6, 61, 97
end

lighthouseCybilDahliaSteps[34] = function()
    LhAdv(24576, 249856, 397312, true, true)
end

lighthouseCybilDahliaSteps[35] = function()
    sh.npc.clear(1, 0, 0)
    LhSfx("lake_door_b_open", 1)
    sh.event_chara_anim(0, 0, "scene_dialogue_1", false)
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[36]()
end

lighthouseCybilDahliaSteps[36] = function()
    sh.event_wait_timer(1228, false) -- 0.3
    if sh.event_step_get(0) ~= 36 then
        LhSfx("lake_door_b_close", 1)
    end
    return lighthouseCybilDahliaSteps[37]()
end

lighthouseCybilDahliaSteps[37] = function()
    if sh.event_step_get(0) == 37 then
        sh.event_chara_anim(1, 0, 0, false) -- wait for the animation to end
    end
    LhAdv(40960, 401408, 495616, true, false) -- 10, 98, 121
end

lighthouseCybilDahliaSteps[38] = function()
    sh.event_chara_anim(0, 0, "scene_lighthouse_2", false)
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[39]()
end

lighthouseCybilDahliaSteps[39] = function()
    LhMsg("map6_s01.you_will_need_to_use")
    LhAdv(40960, 401408, 495616, true, false)
    sh.event_wait_timer(2457, false) -- 0.6
end

lighthouseCybilDahliaSteps[40] = function()
    sh.event_chara_anim(0, 0, "scene_lighthouse_3", false)
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[41]()
end

lighthouseCybilDahliaSteps[41] = function()
    LhMsg("map6_s01.you_will_need_to_use")
    LhAdv(40960, 401408, 495616, true, false)
end

lighthouseCybilDahliaSteps[42] = function()
    sh.event_chara_anim(0, 2, "turn_away", false)
    sh.npc.anim_flags_clear(0, 2)
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[43]()
end

lighthouseCybilDahliaSteps[43] = function()
    LhMsg("map6_s01.use_what")
    LhAdv(40960, 499712, 581632, true, false) -- 10, 122, 142
end

lighthouseCybilDahliaSteps[44] = function()
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[45]()
end

lighthouseCybilDahliaSteps[45] = function()
    LhMsg("map6_s01.what_about_cybil")
    LhAdv(40960, 499712, 643072, true, false) -- 10, 122, 157
end

lighthouseCybilDahliaSteps[46] = function()
    sh.npc.clear(2, 0, 0)
    LhSfx("lake_door_open", 0)
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[47]()
end

lighthouseCybilDahliaSteps[47] = function()
    sh.event_wait_timer(1228, false)
end

lighthouseCybilDahliaSteps[48] = function()
    LhSfx("lake_door_close", 0)
    sh.event_step_increment(0)
    return lighthouseCybilDahliaSteps[49]()
end

lighthouseCybilDahliaSteps[49] = function()
    sh.event_wait_timer(2457, false)
end

lighthouseCybilDahliaSteps[50] = function()
    sh.npc.anim_play_to_end(0, "scene_lighthouse_1")
    sh.npc.anim_flags_set(0, 2)
    LhAdv(40960, 647168, 688128, true, false) -- 10, 158, 168
end

lighthouseCybilDahliaSteps[51] = function()
    LhTimer(692224) -- 169
    sh.event_wait_timer(8192, false) -- 2.0
end

lighthouseCybilDahliaSteps[52] = function()
    sh.event_step_increment(0)
end

lighthouseCybilDahliaSteps[53] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

lighthouseCybilDahliaSteps[54] = function()
    sh.npc.clear(1, 0, 0)
    sh.npc.clear(2, 0, 0)
    LhTimer(692224)
    sh.event_step_increment(0)
    sh.map.set("lighthouse_door_hidden", 0)
end

lighthouseCybilDahliaSteps.default = function()
    sh.player_freeze(false, true)
    sh.state_set(0)
    sh.light.flashlight_on()
    sh.world.effects_update(7, 4)
    sh.water_effect_disable()
    sh.light.flashlight_fix()
    sh.light.set_intensity(4096)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.camera.auto_return(true)
    LhTimer(-1) -- stop the cutscene timer
    sh.sd_call("stop_xa_audio")
    sh.flag_set("M6S01_CybilDahliaCutsceneDone", true)
    sh.flag_set("MapMark_ResortTown_LighthouseCircle", true) -- map mark: resort town lighthouse circle
    sh.held_item_show()
    sh.player.weapon_restore()
    sh.npc.anim_flags_set(0, 2)
end

function Cutscene_LighthouseCybilDahlia()
    local s = sh.event_step_get(0)
    if sh.input_action("skip", 1) and s >= 3 and s < 52 then
        sh.event.step_set(0, 53)
        s = 53
    end
    runStep(lighthouseCybilDahliaSteps, s)

    -- Update objects.
    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, 15)
        if sh.map.get("update_sibyl") ~= 0 then
            sh.cutscene.chara(1, "SIBYL", t, 15)
        end
        if sh.map.get("update_daria") ~= 0 then
            sh.cutscene.chara(2, "DARIA", t, 15)
        end
        sh.cutscene.camera(t, 15, "camera_target_pos", "camera_target_look")
        sh.cutscene.light(t, 15)
    end
end

local lighthouseCybilLeavesSteps = {}

lighthouseCybilLeavesSteps[0] = function()
    sh.player_freeze(true)
    sh.map.set("msg_audio_idx1", 0)
    sh.screen_fade_reset_timestep()
    sh.cutscene.border_show()
    sh.npc.set_pos_x(0, -85934) -- -20.98 m
    sh.npc.set_pos_z(0, 240312) -- 58.67 m
    sh.npc.set_rot_y(0, 1024) -- 90.0 deg
    sh.camera.position(nil, -76595, -22896, 234373, 0, 0, 0, 0, true) -- (-18.7, -5.59, 57.22)
    sh.camera.look_at(nil, -79708, -7864, 240107, 0, 0, 0, 0, true) -- (-19.46, -1.92, 58.62)
    LhSfx("lake_door_open", 0)
    sh.light.flashlight_on()
    sh.event_step_increment(0)
    return lighthouseCybilLeavesSteps[1]()
end

lighthouseCybilLeavesSteps[1] = function()
    sh.event_wait_timer(3276, false) -- 0.8
end

lighthouseCybilLeavesSteps[2] = function()
    LhSfx("lake_door_close", 0)
    sh.event_step_increment(0)
    return lighthouseCybilLeavesSteps[3]()
end

lighthouseCybilLeavesSteps[3] = function()
    sh.event_fade_cmd(2, false, 0, 2048, false) -- 0.5
end

lighthouseCybilLeavesSteps[4] = function()
    sh.event_wait_timer(6144, false) -- 1.5
end

lighthouseCybilLeavesSteps[5] = function()
    sh.msg.show_with_audio("map6_s01.cybil_hasnt_come_back", "msg_audio_idx1", "msg_audio_cmds1")
end

lighthouseCybilLeavesSteps[6] = function()
    sh.event_wait_timer(14336, false) -- 3.5
end

lighthouseCybilLeavesSteps[7] = function()
    sh.msg.show_with_audio("map6_s01.that_creeps_sure_to_show", "msg_audio_idx1", "msg_audio_cmds1")
end

lighthouseCybilLeavesSteps[8] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

lighthouseCybilLeavesSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.camera.auto_return(true)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.flag_set("M6S01_CybilLeavesCutsceneDone", true)
    sh.flag_set("MapMark_ResortTown_ThemeParkArrow", true) -- map mark: resort town theme park arrow
end

-- index 4: Cybil leaves through the lighthouse door (lines 84/85).
function Cutscene_LighthouseCybilLeaves()
    local s = sh.event_step_get(0)
    if sh.input_action("skip", 1) and s >= 3 and s < 8 then
        sh.event.step_set(0, 8)
        s = 8
    end
    runStep(lighthouseCybilLeavesSteps, s)
end

-- index 5: UFO ending at the lighthouse: Channeling Stone, UFO4.DMS, five lights
-- ufo_light_pos[0..4] on the circle ufo_light_ring[0..4] (radius, speed).
-- ufo_hum_volume is the volume of the UFO hum (sound ufo_hum), ufo_hum_clock a counter.
local UfoLightAxis = {[0] = "ufo_light_pos.x", "ufo_light_pos.y", "ufo_light_pos.z"}
local function UfoLightAddr(i) return "ufo_light_pos[" .. i .. "]" end
local function UfoLightGet(i, c) return sh.map.get(UfoLightAxis[c], i) end
local function UfoLightSet(i, c, v) sh.map.set(UfoLightAxis[c], v, i) end
local function UfoLightsUpdate()
    for i = 0, 4 do
        sh.map.ufo_glow_spawn(UfoLightAddr(i), 2048, 204, 0) -- 0.5, 0.05
    end
end

local lighthouseUfoSteps = {}

lighthouseUfoSteps[0] = function()
    sh.player_freeze(true)
    sh.map.set("msg_audio_idx0", 0)
    sh.load.tim("TIM/ENBAN.TIM", 1, "ufo_glow_tex") -- ENBAN.TIM
    sh.load.file("ANIM/UFO4.DMS", 13) -- UFO4.DMS
    sh.map.set("cutscene_timer", -1)
    sh.screen_fade_reset_timestep()
    sh.sd_call("ufo_hum")
    sh.sfx_attributes_update("ufo_hum", 0, 255, 0) -- full volume
    sh.map.set("ufo_hum_volume", 0)
    sh.map.set("ufo_hum_clock", 0)
    for i = 0, 4 do
        local r = sh.map.get("ufo_light_ring.radius", i)
        local a = (i * 4096) // 5
        UfoLightSet(i, 0, (r * sh.math_sin(a)) // 4096 - 94208) -- - 23.0 m
        UfoLightSet(i, 1, (r * sh.math_cos(a)) // 4096 - 4096)  -- - 1.0 m
        UfoLightSet(i, 2, 73728) -- 18.0 m
    end
    sh.event_step_increment(0)
    return lighthouseUfoSteps[1]()
end

lighthouseUfoSteps[1] = function()
    sh.event.wait_player_stop()
end

lighthouseUfoSteps[2] = function()
    if sh.load.chunks() ~= 0 then
        sh.event_step_increment(0)
    end
end

lighthouseUfoSteps[3] = function()
    sh.map.set("cutscene_timer", 0)
    sh.event_fade_cmd(0, true, 3, 0, false)
    sh.cutscene.prepare(13)
    sh.held_item_hide()
    sh.player.weapon_stash()
    sh.light.flashlight_on()
    sh.event_step_increment(0)
    return lighthouseUfoSteps[4]()
end

lighthouseUfoSteps[4] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
    return lighthouseUfoSteps[5]()
end

lighthouseUfoSteps[5] = function()
    sh.msg.show_with_audio("map6_s01.what", "msg_audio_idx0", "ufo_msg_voice_cmds[0]")
end

lighthouseUfoSteps[6] = function()
    sh.cutscene.timer_advance("cutscene_timer", 163840, 0, 122880, true, true) -- 40, 0, 30
    UfoLightsUpdate()
end

lighthouseUfoSteps[7] = function()
    sh.msg.show(false, "map6_s01.that_light", 0, 0, 0, false)
    UfoLightsUpdate()
end

lighthouseUfoSteps[8] = function()
    sh.vec.short_set(15, 0, 0, 0)                 -- no rotation
    sh.vec.set(15, -95723, -5120, 78970)      -- (-23.37, -1.25, 19.28) m
    sh.camera.look_at_bone("player_bone:2", "svec:15", "vec:15") -- Harry's head
    for i = 0, 4 do
        sh.map.ufo_glow_spawn(UfoLightAddr(i), 2048, 204, i == 0)
    end
    sh.event_wait_timer(10240, false) -- 2.5 s
end

lighthouseUfoSteps[9] = function()
    local timer = sh.event.state_timer()
    if timer > 3276 then -- 0.8 s
        local v = (timer - 3276) // 4
        if v > 1024 then v = 1024 end -- 0.25
        v = 4096 - sh.math_cos(v)
        local dt = sh.delta_time_get()
        for i = 0, 4 do
            local vy = sh.map.get("ufo_light_ring.speed", i)
            local k = (v * vy) // 4096 + vy
            k = (k * 4915) // 4096 -- k * 1.2
            local a = (i * 4096) // 5
            UfoLightSet(i, 0, UfoLightGet(i, 0) + (dt * ((k * sh.math_sin(a)) // 4096)) // 4096)
            UfoLightSet(i, 1, UfoLightGet(i, 1) + (dt * ((k * sh.math_cos(a)) // 4096)) // 4096)
        end
    end
    UfoLightsUpdate()
    sh.event.set_state_timer(sh.event.state_timer() + sh.delta_time_get())
    if sh.event.state_timer() > 7372 then -- 1.8 s
        sh.event_step_increment(0)
    end
end

lighthouseUfoSteps[10] = function()
    sh.event_wait_timer(10240, false)
end

lighthouseUfoSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.camera.auto_return(true)
    sh.flag_set("M6S01_ChannelingStoneUsed", true)
    sh.event.inv_item(3, "ChannelingStone", 1, false) -- add the Channeling Stone
    sh.held_item_show()
    sh.player.weapon_restore()
    sh.map.set("cutscene_timer", -1)
    sh.sfx_stop("ufo_hum")
end

function Cutscene_LighthouseUfo()
    sh.map.set("ufo_hum_clock", sh.map.get("ufo_hum_clock") + sh.delta_time_get())

    local step = sh.event_step_get(0)
    if step >= 10 then
        local v = sh.map.get("ufo_hum_volume") - (sh.delta_time_get() * 2048) // 4096
        if v < 0 then v = 0 end
        sh.map.set("ufo_hum_volume", v)
        sh.sfx_attributes_update("ufo_hum", 0, ~(sh.map.get("ufo_hum_volume") // 16), 0)
    elseif step > 0 then
        local v = sh.map.get("ufo_hum_volume") + (sh.delta_time_get() * 1024) // 4096
        if v > 4080 then v = 4080 end -- 0.9961
        sh.map.set("ufo_hum_volume", v)
        sh.sfx_attributes_update("ufo_hum", 0, ~(sh.map.get("ufo_hum_volume") // 16), 0)
    end

    local s = sh.event_step_get(0)
    runStep(lighthouseUfoSteps, s)

    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, 13)
        sh.cutscene.camera(t, 13, "camera_target_pos", "camera_target_look")
    end
end
