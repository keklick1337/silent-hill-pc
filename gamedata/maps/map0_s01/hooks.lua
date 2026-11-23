-- Engine hooks of map0_s01 (loaded with the map, before any of its code runs; see scripts/map_hooks.lua).

-- The track on entry: the room theme until the cafe scene, the Air Screamer fight after it, the
-- town theme once the Air Screamer is dead.
local function track()
    local t
    if not sh.flag_get("M0S01_AirScreamerDied") then
        t = sh.flag_get("M0S01_CafeCutsceneDone") and 30 or 9
    else
        t = 6
    end
    sh.sound.bgm_change(t)
end

-- The per-tick layer mix. Fade speeds: 0.333 -> 1363, 0.125 -> 512, 240.0 -> 983040.
local function mix()
    local fade = 1363
    local flags
    if not sh.flag_get("M2S00_CafeExitCutsceneSeen") then
        if not (sh.flag_get("M0S01_AirScreamerDied") or sh.flag_get("M0S01_CafeCutsceneDone")) then
            fade = 983040
            -- FMV has just played while the room track is on.
            if sh.flag_get("M0S01_FmvStarted") and sh.game.bgm_idx() == 9 then
                flags = 2
            else
                flags = 1 | 512
            end
        elseif sh.flag_get("M0S01_AirScreamerFightStarted") then
            if not sh.flag_get("M0S01_AirScreamerDied") then
                fade = 983040
                flags = 256
            else
                fade = 512
                flags = 1 | 512
            end
        else
            flags = 1
        end
    else
        flags = 1
    end
    sh.music.update(flags, fade)
end

bgm = map_music.pair(track, mix)
