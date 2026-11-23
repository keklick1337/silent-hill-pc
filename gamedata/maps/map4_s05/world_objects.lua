-- Per-tick rules of map4_s05: collision trigger bits by the player's 40 m
-- cell, the map-effects init once on entering the z cell 2, the TownStairwellEntryTriggered reset and the two map marks.
-- The original's chunk tests only ever match positive z cells 2, 5, 6 and the x cell -2 (x <= 0, trunc).

local CELL = 163840
local effects_done = false -- reset when the map loads

local function trunc_cell(v)
    if v >= 0 then return v // CELL end
    return -((-v) // CELL)
end

function update(dt)
    local x, _, z = sh.player_pos()
    sh.collision.flag_bits_clear(0xFFFF)
    local zc = (z > 0) and (z // CELL) or nil
    if zc == 2 then
        if not effects_done then
            sh.map_effects_init(6, 12)
            effects_done = true
        end
        if sh.flag_get("M4S05_CybilStairsSceneWarpDone") then
            sh.collision.flag_bits_set(4)
        else
            sh.collision.flag_bits_set(2)
        end
    elseif zc == 5 then
        if sh.flag_get("TownStairwellEntryTriggered") then
            sh.collision.flag_bits_set(4)
            if z < 868352 then -- Q12(212)
                sh.flag_set("TownStairwellEntryTriggered", false)
            end
        else
            sh.collision.flag_bits_set(2)
        end
    else
        if x <= 0 and trunc_cell(x) == -2 and zc == 4 then
            sh.flag_set("MapMark_AltCentralTown_KoontzStBigCross", true)
            sh.flag_set("MapMark_AltCentralTown_CrichtonKoontzBigCross", true)
        end
        sh.collision.flag_bits_clear(0xFFFF)
    end
end
