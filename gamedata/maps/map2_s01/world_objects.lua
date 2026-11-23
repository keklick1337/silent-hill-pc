-- World objects script of map2_s01: the altar
-- camera hold and the Balkan Church map mark.
-- While the drawbridge key is not taken and Harry stands at the altar, the camera holds
-- on the altar; stepping away hands it back to the auto camera (the wider leave window
-- of the original: 1.2004 m in x, 1.0004 m in z). Standing in the altar cell sets the
-- map-mark flag while the key is still there and clears it once it is taken.

local CELL = 163840 -- 40 m

-- Set-up when the map loads (the start of Map_WorldObjectsInit, before the objects): the map effects 1, 1.
sh.map_effects_init(1, 1)

-- PLAYER_IN_MAP_CHUNK: the C division truncates toward zero.
local function in_chunk(p, x0, x1, x2, x3)
    local c = p // CELL
    if p < 0 and c * CELL ~= p then
        c = c + 1
    end
    return (p > 0 and c + x0 == x1) or (p <= 0 and c + x2 == x3)
end

-- PLAYER_NEAR_POS on the Q12 position: the >= 0 side tests p - base < tol, the < 0 side
-- base - p < tol (strictly, exactly the original's two comparisons).
local function near(p, baseQ12, tolQ12)
    local d = p - baseQ12
    if d >= 0 then
        return d < tolQ12
    end
    return (baseQ12 - p) < tolQ12
end

-- Q12(20.0), Q12(0.7), Q12(23.9), Q12(0.5), Q12(1.2004), Q12(1.0004): the float literals
-- of the original truncate to exactly these.
local ALTAR_X, ALTAR_X_TOL = 81920, 2867
local ALTAR_Z, ALTAR_Z_TOL = 97894, 2048
local LEAVE_X_TOL, LEAVE_Z_TOL = 4918, 4097

local holding = false

function update(dt)
    local px, _, pz = sh.player_pos()

    -- The map mark follows the key while Harry is in the altar cell
    -- (x chunk 0 on the positive side / 2 on the other, same for z).
    if in_chunk(px, 0, 0, -1, 1) and in_chunk(pz, 0, 0, -1, 1) then
        sh.flag_set("MapMark_OldTown_BalkanChurchCheck",
                    sh.flag_get("M2S01_PickupDrawbridgeKey"))
    end

    if not holding then
        if not sh.flag_get("M2S01_PickupDrawbridgeKey") and
            near(px, ALTAR_X, ALTAR_X_TOL) and near(pz, ALTAR_Z, ALTAR_Z_TOL) then
            -- Metres: the host's *4096 conversion truncates to the original's Q12 values.
            sh.camera_pos(19.57, -2.91, 23.91, 0, 0, 0, 0, 0, 0, 0, true)
            sh.camera_look(20.69, 0.92, 24.22, 0, 0, 0, 0, 0, 0, 0, true)
            holding = true
        end
    else
        if not (near(px, ALTAR_X, LEAVE_X_TOL) and near(pz, ALTAR_Z, LEAVE_Z_TOL)) then
            sh.camera_auto_return(true)
            holding = false
        end
    end
end
