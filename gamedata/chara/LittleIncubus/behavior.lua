-- Little Incubus (map6_s04): grows in over 8 seconds (bones 1 and 7 scaled by a sine ease).

local ANIM = clips("LittleIncubus", { growth = 1 })

-- Grow timer, shared by every Little Incubus of the map (Q12, 10 per second).
local grow = 0

function update(slot)
    local c = record(slot)
    if c.control_state == 0 then
        c.anim_alpha = 0
        c.control_state = 1
        c.state_step = 0
        c.anim_status = anim_status(ANIM.growth, true)
        c.anim_time = 0
        c.anim_keyframe = 0
        grow = 0
    end

    grow = s32(grow + sh.game.delta_time() * 10)
    local progress = idiv(s32(grow << 12), 80 * 4096)
    if progress > 4096 then
        progress = 4096
    end
    local scale = sh.math.sin(asr(progress, 2))

    sh.entity.bone_flag_set(slot, 0, 0)
    sh.entity.bone_translation_set(slot, 0, asr(c.pos_x, 4), asr(c.pos_y, 4), asr(c.pos_z, 4))
    sh.entity.bone_rotation_set(slot, 0, c.rot_x, c.rot_y, c.rot_z)
    sh.entity.anim_step(slot)

    sh.entity.bone_scale(slot, 1, scale, scale, scale)
    sh.entity.bone_scale(slot, 7, scale, scale, scale)
end
