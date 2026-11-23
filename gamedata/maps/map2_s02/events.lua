-- map2_s02: Central Silent Hill - streets
-- Event scripts of this area: the engine runs a handler every tick while its event is active.
-- Fixed-point numbers: 4096 = 1.0 (metres, seconds); angles: 4096 = 360 degrees.

function MapEvent_DoorJammed()
    sh.event_msg_with_sfx("common.lock_jammed", "door_jammed")
end

function MapEvent_DoorLocked()
    sh.event_msg_with_sfx("common.locked", "door_locked")
end

-- Antique shop key used on the door.
function MapEvent_AntiqueShopKeyUse()
    sh.item_remove("AntiqueShopKey", 1)
    sh.event_msg_with_sfx("map2_s02.used_the_antique_shop_key", "use_key")  -- message map2_s02.used_the_antique_shop_key, use key sound
    sh.flag_set("M2S02_AntiqueShopOpen", true)
end

-- Common pickups by the event's point of interest.
function MapEvent_CommonItemTake()
    local t = {
        [41] = {"HealthDrink",    "M2S02_HealthDrink0"},
        [42] = {"HealthDrink",    "M2S02_HealthDrink1"},
        [43] = {"HandgunBullets", "M2S02_HandgunBullets0"},
        [44] = {"HandgunBullets", "M2S02_HandgunBullets1"},
        [45] = {"HealthDrink",    "M2S02_HealthDrink2"},
    }
    local e = t[sh.event_point()] or {"FirstAidKit", 0}
    sh.event_common_item_take(e[1], e[2])
end

-- index 5: entering the room — wait for the ambient sounds to load, then flag M2S02_AmbienceRestoredAfterOtherworld + fade.
function MapEvent_RoomEnterAmbientWait()
    sh.delta_time_reset()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.screen_fade_reset_timestep()
        sh.event_step_increment(0)
        step = 1
    end
    if step == 1 then
        if sh.ambient_sfx() then
            return  -- the ambient sounds are still loading: wait a tick
        end
        sh.event_step_increment(0)
    end
    if sh.event_step_get(0) >= 2 then
        sh.player_freeze(false)
        sh.state_set(0)
        sh.flag_set("M2S02_AmbienceRestoredAfterOtherworld", true)
        sh.event_fade_cmd(0, 0, 0, 0, 0)      -- start, type 0, 0
    end
end

-- index 4: darken the screen, wait 1.2 s, flag M2S02_BackFromOtherworld, flashlight off.
function MapEvent_DarkenAndWait()
    local step = sh.event_step_get(0)
    if step == 0 then
        sh.player_freeze(true)
        sh.screen_fade_status_set(12)
        sh.sfx_stop("darkness_drone")
        sh.event_step_increment(0)
    elseif step == 1 or step == 2 then
        if step == 1 then
            sh.event_fade_cmd(0, 0, 1, 1024, 0) -- Start, type 1, 0.25
            sh.event_step_increment(0)
        end
        sh.event_wait_timer(4915, 0)               -- 1.2 s
    elseif step > 2 then
        sh.player_freeze(false)
        sh.state_set(0)
        sh.flag_set("M2S02_BackFromOtherworld", true)
        sh.flashlight(false)
        sh.game_state_update()                         -- refresh the game state
    end
end
