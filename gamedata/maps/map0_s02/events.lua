-- map0_s02: Old Silent Hill - bonus unlockable areas
-- Event scripts of this area: the engine runs a handler every tick while its event is active.

function on_boot()
end

-- index 2: common pickups (first aid kit, drinks, bullets) by the event's point of interest.
function MapEvent_CommonItemTake()
    local poi = sh.event_point()
    local t = {
        [29] = {"FirstAidKit",    "M0S02_FirstAidKit"},
        [30] = {"HealthDrink",    "M0S02_HealthDrink0"},
        [31] = {"HealthDrink",    "M0S02_HealthDrink1"},
        [32] = {"HealthDrink",    "M0S02_HealthDrink2"},
        [33] = {"HandgunBullets", "M0S02_HandgunBullets"},
        [34] = {"HealthDrink",    "M0S02_HealthDrink3"},
        [35] = {"HealthDrink",    "M0S02_HealthDrink4"},
    }
    local e = t[poi]
    if e == nil then
        -- any other point gives a first aid kit with flag 0
        e = {"FirstAidKit", 0}
    end
    sh.event_common_item_take(e[1], e[2])
end

-- index 3: no action.
function MapEvent_EmptyFunction()
end

-- index 4: gasoline tank (message map0_s02.there_is_a_gasoline_tank "Take it?").
function MapEvent_GasolineTankItemTake()
    sh.event_item_take("GasolineTank", 1, "M0S02_PickupGasolineTank", "map0_s02.there_is_a_gasoline_tank")
end

-- index 5: katana (message map0_s02.there_is_a_katana_take).
function MapEvent_KatanaItemTake()
    sh.event_item_take("Katana", 1, "M0S02_PickupKatana", "map0_s02.there_is_a_katana_take")
end

-- index 6: channeling stone (message map0_s02.there_is_a_channeling_stone).
function MapEvent_ChannelingStoneItemTake()
    sh.event_item_take("ChannelingStone", 1, "M0S02_PickupChannelingStone", "map0_s02.there_is_a_channeling_stone")
end
