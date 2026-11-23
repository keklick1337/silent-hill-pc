-- Incubator (map7_s03, the ending): the cultist woman of the last scenes. The cutscene picks the
-- clip through npc_control_state and moves her with npc_move_speed.

local ANIM = clips("Incubator", { rise = 1, appear = 2, float = 3 })
local CHARA_ID = sh.chara_id("Incubator") or 36

local function control_update(c)
    local state = c.npc_control_state
    if state == 1 then
        anim_status_set(c, ANIM.appear, false)
        anim_state_reset(c)
    elseif state == 2 then
        anim_status_keyframe_set(c, ANIM.float, true)
        anim_state_reset(c)
    elseif state == 3 then
        anim_status_set(c, ANIM.rise, false)
        anim_state_reset(c)
    end
    control_tail(c, nil)
end

function update(slot)
    local c = record(slot)
    if c.chara_id ~= CHARA_ID then
        collision_reset(c)
    end
    control_update(c)
    move_flat(c, 0)
    anim_update(c)
end
