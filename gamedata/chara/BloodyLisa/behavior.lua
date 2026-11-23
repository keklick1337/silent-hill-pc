-- Bloody Lisa (map7_s01..s03): walks when the cutscene sets npc_control_state 6, with footsteps
-- on the walk's keyframes. Moved by npc_move_speed, no collision.

local ANIM = clips("BloodyLisa", { walk = 1 })

-- Turn per tick (Q8); nothing sets it, so she walks straight.
local turn_rate = 0

local function anim_state_update(c)
    local state = c.npc_control_state
    if state == 6 then
        anim_status_set(c, ANIM.walk, false)
        anim_state_reset(c)
    end

    local _, ground_type = sh.entity.ground_at(c.pos_x, c.pos_z)
    local sfx, pitch0 = sh.entity.footstep_sfx(ground_type, c.chara_id)

    if c.npc_control_state == 6 then
        local walk = anim_status(ANIM.walk, true)
        local keyframe = c.anim_keyframe
        if keyframe <= 12 then
            footstep_pair(c, walk, 12, 27, sfx, pitch0)
        elseif keyframe <= 49 then
            footstep_pair(c, walk, 49, 27, sfx, pitch0)
        else
            footstep_pair(c, walk, 92, 69, sfx, pitch0)
        end
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
