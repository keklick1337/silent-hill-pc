-- Lisa (map3_s04, map4_s04, map7_s00..s02): the nurse of Alchemilla. Each cutscene sets
-- npc_control_state; she walks with npc_move_speed and steps on the keyframes of her clips.

-- Clip names of the character data ("anims"), in clip order: A[i] resolves name -> number.
local NAMES = {
    "stand", "walk_early", "talk_gesture", "lean_forward", "walk_away", "worry", "scene_pose",
    "turn_greet", "walk", "plead_stay", "lean_in", "sob", "reach_out", "lean_back",
    "turn_away", "scene_final", "look_away",
}
local A = {}
for i = 1, #NAMES do
    A[i] = sh.chara_anim("Lisa", NAMES[i], i)
end

local WALK_SPEED = 5120   -- Q12(1.25)
local ACCEL = 1638        -- Q12(0.4) per 30 fps tick
local DECEL = 2048        -- Q12(0.5) per 30 fps tick

-- Turn per tick (Q8); only the init writes it.
local turn_rate = 0

-- npc_control_state -> { clip, starts at its first keyframe, resets on a cutscene request }
local STATES = {
    [6] = { 3, false, true }, [8] = { 5, false, true }, [9] = { 6, true, true }, [10] = { 7, false, true },
    [11] = { 8, true, true }, [12] = { 9, true, true }, [13] = { 10, false, true }, [14] = { 11, true, false },
    [15] = { 12, false, false }, [16] = { 13, false, true }, [17] = { 14, true, true }, [18] = { 15, true, true },
    [19] = { 16, false, true }, [21] = { 17, false, true },
}

local function slow_down(c)
    if c.npc_move_speed ~= 0 then
        c.npc_move_speed = c.npc_move_speed - timestep_30fps(ACCEL) * 2
        if c.npc_move_speed < 0 then
            c.npc_move_speed = 0
        end
    end
end

-- Footsteps of a clip in up to three keyframe ranges: { upper keyframe or nil, keyframe0, keyframe1 }.
local function steps(c, clip, pitch, ranges)
    local status = anim_status(A[clip], true)
    local keyframe = c.anim_keyframe
    for _, r in ipairs(ranges) do
        if r[1] == nil or keyframe <= r[1] then
            footstep_pair(c, status, r[2], r[3], sound(c, "footstep", 1529), pitch)
            return
        end
    end
end

local function anim_state_update(c)
    local state = c.npc_control_state
    if state == 1 then
        if c.npc_move_speed > WALK_SPEED then
            c.npc_move_speed = c.npc_move_speed - timestep_30fps(DECEL)
            if c.npc_move_speed < WALK_SPEED then
                c.npc_move_speed = WALK_SPEED
            end
        elseif c.npc_move_speed < WALK_SPEED then
            c.npc_move_speed = c.npc_move_speed + timestep_30fps(ACCEL)
            local v = c.npc_move_speed
            c.npc_move_speed = v < 0 and 0 or (v > WALK_SPEED and WALK_SPEED or v)
        end
        anim_status_set(c, A[2], false)
        anim_state_reset(c)
    elseif state == 5 or state == 20 then
        slow_down(c)
        if state == 5 then
            anim_status_keyframe_set(c, A[1], true)
        else
            anim_status_set(c, A[1], true)
        end
        anim_state_reset(c)
    elseif state == 7 then
        anim_status_set(c, A[4], false)
        if c.anim_keyframe == 72 then
            c.npc_control_state = 20
            c.state_step = 0
        end
    elseif STATES[state] ~= nil then
        local s = STATES[state]
        if s[2] then
            anim_status_keyframe_set(c, A[s[1]], true)
        else
            anim_status_set(c, A[s[1]], false)
        end
        if s[3] then
            anim_state_reset(c)
        end
    end

    local _, ground_type = sh.entity.ground_at(c.pos_x, c.pos_z)
    local _, pitch0, pitch1 = sh.entity.footstep_sfx(ground_type, c.chara_id)

    state = c.npc_control_state
    if state == 1 then
        steps(c, 2, pitch0, { { nil, 14, 31 } })
    elseif state == 8 then
        steps(c, 5, pitch0, { { 125, 125, 138 }, { 153, 153, 138 }, { nil, 189, 184 } })
    elseif state == 12 then
        -- The first range pairs 263 with 258 (as the game does).
        steps(c, 9, pitch1, { { 258, 263, 258 }, { nil, 263, 266 } })
    elseif state == 13 then
        steps(c, 10, pitch0, { { 332, 332, 337 }, { 344, 344, 337 }, { nil, 352, 348 } })
    elseif state == 11 then
        steps(c, 8, pitch0, { { nil, 240, 240 } })
    elseif state == 15 then
        if sh.map_name() == "map7_s00" then
            keyframe_sfx(c, anim_status(A[12], true), 408, sound(c, "cue", 1639))
        end
        steps(c, 12, pitch0, { { nil, 406, 406 } })
    elseif state == 16 then
        steps(c, 13, pitch0, { { 423, 423, 425 }, { 429, 429, 425 }, { nil, 435, 432 } })
    elseif state == 17 then
        steps(c, 14, pitch0, { { 460, 460, 477 }, { 497, 497, 477 }, { nil, 536, 516 } })
    elseif state == 18 then
        steps(c, 15, pitch0, { { 551, 551, 555 }, { 559, 559, 555 }, { nil, 564, 564 } })
    end

    control_tail(c, asr(turn_rate, 4))
end

function update(slot)
    local c = record(slot)
    if c.control_state == 0 then
        collision_reset(c)
        turn_rate = 0
    end
    anim_state_update(c)
    move_flat(c, 0)
    anim_update(c)
end
