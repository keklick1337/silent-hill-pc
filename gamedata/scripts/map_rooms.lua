-- Room rules several maps share (the "zones.rule" of gamedata/maps/<map>/rooms.json). The engine
-- runs this file in each map's hook state after scripts/map_hooks.lua, before the map's hooks.lua.
--
-- A rule is rule(x, z) with Q12 world coordinates. With "zones.table" in rooms.json it returns a
-- cell of that table (-1: none), else the room itself (0: none); either way 0 falls back to the
-- map's room grid. Ranges are inclusive at both ends, as the game tests them. Sums wrap to 32 bits
-- like the game's integer arithmetic.

-- Whole or half metres in Q12 (exact in float, so no rounding question).
local function Q(m)
    return math.tointeger(m * 4096)
end

local function s32(v)
    v = v & 0xFFFFFFFF
    if v >= 0x80000000 then
        v = v - 0x100000000
    end
    return v
end

-- lo <= v + offset <= hi
local function band(v, offset, lo, hi)
    local w = s32(v + offset)
    return lo <= w and w <= hi
end

-- The Q12 square of a Q12 value: 64-bit product, back to Q12, cut to 32 bits.
local function sq(v)
    return s32((v * v) >> 12)
end

-- ---- the town street crossings (map0_s00, map0_s01, map2_s00, map2_s03): table street_rooms, 5 x 5 ----

local function street_column(x)
    if band(x, Q(240), Q(-24), Q(24)) then
        return 1
    elseif band(x, Q(120), Q(-24), Q(24)) then
        return 2
    elseif band(x, Q(16), 0, Q(40)) then
        return 3
    elseif band(x, Q(-120), Q(-24), Q(24)) then
        return 4
    end
    return 0
end

local function street_row(x, z)
    if band(z, Q(202), Q(-18), Q(18)) then
        return 1
    elseif band(z, Q(79), Q(-15), Q(15)) and x < Q(160) then
        return 2
    elseif band(z, Q(16), 0, Q(32)) then
        return 3
    elseif band(z, Q(-124), Q(-20), Q(20)) then
        return 4
    end
    return 0
end

function rooms_town_streets(x, z)
    if x < Q(-320) or x >= Q(240) or z < Q(-240) or z >= Q(200) then
        return -1
    end
    return street_column(x) * 5 + street_row(x, z)
end

-- ---- the street blocks (map2_s02, map4_s00, map4_s02, map4_s03, map4_s05, map4_s06): table block_rooms, 3 x 3 ----

function rooms_street_blocks(x, z)
    local column = 0
    if band(x, Q(120), Q(-14), Q(14)) and z < Q(40) then
        column = 1
    elseif band(x, Q(60), Q(-9), Q(9)) and band(z, Q(40), Q(-34), Q(34)) then
        column = 2
    elseif band(x, 0, Q(-14), Q(14)) and z > Q(-80) then
        column = 3
    end

    local row = 0
    if not band(x, Q(40), Q(-120), Q(120)) then
        row = 0
    elseif band(z, Q(80), Q(-14), Q(14)) then
        row = 1
    elseif band(z, 0, Q(-14), Q(14)) then
        row = 2
    end
    return column * 3 + row
end

-- ---- map5_s01: table block_rooms ----

function rooms_map5_s01(x, z)
    local column = 0
    if band(x, Q(38.5), Q(-16.5), Q(16.5)) and z > Q(-160) and
        (z < Q(-120) or (z < Q(-24) and x < Q(-32)) or band(x, Q(40), Q(-12), Q(12))) then
        column = 1
    elseif band(x, Q(-38), Q(-6), Q(6)) and z < Q(-40) then
        column = 2
    end

    local row = 0
    if band(z, Q(163), Q(-18), Q(18)) then
        row = 1
    elseif x > Q(-40) then
        if x > Q(40) then
            if band(z, Q(36), Q(-16), Q(16)) then
                row = 2
            end
        elseif band(z, Q(43.5), Q(-12), Q(12)) then
            row = 2
        end
    end
    return column * 3 + row
end

-- ---- map5_s00: the room ----

function rooms_map5_s00(x, z)
    if x >= Q(-40) then
        if band(x, 0, Q(16), Q(24)) and z > Q(-8) then
            return z < Q(48) and 2 or 1
        elseif band(z, Q(12), Q(-4), Q(4)) and band(x, Q(-40), Q(-8), Q(8)) then
            return 3
        elseif band(x, 0, Q(48), Q(56)) and band(z, Q(40), Q(-8), Q(8)) then
            return 4
        end
    elseif band(x, 0, Q(-64), Q(-56)) then
        if z > Q(-8) then
            return 9
        elseif band(z, Q(52), Q(-36), Q(36)) then
            return 8
        end
    elseif x > Q(-64) and z < Q(-88) then
        return 10
    elseif band(z, 0, Q(-94), Q(-90)) and x > Q(-89) then
        return 11
    elseif band(x, 0, Q(-96), Q(-88)) and band(z, Q(46), Q(-30), Q(30)) then
        return 13
    end
    return 0
end

-- ---- map6_s00, map6_s02: the room (the lower floor splits on the ground height) ----

function rooms_map6_s00(x, z)
    -- The game tests x + 192 m as unsigned against 8 m: outside [0, 8 m) counts.
    local gap = s32(x + Q(192))
    if band(z, Q(-127), Q(-17), Q(17)) and (gap < 0 or gap > Q(8) - 1 or z < Q(130)) and
        (x < Q(-166) or z > Q(112)) then
        return 3
    elseif band(z, Q(-8.5), Q(-31.5), Q(31.5)) and x < Q(-159.5) then
        return 16
    elseif z < 0 and x < Q(-80) then
        local ground = sh.world.ground_height(x, z)
        if x < Q(-160) then
            return ground > Q(1) and 17 or 18
        elseif x >= Q(-148) then
            return ground > Q(1) and 18 or 19
        end
        return 18
    end
    return 0
end

-- ---- map6_s04, map6_s05: the room (round areas; everything else is room 1) ----

function rooms_map6_s04(x, z)
    if z > Q(80) then
        return 5
    elseif s32(sq(s32(x + Q(16))) + sq(s32(z + Q(64)))) < Q(18 * 18) then
        return 3
    elseif s32(sq(s32(x - Q(14))) + sq(s32(z + Q(28)))) < Q(18 * 18) then
        return 2
    elseif s32(sq(s32(x + Q(14))) + sq(s32(z + Q(28)))) < Q(5 * 5) then
        return 4
    end
    return 1
end

-- ---- map6_s03: the room (bands along x) ----

function rooms_map6_s03(x, z)
    if x < Q(27) then
        if x > Q(13) then
            return 17
        elseif x > Q(-13) then
            return 16
        end
        return 15
    elseif x < Q(56) then
        return 18
    elseif x < Q(72) then
        return 19
    end
    return 20
end
