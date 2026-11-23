-- map6_s04: Amusement Park - Cybil boss, Alessa kidnapping
-- Event scripts of this area: the engine runs a handler every tick while its event is active.

-- Runs one step of a handler's step table: steps[step], or steps.default for any other step.
local function runStep(steps, step, ...)
    return (steps[step] or steps.default)(...)
end

function on_boot()
    -- per-scenario script entry point
end

-- index 2: no action.
function MapEvent_Nop()
end

local SYSFLAG_CUTSCENE_ACTIVE = 8   -- system flag bit 3
local SYSFLAG_NO_ENEMY_SPAWN = 16   -- system flag bit 4
local ParkDmsBuf = 14               -- PARK.DMS, the boss scenes' DMS
local CybilAnimBufferSize = 230960  -- 0x38630: group 0 widened for MonsterCybil's animations

-- The fight setup both ways into the fight share: no ambient spawns, the map's fight
-- animations for Harry, and MonsterCybil loaded into group 0.
local function CybilFightLoadBegin()
    sh.sys_flags_set(sh.sys_flags_get() | SYSFLAG_NO_ENEMY_SPAWN)
    -- PC: the approach area spawns ambient enemies;
    -- free the NPC slots so none of them can deny the boss's spawn (DEVIATIONS X-72).
    sh.npc.clear_all()
end

-- Harry's fight clips of the map into his map tables (the clips Player_MapAnimsAppend reads): eight clips of
-- the "harry_m6s04_extra" table from clip 16 on, four keyframe ranges of the map's CybilFightAnimRanges from
-- row 8 on.
local function CybilFightAnimsSet()
    sh.player.map_anim_infos_set(16, "harry_m6s04_extra", 8)
    sh.player.map_anim_ranges_set(8, "CybilFightAnimRanges", 4)
end

-- The fight camera around the carousel (world_objects.lua cybil_boss_camera): jumps to its place, its turning
-- speeds from zero; the world update then keeps easing it.
local function CybilBossCameraSnap()
    sh.world.script_call("cybil_boss_camera", 1)
end

-- The fight setup the scenes hand the boss over with: her collision, her health, the first phase. The fight
-- words are the first NPC's whatever the slot, as in the original.
local function MonsterCybilFightSetup(slot)
    sh.entity.set(slot, "cylinder_radius", 1228) -- 0.3 m
    sh.entity.set(slot, "cylinder_extent", 942)  -- 0.23 m, Harry's
    sh.entity.set(slot, "box_top", -6553)        -- -1.6 m, Harry's
    sh.entity.set(slot, "box_offset_y", -4300)   -- -1.05 m
    sh.entity.set(slot, "box_extent_x", -5939)   -- -1.45 m
    sh.entity.set(slot, "health", sh.chara_param_q12("MonsterCybil", "health", 16384000)) -- 4000
    sh.entity.set(slot, "box_bottom", 0)
    sh.entity.set(slot, "box_height", 0)
    sh.entity.set(slot, "cylinder_offset_z", 0)
    sh.entity.set(slot, "cylinder_offset_x", 0)
    sh.entity.set(slot, "box_offset_z", 0)
    sh.entity.set(slot, "box_offset_x", 0)
    sh.entity.set(slot, "state_step", 0)
    sh.entity.set(1, "monster_cybil_shot_hit", 0)
    sh.entity.set_monster_cybil_upper("state_step", 0)
    sh.entity.set(1, "monster_cybil_shots_left", 10)
    sh.entity.set(1, "monster_cybil_ambush_point", 0)
    sh.entity.set(1, "monster_cybil_turn_timer", 0)
    sh.entity.set(1, "npc_freeze", 0)
    sh.entity.set(1, "monster_cybil_on_screen_timer", 0)
    sh.entity.set(1, "monster_cybil_wander_timer", 0)
    sh.entity.set(1, "npc_reset_control_state", 0)
end

-- The sparks of the liquid Harry pours, from his right hand, while the time is past 6 s and before 8 s.
local function ExorcismPourSparks(t)
    if t > 24576 and t < 32768 then
        local x, y, z = sh.math.bone_point("player_bone:10", 10, -17, 30) -- HarryBone_RightHand
        sh.npc.effect_spawn(1, x * 16, y * 16, z * 16, 9, 15)
    end
end

-- The parasite's blood (the map's smoke column, effect slot types 13 and 14): 0 starts it at the parasite's
-- head bone, 1 the parasite is out (its upper bone), 2 follows the bone, 3 stops it.
local bloodUpperBone = false

local function s16(v)
    v = v & 0xFFFF
    return v >= 0x8000 and v - 0x10000 or v
end

local function ExorcismBloodParticles(mode)
    if mode ~= 3 then
        local x, y, z
        if bloodUpperBone then
            x, y, z = sh.math.bone_point("npc_bone:31", 0, -14, 20)
        else
            x, y, z = sh.math.bone_point("npc_bone:1", 0, -41, -21)
        end
        sh.map.set("smoke_emitter.center_x", x * 16)
        sh.map.set("smoke_emitter.center_z", z * 16)
        -- The column's floor follows the bone; the particles already rising (type 14) move with it.
        local rise = (sh.map.get("smoke_emitter.bottom_y") - y * 16) // 2
        sh.effects.slot_y_add(14, rise, 300)
        local bottom = s16((y & 0xFFFF) * 16)
        sh.map.set("smoke_emitter.bottom_y", bottom)
        sh.map.set("smoke_emitter.top_y", s16(bottom - 819)) -- 0.2 m
    end

    if mode == 0 then
        sh.map.set("smoke_emitter.square_area", 1)
        sh.map.set("smoke_emitter.kind", 0)
        sh.map.set("smoke_emitter.count", 28)
        sh.map.set("smoke_emitter.radius", 614)
        sh.map.set("smoke_emitter.opacity", 28)
        sh.map.set("smoke_emitter.rise_speed", -409)
        sh.map.set("smoke_emitter.unread", 81)
        sh.map.set("smoke_emitter.keep_alive", 0x8000)
        sh.map.set("smoke_emitter.fade_rate", -128)
        sh.effects.smoke_column_start()
        bloodUpperBone = false
    elseif mode == 1 then
        sh.map.set("smoke_emitter.radius", 286)
        sh.map.set("smoke_emitter.opacity", 16)
        sh.map.set("smoke_emitter.fade_rate", 0)
        bloodUpperBone = true
    elseif mode == 3 then
        sh.map.set("map_effect_flags", sh.map.get("map_effect_flags") & 0xFD)
        sh.effects.slot_free(13, 300)
        sh.effects.slot_free(14, 300)
    end
end

-- The Flauros glows (the engine's glow field FlaurosGlow; the world update draws the stage
-- flauros_glow_stage). Each add keeps the references it is given and reads them every frame.
local FlaurosGlow = "FlaurosGlow"

local function FlaurosGlowStageNext()
    sh.map.set("flauros_glow_stage", sh.map.get("flauros_glow_stage") + 1)
end

-- The flash at the raised Flauros once the time passes 118 s (sound, pad rumble).
local function FlaurosFlashAdd(timerRef)
    sh.effects.glow_field_add_flash(FlaurosGlow, timerRef)
    FlaurosGlowStageNext()
end

local function FlaurosFlashRemove()
    sh.effects.glow_field_remove(FlaurosGlow, "Flash")
    sh.effects.glow_field_reset(FlaurosGlow)
    FlaurosGlowStageNext()
end

-- The glow of the small Flauros at its pose.
local function FlaurosGlowAdd(posRef, rotRef)
    sh.effects.glow_field_add_point(FlaurosGlow, posRef, rotRef)
end

local function FlaurosGlowRemove()
    sh.effects.glow_field_remove(FlaurosGlow, "Point")
end

-- Six glows on the big Flauros's bones, growing until 350 s.
local function FlaurosBoneGlowsAdd(posRef, bonesRef, timerRef)
    sh.effects.glow_field_add_bones(FlaurosGlow, posRef, bonesRef, timerRef)
end

local function FlaurosBoneGlowsRemove()
    sh.effects.glow_field_remove(FlaurosGlow, "Bones")
    FlaurosGlowStageNext()
end

-- The ray from the Flauros to a point.
local function FlaurosRayAdd(fromRef, toRef)
    sh.effects.glow_field_add_ray(FlaurosGlow, fromRef, toRef)
end

local function FlaurosRayRemove()
    sh.effects.glow_field_remove(FlaurosGlow, "Ray")
    sh.effects.glow_field_reset(FlaurosGlow)
    FlaurosGlowStageNext()
end

-- The glow sequence jumps to its end (the scene is skipped).
local function FlaurosEffectsSkip()
    sh.map.set("flauros_glow_stage", 6)
end

-- index 3: the boss intro (PARK.DMS frames 0..19): Harry walks up to the carousel and the
-- fight starts; skippable from step 1 on. M6S04_CybilBossIntroCutsceneDone at the end.
function Cutscene_CybilBossIntro()
    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step > 0 and step < 3 then
        sh.event.step_set(0, 3)
    end

    step = sh.event_step_get(0)
    if step == 0 or step == 1 then
        if step == 0 then
            sh.player_freeze(true)
            sh.map.set("voice_msg_audio_idx", 0)
            sh.map.set("cutscene_timer", 0)
            CybilFightLoadBegin()
            sh.light.flashlight_on()
            CybilFightAnimsSet()
            sh.player.map_anims_append()
            sh.load.file("ANIM/HBM6_S4B.ANM", 4)
            sh.load.file("ANIM/PARK.DMS", ParkDmsBuf)
            sh.npc.set_anim_buffer_size(0, CybilAnimBufferSize)
            sh.load.wait()
            sh.cutscene.prepare(ParkDmsBuf)
            sh.light.bone_detach()
            sh.light.lens_flare_detach()
            sh.light.set_intensity(4096) -- 1.0
            sh.npc.anim_info_clear()
            sh.npc.load(0, "MonsterCybil", "npc_bone:0", -1, nil, nil)
            sh.cutscene.border_show()
            sh.sys_flags_set(sh.sys_flags_get() | SYSFLAG_CUTSCENE_ACTIVE)
            sh.event_fade_cmd(0, true, 2, 0, false) -- Start
            sh.event_chara_anim(0, 0, "cutscene_stand", false)
            sh.event_step_increment(0)
        end
        sh.event_chara_anim(0, 0, "scene_dialogue_1", false)
        sh.event_step_increment(0)
    elseif step == 2 then
        sh.cutscene.timer_advance("cutscene_timer", 40960, 0, 77824, false, true) -- 10.0, 0.0 .. 19.0
    elseif step == 3 then
        sh.event_fade_cmd(2, true, 0, 0, false) -- Auto
    else
        sh.map.set("cutscene_timer", -1) -- NO_VALUE
        sh.player_freeze(false, false)
        sh.state_set(0) -- gameplay
        sh.camera_auto_return(true)
        sh.npc.process_loads()
        sh.npc.spawn("MonsterCybil", 0, 108544, 444416, 128, 3) -- (26.5, 108.5), 11.25 deg
        CybilBossCameraSnap()
        sh.flag_set("M6S04_CybilBossIntroCutsceneDone", true)
    end

    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, ParkDmsBuf)
        sh.cutscene.camera(t, ParkDmsBuf, "camera_target_pos", "camera_target_look")
        sh.cutscene.light(t, ParkDmsBuf)
    end
end

-- index 4: after the FMV (PARK.DMS frames 21..95): Cybil gets up, raises the gun and shoots;
-- skippable on steps 2..7 (to the end). Both are put at their fight places and the fight
-- starts (M6S04_CybilBossFightStarted, M6S04_CybilFightSetUp). Steps 0/1, 3/4 and 6/7 fall through in one tick.
function Cutscene_CybilBossAims()
    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step >= 2 and step < 8 then
        sh.event.step_reset()
    end

    step = sh.event_step_get(0)
    if step == 0 or step == 1 or step == 2 then
        if step == 0 then
            sh.player_freeze(true)
            sh.light.flashlight_on()
            sh.map.set("voice_msg_audio_idx", 0)
            sh.flag_set("M6S04_CutsceneCybilShootsStarted", true)
            sh.map.set("cutscene_timer", 86016) -- 21.0
            sh.event_step_increment(0)
            sh.event_fade_cmd(2, false, 0, 0, false) -- Auto, in
            sh.light.bone_detach()
            sh.light.lens_flare_detach()
            step = 1
        end
        if step == 1 then
            sh.event_chara_anim(0, 0, "scene_nowhere_b", false)
            sh.event_chara_anim(0, 1, "MonsterCybil:scene_stand", false)
            sh.event_step_increment(0)
        end
        sh.cutscene.timer_advance("cutscene_timer", 40960, 86016, 278528, false, true) -- 10.0, 21.0 .. 68.0
    elseif step == 3 or step == 4 then
        if step == 3 then
            sh.event_chara_anim(0, 1, "MonsterCybil:scene_raise_gun", false)
            sh.npc.anim_flags_clear(0, 2)
            sh.event_step_increment(0)
        end
        sh.cutscene.timer_advance("cutscene_timer", 40960, 282624, 327680, false, true) -- 69.0 .. 80.0
    elseif step == 5 then
        sh.event_wait_timer(8192, false) -- 2.0 s
    elseif step == 6 or step == 7 then
        if step == 6 then
            sh.npc.anim_flags_set(0, 2)
            sh.event_chara_anim(0, 0, "scene_nowhere_a", false)
            sh.event_chara_anim(0, 1, "MonsterCybil:scene_shot", false)
            sh.event_step_increment(0)
        end
        sh.cutscene.timer_advance("cutscene_timer", 40960, 331776, 389120, true, true) -- 81.0 .. 95.0
    elseif step == 8 then
        sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
    else
        sh.map.set("cutscene_timer", -1) -- NO_VALUE

        sh.npc.set_pos_x(0, 102400) -- 25.0
        sh.npc.set_pos_z(0, 409600) -- 100.0
        sh.npc.set_rot_y(0, 3840)   -- 337.5 deg

        sh.npc.set_pos_x(1, 98304)  -- 24.0
        sh.npc.set_pos_z(1, 417792) -- 102.0
        sh.npc.set_rot_y(1, 1792)   -- 157.5 deg

        sh.entity.set(1, "npc_control_state", 0)
        sh.entity.set(1, "monster_cybil_legs_state", 1)
        sh.entity.set(1, "monster_cybil_arms_state", 1)

        sh.camera_auto_return(true)
        CybilBossCameraSnap()
        sh.player_freeze(false, false)

        sh.state_set(0) -- gameplay
        sh.event_fade_cmd(2, false, 0, 0, false) -- Auto, in
        sh.event_fade_cmd(0, false, 2, 0, false) -- Start

        sh.world.water_disable()
        sh.light.flashlight_fix()
        sh.light.set_intensity(4096) -- 1.0

        sh.flag_set("M6S04_CybilBossFightStarted", true)

        sh.npc.anim_flags_set(0, 2)
        MonsterCybilFightSetup(1)
        sh.game.state_update()

        sh.flag_set("M6S04_CybilFightSetUp", true)
    end

    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, ParkDmsBuf)
        sh.cutscene.chara(1, "MSB", t, ParkDmsBuf)
        sh.cutscene.camera(t, ParkDmsBuf, "camera_target_pos", "camera_target_look")
        sh.cutscene.light(t, ParkDmsBuf)
    end
end

-- index 5: the fight resumes without a scene (M6S04_CybilBossFightStarted set, M6S04_CybilFightSetUp not, e.g. after a load):
-- MonsterCybil is loaded and placed, the camera warped, M6S04_CybilFightSetUp.
function MapEvent_CybilBossFightResume()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.screen_fade_reset_timestep()
        CybilFightLoadBegin()

        CybilFightAnimsSet()
        sh.player.map_anims_append()
        sh.load.file("ANIM/HBM6_S4B.ANM", 4)

        sh.npc.set_anim_buffer_size(0, CybilAnimBufferSize)

        sh.npc.anim_info_clear()

        sh.npc.load(0, "MonsterCybil", "npc_bone:0", 255, nil, nil) -- free all character slots
        sh.npc.process_loads()
        sh.npc.spawn("MonsterCybil", 0, 108544, 444416, 128, 3) -- (26.5, 108.5), 11.3 deg

        sh.camera.position(nil, 103505, -9830, 403169, 0, 0, 0, 0, true) -- (25.27, -2.4, 98.43), warp
        sh.camera.look_at(nil, 97402, -4259, 417300, 0, 0, 0, 0, true)  -- (23.78, -1.04, 101.88)

        sh.npc.set_pos_x(0, 102400) -- 25.0
        sh.npc.set_pos_z(0, 409600) -- 100.0
        sh.npc.set_rot_y(0, 3840)   -- 337.5 deg

        sh.npc.set_pos_x(1, 98304)  -- 24.0
        sh.npc.set_pos_z(1, 417792) -- 102.0
        sh.npc.set_rot_y(1, 1792)   -- 157.5 deg

        sh.event_step_increment(0)
    elseif step == 1 then
        -- The original stores the step number itself (1) into both clip states.
        sh.entity.set(1, "npc_control_state", 0)
        sh.entity.set(1, "monster_cybil_legs_state", step)
        sh.entity.set(1, "monster_cybil_arms_state", step)
        MonsterCybilFightSetup(1)

        sh.flag_set("M6S04_CybilFightSetUp", true)

        sh.player_freeze(false, false)
        sh.state_set(0) -- gameplay

        sh.camera_auto_return(true)
        CybilBossCameraSnap()
        sh.event_fade_cmd(0, false, 0, 0, false) -- Start
    end
end

-- index 6: between the fight's phases: MonsterCybil drops into the water by the carousel
-- (a fixed camera, the flashlight from the camera), then the fight goes on (M6S04_CybilBossPhaseChangeCutsceneDone).
-- Step 0 falls through into 1.
function Cutscene_CybilBossPhaseChange()
    local step = sh.event_step_get(0)
    if step == 0 or step == 1 then
        if step == 0 then
            sh.player_freeze(true)

            sh.entity.harry_collision_state_reset(1)
            sh.chara_props_reset(1)

            sh.cutscene.border_show()

            sh.event_fade_cmd(0, true, 2, 0, false) -- Start
            sh.light.flashlight_on()

            sh.npc.set_pos_x(0, 94208)  -- 23.0
            sh.npc.set_pos_z(0, 421888) -- 103.0
            sh.npc.set_rot_y(0, 1536)   -- 135 deg

            sh.npc.set_pos_x(1, 73728)  -- 18.0
            sh.npc.set_pos_z(1, 393216) -- 96.0
            sh.npc.set_rot_y(1, 1536)   -- 135 deg

            local camX, camY, camZ = 73728, -6144, 387072   -- (18.0, -1.5, 94.5)
            local lookX, lookY, lookZ = 73728, -5324, 393216 -- (18.0, -1.3, 96.0)
            sh.vec.set(0, camX, camY, camZ)
            sh.vec.set(1, lookX, lookY, lookZ)
            sh.camera.position("vec:0", 0, 0, 0, 0, 0, 0, 0, true)
            sh.camera.look_at("vec:1", 0, 0, 0, 0, 0, 0, 0, true)
            sh.world.water_enable()

            sh.light.lens_flare_detach()

            sh.light.set_rot_x(-sh.math.atan2(lookY - camY, sh.math.vec2_len_q6(lookX - camX, lookZ - camZ)))
            sh.light.set_rot_y(sh.math.atan2(lookX - camX, lookZ - camZ))
            sh.light.set_rot_z(0)

            sh.light.bone_detach()

            sh.light.set_pos_x(camX)
            sh.light.set_pos_y(math.max(-6144, camY)) -- not above -1.5
            sh.light.set_pos_z(camZ)

            sh.event_chara_anim(0, 1, "MonsterCybil:scene_between_phases", false)

            sh.event_step_increment(0)
        end
        sh.event_chara_anim(2, 1, 0, false) -- AnimLock
        sh.event_wait_timer(4096, false)    -- 1.0 s
    elseif step == 2 then
        sh.event_chara_anim(3, 1, 0, false) -- AnimUnlock
        sh.event_step_increment(0)
    elseif step == 3 then
        sh.event_chara_anim(1, 1, 0, false) -- AwaitAnimEnd
    else
        sh.camera_auto_return(true)
        CybilBossCameraSnap()
        sh.player_freeze(false, false)

        sh.state_set(0) -- gameplay
        sh.event_fade_cmd(0, false, 2, 0, false) -- Start

        sh.world.water_disable()
        sh.light.flashlight_fix()

        sh.light.set_intensity(4096) -- 1.0
        sh.flag_set("M6S04_CybilBossPhaseChangeCutsceneDone", true)
    end
end

-- index 7: Cybil's death cutscene (the DMS in file buffer 14 from frame 96 on): Harry
-- shoots, the message, then flags 447/450/463. Steps 3..7 run in one tick, as the C
-- switch falls through them.
local CybilDmsBuf = 14

function MapEvent_CutsceneCybilDeath()
    if sh.input_action("skip", 1) and sh.event_step_get(0) == 7 then
        sh.event.step_set(0, 8)
    end

    local step = sh.event_step_get(0)
    if step == 0 then
        sh.map.set("cutscene_unused_value", 0)
        sh.map.set("ending_camera_angle", 0)
        sh.light.flashlight_on()
        sh.player.held_item_hide()
        sh.sfx_stop("carousel_turning_loop")
        sh.player_freeze(true)
        sh.npc.set_health(1, -1) -- NO_VALUE
        sh.map.set("voice_msg_audio_idx", 0)
        sh.map.set("cutscene_timer", 393216) -- frame 96.0
        sh.light.bone_detach()
        sh.light.lens_flare_detach()
        sh.light.set_intensity(4096) -- 1.0
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_fade_cmd(2, true, 0, 0, false) -- Auto
    elseif step == 2 then
        sh.event_fade_cmd(2, false, 0, 0, false)
        sh.event_fade_cmd(0, true, 2, 0, false) -- Start
        sh.event_step_increment(0)
    elseif step >= 3 and step <= 7 then
        if step == 3 then
            sh.sd_call("stop_all_sfx")
            sh.sd_call("stop_xa_audio")
            sh.event_chara_anim(0, 0, "ending_scene_a", false)
            sh.event_chara_anim(0, 1, "MonsterCybil:scene_hold_a", false)
            sh.event_step_increment(0)
            step = 4
        end
        if step == 4 then
            sh.cutscene.timer_advance("cutscene_timer", 40960, 393216, 536576, false, false) -- 10.0, 96.0 .. 131.0
            if sh.npc.anim_keyframe(0) > 1282 then
                sh.event_chara_anim(2, 0, 0, false) -- AnimLock
            end
            sh.event_step_increment(0)
            step = 5
        end
        if step == 5 then
            if sh.npc.anim_keyframe(0) > 1282 then
                sh.event_chara_anim(2, 0, 0, false) -- AnimLock
            end
            sh.event_step_increment(0)
            step = 6
        end
        if step == 6 then
            sh.event_chara_anim(3, 0, 0, false) -- AnimUnlock
            sh.event_step_increment(0)
        end
        sh.cutscene.timer_advance("cutscene_timer", 40960, 536576, 798720, false, false) -- 131.0 .. 195.0
        sh.event_msg(false, "map6_s04.what_in_the_hell_how", 0, 0, 0, false)
        if sh.event_step_get(0) ~= 7 then
            sh.flag_set("M6S04_CybilDeathLineShown", true)
        end
    elseif step == 8 then
        sh.event_fade_cmd(2, true, 0, 2048, false) -- 0.5
    else
        sh.map.set("cutscene_timer", -1) -- NO_VALUE
        sh.player_freeze(false, false)
        sh.state_set(0)
        sh.flag_set("M6S04_CybilDeathCutsceneDone", true)
        sh.flag_set("M6S04_CybilFightResolved", true)
        sh.camera_auto_return(true)
        sh.water_effect_disable()
        sh.light.flashlight_fix()
        sh.light.set_intensity(4096)
        sh.sd_call("stop_xa_audio")
        sh.npc.clear(1, 0, 0)
    end

    local t = sh.map.get("cutscene_timer")
    if t > 393216 then
        sh.cutscene.chara(0, "HERO", t, CybilDmsBuf)
        sh.cutscene.chara(1, "MSB", t, CybilDmsBuf)
        sh.cutscene.camera(t, CybilDmsBuf, "camera_target_pos", "camera_target_look")
        sh.cutscene.light(t, CybilDmsBuf)
    end
end

-- index 8: Cybil's exorcism (AMUSE1.DMS in file buffer 14; event 11: M6S04_UnknownLiquidUsedOnCybil -> M6S04_CybilSaved).
-- Harry pours the red liquid over Cybil, the parasite crawls out of her and dies, she wakes
-- up; then talk on the turning carousel under a dark blue tint. Skippable on steps 2..26
-- except the fades 20 and 22. M6S04_CybilParasiteOut when the parasite comes out; M6S04_CybilSaved and M6S04_CybilFightResolved at
-- the end.
local Amuse1DmsBuf = 14
local ExorcismSkipStep = 28

local function ExorcismMsg(key)
    sh.event_msg_audio(key, "voice_msg_audio_idx", "exorcism_voice_cmds")
end

-- The parasite sound at the parasite (character slot 2) with the given call.
local function ParasiteSfxFalloff()
    sh.sfx_falloff_pitch_play("parasite_squirm_loop", sh.npc.pos_x(2), sh.npc.pos_y(2), sh.npc.pos_z(2),
                              32, 65536, 0) -- 16.0 m falloff
end

local cybilExorcismSteps = {}

cybilExorcismSteps[0] = function()
    sh.map.set("cutscene_unused_value", 0)
    sh.map.set("ending_camera_angle", 0)
    sh.sfx_stop("carousel_turning_loop")
    sh.player_freeze(true)
    sh.chara_props_reset(1)
    sh.npc.set_health(1, -1) -- NO_VALUE
    sh.light.flashlight_on()
    sh.map.set("cutscene_timer", 0)
    sh.event_fade_cmd(0, true, 2, 0, false) -- Start
    sh.map.set("voice_msg_audio_idx", 0)
    sh.load.file("ANIM/AMUSE1.DMS", Amuse1DmsBuf)
    sh.load.wait()
    sh.cutscene.prepare(Amuse1DmsBuf)
    sh.light.bone_detach()
    sh.light.lens_flare_detach()
    sh.light.set_intensity(2048) -- 0.5
    sh.npc.load(1, "Parasite", "npc_bone:30", 0, nil, nil)
    sh.cutscene.border_show()
    sh.cutscene_flag(true)
    sh.held_item_hide()
    sh.player.weapon_stash()
    sh.player.held_item_set("CutscenePlasticBottle")
    sh.load.wait()
    sh.event_step_increment(0)
    return cybilExorcismSteps[1]()
end

cybilExorcismSteps[1] = function()
    sh.event_chara_anim(0, 0, "ending_scene_b", false)
    sh.event_chara_anim(0, 1, "MonsterCybil:scene_hold_b", false)
    sh.event_step_increment(0)
    return cybilExorcismSteps[2]()
end

cybilExorcismSteps[2] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 0, 98304, false, true) -- 10.0, 0.0 .. 24.0
    ExorcismPourSparks(sh.map.get("cutscene_timer"))
end

cybilExorcismSteps[3] = function()
    sh.event_step_increment(0)
    return cybilExorcismSteps[4]()
end

cybilExorcismSteps[4] = function()
    sh.event_chara_anim(0, 0, "ending_scene_o", false)
    sh.event_step_increment(0)
    return cybilExorcismSteps[5]()
end

cybilExorcismSteps[5] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 102400, 237568, false, true) -- 25.0 .. 58.0
end

cybilExorcismSteps[6] = function()
    sh.map.set("cutscene_timer", 241664) -- 59.0
    sh.npc.process_loads()
    sh.event_chara_anim(0, 0, "scene_garage_a", false)
    sh.event_chara_anim(0, 1, "MonsterCybil:scene_fall", false)
    sh.flag_set("M6S04_CybilParasiteOut", true)
    ExorcismBloodParticles(0)
    ExorcismMsg("map6_s04.msg39")
    sh.event_step_increment(0)
    return cybilExorcismSteps[7]()
end

cybilExorcismSteps[7] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 241664, 569344, false, true) -- 59.0 .. 139.0
    ExorcismBloodParticles(2)
end

cybilExorcismSteps[8] = function()
    sh.npc.spawn("Parasite", 0, 81920, 409600, 0, 3) -- (20.0, 100.0)
    sh.sfx_flags_play("parasite_emerge", sh.npc.pos_x(2), sh.npc.pos_y(2), sh.npc.pos_z(2), 32, 0)
    sh.sfx_flags_play("parasite_squirm_loop", sh.npc.pos_x(2), sh.npc.pos_y(2), sh.npc.pos_z(2), 32, 0)
    ExorcismBloodParticles(1)
    sh.event_step_increment(0)
    return cybilExorcismSteps[9]()
end

cybilExorcismSteps[9] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 573440, 749568, false, true) -- 140.0 .. 183.0
    ParasiteSfxFalloff()
    ExorcismBloodParticles(2)
end

cybilExorcismSteps[10] = function()
    sh.sd_call("stop_xa_audio")
    sh.map.set("cutscene_timer", 761856) -- 186.0
    sh.event_chara_anim(0, 0, "scene_garage_c", false)
    ExorcismBloodParticles(3)
    sh.event_step_increment(0)
    return cybilExorcismSteps[11]()
end

cybilExorcismSteps[11] = function()
    sh.cutscene.timer_advance("cutscene_timer", 20480, 761856, 790528, false, true) -- 5.0, 186.0 .. 193.0
    ParasiteSfxFalloff()
end

cybilExorcismSteps[12] = function()
    sh.map.set("cutscene_timer", 798720) -- 195.0
    sh.npc.set_anim_status(2, sh.npc.anim_status(2) + 1) -- the parasite's animation status steps on
    sh.event_chara_anim(0, 0, "ending_pose_c", false)
    sh.event_step_increment(0)
    return cybilExorcismSteps[13]()
end

cybilExorcismSteps[13] = function()
    if sh.map.get("cutscene_timer") < 827392 then -- 202.0
        ParasiteSfxFalloff()
    else
        sh.sfx_stop("parasite_squirm_loop")
    end
    sh.cutscene.timer_advance("cutscene_timer", 40960, 798720, 872448, false, true) -- 195.0 .. 213.0
end

cybilExorcismSteps[14] = function()
    sh.map.set("cutscene_timer", 884736) -- 216.0
    sh.npc.clear(2, 0, 0)
    sh.event_chara_anim(0, 0, "scene_garage_b", false)
    sh.event_step_increment(0)
    return cybilExorcismSteps[15]()
end

cybilExorcismSteps[15] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 884736, 995328, false, true) -- 216.0 .. 243.0
    if sh.map.get("cutscene_timer") >= 897024 then -- 219.0
        ExorcismMsg("map6_s04.cybil")
    end
end

cybilExorcismSteps[16] = function()
    sh.event_step_increment(0)
    return cybilExorcismSteps[17]()
end

cybilExorcismSteps[17] = function()
    ExorcismMsg("map6_s04.wake_up_snap_out_of") -- "Wake up! Snap out of it!"
end

cybilExorcismSteps[18] = function()
    ExorcismMsg("map6_s04.cybil_2") -- "Cybil!"
    if sh.event_step_get(0) ~= 18 then
        sh.event_chara_anim(0, 1, "MonsterCybil:scene_lie", false)
    end
end

cybilExorcismSteps[19] = function()
    ExorcismMsg("map6_s04.harry_what_happened") -- "...Harry? What... happened?"
end

cybilExorcismSteps[20] = function()
    sh.event_fade_cmd(2, true, 0, 4096, false) -- Auto, out, 1.0 s
end

cybilExorcismSteps[21] = function()
    sh.map.set("carousel_drift_speed", -122)
    sh.npc.anim_flags_clear(0, 2)
    sh.npc.clear(1, 0, 0)
    sh.map.set("cutscene_timer", -1) -- NO_VALUE
    sh.world.water_enable()
    sh.light.set_intensity(1472) -- 0.3594
    sh.map.set("carousel_chair_hidden", 1)
    sh.event_step_increment(0)
    return cybilExorcismSteps[22]()
end

cybilExorcismSteps[22] = function()
    sh.event_fade_cmd(2, false, 0, 4096, false) -- Auto, in, 1.0 s
end

cybilExorcismSteps[23] = function()
    ExorcismMsg("map6_s04.harry_why_did_they_take")
end

cybilExorcismSteps[24] = function()
    ExorcismMsg("map6_s04.we_found_her_abandoned_on")
end

cybilExorcismSteps[25] = function()
    ExorcismMsg("map6_s04.so_in_that_case")
end

cybilExorcismSteps[26] = function()
    ExorcismMsg("map6_s04.so_what_do_you_do")
end

cybilExorcismSteps[27] = function()
    sh.sd_call("stop_xa_audio")
    sh.event_fade_cmd(2, true, 0, 3276, false) -- Auto, out, 0.8 s
end

cybilExorcismSteps[ExorcismSkipStep] = function()
    sh.sd_call("stop_xa_audio")
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
end

cybilExorcismSteps.default = function()
    ExorcismBloodParticles(3)
    sh.map.set("cutscene_timer", -1) -- NO_VALUE
    sh.player.weapon_restore()
    sh.player.held_item_prev()
    sh.player_freeze(false, false)
    sh.state_set(0) -- gameplay
    sh.map.chara_anim_reset(1, false)
    sh.camera_auto_return(true)
    sh.world.water_disable()
    sh.light.flashlight_fix()
    sh.light.set_intensity(4096) -- 1.0
    sh.flag_set("M6S04_CybilSaved", true)
    sh.flag_set("M6S04_CybilFightResolved", true)
    sh.npc.clear(1, 0, 0)
    sh.npc.clear(2, 0, 0)
    sh.sfx_stop("parasite_emerge")
    sh.sfx_stop("parasite_squirm_loop")
    sh.npc.anim_flags_set(0, 2)
end

function Cutscene_CybilExorcism()
    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step >= 2 and step < 27 and step ~= 20 and step ~= 22 then
        sh.event.step_set(0, ExorcismSkipStep)
    end

    local s = sh.event_step_get(0)
    runStep(cybilExorcismSteps, s)
    step = sh.event_step_get(0)
    if step >= 22 and step <= 27 then
        -- The camera, its target and the light circle the carousel with its angle.
        local a = sh.map.get("ending_camera_angle")
        sh.camera.position(nil, (sh.math.sin(a) * 21299) // 4096 + 81920, -8192,
                           (sh.math.cos(a) * 21299) // 4096 + 409600, 0, 0, 0, 0, true) -- 5.2 m, (20.0, -2.0, 100.0)
        sh.camera.look_at(nil, (sh.math.sin(a - 704) * 15155) // 4096 + 81920, -4096,
                          (sh.math.cos(a - 704) * 15155) // 4096 + 409600, 0, 0, 0, 0, true) -- 3.7 m, 0.172 turn back
        sh.light.set_pos_x((sh.math.sin(a - 896) * 19660) // 4096 + 81920) -- 4.8 m, 0.2188 turn back
        sh.light.set_pos_y(-4096)
        sh.light.set_pos_z((sh.math.cos(a - 896) * 19660) // 4096 + 409600)
        sh.light.set_rot_x(-448)    -- -39.4 deg
        sh.light.set_rot_y(a + 2080) -- + 182.9 deg (a 16-bit angle)
        sh.light.set_rot_z(0)
        -- Dusk tint: a subtractive dark-blue screen fill in the world layer; its page packet
        -- is page 0 with the dither bit, as the original wrote it.
        sh.render.tile{rect = {-160, -112, 320, 224}, colour = {0, 8, 48}, blend = "sub",
                       layer = "world", tpage = 0x40, dither = true}
    end

    local t = sh.map.get("cutscene_timer")
    if t > 0 then
        sh.cutscene.chara(0, "HERO", t, Amuse1DmsBuf)
        sh.cutscene.chara(1, "MSB", t, Amuse1DmsBuf)
        sh.cutscene.chara(2, "ICU", t, Amuse1DmsBuf)
        sh.cutscene.camera(t, Amuse1DmsBuf, "camera_target_pos", "camera_target_look")
        sh.cutscene.light(t, Amuse1DmsBuf)
    end
end

-- index 9: "I figured you'd show" (AMUSE2.DMS, then AMUSE2B.DMS, in file buffer 14; event 13:
-- M6S04_CutsceneAlessaLooksBackDone -> M6S04_CutsceneDahliaTakesAlessaDone). Alessa holds Harry off, Harry raises the Flauros, its light rays and
-- glows grow until it breaks free as the big Flauros (character slot 2) and Alessa falls; then
-- Dahlia comes (slot 2 again) and takes her. Skippable on steps 4..62 (the glow effects end at
-- once). M6S04_FlaurosRaised while the Flauros is up, M6S04_SmallFlaurosVisible while its small model is drawn (the world
-- update draws it), M6S04_FlaurosActivated/465 on the way, M6S04_CutsceneDahliaTakesAlessaDone at the end with the Flauros item taken.
local Amuse2DmsBuf = 14
local IFiguredSkipStep = 65
local DahliaAnimBufferSize = 190000 -- group 0's animation buffer after the scene

-- Overlay bytes only this handler reads: whether DARIA drives slot 2, what the Flauros follows
-- (3: the small one follows the DMS, 1: also drawn (M6S04_SmallFlaurosVisible), 2: the big one in slot 2
-- follows BFLAU, 0: none), and whether the previous frame is held over the new one.
local s_dahliaFollowsDms = false
local s_flaurosMode = 0
local s_ghosting = false

local function IFiguredMsg(key)
    sh.event_msg_audio(key, "voice_msg_audio_idx", "i_figured_voice_cmds")
end

-- A sound of the scene, centred and at full volume wherever the camera is.
local function SceneSfxPlay(name)
    sh.sound.play_with_flags(name, nil, 255, 3) -- 1.0, Mono | NoDistAtten
end

-- Dahlia's portal: the light that gathers into it is the engine heat field "DahliaPortal"
-- (gamedata/effects/heat_fields.json). It starts once the scene time passes 100 s, at a fixed
-- point of the park, with its sound; from then on ever more particles a second (the rate grows
-- by 30 a second each second), a frame's share rounded up by chance (one random number).
local DahliaPortalAt = {8192, -1228, -124928} -- (2.0, -0.3, -30.5) m
local s_portalState = 0
local s_portalRate = 0

local function MulQ12(a, b)
    return (a * b) // 4096
end

local function DahliaPortalUpdate(time)
    local dt = sh.game.delta_time()
    if s_portalState == 0 then
        if time <= 100 * 4096 then
            return
        end
        s_portalState = 1
        sh.effects.heat_field_start("DahliaPortal", DahliaPortalAt[1], DahliaPortalAt[2], DahliaPortalAt[3])
        s_portalRate = 0
        SceneSfxPlay("dahlia_portal_hum")
    elseif s_portalState == 1 then
        s_portalRate = s_portalRate + MulQ12(dt, 30 * 4096)
        local share = MulQ12(s_portalRate, dt)
        local count = share // 4096
        if (sh.rng_int(0, 0x7FFF) // 8) < share % 4096 then
            count = count + 1
        end
        sh.effects.heat_field_spawn("DahliaPortal", 0, 0, 0, count)
    end
    sh.effects.heat_field_update("DahliaPortal")
end

local cutsceneIFiguredYoudShowSteps = {}

cutsceneIFiguredYoudShowSteps[0] = function()
    sh.player_freeze(true)
    sh.map.set("park_scene_sfx_pan_mode", 1)
    sh.map.set("voice_msg_audio_idx", 0)
    sh.load.file("ANIM/AMUSE2.DMS", Amuse2DmsBuf)
    sh.load.wait()
    sh.cutscene.prepare(Amuse2DmsBuf)
    sh.world.object_model_name_set("flauros", "SFLAU_HI")
    sh.map.set("cutscene_timer", 0)
    s_dahliaFollowsDms = false
    s_flaurosMode = 0
    sh.cutscene.border_show()
    sh.screen_fade_reset_timestep()
    sh.sys_flags_set(sh.sys_flags_get() | SYSFLAG_CUTSCENE_ACTIVE)
    sh.world.water_enable()
    sh.light.bone_detach()
    sh.light.lens_flare_detach()
    sh.light.set_intensity(4096) -- 1.0
    sh.player.weapon_stash()
    sh.held_item_hide()
    sh.event_step_increment(0)
end

cutsceneIFiguredYoudShowSteps[1] = function()
    sh.sound.bgm_crossfade(36)
end

cutsceneIFiguredYoudShowSteps[2] = function()
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_chara_anim(0, 1, "Alessa:stand", false)
    sh.light.set_intensity(4096) -- 1.0
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[3]()
end

cutsceneIFiguredYoudShowSteps[3] = function()
    sh.event_fade_cmd(2, false, 0, 0, false) -- Auto, in
end

cutsceneIFiguredYoudShowSteps[4] = function()
    IFiguredMsg("map6_s04.i_figured_youd_show")
end

cutsceneIFiguredYoudShowSteps[5] = function()
    sh.event_chara_anim(0, 1, "Alessa:force_push", false)
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[6]()
end

cutsceneIFiguredYoudShowSteps[6] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 4096, 98304, false, true) -- 10.0, 1.0 .. 24.0
end

cutsceneIFiguredYoudShowSteps[7] = function()
    IFiguredMsg("map6_s04.what")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 4096, 159744, false, true) -- 1.0 .. 39.0
end

cutsceneIFiguredYoudShowSteps[8] = function()
    sh.event_chara_anim(0, 0, "ending_pose_a", false)
    s_ghosting = true
    sh.pad.effect(8)
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[9]()
end

cutsceneIFiguredYoudShowSteps[9] = function()
    IFiguredMsg("map6_s04.what")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 4096, 241664, false, false) -- 1.0 .. 59.0
end

cutsceneIFiguredYoudShowSteps[10] = function()
    sh.npc.load(1, "Flauros", "npc_bone:30", 0, nil, nil)
    s_ghosting = false
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[11]()
end

cutsceneIFiguredYoudShowSteps[11] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 4096, 241664, false, true) -- 1.0 .. 59.0
end

cutsceneIFiguredYoudShowSteps[12] = function()
    sh.event_chara_anim(0, 1, "Alessa:stand_look_right", false)
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[13]()
end

cutsceneIFiguredYoudShowSteps[13] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 4096, 323584, true, true) -- 1.0 .. 79.0
end

cutsceneIFiguredYoudShowSteps[14] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 327680, 425984, true, true) -- 80.0 .. 104.0
    sh.light.set_intensity(8192) -- 2.0
    sh.flag_set("M6S04_FlaurosRaised", true)
end

cutsceneIFiguredYoudShowSteps[15] = function()
    sh.event_chara_anim(0, 0, "ending_pose_b", false)
    FlaurosFlashAdd("cutscene_timer")
    sh.light.set_intensity(3276) -- 0.8
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[16]()
end

cutsceneIFiguredYoudShowSteps[16] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 430080, 466944, false, true) -- 105.0 .. 114.0
end

cutsceneIFiguredYoudShowSteps[17] = function()
    IFiguredMsg("map6_s04.damn")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 430080, 663552, true, false) -- 105.0 .. 162.0
end

cutsceneIFiguredYoudShowSteps[18] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 430080, 663552, true, true) -- 105.0 .. 162.0
end

cutsceneIFiguredYoudShowSteps[19] = function()
    sh.event_chara_anim(0, 0, "ending_pose_d", false)
    FlaurosFlashRemove()
    FlaurosGlowAdd("flauros.pos", "flauros.rot")
    s_flaurosMode = 3
    sh.flag_set("M6S04_FlaurosActivated", true)
    sh.light.set_intensity(2867) -- 0.7
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[20]()
end

cutsceneIFiguredYoudShowSteps[20] = function()
    IFiguredMsg("map6_s04.huh")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 667648, 815104, true, false) -- 163.0 .. 199.0
end

cutsceneIFiguredYoudShowSteps[21] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 667648, 815104, true, true) -- 163.0 .. 199.0
end

cutsceneIFiguredYoudShowSteps[22] = function()
    sh.event_chara_anim(0, 0, "scene_ending_a", false)
    sh.light.set_intensity(2457) -- 0.6
    s_flaurosMode = 1
    SceneSfxPlay("flauros_glow_hum")
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[23]()
end

cutsceneIFiguredYoudShowSteps[23] = function()
    IFiguredMsg("map6_s04.what_is_this")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 819200, 937984, true, false) -- 200.0 .. 229.0
end

cutsceneIFiguredYoudShowSteps[24] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 819200, 937984, true, true) -- 200.0 .. 229.0
end

cutsceneIFiguredYoudShowSteps[25] = function()
    sh.npc.process_loads()
    sh.light.set_intensity(4096) -- 1.0
    SceneSfxPlay("dahlia_portal_hum")
    FlaurosGlowRemove()
    FlaurosBoneGlowsAdd("pos:2", "npc_bone:30", "cutscene_timer")
    sh.npc.spawn("Flauros", 0, sh.npc.pos_x(0) + 4096, sh.npc.pos_z(0), 128, 3) -- 1.0 m to Harry's right, 11.3 deg
    s_flaurosMode = 2
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[26]()
end

cutsceneIFiguredYoudShowSteps[26] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 942080, 1224704, true, true) -- 230.0 .. 299.0
end

cutsceneIFiguredYoudShowSteps[27] = function()
    sh.event_chara_anim(0, 1, "Alessa:walk_forward_turn_stumble", false)
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[28]()
end

cutsceneIFiguredYoudShowSteps[28] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 1228800, 1269760, true, true) -- 300.0 .. 310.0
end

cutsceneIFiguredYoudShowSteps[29] = function()
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.npc.clear(2, 0, 0)
    s_flaurosMode = 1
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[30]()
end

cutsceneIFiguredYoudShowSteps[30] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 1273856, 1462272, false, true) -- 311.0 .. 357.0
end

cutsceneIFiguredYoudShowSteps[31] = function()
    sh.sfx_stop("dahlia_portal_hum")
    sh.sfx_stop("flauros_glow_hum")
    SceneSfxPlay("flauros_break_free")
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[32]()
end

cutsceneIFiguredYoudShowSteps[32] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 1273856, 1495040, false, true) -- 311.0 .. 365.0
end

cutsceneIFiguredYoudShowSteps[33] = function()
    IFiguredMsg("map6_s04.msg51")
    FlaurosBoneGlowsRemove()
    -- The ray ends 1 m above Alessa; the effect keeps this point and reads it every frame.
    sh.map.set("flauros_ray_target.x", sh.npc.pos_x(1))
    sh.map.set("flauros_ray_target.y", sh.npc.pos_y(1) - 4096)
    sh.map.set("flauros_ray_target.z", sh.npc.pos_z(1))
    FlaurosRayAdd("flauros.pos", "flauros_ray_target")
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[34]()
end

cutsceneIFiguredYoudShowSteps[34] = function()
    sh.event_wait_timer(819, false) -- 0.2 s
end

cutsceneIFiguredYoudShowSteps[35] = function()
    SceneSfxPlay("flauros_ray_strike")
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[36]()
end

cutsceneIFiguredYoudShowSteps[36] = function()
    sh.event_chara_anim(1, 1, 0, false) -- AwaitAnimEnd
    sh.cutscene.timer_advance("cutscene_timer", 40960, 1273856, 1581056, true, false) -- 311.0 .. 386.0
end

cutsceneIFiguredYoudShowSteps[37] = function()
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[38]()
end

cutsceneIFiguredYoudShowSteps[38] = function()
    sh.chara_anim_play_to_end(1, "Alessa:stumble_forward_crumple")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 1585152, 1765376, true, false) -- 387.0 .. 431.0
end

cutsceneIFiguredYoudShowSteps[39] = function()
    sh.event_chara_anim(0, 0, "walk_forward", false)
    FlaurosRayRemove()
    s_flaurosMode = 0
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[40]()
end

cutsceneIFiguredYoudShowSteps[40] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 1769472, 1859584, true, true) -- 432.0 .. 454.0
end

cutsceneIFiguredYoudShowSteps[41] = function()
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.light.set_intensity(2457) -- 0.6
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[42]()
end

cutsceneIFiguredYoudShowSteps[42] = function()
    IFiguredMsg("map6_s04.wheres_cheryl")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 1863680, 1863680, true, false) -- 455.0 .. 455.0
    if sh.event_step_get(0) ~= 42 then
        sh.sd_call("stop_xa_audio")
    end
end

cutsceneIFiguredYoudShowSteps[43] = function()
    if sh.sound.streaming() ~= 0 then
        return
    end
    sh.map.set("cutscene_timer", -1) -- NO_VALUE
    sh.npc.image_calc("img:0", sh.chara_id("Dahlia"), 2)
    sh.npc.load(1, "Dahlia", "npc_bone:30", 2, nil, "img:0")
    sh.load.file("ANIM/AMUSE2B.DMS", Amuse2DmsBuf)
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[44]()
end

cutsceneIFiguredYoudShowSteps[44] = function()
    if sh.load.chunks() ~= 0 then
        sh.event_step_increment(0)
    end
end

cutsceneIFiguredYoudShowSteps[45] = function()
    sh.cutscene.prepare(Amuse2DmsBuf)
    sh.npc.process_loads()
    sh.npc.spawn("Dahlia", 0, -163840, -163840, 0, 3) -- (-40.0, -40.0)
    sh.flag_set("M6S04_FlaurosRaised", false)
    sh.flag_set("M6S04_DahliaAppeared", true)
    sh.light.set_intensity(2457) -- 0.6
    sh.event_step_increment(0)
end

cutsceneIFiguredYoudShowSteps[46] = function()
    sh.event_chara_anim(0, 2, "Dahlia:scene_talk_b", false)
    s_dahliaFollowsDms = true
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[47]()
end

cutsceneIFiguredYoudShowSteps[47] = function()
    IFiguredMsg("map6_s04.we_meet_at_last_alessa")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 0, 40960, false, true) -- 0.0 .. 10.0
end

cutsceneIFiguredYoudShowSteps[48] = function()
    sh.event_chara_anim(0, 1, "Alessa:crumple_look_up", false)
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[49]()
end

cutsceneIFiguredYoudShowSteps[49] = function()
    IFiguredMsg("map6_s04.we_meet_at_last_alessa")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 0, 122880, true, false) -- 0.0 .. 30.0
end

cutsceneIFiguredYoudShowSteps[50] = function()
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_chara_anim(0, 1, "Alessa:crumple_idle", false)
    sh.event_chara_anim(0, 2, "Dahlia:confront", false)
    sh.map.set("cutscene_timer", 126976) -- 31.0
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[51]()
end

cutsceneIFiguredYoudShowSteps[51] = function()
    IFiguredMsg("map6_s04.dahlia_gillespie")
end

cutsceneIFiguredYoudShowSteps[52] = function()
    IFiguredMsg("map6_s04.alessa")
end

cutsceneIFiguredYoudShowSteps[53] = function()
    IFiguredMsg("map6_s04.youve_been_a_ghastly_little")
end

cutsceneIFiguredYoudShowSteps[54] = function()
    sh.event_chara_anim(0, 2, "Dahlia:scene_talk_a", false)
    sh.light.set_intensity(2867) -- 0.7
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[55]()
end

cutsceneIFiguredYoudShowSteps[55] = function()
    IFiguredMsg("map6_s04.no_get_away_from_me")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 131072, 327680, false, true) -- 32.0 .. 80.0
end

cutsceneIFiguredYoudShowSteps[56] = function()
    sh.event_chara_anim(0, 1, "Alessa:crumple_shake_head", false)
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[57]()
end

cutsceneIFiguredYoudShowSteps[57] = function()
    IFiguredMsg("map6_s04.no_get_away_from_me")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 131072, 450560, true, false) -- 32.0 .. 110.0
end

cutsceneIFiguredYoudShowSteps[58] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 131072, 450560, true, true) -- 32.0 .. 110.0
end

cutsceneIFiguredYoudShowSteps[59] = function()
    sh.event_chara_anim(0, 0, "scene_twinfeeler_1", false)
    sh.light.set_intensity(4096) -- 1.0
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[60]()
end

cutsceneIFiguredYoudShowSteps[60] = function()
    IFiguredMsg("map6_s04.whats_going_on_here")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 454656, 593920, true, false) -- 111.0 .. 145.0
end

cutsceneIFiguredYoudShowSteps[61] = function()
    sh.event_fade_cmd(0, true, 0, 2048, false)  -- Start, out, 0.5 s
    sh.event_fade_cmd(0, false, 2, 0, false)    -- Start, in, type 2
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[62]()
end

cutsceneIFiguredYoudShowSteps[62] = function()
    IFiguredMsg("map6_s04.msg75")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 454656, 593920, true, false) -- 111.0 .. 145.0
end

cutsceneIFiguredYoudShowSteps[63] = function()
    sh.event_step_increment(0)
    sh.sfx_stop("dahlia_portal_hum")
    return cutsceneIFiguredYoudShowSteps[64]()
end

cutsceneIFiguredYoudShowSteps[64] = function()
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps[IFiguredSkipStep]()
end

cutsceneIFiguredYoudShowSteps[IFiguredSkipStep] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
end

cutsceneIFiguredYoudShowSteps[66] = function()
    if sh.npc.chara_id(2) == sh.chara_id("Flauros") then
        sh.npc.clear(2, 0, 0)
    end
    sh.event_step_increment(0)
    return cutsceneIFiguredYoudShowSteps.default()
end

cutsceneIFiguredYoudShowSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0) -- gameplay
    sh.screen_fade_reset_timestep()
    sh.npc.clear(1, 0, 0)
    sh.npc.clear(2, 0, 0)
    sh.flag_set("M6S04_CutsceneDahliaTakesAlessaDone", true)
    sh.item_remove("Flauros", 1)
    sh.held_item_show()
    sh.player.weapon_restore()
    sh.npc.set_anim_buffer_size(0, DahliaAnimBufferSize)
    sh.sd_call("stop_xa_audio")
    sh.world.water_disable()
    sh.light.flashlight_fix()
    sh.light.set_intensity(4096) -- 1.0
    sh.sys_flags_set(sh.sys_flags_get() & ~SYSFLAG_NO_ENEMY_SPAWN)
end

function MapEvent_CutsceneIFiguredYoudShow()
    local step = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step >= 4 and step < 63 then
        FlaurosEffectsSkip()
        sh.event.step_set(0, IFiguredSkipStep)
    end

    local s = sh.event_step_get(0)
    runStep(cutsceneIFiguredYoudShowSteps, s)
    if s_ghosting then
        sh.render.frame_hold{source = "previous", tint = 128, blend = "half", layer = "overlay"}
    end

    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, Amuse2DmsBuf)
        sh.cutscene.camera(t, Amuse2DmsBuf, "camera_target_pos", "camera_target_look")
        sh.cutscene.light(t, Amuse2DmsBuf)
        sh.cutscene.chara(1, "ARSIA", t, Amuse2DmsBuf)
        if s_dahliaFollowsDms then
            sh.cutscene.chara(2, "DARIA", t, Amuse2DmsBuf)
        end
        if s_flaurosMode == 3 then
            sh.cutscene.transform_to("flauros.pos", "flauros.rot", "SFLAU", t, Amuse2DmsBuf)
        elseif s_flaurosMode == 1 then
            sh.cutscene.transform_to("flauros.pos", "flauros.rot", "SFLAU", t, Amuse2DmsBuf)
            sh.flag_set("M6S04_SmallFlaurosVisible", true)
        else
            sh.flag_set("M6S04_SmallFlaurosVisible", false)
        end
        if s_flaurosMode == 2 then
            sh.cutscene.chara(2, "BFLAU", t, Amuse2DmsBuf)
        end
    end

    if sh.event_step_get(0) >= 48 then
        DahliaPortalUpdate(sh.map.get("cutscene_timer"))
    end
end

-- index 10: Alessa looks back (LOOKAR.DMS in file buffer 14). Harry walks up in slow motion
-- (time at 0.65), Alessa appears beside him, and from step 4 on every frame is drawn over
-- the next at half blend (the ghosting). The park scene sound the map plays fades in to 0.5
-- over the scene, centred (pan mode 2); flag M6S04_CutsceneAlessaLooksBackDone on the way out.
local LookarDmsBuf = 14

local alessaLooksBackSteps = {}

alessaLooksBackSteps[0] = function()
    sh.player_freeze(true)
    sh.flag_set("M6S04_CutsceneAlessaLooksBackStarted", true)
    sh.load.file("ANIM/HBM6_S4C.ANM", 4)
    sh.load.file("ANIM/LOOKAR.DMS", LookarDmsBuf)
    sh.load.wait()
    sh.cutscene.prepare(LookarDmsBuf)
    sh.screen_fade_reset_timestep()
    sh.cutscene.border_show()
    sh.map.set("cutscene_timer", -1) -- NO_VALUE
    sh.cutscene_flag(true)
    sh.held_item_hide()
    sh.player.weapon_stash()
    sh.npc.anim_info_clear()
    sh.npc.load(0, "Alessa", "npc_bone:0", 255, nil, nil) -- free all character slots
    sh.map.set("park_scene_sfx_pan_mode", 2) -- centred
    sh.event_step_increment(0)
end

alessaLooksBackSteps[1] = function()
    sh.event_chara_anim(0, 0, "walk_forward", false)
    sh.event_fade_cmd(0, false, 0, 0, false) -- Start
    sh.event_step_increment(0)
    return alessaLooksBackSteps[2]()
end

alessaLooksBackSteps[2] = function()
    sh.delta_time_set((sh.delta_time_get() * 2662) // 4096) -- dt * 0.65
    sh.cutscene.timer_advance("cutscene_timer", 40960, 0, 90112, true, true) -- 10.0, 0.0 .. 22.0
end

alessaLooksBackSteps[3] = function()
    sh.npc.process_loads()
    sh.npc.spawn("Alessa", 0, sh.npc.pos_x(0) + 4096, sh.npc.pos_z(0), 128, 3) -- 1.0 m to Harry's right, 11.3 deg
    sh.event_chara_anim(0, 0, "scene_bar_fight_1", false)
    sh.light.set_intensity(10240) -- 2.5
    sh.event_step_increment(0)
    return alessaLooksBackSteps[4]()
end

alessaLooksBackSteps[4] = function()
    sh.cutscene.timer_advance("cutscene_timer", 14336, 94208, 135168, true, true) -- 3.5, 23.0 .. 33.0
end

alessaLooksBackSteps[5] = function()
    sh.event_fade_cmd(2, true, 0, 4096, false) -- Auto, out, 1.0 s
end

alessaLooksBackSteps[6] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

alessaLooksBackSteps[7] = function()
    if sh.npc.chara_id(1) ~= sh.chara_id("Alessa") then
        sh.npc.process_loads()
        sh.npc.spawn("Alessa", 0, sh.npc.pos_x(0) + 4096, sh.npc.pos_z(0), 128, 3)
    end
    sh.event_step_increment(0)
    return alessaLooksBackSteps.default()
end

alessaLooksBackSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0) -- gameplay
    sh.flag_set("M6S04_CutsceneAlessaLooksBackDone", true)
    sh.held_item_show()
    sh.player.weapon_restore()
    sh.map.set("park_scene_sfx_volume", 0)
end

function Cutscene_AlessaLooksBack()
    if sh.input_action("skip", 1) and sh.event_step_get(0) >= 2 and sh.event_step_get(0) < 6 then
        sh.event.step_set(0, 6)
    end

    -- An 8-bit volume: the store wraps like the byte it is, then the clamp reads it back.
    sh.map.set("park_scene_sfx_volume", sh.map.get("park_scene_sfx_volume") + (sh.delta_time_get() * 64) // 4096)
    if sh.map.get("park_scene_sfx_volume") >= 128 then
        sh.map.set("park_scene_sfx_volume", 128)
    end

    local s = sh.event_step_get(0)
    runStep(alessaLooksBackSteps, s)
    if sh.event_step_get(0) >= 4 then
        sh.render.frame_hold{source = "previous", tint = 128, blend = "half", layer = "overlay"}
    end

    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.camera(t, LookarDmsBuf, "camera_target_pos", "camera_target_look")
        if sh.event_step_get(0) >= 4 then
            sh.cutscene.chara(1, "ARSIA", t, LookarDmsBuf)
            sh.cutscene.chara(0, "HERO", t, LookarDmsBuf)
            sh.npc.set_rot_y(0, sh.npc.rot_y(0) + 256) -- + 22.5 deg (a 16-bit angle)
        else
            sh.cutscene.chara(0, "HERO", t, LookarDmsBuf)
        end
    end
end
