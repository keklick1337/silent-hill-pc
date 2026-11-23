-- Engine hooks of map6_s03 (loaded with the map, before any of its code runs; see scripts/map_hooks.lua).

-- Layer limits; layers 5..7 are rewritten by the player's distance in rooms 15..20.
local limits = { 128, 128, 255, 96, 96, 80, 80, 80 }
local ROOM_FLAGS = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 14, 22, 510, 510, 510, 510, 0 }
local SOURCE_X, SOURCE_Z = 94208, 81920 -- room 17's sound source, (23, 20)
local CAP = 80 -- Q8(0.313)

-- CAP minus the distance times k (Q12), at least 0; a negative distance is the full CAP.
local function fall(d, k)
    if d < 0 then
        return CAP
    end
    d = CAP - (d * k) // 4096
    return d < 0 and 0 or d
end

-- Music: the per-room layer mix (the same on entry).
function bgm(init)
    local r = map_music.room()
    local fade = 0x400
    local flags
    if sh.flag_get("M6S03_SewerFloorChanged") then
        flags = 1
        if sh.flag_get("M6S03_HatchClimbUpStarted") then
            fade = 0x547
        else
            flags = map_music.at(ROOM_FLAGS, r)
            if r == 15 or r == 16 then
                limits[6], limits[7], limits[8] = 0, 0, 0
            elseif r == 17 then
                local x, _, z = sh.player_pos()
                local d = sh.math.distance_2d(x, z, SOURCE_X, SOURCE_Z)
                if d < 20480 then
                    d = CAP
                else
                    d = fall(d - 20480, 3)
                end
                limits[6], limits[7], limits[8] = d & 0xFF, 0, 0
            elseif r >= 18 and r <= 20 then
                local x = sh.player_pos()
                limits[6] = CAP
                limits[7] = fall(262144 - x, 3) & 0xFF
                limits[8] = fall(327680 - x, 5) & 0xFF
            end
        end
    else
        flags = 2 | 4
        if not sh.flag_get("M6S03_HatchDescendCutsceneDone") then
            fade = 0x547
            flags = 2
        end
    end
    map_music.mix(flags, fade, limits)
end
