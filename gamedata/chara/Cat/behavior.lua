-- Cat (map1_s01, the school locker): jumps out and runs off when the cutscene sets its
-- control state; meows on fixed keyframes of the jump and the run.

local ANIM = clips("Cat", { jump = 1, run = 2, idle_to_jump = 3 })

local function start(c, clip, keyframe)
    c.anim_status = anim_status(clip, true)
    c.anim_time = keyframe * 4096
    c.anim_keyframe = keyframe
end

-- keyframe (time >> 12) within [from, from + 2]
local function near(keyframe, from)
    return keyframe - from >= 0 and keyframe - from < 3
end

function update(slot)
    local c = record(slot)
    if c.control_state == 0 then
        c.control_state = 1
        c.anim_status = anim_status(ANIM.idle_to_jump, true)
        c.anim_time = 7 * 4096
        c.anim_alpha = 0
        c.state_step = 0
        c.anim_keyframe = 7
        c.pos_y = 0
        c.cat_meow_toggle = 0
    end

    if c.state_step == 0 then
        if c.control_state == 2 then
            start(c, ANIM.jump, 7)
        elseif c.control_state == 3 then
            start(c, ANIM.run, 23)
        end
        c.state_step = c.state_step + 1
    end

    sh.entity.bone_flag_set(slot, 0, 0)
    sh.entity.bone_translation_set(slot, 0, asr(c.pos_x, 4), asr(c.pos_y, 4), asr(c.pos_z, 4))
    sh.entity.bone_rotation_set(slot, 0, c.rot_x, c.rot_y, c.rot_z)
    sh.entity.anim_step(slot)

    -- Meow twice per clip: the toggle picks the first or the second keyframe window.
    local keyframe = asr(c.anim_time, 12)
    local second = c.cat_meow_toggle ~= 0
    local meow
    if c.anim_status == anim_status(1, true) then
        meow = (not second and near(keyframe, 20)) or (second and near(keyframe, 25))
    else
        meow = (not second and near(keyframe, 29)) or (second and near(keyframe, 36))
    end
    if meow then
        sh.entity.sfx_pitched(slot, sound(c, "idle", 1447), 128, sh.rng.uint(-7, 8))
        c.cat_meow_toggle = c.cat_meow_toggle ~ 1
    end
end
