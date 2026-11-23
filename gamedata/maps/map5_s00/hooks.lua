-- Engine hooks of map5_s00 (loaded with the map, before any of its code runs; see scripts/map_hooks.lua).

-- Music: the same on entry and per tick (the map's music event ignores init). The room's layer
-- flags, plus layers 1, 3 and 5 after the sewer scene (M5S00_DrainKeyAmbushCutsceneDone), fade 0.25.

-- Layer flags per room index (0..21), from the map's data.
local ROOM_FLAGS = { 0, 6, 2, 12, 6, 36, 6, 12, 32, 2, 6, 8, 20, 12, 36, 0, 0, 0, 0, 0, 0, 0 }
local LIMITS = { 128, 128, 255, 128, 128, 128, 0, 0 }

function bgm(init)
    local flags = map_music.at(ROOM_FLAGS, map_music.room())
    if map_music.flag("M5S00_DrainKeyAmbushCutsceneDone") then
        flags = flags | 42 -- layers 5, 3, 1
    end
    map_music.mix(flags, 1024, LIMITS)
end
