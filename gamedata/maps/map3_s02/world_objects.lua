-- Per-tick rules of map3_s02: the two
-- one-shot machine-room cries of Sfx_Unk1523, and
-- post_draw(): the fog band, called by the overlay after the objects are drawn (a pre-draw
-- fog write would lead the original's post-draw one, section 10.5).

local CELL = 163840 -- 40 m

-- PLAYER_IN_MAP_CHUNK: the C division truncates toward zero.
local function in_chunk(p, x0, x1, x2, x3)
    local c = p // CELL
    if p < 0 and c * CELL ~= p then
        c = c + 1
    end
    return (p > 0 and c + x0 == x1) or (p <= 0 and c + x2 == x3)
end

-- Q12(-129.0), Q12(67.0): the float literals of the original truncate to exactly these.
-- The first cry sounds at (100.0, -1.2, -127.2) m, the second at (69.6, -1.2, -138.4) m.
local Z_LIMIT, X_LIMIT = -528384, 274432
local SND_A = {409600, -4915, -521011}
local SND_B = {285081, -4915, -566886}

function update(dt)
    local px, _, pz = sh.player_pos()

    if in_chunk(px, 1, 3, -1, 3) and in_chunk(pz, 1, -4, -1, -4) then
        if not sh.flag_get("M3S02_CorridorDoorNoiseHeard") and pz < Z_LIMIT then
            sh.flag_set("M3S02_CorridorDoorNoiseHeard", true)
            sh.sfx_flags_play("darkness_fall", SND_A[1], SND_A[2], SND_A[3], 128, 0)
        end
    end

    if in_chunk(px, 1, 2, -1, 2) and in_chunk(pz, 1, -4, -1, -4) then
        if not sh.flag_get("M3S02_DarkRoomDoorNoiseHeard") and px < X_LIMIT then
            sh.flag_set("M3S02_DarkRoomDoorNoiseHeard", true)
            sh.sfx_flags_play("darkness_fall", SND_B[1], SND_B[2], SND_B[3], 128, 0)
        end
    end
end

local function s32(v)
    v = v & 0xFFFFFFFF
    return v >= 0x80000000 and v - 0x100000000 or v
end

-- Q12(95.2), Q12(140.0), Q12(4.8) + 1, Q12(12.8) + 1: float literals truncated as the original does.
local CX, CZ, DIR_X, DIR_Z = 389939, 573440, 19661, 52429

-- Math_AngleNormalizeSigned
local function angle_signed(a)
    a = a & 0xFFF
    return a > 2048 and a - 4096 or a
end

-- C division truncates toward zero.
local function cdiv(a, b)
    local q = a // b
    if q < 0 and q * b ~= a then
        q = q + 1
    end
    return q
end

-- Brightness falloff along the corridor, Q8 (4096 = 16.0): 16.0 at the far end, less toward the near one.
local function falloff()
    local px, _, pz = sh.player_pos()
    local x = px - CX
    local z = pz + CZ
    local cos0 = sh.math.cos(angle_signed(sh.math.atan2(x, z) - sh.math.atan2(DIR_X, DIR_Z)))
    local x6, z6 = x >> 6, z >> 6
    local dist = sh.math.sqrt(s32(x6 * x6 + z6 * z6)) << 6
    local along = s32(cos0 * dist) >> 12
    local fraction = cdiv(s32(along << 12), sh.math.sqrt(0xBAC52) << 6)
    if math.min(fraction, 4096) >= 0 then
        return 4096 - math.min(fraction, 4096)
    end
    return 4096
end

-- The fog band is on from the map's load (Map_WorldObjectsInit); the corridor scene of events.lua turns
-- it off.
sh.map.set("fog_effect_active", 1)

function post_draw()
    local px, _, pz = sh.player_pos()
    local active = sh.map.get("fog_effect_active") ~= 0
    local level = sh.map.get("fog_effect_level")
    if active and in_chunk(px, 1, 4, -1, 4) and in_chunk(pz, 1, -4, -1, -4) then
        level = math.max(level, 40960) -- Q12(10.0)
    end
    if in_chunk(px, 1, 3, -1, 3) and in_chunk(pz, 1, -4, -1, -4) and active then
        level = math.max(level, ((falloff() * 98304) >> 12) + 102400) -- Q12_MULT_PRECISE(.., Q12(24.0)) + Q12(25.0)
    end
    sh.map.set("fog_effect_level", level)
end
