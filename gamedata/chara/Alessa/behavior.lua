-- Alessa (map3_s02, map6_s02, map6_s04, map7_s03): walks, stumbles, pushes Harry back with her
-- power and crumples, as the cutscenes set npc_control_state. Footsteps from the ground type.

local ANIM = clips("Alessa", { stand_idle = 1, walk_forward = 2, walk_forward_turn_stumble = 3, force_push = 4,
                               stumble_forward_crumple = 5, crumple_look_up = 6, crumple_shake_head = 7,
                               stand_look_right = 8, crumple_idle = 9, kneel = 10 })

local WALK_SPEED = 5120   -- Q12(1.25)
local ACCEL = 1638        -- Q12(0.4) per 30 fps tick
local DECEL = 2048        -- Q12(0.5) per 30 fps tick

-- Turn per tick (Q8); only the init writes it.
local turn_rate = 0

-- npc_control_state -> clip for the states that only play a clip.
local PLAIN = { [4] = ANIM.force_push, [3] = ANIM.walk_forward_turn_stumble, [5] = ANIM.stumble_forward_crumple,
                [6] = ANIM.crumple_look_up, [7] = ANIM.crumple_shake_head, [8] = ANIM.stand_look_right,
                [9] = ANIM.crumple_idle, [10] = ANIM.kneel }

local function anim_state_update(c)
    local state = c.npc_control_state
    if state == 2 then
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
        anim_status_set(c, ANIM.walk_forward, false)
        anim_state_reset(c)
    elseif state == 1 then
        if c.npc_move_speed ~= 0 then
            c.npc_move_speed = c.npc_move_speed - timestep_30fps(ACCEL) * 2
            if c.npc_move_speed < 0 then
                c.npc_move_speed = 0
            end
        end
        anim_status_keyframe_set(c, ANIM.stand_idle, true)
        anim_state_reset(c)
    elseif PLAIN[state] ~= nil then
        anim_status_set(c, PLAIN[state], false)
        anim_state_reset(c)
    end

    local _, ground_type = sh.entity.ground_at(c.pos_x, c.pos_z)
    local sfx, pitch0, pitch1 = sh.entity.footstep_sfx(ground_type, c.chara_id)

    state = c.npc_control_state
    local keyframe = c.anim_keyframe
    if state == 2 then
        footstep_pair(c, anim_status(ANIM.walk_forward, true), 24, 37, sfx, pitch0)
    elseif state == 3 then
        local stumble = anim_status(ANIM.walk_forward_turn_stumble, true)
        if keyframe < 90 then
            if keyframe <= 60 then
                footstep_pair(c, stumble, 60, 71, sfx, pitch0)
            elseif keyframe <= 81 then
                footstep_pair(c, stumble, 81, 71, sfx, pitch0)
            else
                footstep_pair(c, stumble, 89, 89, sfx, pitch0)
            end
        else
            if keyframe <= 111 then
                footstep_pair(c, stumble, 121, 111, sfx, pitch1)
            else
                footstep_pair(c, stumble, 121, 128, sfx, pitch1)
            end
        end
    elseif state == 5 then
        keyframe_sfx(c, c.anim_status, 210, sound(c, "collapse", 1638))
        local crumple = anim_status(ANIM.stumble_forward_crumple, true)
        if c.anim_keyframe <= 173 then
            footstep_pair(c, crumple, 182, 173, sfx, pitch0)
        else
            footstep_pair(c, crumple, 182, 199, sfx, pitch0)
        end
    elseif state == 8 then
        footstep_pair(c, anim_status(ANIM.stand_look_right, true), 298, 298, sfx, pitch0)
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
