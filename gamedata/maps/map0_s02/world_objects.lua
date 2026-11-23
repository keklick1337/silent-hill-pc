-- World objects script of map0_s02: the two map-effect triggers of
-- the map's set-up and per-tick update.

-- On entry: effects 7/4 when flag M2S00_DarknessFell is set and flag M1S05_SplitHeadKilled is not.
if not sh.flag_get("M1S05_SplitHeadKilled") and sh.flag_get("M2S00_DarknessFell") then
    sh.world.effects_update(7, 4)
end

local fired = false

function update(dt)
    if fired then
        return
    end
    local x, _, z = sh.player_pos()
    -- Original chunk test: x chunk -3 (x <= 0, truncating division by 40 m), z chunk 1 (z > 0).
    if x <= 0 and x > -655360 and x <= -491520 and z >= 163840 and z < 327680 then
        if sh.flag_get("M1S06_WokeUpAfterBoss") or not sh.flag_get("M2S00_DarknessFell") then
            sh.world.effects_update(13, 13)
            fired = true
        end
    end
end
