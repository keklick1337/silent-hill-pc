-- Ghost child Alessa (map7_s01, map7_s02): the girl of the hospital memories. npc_control_state
-- picks the clip; she walks with the cutscene's npc_move_distance as speed.

local ANIM = clips("GhostChildAlessa", { walk = 1, walk_alt = 2, approach = 3, stand_close = 4, reach_out = 5,
                                         walk_off = 6, stand_watch = 7, appear = 8 })
local CHARA_ID = sh.chara_id("GhostChildAlessa") or 35

-- Turn per tick (Q8); only the init writes it.
local turn_rate = 0

-- npc_control_state -> clip for the states that only play a clip.
local PLAIN = { [7] = ANIM.stand_close, [8] = ANIM.reach_out, [9] = ANIM.walk_off, [10] = ANIM.stand_watch, [11] = ANIM.appear }

local function anim_state_update(c)
    local state = c.npc_control_state
    if state == 1 or state == 2 then
        c.npc_move_speed = c.npc_move_distance
        anim_status_set(c, state == 1 and ANIM.walk or ANIM.walk_alt, false)
        anim_state_reset(c)
    elseif state == 6 then
        anim_status_set(c, ANIM.approach, false)
        if c.anim_keyframe == 63 then
            c.npc_control_state = 7
            c.state_step = 0
        end
    elseif PLAIN[state] ~= nil then
        anim_status_set(c, PLAIN[state], false)
        anim_state_reset(c)
    end
    control_tail(c, asr(turn_rate, 4))
end

function update(slot)
    local c = record(slot)
    if c.chara_id ~= CHARA_ID then
        collision_reset(c)
        turn_rate = 0
    end
    anim_state_update(c)
    move_flat(c, 0)
    anim_update(c)
end
