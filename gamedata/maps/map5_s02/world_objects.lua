-- World objects of map5_s02. Once the safe key was used, the safe
-- door stands open (-7.5 degrees) from the map's load on; the safe scenes of events.lua turn it through the
-- map variable safe_door. Kaufmann's bag is never drawn by the map: only the bar scene of events.lua draws
-- it, through the map variable kaufmann_bag.

local SAFE_DOOR_OPEN = -85 -- Q12_ANGLE(-7.5)

if sh.flag_get("M5S02_UsedSafeKey") then
    local rx, _, rz = sh.world_object.rotation_get("SafeDoor")
    sh.world_object.rotation_set("SafeDoor", rx, SAFE_DOOR_OPEN, rz)
end

sh.world_object.visible_set("KaufmannBag", false)

-- Per-tick collision rule: the objects' collision of
-- the cell x 3, z 1 is on once flag M5S02_BoxPushed is set.
local CELL = 163840 -- 40 m
local COLLISION_ALL = 0xFFFF
local COLLISION_OBJECTS = 2 -- CollisionTriggerFlag_1

-- The map's chunk test (PLAYER_IN_MAP_CHUNK): the index truncates toward zero; the offsets differ by sign.
local function in_chunk(p, off_pos, want_pos, off_neg, want_neg)
    local idx = p // CELL
    if p < 0 and idx * CELL ~= p then
        idx = idx + 1
    end
    if p > 0 then
        return idx + off_pos == want_pos
    end
    return idx + off_neg == want_neg
end

function update(dt)
    sh.collision.flag_bits_clear(COLLISION_ALL)
    local x, _, z = sh.player_pos()
    if in_chunk(x, 1, 4, -1, 4) and in_chunk(z, 1, 2, -1, 2) and sh.flag_get("M5S02_BoxPushed") then
        sh.collision.flag_bits_set(COLLISION_OBJECTS)
    end
end
