-- Engine hooks of map1_s05 (loaded with the map, before any of its code runs; see scripts/map_hooks.lua).

-- Music: layer 0 once the Split Head (NPC slot 1) is dead, layer 1 while it lives, layer 2 on top
-- while its state flag 4 is set (the same on entry).
function bgm(init)
    local flags = 1
    if sh.npc.health(1) > 0 then
        flags = 2
        if sh.npc.split_head_flags(1) & 16 ~= 0 then
            flags = 6
        end
    end
    map_music.mix(flags, 409, map_music.FULL)
end
