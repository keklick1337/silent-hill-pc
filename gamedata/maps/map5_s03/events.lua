-- map5_s03: Norman's Motel (Resort Area)
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
        [37] = {"HealthDrink","M5S03_HealthDrink0"},
        [38] = {"ShotgunShells","M5S03_ShotgunShells"},
        [39] = {"HealthDrink","M5S03_HealthDrink1"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point gives a first aid kit and sets no flag
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

local motorcycleKeyTakeSteps = {}

motorcycleKeyTakeSteps[0] = function()
    sh.player_freeze(true)
    sh.event.inv_item(0, "MotorcycleKey", 0, false) -- QueueLoad, motorcycle key
    sh.event_fade_cmd(0, true, 2, 0, false)
    -- Warp player.
    sh.npc.set_pos_x(0, 424509) -- 103.64 m
    sh.npc.set_pos_z(0, 243671) -- 59.49 m
    sh.npc.set_rot_y(0, 580) -- 51.0 deg
    -- Warp camera.
    sh.camera.position(nil, 424099, -11919, 242483, 0, 0, 0, 0, true) -- (103.54, -2.91, 59.2)
    sh.camera.look_at(nil, 428646, 2949, 247562, 0, 0, 0, 0, true) -- (104.65, 0.72, 60.44)
    sh.event_step_increment(0)
    return motorcycleKeyTakeSteps[1]()
end

motorcycleKeyTakeSteps[1] = function()
    sh.event.wait_player_stop()
end

motorcycleKeyTakeSteps[2] = function()
    sh.event_chara_anim(0, 0, "motorcycle_key_take", false)
    sh.event_step_increment(0)
    return motorcycleKeyTakeSteps[3]()
end

motorcycleKeyTakeSteps[3] = function()
    sh.event_wait_timer(15564, false) -- 3.8 s
end

motorcycleKeyTakeSteps[4] = function()
    sh.event_chara_anim(2, 0, 0, false) -- AnimLock
    sh.sfx_flags_play("key_metal_clink", 427008, 409, 245760, 128, 0) -- sound key_metal_clink at (104.25, 0.1, 60.0)
    sh.event_step_increment(0)
    return motorcycleKeyTakeSteps[5]()
end

motorcycleKeyTakeSteps[5] = function()
    sh.event.inv_item(1, "MotorcycleKey", 0, false) -- AwaitLoad
end

motorcycleKeyTakeSteps[6] = function()
    if sh.screen.pickup_animate("MotorcycleKey") ~= 0 then
        sh.msg.show(false, "map5_s03.ive_got_a_motorcycle_key", 0, 0, 0, false)
        if sh.event_step_get(0) ~= 6 then
            sh.sd_call("menu_confirm") -- menu confirm sound
        end
    end
end

motorcycleKeyTakeSteps[7] = function()
    sh.npc.anim_unlock_play_to_end(0)
end

motorcycleKeyTakeSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.camera.auto_return(true)
    sh.player.item_remove("Magnet", 1)
    sh.event.inv_item(3, "MotorcycleKey", 1, false) -- AddItem
    sh.flag_set("M5S03_PickupMotorcycleKey", true)
    sh.event_fade_cmd(0, false, 2, 0, false)
end

-- index 4: motorcycle key from the tank: magnet, pickup with animation.
function MotorcycleKeyTake()
    local s = sh.event_step_get(0)
    runStep(motorcycleKeyTakeSteps, s)
end

local motelKeyInspectSteps = {}

motelKeyInspectSteps[0] = function()
    sh.player_freeze(true)
    sh.event.bg_texture(0, "TIM/MOTELKEY.TIM", false) -- QueueRead MOTELKEY.TIM
    sh.event_step_increment(0)
    return motelKeyInspectSteps[1]()
end

motelKeyInspectSteps[1] = function()
    sh.event.wait_player_stop()
end

motelKeyInspectSteps[2] = function()
    sh.npc.anim_play_to_end(0, "pickup_crouch")
end

motelKeyInspectSteps[3] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

motelKeyInspectSteps[4] = function()
    sh.event.bg_texture(1, 0, false) -- AwaitLoad
end

motelKeyInspectSteps[5] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
    sh.event.bg_texture(2, 0, false) -- Draw
end

motelKeyInspectSteps[6] = function()
    sh.msg.show(false, "map5_s03.theres_a_crack_something", 0, 0, 0, false)
    sh.event.bg_texture(2, 0, false)
end

motelKeyInspectSteps[7] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
    sh.event.bg_texture(2, 0, false)
end

motelKeyInspectSteps[8] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
end

motelKeyInspectSteps[9] = function()
    sh.npc.anim_play_to_end(0, "pickup_stand_up")
end

motelKeyInspectSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
end

-- index 5: inspecting the motel key (MOTELKEY.TIM, msg map5_s03.theres_a_crack_something).
function MotelKeyInspect()
    local s = sh.event_step_get(0)
    runStep(motelKeyInspectSteps, s)
end

-- index 6: message over a background picture, then a flag is set.
function MapEvent_MotelDiaryInspect()
    sh.event_msg_with_bg("TIM/DIARYMTL.TIM", 10240, 8192, "map5_s03.september_10") -- 2.5, 2.0
    sh.flag_set("M5S03_MotelDiaryRead", true)
end

-- index 7: item pickup.
function MapEvent_MagnetTake()
    sh.event_item_take("Magnet", 1, "M5S03_PickupMagnet", "map5_s03.there_is_a_magnet_take")
end

-- index 8: garage cutscene GARAGE1.DMS: motorcycle key, Aglaophotis vial, Kaufmann.
-- garage_msg_audio_idx is the voice line index, garage_msg_voice_cmds
-- the voice commands, bicitem_tex_load_id the BICITEM.TIM read queue entry.
local function GarageSfx0() sh.sfx_flags_play("garage_ambient", 421888, -4915, 72089, 128, 0) end -- sound garage_ambient at (103.0, -1.2, 17.6)

local garageKaufmannSteps = {}

garageKaufmannSteps[0] = function()
    sh.player_freeze(true)
    sh.map.set("garage_msg_audio_idx", 0)
    sh.map.set("cutscene_timer", -1) -- none
    sh.held_item_hide()
    sh.player.weapon_stash()
    sh.player.held_item_set("CutsceneAglaophotis")
    sh.map.set("bicitem_tex_load_id", sh.load.tim("TIM/BICITEM.TIM", 1, "item_inspection_img")) -- BICITEM.TIM
    sh.load.file("ANIM/GARAGE1.DMS", 17) -- GARAGE1.DMS
    sh.npc.load(0, "Kaufmann", "npc_bone:0", 255, nil, nil) -- Kaufmann, free all character slots
    sh.sfx_flags_play("garage_motorcycle_key_turn", 416587, -3641, 80971, 128, 0) -- sound garage_motorcycle_key_turn at pos (101.706, -0.889, 19.7684)
    sh.event_step_increment(0)
    return garageKaufmannSteps[1]()
end

garageKaufmannSteps[1] = function()
    sh.msg.show(false, "map5_s03.used_the_motorcycle_key", 0, 0, 0, false) -- "Used the Motorcycle Key."
end

garageKaufmannSteps[2] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

garageKaufmannSteps[3] = function()
    sh.sfx_flags_play("garage_motorcycle_tank_open", 416587, -3641, 80971, 128, 0) -- sound garage_motorcycle_tank_open at pos
    sh.event_step_increment(0)
    return garageKaufmannSteps[4]()
end

garageKaufmannSteps[4] = function()
    if sh.load.is_loaded(sh.map.get("bicitem_tex_load_id")) ~= 0 then
        sh.event_step_increment(0)
        sh.held_item_show()
    end
end

garageKaufmannSteps[5] = function()
    sh.event.bg_texture(2, 0, false)
    sh.event_fade_cmd(2, false, 0, 0, false)
end

garageKaufmannSteps[6] = function()
    sh.event.bg_texture(2, 0, false)
    sh.msg.show(false, "map5_s03.a_small_glass_vial_wrapped", 0, 0, 0, false)
    if sh.event_step_get(0) ~= 6 and not sh.flag_get("M3S01_SeenShatteredVial") then
        -- skip the next step unless flag M3S01_SeenShatteredVial is set
        sh.event.step_set(0, 8)
    end
end

garageKaufmannSteps[7] = function()
    sh.event.bg_texture(2, 0, false)
    sh.msg.show(false, "map5_s03.its_like_the_busted_vial", 0, 0, 0, false)
end

garageKaufmannSteps[8] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
    sh.event.bg_texture(2, 0, false)
end

garageKaufmannSteps[9] = function()
    sh.npc.process_loads()
    sh.cutscene.prepare(17)
    sh.event_step_increment(0)
    return garageKaufmannSteps[10]()
end

garageKaufmannSteps[10] = function()
    sh.sound.bgm_crossfade(34) -- music track 34
end

garageKaufmannSteps[11] = function()
    sh.cutscene.border_show()
    sh.cutscene_flag(true)
    sh.water_effect_enable()
    sh.light.set_intensity(4096)
    sh.light.bone_detach()
    sh.light.lens_flare_detach()
    sh.map.set("cutscene_timer", 0)
    sh.event_chara_anim(0, 0, "scene_garage_kaufmann_1", false)
    sh.event_step_increment(0)
end

garageKaufmannSteps[12] = function()
    sh.event_fade_cmd(2, false, 0, 0, false)
    sh.event_chara_anim(2, 0, 0, false) -- AnimLock
end

garageKaufmannSteps[13] = function()
    sh.npc.spawn("Kaufmann", 0, 575488, 94208, 0, 3) -- Kaufmann at (140.5, 23.0)
    GarageSfx0()
    sh.event_chara_anim(3, 0, 0, false) -- AnimUnlock
    sh.flag_set("M5S03_GarageKaufmannPresent", true)
    sh.event_step_increment(0)
    return garageKaufmannSteps[14]()
end

garageKaufmannSteps[14] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 0, 69632, true, true) -- 10, 0, 17
    sh.npc.anim_flags_clear(1, 2)
end

garageKaufmannSteps[15] = function()
    sh.npc.anim_play_to_end(1, "burst_in")
    sh.npc.anim_flags_set(1, 2)
    sh.cutscene.timer_advance("cutscene_timer", 40960, 73728, 155648, true, false) -- 10, 18, 38
end

garageKaufmannSteps[16] = function()
    sh.msg.show_with_audio("map5_s03.give_me_that", "garage_msg_audio_idx", "garage_msg_voice_cmds[0]")
    sh.cutscene.timer_advance("cutscene_timer", 40960, 73728, 155648, true, false)
end

garageKaufmannSteps[17] = function()
    sh.event_chara_anim(0, 0, "scene_garage_kaufmann_2", false)
    sh.event_chara_anim(0, 1, "snatch_item", false)
    sh.event_step_increment(0)
    return garageKaufmannSteps[18]()
end

garageKaufmannSteps[18] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 159744, 307200, true, true) -- 10, 39, 75
end

garageKaufmannSteps[19] = function()
    sh.event_chara_anim(0, 0, "cutscene_stand", false)
    sh.event_chara_anim(0, 1, "stand_talk", false)
    sh.held_item_hide()
    sh.map.set("cutscene_timer", 311296) -- frame 76.0
    sh.event_step_increment(0)
    return garageKaufmannSteps[20]()
end

garageKaufmannSteps[20] = function()
    sh.msg.show_with_audio("map5_s03.thats_none_of_your_business", "garage_msg_audio_idx", "garage_msg_voice_cmds[0]")
end

garageKaufmannSteps[21] = function()
    sh.msg.show_with_audio("map5_s03.you_shouldnt_be_hanging_around", "garage_msg_audio_idx", "garage_msg_voice_cmds[0]")
    sh.map.set("cutscene_timer", 315392) -- frame 77.0
end

garageKaufmannSteps[22] = function()
    sh.map.set("cutscene_timer", 319488) -- frame 78.0
    sh.msg.show_with_audio("map5_s03.unless_you_want_to_die", "garage_msg_audio_idx", "garage_msg_voice_cmds[0]")
end

garageKaufmannSteps[23] = function()
    sh.event_chara_anim(0, 0, "cutscene_react", false)
    sh.event_step_increment(0)
    return garageKaufmannSteps[24]()
end

garageKaufmannSteps[24] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 319488, 401408, true, true) -- 10, 78, 98
end

garageKaufmannSteps[25] = function()
    sh.event_chara_anim(0, 1, "walk", false)
    sh.player.weapon_restore()
    sh.player.held_item_prev()
    sh.player.weapon_stash()
    sh.event_step_increment(0)
    return garageKaufmannSteps[26]()
end

garageKaufmannSteps[26] = function()
    sh.cutscene.timer_advance("cutscene_timer", 40960, 405504, 495616, true, true) -- 10, 99, 121
end

garageKaufmannSteps[27] = function()
    sh.npc.clear(1, 0, 0)
    GarageSfx0()
    sh.flag_set("M5S03_GarageKaufmannPresent", false)
    sh.map.set("cutscene_timer", 499712) -- frame 122.0
    sh.event_step_increment(0)
    return garageKaufmannSteps[28]()
end

garageKaufmannSteps[28] = function()
    if sh.flag_get("M5S02_SafeOpened") and sh.flag_get("M5S02_DiaryRead") and sh.flag_get("M5S03_MotelDiaryRead") then
        sh.msg.show(false, "map5_s03.that_guys_gotta_be_involved", 0, 0, 0, false)
    else
        sh.msg.show(false, "map5_s03.man_was_he_pissed_and_2", 0, 0, 0, false)
    end
end

garageKaufmannSteps[29] = function()
    sh.camera.auto_return(true)
    sh.map.set("cutscene_timer", -1)
    sh.player.weapon_restore()
    sh.held_item_show()
    sh.water_effect_disable()
    sh.light.flashlight_fix()
    sh.light.set_intensity(4096)
    sh.event_step_increment(0)
    return garageKaufmannSteps[30]()
end

garageKaufmannSteps[30] = function()
    sh.msg.show(false, "map5_s03.i_guess_i_wasted_my", 0, 0, 0, false)
end

garageKaufmannSteps[31] = function()
    sh.event_step_reset()
end

garageKaufmannSteps[32] = function()
    sh.event_fade_cmd(2, true, 0, 0, false)
end

garageKaufmannSteps[33] = function()
    sh.player.weapon_restore()
    sh.player.held_item_prev()
    sh.npc.clear(1, 0, 0)
    sh.load.wait()
    sh.camera.auto_return(true)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.map.set("cutscene_timer", 499712)
    sh.event_step_increment(0)
end

garageKaufmannSteps.default = function()
    sh.player_freeze(false, false)
    sh.state_set(0)
    sh.camera.auto_return(true)
    sh.flag_set("M5S03_KaufmannHasMotorcycleVial", true)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.map.set("cutscene_timer", -1)
    sh.water_effect_disable()
    sh.light.flashlight_fix()
    sh.light.set_intensity(4096)
    sh.sd_call("stop_xa_audio")
    sh.player.weapon_restore()
    sh.held_item_show()
end

function Cutscene_GarageKaufmann()
    local s = sh.event_step_get(0)
    if sh.input_action("skip", 1) and s >= 14 and s < 29 then
        sh.event.step_set(0, 32)
        s = 32
    end
    runStep(garageKaufmannSteps, s)

    local t = sh.map.get("cutscene_timer")
    if t >= 0 then
        sh.cutscene.chara(0, "HERO", t, 17)
        sh.cutscene.chara(1, "KAU", t, 17)
        sh.cutscene.camera(t, 17, "camera_target_pos", "camera_target_look")
        sh.cutscene.light(t, 17)
    end
end

-- index 9: newspaper NEWSP2.TIM (msg map5_s03.newspaper/48) over a dimmed background.
-- The US release never sets flag 393 here, so the newspaper in Nowhere never appears; it is set as the PAL and later
-- Japanese releases do.
function SecondNewspaperRead()
    sh.msg.show_dimmed(1917, 0, 0, "map5_s03.newspaper", "map5_s03.investigation_stalled_ptv") -- NEWSP2.TIM
    sh.flag_set("M5S03_SeenSecondNewspaper", true) -- flag: seen second newspaper
end

-- index 10: pushing the shelf: Yes/No question, warp, anim 105, the shelf world object and
-- the player move forward, flag M5S03_ShelfPushed.
-- "shelf" — the shelf's world object placement (map state, "shelf.pos.z");
-- shelf_slide_volume is the slide sound volume (4096 = 1.0).

local pushShelfSteps = {}

pushShelfSteps[0] = function()
    sh.player_freeze(true)
    sh.event_step_increment(0)
    return pushShelfSteps[1]()
end

pushShelfSteps[1] = function()
    sh.event.wait_player_stop()
end

pushShelfSteps[2] = function()
    sh.msg.show(true, "map5_s03.do_you_want_to_push", 4, 3, 0, false) -- "Do you want to push the shelf?"
end

pushShelfSteps[3] = function()
    sh.player_freeze(false, false)
    sh.state_set(0) -- back to gameplay
end

pushShelfSteps[4] = function()
    sh.event_fade_cmd(0, true, 2, 0, false) -- Start, type 2
    sh.npc.set_pos_x(0, 426680) -- 104.17 m
    sh.npc.set_pos_z(0, 243712) -- 59.5 m
    sh.npc.set_rot_y(0, 0)
    sh.camera.position(nil, 418078, -5816, 240721, 0, 0, 0, 0, true) -- 102.07, -1.42, 58.77 m
    sh.camera.look_at(nil, 430653, 1515, 248258, 0, 0, 0, 0, true)    -- 105.14, 0.37, 60.61 m
    sh.event_chara_anim(0, 0, "push_object", false)
    sh.sd_call("shelf_scrape")
    sh.event_step_increment(0)
    return pushShelfSteps[5]()
end

pushShelfSteps[5] = function()
    local dt = sh.delta_time_get()
    local moveDist = (dt * 573) // 4096 -- dt * 0.14
    sh.npc.set_pos_z(0, sh.npc.pos_z(0) + moveDist)
    sh.map.set("shelf.pos.z", sh.map.get("shelf.pos.z") + moveDist)

    if sh.event_step_get(0) == 5 then
        if sh.map.get("shelf.pos.z") > 247193 then -- 60.35 m
            sh.event.step_set(0, 6)
        end
        sh.map.set("shelf_slide_volume", math.min(sh.map.get("shelf_slide_volume") + dt * 2, 4096))
    else
        local z = sh.map.get("shelf.pos.z")
        if z > 247562 then -- 60.44 m
            moveDist = z - 247562
            sh.npc.set_pos_z(0, sh.npc.pos_z(0) - moveDist)
            sh.map.set("shelf.pos.z", z - moveDist)
        end
        local v = sh.map.get("shelf_slide_volume")
        sh.map.set("shelf_slide_volume", (v - dt * 2 >= 0) and (v - (dt & 0xFFFF) * 2) or 0)
        sh.event_fade_cmd(2, true, 0, 0, false) -- Auto
    end

    sh.sound.play_falloff("cabinet_slide", "shelf.pos",
            sh.map.get("shelf_slide_volume") // 32, 49152, 0) -- volume / 32, falloff 12.0
end

pushShelfSteps[6] = pushShelfSteps[5]

pushShelfSteps.default = function()
    sh.flag_set("M5S03_ShelfPushed", true)
    sh.event_fade_cmd(0, false, 2, 0, false)
    sh.event_fade_cmd(0, false, 0, 0, false)
    sh.player_freeze(false, true)
    sh.state_set(0)
    sh.camera_auto_return(true)
    sh.sfx_stop("shelf_scrape")
end

function PushShelf()
    local s = sh.event_step_get(0)
    runStep(pushShelfSteps, s)
end
