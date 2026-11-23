-- Engine hooks of map6_s01 (loaded with the map, before any of its code runs; see scripts/map_hooks.lua).

-- Music: track 23 on entry until M6S01_CybilDahliaCutsceneDone (or while M6S01_BoatReturnedAfterLighthouse is set without M6S01_CybilLeavesCutsceneDone), track 20 after;
-- the per-room layer mix is the one map0_s02 uses.
local function track()
    if not sh.flag_get("M6S01_CybilDahliaCutsceneDone") or (sh.flag_get("M6S01_BoatReturnedAfterLighthouse") and not sh.flag_get("M6S01_CybilLeavesCutsceneDone")) then
        sh.sound.bgm_change(23)
    else
        sh.sound.bgm_change(20)
    end
end

bgm = map_music.pair(track, map_music.map0_s02_mix)
