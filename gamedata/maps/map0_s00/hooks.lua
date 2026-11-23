-- Engine hooks of map0_s00 (loaded with the map, before any of its code runs; see scripts/map_hooks.lua).

-- The flags that each add one music layer (layer 1..6), while Harry is alive and the alley chase
-- has not ended.
local LAYER_FLAGS = { "M0S00_CherylFmvSeen", "M0S00_LastAlleyMusicStep1", "M0S00_LastAlleyMusicStep3", "M0S00_LastAlleyMusicStep4", "M0S00_LastAlleyMusicStep2", "M0S00_AlleyNightmareStarted" }

-- Music: the layer mix (the same on entry).
function bgm(init)
    local fade = 409
    local flags = 256
    if sh.npc.health(0) > 0 and not (sh.flag_get("M0S00_LastAlleyMusicStep4") and not sh.flag_get("M0S00_LastAlleyMusicStep1")) then
        for i = 1, 6 do
            if sh.flag_get(LAYER_FLAGS[i]) then
                flags = flags | (1 << i)
            end
        end
    else
        sh.flag_set("M0S00_LastAlleyMusicStep1", false)
        fade = 1228
    end
    map_music.mix(flags, fade, map_music.FULL)
end
