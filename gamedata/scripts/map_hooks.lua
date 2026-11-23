-- Functions several maps share, for their gamedata/maps/<map>/hooks.lua. The engine runs this file
-- first in each map's hook state, then the map's hooks.lua; a new state is made for every visit of
-- a map, so the locals here (tables a map rewrites as it plays) start from the data each time.
--
-- Music (the hook bgm(init)): init 1 when the room's track is chosen, 0 for the per-tick mix.
-- Layer flags: layer N = 1 << N (0..7), keep alive = 256, mute all = 512. Fade speeds are Q12 per
-- second: 0.1 -> 409, 0.125 -> 512, 0.15 -> 614, 0.2 -> 819, 0.25 -> 1024, 0.3 -> 1228,
-- 0.5 -> 2048, 1 -> 4096, 240 -> 983040 (instant). Layer limits are eight volumes 0..255.

map_music = {}

local function flag(name)
    return sh.flag_get(name)
end

local function room()
    return sh.save.map_room_idx()
end

-- Element i (from 0) of a per-room table; rooms past its end read 0.
local function at(t, i)
    return t[i + 1] or 0
end

-- C division (truncates toward zero).
local function tdiv(a, b)
    local q = a // b
    if q < 0 and q * b ~= a then
        q = q + 1
    end
    return q
end

-- A 32-bit signed wrap, as the game's counters.
local function s32(v)
    v = v & 0xFFFFFFFF
    if v >= 0x80000000 then
        v = v - 0x100000000
    end
    return v
end

local function mix(flags, fade, limits)
    if limits ~= nil then
        sh.music.update(flags, fade, limits[1], limits[2], limits[3], limits[4], limits[5], limits[6], limits[7],
            limits[8])
    else
        sh.music.update(flags, fade)
    end
end

map_music.flag = flag
map_music.room = room
map_music.at = at
map_music.tdiv = tdiv
map_music.mix = mix

local FULL = { 128, 128, 128, 128, 128, 128, 128, 128 }
map_music.FULL = FULL

-- The track (CondTrue) / mix (CondFalse) pair: a map's bgm from its two halves.
function map_music.pair(track, mix_fn)
    return function(init)
        if init ~= 0 then
            track()
        else
            mix_fn()
        end
    end
end

-- ---- map0_s02, map2_s01, map2_s04, map4_s01, map5_s02, map5_s03 (the mix also map6_s01) ----

function map_music.map0_s02_track()
    local r = room()
    if r == 5 then
        sh.sound.bgm_change(6)
    elseif r == 18 then
        sh.sound.bgm_change(flag("M5S02_KaufmannBarFightCutsceneDone") and 7 or 29)
    elseif r == 17 then
        sh.sound.bgm_change(7)
    end
end

local A_ROOM = FULL
local A_DARK = { 128, 128, 128, 0, 128, 0, 0, 0 }
local A_STAIRS = { 128, 96, 96, 96, 0, 0, 0, 0 }
local A_HALL = { 112, 96, 112, 128, 0, 0, 0, 0 }
local A_TRACK7 = { 112, 96, 112, 128, 0, 0, 0, 0 }
local A_OTHER = { 96, 0, 0, 0, 0, 0, 0, 0 }
local A_WARD = { 112, 96, 112, 128, 0, 0, 0, 0 }
local A_TRACK23 = { 112, 96, 112, 128, 128, 0, 0, 0 }
local A_END = { 112, 96, 112, 128, 128, 0, 0, 0 }

function map_music.map0_s02_mix()
    local fade = 409
    local limits = A_ROOM
    local flags
    local r = room()
    local track = sh.game.bgm_idx()
    if r == 1 then
        fade = 983040
        flags = flag("M2S01_DahliaCutsceneStarted") and 0xFE or (1 | 512)
    elseif r == 2 or r == 3 or r == 4 then
        flags = (sh.world.effects_flags() & 2) ~= 0 and 2 or 1
        limits = A_DARK
    elseif r == 10 or r == 12 or r == 13 then
        fade = 1024
        limits = A_STAIRS
        if flag("M4S01_HoleDescentStarted") then
            flags = 4 | 8
        elseif not flag("M4S01_AntiqueShopCybilCutsceneDone") then
            if flag("M4S01_CybilWalksToHarry") then
                fade = 512
                flags = 2
            else
                fade = 983040
                flags = 1 | 512
            end
        else
            flags = 8
        end
    elseif r == 11 then
        flags = 1 | 512
    elseif r == 17 then
        limits = A_HALL
        flags = 1 | 512
    elseif r == 18 then
        limits = (track == 7) and A_TRACK7 or A_OTHER
        if flag("M5S02_KaufmannBarFightCutsceneDone") then
            if flag("M5S02_BarFightInProgress") then
                fade = 983040
                sh.flag_set("M5S02_BarFightInProgress", false)
            else
                fade = 512
            end
            flags = 1 | 256 | 512
        elseif flag("M5S02_BarFightInProgress") then
            fade = 983040
            flags = 0xFE | 256
        else
            fade = flag("M5S02_AnniesBarFirstEntered") and 1024 or 512
            flags = 1 | 256 | 512
        end
    elseif r >= 6 and r <= 9 then
        if track == 34 then
            if flag("M5S03_KaufmannHasMotorcycleVial") then
                fade = 983040
                flags = 1 | 512
            elseif flag("M5S03_GarageKaufmannPresent") then
                fade = 983040
                flags = 0xFE
            else
                flags = 1 | 512
            end
        else
            limits = A_WARD
            flags = 1
        end
    elseif r == 5 then
        flags = 1
    elseif r == 19 or r == 20 then
        if track == 23 then
            limits = A_TRACK23
            fade = 1024
            if flag("M6S01_BoatCabinEntered") and not flag("M6S01_CybilDahliaCutsceneDone") then
                flags = flag("M6S01_DahliaAppearedOnBoat") and (2 | 8 | 16) or (2 | 4 | 8)
            elseif flag("M6S01_BoatReturnedAfterLighthouse") and not flag("M6S01_CybilLeavesCutsceneDone") then
                flags = 2 | 4
            else
                flags = 2
            end
        else
            limits = A_END
            flags = 2 | 4 | 16
        end
    else
        flags = 0xFE
    end
    mix(flags, fade, limits)
end

map_music.map0_s02 = map_music.pair(map_music.map0_s02_track, map_music.map0_s02_mix)

-- ---- map1_s00, map1_s01 ----

local B_ROOMS = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 2, 0, 0, 0, 0, 0, 32, 2, 2, 2, 0, 0, 0, 0, 0, 4, 0, 0,
    32, 4, 4, 2, 32, 0, 0, 0, 2, 0, 0 }

function map_music.map1_s00(init)
    local r = room()
    local flags
    if r == 23 then
        flags = 32
        if flag("M1S00_GoldMedallionPlaced") and flag("M1S00_SilverMedallionPlaced") then
            flags = 0x1FE
        end
    else
        flags = at(B_ROOMS, r)
    end
    mix(flags, 409, FULL)
end

-- ---- map1_s02, map1_s03, map1_s04 ----

local C_ROOMS = { 0, 4, 1, 2, 4, 4, 4, 4, 4, 1, 4, 2, 2, 2, 8, 4, 2, 8, 8, 4, 0, 0, 0, 16, 4, 4, 16, 8, 16, 8, 2,
    4, 2, 2, 0, 0, 4, 4, 2, 0, 2, 4 }

function map_music.map1_s02(init)
    local r = room()
    local flags = at(C_ROOMS, r)
    local fade = 409
    if r == 23 then
        flags = 1
        if flag("MapMark_AltSchool1F_CourtyardQuestion") then
            flags = 0x1FE
            if flag("MapMark_AltSchool1F_CourtyardTopBlank") or flag("MapMark_AltSchool1F_CourtyardTopArrows") then
                flags = 16
            end
        end
    elseif r == 38 then
        if flag("M1S03_LockerInspectStarted") and not flag("M1S03_LockerBodySceneDone") then
            flags = 1
            fade = 1228
        end
    elseif r == 5 then
        if flag("M1S02_BathroomDoorSceneStarted") then
            fade = 983040
            if flag("M1S02_BathroomDoorPushedOpen") then
                flags = flags | 8
            else
                flags = 1
            end
        end
    elseif r == 11 then
        sh.flag_set("M1S02_BathroomDoorSceneStarted", false)
    elseif r == 7 then
        if not flag("M1S02_PhoneCallDone") and flag("M1S02_PhoneCallStarted") then
            fade = 983040
            flags = 1
        end
    end
    mix(flags, fade, FULL)
end

-- ---- map2_s00, map2_s03 ----

local D_LIMITS = { 128, 128, 128, 0, 128, 0, 0, 0 }

function map_music.map2_s00(init)
    local r = room()
    local dark = (sh.world.effects_flags() & 2) ~= 0
    local flags = dark and 4 or 2
    if r == 33 then
        if not flag("M3S00_HospitalEntered") and flag("M2S00_DrawbridgeOperated") then
            flags = flags | 16
        end
    elseif r == 37 or r == 38 then
        flags = dark and 2 or 1
    elseif r == 27 or r == 30 then
        flags = flags | 2
    elseif r == 35 then
        flags = 2 | 4
    end
    if flags & 8 ~= 0 then
        D_LIMITS[4] = sh.music.wobble() & 0xFF
    end
    mix(flags, 409, D_LIMITS)
end

-- ---- map2_s02, map4_s00, map4_s06 ----

local E_LIMITS = { 128, 128, 128, 0, 128, 0, 0, 0 }

function map_music.map2_s02(init)
    local r = room()
    local flags = 2
    if r == 17 then
        if flag("M3S06_DahliaCutsceneDone") and not flag("M4S05_CybilAimCutsceneDone") then
            flags = 2 | 4
        end
    elseif r == 12 then
        if not flag("M3S06_DahliaCutsceneDone") then
            flags = 2 | 16
        end
    end
    if flag("M3S06_PickupAntiqueShopKey") and not flag("M2S02_AntiqueShopOpen") then
        flags = flags | 8
    end
    if flags & 8 ~= 0 then
        E_LIMITS[4] = sh.music.wobble() & 0xFF
    end
    mix(flags, 409, E_LIMITS)
end

-- ---- map3_s00, map3_s01, map3_s06 ----

local F_ROOMS = { 0, 4, 4, 20, 4, 4, 0, 0, 4, 4, 20, 28, 4, 4, 4, 4, 36, 36, 0, 0, 20 }

function map_music.map3_s00_track()
    sh.sound.bgm_change(flag("M3S06_DahliaCutsceneDone") and 16 or 25)
end

function map_music.map3_s00_mix()
    local r = room()
    local fade = 614
    local flags
    local track = sh.game.bgm_idx()
    if track == 16 then
        flags = at(F_ROOMS, r)
        if r == 1 then
            if not flag("M3S00_KaufmannCutsceneDone") then
                local _, _, z = sh.player_pos()
                flags = (z > 586547) and 6 or 14 -- Q12(143.2)
            end
        elseif r == 17 or r == 20 then
            if flag("M3S01_GeneratorOn") then
                flags = flags | 0x40
            end
        end
        if flag("MapMark_FogHospital2F_NearElevatorBroken") and flag("MapMark_FogHospital3F_NearElevatorBroken") then
            flags = flags | 0x11E
        end
    elseif track == 34 then
        if not flag("MapMark_FogHospital1F_ExaminationRoomLeftArrows") then
            fade = 983040
            flags = 0x201
        elseif flag("M3S00_KaufmannCutsceneDone") then
            fade = 819
            flags = 0x201
        else
            fade = 983040
            flags = 0x1FE
        end
    elseif track == 25 then
        if not flag("M3S06_DahliaCutsceneDone") then
            fade = 983040
            flags = 0x1FE
        else
            fade = 819
            flags = 0x301
        end
    else
        flags = 1
    end
    mix(flags, fade, nil)
end

map_music.map3_s00 = map_music.pair(map_music.map3_s00_track, map_music.map3_s00_mix)

-- ---- map3_s02, map3_s03, map3_s04, map3_s05, map4_s04 ----

local G_LIMITS = { 128, 128, 128, 128, 128, 96, 96, 0 }
local G_LOBBY = { 0x80, 0x70, 0x80, 0x80, 0, 0, 0, 0 }
local G_ROOMS = { 0, 0, 0, 0, 14, 2, 8, 8, 0, 0, 8, 6, 4, 1, 4, 4, 0, 0, 0, 0, 0, 2, 2, 4, 10, 2, 4, 2, 2, 10, 2,
    4, 24, 40, 48, 4, 4, 4, 2, 2, 2, 2, 2, 6, 16, 4, 16, 8, 2, 10, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0 }

-- Rooms whose visit marks HospitalMusicRoomVisited (room 4 first checks the examination room key).
local G_MARK = {}
for _, r in ipairs({ 4, 6, 7, 10, 11, 12, 13, 14, 15, 21, 22, 24, 27, 28, 29, 30, 31, 32, 34, 35, 36, 39, 40, 41,
    42, 43, 44, 46, 47 }) do
    G_MARK[r] = true
end

function map_music.map3_s02_track()
    sh.sound.bgm_change(flag("M3S02_AlessaStairwayCutsceneDone") and 2 or 8)
end

function map_music.map3_s02_mix()
    local limits = G_LIMITS
    local r = room()
    local track = sh.game.bgm_idx()
    local flags, fade
    if track == 8 then
        fade = 983040
        flags = 0xFE
    elseif track == 32 then
        fade = 614
        if flag("M4S04_WhereAmICutsceneDone") then
            flags = 4
            if not flag("M4S04_LisaTalkCutsceneDone") then
                flags = 2
                if flag("M4S04_LisaRoomEntered") then
                    flags = 0xE
                end
            end
        else
            flags = 1
        end
    else
        flags = at(G_ROOMS, r)
        fade = 614
        if r == 23 or r == 26 or r == 38 or r == 45 then
            if not (flag("HospitalMusicCorridorStateLatched") or flag("HospitalMusicRoomVisited")) then
                flags = 0x1E
            elseif flag("HospitalMusicCorridorStateLatched") then
                sh.flag_set("HospitalMusicRoomVisited", true)
            end
        elseif r == 5 or r == 25 or r == 37 then
            if not flag("HospitalMusicCorridorStateLatched") and flag("HospitalMusicRoomVisited") then
                sh.flag_set("HospitalMusicCorridorStateLatched", true)
                sh.flag_set("HospitalMusicRoomVisited", false)
            end
            if not flag("HospitalMusicCorridorStateLatched") then
                flags = 0x1FE
            elseif not flag("HospitalMusicRoomVisited") then
                flags = 0x13E
            end
        elseif G_MARK[r] then
            if r == 4 and not sh.item_remove("ExaminationRoomKey", 0) then
                flags = 1
            end
            sh.flag_set("HospitalMusicRoomVisited", true)
        elseif r == 3 then
            if track == 31 then
                limits = G_LOBBY
                if not flag("M3S04_LisaFmvSeen") then
                    fade = 983040
                    flags = 4
                elseif not flag("M3S04_LisaCutsceneTalkDone") then
                    fade = 1024
                    flags = 2
                elseif not flag("M3S04_LisaCutsceneHarryCollapsed") then
                    fade = 512
                    flags = 8
                else
                    fade = 983040
                    if not flag("M3S04_LisaCutsceneDone") then
                        fade = 2048
                        flags = 1 | 512
                    else
                        flags = 1
                    end
                end
            else
                flags = 1 | 512
            end
        elseif r == 58 then
            if not flag("M3S05_AlessaCutsceneDone") then
                flags = 1
            end
        end
    end
    mix(flags, fade, limits)
end

map_music.map3_s02 = map_music.pair(map_music.map3_s02_track, map_music.map3_s02_mix)

-- ---- map4_s02, map4_s03, map4_s05 ----

local H_TRACK13 = FULL
local H_TRACK33 = FULL
local H_OTHER = { 128, 128, 96, 80, 80, 112, 0, 0 }
local H_ROOMS = { 0, 48, 40, 40, 60, 36, 56, 52, 0, 44, 0, 44, 52, 32, 32, 6, 0, 2, 0, 0, 0, 0 }
local h_boss_layers = 266 -- the boss fight's layers by the player's height, kept between ticks

function map_music.map4_s02(init)
    local r = room()
    local fade = 409
    local flags, limits
    local track = sh.game.bgm_idx()
    if track == 13 then
        limits = H_TRACK13
        if r == 15 then
            flags = 4
        elseif r == 16 then
            flags = 2 | 4
        elseif r == 14 then
            if flag("M4S05_CybilAimCutsceneDone") then
                fade = 40960
                flags = 1 | 256
            elseif flag("M4S05_FloatstingerKilled") then
                fade = 512
                flags = 1 | 256
            elseif flag("M4S05_CybilStairsSceneWarpDone") then
                if sh.npc.health(1) < 4096000 then
                    flags = 0x1FE
                else
                    local _, y, _ = sh.player_pos()
                    if y > -2048 then
                        h_boss_layers = 2 | 8 | 256
                    elseif y < -10240 then
                        h_boss_layers = 2 | 16 | 256
                    end
                    flags = h_boss_layers
                    if sh.npc.health(1) < 8192000 then
                        flags = flags | 4 | 32
                    elseif sh.npc.health(1) < 12288000 then
                        flags = flags | 4
                    end
                end
            else
                fade = 4096
                flags = 1 | 256
            end
        else
            flags = 1
        end
    elseif track == 33 then
        limits = H_TRACK33
        if r == 18 then
            if flag("M4S03_LeftTvWallRoom") or not flag("M4S03_TvScreensOn") then
                flags = 2
            elseif flag("M4S03_TvWallCutsceneDone") then
                flags = 2 | 4 | 8 | 16
                fade = 163840
            else
                flags = 2 | 4 | 8
            end
        elseif r == 19 then
            sh.flag_set("M4S03_LeftTvWallRoom", true)
            flags = flag("M4S03_FenceSteppedOn") and (1 | 256) or (4 | 256)
        elseif r == 21 then
            flags = 8
        elseif r == 20 then
            if flag("M4S03_TwinfeelerDiveCutsceneDone") then
                flags = 16 | 256
            elseif flag("M4S03_TwinfeelerAttackCutsceneDone") or flag("M4S03_TwinfeelerBurstOut") then
                fade = 2048
                if sh.npc.health(1) > 0 and sh.npc.pos_y(1) < 4096 then
                    flags = 32 | 64 | 256
                else
                    flags = 32 | 256
                end
            else
                flags = 16 | 256
            end
        else
            flags = 1 | 256
        end
    else
        limits = H_OTHER
        if not flag("M4S02_StreetMusicStarted") then
            local _, _, z = sh.player_pos()
            if z < 163840 then
                sh.flag_set("M4S02_StreetMusicStarted", true)
            end
            flags = 2
        else
            flags = at(H_ROOMS, r)
        end
    end
    mix(flags, fade, limits)
end

-- ---- map6_s00, map6_s02 ----

local I_LIMITS = { 128, 128, 128, 128, 128, 48, 40, 56 }
local I_ROOMS_AFTER = { 0, 0, 0, 38, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 70, 14, 22, 22, 14, 14, 0, 0 }
local I_ROOMS_BEFORE = { 0, 0, 0, 38, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 70, 14, 22, 30, 62, 126, 0, 0 }
local i_swell = 0 -- running time of layer 2's slow swell (Q12)

-- The save-screen jingle (track 5): the room music under the inventory, load and save screens.
local function save_jingle(screen_fade_in_range)
    sh.bgm_status_flags_set(sh.bgm_status_flags_get() & ~8)
    local screen = sh.game.screen()
    if screen == "InventoryScreen" or screen == "LoadStatusScreen" then
        if screen ~= "InventoryScreen" or sh.game.screen_step(1) ~= 25 then
            if sh.game.screen_prev() ~= "SaveScreen" then
                return 0xFE, 983040
            end
        end
        return 0x201, 12288
    elseif screen == "SaveScreen" then
        if screen_fade_in_range(sh.screen.fade_status()) then
            return 0x201, 12288
        end
        return 0xFE, 983040
    end
    return 0x201, 12288
end

function map_music.map6_s00(init)
    local limits = I_LIMITS
    local r = room()
    local fade = 819
    local flags
    local track = sh.game.bgm_idx()
    if track == 5 then
        flags, fade = save_jingle(function(s)
            return s >= 2 and s < 6
        end)
    elseif track == 41 then
        limits = nil
        if flag("M6S02_UfoEndingImageShown") then
            fade = 8192
            flags = 0x104
        else
            flags = 0x301
            if not flag("M6S02_UfoEndingFadeStarted") then
                fade = 8192
                flags = 0x102
            end
        end
    elseif track == 21 then
        flags = 0x301
        if not flag("M6S02_UfoEndingTriggered") then
            flags = 0x1FE
            if flag("M6S02_ChannelingStoneUfoStarted") and not flag("M6S02_ChannelingStoneUsed") then
                flags = 0x101
            end
        end
    else
        if flag("M6S02_LighthouseAlessaCutsceneDone") then
            flags = at(I_ROOMS_AFTER, r)
            if flag("M6S01_CybilLeavesCutsceneDone") then
                flags = flags | 0x80
            end
        elseif flag("M6S00_OtherworldSpreadsCutsceneDone") then
            flags = at(I_ROOMS_BEFORE, r)
        else
            flags = 6
        end
    end
    local cap = 96
    if r < 20 and r >= 15 then
        cap = 127
    end
    i_swell = s32(i_swell + sh.game.delta_time())
    I_LIMITS[3] = ((cap * ((sh.math_sin(tdiv(i_swell, 12)) + 12288) // 4)) // 4096) & 0xFF
    mix(flags & 0xFFFF, fade, limits)
end

-- ---- map6_s04, map6_s05 ----

local J_TRACK36 = { 128, 128, 0, 0, 128, 128, 0, 0 }
local J_TRACK39 = { 128, 128, 128, 128, 128, 0, 0, 0 }
local J_OTHER = { 128, 128, 128, 0, 0, 0, 0, 0 }
local J_WATER_X, J_WATER_Z = -65536, -262144 -- the water layer's source (room 3)

function map_music.map6_s04_track()
    sh.sound.bgm_change(flag("M6S04_CybilBossArenaEntered") and 39 or 37)
end

function map_music.map6_s04_mix()
    local fade = 819
    local flags, limits
    local track = sh.game.bgm_idx()
    if track == 36 then
        limits = J_TRACK36
        fade = 409
        if flag("M6S04_DahliaAppeared") then
            flags = 0x20
        elseif not flag("M6S04_FlaurosActivated") then
            fade = 983040
            flags = 2
        else
            flags = 0x10
        end
    elseif track == 39 then
        limits = J_TRACK39
        if flag("M6S04_CybilBossPhaseChangeCutsceneDone") then
            if flag("M6S04_UnknownLiquidUsedOnCybil") then
                flags = 0x100
                if flag("M6S04_CybilParasiteOut") then
                    flags = 0x110
                end
            elseif flag("M6S04_CybilDeathMovieSeen") then
                J_TRACK39[2] = 0x80
                fade = 2048
                if not flag("M6S04_CybilDeathCutsceneDone") then
                    flags = 0x102
                    if flag("M6S04_CybilDeathLineShown") then
                        flags = 0x101
                    end
                else
                    fade = 983040
                    flags = 0x101
                end
            elseif flag("M6S04_CybilBossKilled") then
                J_TRACK39[2] = 0
                fade = 983040
                flags = 0x101
            else
                J_TRACK39[2] = 0x60
                flags = 0x10A
            end
        else
            J_TRACK39[2] = 0x60
            if flag("M6S04_CybilBossFightStarted") then
                flags = 0x108
                if not flag("M6S04_CybilBossPhaseThreeReached") then
                    if flag("M6S04_CarouselTurning") then
                        flags = 0x10A
                    end
                else
                    flags = 0x10A
                end
            else
                fade = 983040
                flags = 0x301
                if flag("M6S04_CutsceneCybilShootsStarted") then
                    flags = 0x104
                end
            end
        end
    else
        local dist = 0
        limits = J_OTHER
        local r = room()
        if r == 3 then
            flags = 4
            local x, _, z = sh.player_pos()
            dist = sh.math.distance_2d(x, z, J_WATER_X, J_WATER_Z)
            if dist < 40960 then
                dist = 128
            else
                dist = 128 - ((dist - 40960) * 16) // 4096
                if dist < 0 then
                    dist = 0
                end
            end
        elseif r == 1 or r == 2 or r == 4 or r == 5 then
            flags = 2
        else
            flags = 1
        end
        -- The water layer's cap lives in the limits table itself.
        J_OTHER[3] = dist & 0xFF
    end
    mix(flags, fade, limits)
end

map_music.map6_s04 = map_music.pair(map_music.map6_s04_track, map_music.map6_s04_mix)

-- ---- map7_s00, map7_s01, map7_s02, map7_s03 ----

local K_TRACK4 = { 112, 128, 128, 0, 0, 0, 0, 0 }
local K_TRACK10 = { 128, 0, 0, 0, 0, 0, 0, 0 }
local K_ROOMS = { 0, 0, 0, 0, 2, 4, 2, 4, 2, 2, 16, 2, 4, 4, 4, 2, 2, 2, 4, 8, 4, 513, 8, 2, 4, 8, 4, 2, 4, 8, 1, 4,
    8, 1, 1, 1, 1, 0 }
local K_TRACK14 = { 128, 128, 128, 128, 128, 128, 0, 0 }
local K_TRACK29 = { 128, 0, 0, 0, 0, 0, 0, 0 }
local K_TRACK40 = { 128, 128, 128, 128, 128, 128, 64, 64 }

-- The track of the Nowhere maps on entry.
function map_music.map7_s00_track()
    local track
    if room() == 3 then
        track = 15
    elseif flag("M7S01_LisaWardEntered") then
        if not flag("NowhereAfterLisaWardEntered") then
            sh.flag_set("NowhereAfterLisaWardEntered", true)
            track = 1
        else
            track = 22
        end
    else
        track = 14
    end
    sh.sound.bgm_change(track)
end

-- The track of the final boss rooms on entry.
function map_music.map7_s03_track()
    sh.sound.bgm_change(flag("M5S03_KaufmannHasMotorcycleVial") and 40 or 35)
end

function map_music.map7_mix()
    local flags = 1
    local fade = 409
    local r = room()
    local limits = nil
    local track = sh.game.bgm_idx()
    if track == 5 then
        flags, fade = save_jingle(function(s)
            return s >= 2 and s <= 5
        end)
    elseif track == 4 then
        limits = K_TRACK4
        if r == 20 or r == 21 or r == 23 or r == 24 then
            if flag("M7S01_CutsceneLisaWardDone") or flag("M7S01_LisaWardMovieSeen") then
                flags = 4
                fade = 983040
            elseif flag("M7S01_LisaWardSceneFadedOut") then
                if flag("M7S01_LisaWardMusicSilenced") then
                    fade = 20480
                    flags = 4
                else
                    fade = 983040
                    flags = 0x201
                    if sh.sound.music_channels() == 0 then
                        sh.flag_set("M7S01_LisaWardMusicSilenced", true)
                    end
                end
            elseif flag("M7S01_LisaWardMusicSilenced") then
                fade = 4096
                flags = 0x201
            elseif flag("M7S01_LisaWardSceneTurnedAway") then
                fade = 4096
                flags = 0x201
                if sh.sound.music_channels() == 0 then
                    sh.flag_set("M7S01_LisaWardMusicSilenced", true)
                end
            else
                flags = 2
            end
        else
            flags = 0x201
        end
    elseif track == 10 or track == 15 then
        limits = K_TRACK10
        if r == 1 then
            if not flag("M7S00_EntryMovieSeen") then
                flags = 1
            elseif flag("M7S00_CutsceneLisaHospitalDone") then
                fade = 983040
                flags = 0x201
            elseif flag("M7S00_LisaSobbingStarted") then
                fade = 409
                flags = 1
            else
                fade = 983040
                flags = 0xFE
            end
        elseif r == 2 then
            if flag("M7S00_CutsceneElevatorSummonsStarted") then
                fade = 4096
                flags = 0xFE
            else
                fade = 1024
                flags = 0x201
            end
        elseif r == 3 then
            fade = 614
            flags = 0x201
        else
            flags = 1
        end
    elseif track == 14 or track == 22 then
        limits = K_TRACK14
        flags = at(K_ROOMS, r)
        if r == 36 then
            fade = 2048
        elseif r == 21 then
            fade = 983040
        elseif r == 4 then
            if not flag("M7S01_GhostAlessaCorridorPassed") then
                flags = 1
            end
        elseif r == 7 then
            if not flag("M7S01_PickupKeyOfPhaleg") then
                flags = 1
            end
        elseif r == 22 then
            if flag("M7S01_StoneOfTimeReleased") and not flag("M7S01_ClockOpened") then
                flags = 4 | 8
            end
        elseif r == 17 then
            if flag("M7S01_FuseBoxPlateRemoved") then
                flags = flag("M7S02_GeneratorStopped") and 4 or 16
            end
        elseif r == 25 then
            if not flag("M7S01_Door3x3AUnlocked") and flag("M7S01_DarknessPicturePhotographed") then
                flags = flags | 4
            end
            if not flag("M7S01_Door3x3BUnlocked") and flag("M7S01_FuturePicturePhotographed") then
                flags = flags | 2
            end
        elseif r == 29 then
            if flag("M7S02_ChainsBloodsuckerSceneDone") then
                flags = (track == 22) and 4 or 8
            elseif flag("M7S02_BloodsuckerAppeared") then
                if flag("M7S02_RingOfContractOnChain") then
                    flags = (track == 22) and 8 or 2
                else
                    flags = (track == 22) and (8 | 16) or (2 | 16)
                end
            elseif not flag("M7S02_FridgeOpened") then
                flags = flag("M7S02_PickupDaggerOfMelchior") and (4 | 8) or 4
            else
                flags = 16
            end
        elseif r == 31 then
            if not flag("M7S02_CutsceneGhostAlessaCorridorDone") then
                flags = 1
            end
        elseif r == 32 then
            if not flag("M7S02_CutsceneDahliaKaufmannPlanDone") then
                fade = 983040
                flags = 1
            end
        elseif r == 33 then
            if not flag("M7S02_CutsceneHallwayAlessaDahliaDone") then
                flags = 0
                for _, seal in ipairs({"M7S02_LastDoorAmuletOfSolomonPlaced", "M7S02_LastDoorCrestOfMercuryPlaced", "M7S02_LastDoorAnkhPlaced", "M7S02_LastDoorDaggerOfMelchiorPlaced", "M7S02_LastDoorDiskOfOuroborosPlaced"}) do
                    if flag(seal) then
                        flags = (flags * 2) | 2
                    end
                end
            end
        end
        if track == 22 then
            -- On the second track layers 1 and 2 trade places.
            local l1 = flags & 2
            local l2 = flags & 4
            flags = flags ~ (l1 | l2)
            flags = flags | (l1 * 2) | (l2 >> 1)
        end
    elseif track == 35 then
        if flag("M7S03_IncubatorKilled") then
            flags = 0x301
        elseif flag("M7S03_IncubusDeathMovieSeen") then
            if not flag("M7S03_FinaleMusicLayer4") then
                fade = 983040
                flags = 0x120
            else
                fade = 2048
                flags = 0x301
            end
        elseif flag("M7S03_FinalBossIntroDone") then
            if sh.npc.health(1) < 2048000 then
                flags = 0x13E
            else
                flags = 0x128
            end
        else
            if flag("M7S03_FinaleMusicLayer1") then
                flags = 0x102
            else
                flags = 0x100
            end
            if flag("M7S03_FinaleMusicLayer2") then
                flags = flags | 4
            end
            if flag("M7S03_FinaleMusicLayer3") then
                flags = flags | 8
            end
            if flag("M7S03_FinaleMusicLayer4") then
                flags = flags | 16
            end
            if flag("M7S03_FinaleSceneMusicCut") then
                fade = 983040
            end
            if flags == 0x100 then
                flags = 0x301
            end
        end
    elseif track == 29 then
        limits = K_TRACK29
        fade = 983040
        if not flag("M7S03_FinaleSceneMusicCut") then
            flags = 0x101
        else
            if flag("M7S03_FinaleMusicLayer1") then
                sh.bgm_status_flags_set(sh.bgm_status_flags_get() | 4)
            end
            flags = 0x100
        end
    elseif track == 40 then
        limits = K_TRACK40
        if flag("M7S03_FinaleMusicLayer4") then
            fade = 1024
            flags = 0x101
        elseif flag("M7S03_IncubusDeathMovieSeen") then
            fade = 8192
            flags = 0x301
        elseif flag("M7S03_IncubusKilled") then
            if not flag("M7S03_FinaleMusicLayer3") then
                fade = 983040
                flags = 0x101
            else
                flags = 0x120
            end
            if flag("M7S03_FinaleMusicLayer2") then
                if flag("M7S03_FinaleMusicLayer3") then
                    flags = flags | 0xC0
                else
                    flags = flags | 0x40
                end
            end
        elseif flag("M7S03_FinalBossIntroDone") or flag("M7S03_FinaleMusicLayer1") then
            if sh.music.layer_volume(1) == 4096 then
                if flag("M7S03_FinalBossIntroDone") then
                    fade = 4096
                    if (sh.npc.flags(3) & 4) == 0 then
                        flags = 0x102
                    else
                        flags = 0x11E
                    end
                else
                    fade = 491
                    if sh.music.layer_volume(2) == 4096 then
                        if sh.music.layer_volume(3) == 4096 then
                            flags = 0x11E
                        else
                            flags = 0x10E
                        end
                    elseif flag("M7S03_FinaleSceneMusicCut") then
                        flags = 0x106
                    else
                        flags = 0x102
                    end
                end
            else
                if flag("M7S03_FinalBossIntroDone") then
                    fade = 983040
                end
                flags = 0x102
                if sh.music.layer_volume(0) ~= 4096 then
                    if sh.sound.music_channels() == 0 then
                        sh.music.channel_set()
                    else
                        sh.sound.music_layer_volume_set(0, 0x7F)
                        sh.sound.music_layer_volume_set(1, 1)
                        sh.music.set_layer_volume(0, 4096)
                        sh.music.set_layer_volume(1, 32)
                    end
                end
            end
        else
            flags = 0x301
        end
    end
    mix(flags, fade, limits)
end

map_music.map7_s00 = map_music.pair(map_music.map7_s00_track, map_music.map7_mix)
map_music.map7_s03 = map_music.pair(map_music.map7_s03_track, map_music.map7_mix)
