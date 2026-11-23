-- Per-tick rule of map2_s04: the map effects 7/4 once on entering the
-- chunk x -2 / z 1, re-armed on leaving it.

-- The map's chunk test (PLAYER_IN_MAP_CHUNK): the index truncates toward zero; the offsets differ by sign.
local CELL = 163840 -- 40 m
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

local fired = false

function update(dt)
    local x, _, z = sh.player_pos()
    if in_chunk(x, 1, -3, -1, -3) and in_chunk(z, 1, 2, -1, 2) then
        if not fired then
            sh.world.effects_update(7, 4)
            fired = true
        end
    else
        fired = false
    end
end
