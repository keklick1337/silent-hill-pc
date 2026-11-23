-- World objects of map6_s01: the parts of the lighthouse door are hidden
-- while the map variable lighthouse_door_hidden is set (events.lua sets and clears it); otherwise the data
-- decides (their cell).

local DOOR_PARTS = {"Door", "DoorBrick", "DoorBar", "DoorWide1", "DoorWide2", "DoorGlass", "DoorRight"}

-- The flag lives outside the map's reset data: the original cleared it when the map loads.
sh.map.set("lighthouse_door_hidden", 0)

function update(dt)
    local hidden = sh.map.get("lighthouse_door_hidden") ~= 0
    for _, name in ipairs(DOOR_PARTS) do
        if hidden then
            sh.world_object.visible_set(name, false)
        else
            sh.world_object.visible_set(name, nil)
        end
    end
end
