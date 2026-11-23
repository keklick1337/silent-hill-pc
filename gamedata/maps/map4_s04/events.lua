-- map4_s04: Hospital - 1F (Lisa cutscene)
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

-- index 3: HSPTL4.DMS cutscene, Harry comes to next to Lisa ("Where am I?"), seen double as
-- in the map3_s02 stairway scene: every tick the held picture is cleared, the world is drawn
-- into it and comes onto the screen as four shifted copies. Lisa follows the DMS from step 1;
-- the chair slides with the scene. Flag 338 at the end.
local WHERE_AM_I_DMS_BUF = 15

-- Bottom first: an opaque copy one pixel up, then half blends one pixel down (full) and one
-- right (half bright), an added quarter-bright copy one pixel left on top.
local DOUBLE_VISION = {
    {offset = {0, -1}, tint = 128, blend = "none"},
    {offset = {0, 1}, tint = 128, blend = "half"},
    {offset = {1, 0}, tint = 64, blend = "half"},
    {offset = {-1, 0}, tint = 32, blend = "add"},
}
local lisaFollowsDms = false -- an overlay u8 in the original, read only here

local function WhereAmIMsg(msgKey)
    sh.msg.show_with_audio(msgKey, "lisa_msg_audio_idx", "where_am_i_voice_cmds[0]")
end

local function WhereAmITimerAdvance(rate, startTime, endTime)
    sh.cutscene.timer_advance("cutscene_timer", rate, startTime, endTime, true, false)
end

local cutsceneWhereAmISteps = {}

cutsceneWhereAmISteps[0] = function()
    sh.player_freeze(true)
    sh.map.set("lisa_msg_audio_idx", 0)
    sh.load.file("ANIM/HSPTL4.DMS", WHERE_AM_I_DMS_BUF)
    sh.load.wait()
    sh.cutscene.prepare(WHERE_AM_I_DMS_BUF)
    sh.map.set("cutscene_timer", 0)
    sh.npc.load(0, "Lisa", "npc_bone:0", 255, nil, nil) -- free all character slots
    sh.npc.process_loads()
    sh.npc.spawn("Lisa", 0, 286720, 614400, 0, 3) -- (70.0, 150.0) m
    sh.held_item_hide()
    sh.player.weapon_stash()
    sh.map.set("lisa_msg_audio_idx", 0)
    sh.light.bone_detach()
    sh.light.lens_flare_detach()
    sh.light.set_intensity(4096)
    sh.light.set_pos_x(233472)  -- 57.0
    sh.light.set_pos_y(-12288)  -- -3.0
    sh.light.set_pos_z(580812)  -- 141.8
    sh.light.set_rot_x(-1024)   -- -90 deg
    sh.light.set_rot_y(0)
    sh.light.set_rot_z(0)
    sh.water_effect_enable()
    sh.cutscene.border_show()
    sh.screen_fade_reset_timestep()
    sh.cutscene_flag(true)
    sh.event_chara_anim(0, 0, "scene_hospital_dahlia", false)
    lisaFollowsDms = false
    sh.map_effects_init(15, 15)
    sh.event_step_increment(0)
    sh.map.set("chair.pos.z", 577536) -- 141.0
end

cutsceneWhereAmISteps[1] = function()
    sh.event_chara_anim(2, 0, 0, false) -- AnimLock
    sh.event_chara_anim(0, 1, "worry", false)
    lisaFollowsDms = true
    sh.event_step_increment(0)
    return cutsceneWhereAmISteps[2]()
end

cutsceneWhereAmISteps[2] = function()
    sh.event_fade_cmd(2, false, 0, 4096, false) -- Auto, in, 1.0 s
end

cutsceneWhereAmISteps[3] = function()
    sh.event_chara_anim(3, 0, 0, false) -- AnimUnlock
    sh.event_step_increment(0)
end

cutsceneWhereAmISteps[4] = function()
    sh.event_wait_timer(4915, false) -- 1.2 s
    WhereAmITimerAdvance(32768, 0, 94208) -- 8.0, 0.0 .. 23.0
end

cutsceneWhereAmISteps[5] = function()
    WhereAmIMsg("map4_s04.where_am_i")
    WhereAmITimerAdvance(32768, 0, 94208)
end

cutsceneWhereAmISteps[6] = function()
    sh.map.set("cutscene_timer", 98304) -- frame 24.0
    sh.map.set("chair.pos.z", 576716) -- 140.8
    sh.event_chara_anim(0, 0, "scene_lisa_hospital_1", false)
    sh.event_step_increment(0)
    return cutsceneWhereAmISteps[7]()
end

cutsceneWhereAmISteps[7] = function()
    WhereAmIMsg("map4_s04.harry")
end

cutsceneWhereAmISteps[8] = function()
    WhereAmIMsg("map4_s04.lisa_do_you_know_a")
end

cutsceneWhereAmISteps[9] = function()
    WhereAmIMsg("map4_s04.well_she_says_the_town")
end

cutsceneWhereAmISteps[10] = function()
    WhereAmIMsg("map4_s04.weird_occult_stuff_black_magic")
end

cutsceneWhereAmISteps[11] = function()
    WhereAmIMsg("map4_s04.last_time_i_heard_anything")
    WhereAmITimerAdvance(40960, 102400, 188416) -- 10.0, 25.0 .. 46.0
    sh.map.set("chair.pos.z", 577331) -- 140.95
end

cutsceneWhereAmISteps[12] = function()
    sh.npc.anim_play_to_end(1, "scene_pose")
    WhereAmITimerAdvance(40960, 102400, 188416)
end

cutsceneWhereAmISteps[13] = function()
    sh.event_fade_cmd(2, true, 0, 3276, false) -- Auto, out, 0.8 s
    WhereAmITimerAdvance(40960, 102400, 188416)
end

cutsceneWhereAmISteps[14] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

cutsceneWhereAmISteps.default = function()
    sh.event_chara_anim(3, 0, 0, false) -- AnimUnlock
    sh.player_freeze(false, false)
    sh.state_set(0) -- back to gameplay
    sh.npc.clear(1, 0, 0)
    sh.flag_set("M4S04_WhereAmICutsceneDone", true)
    sh.sd_call("stop_xa_audio")
    sh.held_item_show()
    sh.player.weapon_restore()
end

function MapEvent_CutsceneWhereAmI()
    sh.screen.debug_text_position(30, 30)
    sh.render.held_clear()
    sh.render.world_redirect{layers = DOUBLE_VISION}

    local s = sh.event_step_get(0)
    if sh.input_action("skip", 1) and s > 0 and s < 14 then
        sh.event.step_set(0, 14)
        s = 14
    end

    runStep(cutsceneWhereAmISteps, s)
    sh.npc.set_pos_y(1, 0)

    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, WHERE_AM_I_DMS_BUF)
        if lisaFollowsDms then
            sh.cutscene.chara(1, "LISA", t, WHERE_AM_I_DMS_BUF)
        end
        sh.cutscene.camera(t, WHERE_AM_I_DMS_BUF, "camera_target_pos", "camera_target_look")
    end
end

-- index 4: HSPTL5.DMS cutscene, talking with Lisa in the hospital
-- (steps 0-26 + default, DMS HERO/LISA/LIGHT).
local function MsgAudio(msgIdx)
    sh.msg.show_with_audio(msgIdx, "lisa_msg_audio_idx", "lisa_talk_voice_cmds[0]")
end

local function TimerAdvance(rate, startTime, endTime, clampToEnd, autoAdvance)
    sh.cutscene.timer_advance("cutscene_timer", rate, startTime, endTime, clampToEnd, autoAdvance)
end

local function TimerGet()
    return sh.map.get("cutscene_timer")
end

local function TimerSet(v)
    sh.map.set("cutscene_timer", v)
end

-- Harry's anim 134 once the timeline passes frame 32 (steps 7/8).
local function HarryTurnOnce()
    if TimerGet() >= 32 * 4096 and sh.map.get("harry_turn_anim_done") == 0 then
        sh.event_chara_anim(0, 0, "turn_around_once", false)
        sh.map.set("harry_turn_anim_done", sh.map.get("harry_turn_anim_done") + 1)
    end
end

local hospitalLisaTalkSteps = {}

hospitalLisaTalkSteps[0] = function()
    sh.player_freeze(true)
    sh.light.flashlight_on()
    sh.map.set("lisa_msg_audio_idx", 0)
    sh.load.file("ANIM/HSPTL5.DMS", 15) -- HSPTL5.DMS
    sh.load.wait()
    sh.cutscene.prepare(15)
    TimerSet(0)
    sh.npc.load(0, "Lisa", "npc_bone:0", 255, nil, nil) -- Lisa, free all character slots
    sh.npc.process_loads()
    sh.npc.spawn("Lisa", 0, 60 * 4096, 140 * 4096, 0, 3)
    sh.held_item_hide()
    sh.player.weapon_stash()
    sh.map.set("lisa_msg_audio_idx", 0)
    sh.light.bone_detach()
    sh.light.lens_flare_detach()
    sh.light.set_intensity(4915) -- 1.2
    sh.water_effect_enable()
    sh.cutscene.border_show()
    sh.screen_fade_reset_timestep()
    sh.map.set("harry_turn_anim_done", 0)
    sh.cutscene_flag(true)
    sh.sfx_flags_play("door_close_0", 223641, -4915, 579584, 128, 0) -- door close 0 sound at (54.6, -1.2, 141.5), 0.5
    sh.event_step_increment(0)
end

hospitalLisaTalkSteps[1] = function()
    sh.event_chara_anim(0, 1, "turn_greet", false)
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_step_increment(0)
end

hospitalLisaTalkSteps[2] = function()
    sh.event_chara_anim(2, 1, 0, false) -- AnimLock
    sh.event_fade_cmd(2, false, 0, 0, false) -- Auto
end

hospitalLisaTalkSteps[3] = function()
    sh.event_chara_anim(3, 1, 0, false) -- AnimUnlock
    sh.event_step_increment(0)
    return hospitalLisaTalkSteps[4]()
end

hospitalLisaTalkSteps[4] = function()
    MsgAudio("map4_s04.harry_2")
    TimerAdvance(26624, 0, 20 * 4096, true, false) -- 6.5 s
end

hospitalLisaTalkSteps[5] = function()
    TimerAdvance(26624, 0, 20 * 4096, true, true)
end

hospitalLisaTalkSteps[6] = function()
    sh.event_chara_anim(0, 1, "walk", false)
    sh.light.set_intensity(4096)
    sh.event_step_increment(0)
    return hospitalLisaTalkSteps[7]()
end

hospitalLisaTalkSteps[7] = function()
    MsgAudio("map4_s04.glad_youre_ok")
    TimerAdvance(8 * 4096, 21 * 4096, 54 * 4096, true, false)
    HarryTurnOnce()
end

hospitalLisaTalkSteps[8] = function()
    TimerAdvance(8 * 4096, 21 * 4096, 54 * 4096, true, true)
    HarryTurnOnce()
end

hospitalLisaTalkSteps[9] = function()
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_chara_anim(0, 1, "stand_talk", false)
    sh.light.set_intensity(4915)
    TimerSet(55 * 4096)
    sh.event_step_increment(0)
    return hospitalLisaTalkSteps[10]()
end

hospitalLisaTalkSteps[10] = function()
    MsgAudio("map4_s04.thank_god_you_came_back")
end

hospitalLisaTalkSteps[11] = function()
    TimerSet(56 * 4096)
    sh.event_step_increment(0)
    return hospitalLisaTalkSteps[12]()
end

hospitalLisaTalkSteps[12] = function()
    MsgAudio("map4_s04.lisa_can_you_tell_me")
end

hospitalLisaTalkSteps[13] = function()
    sh.event_wait_timer(2 * 4096, false)
end

hospitalLisaTalkSteps[14] = function()
    MsgAudio("map4_s04.wait_i_just_remembered")
end

hospitalLisaTalkSteps[15] = function()
    MsgAudio("map4_s04.really")
end

hospitalLisaTalkSteps[16] = function()
    TimerSet(57 * 4096)
    sh.light.set_intensity(4096)
    MsgAudio("map4_s04.harry_dont_go")
end

hospitalLisaTalkSteps[17] = function()
    sh.event_chara_anim(0, 1, "plead_stay", false)
    sh.light.set_intensity(4915)
    sh.event_step_increment(0)
    return hospitalLisaTalkSteps[18]()
end

hospitalLisaTalkSteps[18] = function()
    MsgAudio("map4_s04.harry_3")
    TimerAdvance(10 * 4096, 58 * 4096, 126 * 4096, true, false)
end

hospitalLisaTalkSteps[19] = function()
    TimerAdvance(10 * 4096, 58 * 4096, 126 * 4096, true, true)
end

hospitalLisaTalkSteps[20] = function()
    sh.npc.anim_flags_clear(0, 2)
    sh.event_chara_anim(0, 0, "walk_forward", false)
    sh.event_chara_anim(0, 1, "look_away", false)
    sh.event_step_increment(0)
    return hospitalLisaTalkSteps[21]()
end

hospitalLisaTalkSteps[21] = function()
    TimerAdvance(10 * 4096, 127 * 4096, 151 * 4096, false, true)
end

hospitalLisaTalkSteps[22] = function()
    sh.sfx_flags_play("door_open_0", 223641, -4915, 579584, 128, 0) -- door open 0 sound
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_step_increment(0)
    return hospitalLisaTalkSteps[23]()
end

hospitalLisaTalkSteps[23] = function()
    TimerAdvance(10 * 4096, 127 * 4096, 160 * 4096, false, true)
end

hospitalLisaTalkSteps[24] = function()
    sh.sfx_flags_play("door_close_0", 223641, -4915, 579584, 128, 0) -- door close 0 sound
    sh.event_step_increment(0)
    return hospitalLisaTalkSteps[25]()
end

hospitalLisaTalkSteps[25] = function()
    TimerAdvance(10 * 4096, 127 * 4096, 200 * 4096, true, true)
end

hospitalLisaTalkSteps[26] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

hospitalLisaTalkSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0) -- back to gameplay
    sh.sd_call("stop_xa_audio")
    sh.npc.anim_flags_set(0, 2)
    sh.held_item_show()
    sh.npc.clear(1, 0, 0)
    sh.flag_set("MapMark_AltHospital1F_ExaminationRoomLeftArrows", true) -- map mark: arrows on the Otherworld hospital 1F examination room door (left)
    sh.flag_set("M4S04_LisaTalkCutsceneDone", true)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.water_effect_disable()
    sh.light.flashlight_fix()
    sh.light.set_intensity(4096)
    TimerSet(-1) -- none
    sh.camera_auto_return(false)
    sh.held_item_show()
    sh.player.weapon_restore()
end

function Cutscene_HospitalLisaTalk()
    local s = sh.event_step_get(0)
    if sh.input_action("skip", 1) and s > 0 and s < 22 then
        sh.event.step_set(0, 26)
        s = 26
    end

    runStep(hospitalLisaTalkSteps, s)
    local t = TimerGet()
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, 15)
        sh.cutscene.chara(1, "LISA", t, 15)
        sh.cutscene.camera(t, 15, "camera_target_pos", "camera_target_look")
        sh.cutscene.light(t, 15)
    end
end
