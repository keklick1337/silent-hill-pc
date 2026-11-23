-- map2_s04: Police station (Central Silent Hill)
-- Event scripts of this area: the engine runs a handler every tick while its event is active.
-- Fixed-point numbers: 4096 = 1.0 (metres, seconds); angles: 4096 = 360 degrees.


function MapEvent_DoorJammed()
    sh.event_msg_with_sfx("common.lock_jammed", "door_jammed")
end

function MapEvent_DoorLocked()
    sh.event_msg_with_sfx("common.locked", "door_locked")
end

-- Common pickups by the event's point of interest.
function MapEvent_CommonItemTake()
    local t = {
        [24] = {"ShotgunShells",  "M2S04_ShotgunShells0"},
        [25] = {"HandgunBullets", "M2S04_HandgunBullets0"},
        [26] = {"HandgunBullets", "M2S04_HandgunBullets1"},
        [27] = {"ShotgunShells",  "M2S04_ShotgunShells1"},
        [28] = {"HandgunBullets", "M2S04_HandgunBullets2"},
        [29] = {"HandgunBullets", "M2S04_HandgunBullets3"},
        [30] = {"RifleShells",    "M2S04_RifleShells"},
    }
    local e = t[sh.event_point()] or {"FirstAidKit", 0}
    sh.event_common_item_take(e[1], e[2])
end

-- Note on the police chalkboard (PLCSBORD picture background) + flag M2S04_SeenPoliceChalkboard.
function MapEvent_PoliceChalkboardInspect()
    sh.event_msg_with_bg("TIM/PLCSBORD.TIM", 0, 0, "map2_s04.theres_something_on_it")  -- PLCSBORD.TIM, 0.0
    sh.flag_set("M2S04_SeenPoliceChalkboard", true)
end

function MapEvent_PaperMapTake()
    sh.event_paper_map_take("FogCentralTown", "M2S00_PickupMap", "map2_s04.there_is_a_shopping_district")
end

function MapEvent_ShotgunTake()
    sh.event_item_take("Shotgun", 6, "M1S02_PickupShotgun", "map2_s04.there_is_a_shotgun_take")  -- shotgun, shotgun shells pickup count
end

-- index 3: memo on the board (dimmed background + PLCSMEMO picture).
-- Dimming 10/32 = 80; picture at 0.0.
function MapEvent_PoliceMemoInspect()
    sh.background_gamma(80)
    sh.event_msg_with_bg("TIM/PLCSMEMO.TIM", 0, 0, "map2_s04.theres_a_memo_on_the")  -- PLCSMEMO.TIM
end
