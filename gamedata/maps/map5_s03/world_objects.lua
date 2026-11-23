-- World objects of map5_s03: once the shelf has been pushed (flag
-- 389), the shelf stands at its pushed spot whenever the player is in its cell. The original wrote the
-- z only there, and the write stays for the rest of the visit; the push scene of events.lua moves the
-- same object through the map variable "shelf".

local CELL = 163840 -- 40 m
local PUSHED_Z = 0x3C720
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
    local x, _, z = sh.player_pos()
    -- Positive positions only: the cell is x 2, z 1 (truncation = floor there).
    if x > 0 and x // CELL == 2 and z > 0 and z // CELL == 1 and sh.flag_get("M5S03_ShelfPushed") then
        local sx, sy = sh.world_object.position_get("Shelf")
        sh.world_object.position_set("Shelf", sx, sy, PUSHED_Z)
    end

    -- The collision rule: the shelf's collision is on in
    -- its cell until it has been pushed.
    local flags = 0
    if in_chunk(x, 1, 3, -1, 3) and in_chunk(z, 1, 2, -1, 2) then
        if not sh.flag_get("M5S03_ShelfPushed") then
            flags = COLLISION_OBJECTS
        end
    end
    sh.collision.flag_bits_clear(COLLISION_ALL)
    sh.collision.flag_bits_set(flags)
end
