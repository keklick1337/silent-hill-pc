-- Setup and per-tick rules of map1_s06: the basement/stairwell
-- effects, the school-circle map marks and the basement hum, which the arrival scene in events.lua also drives
-- through the map variable basement_hum_volume (Q12).

local CELL = 163840 -- 40 m

-- The map's chunk test (PLAYER_IN_MAP_CHUNK): the index truncates toward zero; the offsets differ by sign.
local function in_chunk(p, off_pos, want_pos, off_neg, want_neg)
    local idx = p // CELL
    if p < 0 and idx * CELL ~= p then
        idx = idx + 1
    end
    if p > 0 then
        return idx + off_pos == want_pos
    end
    return idx + off_neg == want_neg
end

-- Setup. The hum's balance values (s8 in the original) start at zero with the overlay.
local area = 0          -- 1 = basement hall, 0 = stairs side
local hum_playing = false
local balance_now, balance_target = 0, 0
sh.map.set("basement_hum_volume", 0)
if not sh.flag_get("M1S06_BasementHumStarted") then
    sh.sfx_stop("darkness_hum")
end

function update(dt)
    local x, y, z = sh.player_pos()
    local delta = sh.game.delta_time()
    local hum = sh.map.get("basement_hum_volume")

    if in_chunk(x, 0, 0, -1, 1) and in_chunk(z, 1, -1, 0, 0) then
        if area ~= 1 then
            sh.world.effects_update(7, 4)
            area = 1
        end
        if sh.flag_get("M1S06_WokeUpAfterBoss") then
            hum = 614 -- 0.15
        end
    elseif in_chunk(x, 1, 2, -1, 2) and in_chunk(z, 1, -1, 0, 0) then
        if area ~= 0 then
            sh.world.effects_update(2, 2)
            sh.light.flashlight_off()
            area = 0
        end
        if not sh.flag_get("M1S06_EntranceHallReached") then
            hum = (y * -819) // 4096 + 1024 -- y * -0.2 + 0.25
        end
    else
        if in_chunk(x, 1, 3, -1, 3) and in_chunk(z, 0, 0, -1, 1) then
            if sh.flag_get("MapMark_OldTown_SchoolCircle") then
                sh.flag_set("MapMark_OldTown_SchoolCheck", true)
            end
            sh.flag_set("M1S06_EntranceHallReached", true)
        end
        if sh.flag_get("M1S06_EntranceHallReached") then
            hum = hum - (delta * 819) // 4096 -- 0.2 per second
            if hum < 0 then
                hum = 0
                hum_playing = false
            end
        else
            hum = 1843 -- 0.45
        end
    end
    sh.map.set("basement_hum_volume", hum)

    if not hum_playing then
        if not sh.flag_get("M1S06_EntranceHallReached") and sh.flag_get("M1S06_BasementHumStarted") then
            sh.sd_call("basement_hum")
            hum_playing = true
        end
        if not hum_playing then
            return
        end
    end

    -- The hum sits 16 m north of Harry.
    balance_target = sh.vc_stereo_balance(x, y, z - 65536)
    if balance_now < balance_target then
        balance_now = math.min(balance_now + delta // 64, 127)
    end
    if balance_target < balance_now then
        local lowered = balance_now - delta // 64
        balance_now = lowered < -127 and -127 or lowered
    end
    local vol = math.max(255 - hum // 16, 0)
    sh.sfx_attributes_update("basement_hum", balance_now, vol & 255, 0)
end

-- The room's spawn pass (the overlay's npc spawn event): standing exactly on the church entrance point
-- (21.45, -15.26) m, the camera jumps to (21.69, -2.38, -19.16) m looking at (21.08, -0.45, -15.71) m, runs
-- once and hands over to the auto camera.
function npc_spawn()
    local x, _, z = sh.player_pos()
    if x == 87859 and z == -62504 then
        sh.camera_pos(88842 / 4096, -9748 / 4096, -78479 / 4096, 0, 0, 0, 0, 0, 0, 0, true)
        sh.camera_look(86343 / 4096, -1843 / 4096, -64348 / 4096, 0, 0, 0, 0, 0, 0, 0, true)
        sh.camera.exec()
        sh.camera_auto_return(false)
    end
end
