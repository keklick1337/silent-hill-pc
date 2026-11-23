-- World objects of map4_s01. The cabinet stands 0.935 m further in
-- (z 62.7466) until M4S01_CabinetPushed; the cabinet scene of events.lua pushes it through the map variable "cabinet",
-- which is this object. The cube is drawn with one of its two models, chosen by the map variable
-- cube_model_variant (events.lua sets it).

if not sh.flag_get("M4S01_CabinetPushed") then
    local x, y = sh.world_object.position_get("Cabinet")
    sh.world_object.position_set("Cabinet", x, y, 257010) -- Q12(62.7466)
end

local CUBES = {[0] = "Cube", [1] = "CubeTurned"}

function update(dt)
    local variant = sh.map.get("cube_model_variant")
    for i, name in pairs(CUBES) do
        if i == variant then
            sh.world_object.visible_set(name, nil)
        else
            sh.world_object.visible_set(name, false)
        end
    end
end

-- Per-tick collision rule: inside the cell x 4, z 1 (40 m
-- cells, positive coordinates only in effect) the object collision group stays enabled until M4S01_CabinetPushed:
-- it holds the cabinet's shapes at its first place. The map geometry group (1) is always on, so setting it
-- changes nothing.
local CELL = 163840
local COLLISION_ALL, COLLISION_OBJECTS = 0xFFFF, 2
local update_cubes = update
function update(dt)
    update_cubes(dt)
    local x, _, z = sh.player_pos()
    if x > 0 and x // CELL == 4 and z > 0 and z // CELL == 1 then
        if sh.flag_get("M4S01_CabinetPushed") then
            sh.collision.flag_bits_clear(COLLISION_ALL)
        else
            sh.collision.flag_bits_set(COLLISION_OBJECTS)
        end
    else
        sh.collision.flag_bits_clear(COLLISION_ALL)
    end
end

-- After the objects are set up (the end of Map_WorldObjectsInit): the cube shows its first model.
function post_init()
    sh.map.set("cube_model_variant", 0)
end
