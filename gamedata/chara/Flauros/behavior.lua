-- Flauros (map6_s04): the pyramid of the final scene. Its light rays (bones 6..10) grow from
-- nothing between keyframes 13 and 21 of the clip.

local ANIM = clips("Flauros", { ray_growth = 1 })

function update(slot)
    local c = record(slot)
    if c.control_state == 0 then
        c.anim_alpha = 0
        c.control_state = 1
        c.state_step = 0
        c.anim_status = anim_status(ANIM.ray_growth, true)
        c.anim_time = 0
        c.anim_keyframe = 0
    end

    local time = c.anim_time
    local scale
    if time <= 21 * 4096 then
        if time > 13 * 4096 then
            scale = asr(time - 13 * 4096, 3)
        else
            scale = 0
        end
    else
        scale = 4096
    end

    sh.entity.bone_flag_set(slot, 0, 0)
    sh.entity.bone_translation_set(slot, 0, asr(c.pos_x, 4), asr(c.pos_y, 4), asr(c.pos_z, 4))
    sh.entity.bone_rotation_set(slot, 0, c.rot_x, c.rot_y, c.rot_z)
    sh.entity.anim_step(slot)

    for bone = 6, 10 do
        sh.entity.bone_scale(slot, bone, scale, scale, scale)
    end
end
