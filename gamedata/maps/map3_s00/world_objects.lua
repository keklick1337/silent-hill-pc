-- Per-tick rule of map3_s00: the
-- one-shot raven cry from the trunk while Harry walks the west path below x = 24 m
-- with M3S00_WestPathSoundPlayed still clear. Every tick in the region rolls rand16 % Q12(3.0) against
-- sin(|24 m - x| / 3): the closer to the trunk, the likelier the single cry that
-- sets M3S00_WestPathSoundPlayed.

local CELL = 163840 -- 40 m

-- PLAYER_IN_MAP_CHUNK: the C division truncates toward zero.
local function in_chunk(p, x0, x1, x2, x3)
    local c = p // CELL
    if p < 0 and c * CELL ~= p then
        c = c + 1
    end
    return (p > 0 and c + x0 == x1) or (p <= 0 and c + x2 == x3)
end

-- Q12(24.0), Q12(3.0) and the sound point (22.5, -1.5, 137.0) m in Q12; Q8(7/8) volume;
-- SfxFlag_NoDistAtten (Sfx_Unk1492).
local X_LIMIT, SPAN = 98304, 12288
local SOUND_X, SOUND_Y, SOUND_Z = 92160, -6144, 561152
local VOL, NO_DIST_ATTEN = 224, 2

function update(dt)
    local px, _, pz = sh.player_pos()
    if not (in_chunk(px, 0, 0, -1, 1) and in_chunk(pz, 1, 4, -1, 4)) then
        return
    end
    if sh.flag_get("M3S00_WestPathSoundPlayed") or px >= X_LIMIT then
        return
    end
    local rand0 = sh.rng.rand16() % SPAN
    -- x = |24 m - px| / 3 of the original; the dividend is positive here and the C
    -- division truncates toward zero, exactly like //.
    local x = (X_LIMIT - px) // 3
    if rand0 < sh.math.sin(x) then
        sh.sfx_flags_play("kaufmann_shot", SOUND_X, SOUND_Y, SOUND_Z, VOL, NO_DIST_ATTEN)
        sh.flag_set("M3S00_WestPathSoundPlayed", true)
    end
end
