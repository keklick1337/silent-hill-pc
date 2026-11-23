-- Setup and per-tick rules of map1_s00: the first-visit state
-- update, the school-yard ambience as Harry crosses the street, and the Silver Moon hum at the gate
-- (its volume follows the flags 83/84).

local CELL = 163840 -- 40 m
-- Hum sources: (20.58, -1.14, -17.41) m marker, (18.8, -1.0, -17.5) m and (52.0, -1.0, -16.0) m emitters.
local MARKER = {84295, -4669, -71311}
local GATE = {77004, -4096, -71680}
local FAR = {212992, -4096, -65536}

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

-- Setup. The ambience step and the hum volume (Q12) start at zero with the overlay.
local ambience = 0
local hum = 0
sh.item_remove("NoteToSchool", 1)
sh.flag_set("M2S00_NoteToSchoolFound", true)

function update(dt)
    local x, _, z = sh.player_pos()

    if not sh.flag_get("M1S00_ArrivalAutosaveTaken") then
        sh.flag_set("M1S00_ArrivalAutosaveTaken", true)
        sh.game.state_update()
    end

    if in_chunk(x, 1, -3, -1, -3) and in_chunk(z, 1, -2, -1, -2) then
        if ambience == 0 then
            ambience = 1
            sh.sound.play("darkness_hum", 0, 232)
        end
    end
    if in_chunk(x, 1, -2, -1, -2) and in_chunk(z, 1, -2, -1, -2) then
        if ambience == 1 then
            ambience = 2
            sh.sound.play("darkness_hum", 0, 216)
        end
    end

    local hum_id = sh.sound.id("clock_tower_hum")
    if in_chunk(x, 0, 0, -1, 1) and in_chunk(z, 1, -1, 0, 0) then
        if sh.flag_get("M1S00_SilverMedallionPlaced") then
            sh.combat.table_point_set(MARKER[1], MARKER[2], MARKER[3], 0, 0x385)
        end
        if sh.flag_get("M1S00_SilverMedallionPlaced") then
            if hum == 0 then
                sh.sd_call("clock_tower_hum")
            end
            if not sh.flag_get("M1S00_BoilerSceneDone") then
                if sh.flag_get("M1S00_BoilerSwitchPressed") then
                    hum = hum + (sh.game.delta_time() * 614) // 4096 -- 0.15 per second
                    if hum > 4096 then
                        hum = 4096
                    end
                else
                    hum = 409 -- 0.0999
                end
            else
                hum = 4096
            end
            sh.sound.play_3d_falloff(hum_id, GATE[1], GATE[2], GATE[3], hum // 32, 98304, 0)
        end
    elseif in_chunk(x, 1, 2, -1, 2) and in_chunk(z, 1, -1, 0, 0) then
        if sh.flag_get("M1S00_BoilerSceneDone") then
            if hum == 0 then
                sh.sd_call("clock_tower_hum")
            end
            hum = 819 -- 0.2
            sh.sound.play_3d_falloff(hum_id, FAR[1], FAR[2], FAR[3], 25, 65536, 0)
        end
    else
        if hum ~= 0 then
            sh.sfx_stop("clock_tower_hum")
        end
        hum = 0
    end
end

-- The room's spawn pass (the overlay's npc spawn event): once the Silver Medallion is taken, the first two
-- Grey Children rows spawn with flags 0 and 1.
function npc_spawn()
    if sh.flag_get("M1S01_PickupSilverMedallion") then
        sh.chara.spawn_flags_set("GreyChild", 0, 3)
        sh.chara.spawn_flags_set("GreyChild", 1, 3)
    end
end
