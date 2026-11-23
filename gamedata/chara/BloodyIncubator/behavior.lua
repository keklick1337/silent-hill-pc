-- Bloody Incubator (map7_s03, the ending): clip per npc_control_state, moved by the cutscene.

local CHARA_ID = sh.chara_id("BloodyIncubator") or 37

-- npc_control_state -> clip, and whether it starts at its first keyframe in the playing phase.
local STATES = {
    [1] = { 9, true }, [2] = { 1, true }, [3] = { 2, true }, [4] = { 3, true },
    [5] = { 10, false }, [6] = { 4, false }, [7] = { 5, false }, [8] = { 6, true },
    [9] = { 7, false }, [10] = { 8, false }, [11] = { 11, true },
}

local function control_update(c)
    local entry = STATES[c.npc_control_state]
    if entry ~= nil then
        if entry[2] then
            anim_status_keyframe_set(c, entry[1], true)
        else
            anim_status_set(c, entry[1], false)
        end
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
