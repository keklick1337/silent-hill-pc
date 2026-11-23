-- Locker dead body (map1_s03, the Otherworld school's locker room): hidden and without collision until
-- M1S03_LockerBodySceneStarted; then it hangs in the locker and falls out (no collision) until M1S03_LockerBodyFellOut holds it on the floor,
-- where it blocks like an NPC.

local ANIM = clips("LockerDeadBody", { fall_out = 1, hang_hold = 2, fallen_hold = 3 })

local COLLISION_IGNORE = 0
local COLLISION_NPC = 3
local VISIBLE = 2 -- animation flag bit 1

-- Clip, time and keyframe at once (the game's Chara_AnimSet).
local function anim_set(c, status, keyframe)
    c.anim_status = status
    c.anim_time = keyframe * 4096
    c.anim_keyframe = keyframe
end

function update(slot)
    local c = record(slot)
    if c.control_state == 0 then
        c.anim_alpha = 0
        anim_set(c, anim_status(ANIM.hang_hold, true), 0)
        c.control_state = c.control_state + 1
        c.pos_y = 0
    end

    -- A spawn of the engine is staged by no story: it lies on the floor (both flags as set).
    local unstaged = engine_spawn(c)
    if not unstaged and not sh.flag_get("M1S03_LockerBodySceneStarted") then
        c.anim_flags = c.anim_flags & ~VISIBLE
        sh.entity.set_collision_state(slot, COLLISION_IGNORE)
        return
    end
    c.anim_flags = c.anim_flags | VISIBLE

    if not unstaged and not sh.flag_get("M1S03_LockerBodyFellOut") then
        if c.anim_status == anim_status(ANIM.hang_hold, true) then
            c.anim_status = anim_status(ANIM.fall_out, true)
        end
        sh.entity.set_collision_state(slot, COLLISION_IGNORE)
    else
        anim_set(c, anim_status(ANIM.fallen_hold, true), 16)
        sh.entity.set_collision_state(slot, COLLISION_NPC)
    end

    sh.entity.root_transform(slot)
    if unstaged then
        -- Away from map1_s03 the type may have no clip table: it then keeps its pose.
        pcall(sh.entity.anim_step, slot)
    else
        sh.entity.anim_step(slot)
    end

    -- A 0.33 m cylinder 0.06 m to the side and 0.93 m ahead, its top 0.3 m above the floor.
    c.box_top = -1228
    c.cylinder_offset_x = 245
    c.cylinder_offset_z = 3809
    c.box_bottom = 0
    c.box_height = 0
    c.box_offset_y = 0
    c.box_offset_x = 0
    c.box_offset_z = 0
    c.cylinder_extent = 0
    c.cylinder_radius = 1351
    sh.entity.shape_offsets_update(slot)
end
