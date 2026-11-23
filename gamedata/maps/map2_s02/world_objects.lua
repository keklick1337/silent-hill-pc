-- Setup and per-tick rules of map2_s02.

local CELL = 163840 -- 40 m
local Q12_4 = 16384
local Q12_3 = 12288
local NO_DIST_ATTEN = 2
local COLLISION_ALL, COLLISION_1, COLLISION_2 = 0xFFFF, 2, 4
-- Q8 volume 0.766 and the shot's position (-100.0, -1.2, -108.0 m).
local SHOT_VOL = 196
local SHOT_X, SHOT_Y, SHOT_Z = -409600, -4915, -442368

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

-- Setup when the map loads.
if sh.flag_get("MapMark_FogCentralTown_AntiqueWholeSign") then
    sh.flag_set("MapMark_AltCentralTown_AntiqueWholeSign", true)
end
local shot_timer = 0
local difficulty = sh.save.game_difficulty()
if difficulty == -1 then
    sh.game.set_npc_flags_id(2)
elseif difficulty == 0 then
    sh.game.set_npc_flags_id(3)
else
    sh.game.set_npc_flags_id(4)
end

function update(dt)
    local x, y, z = sh.player_pos()
    local delta = sh.game.delta_time()

    sh.collision.flag_bits_clear(COLLISION_ALL)
    if in_chunk(x, 1, -3, -1, -3) and in_chunk(z, 1, -2, -1, -2) then
        if sh.flag_get("TownStairwellEntryTriggered") then
            sh.collision.flag_bits_set(COLLISION_2)
            sh.collision.flag_bits_clear(COLLISION_1)
            if z < -278528 then -- -68.0 m
                sh.flag_set("TownStairwellEntryTriggered", false)
            end
        else
            sh.collision.flag_bits_clear(COLLISION_2)
            sh.collision.flag_bits_set(COLLISION_1)
        end
    end

    if sh.flag_get("M2S02_BackFromOtherworld") and not sh.flag_get("M2S02_PassageAfterDarknessCrossed") then
        if in_chunk(x, 1, -3, -1, -3) and in_chunk(z, 1, 3, -1, 3) then
            if x > -475136 and y > 0 then -- -116.0 m
                local step = sh.screen_fade_timestep_get()
                if step > 0 then
                    sh.screen_fade_timestep_set(math.min(step + delta, Q12_3))
                end
            end
        end
    end

    if in_chunk(x, 1, -6, -1, -6) and in_chunk(z, 1, 3, -1, 3) then
        sh.light.flashlight_off()
    end

    if not sh.flag_get("M2S02_KaufmannShotsHeard") then
        if in_chunk(x, 1, -3, -1, -3) and in_chunk(z, 1, -3, -1, -3) then
            if shot_timer == 0 then
                if sh.math.vec2_len_q6(x + 393216, z + 364544) < Q12_4 then -- +96.0 m, +89.0 m
                    sh.sfx_flags_play("kaufmann_shot", SHOT_X, SHOT_Y, SHOT_Z, SHOT_VOL, NO_DIST_ATTEN)
                    shot_timer = 1228 -- 0.3 s
                end
            end
        end
        if shot_timer > 0 then
            shot_timer = shot_timer - delta
            if shot_timer < 0 then
                sh.flag_set("M2S02_KaufmannShotsHeard", true)
                sh.sfx_flags_play("kaufmann_shot", SHOT_X, SHOT_Y, SHOT_Z, SHOT_VOL, NO_DIST_ATTEN)
            end
        end
    end
end

-- The room's spawn pass (the overlay's npc spawn event): after M2S02_BackFromOtherworld, M2S02_PassageAfterDarknessCrossed is set once and the
-- ambient sound set starts again.
function npc_spawn()
    if sh.flag_get("M2S02_BackFromOtherworld") and not sh.flag_get("M2S02_PassageAfterDarknessCrossed") then
        sh.flag_set("M2S02_PassageAfterDarknessCrossed", true)
        sh.ambient_sfx()
    end
end
