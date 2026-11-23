-- Parasite (map6_s04, the final boss scene): plays its loop where the cutscene put it.
-- Its clip table is gamedata/tables/anim/parasite.json.

function update(slot)
    local c = record(slot)
    if c.control_state == 0 then
        c.anim_alpha = 0
        c.control_state = 1
        c.state_step = 0
        -- Clip 1, playing, from keyframe 0.
        c.anim_status = anim_status(1, true)
        c.anim_time = 0
        c.anim_keyframe = 0
    end
    sh.entity.bone_flag_set(slot, 0, 0)
    sh.entity.bone_translation_set(slot, 0, asr(c.pos_x, 4), asr(c.pos_y, 4), asr(c.pos_z, 4))
    sh.entity.bone_rotation_set(slot, 0, c.rot_x, c.rot_y, c.rot_z)
    sh.entity.anim_step(slot)
end
