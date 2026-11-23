-- map2_s00: Old Silent Hill - streets
-- Event scripts of this area: the engine runs a handler every tick while its event is active.
-- Fixed-point numbers: 4096 = 1.0 (metres, seconds); angles: 4096 = 360 degrees.

function on_boot()
    -- per-map script entry point
end

local SYSFLAG_NO_ENEMY_SPAWN = 16 -- no enemy spawn system flag (bit 4)

-- The street spawn variant 0, 1 or 2: exactly one of M2S00_StreetSpawnCafe..174 is set, then the generic selector takes
-- the spawn table those flags choose (spawn_overrides.json).
local function street_spawn_variant_set(variant)
    sh.flag_set("M2S00_StreetSpawnCafe", variant == 0)
    sh.flag_set("M2S00_StreetSpawnSchool", variant == 1)
    sh.flag_set("M2S00_StreetSpawnChurch", variant == 2)
    sh.map.spawn_variants_apply()
end

-- Fixed-point world units -> metres for camera_pos/look.
local function M(q) return q / 4096 end

-- Store into a 16-bit value: truncate to signed 16 bits.
local function S16(v) return ((v + 0x8000) & 0xFFFF) - 0x8000 end

-- Setting a step also clears the state timer, the sub-steps and the step timer
-- (sh.event_step_set alone writes only the step).
local function StepSet0(v)
    sh.event_step_set(0, v)
    sh.state_timer_set(0)
    sh.event_step_set(1, 0)
    sh.sys_timer_set(0)
    sh.event_step_set(2, 0)
end

-- index 0: jammed door.
function MapEvent_DoorJammed()
    sh.event_msg_with_sfx("common.lock_jammed", "door_jammed")
end

-- index 1: locked door (some points of interest of this area play a different sound).
function MapEvent_DoorLocked()
    local poi = sh.event_point()
    if poi == 57 or poi == 58 then
        sh.event_msg_with_sfx("common.locked", "door_locked_alt")
    else
        sh.event_msg_with_sfx("common.locked", "door_locked") -- message: door locked, with the door locked sound
    end
end

-- index 2: common pickups, chosen by the event's point of interest.
function MapEvent_CommonItemTake()
    local poi = sh.event_point()
    local t = {
        [139] = {"FirstAidKit","M2S00_FirstAidKit0"},
        [140] = {"HandgunBullets","M2S00_HandgunBullets0"},
        [141] = {"HandgunBullets","M2S00_HandgunBullets1"},
        [142] = {"HealthDrink","M2S00_HealthDrink0"},
        [143] = {"HandgunBullets","M2S00_HandgunBullets2"},
        [144] = {"ShotgunShells","M2S00_ShotgunShells0"},
        [145] = {"HealthDrink","M2S00_HealthDrink1"},
        [146] = {"HandgunBullets","M2S00_HandgunBullets3"},
        [147] = {"HandgunBullets","M2S00_HandgunBullets4"},
        [148] = {"HandgunBullets","M2S00_HandgunBullets5"},
        [149] = {"HandgunBullets","M2S00_HandgunBullets6"},
        [150] = {"HandgunBullets","M2S00_HandgunBullets7"},
        [151] = {"HandgunBullets","M2S00_HandgunBullets8"},
        [152] = {"HealthDrink","M2S00_HealthDrink2"},
        [153] = {"HealthDrink","M2S00_HealthDrink3"},
        [154] = {"HealthDrink","M2S00_HealthDrink4"},
        [155] = {"HandgunBullets","M2S00_HandgunBullets9"},
        [156] = {"HealthDrink","M2S00_HealthDrink5"},
        [157] = {"HandgunBullets","M2S00_HandgunBullets10"},
        [158] = {"HealthDrink","M2S00_HealthDrink6"},
        [159] = {"HealthDrink","M2S00_HealthDrink7"},
        [160] = {"HealthDrink","M2S00_HealthDrink8"},
        [161] = {"HealthDrink","M2S00_HealthDrink9"},
        [162] = {"HealthDrink","M2S00_HealthDrink10"},
        [163] = {"HealthDrink","M2S00_HealthDrink11"},
        [164] = {"ShotgunShells","M2S00_ShotgunShells1"},
        [165] = {"FirstAidKit","M2S00_FirstAidKit1"},
        [166] = {"HandgunBullets","M2S00_HandgunBullets11"},
        [167] = {"HandgunBullets","M2S00_HandgunBullets12"},
        [168] = {"HandgunBullets","M2S00_HandgunBullets13"},
        [169] = {"HandgunBullets","M2S00_HandgunBullets14"},
        [170] = {"FirstAidKit","M2S00_FirstAidKit2"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point: first aid kit with the initial flag
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- index 3: inspect with a question (message map2_s00.my_car_too_banged_up) and take a Health Drink.
-- While any character from Harry to the Cybil monster is alive (health > 0),
-- game time stands still for this tick.
function MapEvent_HealthDrinkInspect()
    for slot = 1, 32 do -- all NPC slots
        local id = sh.chara_field_get(slot, "chara_id")
        if id >= 1 and id <= 24 and sh.chara_field_get(slot, "health") > 0 then
            sh.delta_time_set(0)
            break
        end
    end

    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_selection(false, "map2_s00.my_car_too_banged_up")
    elseif step == 2 then
        sh.event_item_take("HealthDrink", 1, "M2S00_PickupHealthDrink", "common.take_health_drink")
    end
end

-- index 5: item pickup.
function MapEvent_RockDrillTake()
    sh.event_item_take("RockDrill", 1, "M2S00_PickupRockDrill", "map2_s00.there_is_a_rock_drill_2")
end

-- index 6: item pickup.
function MapEvent_ChainsawTake()
    sh.event_item_take("Chainsaw", 1, "M2S00_PickupChainsaw", "map2_s00.there_is_a_chainsaw_take")
end

-- index 7: take the steel pipe (yes/no, message map2_s00.there_is_a_steel_pipe).
-- Load the item model -> wait for the player to stop -> bend down (59)
-- -> flag M2S00_PickupSteelPipe + pickup animation -> question (15, 5 = take / 6 = leave)
-- -> take: add the item + step 7; leave: clear flag M2S00_PickupSteelPipe -> straighten up (60).
function MapEvent_SteelPipeTake()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_inv_item_cmd(0, "SteelPipe", 0, false) -- queue the steel pipe model load
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.chara_anim_play_to_end(0, "pickup_crouch")        -- bend down for the pipe
    elseif step == 3 then
        sh.event_inv_item_cmd(1, "SteelPipe", 0, false) -- wait for the model load
    elseif step == 4 then
        sh.flag_set("M2S00_PickupSteelPipe", true)                   -- flag: steel pipe picked up
        if sh.pickup_animate("SteelPipe") then
            sh.event_selection(true, "map2_s00.there_is_a_steel_pipe", 5, 6, 0) -- take / leave
        end
    elseif step == 5 then
        sh.event_inv_item_cmd(3, "SteelPipe", 1, false) -- add the item
        StepSet0(7)
    elseif step == 6 then
        sh.flag_set("M2S00_PickupSteelPipe", false)
        sh.event_step_increment(0)
        step = 7
    end
    if step == 7 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")        -- straighten up
    elseif step > 7 then
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 11: note on the doghouse (STKENNEL.TIM = 1947).
-- The picture is shown by steps, bend down / straighten up (59/60),
-- fades, flag M2S00_DogHouseNoteFound, message map2_s00.doghouse_levin_st; at the end the doghouse note is added to the inventory.
function MapEvent_DoghouseNote()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.overlay_screen_set(0, "TIM/STKENNEL.TIM", false) -- STKENNEL.TIM
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.chara_anim_play_to_end(0, "pickup_crouch")
    elseif step == 3 then
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
    elseif step == 4 then
        sh.overlay_screen_set(1, 0, false)
    elseif step == 5 then
        sh.overlay_screen_set(2, 0, false)
        sh.flag_set("M2S00_DogHouseNoteFound", true) -- flag: doghouse note found
        sh.event_step_increment_after_fade(2, false, 0, 0, false)
    elseif step == 6 then
        sh.overlay_screen_set(2, 0, false)
        sh.event_msg(false, "map2_s00.doghouse_levin_st") -- "Doghouse Levin St."
    elseif step == 7 then
        sh.overlay_screen_set(2, 0, false)
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
    elseif step == 8 then
        sh.event_step_increment_after_fade(2, false, 0, 0, false)
    elseif step == 9 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")
    else
        sh.player_freeze(false)
        sh.state_set(0)
        sh.event_inv_item_cmd(3, "NoteDoghouse", 1, false) -- add the doghouse note
    end
end

-- index 12: the house key from the doghouse (INKENNEL.TIM = 1865).
-- Same as index 11 + a question (27, 8 = take / 9 = leave);
-- take: add the house key + flag M2S00_PickupDogHouseKey + remove the note;
-- leave: the same exit fade; then straighten up (60).
function MapEvent_DoghouseKeyTake()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.overlay_screen_set(0, "TIM/INKENNEL.TIM", false) -- INKENNEL.TIM
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.chara_anim_play_to_end(0, "pickup_crouch")
    elseif step == 3 then
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
    elseif step == 4 then
        sh.overlay_screen_set(1, 0, false)
    elseif step == 5 then
        sh.overlay_screen_set(2, 0, false)
        sh.event_step_increment_after_fade(2, false, 0, 0, false)
    elseif step == 6 then
        sh.event_step_increment(0)
        step = 7
    end
    if step == 7 then
        sh.overlay_screen_set(2, 0, false)
        sh.event_selection(true, "map2_s00.house_key_was_hidden_in", 8, 9, 0) -- "House Key in doghouse. Take it?"
    elseif step == 8 then
        sh.event_inv_item_cmd(3, "HouseKey", 1, false) -- add the house key
        sh.flag_set("M2S00_PickupDogHouseKey", true)                 -- flag: house key picked up
        sh.item_remove("NoteDoghouse", 1)                    -- remove the note
        sh.event_step_increment(0)
        step = 9
    end
    if step == 9 then
        sh.overlay_screen_set(2, 0, false)
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
    elseif step == 10 then
        sh.event_step_increment_after_fade(2, false, 0, 0, false)
    elseif step == 11 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")
    elseif step > 11 then
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 13: Key of Lion pickup.
-- Key of Lion pickup (message map2_s00.there_is_a_key_of); if the key is taken and the Finney St mark is already
-- on the map, the blank half MapMark_OldTown_FineyStRightKeyArrowBlank is set.
function MapEvent_KeyOfLionTake()
    sh.event_item_take("KeyOfLion", 1, "M2S00_PickupKeyOfLion", "map2_s00.there_is_a_key_of")
    if sh.flag_get("M2S00_PickupKeyOfLion") and
       sh.flag_get("MapMark_OldTown_FineyStRightKeyArrow") then
        sh.flag_set("MapMark_OldTown_FineyStRightKeyArrowBlank", true)
    end
end

-- index 14: take the Key of Woodman (yes/no, msg map2_s00.there_is_a_key_of_2).
-- Load the item model + pickup animation -> question (29, 6/7): the same steps as the shotgun
-- pickup (map1_s02 index 17). If the dog yard key line mark is already set, the blank
-- half MapMark_OldTown_DogYardKeyLineBlank is set.
local WOODMAN_TAKE, WOODMAN_DONT = 6, 7 -- take / don't take

function MapEvent_KeyOfWoodmanTake()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_inv_item_cmd(0, "KeyOfWoodman", 0, false)                -- queue the Key of Woodman model load
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 or step == 3 then
        if step == 2 then                                     -- continues into step 3 in the same tick
            sh.event_chara_anim(0, 0, "pickup_crouch", false)              -- start the animation: bend down
            sh.event_step_increment(0)
        end
        sh.event_inv_item_cmd(1, "KeyOfWoodman", 0, false)                -- wait for the model load
    elseif step == 4 then
        sh.event_chara_anim(1, 0, 0, false)                   -- wait for the animation to end
    elseif step == 5 then
        if sh.pickup_animate("KeyOfWoodman") then
            sh.event_selection(true, "map2_s00.there_is_a_key_of_2", WOODMAN_TAKE, WOODMAN_DONT, 0, false) -- "Key of Woodman. Take it?"
        end
        sh.flag_set("M2S00_PickupKeyOfWoodman", true)                                -- flag: Key of Woodman picked up
    elseif step == WOODMAN_TAKE then
        sh.event_inv_item_cmd(3, "KeyOfWoodman", 1, false)                -- add the item
        if sh.flag_get("MapMark_OldTown_DogYardKeyLine") then                              -- map mark: Old Town dog yard key line
            sh.flag_set("MapMark_OldTown_DogYardKeyLineBlank", true)                            -- the blank half of the dog yard mark
        end
        StepSet0(8)                                           -- step 8 from the next tick
    elseif step == WOODMAN_DONT then
        sh.flag_set("M2S00_PickupKeyOfWoodman", false)
        sh.event_step_increment(0)
        step = 8                                              -- continues into step 8 in the same tick
    end
    if step == 8 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")                      -- straighten up
    elseif step > 8 then
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- index 15: Key of Scarecrow pickup.
-- Key of Scarecrow pickup (message map2_s00.there_is_a_key_of_3); if the key is taken and the Ellroy St mark is
-- already on the map, the blank half MapMark_OldTown_EllroyStKeyCircleBlank is set.
function MapEvent_KeyOfScarecrowTake()
    sh.event_item_take("KeyOfScarecrow", 1, "M2S00_PickupKeyOfScarecrow", "map2_s00.there_is_a_key_of_3")
    if sh.flag_get("M2S00_PickupKeyOfScarecrow") and
       sh.flag_get("MapMark_OldTown_EllroyStKeyCircle") then
        sh.flag_set("MapMark_OldTown_EllroyStKeyCircleBlank", true)
    end
end

-- index 16: using the house key on the doghouse.
-- Message 31 with the key use sound, then the map shows the doghouse as a dot only.
function MapEvent_HouseKeyUse()
    sh.item_remove("HouseKey", 1) -- house key
    sh.event_msg_with_sfx("map2_s00.used_the_house_key", "use_key") -- "Used the House Key."
    sh.flag_set("MapMark_OldTown_DoghouseDotOnly", true)
end

-- index 17: the Door of Eclipse locks (three keys): the picture of the locks; a carried key
-- that fits an unopened lock is used ("Used the Key of ...", sound eclipse_door_key_used) and the
-- picture cross-fades to the one with that lock open, then the check repeats; otherwise the
-- number of remaining locks (or "It's unlocked.", door open) and the scene ends.
-- The pictures (lock_tim_files by the opened locks: Lion 4 + Woodman 2 + Scarecrow 1) take
-- turns in the two background images, as the original's three image pointers did: the one
-- shown, the one faded to, the one the next picture loads into.
local eclipseImgShown = "item_inspection_img"
local eclipseImgNext = "bg_texture_img"
local eclipseImgLoad = "item_inspection_img"
local eclipseMsg = "common.unlocked"

local function EclipseLockTimFile()
    local idx = (sh.flag_get("M2S00_LockOfLionOpen") and 4 or 0) +
                (sh.flag_get("M2S00_LockOfWoodmanOpen") and 2 or 0) +
                (sh.flag_get("M2S00_LockOfScarecrowOpen") and 1 or 0)
    return sh.map.get("lock_tim_files", idx)
end

-- Steps 0/4 of the overlay screen command load a picture into the inspection / background
-- texture image (Fs_QueueStartReadTim to file buffer 1), steps 2/5 draw that image.
local function EclipseImageLoad(file, img)
    sh.overlay_screen_set(img == "item_inspection_img" and 0 or 4, file, false)
end

local function EclipseImageDraw(img)
    sh.overlay_screen_set(img == "item_inspection_img" and 2 or 5, 0, false)
end

local function EclipseKeyUse(pickupFlag, openFlag, item, msg)
    if sh.flag_get(pickupFlag) and not sh.flag_get(openFlag) then
        sh.flag_set(openFlag, true)
        eclipseMsg = msg
        sh.player.item_remove(item, 1)
        sh.event.step_set(0, 5)
        return true
    end
    return false
end

function MapEvent_DoorOfEclipseLocks()
    local step = sh.event_step_get(0)
    if step == 0 or step == 1 then
        if step == 0 then
            sh.player_freeze(true)
            eclipseImgShown = "item_inspection_img"
            eclipseImgNext = "bg_texture_img"
            eclipseImgLoad = "item_inspection_img"
            EclipseImageLoad(EclipseLockTimFile(), eclipseImgLoad)
            sh.event_step_increment(0)
        end
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
    elseif step == 2 then
        sh.overlay_screen_set(3, 0, false)
        sh.load.wait()
        sh.event_step_increment(0)
        eclipseImgLoad = eclipseImgNext
    elseif step == 3 then
        sh.event_step_increment_after_fade(2, false, 0, 0, false)
        EclipseImageDraw(eclipseImgShown)
    elseif step == 4 then
        EclipseImageDraw(eclipseImgShown)
        if not (EclipseKeyUse("M2S00_PickupKeyOfLion", "M2S00_LockOfLionOpen", "KeyOfLion",
                              "map2_s00.used_the_key_of_lion") or
                EclipseKeyUse("M2S00_PickupKeyOfWoodman", "M2S00_LockOfWoodmanOpen", "KeyOfWoodman",
                              "map2_s00.used_the_key_of_woodman") or
                EclipseKeyUse("M2S00_PickupKeyOfScarecrow", "M2S00_LockOfScarecrowOpen", "KeyOfScarecrow",
                              "map2_s00.used_the_key_of_scarecrow")) then
            local locksLeft = 3
            if sh.flag_get("M2S00_LockOfLionOpen") then
                locksLeft = 2
            end
            if sh.flag_get("M2S00_LockOfWoodmanOpen") then
                locksLeft = locksLeft - 1
            end
            if sh.flag_get("M2S00_LockOfScarecrowOpen") then
                locksLeft = locksLeft - 1
            end
            if locksLeft == 0 then
                eclipseMsg = "common.unlocked" -- "It's unlocked."
                sh.flag_set("M2S00_DoorOfEclypseOpen", true)
                sh.flag_set("MapMark_OldTown_DoghouseArrowsOnly", true)
            elseif locksLeft == 1 then
                eclipseMsg = "map2_s00.there_is_one_lock"
            elseif locksLeft == 2 then
                eclipseMsg = "map2_s00.there_are_two_locks"
            elseif locksLeft == 3 then
                eclipseMsg = "map2_s00.there_are_three_locks"
            end
            sh.event.step_set(0, 11)
        end
    elseif step == 5 then
        sh.event_selection(false, eclipseMsg, 0, 0, 0, false)
        EclipseImageDraw(eclipseImgShown)
        sh.sd_play_sfx("eclipse_door_key_used", 0, 128)
        sh.event_step_increment(0)
    elseif step == 6 or step == 7 then
        if step == 6 then
            EclipseImageLoad(EclipseLockTimFile(), eclipseImgLoad)
            sh.event_step_increment(0)
        end
        EclipseImageDraw(eclipseImgShown)
        sh.event_selection(false, eclipseMsg, 0, 0, 0, false)
    elseif step == 8 then
        EclipseImageDraw(eclipseImgShown)
        sh.overlay_screen_set(1, 0, false) -- step on once the load is done
    elseif step == 9 then
        sh.screen.bg_transition(eclipseImgShown, eclipseImgNext, sh.state_timer_get())
        sh.state_timer_set(sh.state_timer_get() + 128) -- 1/32
        if sh.state_timer_get() > 4096 then
            local shown = eclipseImgShown
            eclipseImgLoad = shown
            eclipseImgShown = eclipseImgNext
            eclipseImgNext = shown
            sh.event_step_increment(0)
        end
    elseif step == 10 then
        EclipseImageDraw(eclipseImgShown)
        sh.event_step_increment_delayed(4096, false) -- 1.0 s
        if sh.input_action("enter", 1) or sh.input_action("cancel", 1) then
            sh.event_step_increment(0)
        end
        if sh.event_step_get(0) ~= 10 then
            sh.event.step_set(0, 4)
        end
    elseif step == 11 then
        EclipseImageDraw(eclipseImgShown)
        sh.event_selection(false, eclipseMsg, 0, 0, 0, false)
    elseif step == 12 then
        EclipseImageDraw(eclipseImgShown)
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
    else
        sh.overlay_screen_set(6, 0, false)
        sh.player_freeze(false)
        sh.state_set(0) -- gameplay
        sh.event_step_increment_after_fade(0, false, 0, 0, false)
    end
end

-- index 19: the map on the wall by Finney St (KEYMAP.TIM).
-- The background is dimmed (9/32) every tick; steps: the picture,
-- fades, message map2_s00.something_is_written_on_the_2/52 by the Finney St flag; at the end the Finney St / dog yard / Ellroy St
-- map mark flags.
function MapEvent_KeyMapInspect()
    sh.background_gamma(72) -- 9/32
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.overlay_screen_set(0, "TIM/KEYMAP.TIM", false) -- KEYMAP.TIM
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
    elseif step == 2 then
        sh.overlay_screen_set(1, 0, false) -- 2ZANKO80.TIM = 0
    elseif step == 3 then
        sh.overlay_screen_set(2, 0, false)
        sh.event_step_increment_after_fade(2, false, 0, 0, false)
    elseif step == 4 then
        if sh.flag_get("MapMark_OldTown_FineyStRightKeyArrow") then
            sh.event_selection(false, "map2_s00.something_is_written_on_the_2") -- "Something is written on the map on the wall."
        else
            sh.event_selection(false, "map2_s00.something_is_written_on_the") -- "... Copied it to the map."
        end
        sh.overlay_screen_set(2, 0, false)
    elseif step == 5 then
        sh.overlay_screen_set(2, 0, false)
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
    else
        sh.player_freeze(false)
        sh.state_set(0)
        sh.event_step_increment_after_fade(0, false, 0, 0, false)
        sh.flag_set("MapMark_OldTown_FineyStRightKeyArrow", true)
        sh.flag_set("MapMark_OldTown_DogYardKeyLine", true)
        sh.flag_set("MapMark_OldTown_EllroyStKeyCircle", true)
    end
end

-- index 20: using K. Gordon's key.
-- Message 39 with the key use sound, then the flags "K. Gordon door open" and
-- "K. Gordon arrows only" on the map.
function MapEvent_KGordonKeyUse()
    sh.item_remove("KGordonKey", 1) -- K. Gordon key
    sh.event_msg_with_sfx("map2_s00.used_the_k_gordon_key", "use_key") -- "Used the K. Gordon key."
    sh.flag_set("M2S00_KGordonDoorOpen", true)
    sh.flag_set("MapMark_OldTown_KGordonArrowsOnly", true)
end

-- index 25: taking the town map (paper map 2, flag "town map picked up", message map2_s00.there_is_a_shopping_district).
function MapEvent_PaperMapTake()
    sh.event_paper_map_take("FogCentralTown", "M2S00_PickupMap", "map2_s00.there_is_a_shopping_district")
end

-- index 26/27/28: street spawn variant 0/1/2 (flags 172/173/174 + a new enemy spawn
-- table), then back to gameplay.
function MapEvent_StreetSpawnVariant0()
    street_spawn_variant_set(0)
    sh.state_set(0)
end

function MapEvent_StreetSpawnVariant1()
    street_spawn_variant_set(1)
    sh.state_set(0)
end

function MapEvent_StreetSpawnVariant2()
    street_spawn_variant_set(2)
    sh.state_set(0)
end

-- index 29: "Nothing special." (an empty sign).
-- freeze -> wait for the player to stop -> message map2_s00.nothing_special -> unfreeze -> gameplay.
function MapEvent_NothingSpecial()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.event_msg(false, "map2_s00.nothing_special") -- "Nothing special."
    else
        sh.player_freeze(false)
        sh.state_set(0)         -- back to gameplay
    end
    sh.pad_effect(9, 96)        -- controller rumble preset 9 at level 96
end

-- index 4: cutscene at the water works: the paper map (steps 1-3),
-- then the view of the water works and message 78.
function MapEvent_AtWaterWorks()
    local step = sh.event_step_get(0)
    if step >= 0 and step <= 3 then
        if step == 0 then
            sh.player_freeze(true)
            local _, py = sh.player_pos()
            sh.player_set_pos(-1047674, py, -440156)        -- -255.78, -107.46 m
            sh.player_rotate(-512)                          -- -45.0 deg
            sh.screen_fade_reset_timestep()
            sh.paper_map_state(0, "OldTown")                        -- paper map state (0, 1)
            street_spawn_variant_set(1)
            sh.event_step_increment(0)
        end
        sh.map_screen_vertex_draw(0, 120, 2048)             -- 0.5
        sh.map_screen_marking_draw(1, 1, 0, 0, 0, 120, 2048)
        sh.map_screen_player_mark_draw(1, 0, 120, 2048)
        local sysState0 = sh.event_step_get(0)
        if sysState0 == 2 then
            sh.event_step_increment_delayed(14336, false)   -- 3.5 s
            if sh.event_step_get(0) == sysState0 and
               (sh.input_action("enter", 1) or sh.input_action("cancel", 1)) then
                StepSet0(3)
            end
        elseif sysState0 == 3 then
            sh.event_step_increment_after_fade(2, true, 0, 4096, false)
        elseif sysState0 == 1 then
            sh.event_step_increment_after_fade(2, false, 0, 4096, false)
        end
    elseif step == 4 or step == 5 then
        if step == 4 then
            sh.cutscene_border(20)
            sh.paper_map_state(2, "OtherPlaces")
            sh.camera_pos(M(-1040998), M(-7987), M(-447692), 0, 0, 0, 0, 0, 0, 0, true)  -- -254.15, -1.95, -109.3 m
            sh.camera_look(M(-1052835), M(-7946), M(-436387), 0, 0, 0, 0, 0, 0, 0, true) -- -257.0398, -1.94, -106.54 m
            sh.event_step_increment(0)
        end
        sh.event_step_increment_after_fade(2, false, 0, 4096, false)
    elseif step == 6 then
        sh.event_selection(false, "map2_s00.this_must_be_that_water", 0, 0, 0, false)       -- "This must be the water works."
    else
        sh.player_freeze(false)
        sh.state_set(0)
        sh.camera_auto_return(false)
        sh.event_step_increment_after_fade(0, false, 2, 0, false)
        sh.flag_set("M2S00_WaterWorksCutscene", true)                               -- flag: water works cutscene
        sh.sys_flags_set(sh.sys_flags_get() & ~SYSFLAG_NO_ENEMY_SPAWN)
    end
end

-- index 10: Cheryl's sketchbook page (SKETCHBK.TIM through the overlay screen).
function InspectCherylsSketchbookPage()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.overlay_screen_set(0, "TIM/SKETCHBK.TIM", false)
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.chara_anim_play_to_end(0, "pickup_crouch")
    elseif step == 3 then
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
    elseif step == 4 then
        sh.overlay_screen_set(1, "1ST/2ZANKO80.TIM", false)
    elseif step == 5 then
        sh.overlay_screen_set(2, "1ST/2ZANKO80.TIM", false)
        sh.event_step_increment_after_fade(2, false, 0, 0, false)
    elseif step == 6 then
        sh.overlay_screen_set(2, "1ST/2ZANKO80.TIM", false)
        sh.event_selection(false, "map2_s00.sketchbook_cover_this_picture", 0, 0, 0, false)
    elseif step == 7 then
        sh.event_step_increment_after_fade(2, false, 0, 0, false)
    elseif step == 8 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")
    else
        sh.player_freeze(false)
        sh.state_set(0)
    end
end

-- Bridge switch (index 21/22): sound positions and the shared bridge movement.
-- bridge_rot.z lives in the map state (the bridge drawing reads it); the hum level
-- belongs to these handlers only, a Lua local.
local BRIDGE_SWITCH_POS = {-170295, -14823, 1417183} -- (-41.576, -3.619, 345.992) m
local BRIDGE_POS        = {-143360, 0, 1441792}      -- (-35.0, 0.0, 352.0) m
local s_bridgeHum = 0

-- Volume of the fading hum: inverted (h >> 4) while it fits in 0..255, otherwise 0.
local function BridgeHumVolume(h)
    local hs = h >> 4 -- h is in [0, 1.0], never negative
    if 255 - hs >= 0 then
        return (~hs) & 255
    end
    return 0
end

-- Move the bridge towards 0 along bridge_rot.z; true once it gets there.
local function BridgeMove(speedQ12)
    local vz = sh.map.get("bridge_rot.z")
    vz = S16(vz + (sh.delta_time_get() * speedQ12) // 4096) -- dt * speed
    sh.map.set("bridge_rot.z", vz)
    if vz > 0 then
        sh.map.set("bridge_rot.z", 0)
        sh.sfx_stop("bridge_motor_loop")
        sh.sd_play_sfx("bridge_motor", sh.vc_stereo_balance(BRIDGE_POS[1], BRIDGE_POS[2], BRIDGE_POS[3]), 0)
        sh.event_step_increment(0)
    end
end

local function BridgeFlags()
    sh.flag_set("M2S00_DrawbridgeOperated", true)
    sh.flag_set("M2S00_DrawbridgeLowered", true)
    if sh.flag_get("MapMark_OldTown_BotBridgeCross") then                                -- map mark: Old Town bottom bridge cross
        sh.flag_set("MapMark_OldTown_BotBridgeCorrection", true)                              -- ...bottom bridge correction
        sh.flag_set("MapMark_OldTown_BotBridgeArrow", true)                              -- ...bottom bridge arrow
    end
end

-- index 21: bridge switch (first use, msg map2_s00.the_machinery_is_running_do).
function UseBridgeSwitchFirst()
    local PRESS = 5 -- "press the switch"; "don't press" = -1 -> final step
    local step = sh.event_step_get(0)
    if step == 0 or step == 1 then
        if step == 0 then
            sh.player_freeze(true)
            sh.event_step_increment_after_fade(0, true, 3, 0, false)
            sh.event_step_increment(0)
        end
        sh.event_wait_player_stop()
    elseif step == 2 or step == 3 then
        if step == 2 then
            sh.sfx_3d_at("bridge_switch_click", BRIDGE_SWITCH_POS[1], BRIDGE_SWITCH_POS[2], BRIDGE_SWITCH_POS[3], 128)
            sh.flag_set("M2S00_DrawbridgeKeyUsed", true)
            sh.sd_call("bridge_switch_motor")
            sh.map.set("bridge_switch_state", -1)          -- no value
            sh.event_step_increment(0)
        end
        sh.event_step_increment_delayed(8192, false)        -- 2.0 s
    elseif step == 4 then
        sh.selection_silent(true)
        sh.event_selection(true, "map2_s00.the_machinery_is_running_do", PRESS, -1, 0, false)   -- "The machinery is running..."
    elseif step == PRESS or step == 6 then
        if step == PRESS then
            sh.sfx_3d_at("bridge_switch_click_alt", BRIDGE_SWITCH_POS[1], BRIDGE_SWITCH_POS[2], BRIDGE_SWITCH_POS[3], 128)
            sh.event_step_increment(0)
        end
        sh.event_step_increment_delayed(2048, false)        -- 0.5 s
    elseif step == 7 or step == 8 then
        if step == 7 then
            sh.sd_call("bridge_motor_loop")
            s_bridgeHum = 0
            sh.event_step_increment(0)
        end
        s_bridgeHum = S16(s_bridgeHum + (sh.delta_time_get() & 0xFFFF) * 4) -- dt (16 bits) * 4
        if s_bridgeHum > 4096 then
            s_bridgeHum = 4096
        end
        local balance = sh.vc_stereo_balance(BRIDGE_POS[1], BRIDGE_POS[2], BRIDGE_POS[3])
        sh.sfx_attributes_update("bridge_motor_loop", balance, BridgeHumVolume(s_bridgeHum), 0)
        BridgeMove(136)                                     -- 0.0332
    else
        if step == 9 then
            BridgeFlags()
            sh.event_step_increment(0)
        end
        sh.player_freeze(false)
        sh.state_set(0)
        sh.event_step_increment_after_fade(0, false, 2, 0, false)
    end

    if sh.flag_get("M2S00_DrawbridgeKeyUsed") then
        if sh.event_step_get(0) > 4 then
            sh.pad_effect(9, 0x90)                          -- controller rumble preset 9 at level 144
        else
            sh.pad_effect(9, 0x70)
        end
    end
end

-- index 22: bridge switch (used again, msg map2_s00.the_machinery_is_running_do).
function UseBridgeSwitchAgain()
    local PRESS = 3 -- "press the switch"; "don't press" = -1 -> final step
    local step = sh.event_step_get(0)
    if step == 0 or step == 1 then
        if step == 0 then
            sh.player_freeze(true)
            sh.event_step_increment(0)
        end
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.selection_silent(true)
        sh.event_selection(true, "map2_s00.the_machinery_is_running_do", PRESS, -1, 0, false)
    elseif step == PRESS or step == 4 or step == 5 then
        if step == PRESS then
            sh.event_step_increment_after_fade(0, true, 3, 0, false)
            sh.event_step_increment(0)
        end
        if step <= 4 then
            sh.sfx_3d_at("bridge_switch_click_alt", BRIDGE_SWITCH_POS[1], BRIDGE_SWITCH_POS[2], BRIDGE_SWITCH_POS[3], 128)
            sh.event_step_increment(0)
        end
        sh.event_step_increment_delayed(2048, false)        -- 0.5 s
    elseif step == 6 or step == 7 then
        if step == 6 then
            sh.sd_call("bridge_motor_loop")
            s_bridgeHum = 0
            sh.event_step_increment(0)
        end
        s_bridgeHum = S16(s_bridgeHum + sh.delta_time_get())
        if s_bridgeHum > 4096 then
            s_bridgeHum = 4096
        end
        local balance = sh.vc_stereo_balance(BRIDGE_POS[1], BRIDGE_POS[2], BRIDGE_POS[3])
        sh.sfx_attributes_update("bridge_motor_loop", balance, BridgeHumVolume(s_bridgeHum), 0)
        BridgeMove(136)                                     -- 0.1 / 3.0 = 136
    else
        if step == 8 then
            BridgeFlags()
            sh.event_step_increment_after_fade(0, false, 2, 0, false)
            sh.event_step_increment(0)
        end
        sh.player_freeze(false)
        sh.state_set(0)
    end

    if sh.event_step_get(0) > 3 then
        sh.pad_effect(9, 0x90)
    else
        sh.pad_effect(9, 0x70)
    end
end

-- index 23: the worn lock (cutscene, msg map2_s00.hmm_the_lock_is_worn).
function InspectWornLock()
    local step = sh.event_step_get(0)
    if step == 0 or step == 1 then
        if step == 0 then
            sh.player_freeze(true)
            sh.sfx_3d("door_locked_alt", 128)                            -- 3D sound at the event point, y = -1.2
            sh.event_step_increment(0)
        end
        sh.event_step_increment_delayed(819, false)         -- 0.2 s
    elseif step == 2 then
        sh.event_wait_player_stop()
    elseif step == 3 then
        sh.event_selection(false, "common.locked", 0, 0, 0, false)       -- message: door locked
    elseif step == 4 or step == 5 then
        if step == 4 then
            sh.camera_pos(M(-1070161), M(-8642), M(-431964), 0, 0, 0, 0, 0, 0, 0, true)  -- -261.27, -2.11, -105.46 m
            sh.camera_look(M(-1076183), M(286), M(-419635), 0, 0, 0, 0, 0, 0, 0, true)   -- -262.74, 0.07, -102.45 m
            local _, py = sh.player_pos()
            sh.player_set_pos(-1074749, py, -427335)        -- -262.39, -104.33 m
            sh.player_rotate(512)                           -- 45.0 deg
            sh.held_item_hide()
            sh.player_weapon_stash()
            sh.event_step_increment(0)
        end
        sh.event_step_increment_after_fade(2, true, 2, 0, false)
    elseif step == 6 then
        sh.event_step_increment_delayed(4096, false)
    elseif step == 7 then
        sh.event_selection(false, "map2_s00.hmm_the_lock_is_worn", 0, 0, 0, false)       -- "Hmm... the lock is worn out."
    elseif step == 8 then
        sh.event_step_increment_delayed(2048, false)
    else
        sh.player_freeze(false)
        sh.state_set(0)
        sh.camera_auto_return(true)
        sh.held_item_show()
        sh.player_weapon_restore()
        sh.flag_set("M2S00_WornPadlockInspected", true)
        sh.event_step_increment_after_fade(0, false, 2, 0, false)
    end
end

-- index 24: inspect the door: rustle (sound door_creak_open), jerk (sound door_creak_close) and the swinging
-- door (swing_door_rot.y in the map state). The swing speed belongs to this handler only.
local s_doorSwingSpeed = 0

function InspectSwingingDoor()
    local step = sh.event_step_get(0)
    if step == 0 or step == 1 then
        if step == 0 then
            sh.player_freeze(true)
            sh.sfx_3d_at("door_creak_open", -1073057, -409, -427155, 128) -- -261.977, -0.1, -104.286 m
            sh.event_step_increment(0)
        end
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
    elseif step == 2 then
        sh.event_wait_player_stop()
    elseif step == 3 or step == 4 then
        if step == 3 then
            sh.camera_pos(M(-1077370), M(-14049), M(-431595), 0, 0, 0, 0, 0, 0, 0, true) -- -263.03, -3.43, -105.37 m
            sh.camera_look(M(-1071431), M(-327), M(-424878), 0, 0, 0, 0, 0, 0, 0, true)  -- -261.58, -0.08, -103.73 m
            s_doorSwingSpeed = 0
            sh.flag_set("M2S00_ChainedDoorSwungOpen", true)
            sh.cutscene_border(20)
            sh.chara_slot_anim_flags_clear(0, 2)            -- hide the player model
            sh.event_step_increment(0)
        end
        sh.event_step_increment_after_fade(2, false, 0, 0, false)
    elseif step == 5 then
        sh.event_step_increment_delayed(3276, false)        -- 0.8 s
    elseif step == 6 or step == 7 then
        if step == 6 then
            sh.sfx_3d_at("door_creak_close", -1077248, -5120, -425984, 128) -- -263.0, -1.25, -104.0 m
            sh.event_step_increment(0)
        end
        sh.event_step_increment_delayed(8192, false)        -- 2.0 s
        local dt = sh.delta_time_get()
        s_doorSwingSpeed = S16(s_doorSwingSpeed + (dt * -113) // 4096) -- -0.0277
        s_doorSwingSpeed = math.max(-56, s_doorSwingSpeed)  -- -0.0137
        local vy = sh.map.get("swing_door_rot.y")
        vy = S16(vy + (dt * s_doorSwingSpeed) // 4096)
        if vy < -91 then                                    -- -0.02222
            vy = -91
        end
        sh.map.set("swing_door_rot.y", vy)
    else
        sh.player_freeze(false)
        sh.state_set(0)
        sh.event_step_increment_after_fade(0, false, 2, 0, false)
        sh.chara_slot_anim_flags_set(0, 2)
        local attack = sh.player_weapon_attack_get()
        if attack < 32 then                                 -- a handgun tap attack
            sh.player_weapon_attack_set(math.fmod(attack, 10)) -- keep only the attack id
        end
        sh.camera_auto_return(true)
    end
end

-- Paper map screen (index 8/9). Timesteps are scaled to 30 fps: (x * dt) / (1/30 s);
-- the arguments are never negative, so // is exact. The zoom/alpha/skip state and
-- the map mark alpha are read only by these two handlers, so they are Lua locals,
-- as are the fade accumulators.
-- MP_0TOWN.TIM + paper_map_tex_offset (map state, sh.map.get).
local TIMESTEP_30_FPS = 136 -- 1.0 / 30 s
local function Ts30(dt, x) return (x * dt) // TIMESTEP_30_FPS end

local function PaperMapSeek()
    sh.fs_seek(sh.file_number("TIM/MP_0TOWN.TIM") + sh.map.get("paper_map_tex_offset"))
end

local function EnterOrCancel()
    return sh.input_action("enter", 1) or sh.input_action("cancel", 1)
end

-- index 8: leaving the cafe: line 16, the town map with
-- zoom/frame, a mark fading in (MapMark_OldTown_CherylArrowAndSign).
local s_cafeZoom = 0
local s_cafeSkipFade = 0
local s_cafeMarkAlpha = 0  -- map mark alpha
local s_cafeFadeAccum = 0  -- fade accumulator, kept across ticks

function CutsceneExitCafe()
    if sh.input_action("skip", 1) then
        local step = sh.event_step_get(0)
        if step == 2 then
            StepSet0(13)
        elseif s_cafeSkipFade == 0 and step > 5 and step < 11 then
            sh.event_step_increment_after_fade(0, true, 0, 0, false)
            s_cafeSkipFade = 1
        end
    end
    if s_cafeSkipFade ~= 0 and sh.screen_fade_finished() then
        s_cafeSkipFade = 0
        StepSet0(12)
    end

    local step = sh.event_step_get(0)
    if step >= 0 and step <= 2 then
        if step == 0 then
            sh.player_freeze(true)
            sh.cutscene_border(20)
            s_cafeZoom = 0
            s_cafeMarkAlpha = 0
            sh.flag_set("MapMark_OldTown_CofeArrowOnly", true)                          -- map mark: Old Town cafe arrow only
            sh.flag_set("MapMark_OldTown_CofeSignOnly", true)                          -- map mark: Old Town cafe sign only
            s_cafeSkipFade = 0
            sh.event_step_increment(0)
        end
        if step <= 1 then
            sh.camera_pos(M(63651), M(-6144), M(227901), 0, 0, 0, 0, 0, 0, 0, true) -- 15.54, -1.5, 55.64 m
            sh.camera_look(M(79544), M(-7168), M(231833), 0, 0, 0, 0, 0, 0, 0, true) -- 19.42, -1.75, 56.6 m
            sh.event_step_increment(0)
        end
        sh.event_selection(false, "map2_s00.where_could_cheryl_have_gone", 0, 0, 0, false)       -- "Where could Cheryl have gone?"
    elseif step == 3 or step == 4 then
        if step == 3 then
            PaperMapSeek()
            sh.event_step_increment(0)
        end
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
    elseif step == 5 or step == 6 then
        if step == 5 then
            sh.paper_map_state(0, "OldTown")
            sh.event_step_increment(0)
        end
        sh.map_screen_vertex_draw(0, 0, 4096)
        sh.map_screen_marking_draw(1, 1, 0, 0, 0, 0, 4096)
        sh.event_step_increment_after_fade(2, false, 0, 0, false)
    elseif step == 7 then
        sh.map_screen_vertex_draw(0, 0, 4096)
        sh.map_screen_marking_draw(1, 1, 0, 0, 0, 0, 4096)
        s_cafeZoom = math.min(s_cafeZoom + Ts30(sh.delta_time_get(), 0x20), 4096)
        sh.map_box_outline_draw(s_cafeZoom, -0xA0, -0xE0, 0x13F, 0x1BF, -0x58, -0xC0, 0x9F, 0xDF)
        if s_cafeZoom == 4096 then
            sh.event_step_increment(0)
        end
    elseif step == 8 then
        s_cafeZoom = math.max(s_cafeZoom - Ts30(sh.delta_time_get(), 0x40), 0)
        local x = 0x48 - (s_cafeZoom * 0x48) // 4096
        local y = 0x10 - (s_cafeZoom * 0x10) // 4096
        local scale = (s_cafeZoom >> 1) + 2048
        sh.map_screen_vertex_draw(x, y, scale)
        sh.map_screen_marking_draw(1, 1, 0, 0, x, y, scale)
        local curve = S16(-((s_cafeZoom << 12) // (s_cafeZoom + 4096)))
        sh.map_box_outline_draw(curve, -0xA0, -0xE0, 0x13F, 0x1BF, -0x10, -0xA0, 0, 0)
        if s_cafeZoom == 0 then
            sh.event_step_increment(0)
        end
    elseif step == 9 then
        sh.map_screen_vertex_draw(0x48, 0x10, 2048)
        sh.map_screen_marking_draw(1, 1, 0, 0, 72, 0x10, 2048)
        sh.event_step_increment_delayed(4096, false)
    elseif step == 10 then
        sh.map_screen_vertex_draw(0x48, 0x10, 2048)
        sh.map_screen_marking_draw(1, 1, 0, 0, 72, 0x10, 2048)
        sh.map_screen_marking_draw(2, 1, "MapMark_OldTown_CherylArrowAndSign", s_cafeMarkAlpha, 0x48, 0x10, 2048)
        s_cafeFadeAccum = s_cafeFadeAccum + Ts30(sh.delta_time_get(), 4096)
        local fadeStep = s_cafeFadeAccum >> 12
        s_cafeFadeAccum = s_cafeFadeAccum - (fadeStep << 12)
        s_cafeMarkAlpha = s_cafeMarkAlpha + fadeStep
        if s_cafeMarkAlpha >= 128 then
            if EnterOrCancel() then
                sh.event_step_increment(0)
            end
            sh.event_step_increment_delayed(6144, false)    -- 1.5 s
            if sh.event_step_get(0) > 11 then
                sh.event_step_set(0, 11)                    -- writes only the step
            end
            s_cafeMarkAlpha = 128
        end
    elseif step == 11 then
        sh.map_screen_vertex_draw(0x48, 0x10, 2048)
        sh.map_screen_marking_draw(1, 1, 0, 0, 0x48, 0x10, 2048)
        sh.map_screen_marking_draw(2, 1, "MapMark_OldTown_CherylArrowAndSign", 128, 0x48, 0x10, 2048)
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
    elseif step == 12 or step == 13 then
        if step == 12 then
            sh.paper_map_state(2, "OtherPlaces")
            s_cafeSkipFade = 0
            sh.event_step_increment(0)
        end
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
    else
        if step == 14 then
            sh.event_step_increment_after_fade(0, false, 0, 0, false)
            sh.event_step_increment(0)
        end
        sh.player_freeze(false)
        sh.state_set(0)
        sh.flag_set("MapMark_OldTown_CherylArrowAndSign", true)                              -- map mark: Old Town Cheryl arrow and sign
        sh.camera_auto_return(false)
        sh.event_step_increment_after_fade(0, false, 2, 0, false)
        sh.sys_flags_set(sh.sys_flags_get() & ~SYSFLAG_NO_ENEMY_SPAWN)
        sh.game_state_update()
    end
end

-- index 9: the "To school" note: TOSCHOOL.TIM screen,
-- voiced line 19 (msg_audio_idx / msg_audio_cmds in the map state),
-- the map with zoom and the school mark (MapMark_OldTown_SchoolCircle).
local s_sketchSkip = 0
local s_sketchMarkAlpha = 0
local s_sketchZoom = 0
local s_sketchMarkAccum = 0  -- mark accumulator, kept across ticks

function CutsceneCherylsSketchbook()
    if sh.input_action("skip", 1) and s_sketchSkip == 0 then
        local step = sh.event_step_get(0)
        if step > 1 and step < 10 then
            sh.event_step_increment_after_fade(0, true, 0, 0, false)
            s_sketchSkip = 1
        elseif step > 10 and step < 16 then
            sh.event_step_increment_after_fade(0, true, 0, 0, false)
            s_sketchSkip = 2
        end
    end
    if s_sketchSkip ~= 0 and sh.screen_fade_finished() then
        if s_sketchSkip == 1 then
            StepSet0(21)
        else
            StepSet0(22)
        end
        s_sketchSkip = 0
    end

    local step = sh.event_step_get(0)
    if step == 0 or step == 1 then
        if step == 0 then
            sh.player_freeze(true)
            sh.flag_set("MapMark_OldTown_CherylArrowAndSignBlank", true)                          -- the blank half of Cheryl's mark
            sh.overlay_screen_set(0, "TIM/TOSCHOOL.TIM", false)           -- TOSCHOOL.TIM
            sh.map.set("msg_audio_idx", 0)
            s_sketchMarkAlpha = 0
            s_sketchZoom = 0
            sh.event_step_increment(0)
        end
        sh.event_wait_player_stop()
    elseif step == 2 then
        sh.chara_anim_play_to_end(0, "pickup_crouch")
    elseif step == 3 then
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
    elseif step == 4 then
        sh.overlay_screen_set(1, 0, false)
    elseif step == 5 then
        sh.overlay_screen_set(2, 0, false)
        sh.event_step_increment_after_fade(2, false, 0, 0, false)
    elseif step == 6 then
        sh.overlay_screen_set(2, 0, false)
        sh.event_selection(false, "map2_s00.to_school", 0, 0, 0, false)       -- "To school."
    elseif step == 7 then
        sh.background_gamma(48)                             -- 6 / 32
        sh.overlay_screen_set(2, 0, false)
        sh.map_msg_audio("map2_s00.isnt_this_cheryls_sketchbook", "msg_audio_idx", "msg_audio_cmds")
    elseif step == 8 or step == 9 then
        if step == 8 then
            PaperMapSeek()
            sh.event_step_increment(0)
        end
        sh.background_gamma(48)
        sh.overlay_screen_set(2, 0, false)
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
    elseif step == 10 or step == 11 then
        if step == 10 then
            sh.paper_map_state(0, "OldTown")
            sh.flag_set("M2S00_NoteToSchoolFound", true)
            sh.event_step_increment(0)
        end
        sh.map_screen_vertex_draw(0, 0, 4096)
        sh.map_screen_marking_draw(1, 1, 0, 0, 0, 0, 4096)
        sh.event_step_increment_after_fade(2, false, 0, 0, false)
    elseif step == 12 then
        sh.map_screen_vertex_draw(0, 0, 4096)
        sh.map_screen_marking_draw(1, 1, 0, 0, 0, 0, 4096)
        s_sketchZoom = math.min(s_sketchZoom + Ts30(sh.delta_time_get(), 0x20), 4096)
        sh.map_box_outline_draw(s_sketchZoom, -0xA0, -0xE0, 0x13F, 0x1BF, -0xA0, 8, 0x9F, 0xDF)
        if s_sketchZoom == 4096 then
            sh.event_step_increment(0)
        end
    elseif step == 13 then
        s_sketchZoom = math.max(s_sketchZoom - Ts30(sh.delta_time_get(), 0x40), 0)
        local y = 0x74 - (s_sketchZoom * 0x74) // 4096
        local scale = (s_sketchZoom >> 1) + 2048
        sh.map_screen_vertex_draw(0, y, scale)
        sh.map_screen_marking_draw(1, 1, 0, 0, 0, y, scale)
        local curve = S16(-((s_sketchZoom << 12) // (s_sketchZoom + 4096)))
        sh.map_box_outline_draw(curve, -0xA0, -0xE0, 0x13F, 0x1BF, -0xA0, 0xF0, 0, 0)
        if s_sketchZoom == 0 then
            sh.event_step_increment(0)
        end
    elseif step == 14 then
        sh.map_screen_vertex_draw(0, 0x74, 2048)
        sh.map_screen_marking_draw(1, 1, 0, 0, 0, 0x74, 2048)
        sh.event_step_increment_delayed(4096, false)
    elseif step == 15 then
        sh.map_screen_vertex_draw(0, 0x74, 2048)
        sh.map_screen_marking_draw(1, 1, 0, 0, 0, 0x74, 2048)
        sh.map_screen_marking_draw(2, 1, "MapMark_OldTown_SchoolCircle", s_sketchMarkAlpha, 0, 0x74, 2048)
        s_sketchMarkAccum = s_sketchMarkAccum + Ts30(sh.delta_time_get(), 4096)
        local markStep = s_sketchMarkAccum >> 12
        s_sketchMarkAccum = s_sketchMarkAccum - (markStep << 12)
        s_sketchMarkAlpha = s_sketchMarkAlpha + markStep
        if s_sketchMarkAlpha >= 0x80 then
            if EnterOrCancel() then
                sh.event_step_increment(0)
            end
            sh.event_step_increment_delayed(6144, false)
            if sh.event_step_get(0) > 16 then
                sh.event_step_set(0, 16)                    -- writes only the step
            end
            s_sketchMarkAlpha = 0x80
        end
    elseif step == 16 then
        sh.map_screen_vertex_draw(0, 0x74, 2048)
        sh.map_screen_marking_draw(1, 1, 0, 0, 0, 0x74, 2048)
        sh.map_screen_marking_draw(2, 1, "MapMark_OldTown_SchoolCircle", 128, 0, 0x74, 2048)
        sh.event_step_increment_after_fade(2, true, 0, 0, false)
    elseif step == 17 or step == 18 then
        if step == 17 then
            sh.paper_map_state(2, "OtherPlaces")
            sh.flag_set("MapMark_OldTown_SchoolCircle", true)                          -- map mark: Old Town school circle
            s_sketchSkip = 0
            sh.event_step_increment(0)
        end
        sh.event_step_increment_after_fade(2, false, 0, 0, false)
    elseif step == 19 then
        sh.chara_anim_play_to_end(0, "pickup_stand_up")
    else
        if step == 22 then
            sh.paper_map_state(2, "OtherPlaces")
        end
        if step == 22 or step == 21 then
            sh.flag_set("M2S00_NoteToSchoolFound", true)
            sh.flag_set("MapMark_OldTown_SchoolCircle", true)
            sh.event_step_increment_after_fade(0, false, 0, 0, false)
            sh.player_freeze(false, true)                   -- unfreeze the player
            sh.state_set(0)
        end
        sh.player_freeze(false)
        sh.state_set(0)
        sh.event_inv_item_cmd(3, "NoteToSchool", 1, false)
        sh.game_state_update()
    end
end

-- index 18: entering the Door of Eclipse: "it's getting dark again".
-- The map effects read eclipse_darkness (map state) every tick, and
-- step 1 animates: 40 + 60 * (1 - cos(state_timer / 12)).
function MapEvent_DoorOfEclipseEnter()
    local step = sh.event_step_get(0)
    if step == 0 or step == 1 then
        if step == 0 then
            sh.player_freeze(true)
            sh.map_effects_init(1, 1)
            sh.map_effects_update(6, 6, 5, "eclipse_darkness", 0, 409600) -- 32-bit value, 100.0
            local _, py = sh.player_pos()
            sh.player_set_pos(604979, py, 1542144)          -- 147.7, 376.5 m
            sh.player_rotate(-1024)                         -- -90 deg
            sh.cutscene_border(20)
            sh.cutscene_flag(true)                          -- cutscene active
            sh.event_step_increment_after_fade(0, false, 0, 0, false)
            sh.event_step_increment(0)
        end
        local t = sh.state_timer_get() + sh.delta_time_get()
        sh.state_timer_set(t)
        local tmp = 4096 - sh.math_cos(t // 12)             -- t >= 0, so // is exact
        sh.map.set("eclipse_darkness", (tmp * 245760) // 4096 + 163840) -- tmp * 60 + 40
        if t > 6144 and t < 18432 then                      -- (1.5, 4.5)
            sh.event_selection(false, "map2_s00.what", 0, 0, 0, false)   -- "What? It's getting dark again?"
        end
        sh.event_step_increment_delayed(24576, false)       -- 6.0 s
    elseif step == 2 then
        sh.map.set("eclipse_darkness", 409600)         -- 100.0
        sh.flashlight(false)
        sh.event_step_increment_delayed(819, false)         -- 0.2 s
    elseif step == 3 then
        sh.event_selection(false, "map2_s00.its_getting_dark_again", 0, 0, 0, false)
    elseif step == 4 or step == 5 then
        if step == 4 then
            sh.map_effects_init(6, 3)
            sh.flashlight(true)
            sh.event_step_increment(0)
        end
        sh.event_step_increment_delayed(4915, false)        -- 1.2 s
    else
        sh.player_freeze(false)
        sh.state_set(0)
        sh.event_step_increment_after_fade(0, false, 2, 0, false)
        sh.flag_set("M2S00_DarknessFell", true)
        sh.camera_auto_return(false)
    end
end

MapEvent_CutsceneExitCafe = CutsceneExitCafe
MapEvent_CherylsSketchbook = CutsceneCherylsSketchbook
