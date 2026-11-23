-- Ghost Doctor (map ghost scenes): stands and plays clip 1 where the cutscene places him.

local ANIM = clips("GhostDoctor", { stand = 1 })
local CHARA_ID = sh.chara_id("GhostDoctor") or 42

function update(slot)
    local c = record(slot)
    if c.chara_id ~= CHARA_ID then
        collision_reset(c)
    end

    if c.npc_control_state == 0 then
        if c.state_step == 0 then
            c.anim_status = anim_status(ANIM.stand, false)
            c.state_step = c.state_step + 1
        end
        anim_state_reset(c)
    end
    sh.entity.bone_flag_set(slot, 0, 0)
    sh.entity.bone_rotation_set(slot, 0, c.rot_x, c.rot_y, c.rot_z)

    sh.entity.bone_translation_set(slot, 0, asr(c.pos_x, 4), asr(c.pos_y, 4), asr(c.pos_z, 4))
    anim_update(c)
end
