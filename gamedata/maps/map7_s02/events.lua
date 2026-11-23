-- map7_s02: Nowhere - Alessa vs. Dahlia cutscene
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

-- index 2: unlocked door.
function MapEvent_DoorUnlocked()
    sh.event_msg_with_sfx("common.unlocked", "door_unlocked")
end

-- index 3: common pickups, chosen by the event's point of interest.
function MapEvent_CommonItemTake()
    local poi = sh.event_point()
    local t = {
        [212] = {"RifleShells","M7S01_RifleShells"},
        [213] = {"HandgunBullets","M7S01_HandgunBullets0"},
        [214] = {"HealthDrink","M7S01_HealthDrink0"},
        [215] = {"HandgunBullets","M7S01_HandgunBullets1"},
        [216] = {"ShotgunShells","M7S01_ShotgunShells"},
        [217] = {"FirstAidKit","M7S01_FirstAidKit"},
        [218] = {"HandgunBullets","M7S01_HandgunBullets2"},
        [219] = {"HealthDrink","M7S01_HealthDrink1"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point gives a first aid kit and sets no flag
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- index 4: message with a sound played at the event's point of interest.
function MapEvent_DoorLocked86()
    sh.event_msg_with_sfx("map7_s02.its_locked", "door_locked")
end

-- index 5: message with a sound played at the event's point of interest.
function MapEvent_DoorLocked88()
    sh.event_msg_with_sfx("map7_s02.its_locked_2", "door_locked")
end

-- index 6: Key of Bethor used: the key leaves the inventory, message with the key
-- sound, then a flag is set.
function MapEvent_KeyOfBethorUse()
    sh.player.item_remove("KeyOfBethor", 1)
    sh.event_msg_with_sfx("map7_s02.used_the_key_of_bethor", "use_key")
    sh.flag_set("M7S02_BethorDoorUnlocked", true) -- flag: key of bethor used
end

-- index 7: Key of Aratron used: the key leaves the inventory, message with the key
-- sound, then a flag is set.
function MapEvent_KeyOfAratronUse()
    sh.player.item_remove("KeyOfAratron", 1)
    sh.event_msg_with_sfx("map7_s02.used_the_key_of_aratron", "use_key")
    sh.flag_set("M7S02_AratronDoorUnlocked", true) -- flag: key of aratron used
end

-- index 8: Lisa's videotape: LISAVTR.TIM + voiced message map7_s02.msg32,
-- leaves on skip/cancel.
local function LisaVtrSfx()
    sh.sfx_flags_play("vtr_clunk", 398950, -2048, -241664, 128, 0) -- at (97.4, -0.5, -59.0) m, volume 0.5
end

local lisaVideoTapeSteps = {}

lisaVideoTapeSteps[0] = function()
    sh.player_freeze(true)
    sh.event_bg_texture_cmd(0, "TIM/LISAVTR.TIM", false) -- start loading, LISAVTR.TIM
    sh.map.set("voice_msg_audio_idx", 0)
    sh.event_step_increment(0)
    return lisaVideoTapeSteps[1]()
end

lisaVideoTapeSteps[1] = function()
    sh.event.wait_player_stop()
end

lisaVideoTapeSteps[2] = function()
    sh.event_chara_anim(0, 0, "scene_videotape_1", false)
    sh.event_fade_cmd(0, true, 2, 0, false) -- Start
    sh.npc.set_pos_x(0, 399810) -- 97.61 m
    sh.npc.set_pos_z(0, -244080) -- -59.59 m
    sh.npc.set_rot_y(0, 3651) -- 0.8915 m
    sh.camera.position(nil, 411893, -4341, -252026, 0, 0, 0, 0, true) -- (100.56, -1.06, -61.53)
    sh.camera.look_at(nil, 399810, -2990, -242483, 0, 0, 0, 0, true) -- (97.61, -0.73, -59.2)
    sh.event_step_increment(0)
    return lisaVideoTapeSteps[3]()
end

lisaVideoTapeSteps[3] = function()
    sh.event_wait_timer(10240, false) -- 2.5 s
end

lisaVideoTapeSteps[4] = function()
    LisaVtrSfx()
    sh.event_step_increment(0)
    return lisaVideoTapeSteps[5]()
end

lisaVideoTapeSteps[5] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto
end

lisaVideoTapeSteps[6] = function()
    sh.event_bg_texture_cmd(1, 0, false) -- wait for loading
end

lisaVideoTapeSteps[7] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
    sh.event_bg_texture_cmd(2, 0, false) -- draw
end

lisaVideoTapeSteps[8] = function()
    sh.msg.show_with_audio("map7_s02.msg32", "voice_msg_audio_idx", "lisa_tape_voice_cmds[0]")
    sh.event_bg_texture_cmd(2, 0, false)
    if sh.event_step_get(0) == 8 and (sh.input_action("skip", 1) or sh.input_action("cancel", 1)) then
        sh.sd_call("stop_xa_audio")
        sh.event_step_increment(0)
    end
end

lisaVideoTapeSteps[9] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
    sh.event_bg_texture_cmd(2, 0, false)
end

lisaVideoTapeSteps[10] = function()
    sh.event_chara_anim(0, 0, "scene_videotape_2", false)
    sh.event_step_increment(0)
end

lisaVideoTapeSteps[11] = function()
    sh.event_chara_anim(2, 0, 0, false) -- lock the animation
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_step_increment(0)
    return lisaVideoTapeSteps[12]()
end

lisaVideoTapeSteps[12] = function()
    sh.event_wait_timer(2048, false) -- 0.5 s
end

lisaVideoTapeSteps[13] = function()
    LisaVtrSfx()
    sh.event_step_increment(0)
    return lisaVideoTapeSteps[14]()
end

lisaVideoTapeSteps[14] = function()
    sh.npc.anim_unlock_play_to_end(0)
end

lisaVideoTapeSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.camera_auto_return(false)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.event_inv_item_cmd(3, "VideoTape", 1, false) -- add to the inventory, video tape
end

function MapEvent_LisaVideoTape()
    local s = sh.event_step_get(0)
    runStep(lisaVideoTapeSteps, s)
end

local ghostAlessaCorridorSteps = {}

ghostAlessaCorridorSteps[0] = function()
    sh.player_freeze(true)
    sh.npc.anim_info_clear()
    sh.npc.load(0, "GhostChildAlessa", "npc_bone:0", 255, nil, nil) -- ghost child Alessa, free all character slots
    sh.cutscene.border_show()
    sh.screen_fade_reset_timestep()
    sh.npc.anim_flags_clear(0, 2) -- hide the model
    sh.water_effect_enable()
    sh.camera.position(nil, 82288, -3686, -400506, 0, 0, 0, 0, true)
    sh.camera.look_at(nil, 75776, 819, -414842, 0, 0, 0, 0, true)
    sh.event_step_increment(0)
    return ghostAlessaCorridorSteps[1]()
end

ghostAlessaCorridorSteps[1] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
end

ghostAlessaCorridorSteps[2] = function()
    sh.event_tween_reset(3)
    sh.event_tween_reset(4)
    sh.event_tween_reset(5)
    sh.event_step_increment(0)
    return ghostAlessaCorridorSteps[3]()
end

ghostAlessaCorridorSteps[3] = function()
    sh.event_wait_timer(3276, false) -- 0.8 s
end

ghostAlessaCorridorSteps[4] = function()
    sh.camera.look_at(nil, sh.event_tween_linear(-3236, 12288, 3) + 75776, sh.event_tween_linear(-8724, 12288, 4) + 819, sh.event_tween_linear(1843, 12288, 5) + -414842, 0, 0, 0, 0, false)
    sh.event_wait_timer(12288, false)
end

ghostAlessaCorridorSteps[5] = function()
    sh.event_tween_reset(3)
    sh.event_tween_reset(4)
    sh.event_tween_reset(5)
    sh.npc.process_loads()
    sh.npc.spawn("GhostChildAlessa", 0, 86835, -411648, 2048, 3) -- 21.2, -100.5, 180.0 deg
    sh.event_step_increment(0)
end

ghostAlessaCorridorSteps[6] = function()
    sh.event_chara_anim(0, 1, "appear", false)
    sh.sd_call("ghost_alessa_corridor_voice") -- xa audio 603 sound
    sh.event_step_increment(0)
    return ghostAlessaCorridorSteps[7]()
end

ghostAlessaCorridorSteps[7] = function()
    sh.event_wait_timer(2048, false) -- 0.5 s
end

ghostAlessaCorridorSteps[8] = function()
    sh.camera.look_at(nil, sh.event_tween_linear(15319, 13926, 3) + 72540, sh.event_tween_linear(1598, 13926, 4) + -7905, sh.event_tween_linear(-2704, 13926, 5) + -412999, 0, 0, 0, 0, false)
    sh.event_wait_timer(13926, false) -- 3.4 s
end

ghostAlessaCorridorSteps[9] = function()
    sh.event_tween_reset(3)
    sh.event_tween_reset(4)
    sh.event_tween_reset(5)
    sh.event_step_increment(0)
end

ghostAlessaCorridorSteps[10] = function()
    sh.event_wait_timer(2048, false)
end

ghostAlessaCorridorSteps[11] = function()
    sh.camera.look_at(nil, sh.event_tween_linear(-533, 2457, 3) + 87859, sh.event_tween_linear(4956, 2457, 4) + -6307, sh.event_tween_linear(-204, 2457, 5) + -415703, 0, 0, 0, 0, true)
    sh.event_wait_timer(2457, false) -- 0.6 s
end

ghostAlessaCorridorSteps[12] = function()
    sh.npc.set_aux_timer(1, sh.npc.aux_timer(1) + (1024 * sh.delta_time_get()) // 4096)
    if sh.npc.aux_timer(1) > 4096 then
        sh.npc.clear(1, 0, 0)
        sh.event_step_increment(0)
    end
end

ghostAlessaCorridorSteps[13] = function()
    sh.event_wait_timer(4096, false)
    if sh.event_step_get(0) ~= 13 then
        sh.event_step_reset()
    end
end

ghostAlessaCorridorSteps[14] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

ghostAlessaCorridorSteps[15] = function()
    sh.npc.clear(1, 0, 0)
    sh.sd_call("stop_xa_audio")
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_step_increment(0)
    return ghostAlessaCorridorSteps.default()
end

ghostAlessaCorridorSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.camera_auto_return(true)
    sh.flag_set("M7S02_CutsceneGhostAlessaCorridorDone", true)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.npc.anim_flags_set(0, 2)
    sh.water_effect_disable()
    sh.npc.anim_info_clear()
end

-- index 9: Alessa's ghost in the corridor (camera tweens).
function Cutscene_GhostAlessaCorridor()
    if sh.input_action("skip", 1) and sh.event_step_get(0) >= 1 and sh.event_step_get(0) <= 13 then
        sh.event.step_set(0, 14)
    end

    local s = sh.event_step_get(0)
    runStep(ghostAlessaCorridorSteps, s)
end

-- index 10: item pickup.
function MapEvent_AnkhTake()
    sh.event_item_take("Ankh", 1, "M7S02_PickupAnkh", "map7_s02.there_is_an_ankh_take")
end

-- index 11: jelly bean jar: JELYBEAN.TIM inspection, beans fall.
-- jelly_beans[16] — the beans' world object poses (map state: "jelly_beans[i]",
-- "jelly_beans.pos[i]", "jelly_beans.pos.y" element i, ...).
local function BeanRef(field, i)
    return "jelly_beans" .. field .. "[" .. i .. "]"
end

local jellyBeansJarSteps = {}

jellyBeansJarSteps[0] = function()
    sh.player_freeze(true)
    sh.event_bg_texture_cmd(0, "TIM/JELYBEAN.TIM", false) -- start loading, JELYBEAN.TIM
    sh.event_step_increment(0)
    return jellyBeansJarSteps[1]()
end

jellyBeansJarSteps[1] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

jellyBeansJarSteps[2] = function()
    sh.event_bg_texture_cmd(1, 0, false)
end

jellyBeansJarSteps[3] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.event_fade_cmd(2, false, 0, 0, false)
end

jellyBeansJarSteps[4] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.msg.show(true, "map7_s02.jelly_beans_do_you_want", 7, 5, 0, false)
end

jellyBeansJarSteps[5] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.event_fade_cmd(2, true, 0, 0, false)
end

jellyBeansJarSteps[6] = function()
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.player_freeze(false, false)
    sh.state_set(0)
end

jellyBeansJarSteps[7] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.event_fade_cmd(2, true, 0, 0, false)
end

jellyBeansJarSteps[8] = function()
    sh.flag_set("M7S02_JellyBeanJarEmptied", true)
    sh.cutscene.border_show()
    sh.event_chara_anim(0, 0, "scene_jelly_beans", false)
    sh.npc.set_pos_x(0, 253799)  -- 61.9627 m
    sh.npc.set_pos_z(0, -249838) -- -60.9957 m
    sh.npc.set_rot_y(0, 1024)    -- 90.0 deg
    sh.event_step_increment(0)
end

jellyBeansJarSteps[9] = function()
    sh.event_chara_anim(2, 0, 0, false) -- lock the animation
    sh.camera.position(nil, 244695, -4997, -247234, 0, 0, 0, 0, true)
    sh.camera.look_at(nil, 260792, -4177, -250060, 0, 0, 0, 0, true)
    sh.event_step_increment(0)
    return jellyBeansJarSteps[10]()
end

jellyBeansJarSteps[10] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
end

jellyBeansJarSteps[11] = function()
    sh.event_chara_anim(3, 0, 0, false) -- unlock the animation
    sh.event_step_increment(0)
    return jellyBeansJarSteps[12]()
end

jellyBeansJarSteps[12] = function()
    sh.event_wait_timer(18432, false) -- 4.5 s
end

jellyBeansJarSteps[13] = function()
    sh.map.set("jelly_bean_drop_height", -5300) -- -1.294
    sh.map.set("jelly_bean_fall_speed", 0)
    sh.sfx_flags_play("jellybeans_scatter", 255180, -5939, -250142, 128, 0) -- sound jellybeans_scatter, pos (62.3, -1.45, -61.07)
    sh.event_step_increment(0)
    return jellyBeansJarSteps[14]()
end

jellyBeansJarSteps[14] = function()
    local fall = sh.map.get("jelly_bean_fall_speed") + sh.game.gravity_speed()
    if fall > 20480 then -- 5.0
        fall = 20480
    end
    sh.map.set("jelly_bean_fall_speed", fall)

    local dt = sh.delta_time_get()
    local floorHitCount = 0
    for i = 0, 15 do
        local speed = sh.map.get("jelly_bean_scatter_speeds", i)
        local angle = sh.map.get("jelly_bean_scatter_angles", i)
        sh.map.set("jelly_beans.pos.y", sh.map.get("jelly_beans.pos.y", i)
            + (dt * (fall + (speed * sh.math_sin(angle)) // 4096)) // 4096, i)
        sh.map.set("jelly_beans.pos.z", sh.map.get("jelly_beans.pos.z", i)
            + (dt * ((speed * sh.math_cos(angle)) // 4096)) // 4096, i)

        if sh.map.get("jelly_beans.pos.y", i) > 0 then
            sh.map.set("jelly_beans.pos.y", 0, i)
            floorHitCount = floorHitCount + 1
        end

        sh.world.object_add(BeanRef("", i), BeanRef(".pos", i), BeanRef(".rot", i))
    end

    if sh.map.get("jelly_bean_drop_height") < 0 then
        sh.map.set("jelly_bean_drop_height", sh.map.get("jelly_bean_drop_height") + (dt * fall) // 4096)
        if sh.map.get("jelly_bean_drop_height") > 0 then
            sh.sfx_flags_play("jellybeans_rattle", 255180, 0, -250142, 128, 0) -- sound jellybeans_rattle, pos0 (62.3, 0, -61.07)
        end
    end

    if floorHitCount == 16 then
        sh.event_step_increment(0)
    end
end

jellyBeansJarSteps[15] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

jellyBeansJarSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.camera_auto_return(true)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_fade_cmd(0, false, 2, 0, false)
end

function MapEvent_JellyBeansJar()
    local s = sh.event_step_get(0)
    runStep(jellyBeansJarSteps, s)
end

-- index 12: Key of Bethor pickup with the inspection animation.
function MapEvent_KeyOfBethorTake()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_inv_item_cmd(0, "KeyOfBethor", 0, false) -- start loading, Key of Bethor
        sh.event_step_increment(0)
        step = step + 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
        return
    end
    if step == 2 then
        sh.chara_anim_play_to_end(0, "pickup_crouch")
        return
    end
    if step == 3 then
        sh.event_inv_item_cmd(1, "KeyOfBethor", 0, false) -- wait for loading
        return
    end
    if step == 4 then
        sh.flag_set("M7S02_PickupKeyOfBethor", true) -- flag: pickup key of bethor
        if sh.pickup_animate("KeyOfBethor") then
            sh.event_msg(true, "map7_s02.there_is_a_key_of", 5, 6, 0, false)
        end
        return
    end
    if step == 5 then
        sh.event_inv_item_cmd(3, "KeyOfBethor", 1, false) -- add to the inventory
        sh.event.step_set(0, 7) -- jump to the step
        return
    end
    if step == 6 then
        sh.flag_set("M7S02_PickupKeyOfBethor", false)
        sh.event_step_increment(0)
        step = step + 1 -- continues into step 7 in the same tick
    end
    if step == 7 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")
        return
    end
    sh.player_freeze(false)
    sh.state_set(0) -- back to gameplay
end

-- index 13: no action.
function MapEvent_Empty13()
end

local rafaelChainInspectSteps = {}

rafaelChainInspectSteps[0] = function()
    sh.player_freeze(true)
    sh.event_bg_texture_cmd(0, "TIM/CHAINRF1.TIM", false) -- start loading, CHAINRF1.TIM
    sh.map.set("rafael_chain_rattle_timer", 0)
    sh.event_step_increment(0)
    return rafaelChainInspectSteps[1]()
end

rafaelChainInspectSteps[1] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto
end

rafaelChainInspectSteps[2] = function()
    sh.event_bg_texture_cmd(1, 0, false) -- wait for loading
end

rafaelChainInspectSteps[3] = function()
    sh.event_bg_texture_cmd(3, 0, false) -- save the picture
    sh.event_fade_cmd(0, false, 0, 0, false) -- Start
    sh.event_step_increment(0)
    return rafaelChainInspectSteps[4]()
end

rafaelChainInspectSteps[4] = function()
    sh.event_bg_texture_cmd(8, "TIM/CHAINRF2.TIM", false) -- 8, CHAINRF2.TIM
    sh.event_bg_texture_cmd(2, 0, false) -- draw
end

rafaelChainInspectSteps[5] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.event_fade_cmd(1, false, 0, 0, false) -- Wait
end

rafaelChainInspectSteps[6] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.event_wait_timer(2048, false) -- 0.5 s
end

rafaelChainInspectSteps[7] = function()
    sh.sound.play("rafael_chain_clank", 0, 128) -- sound rafael_chain_clank, 0.5
    sh.event_step_increment(0)
    return rafaelChainInspectSteps[8]()
end

rafaelChainInspectSteps[8] = function()
    sh.bg_img_transition(0, sh.event.state_timer()) -- inspection image -> transition image
    sh.event.set_state_timer(sh.event.state_timer() + 256) -- 0.0625 s
    if sh.event.state_timer() > 4096 then
        sh.event_step_increment(0)
    end
end

rafaelChainInspectSteps[9] = function()
    sh.msg.show(false, "map7_s02.used_the_ring_of_contract", 0, 0, 0, false)
    sh.event_bg_texture_cmd(5, 0, false) -- draw the second picture
end

rafaelChainInspectSteps[10] = function()
    sh.event_bg_texture_cmd(5, 0, false)
    sh.event_fade_cmd(2, true, 0, 0, false)
end

rafaelChainInspectSteps.default = function()
    sh.event_bg_texture_cmd(6, 0, false) -- restore the picture
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.flag_set("M7S02_RingOfContractOnChain", true)
    sh.event_fade_cmd(0, false, 0, 0, false)
end

-- index 14: Rafael's chain: CHAINRF1/2.TIM, cross-fade, chain rattle
-- (rafael_chain_rattle_timer) on steps 4..10.
function MapEvent_RafaelChainInspect()
    local s = sh.event_step_get(0)
    runStep(rafaelChainInspectSteps, s)
    local step = sh.event_step_get(0)
    if step >= 4 and step < 11 then
        local t = sh.map.get("rafael_chain_rattle_timer")
        if t == 0 and not sh.flag_get("M7S02_ChainsBloodsuckerSceneDone") then
            sh.sound.play("chain_rattle", 0, 224) -- sound chain_rattle, 7/8
            sh.map.set("rafael_chain_rattle_timer", sh.rng_int(4915, 11467)) -- 1.2, 2.8 - 1
        else
            sh.map.set("rafael_chain_rattle_timer", math.max(0, t - sh.delta_time_get()))
        end
        return
    end
    sh.sfx_stop("chain_rattle")
end

-- chain rattle timer, kept while the player stays in this area.
local s_chainRattleTimer = 0

local function chain_rattle_tick()
    if s_chainRattleTimer == 0 and not sh.flag_get("M7S02_ChainsBloodsuckerSceneDone") then
        sh.sound.play("chain_rattle", 0, 224) -- volume 224
        s_chainRattleTimer = sh.rng_int(4915, 11467) -- 1.2..2.8 s
    else
        s_chainRattleTimer = math.max(0, s_chainRattleTimer - sh.delta_time_get())
    end
end

-- index 15: rattling chains + Dagger of Melchior (steps 0-14).
function MapEvent_MelchiorChainsDagger()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        if sh.flag_get("M7S02_RingOfContractOnChain") then
            sh.event_bg_texture_cmd(0, "TIM/CHAINRF2.TIM", false)
        else
            sh.event_bg_texture_cmd(0, "TIM/CHAINRF1.TIM", false)
        end
        s_chainRattleTimer = 0
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
    elseif step == 2 then
        sh.event_bg_texture_cmd(1, 0, false) -- wait for loading
    elseif step == 3 then
        sh.event_bg_texture_cmd(2, 0, false) -- draw
        sh.event_fade_cmd(2, false, 0, 0, false)
        chain_rattle_tick()
    elseif step == 4 then
        sh.sound.play("dagger_pick_up", 0, 128) -- volume 128
        sh.event_step_increment(0)
        step = 5
    end
    if step == 5 then
        if sh.flag_get("M7S02_RingOfContractOnChain") then sh.event_msg(false, "map7_s02.the_chain_is_fixed_with", 0, 0, 0, false)
        else sh.event_msg(false, "map7_s02.the_chain_is_missing_a", 0, 0, 0, false) end
        sh.event_bg_texture_cmd(2, 0, false)
        chain_rattle_tick()
    elseif step == 6 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event.branch_on_flag("M7S02_PickupDaggerOfMelchior", 14, 7, false) -- flag: pickup Dagger of Melchior
    elseif step == 7 or step == 8 then
        if step == 7 then
            sh.event_inv_item_cmd(0, "DaggerOfMelchior", 0, false) -- start loading, Dagger of Melchior
            sh.event_step_increment(0) -- continues into step 8 in the same tick
        end
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_fade_cmd(2, true, 0, 0, false)
        chain_rattle_tick()
        if sh.event_step_get(0) ~= 8 then
            sh.sfx_stop("chain_rattle")
        end
    elseif step == 9 then
        sh.event_fade_cmd(2, false, 0, 0, false)
    elseif step == 10 then
        sh.event_inv_item_cmd(1, "DaggerOfMelchior", 0, false) -- wait for loading
    elseif step == 11 then
        if sh.pickup_animate("DaggerOfMelchior") then
            sh.event_msg(true, "map7_s02.there_is_a_dagger_of", 12, 13, 0, false)
        end
        sh.flag_set("M7S02_PickupDaggerOfMelchior", true)
    elseif step == 12 then
        sh.event_inv_item_cmd(3, "DaggerOfMelchior", 1, false) -- add to the inventory
        sh.player_freeze(false)
        sh.state_set(0)
    elseif step == 13 then
        sh.flag_set("M7S02_PickupDaggerOfMelchior", false)
        sh.player_freeze(false)
        sh.state_set(0)
    elseif step == 14 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_fade_cmd(2, true, 0, 0, false)
        chain_rattle_tick()
    elseif step < 0 or step > 14 then -- the switch default
        sh.player_freeze(false)
        sh.state_set(0)
        sh.event_fade_cmd(0, false, 0, 0, false) -- Start
        sh.sfx_stop("chain_rattle")
    end
end

-- index 16: KITCHEN1.DMS cutscene: the bloodsucker in the kitchen kills Harry. The DMS clock
-- follows Harry's scene animation; from step 6 on, the bloodsucker's clip offset follows it
-- too. While step 6 runs, every frame is drawn over the next at half blend (the ghosting);
-- the scene ends in a game over.
local KITCHEN_DMS_BUF = 11

local function KitchenSceneTime()
    return sh.npc.anim_time(0) - sh.player.anim_something() * 4096
end

local kitchenBloodsuckerSteps = {}

kitchenBloodsuckerSteps[0] = function()
    sh.player_freeze(true)
    sh.sfx_flags_play("kitchen_bloodsucker_trap_noise", 86425, -6144, -248832, 128, 0) -- at (21.1, -1.5, -60.75) m, 0.5
    sh.load.file("ANIM/KITCHEN1.DMS", KITCHEN_DMS_BUF)
    sh.flag_set("M7S02_FridgeOpened", true)
    sh.map.set("cutscene_timer", -1) -- not running
    sh.event_step_increment(0)
    return kitchenBloodsuckerSteps[1]()
end

kitchenBloodsuckerSteps[1] = function()
    sh.event.wait_player_stop()
end

kitchenBloodsuckerSteps[2] = function()
    if sh.load.chunks() ~= 0 then
        sh.event_step_increment(0)
    end
end

kitchenBloodsuckerSteps[3] = function()
    sh.cutscene.prepare(KITCHEN_DMS_BUF)
    sh.cutscene.border_show()
    sh.cutscene_flag(true)
    sh.npc.anim_info_clear()
    sh.npc.load(0, "Bloodsucker", "npc_bone:0", 255, nil, nil) -- free all character slots
    sh.event_chara_anim(0, 0, "scene_bloodsucker_chains", false)
    sh.map.set("cutscene_timer", 0)
    sh.event_step_increment(0)
    return kitchenBloodsuckerSteps[4]()
end

kitchenBloodsuckerSteps[4] = function()
    if (sh.npc.anim_status(0) & 1) ~= 0 then
        local t = KitchenSceneTime()
        sh.map.set("cutscene_timer", t)
        if t > 118784 then -- frame 29.0
            sh.map.set("cutscene_timer", 118784)
            sh.event_step_increment(0)
        end
    end
end

kitchenBloodsuckerSteps[5] = function()
    sh.npc.process_loads()
    sh.npc.spawn("Bloodsucker", 64, 88064, -249036, -1024, 17) -- (21.5, -60.8) m, -90.0 deg
    sh.sd_call("bloodsucker_appear")
    sh.flag_set("M7S02_BloodsuckerAppeared", true)
    sh.map.set("kitchen_doors.rot.y", 284, 0)  -- 25.0 deg
    sh.map.set("kitchen_doors.rot.y", 659, 1)  -- 58.0 deg
    sh.map.set("kitchen_doors.rot.y", -568, 2) -- -50.0 deg
    sh.map.set("kitchen_doors.rot.y", -170, 3) -- -15.0 deg
    sh.event_step_increment(0)
    return kitchenBloodsuckerSteps[6]()
end

kitchenBloodsuckerSteps[6] = function()
    local t = KitchenSceneTime()
    sh.map.set("cutscene_timer", t)
    sh.render.frame_hold{source = "previous", tint = 128, blend = "half", layer = "overlay"}
    if t > 159744 then -- frame 39.0
        sh.map.set("cutscene_timer", 159744)
        sh.event_step_increment(0)
    end
end

kitchenBloodsuckerSteps[7] = function()
    local t = KitchenSceneTime()
    sh.map.set("cutscene_timer", t)
    if t > 393216 then -- frame 96.0
        sh.sd_call("harry_death_scream_voice")
        sh.event_step_increment(0)
    end
end

kitchenBloodsuckerSteps[8] = function()
    sh.event_fade_cmd(2, true, 0, 4096, false) -- Auto, out, 1.0 s
    sh.map.set("cutscene_timer", KitchenSceneTime())
end

kitchenBloodsuckerSteps[9] = function()
    sh.event_wait_timer(8192, false) -- 2.0 s
end

kitchenBloodsuckerSteps.default = function()
    sh.event_fade_cmd(0, false, 2, 0, false) -- Start
    sh.state_set(13) -- game over
end

function Cutscene_KitchenBloodsucker()
    local s = sh.event_step_get(0)
    runStep(kitchenBloodsuckerSteps, s)
    if sh.event_step_get(0) >= 6 then
        sh.entity.set(1, "bloodsucker_anim_offset", KitchenSceneTime())
    end

    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, KITCHEN_DMS_BUF)
        sh.cutscene.camera(t, KITCHEN_DMS_BUF, "camera_target_pos", "camera_target_look")
    end
end

local chainsBloodsuckerSceneSteps = {}

chainsBloodsuckerSceneSteps[0] = function()
    sh.player_freeze(true)
    sh.map.set("bloodsucker_chain_rattle_timer", 0)
    sh.flag_set("M7S02_FridgeOpened", true)
    sh.cutscene.border_show()
    sh.event_step_increment(0)
    return chainsBloodsuckerSceneSteps[1]()
end

chainsBloodsuckerSceneSteps[1] = function()
    sh.event.wait_player_stop()
end

chainsBloodsuckerSceneSteps[2] = function()
    sh.event_chara_anim(0, 0, "scene_bloodsucker_chains", false)
    sh.camera.position(nil, 85934, -16711, -245350, 0, 0, 0, 0, true) -- (20.98, -4.08, -59.9)
    sh.camera.look_at(nil, 76021, -4382, -249610, 0, 0, 0, 0, true) -- (18.56, -1.07, -60.94)
    sh.npc.set_pos_x(0, 78643) -- 19.2 m
    sh.npc.set_pos_z(0, -249856) -- -61.0 m
    sh.npc.set_rot_y(0, -1024) -- -90.0 deg
    sh.event_step_increment(0)
    return chainsBloodsuckerSceneSteps[3]()
end

chainsBloodsuckerSceneSteps[3] = function()
    sh.event_wait_timer(10240, false) -- 2.5 s
end

chainsBloodsuckerSceneSteps[4] = function()
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.npc.set_rot_y(0, 1024) -- 90.0 deg
    sh.camera.position(nil, 67051, -10485, -247439, 0, 0, 0, 0, true) -- (16.37, -2.56, -60.41)
    sh.camera.look_at(nil, 82657, -5734, -248872, 0, 0, 0, 0, true) -- (20.18, -1.4, -60.76)
    sh.event_tween_reset(0)
    sh.event_tween_reset(2)
    sh.event_tween_reset(3)
    sh.event_tween_reset(5)
    sh.flag_set("M7S02_BloodsuckerAppeared", true)
    sh.event_step_increment(0)
end

chainsBloodsuckerSceneSteps[5] = function()
    local px = sh.event_tween_linear(7250, 8192, 0) + 67051 -- 1.7701, 2.0; + 16.37
    local pz = sh.event_tween_linear(-655, 8192, 2) - 247439 -- -0.16; - 60.41
    sh.camera.position(nil, px, -10485, pz, 0, 0, 0, 0, false)
    local lx = sh.event_tween_linear(7291, 8192, 0) + 82657 -- 1.7801; + 20.18
    local lz = sh.event_tween_linear(-123, 8192, 2) - 248872 -- -0.0301; - 60.76
    sh.camera.look_at(nil, lx, -5734, lz, 0, 0, 0, 0, false)
    sh.event_wait_timer(8192, false) -- 2.0 s
end

chainsBloodsuckerSceneSteps[6] = function()
    sh.event_wait_timer(8192, false)
end

chainsBloodsuckerSceneSteps[7] = function()
    sh.event_wait_timer(9830, false) -- 2.4 s
end

chainsBloodsuckerSceneSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.flag_set("M7S02_ChainsBloodsuckerSceneDone", true)
    sh.camera_auto_return(false)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.water_effect_disable()
    sh.light.flashlight_fix()
    sh.light.set_intensity(4096) -- 1.0
end

-- index 17: second bloodsucker scene at the chains: sound bloodsucker_scrape
-- rattle (bloodsucker_chain_rattle_timer) on steps 0..6, camera tweens on step 5.
function MapEvent_ChainsBloodsuckerScene()
    local s = sh.event_step_get(0)
    if s >= 0 and s <= 6 then
        local t = sh.map.get("bloodsucker_chain_rattle_timer")
        if t == 0 then
            sh.sfx_flags_play("bloodsucker_scrape", 86425, -6144, -248832, 128, 0) -- sound bloodsucker_scrape at (21.1, -1.5, -60.75), 0.5
            if s == 6 then
                sh.map.set("bloodsucker_chain_rattle_timer", sh.rng_int(409, 817)) -- 0.1, 0.2 - 2
            else
                sh.map.set("bloodsucker_chain_rattle_timer", sh.rng_int(1638, 4913)) -- 0.4..1.2 s
            end
        else
            -- count down to zero; the timer wraps like a 16-bit value
            local dt = sh.delta_time_get()
            local dt16 = ((dt + 0x8000) & 0xFFFF) - 0x8000
            sh.map.set("bloodsucker_chain_rattle_timer", (t - dt) >= 0 and (t - dt16) or 0)
        end
    end

    runStep(chainsBloodsuckerSceneSteps, s)
end

-- index 18: "turn the machine on/off?" (Yes/No, flag M7S02_GeneratorStopped,
-- machine_hum_active = the choice, hum sounds 1495/1503 at machine_hum_sfx_pos, rumble).
local function ApparatusSfxAtPos(sfxId)
    sh.sfx_flags_play(sfxId, 739328, -2048, -248217, 128, 0) -- pos (180.5, -0.5, -60.6), 0.5
end

local apparatusTogglePromptSteps = {}

apparatusTogglePromptSteps[0] = function()
    sh.player_freeze(true)
    sh.event_step_increment(0)
    return apparatusTogglePromptSteps[1]()
end

apparatusTogglePromptSteps[1] = function()
    sh.event.wait_player_stop()
end

apparatusTogglePromptSteps[2] = function()
    sh.event_sys_state_branch_on_flag("M7S02_GeneratorStopped", 3, 7, false)
end

apparatusTogglePromptSteps[3] = function()
    sh.msg.set_silent_yes_selection(1)
    sh.msg.show(true, "map7_s02.the_generator_is_stopped_do", 4, -1, 0, false)
end

apparatusTogglePromptSteps[4] = function()
    ApparatusSfxAtPos("generator_hum")
    sh.sd_call("generator_start")
    sh.flag_set("M7S02_GeneratorStopped", false)
    sh.map.set("machine_hum_active", 1)
    sh.event_step_increment(0)
    return apparatusTogglePromptSteps[5]()
end

apparatusTogglePromptSteps[5] = function()
    sh.event_wait_timer(10240, false) -- 2.5 s
end

apparatusTogglePromptSteps[7] = function()
    sh.msg.set_silent_yes_selection(1)
    sh.msg.show(true, "map7_s02.the_generator_is_working_do", 8, -1, 0, false)
end

apparatusTogglePromptSteps[8] = function()
    ApparatusSfxAtPos("generator_hum")
    sh.flag_set("M7S02_GeneratorStopped", true)
    sh.map.set("machine_hum_active", 0)
    sh.event_step_increment(0)
    return apparatusTogglePromptSteps[9]()
end

apparatusTogglePromptSteps[9] = function()
    local vol = (16384 - sh.event.state_timer()) // 128 -- (4.0 s - state timer) / 128
    sh.sound.play_falloff("generator_start", "machine_hum_sfx_pos", vol, 65536, 0) -- heard within 16 m
    sh.sound.play_falloff("machine_hum_falloff", "machine_hum_sfx_pos", vol, 65536, 0)
    sh.event.set_state_timer(sh.event.state_timer() + sh.delta_time_get())
    if sh.event.state_timer() > 12288 then -- 3.0 s
        sh.event_step_increment(0)
    end
end

apparatusTogglePromptSteps[10] = function()
    sh.sfx_stop("generator_start")
    sh.sfx_stop("machine_hum_falloff")
    sh.sound.play_with_flags("machine_hum", "machine_hum_sfx_pos", 128, 0)
    return apparatusTogglePromptSteps[6]()
end

apparatusTogglePromptSteps[6] = function()
    sh.event_step_reset()
end

apparatusTogglePromptSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
end

function MapEvent_ApparatusTogglePrompt()
    local s = sh.event_step_get(0)
    runStep(apparatusTogglePromptSteps, s)
    if not sh.flag_get("M7S02_GeneratorStopped") then
        local step = sh.event_step_get(0)
        sh.pad_effect(9, (step >= 4 and step <= 5) and 0x90 or 0x60) -- controller rumble preset
    end
end

-- index 19: no action.
function MapEvent_Empty19()
end

-- index 20: HIDDEN1.DMS cutscene: "Everything is going according to plan" (Dahlia, Kaufmann,
-- two ghost doctors). After the line (step 4) the frame is copied into the held picture and
-- the cast leaves; then the held picture fades into the live room over 2 s (added at the fade
-- level, the live frame subtracted at the same level) with the half-blend ghosting on top.
-- Time runs at half speed while the DMS plays. Flag 551 at the end.
local HIDDEN_DMS_BUF = 11
local planFade = 4096 -- the held picture's level, 4096 = full (s16 in the original)

local function PlanMsg(msgKey)
    sh.msg.show_with_audio(msgKey, "voice_msg_audio_idx", "plan_voice_cmds[0]")
end

local function BedCoverAdd()
    sh.vec.short_set(0, 0, 0, 0)
    sh.world.object_add("bed_cover", "bed_cover.pos", "svec:0")
end

local cutsceneEverythingIsGoingAccordingSteps = {}

cutsceneEverythingIsGoingAccordingSteps[0] = function()
    sh.player_freeze(true)
    -- (the original also keeps a copy of Harry's position that nothing reads)
    sh.map.set("voice_msg_audio_idx", 0)
    planFade = 4096
    sh.load.file("ANIM/HIDDEN1.DMS", HIDDEN_DMS_BUF)
    sh.load.wait()
    sh.cutscene.prepare(HIDDEN_DMS_BUF)
    sh.map.set("cutscene_timer", 0)
    sh.npc.anim_info_clear()
    sh.npc.load(0, "GhostDoctor", "ghost_doctor_bones", 255, nil, nil) -- free all character slots
    sh.npc.load(1, "Dahlia", "npc_bone:0", 0, nil, nil)
    sh.npc.image_calc("img:0", 38, 2) -- Kaufmann
    sh.npc.load(2, "Kaufmann", "npc_bone:30", 0, "lm:2", "img:0")
    sh.npc.process_loads()
    sh.npc.spawn("Kaufmann", 0, 573440, -245760, 0, 3) -- (140.0, -60.0) m
    sh.npc.spawn("Dahlia", 0, 573440, -245760, 0, 3)
    sh.npc.spawn("GhostDoctor", 0, 573440, -245760, 0, 3)
    sh.npc.spawn("GhostDoctor", 1, 573440, -245760, 0, 3)
    sh.screen_fade_reset_timestep()
    sh.cutscene.border_show()
    sh.cutscene_flag(true)
    sh.water_effect_enable()
    sh.light.bone_detach()
    sh.light.lens_flare_detach()
    sh.light.set_intensity(2457) -- 0.6
    sh.light.flashlight_on()
    sh.event_step_increment(0)
end

cutsceneEverythingIsGoingAccordingSteps[1] = function()
    sh.event_chara_anim(0, 2, "stand_talk", false)  -- Dahlia
    sh.event_chara_anim(0, 1, "stand_talk", false)  -- Kaufmann
    sh.event_chara_anim(0, 3, "still", false)       -- ghost doctors
    sh.event_chara_anim(0, 4, "still", false)
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.npc.anim_flags_clear(0, 2)
    sh.render.held_clear()
    sh.event_step_increment(0)
    return cutsceneEverythingIsGoingAccordingSteps[2]()
end

cutsceneEverythingIsGoingAccordingSteps[2] = function()
    sh.event_fade_cmd(2, false, 0, 0, false) -- Auto, in
    BedCoverAdd()
end

cutsceneEverythingIsGoingAccordingSteps[3] = function()
    PlanMsg("map7_s02.everything_is_going_according")
    BedCoverAdd()
end

cutsceneEverythingIsGoingAccordingSteps[4] = function()
    BedCoverAdd()
    sh.render.held_capture()
    sh.event_step_increment(0)
end

cutsceneEverythingIsGoingAccordingSteps[5] = function()
    sh.npc.clear(2, 0, 0)
    sh.npc.clear(1, 0, 0)
    sh.npc.clear(3, 0, 0)
    sh.npc.clear(4, 0, 0)
    sh.event_step_increment(0)
    return cutsceneEverythingIsGoingAccordingSteps[6]()
end

cutsceneEverythingIsGoingAccordingSteps[6] = function()
    sh.world.object_add("bed", "bed.pos", "bed.rot")
    local level = planFade >> 5
    sh.render.frame_hold{source = "held", tint = level, blend = "add", layer = "world"}
    sh.render.frame_hold{source = "current", tint = level, blend = "sub", layer = "world"}
    sh.render.frame_hold{source = "previous", tint = 128, blend = "half", layer = "overlay"}
    planFade = planFade - (sh.delta_time_get() * 2048) // 4096 -- 0.5 per second
    planFade = ((planFade + 0x8000) & 0xFFFF) - 0x8000
    if planFade < 0 then
        sh.event_step_increment(0)
        sh.flag_set("M7S02_CutsceneDahliaKaufmannPlanDone", true)
        sh.event_chara_anim(2, 0, 0, false) -- AnimLock
    end
end

cutsceneEverythingIsGoingAccordingSteps[7] = function()
    sh.event_wait_timer(6144, false) -- 1.5 s
end

cutsceneEverythingIsGoingAccordingSteps[8] = function()
    sh.event.step_set(0, -1)
end

cutsceneEverythingIsGoingAccordingSteps[9] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
end

cutsceneEverythingIsGoingAccordingSteps[10] = function()
    sh.npc.clear(2, 0, 0)
    sh.npc.clear(1, 0, 0)
    sh.npc.clear(3, 0, 0)
    sh.npc.clear(4, 0, 0)
    sh.event_fade_cmd(0, false, 0, 0, false) -- Start, in
    sh.flag_set("M7S02_CutsceneDahliaKaufmannPlanDone", true)
    sh.event_step_increment(0)
    return cutsceneEverythingIsGoingAccordingSteps.default()
end

cutsceneEverythingIsGoingAccordingSteps.default = function()
    sh.event_chara_anim(3, 0, 0, false) -- AnimUnlock
    sh.player_freeze(false, false)
    sh.state_set(0) -- back to gameplay
    sh.npc.anim_flags_set(0, 2)
    sh.camera_auto_return(false)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.water_effect_disable()
    sh.light.flashlight_fix()
    sh.light.set_intensity(4096)
    sh.map.set("cutscene_timer", -1)
    sh.npc.anim_info_clear()
end

function MapEvent_CutsceneEverythingIsGoingAccording()
    local s = sh.event_step_get(0)
    if sh.input_action("skip", 1) and s >= 2 and s < 8 then
        sh.sd_call("stop_xa_audio")
        sh.event.step_set(0, 9)
        s = 9
    end

    runStep(cutsceneEverythingIsGoingAccordingSteps, s)
    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.delta_time_set(sh.delta_time_get() // 2)
        sh.cutscene.chara(0, "HERO", t, HIDDEN_DMS_BUF)
        sh.cutscene.chara(2, "DARIA", t, HIDDEN_DMS_BUF)
        sh.cutscene.chara(1, "KAU", t, HIDDEN_DMS_BUF)
        sh.cutscene.chara(3, "DOC1", t, HIDDEN_DMS_BUF)
        sh.cutscene.chara(4, "DOC2", t, HIDDEN_DMS_BUF)
        sh.cutscene.light(t, HIDDEN_DMS_BUF)
        sh.camera.look_at_point_update()
    end
end

-- index 21: item pickup.
function MapEvent_DiskOfOuroborosTake()
    sh.event_item_take("DiskOfOuroboros", 1, "M7S02_PickupDiskOfOuroboros", "map7_s02.there_is_a_disk_of")
end

-- index 22: HALLWAY.DMS cutscene (Alessa/Dahlia, voice via
-- voice_msg_audio_idx/hallway_voice_cmds, cutscene tracks HERO/ALESSA/DAHLIA/LIGHT, skip to step 11).
local function HallwayMsg(msgIdx)
    sh.msg.show_with_audio(msgIdx, "voice_msg_audio_idx", "hallway_voice_cmds[0]")
end

local function HallwayTimerAdvance(startTime, endTime, autoAdvance)
    sh.cutscene.timer_advance("cutscene_timer", 20480, startTime, endTime, true, autoAdvance) -- rate 5.0
end

local hallwayAlessaDahliaSteps = {}

hallwayAlessaDahliaSteps[0] = function()
    sh.player_freeze(true)
    sh.map.set("voice_msg_audio_idx", 0)
    sh.light.flashlight_on()
    sh.cutscene.border_show()
    sh.screen.fade_reset_timestep()
    sh.cutscene_flag(true) -- mark the cutscene active
    sh.held_item_hide()
    sh.player.weapon_stash()
    sh.load.file("ANIM/HALLWAY.DMS", 11) -- HALLWAY.DMS
    sh.load.wait()
    sh.cutscene.prepare(11)
    sh.map.set("cutscene_timer", 0)
    sh.npc.anim_info_clear()
    sh.npc.load(0, "GhostChildAlessa", "npc_bone:0", 255, nil, nil) -- ghost child Alessa, free all character slots
    sh.npc.image_calc("img:0", 30, 2) -- Dahlia
    sh.npc.load(1, "Dahlia", "npc_bone:30", 0, "lm:2", "img:0")
    sh.npc.process_loads()
    sh.npc.spawn("GhostChildAlessa", 0, 409600, -409600, 0, 3) -- 100.0, -100.0
    sh.npc.spawn("Dahlia", 0, 409600, -409600, 0, 3)
    sh.light.bone_detach()
    sh.light.lens_flare_detach()
    sh.light.set_intensity(3276) -- 0.8
    sh.flag_set("M7S02_CutsceneHallwayAlessaDahliaDone", true)
    HallwayMsg("map7_s02.msg62")
    sh.event_step_increment(0)
end

hallwayAlessaDahliaSteps[1] = function()
    sh.event_chara_anim(0, 1, "approach", false)
    sh.event_chara_anim(0, 2, "reveal", false)
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.npc.anim_flags_clear(0, 2) -- hide the model
    HallwayMsg("map7_s02.msg62")
    sh.event_step_increment(0)
end

hallwayAlessaDahliaSteps[2] = function()
    sh.event_fade_cmd(0, false, 0, 2457, false) -- Start, 0.6
    sh.event_step_increment(0)
    return hallwayAlessaDahliaSteps[3]()
end

hallwayAlessaDahliaSteps[3] = function()
    HallwayMsg("map7_s02.msg62")
    HallwayTimerAdvance(0, 131072, false) -- 0.0, 32.0 s
end

hallwayAlessaDahliaSteps[4] = function()
    sh.event_chara_anim(0, 1, "reach_out", false)
    sh.event_chara_anim(0, 2, "chant", false)
    sh.npc.anim_flags_set(0, 2)
    sh.event_step_increment(0)
    return hallwayAlessaDahliaSteps[5]()
end

hallwayAlessaDahliaSteps[5] = function()
    HallwayMsg("map7_s02.i_dont_want_to_do")
    HallwayTimerAdvance(135168, 241664, false) -- 33.0, 59.0 s
end

hallwayAlessaDahliaSteps[6] = function()
    HallwayTimerAdvance(135168, 241664, true)
end

hallwayAlessaDahliaSteps[7] = function()
    sh.event_chara_anim(0, 1, "stand_watch", false)
    sh.event_chara_anim(0, 2, "confront", false)
    sh.map.set("cutscene_timer", 245760) -- frame 60.0
    sh.flag_set("M7S02_CutsceneHallwayAlessaDahliaDone", false)
    sh.event_step_increment(0)
    return hallwayAlessaDahliaSteps[8]()
end

hallwayAlessaDahliaSteps[8] = function()
    HallwayMsg("map7_s02.it_will_make_everyone_happy")
end

hallwayAlessaDahliaSteps[9] = function()
    sh.map.set("cutscene_timer", 249856) -- frame 61.0
    sh.flag_set("M7S02_CutsceneHallwayAlessaDahliaDone", true)
    HallwayMsg("map7_s02.oh_yes_i_see_maybe")
end

hallwayAlessaDahliaSteps[10] = function()
    local inc = (sh.delta_time_get() * 1024) // 4096 -- 0.25 per second
    sh.npc.set_aux_timer(2, sh.npc.aux_timer(2) + inc)
    sh.npc.set_aux_timer(1, sh.npc.aux_timer(1) + inc)
    if sh.npc.aux_timer(1) > 4096 then
        sh.npc.set_aux_timer(1, 4096)
        sh.npc.set_aux_timer(2, 4096)
        sh.event_step_reset()
    end
end

hallwayAlessaDahliaSteps[11] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto
end

hallwayAlessaDahliaSteps[12] = function()
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_step_increment(0)
    return hallwayAlessaDahliaSteps.default()
end

hallwayAlessaDahliaSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.camera_auto_return(true)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.npc.clear(1, 0, 0)
    sh.npc.clear(2, 0, 0)
    sh.npc.anim_info_clear()
    sh.water_effect_disable()
    sh.light.flashlight_fix()
    sh.light.set_intensity(4096)
    sh.map.set("cutscene_timer", -1) -- not running
    sh.sd_call("stop_xa_audio")
    sh.flag_set("M7S02_CutsceneHallwayAlessaDahliaDone", true)
    sh.held_item_show()
    sh.player.weapon_restore()
    sh.npc.anim_flags_set(0, 2)
end

function Cutscene_HallwayAlessaDahlia()
    local s = sh.event_step_get(0)
    if sh.input_action("skip", 1) and s >= 3 and s < 11 then
        sh.event.step_set(0, 11)
        s = 11
    end

    runStep(hallwayAlessaDahliaSteps, s)
    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, 11)
        sh.cutscene.chara(1, "ALESSA", t, 11)
        sh.cutscene.chara(2, "DAHLIA", t, 11)
        sh.cutscene.light(t, 11)
        sh.cutscene.camera(t, 11, "camera_target_pos", "camera_target_look")
        sh.npc.set_rot_y(0, sh.npc.rot_y(0) + 192) -- += 16.88 deg
    end
end

-- index 23: message over a background picture.
function MapEvent_AbusedDeskInspect()
    sh.event_msg_with_bg("TIM/ABUSEDSK.TIM", 0, 0, "map7_s02.go_home")
end

-- index 24: item pickup.
function MapEvent_ScrewdriverTake()
    sh.event_item_take("Screwdriver", 1, "M7S01_PickupScrewdriver", "map7_s02.there_is_a_screwdriver_take")
end

-- index 25: lithograph plate: message map7_s02.msg101 over LITHGRPH.TIM, picture brightness 13/32.
function MapEvent_LithographRead()
    sh.background_gamma(104) -- 13/32
    sh.event_msg_with_bg("TIM/LITHGRPH.TIM", 0, 0, "map7_s02.msg101")
end

-- index 26: second lithograph plate: message map7_s02.names_engraved_on_a_lithograph over LITHGR_3.TIM, brightness 13/32.
function MapEvent_Lithograph3Read()
    sh.background_gamma(104)
    sh.event_msg_with_bg("TIM/LITHGR_3.TIM", 0, 0, "map7_s02.names_engraved_on_a_lithograph")
end

-- index 27: item pickup.
function MapEvent_AmuletOfSolomonTake()
    sh.event_item_take("AmuletOfSolomon", 1, "M7S01_PickupAmuletOfSolomon", "map7_s02.there_is_an_amulet_of")
end

-- index 28: NURSE1.DMS cutscene: Lisa in the ward.
local function NurseDmsTail()
    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, 11)
        sh.cutscene.chara(1, "LISA", t, 11)
        sh.cutscene.camera(t, 11, "camera_target_pos", "camera_target_look")
    end
end

local function LisaBloodyLoad()
    sh.npc.image_calc("img:0", 33, 2) -- Bloody Lisa
    sh.npc.load(1, "BloodyLisa", "npc_bone:30", 0, "lm:2", "img:0")
end

local lisaNurseRoomSteps = {}

lisaNurseRoomSteps[0] = function()
    sh.player_freeze(true)
    sh.map.set("voice_msg_audio_idx", 0)
    sh.load.file("ANIM/HBM7_SLS.ANM", 4)   -- HBM7_SLS.ANM
    sh.load.file("ANIM/NURSE1.DMS", 11) -- NURSE1.DMS
    sh.load.wait()
    sh.cutscene.prepare(11)
    sh.map.set("cutscene_timer", 0)
    sh.npc.anim_info_clear()
    sh.npc.load(0, "Lisa", "npc_bone:0", 255, nil, nil) -- Lisa, free all character slots
    sh.screen_fade_reset_timestep()
    sh.cutscene.border_show()
    sh.light.set_intensity(3276) -- 0.8
    sh.cutscene_flag(true)
    sh.light.flashlight_on()
    sh.map.set("nurse_scene_flag", 0)
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.player.weapon_stash()
    sh.held_item_hide()
    sh.sound.bgm_change(4) -- music track 4
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[1]()
end

lisaNurseRoomSteps[1] = function()
    sh.sound.bgm_crossfade(4)
end

lisaNurseRoomSteps[2] = function()
    sh.event_fade_cmd(2, false, 0, 4096, false)
end

lisaNurseRoomSteps[3] = function()
    sh.npc.process_loads()
    sh.npc.spawn("Lisa", 0, 532480, -286720, 0, 3) -- 130.0, -70.0
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[4]()
end

lisaNurseRoomSteps[4] = function()
    sh.msg.show_with_audio("map7_s02.harry", "voice_msg_audio_idx", "nurse_room_voice_cmds[0]")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 0, 81920, true, false)
    sh.event_wait_timer(3276, false)
end

lisaNurseRoomSteps[5] = function()
    sh.event_chara_anim(0, 0, "cutscene_react", false)
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[6]()
end

lisaNurseRoomSteps[6] = function()
    sh.msg.show_with_audio("map7_s02.harry", "voice_msg_audio_idx", "nurse_room_voice_cmds[0]")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 0, 81920, true, false)
end

lisaNurseRoomSteps[7] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 0, 81920, true, true)
end

lisaNurseRoomSteps[8] = function()
    sh.event_chara_anim(0, 1, "stand_talk", false)
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.map.set("cutscene_timer", 86016) -- frame 21.0
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[9]()
end

lisaNurseRoomSteps[9] = function()
    sh.msg.show_with_audio("map7_s02.lisa_whats_the_matter_with", "voice_msg_audio_idx", "nurse_room_voice_cmds[0]")
end

lisaNurseRoomSteps[10] = function()
    sh.msg.show_with_audio("map7_s02.lisa", "voice_msg_audio_idx", "nurse_room_voice_cmds[0]")
end

lisaNurseRoomSteps[11] = function()
    sh.event_chara_anim(0, 1, "lean_back", false)
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[12]()
end

lisaNurseRoomSteps[12] = function()
    sh.msg.show_with_audio("map7_s02.save_me_from_them", "voice_msg_audio_idx", "nurse_room_voice_cmds[0]")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 90112, 397312, false, false)
end

lisaNurseRoomSteps[13] = function()
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[14]()
end

lisaNurseRoomSteps[14] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 90112, 397312, false, true)
end

lisaNurseRoomSteps[15] = function()
    sh.event_chara_anim(0, 0, "scene_lisa_room", false)
    sh.sd_call("lisa_nurse_room_voice") -- xa audio 568 sound
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[16]()
end

lisaNurseRoomSteps[16] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 90112, 540672, true, true)
end

lisaNurseRoomSteps[17] = function()
    sh.event_chara_anim(0, 1, "turn_away", false)
    sh.flag_set("M7S01_LisaWardSceneTurnedAway", true)
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[18]()
end

lisaNurseRoomSteps[18] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 544768, 643072, true, true)
end

lisaNurseRoomSteps[19] = function()
    LisaBloodyLoad()
    sh.npc.set_pos_x(0, 737280)  -- 180.0 m
    sh.npc.set_pos_z(0, -409600) -- -100.0 m
    sh.load.close_range_chunks()
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[20]()
end

lisaNurseRoomSteps[20] = function()
    sh.event_fade_cmd(2, true, 0, 2048, false)
end

lisaNurseRoomSteps[21] = function()
    sh.event_step_reset()
end

lisaNurseRoomSteps[22] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

lisaNurseRoomSteps[23] = function()
    sh.flag_set("M7S01_LisaWardSceneFadedOut", true)
    sh.event_step_increment(0)
    return lisaNurseRoomSteps[24]()
end

lisaNurseRoomSteps[24] = function()
    sh.npc.set_pos_x(0, 737280)
    sh.npc.set_pos_z(0, -409600)
    sh.load.close_range_chunks()
    LisaBloodyLoad()
    sh.npc.process_loads()
    sh.event_step_increment(0)
    return lisaNurseRoomSteps.default()
end

lisaNurseRoomSteps.default = function()
    sh.state_set(0)
    sh.flag_set("M7S01_CutsceneLisaWardDone", true)
    sh.npc.clear(1, 0, 0)
    sh.npc.process_loads()
    sh.sd_call("stop_xa_audio")
end

function Cutscene_LisaNurseRoom()
    if sh.input_action("skip", 1) and sh.event_step_get(0) >= 4 and sh.event_step_get(0) <= 20 then
        sh.event.step_set(0, 22)
    end

    local s = sh.event_step_get(0)
    runStep(lisaNurseRoomSteps, s)
    NurseDmsTail()
end

-- index 29: NURSE2.DMS cutscene: bloody Lisa at the door.
-- lisa_door — the door's world object pose (map state: "lisa_door.pos", "lisa_door.rot").
local function Nurse2DmsTail()
    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, 11)
        sh.cutscene.chara(1, "BLS", t, 11)
        if sh.map.get("door_follows_dms") ~= 0 then
            sh.cutscene.transform_to("lisa_door.pos", "lisa_door.rot", "ADOOR", t, 11)
        end
        sh.cutscene.camera(t, 11, "camera_target_pos", "camera_target_look")
    end
end

local function Adv(startQ12, endQ12, clamp, auto)
    sh.cutscene.timer_advance("cutscene_timer", 40960, startQ12, endQ12, clamp, auto) -- rate 10.0
end

local bloodyLisaDoorSteps = {}

bloodyLisaDoorSteps[0] = function()
    sh.load.file("ANIM/NURSE2.DMS", 11) -- NURSE2.DMS
    sh.load.wait()
    sh.cutscene.prepare(11)
    sh.npc.spawn("BloodyLisa", 0, sh.npc.pos_x(0), sh.npc.pos_z(0) + 4096, 0, 3) -- Bloody Lisa
    sh.map.set("cutscene_timer", 0)
    sh.map.set("voice_msg_audio_idx", 0)
    sh.map.set("door_follows_dms", 0)
    sh.event_step_increment(0)
end

bloodyLisaDoorSteps[1] = function()
    sh.event_chara_anim(0, 0, "scene_bloody_lisa_1", false)
    sh.event_chara_anim(0, 1, "appear", false)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_step_increment(0)
    return bloodyLisaDoorSteps[2]()
end

bloodyLisaDoorSteps[2] = function()
    Adv(0, 176128, true, true)        -- [0, 43]
end

bloodyLisaDoorSteps[3] = function()
    Adv(180224, 241664, true, true)   -- [44, 59]
end

bloodyLisaDoorSteps[4] = function()
    sh.event_chara_anim(0, 0, "scene_bloody_lisa_2", false)
    sh.light.bone_detach()
    sh.light.lens_flare_detach()
    sh.light.set_intensity(4096)
    sh.water_effect_enable()
    sh.event_step_increment(0)
    return bloodyLisaDoorSteps[5]()
end

bloodyLisaDoorSteps[5] = function()
    Adv(245760, 311296, true, true)   -- [60, 76]
    sh.cutscene.light(sh.map.get("cutscene_timer"), 11)
end

bloodyLisaDoorSteps[6] = function()
    sh.map.set("door_follows_dms", 1)
    sh.water_effect_disable()
    sh.light.flashlight_fix()
    sh.light.set_intensity(3276) -- 0.8
    sh.sound.play_with_flags("otherworld_door_open", "lisa_door.pos", 128, 0)
    sh.event_step_increment(0)
    return bloodyLisaDoorSteps[7]()
end

bloodyLisaDoorSteps[7] = function()
    Adv(315392, 409600, true, true)   -- [77, 100]
end

bloodyLisaDoorSteps[8] = function()
    sh.event_chara_anim(0, 0, "scene_bloody_lisa_3", false)
    sh.npc.clear(1, 0, 0)
    sh.sound.play_with_flags("otherworld_door_close", "lisa_door.pos", 128, 0)
    sh.event_step_increment(0)
    return bloodyLisaDoorSteps[9]()
end

bloodyLisaDoorSteps[9] = function()
    Adv(413696, 622592, true, false)  -- [101, 152]
    sh.event_wait_timer(1228, false)  -- 0.3 s
end

bloodyLisaDoorSteps[10] = function()
    sh.sd_call("bloody_lisa_door_voice")
    sh.event_step_increment(0)
    return bloodyLisaDoorSteps[11]()
end

bloodyLisaDoorSteps[11] = function()
    Adv(413696, 581632, false, true)  -- [101, 142]
end

bloodyLisaDoorSteps[12] = function()
    sh.event_step_increment(0)
    return bloodyLisaDoorSteps[13]()
end

bloodyLisaDoorSteps[13] = function()
    Adv(413696, 622592, true, false)
    sh.event_fade_cmd(2, true, 0, 1024, false) -- 0.25
end

bloodyLisaDoorSteps[14] = function()
    if sh.sound.streaming() ~= 0 then
        return
    end
    sh.event_step_increment(0)
    return bloodyLisaDoorSteps[15]()
end

bloodyLisaDoorSteps[15] = function()
    sh.event_wait_timer(8192, false) -- 2.0 s
end

bloodyLisaDoorSteps[16] = function()
    sh.event.step_set(0, 19)
end

bloodyLisaDoorSteps[17] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

bloodyLisaDoorSteps[18] = function()
    sh.sd_call("stop_xa_audio")
    sh.npc.clear(1, 0, 0)
    sh.event_step_increment(0)
end

bloodyLisaDoorSteps[19] = function()
    sh.player.map_anim_load(sh.save.map_idx())
    sh.load.wait()
    sh.event_step_increment(0)
    return bloodyLisaDoorSteps.default()
end

bloodyLisaDoorSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.map.set("cutscene_timer", -1) -- not running
    sh.camera_auto_return(true)
    sh.flag_set("M7S01_CutsceneBloodyLisaDone", true)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.water_effect_disable()
    sh.light.flashlight_fix()
    sh.light.set_intensity(4096)
    sh.light.flashlight_on()
    sh.player.weapon_restore()
    sh.held_item_show()
    sh.npc.anim_info_clear()
    sh.event_fade_cmd(0, false, 0, 4096, false)
end

function Cutscene_BloodyLisaDoor()
    if sh.input_action("skip", 1) and sh.event_step_get(0) >= 2 and sh.event_step_get(0) <= 15 then
        sh.event.step_set(0, 17)
    end

    local s = sh.event_step_get(0)
    runStep(bloodyLisaDoorSteps, s)
    Nurse2DmsTail()
end

-- message of each diary page.
local LisdiaryMsgs = {
    "map7_s02.ask_doctor_to_let_me",
    "map7_s02.still_alive_but_with_wounds",
    "map7_s02.told_the_doctor_i_quit",
    "map7_s02.the_room_is_filled_with",
    "map7_s02.feeling_bad_need_to_throw",
    "map7_s02.blood_and_pus_flow_from",
    "map7_s02.need_drug",
    "map7_s02.help_me",
}
local s_lisdiaryPage = 0 -- current diary page

-- index 30: reading Lisa's diary (8 pages, LISDIARY.TIM).
function MapEvent_LisaDiaryRead()
    sh.background_gamma(72) -- 9/32
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_bg_texture_cmd(0, "TIM/LISDIARY.TIM", false) -- start loading
        s_lisdiaryPage = 0
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.chara_anim_play_to_end(0, "pickup_crouch")
    elseif step == 3 then
        sh.event_msg(false, "map7_s02.there_is_a_diary_on", 0, 0, 0, false)
    elseif step == 4 then
        sh.event_bg_texture_cmd(1, 0, false) -- wait for loading
    elseif step == 5 then
        sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
    elseif step == 6 then
        sh.event_fade_cmd(2, false, 0, 0, false) -- Auto, in
        sh.event_bg_texture_cmd(2, 0, false) -- draw
    elseif step == 7 or step == 8 then
        if step == 7 then
            sh.sound.sd_call("menu_map") -- menu map sound
            sh.event_step_increment(0)
            step = 8
        end
        sh.event_wait_timer(1228, false) -- 0.3 s
        sh.event_bg_texture_cmd(2, 0, false)
    elseif step == 9 then
        sh.event_msg(false, LisdiaryMsgs[s_lisdiaryPage + 1], 0, 0, 0, false)
        sh.event_bg_texture_cmd(2, 0, false)
    elseif step == 10 then
        sh.event_bg_texture_cmd(2, 0, false)
        s_lisdiaryPage = s_lisdiaryPage + 1
        if s_lisdiaryPage >= 8 then
            sh.event_step_increment(0)
        else
            sh.event.step_set(0, 7) -- jump to the step
        end
    elseif step == 11 then
        sh.event_fade_cmd(2, true, 0, 0, false)
        sh.event_bg_texture_cmd(2, 0, false)
    elseif step == 12 then
        sh.event_fade_cmd(2, false, 0, 0, false)
    elseif step == 13 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")
    else
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- Integer helpers of the panel puzzles (C semantics: division truncates toward zero,
-- 16-bit fields wrap).
local function PanelTDiv(a, b)
    local q = a // b
    if q < 0 and q * b ~= a then q = q + 1 end
    return q
end

local function PanelS16(v)
    v = v & 0xFFFF
    if v >= 0x8000 then v = v - 0x10000 end
    return v
end

-- Q12 product with a 64-bit intermediate (Q12_MULT_PRECISE).
local function PanelMulQ12(a, b) return (a * b) // 4096 end

-- puzzle_cursor_x/y += stick * scale / 75 per 1/30 s (136 = 1/30 s), clamped to +-limit.
local function PanelCursorAxis(var, axis, scale, limit)
    local v = sh.map.get(var) + PanelTDiv(PanelTDiv(sh.input_axis(1, axis) * scale, 75) * sh.delta_time_get(), 136)
    v = math.max(math.min(v, limit), -limit)
    sh.map.set(var, v)
end

local function PanelCursorDraw()
    local cx = sh.map.get("puzzle_cursor_x") // 4096 -- whole pixels
    local cy = sh.map.get("puzzle_cursor_y") // 4096
    sh.screen.cursor_draw(PanelS16(cx + 8), cy + 8, 8, 8, 0, 64, 32, 32, 128, 192, 0, 12)
end

-- index 31: the stone panels (an early copy of map7_s01 index 30: the panel picture loads through the
-- background loader, the sounds are the unfinished base id). The event sets one of M7S01_StonePanel0Viewing..M7S01_StonePanel6Viewing when it
-- starts; that picks the picture (stone_panel_pictures). Pictures 1, 5 and 6 carry a 3x3
-- dial whose chosen cell (1..9) is kept in four flags each (M7S01_StonePanel1DialBit0.., M7S01_StonePanel5DialBit0..,
-- M7S01_StonePanel6DialBit0..); when the twelve flags spell stone_panel_solution the Stone of Time
-- (time_stone) drops into the water and M7S01_StoneOfTimeReleased lets it be taken.
local stonePanelCell = 0 -- the chosen cell of the open panel, 0 = none
local stonePanelIdx = 0 -- which of the seven pictures is open
local stoneFallSpeed = 0 -- unsigned 16-bit, like the original variable

-- The viewing flag of each panel picture 0..6, and the twelve dial flags in the order the
-- solution counts them: four bits each of panels 1, 5 and 6.
local StonePanelViewingFlags = {[0] = "M7S01_StonePanel0Viewing", "M7S01_StonePanel1Viewing", "M7S01_StonePanel2Viewing", "M7S01_StonePanel3Viewing", "M7S01_StonePanel4Viewing", "M7S01_StonePanel5Viewing", "M7S01_StonePanel6Viewing"}
local StonePanelDialBits = {"M7S01_StonePanel1DialBit0", "M7S01_StonePanel1DialBit1", "M7S01_StonePanel1DialBit2", "M7S01_StonePanel1DialBit3", "M7S01_StonePanel5DialBit0", "M7S01_StonePanel5DialBit1", "M7S01_StonePanel5DialBit2", "M7S01_StonePanel5DialBit3", "M7S01_StonePanel6DialBit0", "M7S01_StonePanel6DialBit1", "M7S01_StonePanel6DialBit2", "M7S01_StonePanel6DialBit3"}

local StonePanelDialOf = {
    [1] = {"M7S01_StonePanel1DialBit0", "M7S01_StonePanel1DialBit1", "M7S01_StonePanel1DialBit2", "M7S01_StonePanel1DialBit3"},
    [5] = {"M7S01_StonePanel5DialBit0", "M7S01_StonePanel5DialBit1", "M7S01_StonePanel5DialBit2", "M7S01_StonePanel5DialBit3"},
    [6] = {"M7S01_StonePanel6DialBit0", "M7S01_StonePanel6DialBit1", "M7S01_StonePanel6DialBit2", "M7S01_StonePanel6DialBit3"},
}

-- The four dial flags of the open panel: those of panel 1 or 5, for any other panel those of panel 6.
local function StonePanelDialFlags()
    return StonePanelDialOf[stonePanelIdx] or StonePanelDialOf[6]
end

-- The chosen cell marker: a 14x14 piece of the panel picture (texels 320, 0) over its cell.
local function StonePanelMarkDraw()
    if stonePanelCell == 0 then return end
    local s = sh.event_step_get(0)
    if (s >= 8 and s <= 10) or s == 15 then
        local n = stonePanelCell - 1
        sh.render.sprite{texture = "background", layer = "picture",
            rect = {(n % 3) * 35 - 42, (n // 3) * 35 - 32, 14, 14}, uv = {320, 0}}
    end
end

-- Enter over a cell: it becomes the chosen one; its number goes to the panel's flags. The
-- original's flag loop reuses the row counter, so the scan ends after the first hit.
local function StonePanelPress()
    local cx = sh.map.get("puzzle_cursor_x") // 4096
    local cy = sh.map.get("puzzle_cursor_y") // 4096
    for i = 0, 2 do
        for j = 0, 2 do
            local col = j * 35
            local row = i * 35
            if not (cx < col - 46 or cx > col - 24) and not (cy < row - 37 or cy > row - 15) then
                local dx = col - (cx + 35)
                local dy = row - (cy + 26)
                if dx * dx + dy * dy < 122 then
                    stonePanelCell = j + i * 3 + 1
                    sh.sd_call(1280) -- the base sound id: this early copy plays no named sound
                    local dial = StonePanelDialFlags()
                    for b = 0, 3 do
                        sh.flag_set(dial[b + 1], ((stonePanelCell >> b) & 1) ~= 0)
                    end
                    local code = 0
                    for b = 0, 11 do
                        if sh.flag_get(StonePanelDialBits[b + 1]) then code = code + (1 << b) end
                    end
                    if sh.map.get("stone_panel_solution") == code then
                        sh.event.step_set(0, 10)
                    end
                    return
                end
            end
        end
    end
end

-- The stone slides out (2 s), falls onto the ledge, tips over and drops into the water.
local function StoneFall()
    local sub_step = sh.event_step_get(1)
    local dt = sh.delta_time_get()
    if sub_step == 0 then
        sh.sd_call(1280)
        sh.event_step_increment(1)
        sub_step = 1
    end
    if sub_step == 1 then
        if sh.state_timer_get() + dt > 8192 then -- 2.0 s
            sh.state_timer_set(8192)
            sh.world.object_model_name_set("time_stone", "STONE3_H")
            sh.sfx_stop(1280)
            sh.event_step_increment(1)
        else
            sh.state_timer_set(sh.state_timer_get() + dt)
        end
        sh.map.set("time_stone.pos.z", PanelTDiv(sh.state_timer_get() * 409, 8192) - 575897) -- 0.1 per 2 s from -140.6
    elseif sub_step == 2 then
        stoneFallSpeed = (stoneFallSpeed + sh.game.gravity_speed() // 2) & 0xFFFF
        local y = sh.map.get("time_stone.pos.y")
        if y < -4300 then -- -1.05
            y = y + PanelMulQ12(dt, stoneFallSpeed)
            sh.map.set("time_stone.pos.y", y)
            if y > -4300 then
                sh.map.set("time_stone.pos.y", -4300)
                sh.event_step_increment(1)
                sh.state_timer_set(0)
                stoneFallSpeed = 0
                sh.sd_call(1280)
            end
        end
    elseif sub_step == 3 then
        local rz = PanelS16(sh.map.get("time_stone.rot.z") + PanelMulQ12(dt, -284)) -- -25 deg/s
        sh.map.set("time_stone.rot.z", rz)
        sh.map.set("time_stone.pos.y", (sh.math_cos(rz) * 614) // 4096 - 4914) -- 0.15, 1.2
        sh.map.set("time_stone.pos.z", (sh.math_sin(rz) * -614) // 4096 - 575488) -- -0.15, 140.5
        sh.state_timer_set(sh.state_timer_get() + dt)
        if sh.state_timer_get() > 2048 then -- 0.5 s
            sh.event_step_increment(1)
            sh.state_timer_set(0)
        end
    elseif sub_step == 4 then
        stoneFallSpeed = (stoneFallSpeed + sh.game.gravity_speed()) & 0xFFFF
        sh.map.set("time_stone.rot.z", PanelS16(sh.map.get("time_stone.rot.z") + PanelMulQ12(dt, -1365))) -- -120 deg/s
        sh.map.set("time_stone.pos.y", sh.map.get("time_stone.pos.y") + PanelMulQ12(stoneFallSpeed, dt))
        sh.map.set("time_stone.pos.z", sh.map.get("time_stone.pos.z") + PanelMulQ12(dt, 1228)) -- 0.3
        if sh.map.get("time_stone.pos.y") > 0 then
            sh.event_step_increment(1)
        end
    elseif sub_step == 5 then
        sh.sd_call(1280)
        sh.map.set("time_stone.pos.x", -571392) -- -139.5
        sh.map.set("time_stone.pos.y", -204) -- -0.05
        sh.map.set("time_stone.pos.z", -573440) -- -140.0
        sh.map.set("time_stone.rot.z", -1024) -- -0.25
        sh.event_step_increment(0)
    end
end

local stonePanelPuzzleSteps = {}

stonePanelPuzzleSteps[0] = function()
    sh.player_freeze(true)
    stonePanelCell = 0
    stonePanelIdx = 0
    while not sh.flag_get(StonePanelViewingFlags[stonePanelIdx]) do
        stonePanelIdx = stonePanelIdx + 1
    end
    sh.event_bg_texture_cmd(0, sh.map.get("stone_panel_pictures", stonePanelIdx), false) -- start loading
    sh.event_step_increment(0)
    return stonePanelPuzzleSteps[1]()
end

stonePanelPuzzleSteps[1] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto
    return stonePanelPuzzleSteps[2]()
end

stonePanelPuzzleSteps[2] = function()
    sh.event_bg_texture_cmd(1, 0, false) -- wait for loading
end

stonePanelPuzzleSteps[3] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
    sh.event_bg_texture_cmd(2, 0, false) -- draw
end

stonePanelPuzzleSteps[4] = function()
    if sh.input_action("enter", 1) or sh.input_action("cancel", 1) then
        if not sh.flag_get("M7S01_StoneOfTimeReleased") and (stonePanelIdx == 1 or stonePanelIdx == 5 or stonePanelIdx == 6) then
            sh.event.step_set(0, 5)
        else
            sh.event.step_set(0, 15)
        end
    end
    sh.event_bg_texture_cmd(2, 0, false)
end

stonePanelPuzzleSteps[6] = function()
    sh.event_bg_texture_cmd(7, "TIM/HS_PANEL.TIM", false) -- Auto
end

stonePanelPuzzleSteps[7] = function()
    do
        local dial = StonePanelDialFlags()
        for b = 0, 3 do
            if sh.flag_get(dial[b + 1]) then
                stonePanelCell = (stonePanelCell + (1 << b)) & 0xFF
            end
        end
    end
    sh.map.set("puzzle_cursor_x", 0)
    sh.map.set("puzzle_cursor_y", 0)
    sh.event_step_increment(0)
    return stonePanelPuzzleSteps[8]()
end

stonePanelPuzzleSteps[8] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.event_fade_cmd(2, false, 0, 0, false)
end

stonePanelPuzzleSteps[9] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.screen.puzzle_cursor("puzzle_cursor_x", "puzzle_cursor_y")
    PanelCursorAxis("puzzle_cursor_x", 0, 16384, 327680) -- +-80
    PanelCursorAxis("puzzle_cursor_y", 1, 16384, 327680)
    sh.game.timer_update()
    PanelCursorDraw()
    if sh.input_action("cancel", 1) then
        sh.event.step_set(0, 15)
        return
    end
    if sh.input_action("enter", 1) then
        StonePanelPress()
    end
end

stonePanelPuzzleSteps[11] = function()
    sh.cutscene.border_show()
    stoneFallSpeed = 0
    sh.npc.anim_flags_clear(0, 2) -- hide the player model
    sh.water_effect_enable()
    sh.camera.position(nil, -565862, -7004, -568401, 0, 0, 0, 0, true) -- (-138.15, -1.71, -138.77)
    sh.camera.look_at(nil, -575733, -4218, -581140, 0, 0, 0, 0, true) -- (-140.56, -1.03, -141.88)
    sh.world.object_model_name_set("time_stone", "STONE2_H")
    sh.event_step_increment(0)
    return stonePanelPuzzleSteps[12]()
end

stonePanelPuzzleSteps[12] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
end

stonePanelPuzzleSteps[13] = function()
    StoneFall()
end

stonePanelPuzzleSteps[14] = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.camera_auto_return(true)
    sh.water_effect_disable()
    sh.npc.anim_flags_set(0, 2) -- show the player model
    sh.flag_set(StonePanelViewingFlags[stonePanelIdx], false)
    sh.flag_set("M7S01_StoneOfTimeReleased", true)
    sh.event_fade_cmd(0, false, 2, 0, false)
end

stonePanelPuzzleSteps[15] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.event_fade_cmd(2, true, 0, 0, false)
end

stonePanelPuzzleSteps[5] = stonePanelPuzzleSteps[15]
stonePanelPuzzleSteps[10] = stonePanelPuzzleSteps[15]

stonePanelPuzzleSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.flag_set(StonePanelViewingFlags[stonePanelIdx], false)
end

function MapEvent_StonePanelPuzzle()
    StonePanelMarkDraw()

    local s = sh.event_step_get(0)
    runStep(stonePanelPuzzleSteps, s)
end

-- index 32: item pickup.
function MapEvent_CrestOfMercuryTake()
    sh.event_item_take("CrestOfMercury", 1, "M7S01_PickupCrestOfMercury", "map7_s02.there_is_a_crest_of")
end

-- index 33: item pickup.
function MapEvent_RingOfContractTake()
    sh.event_item_take("RingOfContract", 1, "M7S01_PickupRingOfContract", "map7_s02.there_is_a_ring_of")
end

-- index 34: item pickup.
function MapEvent_CameraTake()
    sh.event_item_take("Camera", 1, "M7S01_PickupCamera", "map7_s02.there_is_a_camera_take")
end

-- index 35: DRIVR.DMS cutscene: screwdriver in the fuse box
-- screwdriver_sfx_timer repeats the sound.
local function DriverSfxPulse()
    local timer = sh.map.get("screwdriver_sfx_timer")
    if timer == 0 then
        sh.sfx_flags_play("engine_idle", -258867, -5939, -569548, 128, 0) -- at the fuse box
        sh.map.set("screwdriver_sfx_timer", 4915)                -- 1.2 s
    else
        sh.map.set("screwdriver_sfx_timer", timer - sh.delta_time_get())
        if sh.map.get("screwdriver_sfx_timer") < 0 then
            sh.map.set("screwdriver_sfx_timer", 0)
        end
    end
end

local fuseBoxScrewdriverSteps = {}

fuseBoxScrewdriverSteps[0] = function()
    sh.player_freeze(true)
    sh.cutscene.border_show()
    sh.cutscene_flag(true)
    sh.load.file("ANIM/DRIVR.DMS", 11) -- DRIVR.DMS
    sh.load.wait()
    sh.cutscene.prepare(11)
    sh.event_chara_anim(0, 0, "scene_arrive_hospital", false)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.map.set("screwdriver_sfx_timer", 1228) -- 0.3 s
    sh.map.set("cutscene_timer", 0)
    sh.event_step_increment(0)
    return fuseBoxScrewdriverSteps[1]()
end

fuseBoxScrewdriverSteps[1] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 0, 126976, false, true) -- [0, 31]
    DriverSfxPulse()
end

fuseBoxScrewdriverSteps[2] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
    sh.cutscene.timer_advance("cutscene_timer", 40960, 0, 131072, true, false) -- [0, 32]
    DriverSfxPulse()
end

fuseBoxScrewdriverSteps[3] = function()
    sh.sfx_flags_play("fuse_box_door", -257638, 0, -569753, 128, 0) -- at the fuse box door
    sh.event_step_increment(0)
    return fuseBoxScrewdriverSteps[4]()
end

fuseBoxScrewdriverSteps[4] = function()
    sh.event_wait_timer(2048, false) -- 0.5 s
end

fuseBoxScrewdriverSteps.default = function()
    sh.player_freeze(false, true)
    sh.state_set(0)
    sh.camera_auto_return(true)
    sh.map.set("cutscene_timer", -1) -- not running
    sh.flag_set("M7S01_FuseBoxPlateRemoved", true)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_fade_cmd(0, false, 2, 0, false)
end

function Cutscene_FuseBoxScrewdriver()
    local s = sh.event_step_get(0)
    runStep(fuseBoxScrewdriverSteps, s)
    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, 11)
        sh.cutscene.camera(t, 11, "camera_target_pos", "camera_target_look")
    end
end

local fuseBoxSteps = {}

fuseBoxSteps[0] = function()
    sh.player_freeze(true)
    sh.event_step_increment(0)
    return fuseBoxSteps[1]()
end

fuseBoxSteps[1] = function()
    sh.event.bg_texture_load_fade_in("TIM/ELCWIRE2.TIM", 0) -- ELCWIRE2.TIM
end

fuseBoxSteps[2] = function()
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.event_step_increment(0)
    return fuseBoxSteps[3]()
end

fuseBoxSteps[3] = function()
    sh.event_step_increment(0)
    return fuseBoxSteps[4]()
end

fuseBoxSteps[4] = function()
    sh.event_bg_texture_cmd(2, 0, false) -- draw
    sh.event_fade_cmd(1, false, 0, 0, false) -- Wait
end

fuseBoxSteps[5] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.msg.show(true, "map7_s02.there_is_a_key_of_2", 6, 7, 0, false)
end

fuseBoxSteps[6] = function()
    sh.event_inv_item_cmd(3, "KeyOfAratron", 1, false) -- add to the inventory, Key of Aratron
    sh.flag_set("M7S02_PickupKeyOfAratron", true) -- flag: pickup key of aratron
    sh.event_step_increment(0)
    return fuseBoxSteps[7]()
end

fuseBoxSteps[7] = function()
    sh.event.bg_texture_fade_out(0, 0)
end

fuseBoxSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
end

-- index 36: fuse box ELCWIRE0/1/2.TIM: Key of Aratron with flags 486+549, sparks
-- (sound electric_spark) timed by fuse_box_spark_sfx_timer during sub-steps 3..5.
function MapEvent_FuseBox()
    if sh.flag_get("M7S01_FuseBoxPlateRemoved") and sh.flag_get("M7S02_GeneratorStopped") then
        sh.screen.set_bg_gamma(120) -- 15 / 32

        local s = sh.event_step_get(0)
        runStep(fuseBoxSteps, s)
    elseif sh.flag_get("M7S01_FuseBoxPlateRemoved") then
        sh.event_msg_with_bg("TIM/ELCWIRE1.TIM", 0, 0, "map7_s02.there_is_a_key") -- ELCWIRE1.TIM
        local sub_step = sh.event_step_get(1)
        if sub_step >= 3 and sub_step <= 5 then
            local t = sh.map.get("fuse_box_spark_sfx_timer")
            if t ~= 0 then
                t = ((t - sh.delta_time_get() + 0x8000) & 0xFFFF) - 0x8000 -- count down, wrapping like a 16-bit value
                if t < 0 then
                    t = 0
                end
                sh.map.set("fuse_box_spark_sfx_timer", t)
            else
                sh.sd_call("electric_spark")
                sh.map.set("fuse_box_spark_sfx_timer", sh.rng_int(819, 9010)) -- 0.2, 2.2 - 1
            end
        end
    else
        sh.screen.set_bg_gamma(112) -- 7 / 16
        sh.event_msg_with_bg("TIM/ELCWIRE0.TIM", 0, 0, "map7_s02.a_steel_plate_is_screwed") -- ELCWIRE0.TIM
    end
end

-- index 37: 3x3 puzzle hint/picture: texture chosen by
-- flags 496/498/499, msg map7_s02.the_light_to_the_future/108, then the work flags 496/497 are cleared.
function MapEvent_Puzzle3x3HintView()
    if sh.flag_get("M7S01_ExaminingDarknessPicture") then
        if sh.flag_get("M7S01_DarknessPicturePhotographed") then
            sh.event_msg_with_bg("TIM/3X3HINT2.TIM", 0, 0, "map7_s02.the_light_illuminating_the")
        else
            sh.event_msg_with_bg("TIM/3X3PICT2.TIM", 0, 0, "map7_s02.the_light_illuminating_the")
        end
    else
        if sh.flag_get("M7S01_FuturePicturePhotographed") then
            sh.event_msg_with_bg("TIM/3X3HINT1.TIM", 0, 0, "map7_s02.the_light_to_the_future")
        else
            sh.event_msg_with_bg("TIM/3X3PICT1.TIM", 0, 0, "map7_s02.the_light_to_the_future")
        end
    end
    sh.flag_set("M7S01_ExaminingDarknessPicture", false)
    sh.flag_set("M7S01_ExaminingFuturePicture", false)
end

-- index 38: 3x3 hint inspection (gives Harry the camera).
function MapEvent_Puzzle3x3HintInspect()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        if sh.flag_get("M7S01_ExaminingDarknessPicture") then
            sh.event_bg_texture_cmd(0, "TIM/3X3HINT2.TIM", false)
            sh.flag_set("M7S01_DarknessPicturePhotographed", true)
        else
            sh.event_bg_texture_cmd(0, "TIM/3X3HINT1.TIM", false)
            sh.flag_set("M7S01_FuturePicturePhotographed", true)
        end
        sh.screen.set_fade_status(0) -- reset the fade
        sh.sound.set_bgm_flags(sh.sound.bgm_flags() | 1) -- pause the music
        sh.sound.sd_call("photo_pick_up") -- sound photo_pick_up
        sh.event_step_increment(0)
    elseif step == 1 then
        sh.event_fade_cmd(2, true, 1, 10240, false) -- Auto, out, type 1, 2.5
        sh.sound.set_bgm_flags(sh.sound.bgm_flags() | 1) -- pause the music
    elseif step == 2 then
        sh.event_bg_texture_cmd(1, 0, false) -- wait for loading
    elseif step == 3 then
        sh.event_fade_cmd(2, false, 1, 1638, false) -- Auto, in, 0.4
        sh.event_bg_texture_cmd(2, 0, false) -- draw
    elseif step == 4 then
        if sh.flag_get("M7S01_ExaminingDarknessPicture") then sh.event_msg(false, "map7_s02.the_light_illuminating_the", 0, 0, 0, false)
        else sh.event_msg(false, "map7_s02.the_light_to_the_future", 0, 0, 0, false) end
        sh.event_bg_texture_cmd(2, 0, false)
    elseif step == 5 then
        sh.event_fade_cmd(2, true, 0, 0, false)
        sh.event_bg_texture_cmd(2, 0, false)
    else
        sh.player_freeze(false)
        sh.state_set(0)
        sh.flag_set("M7S01_ExaminingDarknessPicture", false)
        sh.flag_set("M7S01_ExaminingFuturePicture", false)
        sh.event_fade_cmd(0, false, 0, 0, false) -- Start
        sh.event_inv_item_cmd(3, "Camera", 1, false) -- add to the inventory
    end
end

-- index 39: the 3x3 door (3X3DOR.TIM, the same puzzle as map7_s01): three 3x3 grids of
-- lamps (door_3x3_cells: screen pixel centres, cell [grid][column][row]); Enter over a lamp
-- toggles it (bit grid * 9 + row * 3 + column). The pattern door_3x3_solution_a opens the door
-- of M7S01_Door3x3AExamining, door_3x3_solution_b the one of M7S01_Door3x3BExamining; M7S01_Door3x3AUnlocked / M7S01_Door3x3BUnlocked records which.
local door3x3Lit = 0

local function Door3x3Cell(i, j, k)
    local n = i * 9 + j * 3 + k
    return sh.map.get("door_3x3_cells.x", n), sh.map.get("door_3x3_cells.y", n)
end

-- The lit lamps: 9x9 pieces of the door picture (texels 320, 0) over their cells.
local function Door3x3LampsDraw()
    local lamps = {}
    for i = 0, 2 do
        for j = 0, 2 do
            for k = 0, 2 do
                if (door3x3Lit & (1 << (i * 9 + k * 3 + j))) ~= 0 then
                    local x, y = Door3x3Cell(i, j, k)
                    lamps[#lamps + 1] = {rect = {x - 164, y - 124, 9, 9}}
                end
            end
        end
    end
    sh.render.sprite{texture = "background", layer = "picture", uv = {320, 0}, sprites = lamps}
end

local function Door3x3Press()
    for i = 0, 2 do
        for j = 0, 2 do
            for k = 0, 2 do
                local x, y = Door3x3Cell(i, j, k)
                local cx = sh.map.get("puzzle_cursor_x") // 4096
                local cy = sh.map.get("puzzle_cursor_y") // 4096
                if not ((x - 169) > cx or (x - 151) < cx) and not ((y - 129) > cy or (y - 111) < cy) then
                    local dx = x - 160 - cx
                    local dy = y - 120 - cy
                    if dx * dx + dy * dy <= 81 then
                        local bit = 1 << (i * 9 + k * 3 + j)
                        if (door3x3Lit & bit) == 0 then
                            door3x3Lit = door3x3Lit + bit
                        else
                            door3x3Lit = door3x3Lit - bit
                        end
                        sh.sd_call("lamp_panel_toggle")
                        if (door3x3Lit == sh.map.get("door_3x3_solution_a") and sh.flag_get("M7S01_Door3x3AExamining")) or
                            (door3x3Lit == sh.map.get("door_3x3_solution_b") and sh.flag_get("M7S01_Door3x3BExamining")) then
                            sh.event.step_set(0, 5)
                        end
                    end
                end
            end
        end
    end
end

local puzzle3x3DoorSteps = {}

puzzle3x3DoorSteps[0] = function()
    sh.player_freeze(true)
    sh.event_step_increment(0)
    return puzzle3x3DoorSteps[1]()
end

puzzle3x3DoorSteps[1] = function()
    sh.event_fade_cmd(2, true, 0, 0, false) -- Auto
end

puzzle3x3DoorSteps[2] = function()
    sh.sound.set_bgm_flags(sh.sound.bgm_flags() | 1) -- pause the music
    sh.render.texture_load{file = "TIM/3X3DOR.TIM", texture = "background", buffer = "image"}
    sh.load.wait()
    sh.map.set("puzzle_cursor_x", 0)
    sh.map.set("puzzle_cursor_y", 0)
    door3x3Lit = 0
    sh.event_step_increment(0)
    return puzzle3x3DoorSteps[3]()
end

puzzle3x3DoorSteps[3] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
    sh.event_bg_texture_cmd(2, 0, false) -- draw
end

puzzle3x3DoorSteps[4] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.screen.puzzle_cursor("puzzle_cursor_x", "puzzle_cursor_y")
    PanelCursorAxis("puzzle_cursor_x", 0, 8192, 471040) -- +-115
    PanelCursorAxis("puzzle_cursor_y", 1, 8192, 430080) -- +-105
    sh.game.timer_update()
    PanelCursorDraw()
    if sh.input_action("cancel", 1) then
        if door3x3Lit ~= 0 then
            door3x3Lit = 0
            sh.sd_call("lamp_panel_toggle")
        else
            sh.event.step_set(0, 8)
        end
        return
    end
    if sh.input_action("enter", 1) then
        Door3x3Press()
    end
end

puzzle3x3DoorSteps[5] = function()
    sh.event_wait_timer(2457, false) -- 0.6 s
    sh.event_bg_texture_cmd(2, 0, false)
end

puzzle3x3DoorSteps[6] = function()
    sh.sd_call("door_seal_mech")
    if not sh.flag_get("M7S01_Door3x3AExamining") then
        sh.flag_set("M7S01_Door3x3BUnlocked", true)
    else
        sh.flag_set("M7S01_Door3x3AUnlocked", true)
    end
    sh.event_step_increment(0)
    return puzzle3x3DoorSteps[7]()
end

puzzle3x3DoorSteps[7] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.event_wait_timer(2048, false) -- 0.5 s
end

puzzle3x3DoorSteps[8] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.event_fade_cmd(2, true, 0, 0, false)
end

puzzle3x3DoorSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.flag_set("M7S01_Door3x3AExamining", false)
    sh.flag_set("M7S01_Door3x3BExamining", false)
end

function MapEvent_Puzzle3x3Door()
    local s = sh.event_step_get(0)
    if s >= 4 and s <= 8 then
        Door3x3LampsDraw()
    end

    runStep(puzzle3x3DoorSteps, s)
end

-- index 40: newspaper in the plant room (NEWSP3.TIM).
function MapEvent_NewspaperRead()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_bg_texture_cmd(0, "TIM/NEWSP3.TIM", false) -- start loading
        sh.sfx_3d_at("newspaper_rustle", 413184, -4096, -81920, 128)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_fade_cmd(2, true, 0, 0, false) -- Auto, out
    elseif step == 2 then
        sh.event_bg_texture_cmd(1, 0, false) -- wait for loading
    elseif step == 3 then
        sh.event_fade_cmd(2, false, 0, 0, false) -- Auto, in
        sh.event_bg_texture_cmd(2, 0, false) -- draw
    elseif step == 4 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_msg(false, "map7_s02.same_thing", 0, 0, 0, false)
    elseif step == 5 then
        sh.background_gamma(48) -- 6/32
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_msg(false, "map7_s02.investigation_stalled_ptv", 0, 0, 0, false)
    elseif step == 6 or step == 7 then
        if step == 6 then
            sh.sound.sd_call("newspaper_rustle")
            sh.event_step_increment(0)
            step = 7
        end
        sh.background_gamma(48)
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_msg(false, "map7_s02.are_there_others_its_pretty", 0, 0, 0, false)
    elseif step == 8 then
        sh.background_gamma(48)
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_msg(false, "map7_s02.the_date_of_the_blaze", 0, 0, 0, false)
    elseif step == 9 then
        sh.background_gamma(48)
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_fade_cmd(2, true, 0, 0, false)
    else
        sh.player_freeze(false)
        sh.state_set(0)
        sh.event_fade_cmd(0, false, 0, 0, false) -- Start
    end
end

-- index 41: book by the plant (PLANTBK.TIM, msg map7_s02.theres_a_place_mark_in/23).
function MapEvent_PlantBookRead()
    if sh.event_step_get(1) == 0 then
        sh.sfx_3d_at("plant_book_page", 400385, -4096, -78643, 128)
    end
    sh.event_msg_with_texture("TIM/PLANTBK.TIM", 0, 0, "map7_s02.theres_a_place_mark_in", "map7_s02.white_claudia_perennial_herb")
end

-- index 42: the hospital elevator button panel, this map's variant (ELEVAT_3.TIM, five buttons,
-- the flags one higher than the hospital's). Cursor and elevator state live in the map
-- state (elevator_*); the mouse cursor writes them directly. The six shaft doors are
-- world objects.
local ELEV_X, ELEV_Y, ELEV_IDX, ELEV_DOOR = "elevator_cursor_x", "elevator_cursor_y",
    "elevator_selected_floor", "elevator_door_speed"

local function ElevTDiv(a, b) -- integer division truncating toward zero
    local q = a // b
    if q < 0 and q * b ~= a then
        q = q + 1
    end
    return q
end

local function ElevS8(v)
    v = v & 0xFF
    if v >= 0x80 then v = v - 0x100 end
    return v
end

local function ElevS16(v)
    v = v & 0xFFFF
    if v >= 0x8000 then v = v - 0x10000 end
    return v
end

local function ElevGet(name, i) return sh.map.get(name, i or 0) end
local function ElevSet(name, v, i) sh.map.set(name, v, i or 0) end
local function ElevButton(i, j) return sh.map.get(j == 0 and "elevator_buttons.x" or "elevator_buttons.y", i) end

local function ElevPos(name)
    return sh.map.get(name .. ".x"), sh.map.get(name .. ".y"), sh.map.get(name .. ".z")
end

local function ElevHumSfx(vol)
    local x, y, z = ElevPos("elevator_hum_sfx_pos")
    sh.sfx_falloff_pitch_play("elevator_hum_loop", x, y, z, vol, 65536, 0) -- 16.0
end

local function ElevFlagsSfx(sfxId, sym)
    local x, y, z = ElevPos(sym)
    sh.sfx_flags_play(sfxId, x, y, z, 128, 0) -- volume 0.5, no flags
end

local function ElevDoorsMove(first, last, speed)
    local dt = sh.delta_time_get()
    for i = first, last do
        local v = (i & 1) ~= 0 and speed or -speed
        sh.map.set("sliding_doors.pos.z", sh.map.get("sliding_doors.pos.z", i) + (dt * v) // 4096, i)
    end
end

-- The shaft doors speed up by 0.8 per second up to 0.35; the speed is kept as a 16-bit value.
local function ElevDoorSpeed(idx)
    local v = math.min((sh.delta_time_get() * 3276) // 4096 + ElevGet(ELEV_DOOR, idx), 1433)
    ElevSet(ELEV_DOOR, ElevS16(v), idx)
    return ElevS16(v)
end

-- The hospital elevator flags as in map3: ElevatorFlags[1 + f] Harry boarded at floor f
-- (basement, 1F, 2F, 3F), [5 + f] the pressed button of floor f, [9 + f] arrived at floor f.
local ElevatorFlags = {"HospitalElevatorBoardedAtBasement", "HospitalElevatorBoardedAt1F", "HospitalElevatorBoardedAt2F", "HospitalElevatorBoardedAt3F", "HospitalElevatorGoingToBasement", "HospitalElevatorGoingTo1F", "HospitalElevatorGoingTo2F", "HospitalElevatorGoingTo3F", "HospitalElevatorArrivedAtBasement", "HospitalElevatorArrivedAt1F", "HospitalElevatorArrivedAt2F", "HospitalElevatorArrivedAt3F"}

function ElevatorPanel()
    local step0 = sh.event_step_get(0)
    if sh.input_action("skip", 1) and step0 >= 9 and step0 < 17 then
        sh.event.step_set(0, 17)
    end

    local s = sh.event_step_get(0)
    if s == 0 then
        sh.player_freeze(true)
        sh.event_fade_cmd(0, true, 0, 0, false) -- Start
        ElevSet(ELEV_Y, 0)
        ElevSet(ELEV_X, 0)
        for i = 0, 7 do
            sh.flag_set(ElevatorFlags[5 + i], false)
        end
        for i = 0, 3 do
            if sh.flag_get(ElevatorFlags[2 + i]) then -- the lit floor button
                ElevSet(ELEV_Y, (ElevButton(i + 1, 1) - 110) * 4096)
                ElevSet(ELEV_IDX, i)
            end
        end
        sh.event_step_increment(0)
        s = 1
    end
    if s == 1 then
        sh.event_bg_texture_cmd(7, "TIM/ELEVAT_3.TIM", false) -- Auto
        return
    end
    if s == 2 then
        sh.event_fade_cmd(1, true, 0, 0, false) -- Wait
        return
    end
    if s == 3 then
        sh.event_bg_texture_cmd(2, 0, false) -- Draw
        sh.event_fade_cmd(2, false, 0, 0, false) -- Auto
        return
    end
    if s == 4 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.mouse_cursor_puzzle_override(ELEV_X, ELEV_Y)
        -- Stick moves the cursor at a 30 fps-based rate (16384 / 75 per tick of 1/30 s).
        local dt = sh.delta_time_get()
        local cx = ElevGet(ELEV_X) + ElevTDiv(ElevTDiv(sh.input_axis(1, 0) * 16384, 75) * dt, 136)
        cx = math.max(math.min(cx, 278528), -286720) -- clamp to -70 .. 68
        ElevSet(ELEV_X, cx)
        local cy = ElevGet(ELEV_Y) + ElevTDiv(ElevTDiv(sh.input_axis(1, 1) * 16384, 75) * dt, 136)
        cy = math.max(math.min(cy, 450560), -450560) -- clamp to -110 .. 110
        ElevSet(ELEV_Y, cy)

        sh.game_timer_update()

        local cursorX = ElevS16(cx // 4096 + 8) -- screen pixels + 8
        sh.cursor_draw(cursorX, cy // 4096 + 8, 8, 8, 0, 64, 32, 32, 128, 192, 0, 12)

        if sh.input_action("cancel", 1) then
            sh.event.step_set(0, 6)
            return
        end
        if not sh.input_action("enter", 1) then
            return
        end

        for i = 0, 5 - 1 do
            local temp = ElevButton(i, 0)
            local fx = ElevGet(ELEV_X) // 4096
            local fy = ElevGet(ELEV_Y) // 4096
            local hit = true
            if (temp - 160) > fx then
                hit = false
            elseif i == 0 then
                if not ((temp - 140) >= fx) then
                    hit = false
                end
            elseif (temp - 141) < fx then
                hit = false
            end
            if hit and ((ElevButton(i, 1) - 120) > fy or (ElevButton(i, 1) - 100) < fy) then
                hit = false
            end
            if hit then
                sh.sd_call("elevator_button")
                if i == 4 then -- the "check" button does nothing on this panel
                elseif i <= 0 or not sh.flag_get(ElevatorFlags[1 + i]) then
                    if i ~= 0 then
                        sh.flag_set(ElevatorFlags[5 + i], true) -- the pressed floor button
                        ElevSet(ELEV_X, 1)
                        ElevSet(ELEV_IDX, ElevS8(math.abs((ElevGet(ELEV_IDX) + 1) - i)))
                        sh.event.step_set(0, 5)
                    else
                        for j = 0, 3 do
                            if sh.flag_get(ElevatorFlags[1 + j]) then
                                sh.flag_set(ElevatorFlags[5 + j], true)
                                break
                            end
                        end
                        ElevSet(ELEV_X, 0)
                        sh.event.step_set(0, 5)
                    end
                end
            end
        end
        return
    end
    if s == 5 then
        sh.event_bg_texture_cmd(2, 0, false)
        for i = 0, 3 do
            sh.flag_set(ElevatorFlags[1 + i], false)
        end
        ElevSet(ELEV_DOOR, 0, 1)
        ElevSet(ELEV_DOOR, 0, 0)
        sh.player_rotate(-1024) -- -90 deg
        local _, py, _ = sh.player_pos()
        sh.player_set_pos(-417382, py, -246988) -- (-101.9, -60.3) m
        sh.event.step_set(0, 8)
        return
    end
    if s == 6 then
        sh.event_bg_texture_cmd(2, 0, false)
        sh.event_fade_cmd(2, true, 0, 0, false)
        return
    end
    if s == 7 then
        sh.player_freeze(false, false)
        sh.state_set(0)
        sh.event_fade_cmd(0, false, 0, 0, false)
        return
    end
    if s == 8 then
        if ElevGet(ELEV_X) ~= 0 then
            sh.sd_call("elevator_hum_loop")
        end
        sh.event_step_increment(0)
        s = 9
    end
    if s == 9 then
        sh.event_bg_texture_cmd(2, 0, false)
        if ElevGet(ELEV_X) ~= 0 then
            local f = sh.state_timer_get() + sh.delta_time_get()
            if f > 4096 then
                f = 4096
            end
            sh.state_timer_set(f)
            ElevHumSfx(f >> 5)
            sh.event_fade_cmd(2, true, 0, 4096, false) -- 1.0
        else
            sh.event_fade_cmd(2, true, 0, 8192, false) -- 2.0
        end
        return
    end
    if s == 10 then
        if ElevGet(ELEV_X) ~= 0 then
            sh.event_fade_cmd(2, false, 0, 4096, false)
            ElevHumSfx(128)
        else
            sh.event_fade_cmd(2, false, 0, 8192, false)
            if sh.event_step_get(0) ~= 10 then
                ElevFlagsSfx("load_screen_open", "elevator_door_sfx_pos")
                sh.event.step_set(0, 14)
            end
        end
        return
    end
    if s == 11 then
        if sh.state_timer_get() > (ElevGet(ELEV_IDX) - 1) * 3072 then -- 0.75
            sh.event_step_increment(0)
        else
            sh.state_timer_set(sh.state_timer_get() + sh.delta_time_get())
        end
        return
    end
    if s == 12 then
        ElevHumSfx(128 - (sh.state_timer_get() >> 5))
        sh.state_timer_set(sh.state_timer_get() + sh.delta_time_get())
        if sh.state_timer_get() > 2048 then -- 0.5
            ElevFlagsSfx("elevator_door", "elevator_door_sfx_pos")
            sh.event_step_increment(0)
        end
        return
    end
    if s == 13 then
        ElevHumSfx(64 - (sh.state_timer_get() >> 5)) -- 0.25
        sh.state_timer_set(sh.state_timer_get() + sh.delta_time_get())
        if sh.state_timer_get() > 2048 then
            sh.sd_call("load_screen_open_2")
            ElevFlagsSfx("load_screen_open", "elevator_door_sfx_pos")
            sh.sfx_stop("elevator_hum_loop")
            sh.event_step_increment(0)
        end
        return
    end
    if s == 14 then
        local vx = ElevDoorSpeed(0)
        ElevDoorsMove(0, 3, vx)
        sh.event_wait_timer(1228, false) -- 0.3 s
        return
    end
    if s == 15 or s == 16 then
        local vx = ElevDoorSpeed(0)
        local vy = ElevDoorSpeed(1)
        ElevDoorsMove(0, 3, vx)
        ElevDoorsMove(4, 5, vy)
        if s == 15 then
            sh.event_wait_timer(1228, false)
        else
            sh.event_fade_cmd(2, true, 0, 6144, false) -- 1.5
        end
        return
    end
    if s == 17 then
        sh.event_fade_cmd(2, true, 0, 0, false)
        return
    end
    -- finish
    sh.player_freeze(false, false)
    sh.state_set(0)

    sh.sfx_stop("elevator_hum_loop")
    sh.sfx_stop("load_screen_open")
    sh.sfx_stop("elevator_door")

    for i = 5, 0, -1 do
        sh.map.set("sliding_doors.pos.z", -249446, i) -- -60.9 m
    end
end

-- index 43: message over a background picture.
function MapEvent_AlessaPhotoInspect()
    sh.event_msg_with_bg("TIM/ALESSA.TIM", 0, 0, "map7_s02.alessa")
end

-- index 44: the last door: seal stones (flags 564-568 / 540-545).
local DOOR_SFX_X, DOOR_SFX_Y, DOOR_SFX_Z = 245760, -4915, -393216 -- (60.0, -1.2, -96.0) m

-- The last door: a seal stone placed in slot i (0..4).
local SealPlacedFlags = {[0] = "M7S02_LastDoorAmuletOfSolomonPlaced", "M7S02_LastDoorCrestOfMercuryPlaced", "M7S02_LastDoorAnkhPlaced", "M7S02_LastDoorDaggerOfMelchiorPlaced", "M7S02_LastDoorDiskOfOuroborosPlaced"}

-- The last door: seal stone i (0..4) is ready to be placed.
local SealStoneFlags = {[0] = "M7S02_LastDoorPlacingAmuletOfSolomon", "M7S02_LastDoorPlacingCrestOfMercury", "M7S02_LastDoorPlacingAnkh", "M7S02_LastDoorPlacingDaggerOfMelchior", "M7S02_LastDoorPlacingDiskOfOuroboros"}
-- The message for the seal stone placed in slot i (0..4).
local SealPlacedMsgs = {[0] = "map7_s02.used_the_amulet_of_solomon", "map7_s02.used_the_crest_of_mercury", "map7_s02.used_the_ankh", "map7_s02.used_the_dagger_of_melchior", "map7_s02.used_the_disk_of_ouroboros"}

local lastDoorSealsSteps = {}

lastDoorSealsSteps[0] = function()
    sh.player_freeze(true)
    sh.event_step_increment(0)
    return lastDoorSealsSteps[1]()
end

lastDoorSealsSteps[1] = function()
    sh.event.wait_player_stop()
end

lastDoorSealsSteps[2] = function()
    local i = 0
    while i < 5 do
        if sh.flag_get(SealStoneFlags[i]) then break end
        i = i + 1
    end

    if i ~= 5 then
        sh.map.set("last_door_seal_idx", i)
        sh.flag_set(SealPlacedFlags[sh.map.get("last_door_seal_idx")], true)

        i = 0
        while i < 5 do
            if not sh.flag_get(SealPlacedFlags[i]) then break end
            i = i + 1
        end

        if i == 5 then
            sh.flag_set("M7S02_LastDoorUnsealed", true)
        end

        local bx, by, bz = sh.npc.pos_x(0), sh.npc.pos_y(0), sh.npc.pos_z(0)
        sh.npc.set_pos_x(0, 413696)  -- 101.0 m
        sh.npc.set_pos_z(0, -404684) -- -98.8 m
        sh.load.close_range_chunks()
        sh.npc.set_pos_x(0, bx)
        sh.npc.set_pos_y(0, by)
        sh.npc.set_pos_z(0, bz)

        sh.event.step_set(0, 3)
    else
        sh.event.step_set(0, 7)
    end
end

lastDoorSealsSteps[3] = function()
    sh.msg.show(false, SealPlacedMsgs[sh.map.get("last_door_seal_idx")], 0, 0, 0, false)
end

lastDoorSealsSteps[4] = function()
    sh.sfx_flags_play("door_seal", DOOR_SFX_X, DOOR_SFX_Y, DOOR_SFX_Z, 128, 0)
    if not sh.flag_get("M7S02_LastDoorUnsealed") then
        sh.flag_set(SealStoneFlags[sh.map.get("last_door_seal_idx")], false)
        sh.player_freeze(false, false)
        sh.state_set(0)
    else
        sh.event_step_increment(0)
    end
end

lastDoorSealsSteps[5] = function()
    sh.event_wait_timer(2867, false) -- 0.7 s
end

lastDoorSealsSteps[6] = function()
    sh.sfx_flags_play("door_seal_mech", DOOR_SFX_X, DOOR_SFX_Y, DOOR_SFX_Z, 128, 0)
    sh.player_freeze(false, false)
    sh.state_set(0)
end

lastDoorSealsSteps[7] = function()
    sh.sfx_flags_play("door_locked", DOOR_SFX_X, DOOR_SFX_Y, DOOR_SFX_Z, 128, 0) -- door locked sound
    sh.event_step_increment(0)
    return lastDoorSealsSteps[8]()
end

lastDoorSealsSteps[8] = function()
    sh.msg.show(false, "common.locked", 0, 0, 0, false)
end

lastDoorSealsSteps[9] = function()
    local i = 0
    while i < 5 do
        if sh.flag_get(SealPlacedFlags[i]) then break end
        i = i + 1
    end
    if i ~= 5 then
        sh.player_freeze(false, false)
        sh.state_set(0)
    else
        sh.event_step_increment(0)
    end
end

lastDoorSealsSteps[10] = function()
    sh.event_bg_texture_cmd(0, "TIM/LASTDOOR.TIM", false) -- start loading, LASTDOOR.TIM
    sh.event_step_increment(0)
    return lastDoorSealsSteps[11]()
end

lastDoorSealsSteps[11] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

lastDoorSealsSteps[12] = function()
    sh.event_bg_texture_cmd(1, 0, false)
end

lastDoorSealsSteps[13] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.event_fade_cmd(2, false, 0, 0, false)
end

lastDoorSealsSteps[14] = function()
    sh.msg.show(false, "map7_s02.thou_possess_them_to_guard", 0, 0, 0, false)
    sh.event_bg_texture_cmd(2, 0, false)
end

lastDoorSealsSteps[15] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.event_fade_cmd(2, true, 0, 0, false)
end

lastDoorSealsSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.event_fade_cmd(0, false, 0, 0, false)
end

function MapEvent_LastDoorSeals()
    sh.screen.set_bg_gamma(104) -- 13 / 32

    local s = sh.event_step_get(0)
    runStep(lastDoorSealsSteps, s)
end

-- index 45: ALERTDOR.TIM door keypad (cursor, 5 presses).
local function idiv(a, b)
    local q = a // b
    if q < 0 and q * b ~= a then q = q + 1 end
    return q
end

-- puzzle_cursor_x/y: the cursor position; the stick moves it at a rate tuned for 30 fps (136 = 1/30 s).
local function CursorAxisUpdate(var, axis)
    local v = sh.map.get(var)
    v = v + idiv(idiv(sh.input_axis(1, axis) * 16384, 75) * sh.delta_time_get(), 136)
    v = math.max(math.min(v, 491520), -491520) -- -120..120
    sh.map.set(var, v)
end

local alertDoorKeypadSteps = {}

alertDoorKeypadSteps[0] = function()
    sh.player_freeze(true)
    sh.event_fade_cmd(0, true, 0, 0, false)
    sh.map.set("puzzle_cursor_y", 0)
    sh.map.set("puzzle_cursor_x", 0)
    for i = 0, 4 do
        sh.map.set("keypad_entered_letters", 26, i)
    end
    sh.event_step_increment(0)
    return alertDoorKeypadSteps[1]()
end

alertDoorKeypadSteps[1] = function()
    sh.event_bg_texture_cmd(7, "TIM/ALERTDOR.TIM", false) -- Auto, ALERTDOR.TIM
end

alertDoorKeypadSteps[2] = function()
    sh.event_fade_cmd(1, true, 0, 0, false) -- Wait
end

alertDoorKeypadSteps[3] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
    sh.event_bg_texture_cmd(2, 0, false)
end

alertDoorKeypadSteps[4] = function()
    sh.event_bg_texture_cmd(2, 0, false)

    sh.screen.puzzle_cursor("puzzle_cursor_x", "puzzle_cursor_y")
    CursorAxisUpdate("puzzle_cursor_x", 0)
    CursorAxisUpdate("puzzle_cursor_y", 1)

    sh.game.timer_update()

    local cx = sh.map.get("puzzle_cursor_x") // 4096 -- whole pixels
    local cy = sh.map.get("puzzle_cursor_y") // 4096
    sh.screen.cursor_draw(cx + 8, cy + 8, 8, 8, 0, 64, 32, 32, 128, 192, 0, 12)

    if sh.input_action("cancel", 1) then
        sh.event.step_set(0, 8)
        return
    end

    if sh.input_action("enter", 1) then
        for i = 0, 25 do
            local bx = sh.map.get("keypad_buttons.x", i)
            local by = sh.map.get("keypad_buttons.y", i)
            cx = sh.map.get("puzzle_cursor_x") // 4096
            cy = sh.map.get("puzzle_cursor_y") // 4096
            if not ((bx - 174) > cx or (bx - 146) < cx or (by - 134) > cy or (by - 106) < cy) then
                local dx = bx - 160 - cx
                local dy = by - 120 - cy
                if dx * dx + dy * dy < 197 then
                    sh.sd_call("door_buzzer")
                    for j = 0, 3 do
                        sh.map.set("keypad_entered_letters", sh.map.get("keypad_entered_letters", j + 1), j)
                    end
                    sh.map.set("keypad_entered_letters", i, 4)

                    local j = 0
                    while j < 5 do
                        if sh.map.get("keypad_entered_letters", j) ~= sh.map.get("keypad_solution_letters", j) then break end
                        j = j + 1
                    end
                    if j == 5 then
                        sh.flag_set("M7S01_KeypadDoorUnlocked", true)
                        sh.event.step_set(0, 5)
                    end
                end
            end
        end
    end
end

alertDoorKeypadSteps[5] = function()
    sh.event_wait_timer(2457, false) -- 0.6 s
    sh.event_bg_texture_cmd(2, 0, false)
end

alertDoorKeypadSteps[6] = function()
    sh.sd_call("door_seal_mech")
    sh.event_step_increment(0)
    return alertDoorKeypadSteps[7]()
end

alertDoorKeypadSteps[7] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.event_wait_timer(2457, false)
end

alertDoorKeypadSteps[8] = function()
    sh.event_bg_texture_cmd(2, 0, false)
    sh.event_fade_cmd(2, true, 0, 0, false)
end

alertDoorKeypadSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.event_fade_cmd(0, false, 0, 0, false)
end

function MapEvent_AlertDoorKeypad()
    local s = sh.event_step_get(0)
    runStep(alertDoorKeypadSteps, s)
end
