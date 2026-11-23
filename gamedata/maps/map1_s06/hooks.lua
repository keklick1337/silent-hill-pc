-- Engine hooks of map1_s06 (loaded with the map, before any of its code runs; see scripts/map_hooks.lua).

-- Music: layers 1..7 once M1S06_AfterBossFmvSeen is set, layer 0 before (the same on entry).
function bgm(init)
    map_music.mix(sh.flag_get("M1S06_AfterBossFmvSeen") and 0xFE or 1, 409, map_music.FULL)
end
