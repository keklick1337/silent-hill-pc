-- map7_s03: Nowhere - final boss
-- Event scripts of this area: the engine runs a handler every tick while its event is active.

-- Runs one step of a handler's step table: steps[step], or steps.default for any other step.
local function runStep(steps, step, ...)
    return (steps[step] or steps.default)(...)
end

function on_boot()
    -- per-scenario script entry point; this area has no handlers at index 0 and 1
end

-- Game states
local SysState = {
    Gameplay = 0,
    StatusMenu = 2,
}

-- Screen fade states
local ScreenFade = {
    FadeOutComplete = 5,
}

-- State kept for the whole stay in this area:
-- s_endingStep    - shared step of the ending sequences;
-- s_endingSubStep - sub-step;
-- s_endingIdx     - ending index 1..4;
-- s_vblankClock   - frame counter (picks the ending branch).
local s_endingStep = 0
local s_endingSubStep = 0
local s_endingIdx = 0
local s_vblankClock = 0

-- Fixed-point helper: 4096 = 1.0.
local function Q12(x)
    return math.floor(x * 4096 + 0.5)
end

-- The ending scenes below share one step, ending_step: the slot starts the scene at 0 and the
-- scene body moves it on; past its last step the slot returns to gameplay. Every tick ends
-- with the ending scenes' DMS objects update.
local SYSFLAG_CUTSCENE_ACTIVE = 8 -- system flag bit 3
local FullHealth = 409600        -- 100.0

-- The DMS the ending objects follow: ending_dms_idx 0 = file buffer 20, 1 = file buffer 18.
local EndingDmsBufs = { [0] = 20, [1] = 18 }

-- Every tick of an ending scene while cutscene_timer >= 0: the characters and objects whose
-- update_* is set follow their DMS track, then the camera and the light (LIGHT, aimed at L_INT).
local function EndingObjectsUpdate()
    local t = sh.map.get("cutscene_timer")
    if t < 0 then
        return
    end

    -- Ending shots are framed for the stock fog; thinner fog (whole map) reveals Kaufmann's body
    -- behind them.
    sh.world.fog_floor_request()

    local buf = EndingDmsBufs[sh.map.get("ending_dms_idx")]
    if sh.map.get("update_hero") ~= 0 then sh.cutscene.chara(0, "HERO", t, buf) end
    if sh.map.get("update_sibyl") ~= 0 then sh.cutscene.chara(1, "SIBYL", t, buf) end
    if sh.map.get("update_daria") ~= 0 then sh.cutscene.chara(2, "DARIA", t, buf) end
    if sh.map.get("update_arsia") ~= 0 then sh.cutscene.chara(3, "ARSIA", t, buf) end
    if sh.map.get("update_kau") ~= 0 then sh.cutscene.chara(4, "KAU", t, buf) end
    if sh.map.get("update_mar") ~= 0 then sh.cutscene.chara(5, "MAR", t, buf) end
    if sh.map.get("update_bar") ~= 0 then sh.cutscene.chara(6, "BAR", t, buf) end
    if sh.map.get("update_litl") ~= 0 then sh.cutscene.chara(1, "LITL", t, buf) end
    if sh.map.get("update_bos") ~= 0 then sh.cutscene.chara(3, "BOS", t, buf) end
    if sh.map.get("update_bin") ~= 0 then sh.cutscene.transform_to("bin.pos", "bin.rot", "BIN", t, buf) end
    if sh.map.get("update_baby") ~= 0 then sh.cutscene.transform_to("baby.pos", "baby.rot", "BABY", t, buf) end

    sh.cutscene.camera(t, buf, "camera_target_pos", "camera_target_look")

    sh.cutscene.transform_to("light_pos", "ending_light_rot", "LIGHT", t, buf)
    sh.cutscene.transform_to("ending_light_target", "ending_light_rot", "L_INT", t, buf)
    local dx = sh.map.get("ending_light_target.x") - sh.light.pos_x()
    local dy = sh.map.get("ending_light_target.y") - sh.light.pos_y()
    local dz = sh.map.get("ending_light_target.z") - sh.light.pos_z()
    sh.light.set_rot_x(-sh.math.atan2(dy, sh.math.vec2_len_q6(dx, dz)))
    sh.light.set_rot_y(sh.math.atan2(dx, dz))
    sh.light.set_rot_z(0)
end

-- The effect cues of the scene's current step (flames, sparks): a cue with flag 2 runs only on
-- the first tick of its step.
local function EndingCuesRun(scene, lastStepVar)
    local step = sh.event_step_get(0)
    local i = 0
    repeat
        local flags = sh.map.ending_cue_flags(scene, step, i)
        if flags < 0 then
            break -- no cues at this step
        end
        if (flags & 2) == 0 or step ~= sh.map.get(lastStepVar) then
            sh.map.ending_cue_play(scene, step, i)
        end
        i = i + 1
    until (flags & 1) == 0
    sh.map.set(lastStepVar, step)
end

-- One random lightning bolt in about one tick of eight (mask 0x70) or of 32 (mask 0xF8).
local function LightningBoltRandom(mask)
    if (sh.rng.rand16() & mask) == 0 then
        sh.map.lightning_bolt(0)
    end
end

-- The rumble of the ending scenes (pad effect 9 at 0xA0).
local function EndingRumble()
    sh.pad.effect_level(9, 160)
end

-- value + delta time * rate (rate in 1/4096), the way the scenes grow their timers.
local function DeltaScaled(value, rate)
    return value + (sh.game.delta_time() * rate) // 4096
end

local function AuxTimerGrow(slot, rate, limit)
    sh.npc.set_aux_timer(slot, DeltaScaled(sh.npc.aux_timer(slot), rate))
    if sh.npc.aux_timer(slot) > limit then
        sh.npc.set_aux_timer(slot, limit)
    end
end

local function EffectLevelGrow(rate, limit)
    sh.map.set("ending_effect_level", DeltaScaled(sh.map.get("ending_effect_level"), rate))
    if limit ~= nil and sh.map.get("ending_effect_level") > limit then
        sh.map.set("ending_effect_level", limit)
    end
end

local function StepNext()
    sh.event_step_increment(0)
end

local function EndingStepNext()
    sh.map.set("ending_step", sh.map.get("ending_step") + 1)
end

local function CharaId(name)
    return sh.chara_id(name) or error("no character " .. name)
end

-- The ending scenes' character loads: five buffer groups, each an animation slot with its animation file
-- buffer and bones (an index into the NPC bone buffer, or the map's own bone array) and a model slot with its
-- model buffer and texture place (page, palette x, y). Groups 4 and 5 reuse the slots of 1 and 2 with other
-- buffers and load the characters' LAST animation files.
local EndingCharaGroups = {
    { anim = 1, anim_buf = 29, bones = 0, model = 0, model_buf = 31, page = 28, clut_y = 464 },
    { anim = 2, anim_buf = 30, bones = 30, model = 1, model_buf = "lm:2", page = 29, clut_y = 480 },
    { anim = 3, anim_buf = 26, bones = "EndingCharaBones", model = 2, model_buf = 32, page = 30, clut_y = 496 },
    { anim = 1, anim_buf = 29, bones = 0, model = 0, model_buf = 31, page = 28, clut_y = 464, last = true },
    { anim = 2, anim_buf = 33, bones = 30, model = 1, model_buf = 34, page = 29, clut_y = 480, last = true },
}
local EndingClutX = 704

-- The animation files of the ending characters: the scenes' LAST ones, or the normal ones.
local EndingAnimFiles = {
    { "EndingCybil", "ANIM/SBL_LAST.ANM", "ANIM/SBL2.ANM" },
    { "BloodyIncubator", "ANIM/BAR_LAST.ANM", "ANIM/BAR.ANM" },
    { "EndingKaufmann", "ANIM/KAU_LAST.ANM", "ANIM/KAU2.ANM" },
    { "Incubator", "ANIM/MAR_LAST.ANM", "ANIM/MAR.ANM" },
}

local function EndingAnimFilesSet(last)
    for _, f in ipairs(EndingAnimFiles) do
        sh.chara.anim_file_set(f[1], last and f[2] or f[3])
    end
end

local function EndingAnimFilesDefault()
    EndingAnimFilesSet(false)
end

-- A character's model and animations into one of the buffer groups 1..5.
local function EndingCharaLoad(chara, group)
    local g = EndingCharaGroups[group]
    sh.npc.model_buffers_assign(1 << (group - 1))
    if g == nil then
        return
    end
    if g.last then
        EndingAnimFilesSet(true)
    end
    sh.chara.anim_data_alloc(g.anim, chara, g.anim_buf, g.bones)
    sh.chara.model_load(chara, g.model, g.model_buf, g.page, 0, 0, EndingClutX, g.clut_y)
    if g.last then
        EndingAnimFilesSet(false)
    end
end

-- All queued loads finished, then the models built.
local function EndingCharaLoadsFinish()
    sh.load.wait()
    sh.chara.model_loads_process()
end

-- The slot made an idle character of the type.
local function EndingCharaPlace(chara, slot)
    sh.npc.record_reset(slot, chara)
end

-- The Incubator's slot runs the scene behaviour, or Unknown23's for the fight.
local function IncubatorBehaviourScripted()
    sh.chara.update_as("Incubator", "Incubator")
end

local function IncubatorBehaviourFight()
    sh.chara.update_as("Incubator", "Unknown23")
end

-- The boss effects' picture, and the ending effects' pictures (texture page, palette x and y).
local function BossFxTextureLoad()
    sh.load.tim_at("TIM/BOSEF00.TIM", 1, 21, 0, 0, 48, 0)
end

local function EndingFxTexturesLoad()
    sh.load.tim_at("TIM/ELAST.TIM", 1, 55, 0, 0, 80, 0)
    sh.load.tim_at("TIM/ELAST2.TIM", 1, 56, 0, 0, 96, 0)
    sh.load.tim_at("TIM/EF1.TIM", 1, 54, 0, 0, 64, 0)
    sh.load.tim_at("TIM/ELAST3.TIM", 1, 57, 0, 0, 112, 0)
end

local SfxScene = { -- the final scene's effect sounds
    Thunder = "finale_thunder",
    Drone = "finale_drone",
    Rumble = "finale_rumble",
    Strike = "finale_lightning_strike",
    Crack = "finale_lightning_crack",
}
local SfxMonoNoAtten = 3 -- SfxFlag_Mono | SfxFlag_NoDistAtten
local FullVolume = 255

local function SceneSfxPlay(name)
    sh.sound.play_with_flags(name, nil, FullVolume, SfxMonoNoAtten)
end

local function TimerAdvance(rate, from, to, loop, inc)
    sh.cutscene.timer_advance("cutscene_timer", rate, from, to, loop, inc)
end

-- index 2: the final boss intro (event 7: on entering the area until M7S03_FinalBossIntroDone, set at the end;
-- M7S03_FinaleSceneMusicCut while it plays). LAST12.DMS
-- (file buffer 20): with Cybil saved (M6S04_CybilSaved) her part first, then Harry, Alessa and Dahlia;
-- then the boss rises: the Incubus (M5S03_KaufmannHasMotorcycleVial: Kaufmann has the vial;
-- LAST3.DMS/LAST4.DMS) or the Incubator (LAST5.DMS), and the fight starts. Skippable on
-- ending_step 1..3 (the scene parts); the scene parts advance ending_step themselves.
local Last12DmsBuf = 20
local BossDmsBuf = 18
local BossSlot = { Incubus = 3, Incubator = 5 } -- character slots (npcs[2], npcs[4])
-- Control states the boss behaviours take from the scene. The Incubator's slot runs Unknown23's
-- behaviour once IncubatorBehaviourFight hands it over, so its states are Unknown23's.
local IncubusRise, IncubusArm = "Incubus:rise", "Incubus:arm"
local IncubatorLift, IncubatorFloat = "Unknown23:lift", "Unknown23:float"

local function BossFlag()
    return sh.flag_get("M5S03_KaufmannHasMotorcycleVial")
end

-- The parts of the final boss intro (one tick each, on event step 0; the intro below runs them).
-- Each runs its own steps and, past the last one, moves ending_step on.

local function IntroMsg(key, cmds)
    sh.event_msg_audio("map7_s03." .. key, "ending_msg_audio_idx", cmds)
end

local function Pos(slot)
    return sh.npc.pos_x(slot), sh.npc.pos_y(slot), sh.npc.pos_z(slot)
end

-- From Cybil's last line on, the frame before is drawn over this one at half strength (an
-- afterimage). Cleared when the part starts.
local s_cybilAfterimage = false

local cybilIntroSceneSteps = {}

cybilIntroSceneSteps[0] = function()
    sh.map.set("ending_msg_audio_idx", 0)
    EndingCharaLoad(CharaId("EndingCybil"), 1)
    EndingCharaLoad(CharaId("Alessa"), 2)
    EndingCharaLoad(CharaId("EndingDahlia"), 3)
    EndingCharaLoadsFinish()
    EndingCharaPlace(CharaId("EndingCybil"), 1)
    EndingCharaPlace(CharaId("EndingDahlia"), 2)
    EndingCharaPlace(CharaId("Alessa"), 3)
    sh.map.set("update_arsia", 1)
    sh.map.set("update_daria", 1)
    sh.map.set("update_sibyl", 1)
    sh.light.set_intensity(2867) -- 0.7
    sh.npc.anim_flags_clear(0, 2)
    StepNext()
    s_cybilAfterimage = false
end

cybilIntroSceneSteps[1] = function()
    sh.event_chara_anim(0, 1, "Cybil:ending_walk_up", false)
    sh.event_chara_anim(0, 2, "Dahlia:stand_talk", false)
    sh.event_chara_anim(0, 3, "Alessa:kneel", false)
    StepNext()
end

cybilIntroSceneSteps[2] = function()
    sh.event_chara_anim(2, 1, 0, false) -- lock the animation
    StepNext()
    return cybilIntroSceneSteps[3]()
end

cybilIntroSceneSteps[3] = function()
    sh.event_fade_cmd(2, false, 0, 0, false) -- Auto, in
end

cybilIntroSceneSteps[4] = function()
    sh.event_chara_anim(3, 1, 0, false) -- unlock the animation
    StepNext()
    return cybilIntroSceneSteps[5]()
end

cybilIntroSceneSteps[5] = function()
    IntroMsg("i_was_shocked_to_realize", "cybil_intro_voice_cmds")
    TimerAdvance(40960, 0, 65536, true, false) -- 10.0, 0.0 .. 16.0
end

cybilIntroSceneSteps[6] = function()
    StepNext()
    return cybilIntroSceneSteps[7]()
end

cybilIntroSceneSteps[7] = function()
    IntroMsg("freeze", "cybil_intro_voice_cmds")
    TimerAdvance(40960, 69632, 126976, true, false) -- 17.0 .. 31.0
end

cybilIntroSceneSteps[8] = function()
    TimerAdvance(40960, 69632, 126976, true, true)
end

cybilIntroSceneSteps[9] = function()
    sh.event_chara_anim(0, 1, "Cybil:ending_shot", false)
    StepNext()
    return cybilIntroSceneSteps[10]()
end

cybilIntroSceneSteps[10] = function()
    TimerAdvance(40960, 131072, 151552, false, true) -- 32.0 .. 37.0
end

cybilIntroSceneSteps[11] = function()
    sh.flag_set("M7S03_FinaleSceneMusicCut", false)
    StepNext()
    do
        local x1, y1, z1 = Pos(2)
        local x2, y2, z2 = Pos(1)
        sh.effects.heat_field_start_between("EndingBurst", x1, y1, z1, x2, y2, z2)
    end
    return cybilIntroSceneSteps[12]()
end

cybilIntroSceneSteps[12] = function()
    IntroMsg("what_in_the_devils_name", "cybil_intro_voice_cmds")
    TimerAdvance(40960, 131072, 241664, true, false) -- 32.0 .. 59.0
    sh.effects.heat_field_update("EndingBurst")
end

cybilIntroSceneSteps[13] = function()
    sh.event_chara_anim(0, 1, "Cybil:ending_shot_fall", false)
    StepNext()
    return cybilIntroSceneSteps[14]()
end

cybilIntroSceneSteps[14] = function()
    TimerAdvance(40960, 131072, 241664, true, false)
    sh.effects.heat_field_update("EndingBurst")
    sh.event.wait_timer(4096, false) -- 1.0 s
end

cybilIntroSceneSteps[15] = function()
    sh.event_chara_anim(0, 1, "Cybil:ending_stand_hold", false)
    IntroMsg("msg23", "cybil_intro_voice_cmds")
    sh.light.set_intensity(2457) -- 0.6
    s_cybilAfterimage = true
    StepNext()
    return cybilIntroSceneSteps[16]()
end

cybilIntroSceneSteps[16] = function()
    TimerAdvance(40960, 249856, 372736, true, true) -- 61.0 .. 91.0
end

cybilIntroSceneSteps[17] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
end

cybilIntroSceneSteps.default = function()
    sh.event_step_set(0, 0) -- plain store, no timer reset
    EndingStepNext()
    sh.npc.anim_flags_set(0, 2)
    sh.npc.set_chara_id(1, "None")
    sh.map.set("update_sibyl", 0)
end

-- Cybil's part (Cybil saved, M6S04_CybilSaved; LAST12.DMS): she walks up, is shot by Dahlia's spell and
-- falls; the heat burst goes from Dahlia to her.
local function CybilIntroScene()
    if sh.event_step_get(0) == 0 then
        s_cybilAfterimage = false
    end
    if s_cybilAfterimage then
        sh.render.frame_hold{source = "previous", tint = 128, blend = "half", layer = "overlay"}
    end

    local s = sh.event_step_get(0)
    runStep(cybilIntroSceneSteps, s)
end

-- Whether Harry's bottle smash already started, and whether the Incubator's lightning (the bolt
-- pool) runs at the end of every tick of Harry's part.
local s_harrySmashStarted = false
local s_harryBoltsRunning = false

local harryIntroSceneSteps = {}

harryIntroSceneSteps[0] = function()
    sh.map.set("update_hero", 1)
    s_harryBoltsRunning = false
    if not sh.flag_get("M6S04_CybilSaved") then
        EndingCharaLoad(CharaId("Alessa"), 2)
        EndingCharaLoad(CharaId("EndingDahlia"), 3)
        EndingCharaLoadsFinish()
        EndingCharaPlace(CharaId("EndingDahlia"), 2)
        EndingCharaPlace(CharaId("Alessa"), 3)
        sh.map.set("update_arsia", 1)
        sh.map.set("update_daria", 1)
    end
    sh.event_chara_anim(0, 0, "scene_sqsu_2", false) -- the bottle raised
    sh.map.set("ending_msg_audio_idx", 0)
    s_harrySmashStarted = false
    sh.flag_set("M7S03_FinaleMusicLayer2", true)
    sh.light.set_intensity(3276) -- 0.8
    StepNext()
end

harryIntroSceneSteps[1] = function()
    if not sh.flag_get("M6S04_CybilSaved") then
        sh.event_chara_anim(0, 2, "Dahlia:stand_talk", false)
        sh.event_chara_anim(0, 3, "Alessa:kneel", false)
    end
    StepNext()
    return harryIntroSceneSteps[2]()
end

harryIntroSceneSteps[2] = function()
    sh.event_fade_cmd(2, false, 0, 0, false) -- Auto, in
    TimerAdvance(40960, 376832, 458752, true, false) -- 10.0, 92.0 .. 112.0
end

harryIntroSceneSteps[3] = function()
    IntroMsg("dahlia", "harry_intro_voice_cmds")
    TimerAdvance(40960, 376832, 458752, true, false)
    do
        -- PC: the smash also starts when the raise has already gone back to the looping idle (-1):
        -- at a high frame rate the raise's end is missed and the smash waited ~10 s.
        -- Asked only until the smash starts: the answer is unused after that, and asking runs the
        -- stuck-clip nets, which would flag the long dialogue over the looping smash as stuck.
        local playback = not s_harrySmashStarted and sh.npc.anim_playback_state(0)
        if playback == 1 or playback == -1 then
            sh.event_chara_anim(0, 0, "scene_lighthouse_2", false) -- the smash
            s_harrySmashStarted = true
        end
    end
end

harryIntroSceneSteps[4] = function()
    sh.event_chara_anim(0, 2, "Dahlia:scene_pose_a", false)
    StepNext()
    return harryIntroSceneSteps[5]()
end

harryIntroSceneSteps[5] = function()
    IntroMsg("dont_you_see_shes_right", "harry_intro_voice_cmds")
    TimerAdvance(40960, 462848, 528384, true, false) -- 113.0 .. 129.0
end

harryIntroSceneSteps[6] = function()
    TimerAdvance(40960, 462848, 528384, true, true)
end

harryIntroSceneSteps[7] = function()
    sh.event_chara_anim(0, 0, "scene_bar_fight_1", false)
    EndingCharaLoad(CharaId("Incubator"), 1)
    sh.map.set("incubator_load_state", 1)
    StepNext()
    return harryIntroSceneSteps[8]()
end

harryIntroSceneSteps[8] = function()
    TimerAdvance(40960, 462848, 548864, false, true) -- 113.0 .. 134.0
end

harryIntroSceneSteps[9] = function()
    if sh.load.chunks() then
        StepNext()
    end
end

harryIntroSceneSteps[10] = function()
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_chara_anim(0, 2, "Dahlia:scene_to_stand", false)
    StepNext()
    return harryIntroSceneSteps[11]()
end

harryIntroSceneSteps[11] = function()
    IntroMsg("thats_absurd", "harry_intro_voice_cmds")
    TimerAdvance(40960, 552960, 581632, true, false) -- 135.0 .. 142.0
end

harryIntroSceneSteps[12] = function()
    IntroMsg("its_been_a_long_seven", "harry_intro_voice_cmds")
    TimerAdvance(40960, 552960, 581632, true, false)
end

harryIntroSceneSteps[13] = function()
    IntroMsg("everyone_will_be_released_from", "harry_intro_voice_cmds")
    TimerAdvance(40960, 552960, 581632, true, false)
end

harryIntroSceneSteps[14] = function()
    sh.event_chara_anim(0, 2, "Dahlia:walk_ending", false)
    StepNext()
    return harryIntroSceneSteps[15]()
end

harryIntroSceneSteps[15] = function()
    IntroMsg("my_daughter_will_be_the", "harry_intro_voice_cmds")
    TimerAdvance(34816, 585728, 847872, true, false) -- 8.5, 143.0 .. 207.0
    sh.flag_set("M7S03_FinaleSceneMusicCut", false)
    sh.flag_set("M7S03_FinaleMusicLayer3", true)
    sh.flag_set("M7S03_FinaleMusicLayer4", true)
    if sh.event_step_get(0) ~= 15 then
        sh.sd_call("stop_xa_audio")
    end
end

harryIntroSceneSteps[16] = function()
    sh.sd_call("finale_harry_intro_voice")
    StepNext()
    return harryIntroSceneSteps[17]()
end

harryIntroSceneSteps[17] = function()
    TimerAdvance(34816, 585728, 847872, true, false)
    if sh.sound.streaming() == 1 then
        StepNext()
    end
end

harryIntroSceneSteps[18] = function()
    TimerAdvance(34816, 585728, 847872, true, false)
    if sh.sound.streaming() == 1 then
        StepNext()
    else
        return
    end
    return harryIntroSceneSteps[19]()
end

harryIntroSceneSteps[19] = function()
    sh.event_chara_anim(0, 0, "ending_boss_intro", false)
    sh.event_chara_anim(0, 2, "Dahlia:pose_hold_a", false)
    sh.map.boss_fx_mode_set(1)
    StepNext()
    return harryIntroSceneSteps[20]()
end

harryIntroSceneSteps[20] = function()
    TimerAdvance(40960, 851968, 946176, true, true) -- 208.0 .. 231.0
end

harryIntroSceneSteps[21] = function()
    sh.map.set("update_daria", 0)
    sh.npc.anim_flags_clear(2, 2)
    EndingCharaLoadsFinish()
    EndingCharaPlace(CharaId("Incubator"), 5)
    sh.map.set("incubator_load_state", 2)
    sh.npc.anim_flags_clear(5, 2)
    StepNext()
    return harryIntroSceneSteps[22]()
end

harryIntroSceneSteps[22] = function()
    TimerAdvance(40960, 950272, 1175552, false, true) -- 232.0 .. 287.0
    if sh.map.get("cutscene_timer") > 942080 then -- 230.0
        if sh.map.get("intro_fx_stage") == 0 then
            sh.npc.anim_flags_clear(3, 2)
            sh.map.boss_fx_mode_set(2)
            sh.map.set("intro_fx_stage", 1)
        end
    end
    if sh.map.get("cutscene_timer") > 1064960 and sh.map.get("intro_fx_stage") == 1 then -- 260.0
        sh.map.set("intro_fx_stage", 2)
    end
    if sh.map.get("cutscene_timer") > 1167360 then -- 285.0
        if sh.map.get("intro_fx_stage") == 2 then
            sh.map.boss_fx_mode_set(3)
            sh.map.set("intro_fx_stage", 3)
        end
    end
end

harryIntroSceneSteps[23] = function()
    sh.npc.set_chara_id(3, "None")
    sh.map.set("update_arsia", 0)
    sh.map.set("update_nu", 0)
    StepNext()
    return harryIntroSceneSteps[24]()
end

harryIntroSceneSteps[24] = function()
    TimerAdvance(40960, 950272, 1196032, false, true) -- 232.0 .. 292.0
end

harryIntroSceneSteps[25] = function()
    sh.event_chara_anim(0, 5, "Incubator:appear", false)
    sh.map.set("update_mar", 1)
    sh.npc.anim_flags_set(5, 2)
    sh.effects.bolt_pool_attach("BossBolts", "Ending", sh.npc.pos_x(2), sh.npc.pos_z(2))
    s_harryBoltsRunning = true
    StepNext()
    return harryIntroSceneSteps[26]()
end

harryIntroSceneSteps[26] = function()
    TimerAdvance(40960, 950272, 1359872, false, true) -- 232.0 .. 332.0
end

harryIntroSceneSteps[27] = function()
    sh.event_chara_anim(0, 5, "Incubator:float", false)
    StepNext()
    return harryIntroSceneSteps[28]()
end

harryIntroSceneSteps[28] = function()
    TimerAdvance(40960, 950272, 1396736, true, true) -- 232.0 .. 341.0
end

harryIntroSceneSteps[29] = function()
    sh.map.set("update_arsia", 0)
    EndingFxTexturesLoad()
    if BossFlag() then
        sh.load.file("ANIM/LAST3.DMS", BossDmsBuf)
        sh.map.set("last3_dms_queued", 1)
    else
        sh.load.file("ANIM/LAST5.DMS", BossDmsBuf)
        sh.map.set("last5_dms_queued", 1)
    end
    StepNext()
    return harryIntroSceneSteps[30]()
end

harryIntroSceneSteps[30] = function()
    sh.event.wait_timer(16384, false) -- 4.0 s
end

harryIntroSceneSteps.default = function()
    sh.event_step_set(0, 0) -- plain store, no timer reset
    EndingStepNext()
end

-- Harry's part (LAST12.DMS): he smashes the bottle, Dahlia talks, Alessa is burned, the Incubator
-- appears; LAST3.DMS (the Incubus) or LAST5.DMS (the Incubator) is queued into buffer 18.
local function HarryIntroScene()
    local s = sh.event_step_get(0)
    runStep(harryIntroSceneSteps, s)
    if s_harryBoltsRunning then
        sh.effects.bolt_pool_update("BossBolts")
    end
end

local incubusIntroSceneSteps = {}

incubusIntroSceneSteps[0] = function()
    sh.load.wait()
    sh.map.set("ending_msg_audio_idx", 0)
    sh.cutscene.prepare(BossDmsBuf)
    sh.map.set("ending_dms_idx", 1)
    sh.map.set("cutscene_timer", 0)
    EndingCharaLoad(CharaId("EndingKaufmann"), 2)
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_chara_anim(0, 2, "Dahlia:appear_flash", false)
    sh.sound.play_with_flags("dahlia_appear_flash", "pos:2", 200, 0) -- 0.785, no flags
    sh.light.set_intensity(2867) -- 0.7
    sh.npc.anim_flags_set(2, 2)
    sh.flag_set("M7S03_FinaleMusicLayer1", false)
    sh.flag_set("M7S03_FinaleMusicLayer2", false)
    sh.flag_set("M7S03_FinaleMusicLayer3", false)
    sh.flag_set("M7S03_FinaleMusicLayer4", false)
    sh.flag_set("M7S03_FinaleSceneMusicCut", true)
    sh.map.set("update_daria", 1)
    StepNext()
    return incubusIntroSceneSteps[1]()
end

incubusIntroSceneSteps[1] = function()
    sh.sound.bgm_crossfade(29)
end

incubusIntroSceneSteps[2] = function()
    TimerAdvance(40960, 0, 49152, true, true) -- 10.0, 0.0 .. 12.0
end

incubusIntroSceneSteps[3] = function()
    TimerAdvance(40960, 53248, 151552, true, true) -- 13.0 .. 37.0
end

incubusIntroSceneSteps[4] = function()
    EndingCharaLoadsFinish()
    EndingCharaPlace(CharaId("EndingKaufmann"), 4)
    sh.map.set("update_kau", 1)
    StepNext()
end

incubusIntroSceneSteps[5] = function()
    sh.event_chara_anim(0, 4, "Kaufmann:walk_in", false)
    StepNext()
    return incubusIntroSceneSteps[6]()
end

incubusIntroSceneSteps[6] = function()
    TimerAdvance(40960, 155648, 208896, true, true) -- 38.0 .. 51.0
end

incubusIntroSceneSteps[7] = function()
    TimerAdvance(40960, 212992, 237568, true, true) -- 52.0 .. 58.0
end

incubusIntroSceneSteps[8] = function()
    sh.map.set("cutscene_timer", 241664) -- 59.0
    sh.event_chara_anim(0, 4, "Kaufmann:stand_ready", false)
    sh.event_chara_anim(0, 2, "Dahlia:pose_hold_b", false)
    sh.flag_set("M7S03_FinaleMusicLayer1", true)
    StepNext()
    return incubusIntroSceneSteps[9]()
end

incubusIntroSceneSteps[9] = function()
    IntroMsg("quit_screwing_around", "incubus_intro_voice_cmds")
end

incubusIntroSceneSteps[10] = function()
    sh.chara_anim_play_to_end(2, "Dahlia:scene_pose_b")
    TimerAdvance(40960, 245760, 319488, true, false) -- 60.0 .. 78.0
end

incubusIntroSceneSteps[11] = function()
    sh.event_chara_anim(0, 2, "Dahlia:pose_hold_c", false)
    StepNext()
    return incubusIntroSceneSteps[12]()
end

incubusIntroSceneSteps[12] = function()
    IntroMsg("your_role_is_over_we", "incubus_intro_voice_cmds")
    TimerAdvance(40960, 245760, 319488, true, false)
end

incubusIntroSceneSteps[13] = function()
    sh.chara_anim_play_to_end(4, "Kaufmann:take_out_vial")
    TimerAdvance(40960, 323584, 409600, true, true) -- 79.0 .. 100.0
end

incubusIntroSceneSteps[14] = function()
    sh.event_chara_anim(0, 4, "Kaufmann:hold_vial", false)
    sh.map.set("cutscene_timer", 413696) -- 101.0
    StepNext()
    return incubusIntroSceneSteps[15]()
end

incubusIntroSceneSteps[15] = function()
    IntroMsg("aglaophotis_i_thought_i_got", "incubus_intro_voice_cmds")
end

incubusIntroSceneSteps[16] = function()
    sh.event_chara_anim(0, 2, "Dahlia:scene_pose_c", false)
    sh.light.set_intensity(2457) -- 0.6
    sh.flag_set("M7S03_FinaleMusicLayer1", false)
    StepNext()
    return incubusIntroSceneSteps[17]()
end

incubusIntroSceneSteps[17] = function()
    IntroMsg("stop_it", "incubus_intro_voice_cmds")
    TimerAdvance(40960, 417792, 475136, true, false) -- 102.0 .. 116.0
end

incubusIntroSceneSteps[18] = function()
    TimerAdvance(40960, 417792, 475136, true, true)
end

incubusIntroSceneSteps[19] = function()
    sh.npc.set_chara_id(2, "None")
    sh.npc.set_chara_id(5, "None")
    sh.map.set("update_mar", 0)
    sh.map.set("update_daria", 0)
    sh.npc.model_buffers_assign(1)
    EndingCharaLoad(CharaId("Incubator"), 3)
    sh.event_chara_anim(0, 4, "Kaufmann:throw_vial", false)
    sh.light.set_intensity(2867) -- 0.7
    StepNext()
    return incubusIntroSceneSteps[20]()
end

incubusIntroSceneSteps[20] = function()
    TimerAdvance(40960, 479232, 638976, true, true) -- 117.0 .. 156.0
end

incubusIntroSceneSteps[21] = function()
    sh.map.set("update_bin", 1)
    sh.map.set("bin_spin", -1906)
    if sh.event_step_get(0) ~= 18 then
        sh.flag_set("M7S03_FinaleSceneMusicCut", false)
    end
    sh.sound.bgm_play_new(40)
    StepNext()
    return incubusIntroSceneSteps[22]()
end

incubusIntroSceneSteps[22] = function()
    TimerAdvance(40960, 643072, 790528, true, true) -- 157.0 .. 193.0
end

incubusIntroSceneSteps[23] = function()
    EndingCharaLoadsFinish()
    EndingCharaPlace(CharaId("Incubator"), 5)
    EndingCharaLoad(CharaId("BloodyIncubator"), 1)
    StepNext()
    return incubusIntroSceneSteps[24]()
end

incubusIntroSceneSteps[24] = function()
    TimerAdvance(30720, 794624, 1146880, true, true) -- 7.5, 194.0 .. 280.0
end

incubusIntroSceneSteps[25] = function()
    if sh.load.chunks() then
        StepNext()
    else
        return
    end
    return incubusIntroSceneSteps[26]()
end

incubusIntroSceneSteps[26] = function()
    sh.map.set("update_mar", 1)
    EndingCharaLoadsFinish()
    EndingCharaPlace(CharaId("BloodyIncubator"), 6)
    sh.npc.anim_flags_clear(6, 2)
    sh.event_chara_anim(0, 5, "Incubator:float", false)
    sh.npc.transparent_set(CharaId("BloodyIncubator"), false)
    sh.light.set_intensity(2457) -- 0.6
    sh.xa_preload("incubus_intro_scream_voice")
    StepNext()
    return incubusIntroSceneSteps[27]()
end

incubusIntroSceneSteps[27] = function()
    TimerAdvance(40960, 1150976, 1282048, false, true) -- 281.0 .. 313.0
end

incubusIntroSceneSteps[28] = function()
    StepNext()
    return incubusIntroSceneSteps[29]()
end

incubusIntroSceneSteps[29] = function()
    StepNext()
    return incubusIntroSceneSteps[30]()
end

incubusIntroSceneSteps[30] = function()
    sh.sd_call("incubus_intro_scream_voice")
    -- PC: the bottle breaks on impact (the end of the throw, with the scream), not after the hold.
    if sh.map.get("update_bin") ~= 0 then
        sh.effects.heat_field_start("BottleShatter", sh.map.get("bin.pos.x"), sh.map.get("bin.pos.y"),
                                    sh.map.get("bin.pos.z"))
        sh.map.set("update_bin", 0)
    end
    StepNext()
    return incubusIntroSceneSteps[31]()
end

incubusIntroSceneSteps[31] = function()
    TimerAdvance(40960, 1150976, 1310720, true, false) -- 281.0 .. 320.0
    if sh.map.get("update_bin") == 0 then
        sh.effects.heat_field_update("BottleShatter")
    end
    -- PC: the voice stays the active stream through its whole playback; the impact is over once
    -- the time reaches 320.0, so the scene goes on and the scream overlaps the transformation.
    if sh.map.get("cutscene_timer") >= 1310720 then
        StepNext()
        return
    end
    if sh.sound.streaming() ~= 1 then
        StepNext()
    end
end

incubusIntroSceneSteps[32] = function()
    if sh.sound.streaming() ~= 1 then
        TimerAdvance(40960, 1150976, 1310720, true, false)
        -- PC: the voice already started and ended during the step before; at 320.0 go on.
        if sh.map.get("cutscene_timer") < 1310720 then
            return
        end
    end
    StepNext()
    return incubusIntroSceneSteps[33]()
end

incubusIntroSceneSteps[33] = function()
    if sh.map.get("update_bin") ~= 0 then
        sh.effects.heat_field_start("BottleShatter", sh.map.get("bin.pos.x"), sh.map.get("bin.pos.y"),
                                    sh.map.get("bin.pos.z"))
    end
    sh.map.boss_fx_mode_set(9)
    StepNext()
    return incubusIntroSceneSteps[34]()
end

incubusIntroSceneSteps[34] = function()
    sh.map.set("cutscene_timer", 1314816) -- 321.0
    sh.event.wait_timer(4096, false) -- 1.0 s
    sh.map.set("update_bin", 0)
    sh.effects.heat_field_update("BottleShatter")
end

incubusIntroSceneSteps[35] = function()
    sh.map.set("update_mar", 0)
    sh.npc.set_chara_id(5, "None")
    sh.map.set("update_bar", 1)
    sh.npc.anim_flags_set(6, 2)
    sh.event_chara_anim(0, 6, "BloodyIncubator:rise", false)
    StepNext()
    return incubusIntroSceneSteps[36]()
end

incubusIntroSceneSteps[36] = function()
    if sh.sound.streaming() == 0 then
        EndingCharaLoad(CharaId("EndingDahlia"), 3)
        StepNext()
    end
    sh.effects.heat_field_update("BottleShatter")
    TimerAdvance(40960, 1318912, 1601536, true, false) -- 322.0 .. 391.0
end

incubusIntroSceneSteps[37] = function()
    TimerAdvance(40960, 1318912, 1601536, true, true)
    if sh.event_step_get(0) ~= 37 then
        sh.event_chara_anim(0, 0, "cutscene_stand", false)
    end
end

incubusIntroSceneSteps[38] = function()
    if sh.load.chunks() then
        StepNext()
    end
    sh.light.set_intensity(2867) -- 0.7
    sh.map.set("cutscene_timer", 1605632) -- 392.0
end

incubusIntroSceneSteps[39] = function()
    IntroMsg("msg68", "incubus_intro_voice_cmds")
    sh.flag_set("M7S03_FinaleMusicLayer1", true)
end

incubusIntroSceneSteps[40] = function()
    EndingCharaLoadsFinish()
    EndingCharaPlace(CharaId("EndingDahlia"), 2)
    StepNext()
end

incubusIntroSceneSteps[41] = function()
    sh.map.set("update_daria", 1)
    sh.event_chara_anim(0, 2, "Dahlia:pose_hold_c", false)
    sh.light.set_intensity(2457) -- 0.6
    sh.map.set("cutscene_timer", 1609728) -- 393.0
    StepNext()
    return incubusIntroSceneSteps[42]()
end

incubusIntroSceneSteps[42] = function()
    IntroMsg("msg70", "incubus_intro_voice_cmds")
end

incubusIntroSceneSteps[43] = function()
    sh.event_chara_anim(0, 4, "Kaufmann:back_away", false)
    sh.light.set_intensity(2867) -- 0.7
    sh.map.set("update_daria", 0)
    sh.npc.set_chara_id(2, "None")
    StepNext()
    return incubusIntroSceneSteps[44]()
end

incubusIntroSceneSteps[44] = function()
    IntroMsg("what_on_earth_thats_not", "incubus_intro_voice_cmds")
    TimerAdvance(40960, 1613824, 1708032, true, false) -- 394.0 .. 417.0
end

incubusIntroSceneSteps[45] = function()
    EndingCharaLoad(CharaId("LittleIncubus"), 3)
    StepNext()
    return incubusIntroSceneSteps[46]()
end

incubusIntroSceneSteps[46] = function()
    TimerAdvance(40960, 1613824, 1708032, true, true)
end

incubusIntroSceneSteps[47] = function()
    if sh.load.chunks() then
        StepNext()
    end
end

incubusIntroSceneSteps[48] = function()
    EndingCharaLoadsFinish()
    EndingCharaPlace(CharaId("LittleIncubus"), 1)
    sh.map.set("update_litl", 1)
    sh.npc.transparent_set(CharaId("BloodyIncubator"), true)
    sh.flag_set("M7S03_FinaleSceneMusicCut", true)
    sh.light.set_intensity(2048) -- 0.5
    StepNext()
    return incubusIntroSceneSteps[49]()
end

incubusIntroSceneSteps[49] = function()
    TimerAdvance(40960, 1712128, 2039808, true, true) -- 418.0 .. 498.0
    -- The Little Incubus bleeds: a drop every 0.2..0.3 s, and the Bloody Incubator fades.
    sh.event.set_state_timer(sh.event.state_timer() + sh.game.delta_time())
    sh.npc.set_aux_timer(6, sh.npc.aux_timer(6) + (sh.game.delta_time() * 256) // 4096) -- + 0.0625 per second
    if sh.event.state_timer() > 1228 then -- 0.3 s
        local r = sh.rng.rand16()
        local x, y, z = Pos(1)
        sh.event.set_state_timer(sh.event.state_timer() - 819) -- 0.2 s
        sh.event.set_state_timer(sh.event.state_timer() - r % 409) -- 0 .. 0.1 s
        sh.npc.effect_spawn(1, x + 819, y - 3686, z + 819, 8, 1) -- + (0.2, -0.9, 0.2)
    end
end

incubusIntroSceneSteps[50] = function()
    sh.npc.set_chara_id(6, "None")
    sh.npc.set_chara_id(1, "None")
    sh.map.set("update_litl", 0)
    sh.map.set("update_bar", 0)
    EndingCharaLoad(CharaId("Incubus"), 1)
    sh.map.set("incubus_load_state", 1)
    sh.event_chara_anim(0, 0, "ending_boss_intro", false)
    sh.event_chara_anim(0, 4, "Kaufmann:stand_ready", false)
    sh.world.effects_reset()
    sh.light.set_intensity(2867) -- 0.7
    StepNext()
    return incubusIntroSceneSteps[51]()
end

incubusIntroSceneSteps[51] = function()
    TimerAdvance(40960, 2043904, 2084864, true, true) -- 499.0 .. 509.0
end

incubusIntroSceneSteps[52] = function()
    EndingCharaLoadsFinish()
    EndingCharaPlace(CharaId("Incubus"), BossSlot.Incubus)
    sh.map.set("incubus_load_state", 2)
    sh.map.set("update_bos", 1)
    StepNext()
end

incubusIntroSceneSteps[53] = function()
    sh.npc.control_request(BossSlot.Incubus, IncubusRise)
    EndingCharaLoad(CharaId("EndingDahlia"), 3)
    sh.light.set_intensity(3276) -- 0.8
    StepNext()
    return incubusIntroSceneSteps[54]()
end

incubusIntroSceneSteps[54] = function()
    TimerAdvance(40960, 2088960, 2207744, true, true) -- 510.0 .. 539.0
end

incubusIntroSceneSteps[55] = function()
    EndingCharaLoadsFinish()
    EndingCharaPlace(CharaId("EndingDahlia"), 2)
    sh.map.set("update_daria", 1)
    StepNext()
end

incubusIntroSceneSteps[56] = function()
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_chara_anim(0, 2, "Dahlia:hold_frozen", false)
    sh.light.set_intensity(3686) -- 0.9
    sh.load.file("ANIM/LAST4.DMS", Last12DmsBuf)
    sh.map.set("last4_dms_queued", 0)
    StepNext()
    return incubusIntroSceneSteps[57]()
end

incubusIntroSceneSteps[57] = function()
    TimerAdvance(40960, 2211840, 2400256, true, true) -- 540.0 .. 586.0
end

incubusIntroSceneSteps[58] = function()
    TimerAdvance(40960, 2404352, 2519040, false, true) -- 587.0 .. 615.0
    sh.light.set_intensity(3276) -- 0.8
end

incubusIntroSceneSteps[59] = function()
    sh.map.boss_fx_mode_set(10)
    sh.effects.fire_pool_attached_reset("BossFire")
    sh.effects.fire_pool_attach("BossFire")
    StepNext()
    return incubusIntroSceneSteps[60]()
end

incubusIntroSceneSteps[60] = function()
    TimerAdvance(40960, 2404352, 2576384, false, true) -- 587.0 .. 629.0
end

incubusIntroSceneSteps[61] = function()
    sh.event_chara_anim(0, 2, "Dahlia:scene_walk_off", false)
    StepNext()
    return incubusIntroSceneSteps[62]()
end

incubusIntroSceneSteps[62] = function()
    TimerAdvance(40960, 2404352, 2990080, true, true) -- 587.0 .. 730.0
end

incubusIntroSceneSteps[63] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
end

incubusIntroSceneSteps.default = function()
    sh.event_step_set(0, 0) -- plain store, no timer reset
    EndingStepNext()
    sh.npc.set_chara_id(4, "None")
    sh.npc.set_chara_id(2, "None")
    sh.map.set("update_kau", 0)
    sh.map.set("update_daria", 0)
    sh.npc.model_buffers_assign(6)
end

-- The Incubus rises (Kaufmann has the vial; LAST3.DMS in buffer 18): Kaufmann walks in and throws
-- the vial, the Incubator turns into the Bloody Incubator and then the Little Incubus, the Incubus
-- comes out of it, Dahlia burns; LAST4.DMS is queued into buffer 20.
local function IncubusIntroScene()
    local s = sh.event_step_get(0)
    runStep(incubusIntroSceneSteps, s < 0 and 31 or s)
    s = sh.event_step_get(0)
    if s >= 62 and s < 64 then
        sh.effects.fire_pool_draw_attached("BossFire", 2) -- Dahlia burns
    end
end

local incubatorIntroSceneSteps = {}

incubatorIntroSceneSteps[0] = function()
    sh.load.wait()
    sh.cutscene.prepare(BossDmsBuf)
    sh.map.set("ending_dms_idx", 1)
    sh.map.set("cutscene_timer", 0)
    sh.map.set("ending_msg_audio_idx", 0)
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.map.boss_fx_mode_set(4)
    sh.npc.anim_flags_set(2, 2)
    sh.light.set_intensity(3276) -- 0.8
    StepNext()
    return incubatorIntroSceneSteps[1]()
end

incubatorIntroSceneSteps[1] = function()
    IntroMsg("its_awakening_its_awake", "incubator_intro_voice_cmds")
end

incubatorIntroSceneSteps[2] = function()
    IntroMsg("cheryl", "incubator_intro_voice_cmds")
    sh.map.set("cutscene_timer", 4096) -- 1.0
end

incubatorIntroSceneSteps[3] = function()
    IntroMsg("this_is_a_god", "incubator_intro_voice_cmds")
    TimerAdvance(40960, 8192, 135168, true, false) -- 10.0, 2.0 .. 33.0
end

incubatorIntroSceneSteps[4] = function()
    sh.event_chara_anim(0, 2, "Dahlia:talk_scold", false)
    IntroMsg("msg80", "incubator_intro_voice_cmds")
    sh.map.boss_fx_mode_set(5)
    sh.effects.fire_pool_attached_reset("BossFire")
    sh.effects.fire_pool_attach("BossFire")
    StepNext()
    return incubatorIntroSceneSteps[5]()
end

incubatorIntroSceneSteps[5] = function()
    TimerAdvance(40960, 8192, 167936, true, true) -- 2.0 .. 41.0
end

incubatorIntroSceneSteps[6] = function()
    TimerAdvance(40960, 172032, 278528, true, true) -- 42.0 .. 68.0
end

incubatorIntroSceneSteps[7] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
end

incubatorIntroSceneSteps.default = function()
    sh.event_step_set(0, 0) -- plain store, no timer reset
    EndingStepNext()
    sh.npc.set_chara_id(2, "None")
    sh.map.set("update_daria", 0)
end

-- The Incubator rises (LAST5.DMS in buffer 18): Cheryl's last words, Dahlia scolds her and burns.
local function IncubatorIntroScene()
    local s = sh.event_step_get(0)
    runStep(incubatorIntroSceneSteps, s)
    s = sh.event_step_get(0)
    if s >= 5 and s < 8 then
        sh.effects.fire_pool_draw_attached("BossFire", 2) -- Dahlia burns
    end
end

local finalBossIntroSteps = {}

finalBossIntroSteps[0] = function()
    sh.player_freeze(true)
    sh.game.state_update()
    sh.map_effects_init(18, 18)
    sh.world.water_enable()

    sh.map.set("update_sibyl", 0)
    sh.map.set("update_daria", 0)
    sh.map.set("update_arsia", 0)
    sh.map.set("update_kau", 0)
    sh.map.set("update_mar", 0)
    sh.map.set("update_bar", 0)
    sh.map.set("update_litl", 0)
    sh.map.set("update_bos", 0)
    sh.map.set("update_bin", 0)
    sh.map.set("update_nu", 1)
    sh.map.set("update_baby", 0)
    sh.map.set("incubus_load_state", 0)
    sh.map.set("incubator_load_state", 0)
    sh.map.set("last3_dms_queued", 0)
    sh.map.set("last4_dms_queued", 0)
    sh.map.set("last5_dms_queued", 0)
    sh.map.set("real_world_shown", 0)
    sh.map.set("ura_world_shown", 0)
    sh.map.set("under_world_shown", 0)
    sh.map.set("intro_fx_stage", 0)
    -- PC: the boss effects of an earlier fight never carry over into the scene.
    sh.map.set("boss_fx_time", 0)
    sh.map.set("boss_fx_mode", -1) -- NO_VALUE

    sh.npc.anim_info_clear()
    sh.npc.model_buffers_assign(255) -- free all character model buffers

    sh.screen_fade_reset_timestep()
    sh.cutscene.border_show()
    sh.sys_flags_set(sh.sys_flags_get() | SYSFLAG_CUTSCENE_ACTIVE)

    BossFxTextureLoad()

    sh.load.file("ANIM/LAST12.DMS", Last12DmsBuf)
    sh.load.wait()
    sh.cutscene.prepare(Last12DmsBuf)

    sh.map.set("ending_dms_idx", 0)
    sh.map.set("cutscene_timer", 0)

    sh.held_item_hide()
    sh.player.weapon_stash()
    IncubatorBehaviourScripted()
    EndingAnimFilesDefault()
    sh.npc.anim_info_clear()

    sh.flag_set("M7S03_FinaleSceneMusicCut", true)
    sh.flag_set("M7S03_FinaleMusicLayer1", true)
    sh.map.set("ending_step", sh.map.get("ending_step") + 1)
    return finalBossIntroSteps[1]()
end

finalBossIntroSteps[1] = function()
    if sh.flag_get("M6S04_CybilSaved") then
        CybilIntroScene()
    else
        sh.map.set("ending_step", sh.map.get("ending_step") + 1)
        HarryIntroScene()
    end
end

finalBossIntroSteps[2] = function()
    HarryIntroScene()
end

finalBossIntroSteps[3] = function()
    if BossFlag() then
        IncubusIntroScene()
    else
        IncubatorIntroScene()
    end
end

finalBossIntroSteps[5] = function()
    sh.event_fade_cmd(0, true, 0, 0, false) -- Start, out
    if BossFlag() then
        sh.map.set("ending_step", sh.map.get("ending_step") + 1)
        sh.event.step_set(0, 0)
    else
        sh.map.set("ending_step", sh.map.get("ending_step") + 2)
    end
    return finalBossIntroSteps[6]()
end

finalBossIntroSteps[6] = function()
    sh.sound.bgm_crossfade(40)
    if sh.event_step_get(0) ~= 0 then
        sh.map.set("ending_step", sh.map.get("ending_step") + 1)
    end
end

finalBossIntroSteps[7] = function()
    if sh.screen_fade_finished() then
        sh.map.set("ending_step", sh.map.get("ending_step") + 1)
    end
end

finalBossIntroSteps[8] = function()
    for slot = 1, sh.npc.slot_count() do
        sh.npc.set_chara_id(slot, "None")
    end

    if BossFlag() then
        if sh.map.get("last3_dms_queued") == 0 then
            sh.load.file("ANIM/LAST3.DMS", BossDmsBuf)
        end
        sh.load.wait()
        sh.cutscene.prepare(BossDmsBuf)

        if sh.map.get("last4_dms_queued") == 0 then
            sh.load.file("ANIM/LAST4.DMS", Last12DmsBuf)
            sh.load.wait()
        end

        sh.map.set("ending_dms_idx", 1)

        if sh.map.get("incubus_load_state") ~= 2 then
            if sh.map.get("incubus_load_state") == 0 then
                sh.npc.model_buffers_assign(255)
                EndingCharaLoad(sh.chara_id("Incubus"), 1)
            end
            EndingCharaLoadsFinish()
        end

        EndingCharaPlace(sh.chara_id("Incubus"), BossSlot.Incubus)
        sh.npc.control_request(BossSlot.Incubus, IncubusRise)

        sh.map.set("update_baby", 0)
        sh.map.set("update_nu", 0)
        sh.map.set("update_bin", 0)
        sh.map.set("update_litl", 0)
        sh.map.set("update_bar", 0)
        sh.map.set("update_mar", 0)
        sh.map.set("update_kau", 0)
        sh.map.set("update_arsia", 0)
        sh.map.set("update_daria", 0)
        sh.map.set("update_sibyl", 0)
        sh.map.set("update_bos", 1)
        sh.map.set("cutscene_timer", 2744320) -- 670.0
    else
        if sh.map.get("last5_dms_queued") == 0 then
            sh.load.file("ANIM/LAST5.DMS", BossDmsBuf)
        end
        sh.load.wait()
        sh.cutscene.prepare(BossDmsBuf)

        sh.map.set("ending_dms_idx", 1)
        if sh.map.get("incubus_load_state") ~= 2 then
            if sh.map.get("incubus_load_state") == 0 then
                sh.npc.model_buffers_assign(255)
                EndingCharaLoad(sh.chara_id("Incubator"), 1)
            end
            EndingCharaLoadsFinish()
        end

        EndingCharaPlace(sh.chara_id("Incubator"), BossSlot.Incubator)
        IncubatorBehaviourFight()

        sh.map.set("update_baby", 0)
        sh.map.set("update_nu", 0)
        sh.map.set("update_bin", 0)
        sh.map.set("update_litl", 0)
        sh.map.set("update_bar", 0)
        sh.map.set("update_bos", 0)
        sh.map.set("update_kau", 0)
        sh.map.set("update_arsia", 0)
        sh.map.set("update_daria", 0)
        sh.map.set("update_sibyl", 0)
        sh.map.set("update_mar", 1)
        sh.map.set("cutscene_timer", 278528) -- 68.0

        sh.npc.control_request(BossSlot.Incubator, IncubatorLift)
    end

    EndingFxTexturesLoad()
    sh.load.wait()

    sh.map.set("update_hero", 1)

    sh.npc.anim_flags_set(0, 2)
    EndingObjectsUpdate()

    sh.map.set("ending_step", -1) -- NO_VALUE

    if BossFlag() then
        sh.map.boss_fx_mode_set(-1)
    else
        sh.map.boss_fx_mode_set(6)
    end

    sh.event_fade_cmd(0, false, 0, 0, false) -- Start, in
    return finalBossIntroSteps.default()
end

finalBossIntroSteps.default = function()
    sh.player_freeze(false, true)
    sh.state_set(0) -- gameplay
    sh.camera_auto_return(true)

    if BossFlag() then
        sh.npc.control_request(BossSlot.Incubus, IncubusArm)
    else
        IncubatorBehaviourFight()
        sh.npc.control_request(BossSlot.Incubator, IncubatorFloat)
    end

    sh.flag_set("M7S03_FinalBossIntroDone", true)

    sh.event_fade_cmd(0, false, 0, 0, false) -- Start, in
    sh.event_fade_cmd(0, false, 2, 0, false) -- Start, in, type 2

    sh.map.set("cutscene_timer", -1) -- NO_VALUE

    sh.held_item_show()
    sh.player.weapon_restore()
    sh.sd_call("stop_xa_audio")
    sh.world_objects.call("final_boss_camera", 0, BossSlot.Incubus, 1)

    sh.map.set("ending_step", 0)
    sh.light.set_intensity(4096) -- 1.0
end

function Cutscene_FinalBossIntro()
    local step = sh.map.get("ending_step")
    if sh.input_action("skip", 1) and step > 0 and step < 4 then
        sh.map.set("ending_step", 5)
        sh.flag_set("M7S03_FinaleSceneMusicCut", false)
        sh.sd_call("stop_xa_audio")
    end

    step = sh.map.get("ending_step")
    runStep(finalBossIntroSteps, step)
    EndingObjectsUpdate()

    if sh.map.get("update_mar") ~= 0 and sh.map.get("ending_step") == 3 and BossFlag() then
        sh.npc.set_rot_y(BossSlot.Incubator, sh.npc.rot_y(BossSlot.Incubator) + 2048) -- + 180 deg
    end
end

-- The ending scene bodies (one tick each, on event step 0). Each runs its own steps and, past
-- the last one, moves ending_step on. While boss_fx_time is set the bosses' lightning bolts
-- and the scene's effect cues run after the step.

local incubusDeathSceneSteps = {}

incubusDeathSceneSteps[0] = function()
    sh.cutscene.prepare(Last12DmsBuf)
    sh.map.set("ending_dms_idx", 0)
    sh.map.set("cutscene_timer", 0)
    sh.map.set("ending_msg_audio_idx", 0)
    sh.event_chara_anim(0, 0, "ending_scene_l", false)
    EndingCharaLoad(CharaId("BloodyIncubator"), 5)
    sh.map.set("boss_fx_time", 0)
    sh.rng.set_seed(0)
    sh.light.set_intensity(4096) -- 1.0
    StepNext()
    return incubusDeathSceneSteps[1]()
end

incubusDeathSceneSteps[1] = function()
    TimerAdvance(40960, 0, 98304, true, true) -- 10.0, 0.0 .. 24.0
    if sh.event_step_get(0) ~= 1 then
        EndingCharaLoadsFinish()
    end
end

incubusDeathSceneSteps[2] = function()
    sh.xa_preload("incubus_lightning_call_voice")
    sh.event.wait_timer(1228, false) -- 0.3 s
end

incubusDeathSceneSteps[3] = function()
    sh.sd_call("incubus_lightning_call_voice")
    sh.npc.control_request(BossSlot.Incubus, "Incubus:lightning_call")
    StepNext()
    return incubusDeathSceneSteps[4]()
end

incubusDeathSceneSteps[4] = function()
    TimerAdvance(40960, 102400, 716800, true, true) -- 25.0 .. 175.0
end

incubusDeathSceneSteps[5] = function()
    sh.event_chara_anim(0, 0, "ending_scene_m", false)
    sh.npc.transparent_set(CharaId("Incubus"), true)
    sh.map.boss_fx_mode_set(11)
    StepNext()
    return incubusDeathSceneSteps[6]()
end

incubusDeathSceneSteps[6] = function()
    TimerAdvance(40960, 720896, 819200, false, true) -- 176.0 .. 200.0
end

incubusDeathSceneSteps[7] = function()
    EndingCharaPlace(CharaId("BloodyIncubator"), 6)
    sh.npc.anim_flags_clear(6, 2)
    sh.map.boss_fx_mode_set(12)
    StepNext()
    return incubusDeathSceneSteps[8]()
end

incubusDeathSceneSteps[8] = function()
    TimerAdvance(40960, 720896, 1036288, true, true) -- 176.0 .. 253.0
    AuxTimerGrow(BossSlot.Incubus, 614, 4096) -- + 0.15 per second, up to 1.0
end

incubusDeathSceneSteps[9] = function()
    sh.map.set("update_bar", 1)
    sh.npc.anim_flags_set(6, 2)
    sh.event_chara_anim(0, 6, "BloodyIncubator:transform", false)
    sh.npc.transparent_set(CharaId("BloodyIncubator"), true)
    sh.npc.set_aux_timer(6, 3076) -- 0.751
    sh.npc.set_chara_id(BossSlot.Incubus, "None")
    sh.map.boss_fx_mode_set(13)
    sh.map.set("update_bos", 0)
    sh.map.lightning_init(3)
    sh.map.boss_fire_init()
    sh.flag_set("M7S03_FinaleMusicLayer2", true)
    StepNext()
    return incubusDeathSceneSteps[10]()
end

incubusDeathSceneSteps[10] = function()
    TimerAdvance(40960, 1040384, 1286144, true, true) -- 254.0 .. 314.0
    sh.npc.set_aux_timer(6, sh.npc.aux_timer(6) - (sh.game.delta_time() * 1024) // 4096) -- - 0.25 per second
    if sh.npc.aux_timer(6) < 0 then
        sh.npc.set_aux_timer(6, 0)
    end
end

incubusDeathSceneSteps[11] = function()
    sh.event_chara_anim(0, 0, "ending_scene_e", false)
    sh.event_chara_anim(0, 6, "BloodyIncubator:hover", false)
    sh.npc.transparent_set(CharaId("BloodyIncubator"), false)
    StepNext()
    sh.map.set("boss_fx_time", 1)
    sh.map.set("boss_fx_stage", 0)
    sh.flag_set("M7S03_FinaleMusicLayer3", true)
    return incubusDeathSceneSteps[12]()
end

incubusDeathSceneSteps[12] = function()
    TimerAdvance(40960, 1290240, 1720320, true, true) -- 315.0 .. 420.0
end

incubusDeathSceneSteps[13] = function()
    sh.event_chara_anim(0, 6, "BloodyIncubator:scene_a", false)
    sh.player.held_item_set("CutsceneBaby")
    sh.map.boss_fx_mode_set(14)
    StepNext()
    return incubusDeathSceneSteps[14]()
end

incubusDeathSceneSteps[14] = function()
    TimerAdvance(40960, 1724416, 1945600, true, true) -- 421.0 .. 475.0
end

incubusDeathSceneSteps[15] = function()
    TimerAdvance(40960, 1949696, 2195456, true, false) -- 476.0 .. 536.0
    StepNext()
end

incubusDeathSceneSteps[16] = function()
    TimerAdvance(40960, 1949696, 2195456, true, true)
end

incubusDeathSceneSteps[17] = function()
    sh.event_chara_anim(0, 6, "BloodyIncubator:scene_b", false)
    sh.map.set("update_baby", 1)
    SceneSfxPlay(SfxScene.Drone)
    sh.map.boss_fx_mode_set(15)
    StepNext()
    return incubusDeathSceneSteps[18]()
end

incubusDeathSceneSteps[18] = function()
    TimerAdvance(40960, 2199552, 2457600, true, true) -- 537.0 .. 600.0
end

incubusDeathSceneSteps[19] = function()
    sh.load.wait()
    sh.event_chara_anim(0, 0, "ending_scene_i", false)
    sh.npc.transparent_set(CharaId("BloodyIncubator"), true)
    sh.npc.material_set(CharaId("BloodyIncubator"), 0)
    sh.map.set("update_baby", 0)
    sh.held_item_show()
    StepNext()
    return incubusDeathSceneSteps[20]()
end

incubusDeathSceneSteps[20] = function()
    TimerAdvance(40960, 2461696, 2535424, false, true) -- 601.0 .. 619.0
end

incubusDeathSceneSteps[21] = function()
    sh.event_chara_anim(0, 6, "BloodyIncubator:scene_c", false)
    StepNext()
    return incubusDeathSceneSteps[22]()
end

incubusDeathSceneSteps[22] = function()
    TimerAdvance(40960, 2461696, 2600960, true, true) -- 601.0 .. 635.0
end

incubusDeathSceneSteps[23] = function()
    TimerAdvance(40960, 2605056, 2674688, true, false) -- 636.0 .. 653.0
    StepNext()
end

incubusDeathSceneSteps[24] = function()
    TimerAdvance(40960, 2605056, 2674688, true, true)
end

incubusDeathSceneSteps[25] = function()
    sh.event_chara_anim(0, 0, "ending_scene_j", false)
    StepNext()
    return incubusDeathSceneSteps[26]()
end

incubusDeathSceneSteps[26] = function()
    TimerAdvance(40960, 2678784, 2953216, true, true) -- 654.0 .. 721.0
end

incubusDeathSceneSteps[27] = function()
    sh.event_chara_anim(0, 6, "BloodyIncubator:approach", false)
    if sh.flag_get("M6S04_CybilSaved") then
        EndingCharaLoad(CharaId("EndingCybil"), 4)
    else
        EndingCharaLoad(CharaId("EndingKaufmann"), 4)
    end
    sh.sfx_stop(SfxScene.Drone)
    SceneSfxPlay(SfxScene.Thunder)
    -- (the original calls an empty map function here)
    StepNext()
    return incubusDeathSceneSteps[28]()
end

incubusDeathSceneSteps[28] = function()
    TimerAdvance(40960, 2957312, 3080192, true, true) -- 722.0 .. 752.0
end

incubusDeathSceneSteps[29] = function()
    sh.event_chara_anim(0, 0, "ending_scene_j", false)
    sh.event_chara_anim(0, 6, "BloodyIncubator:fight_hover", false)
    sh.player.flex_rotation_reset()
    sh.flag_set("M7S03_FinaleMusicLayer3", true)
    StepNext()
    return incubusDeathSceneSteps[30]()
end

incubusDeathSceneSteps[30] = function()
    TimerAdvance(40960, 3084288, 3452928, true, true) -- 753.0 .. 843.0
end

incubusDeathSceneSteps[31] = function()
    sh.event_chara_anim(0, 0, "ending_scene_k", false)
    StepNext()
    return incubusDeathSceneSteps[32]()
end

incubusDeathSceneSteps[32] = function()
    TimerAdvance(40960, 3457024, 3829760, true, true) -- 844.0 .. 935.0
end

incubusDeathSceneSteps[33] = function()
    if sh.flag_get("M6S04_CybilSaved") then
        EndingCharaLoadsFinish()
        EndingCharaPlace(CharaId("EndingCybil"), 1)
        sh.map.set("update_sibyl", 1)
        sh.npc.anim_flags_clear(1, 2)
        StepNext()
    else
        sh.event.step_set(0, 38)
    end
end

incubusDeathSceneSteps[34] = function()
    sh.event_chara_anim(0, 1, "Cybil:ending_approach", false)
    sh.npc.anim_flags_set(1, 2)
    sh.npc.set_chara_id(6, "None")
    EndingCharaLoad(CharaId("EndingKaufmann"), 5)
    sh.xa_preload("finale_cybil_walk_in_voice")
    sh.map.set("update_bar", 0)
    StepNext()
    return incubusDeathSceneSteps[35]()
end

incubusDeathSceneSteps[35] = function()
    TimerAdvance(40960, 3833856, 3993600, true, true) -- 936.0 .. 975.0
end

incubusDeathSceneSteps[36] = function()
    sh.event_chara_anim(0, 1, "Cybil:ending_walk_in", false)
    sh.sd_call("finale_cybil_walk_in_voice")
    sh.map.lightning_clear()
    StepNext()
    return incubusDeathSceneSteps[37]()
end

incubusDeathSceneSteps[37] = function()
    TimerAdvance(40960, 3997696, 4333568, true, true) -- 976.0 .. 1058.0
    if sh.event_step_get(0) ~= 37 then
        sh.sd_call("stop_xa_audio")
    end
end

incubusDeathSceneSteps[38] = function()
    EndingCharaLoadsFinish()
    EndingCharaPlace(CharaId("EndingKaufmann"), 4)
    if sh.flag_get("M6S04_CybilSaved") then
        sh.load.file("ANIM/ENDAA.DMS", BossDmsBuf)
    else
        sh.load.file("ANIM/ENDBA.DMS", BossDmsBuf)
    end
    StepNext()
end

incubusDeathSceneSteps[39] = function()
    sh.event_chara_anim(0, 4, "Kaufmann:walk_up", false)
    sh.map.set("update_kau", 1)
    StepNext()
    return incubusDeathSceneSteps[40]()
end

incubusDeathSceneSteps[40] = function()
    TimerAdvance(40960, 4337664, 4595712, true, true) -- 1059.0 .. 1122.0
end

incubusDeathSceneSteps[41] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
end

incubusDeathSceneSteps.default = function()
    sh.state_set(SysState.Gameplay)
    sh.npc.set_chara_id(4, "None")
    sh.map.set("update_kau", 0)
    sh.sfx_stop(SfxScene.Thunder)
    -- PC: the lightning and fire stop here like after the other scenes; the original leaves
    -- them drawn through the Good+ ending that follows.
    sh.map.set("boss_fx_time", 0)
    EndingStepNext()
end

-- The Incubus dies (LAST12.DMS, buffer 20): the Bloody Incubator comes out of it, Harry is given
-- the baby; with Cybil saved (M6S04_CybilSaved) she walks in; Kaufmann walks up. ENDAA.DMS (Cybil saved)
-- or ENDBA.DMS is queued into buffer 18 for the Good+/Good ending. M7S03_FinaleMusicLayer2 when the Incubator
-- appears, M7S03_FinaleMusicLayer3 at its hover and its approach.
local function IncubusDeathScene()
    local s = sh.event_step_get(0)
    if s >= 12 then
        EndingRumble()
    end

    runStep(incubusDeathSceneSteps, s)
    if sh.map.get("boss_fx_time") ~= 0 then
        local stage = sh.map.get("boss_fx_stage")
        if stage == 0 then
            if sh.map.get("cutscene_timer") > 2867200 then -- 700.0
                sh.map.set("boss_fx_stage", 1)
            end
        elseif stage == 1 then
            LightningBoltRandom(0x70)
            if sh.event_step_get(0) >= 34 then
                sh.map.lightning_sfx_volume_set(32)
                sh.map.set("boss_fx_stage", sh.map.get("boss_fx_stage") + 1)
            end
        elseif stage == 2 then
            LightningBoltRandom(0xF8)
        end

        local step = sh.event_step_get(0)
        if step >= 30 and step < 40 then
            if step < 33 then
                sh.map.set("ending_flare", 1)
            else
                sh.map.set("ending_flare", 2)
                sh.map.ending_flare_draw(1)
            end
        else
            sh.map.set("ending_flare", 0)
        end

        EndingCuesRun("incubus_death", "incubus_death_cue_step")
    end
end

local incubatorDeathSceneSteps = {}

incubatorDeathSceneSteps[0] = function()
    sh.map.set("ending_dms_idx", 1)
    IncubatorBehaviourScripted()
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_chara_anim(0, BossSlot.Incubator, "Incubator:rise", false)
    sh.map.set("ending_msg_audio_idx", 0)
    sh.map.set("cutscene_timer", 282624) -- 69.0
    sh.sd_call("incubator_rise_voice")
    sh.light.set_intensity(3276) -- 0.8
    sh.map.boss_fx_mode_set(7)
    StepNext()
    return incubatorDeathSceneSteps[1]()
end

incubatorDeathSceneSteps[1] = function()
    TimerAdvance(27852, 282624, 585728, true, false) -- 6.8, 69.0 .. 143.0
    sh.event_chara_anim(1, BossSlot.Incubator, 0, false) -- next step at the end of its animation
end

incubatorDeathSceneSteps[2] = function()
    TimerAdvance(27852, 282624, 585728, true, true)
    if sh.event_step_get(0) ~= 2 then
        sh.sd_call("stop_xa_audio")
    end
end

incubatorDeathSceneSteps[3] = function()
    sh.map.set("cutscene_timer", 589824) -- 144.0
    sh.event_msg_audio("map7_s03.msg82", "ending_msg_audio_idx", "incubator_death_voice_cmds")
end

incubatorDeathSceneSteps[4] = function()
    sh.event_msg_audio("map7_s03.thank_you_daddy", "ending_msg_audio_idx", "incubator_death_voice_cmds")
end

incubatorDeathSceneSteps[5] = function()
    EndingCharaLoad(CharaId("EndingCybil"), 5)
    sh.map.lightning_init(0)
    sh.map.boss_fire_init()
    sh.map.set("boss_fx_stage", 0)
    sh.map.set("boss_fx_time", 1)
    sh.map.boss_fx_mode_set(8)
    StepNext()
    return incubatorDeathSceneSteps[6]()
end

incubatorDeathSceneSteps[6] = function()
    TimerAdvance(49152, 593920, 823296, true, true) -- 12.0, 145.0 .. 201.0
    AuxTimerGrow(BossSlot.Incubator, 1024, 4096) -- + 0.25 per second, up to 1.0
end

incubatorDeathSceneSteps[7] = function()
    sh.event_chara_anim(0, 0, "ending_scene_a", false)
    sh.light.set_intensity(2867) -- 0.7
    sh.npc.set_chara_id(BossSlot.Incubator, "None")
    sh.map.set("update_mar", 0)
    StepNext()
    return incubatorDeathSceneSteps[8]()
end

incubatorDeathSceneSteps[8] = function()
    sh.event_msg_audio("map7_s03.cheryl_2", "ending_msg_audio_idx", "incubator_death_voice_cmds")
    TimerAdvance(26624, 827392, 1232896, true, false) -- 6.5, 202.0 .. 301.0
end

incubatorDeathSceneSteps[9] = function()
    sh.event_msg_audio("map7_s03.cant_be", "ending_msg_audio_idx", "incubator_death_voice_cmds")
    TimerAdvance(26624, 827392, 1232896, true, false)
end

incubatorDeathSceneSteps[10] = function()
    sh.event_msg_audio("map7_s03.cheryl_3", "ending_msg_audio_idx", "incubator_death_voice_cmds")
    TimerAdvance(26624, 827392, 1232896, true, false)
end

incubatorDeathSceneSteps[11] = function()
    if sh.flag_get("M6S04_CybilSaved") then
        sh.load.file("ANIM/ENDC.DMS", Last12DmsBuf)
    end
    StepNext()
    return incubatorDeathSceneSteps[12]()
end

incubatorDeathSceneSteps[12] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
    TimerAdvance(26624, 827392, 1232896, true, false)
end

incubatorDeathSceneSteps.default = function()
    sh.event_step_set(0, 0) -- plain store, no timer reset
    EndingStepNext()
    sh.event_fade_cmd(0, false, 2, 0, false) -- Start, in, type 2
end

-- The Incubator falls (Bad endings; LAST5.DMS in buffer 18): Dahlia's last words, Cybil's
-- model loaded (group 5); with Cybil saved (M6S04_CybilSaved) ENDC.DMS is queued into buffer 20.
local function IncubatorDeathScene()
    local s = sh.event_step_get(0)
    if s >= 6 and s < 10 then
        EndingRumble()
    end

    runStep(incubatorDeathSceneSteps, s)
    if sh.map.get("boss_fx_time") ~= 0 then
        sh.map.lightning_update()
        if sh.map.get("boss_fx_time") ~= 0 then
            EndingCuesRun("incubator_death", "incubator_death_cue_step")
        end
    end
end

local goodPlusEndingSceneSteps = {}

goodPlusEndingSceneSteps[0] = function()
    sh.sound.bgm_crossfade(35)
end

goodPlusEndingSceneSteps[1] = function()
    sh.load.wait()
    sh.cutscene.prepare(BossDmsBuf)
    sh.map.set("ending_dms_idx", 1)
    sh.map.set("cutscene_timer", 0)
    sh.event_chara_anim(0, 0, "ending_watch_hold", false)
    sh.event_chara_anim(0, 1, "Cybil:run", false)
    sh.load.file("ANIM/ENDAB.DMS", Last12DmsBuf)
    EndingCharaLoad(CharaId("BloodyIncubator"), 5)
    sh.map.set("ura_world_shown", 1)
    sh.map.set("under_world_shown", 1)
    sh.npc.anim_flags_clear(6, 2)
    sh.map.lightning_init(0)
    sh.map.boss_fire_init()
    sh.map.set("boss_fx_stage", 0)
    sh.map.set("boss_fx_time", 1)
    sh.map.set("lightning_bolts_held", 0)
    sh.map.set("lightning_hidden", 1)
    SceneSfxPlay(SfxScene.Thunder)
    StepNext()
    return goodPlusEndingSceneSteps[2]()
end

goodPlusEndingSceneSteps[2] = function()
    sh.event_fade_cmd(2, false, 0, 0, false) -- Auto, in
    TimerAdvance(40960, 0, 262144, true, false) -- 10.0, 0.0 .. 64.0
end

goodPlusEndingSceneSteps[3] = function()
    TimerAdvance(40960, 0, 262144, false, true)
end

goodPlusEndingSceneSteps[4] = function()
    SceneSfxPlay(SfxScene.Strike)
    SceneSfxPlay(SfxScene.Crack)
    StepNext()
    return goodPlusEndingSceneSteps[5]()
end

goodPlusEndingSceneSteps[5] = function()
    TimerAdvance(40960, 0, 278528, true, true) -- 0.0 .. 68.0
end

goodPlusEndingSceneSteps[6] = function()
    sh.event_chara_anim(0, 0, "ending_scene_g", false)
    SceneSfxPlay(SfxScene.Strike)
    sh.event_chara_anim(0, 1, "Cybil:ending_stand", false)
    sh.sd_call("finale_cybil_ending_voice")
    sh.map.set("ura_world_shown", 2)
    StepNext()
    return goodPlusEndingSceneSteps[7]()
end

goodPlusEndingSceneSteps[7] = function()
    TimerAdvance(40960, 282624, 364544, true, true) -- 69.0 .. 89.0
end

goodPlusEndingSceneSteps[8] = function()
    sh.event_chara_anim(0, 0, "ending_scene_n", false)
    sh.event_chara_anim(0, 1, "Cybil:ending_kneel", false)
    StepNext()
    return goodPlusEndingSceneSteps[9]()
end

goodPlusEndingSceneSteps[9] = function()
    TimerAdvance(40960, 368640, 401408, true, true) -- 90.0 .. 98.0
end

goodPlusEndingSceneSteps[10] = function()
    sh.load.staff_roll()
    StepNext()
    return goodPlusEndingSceneSteps[11]()
end

goodPlusEndingSceneSteps[11] = function()
    sh.event_fade_cmd(2, true, 1, 2048, false) -- Auto, out, type 1, 0.5 s
end

goodPlusEndingSceneSteps[12] = function()
    sh.map.boss_fire_stop()
    sh.map.lightning_stop()
    sh.map.set("lightning_bolts_held", 1)
    sh.sfx_stop(SfxScene.Thunder)
    SceneSfxPlay(SfxScene.Drone)
    StepNext()
    return goodPlusEndingSceneSteps[13]()
end

goodPlusEndingSceneSteps[13] = function()
    sh.flag_set("M7S03_FinaleMusicLayer4", true)
    if sh.sound.streaming() ~= 0 then
        return
    end
    EndingCharaLoadsFinish()
    EndingCharaPlace(CharaId("BloodyIncubator"), 6)
    sh.npc.anim_flags_clear(6, 2)
    sh.screen.credits_roll_start(sh.map.get("ending_idx"))
    StepNext()
    return goodPlusEndingSceneSteps[14]()
end

goodPlusEndingSceneSteps[14] = function()
    sh.event_chara_anim(0, 0, "ending_scene_h", false)
    sh.event_fade_cmd(0, false, 1, 2048, false) -- Start, in, type 1, 0.5 s
    StepNext()
    return goodPlusEndingSceneSteps[15]()
end

goodPlusEndingSceneSteps[15] = function()
    if sh.screen.credits_roll_music_started() ~= 0 then
        StepNext()
    end
end

goodPlusEndingSceneSteps[16] = function()
    TimerAdvance(40960, 405504, 630784, true, true) -- 99.0 .. 154.0
end

goodPlusEndingSceneSteps[17] = function()
    sh.map.set("ending_dms_idx", 0)
    sh.map.set("cutscene_timer", 0)
    sh.cutscene.prepare(Last12DmsBuf)
    sh.map.set("update_bar", 1)
    sh.map.set("update_sibyl", 0)
    sh.map.set("update_hero", 0)
    sh.npc.anim_flags_set(6, 2)
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_chara_anim(0, 1, "Cybil:stand_talk", false)
    sh.event_chara_anim(0, 6, "BloodyIncubator:fight_hover", false)
    sh.npc.transparent_set(CharaId("BloodyIncubator"), true)
    sh.npc.set_pos_x(0, 573440)  -- 140.0
    sh.npc.set_pos_z(0, -409600) -- -100.0
    sh.map.set("ura_world_shown", 0)
    sh.map.set("under_world_shown", 0)
    sh.map.set("lightning_hidden", 0)
    sh.map.set("boss_fx_stage", 0)
    sh.npc.anim_flags_clear(0, 2)
    sh.map.lightning_init(0)
    StepNext()
    sh.rng.set_seed(0)
    return goodPlusEndingSceneSteps[18]()
end

goodPlusEndingSceneSteps[18] = function()
    TimerAdvance(40960, 0, 245760, true, true) -- 0.0 .. 60.0
end

goodPlusEndingSceneSteps[19] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
end

goodPlusEndingSceneSteps[20] = function()
    sh.map.set("ending_dms_idx", 1)
    sh.map.set("update_bar", 0)
    sh.map.set("update_sibyl", 1)
    sh.map.set("update_hero", 1)
    sh.map.set("cutscene_timer", 634880) -- 155.0
    sh.event_chara_anim(0, 0, "ending_watch_hold", false)
    sh.event_chara_anim(0, 1, "Cybil:run", false)
    sh.map.set("ura_world_shown", 1)
    sh.npc.anim_flags_set(0, 2)
    sh.event_fade_cmd(0, false, 0, 0, false) -- Start, in
    sh.map_effects_update(17, 17, 5, "ending_effect_level", 0, 409600) -- 32-bit values up to 100.0
    sh.map.set("ending_effect_level", 163840) -- 40.0
    sh.map.set("lightning_hidden", 1)
    sh.map.set("boss_fx_stage", 0)
    sh.map.lightning_init(0)
    StepNext()
    return goodPlusEndingSceneSteps[21]()
end

goodPlusEndingSceneSteps[21] = function()
    TimerAdvance(40960, 634880, 917504, true, true) -- 155.0 .. 224.0
    EffectLevelGrow(12288) -- + 3.0 per second
end

goodPlusEndingSceneSteps[22] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
    EffectLevelGrow(12288)
end

goodPlusEndingSceneSteps[23] = function()
    sh.event_chara_anim(0, 6, "BloodyIncubator:fight_hover", false)
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_chara_anim(0, 1, "Cybil:stand_talk", false)
    sh.map.set("ending_effect_level", 409600) -- 100.0
    sh.map_effects_init(9, 9)
    sh.map.set("ending_dms_idx", 0)
    sh.map.set("cutscene_timer", 249856) -- 61.0
    sh.map.set("update_bar", 1)
    sh.npc.set_pos_x(0, 573440)
    sh.npc.set_pos_z(0, -409600)
    sh.map.set("update_sibyl", 0)
    sh.map.set("update_hero", 0)
    sh.map.set("ura_world_shown", 0)
    sh.npc.anim_flags_clear(0, 2)
    sh.event_fade_cmd(0, false, 0, 0, false) -- Start, in
    sh.map.lightning_init(0)
    StepNext()
    return goodPlusEndingSceneSteps[24]()
end

goodPlusEndingSceneSteps[24] = function()
    TimerAdvance(40960, 249856, 315392, false, true) -- 61.0 .. 77.0
end

goodPlusEndingSceneSteps[25] = function()
    sh.event_chara_anim(0, 6, "BloodyIncubator:fall", false)
    StepNext()
    return goodPlusEndingSceneSteps[26]()
end

goodPlusEndingSceneSteps[26] = function()
    TimerAdvance(40960, 249856, 344064, false, true) -- 61.0 .. 84.0
end

goodPlusEndingSceneSteps[27] = function()
    sh.map.set("boss_fx_stage", 0)
    sh.map.boss_fire_resume()
    sh.map.lightning_resume()
    sh.map.set("lightning_hidden", 0)
    sh.map.set("lightning_bolts_held", 0)
    sh.sfx_stop(SfxScene.Drone)
    SceneSfxPlay(SfxScene.Rumble)
    StepNext()
    return goodPlusEndingSceneSteps[28]()
end

goodPlusEndingSceneSteps[28] = function()
    TimerAdvance(40960, 249856, 389120, true, true) -- 61.0 .. 95.0
end

goodPlusEndingSceneSteps[29] = function()
    TimerAdvance(40960, 393216, 495616, true, true) -- 96.0 .. 121.0
end

goodPlusEndingSceneSteps[30] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
end

goodPlusEndingSceneSteps[31] = function()
    sh.map.set("ending_dms_idx", 1)
    sh.map.set("boss_fx_time", 0)
    sh.map.lightning_init(0)
    sh.sfx_stop(SfxScene.Rumble)
    sh.map.set("update_bar", 0)
    sh.map.set("update_sibyl", 1)
    sh.map.set("update_hero", 1)
    sh.map.set("cutscene_timer", 921600) -- 225.0
    sh.event_chara_anim(0, 0, "ending_watch_hold", false)
    sh.event_chara_anim(0, 1, "Cybil:run", false)
    sh.map.set("ura_world_shown", 0)
    sh.map.set("real_world_shown", 1)
    sh.npc.anim_flags_set(0, 2)
    sh.map_effects_init(9, 9)
    sh.map_effects_update(17, 17, 5, "ending_effect_level", 0, 409600)
    sh.event_fade_cmd(0, false, 0, 0, false) -- Start, in
    sh.map.set("ending_effect_level", 327680) -- 80.0
    StepNext()
    return goodPlusEndingSceneSteps[32]()
end

goodPlusEndingSceneSteps[32] = function()
    TimerAdvance(40960, 921600, 1060864, true, true) -- 225.0 .. 259.0
    EffectLevelGrow(40960, 409600) -- + 10.0 per second, up to 100.0
end

goodPlusEndingSceneSteps[33] = function()
    TimerAdvance(40960, 1064960, 1331200, false, true) -- 260.0 .. 325.0
    EffectLevelGrow(40960, 409600)
end

goodPlusEndingSceneSteps[34] = function()
    sh.event_fade_cmd(0, false, 2, 0, false) -- Start, in, type 2
    StepNext()
    return goodPlusEndingSceneSteps[35]()
end

goodPlusEndingSceneSteps[35] = function()
    sh.event_fade_cmd(2, true, 1, 1638, false) -- Auto, out, type 1, 0.4 s
    TimerAdvance(40960, 1064960, 1392640, true, false) -- 260.0 .. 340.0
end

goodPlusEndingSceneSteps.default = function()
    sh.map.set("real_world_shown", 0)
    sh.map.set("boss_fx_time", 0)
    sh.event_step_set(0, 0) -- plain store, no timer reset
    sh.map.set("ending_effect_level", 409600)
    EndingStepNext()
    sh.player_freeze(false, false)
    sh.state_set(SysState.Gameplay)
    sh.npc.set_chara_id(1, "None")
end

-- The Good+ ending (Cybil saved; ENDAA.DMS in buffer 18, then ENDAB.DMS in buffer 20): Harry and
-- Cybil run with the baby while the worlds scroll by, the staff roll's music starts (M7S03_FinaleMusicLayer4),
-- the Incubator falls, and the run ends in the real world.
local function GoodPlusEndingScene()
    local s = sh.event_step_get(0)
    if s == 31 then
        sh.map.weather_set(2, 127)
    end
    if s >= 31 then
        sh.map.particles_update(0, sh.save.map_idx(), s ~= 31 and 1 or 0)
    end
    s = sh.event_step_get(0)
    if s >= 1 and s < 14 then
        EndingRumble()
    end

    s = sh.event_step_get(0)
    runStep(goodPlusEndingSceneSteps, s)
    if sh.map.get("boss_fx_time") ~= 0 then
        if sh.map.get("boss_fx_stage") == 0 then
            sh.map.set("boss_fx_stage", 1)
        end
        if sh.map.get("boss_fx_stage") == 1 and sh.map.get("lightning_bolts_held") == 0 then
            LightningBoltRandom(0x70)
        end
        if sh.map.get("boss_fx_time") ~= 0 then
            EndingCuesRun("good_plus", "good_plus_cue_step")
        end
    end

    local step = sh.event_step_get(0)
    sh.map.set("ending_flare", (step >= 1 and step < 7) and 3 or 0)
end

local goodEndingSceneSteps = {}

goodEndingSceneSteps[0] = function()
    sh.sound.bgm_crossfade(35)
end

goodEndingSceneSteps[1] = function()
    sh.event_chara_anim(0, 0, "ending_watch_hold", false)
    sh.cutscene.prepare(BossDmsBuf)
    sh.load.file("ANIM/ENDBB.DMS", Last12DmsBuf)
    sh.map.set("ending_dms_idx", 1)
    sh.map.set("update_hero", 1)
    sh.map.set("update_bar", 0)
    sh.map.set("ura_world_shown", 1)
    sh.map.set("under_world_shown", 1)
    sh.event_fade_cmd(0, false, 0, 0, false) -- Start, in
    sh.map.lightning_init(0)
    sh.map.boss_fire_init()
    sh.map.set("boss_fx_stage", 0)
    sh.map.set("boss_fx_time", 1)
    sh.map.set("lightning_hidden", 1)
    sh.load.staff_roll()
    sh.npc.transparent_set(CharaId("BloodyIncubator"), true)
    StepNext()
    return goodEndingSceneSteps[2]()
end

goodEndingSceneSteps[2] = function()
    TimerAdvance(40960, 0, 364544, true, true) -- 0.0 .. 89.0
end

goodEndingSceneSteps[3] = function()
    TimerAdvance(40960, 368640, 655360, true, true) -- 90.0 .. 160.0
end

goodEndingSceneSteps[4] = function()
    sh.load.wait()
    sh.cutscene.prepare(Last12DmsBuf)
    sh.map.set("ending_dms_idx", 0)
    sh.map.set("cutscene_timer", 0)
    sh.map.set("update_hero", 0)
    sh.map.set("update_bar", 1)
    sh.map.set("ura_world_shown", 0)
    sh.map.set("under_world_shown", 0)
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_chara_anim(0, 6, "BloodyIncubator:dead_hold", false)
    sh.npc.set_pos_x(0, 573440)  -- 140.0
    sh.npc.set_pos_z(0, -409600) -- -100.0
    sh.npc.anim_flags_clear(0, 2)
    sh.flag_set("M7S03_FinaleMusicLayer4", true)
    sh.screen.credits_roll_start(sh.map.get("ending_idx"))
    sh.map.set("lightning_hidden", 0)
    StepNext()
    return goodEndingSceneSteps[5]()
end

goodEndingSceneSteps[5] = function()
    TimerAdvance(40960, 0, 245760, true, true) -- 0.0 .. 60.0
end

goodEndingSceneSteps[6] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
end

goodEndingSceneSteps[7] = function()
    if sh.screen.credits_roll_music_started() ~= 0 then
        StepNext()
    end
end

goodEndingSceneSteps[8] = function()
    sh.map.set("ending_dms_idx", 1)
    sh.npc.set_chara_id(6, "None")
    sh.map.set("cutscene_timer", 659456) -- 161.0
    sh.event_chara_anim(0, 0, "ending_watch_hold", false)
    sh.map.set("update_hero", 1)
    sh.map.set("update_bar", 0)
    sh.map.set("boss_fx_time", 0)
    sh.map.set("real_world_shown", 1)
    sh.npc.anim_flags_set(0, 2)
    sh.event_fade_cmd(0, false, 0, 0, false) -- Start, in
    sh.map.set("ending_effect_level", 286720) -- 70.0
    sh.map_effects_init(9, 9)
    sh.map_effects_update(17, 17, 5, "ending_effect_level", 0, 409600)
    StepNext()
    return goodEndingSceneSteps[9]()
end

goodEndingSceneSteps[9] = function()
    TimerAdvance(40960, 659456, 1191936, true, true) -- 161.0 .. 291.0
    EffectLevelGrow(40960, 409600)
end

goodEndingSceneSteps[10] = function()
    sh.event_fade_cmd(0, false, 2, 0, false) -- Start, in, type 2
    StepNext()
    return goodEndingSceneSteps[11]()
end

goodEndingSceneSteps[11] = function()
    sh.event_fade_cmd(2, true, 1, 4096, false) -- Auto, out, type 1, 1.0 s
end

goodEndingSceneSteps.default = function()
    EndingStepNext()
    sh.sfx_stop(SfxScene.Thunder)
    sh.sfx_stop(SfxScene.Drone)
    sh.sfx_stop(SfxScene.Rumble)
    sh.map.set("real_world_shown", 0)
    sh.event_step_set(0, 0) -- plain store, no timer reset
    sh.map.set("boss_fx_time", 0)
    sh.player_freeze(false, false)
    sh.state_set(SysState.Gameplay)
end

-- The Good ending (ENDBB.DMS in buffer 20 after the boss's DMS in 18): Harry alone with the
-- baby, the staff roll's music (M7S03_FinaleMusicLayer4), the real world.
local function GoodEndingScene()
    local s = sh.event_step_get(0)
    if s == 8 then
        sh.map.weather_set(2, 127)
    end
    if s >= 8 then
        sh.map.particles_update(0, sh.save.map_idx(), s ~= 8 and 1 or 0)
    end
    s = sh.event_step_get(0)
    if s >= 1 and s < 6 then
        EndingRumble()
    end

    s = sh.event_step_get(0)
    runStep(goodEndingSceneSteps, s)
    if sh.map.get("boss_fx_time") ~= 0 then
        local stage = sh.map.get("boss_fx_stage")
        if stage == 0 or stage == 1 then
            LightningBoltRandom(0x70)
        end
        if sh.map.get("boss_fx_time") ~= 0 then
            EndingCuesRun("good", "good_cue_step")
        end
    end
end

local badPlusCybilSceneSteps = {}

badPlusCybilSceneSteps[0] = function()
    sh.load.wait()
    sh.cutscene.prepare(Last12DmsBuf)
    sh.map.set("ending_dms_idx", 0)
    EndingCharaLoadsFinish()
    EndingCharaPlace(CharaId("EndingCybil"), 1)
    sh.map.set("update_sibyl", 1)
    sh.map.set("ending_msg_audio_idx", 0)
    sh.npc.anim_flags_clear(0, 2)
    sh.event_chara_anim(3, 0, 0, false) -- unlock the animation
    sh.map.set("cutscene_timer", 0)
    sh.map.lightning_init(0)
    sh.map.boss_fire_init()
    sh.map.set("boss_fx_stage", 0)
    sh.map.set("boss_fx_time", 1)
    sh.light.set_intensity(2867) -- 0.7
    StepNext()
end

badPlusCybilSceneSteps[1] = function()
    sh.event_chara_anim(0, 1, "Cybil:ending_approach", false)
    sh.event_fade_cmd(0, false, 0, 0, false) -- Start, in
    StepNext()
    return badPlusCybilSceneSteps[2]()
end

badPlusCybilSceneSteps[2] = function()
    TimerAdvance(40960, 0, 94208, true, true) -- 0.0 .. 23.0
end

badPlusCybilSceneSteps[3] = function()
    sh.map.lightning_clear()
    StepNext()
    return badPlusCybilSceneSteps[4]()
end

badPlusCybilSceneSteps[4] = function()
    TimerAdvance(40960, 98304, 159744, true, true) -- 24.0 .. 39.0
end

badPlusCybilSceneSteps[5] = function()
    sh.event_chara_anim(0, 1, "Cybil:ending_walk_in", false)
    sh.event_chara_anim(0, 0, "ending_scene_d", false)
    StepNext()
    return badPlusCybilSceneSteps[6]()
end

badPlusCybilSceneSteps[6] = function()
    TimerAdvance(40960, 163840, 499712, true, true) -- 40.0 .. 122.0
end

badPlusCybilSceneSteps[7] = function()
    sh.map.set("cutscene_timer", 503808) -- 123.0
    sh.npc.anim_flags_set(0, 2)
    sh.event.wait_timer(4096, false) -- 1.0 s
end

badPlusCybilSceneSteps[8] = function()
    sh.event_chara_anim(0, 0, "ending_scene_c", false)
    sh.event_chara_anim(0, 1, "Cybil:ending_hit", false)
    StepNext()
    return badPlusCybilSceneSteps[9]()
end

badPlusCybilSceneSteps[9] = function()
    TimerAdvance(40960, 507904, 663552, false, true) -- 124.0 .. 162.0
end

badPlusCybilSceneSteps[10] = function()
    sh.event_msg_audio("map7_s03.harry", "ending_msg_audio_idx", "bad_plus_cybil_voice_cmds")
    TimerAdvance(40960, 507904, 1638400, true, false) -- 124.0 .. 400.0
end

badPlusCybilSceneSteps[11] = function()
    TimerAdvance(40960, 507904, 1056768, false, true) -- 124.0 .. 258.0
end

badPlusCybilSceneSteps[12] = function()
    TimerAdvance(40960, 507904, 1638400, true, false)
    if sh.map.get("cutscene_timer") > 1536000 then -- 375.0
        sh.event_fade_cmd(2, true, 0, 1228, false) -- Auto, out, 0.3 s
    end
end

badPlusCybilSceneSteps.default = function()
    EndingStepNext()
    sh.sfx_stop(SfxScene.Thunder)
    sh.sfx_stop(SfxScene.Drone)
    sh.sfx_stop(SfxScene.Rumble)
    sh.map.set("boss_fx_time", 0)
    sh.event_fade_cmd(0, false, 2, 0, false) -- Start, in, type 2
    sh.player_freeze(false, false)
    sh.state_set(SysState.Gameplay)
    sh.npc.set_chara_id(1, "None")
end

-- The Bad+ ending, Cybil's part (ENDC.DMS in buffer 20): she wakes up beside Harry.
local function BadPlusCybilScene()
    local s = sh.event_step_get(0)
    runStep(badPlusCybilSceneSteps, s)
    if sh.map.get("boss_fx_time") ~= 0 then
        local stage = sh.map.get("boss_fx_stage")
        if stage == 0 or stage == 1 then
            LightningBoltRandom(0x70)
        end
        if sh.map.get("boss_fx_time") ~= 0 then
            EndingCuesRun("bad_plus_cybil", "bad_plus_cybil_cue_step")
        end
    end
end

-- The cutscene setup the Incubus and Incubator death scenes share.
local function EndingSceneBegin()
    sh.cutscene.border_show()
    sh.event_step_set(0, 0) -- plain store, no timer reset
    sh.sys_flags_set(sh.sys_flags_get() | SYSFLAG_CUTSCENE_ACTIVE)
    sh.held_item_hide()
    sh.player.weapon_stash()
    sh.world.effects_reset()
end

-- index 3: the Incubus dies (event 8: M7S03_IncubusKilled -> M7S03_CutsceneIncubusDeathDone).
function Cutscene_IncubusDeath()
    local step = sh.map.get("ending_step")
    if step == 0 then
        sh.player_freeze(true)
        EndingSceneBegin()
        sh.map.set("ending_step", step + 1)
        step = 1
    end

    if step == 1 then
        IncubusDeathScene()
    else
        sh.state_set(SysState.Gameplay)
        sh.map.set("ending_step", 0)
        sh.flag_set("M7S03_CutsceneIncubusDeathDone", true)
    end

    EndingObjectsUpdate()
end

-- index 4: the Good+ (Cybil saved, M6S04_CybilSaved) or Good ending scene (event 10, M7S03_IncubusDeathMovieSeen); Harry's
-- health is kept full while it plays. M7S03_GoodPlusEndingSceneDone/M7S03_GoodEndingSceneDone pick the next scene, M7S03_EndingSceneDone the staff roll.
function Cutscene_EndingGood()
    if sh.map.get("ending_step") == 0 then
        if sh.flag_get("M6S04_CybilSaved") then
            GoodPlusEndingScene()
            sh.flag_set("M7S03_GoodPlusEndingSceneDone", true)
            sh.flag_set("M7S03_EndingSceneDone", true)
        else
            GoodEndingScene()
            sh.flag_set("M7S03_GoodEndingSceneDone", true)
            sh.flag_set("M7S03_EndingSceneDone", true)
        end
        sh.npc.set_health(0, FullHealth)
    else
        sh.state_set(SysState.Gameplay)
        sh.map.set("ending_step", 0)
    end

    sh.map.set("credits_vblank_clock", sh.map.get("credits_vblank_clock") + sh.game.vblanks())
    EndingObjectsUpdate()
end

-- index 5: the Incubator falls (event 11: M7S03_IncubatorKilled). With Cybil saved (M6S04_CybilSaved) M7S03_BadPlusCybilSceneStarted starts
-- her scene (index 6); otherwise the bosses' sounds and effects stop and M7S03_BadEndingSceneDone, M7S03_EndingSceneDone lead
-- to the staff roll.
function Cutscene_EndingBad()
    local step = sh.map.get("ending_step")
    if step == 0 then
        sh.player_freeze(true)
        sh.npc.set_health(0, FullHealth)
        EndingSceneBegin()
        sh.map.set("ending_step", step + 1)
        step = 1
    end

    if step == 1 then
        IncubatorDeathScene()
        EndingObjectsUpdate()
        return
    end

    if step == 2 then
        if sh.flag_get("M6S04_CybilSaved") then
            sh.map.set("ending_step", 3)
            sh.flag_set("M7S03_BadPlusCybilSceneStarted", true)
            EndingObjectsUpdate()
            return
        end

        sh.map.set("ending_step", 3)
        sh.flag_set("M7S03_BadEndingSceneDone", true)
        sh.sfx_stop("finale_thunder")
        sh.sfx_stop("finale_drone")
        sh.sfx_stop("finale_rumble")
        sh.map.set("boss_fx_time", 0)
    end

    sh.state_set(SysState.Gameplay)
    sh.map.set("ending_step", 0)
    sh.flag_set("M7S03_EndingSceneDone", true)

    EndingObjectsUpdate()
end

-- index 6: the Bad+ ending, Cybil's part (event 2: M7S03_BadPlusCybilSceneStarted -> M7S03_EndingVariantPartDone). The step is not reset
-- on the way out, like the original.
function Cutscene_EndingBadPlusCybil()
    local step = sh.map.get("ending_step")
    if step == 0 then
        sh.cutscene.border_show()
        sh.event_step_set(0, 0) -- plain store, no timer reset
        sh.map.set("ending_step", 1)
        sh.sys_flags_set(sh.sys_flags_get() | SYSFLAG_CUTSCENE_ACTIVE)
        step = 1
    end

    if step == 1 then
        BadPlusCybilScene()
    else
        sh.state_set(SysState.Gameplay)
        sh.flag_set("M7S03_EndingVariantPartDone", true)
    end

    EndingObjectsUpdate()
end

-- index 7: the ending staff roll. For the Good+ and Good endings (ending_idx 1, 2) the roll
-- already ran in the ending scene and only the exit is left; otherwise STF_ROLL.BIN is loaded,
-- the roll of the ending is set up and waits for its music. Then, once the screen is faded out
-- and the file queue is empty, EndingCreditsDone and back to gameplay.
function Cutscene_EndingStaffRoll()
    local s = sh.event_step_get(0)
    if s == 0 then
        sh.player_freeze(true)
        sh.event_chara_anim(2, 0, 0, false) -- lock the animation

        local ending = sh.map.get("ending_idx")
        if ending >= 1 and ending < 3 then
            sh.event_step_set(0, 3) -- no timer reset, like the original's plain store
            return
        end

        sh.load.staff_roll()
        sh.event_step_set(0, 1) -- next step without resetting the step timer
        s = 1 -- continues into step 1 in the same tick
    end

    if s == 1 then
        if sh.load.queue_length() == 0 then
            sh.screen.credits_roll_start(sh.map.get("ending_idx"))
            sh.event_step_set(0, 2)
        end
    elseif s == 2 then
        if sh.screen.credits_roll_music_started() ~= 0 then
            sh.event_step_set(0, 3)
        end
    elseif s == 3 then
        if sh.screen.credits_roll_finish() ~= 0 then
            sh.flag_set("EndingCreditsDone", true)
            sh.state_set(SysState.Gameplay)
        end
    end
end

-- index 8: after the staff roll: Next Fear, clear count, the ending bits, the status menu.
function Cutscene_EndingClear()
    sh.screen_fade_status_set(ScreenFade.FadeOutComplete) -- keep the screen faded out

    local s = sh.event_step_get(0)
    if s == 0 then
        sh.player_freeze(true)
        sh.event_chara_anim(2, 0, 0, false) -- lock the animation
        sh.event_step_set(0, s + 1) -- next step without resetting the step timer
    elseif s == 1 then
        sh.sound.bgm_crossfade(5) -- music track 5
    elseif s == 2 then
        sh.save.set_next_fear_mode(1)

        local n = (sh.save.clear_game_count() + 1) & 0xFF
        sh.save.set_clear_game_count(n)
        sh.save.set_clear_game_count(math.min(math.max(n, 1), 99))

        local bit = 1 << (sh.map.get("ending_idx") - 1)
        sh.save.set_current_ending_flags(bit)
        sh.save.set_clear_game_endings(sh.save.clear_game_endings() | bit)
        sh.game.set_extra_options_enabled(sh.game.extra_options_enabled() | bit)

        sh.save.set_location_id(24) -- save location: Next Fear
        sh.state_set(SysState.StatusMenu)
    end
end
