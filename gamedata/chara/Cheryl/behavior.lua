-- Cheryl (map0_s00, the opening street): idles, walks or runs ahead of Harry as the cutscenes
-- set npc_control_state; collides with walls, falls to the ground; her footsteps fade with the
-- distance to Harry while he can move.

local ANIM = clips("Cheryl", { idle = 1, walk_forward = 2, run_forward = 3 })
local CHARA_ID = sh.chara_id("Cheryl") or 28

local DECEL = 1638            -- Q12(0.4) per 30 fps tick
local MAX_SPEED = 10240       -- Q12(2.5)
local RUN_SPEED_LIMIT = 6144  -- Q12(1.5): above it the run clip plays at its top speed
local RUN_CLIP_RATE = 76185   -- Q12(18.6) keyframes per second per (metre per second)
local RUN_CLIP_TOP = 114277   -- Q12(27.9) - 1

-- Turn per tick (Q12 * 16); the special walks 3 and 4 circle at +-7 per frame time.
local turn = 0

local function slow_down(c)
    if c.npc_move_distance ~= 0 then
        c.npc_move_distance = c.npc_move_distance - timestep_30fps(DECEL) * 2
        if c.npc_move_distance < 0 then
            c.npc_move_distance = 0
        end
    end
end

-- Her own footsteps while Harry walks: volume from the squared distance (Q8 units), loudest at
-- 2000 and silent at 12 000 000 .
local function footstep_by_distance(c, status, keyframe0, keyframe1, distance_sq, pitch)
    if c.anim_status ~= status then
        return false
    end
    local d = distance_sq
    if d >= 4000000 then
        if d > 12000000 then
            d = 12000000
        end
    else
        d = 4000000
    end
    local volume = ((12000000 - d) >> 16) & 0xFF
    if c.anim_keyframe >= keyframe1 then
        if c.npc_flags & 0x10 == 0 then
            sh.entity.sfx_at(c.slot, sound(c, "footstep", 1353), volume, pitch)
            c.npc_flags = c.npc_flags | 0x10
            return true
        end
    else
        c.npc_flags = c.npc_flags & ~0x10
    end
    if c.anim_keyframe >= keyframe0 then
        if c.npc_flags & 0x20 == 0 then
            sh.entity.sfx_at(c.slot, sound(c, "footstep", 1353), volume, pitch)
            c.npc_flags = c.npc_flags | 0x20
            return true
        end
    else
        c.npc_flags = c.npc_flags & ~0x20
    end
    return false
end

local function control_update(c)
    turn = 0
    local state = c.npc_control_state
    if state == 0 then
        slow_down(c)
        anim_status_set(c, ANIM.idle, false)
        anim_state_reset(c)
        c.npc_move_distance = 0
    elseif state == 1 then
        anim_status_set(c, ANIM.walk_forward, false)
        anim_state_reset(c)
    elseif state == 2 then
        anim_status_set(c, ANIM.run_forward, false)
        anim_state_reset(c)
    elseif state == 3 or state == 4 then
        slow_down(c)
        turn = s32(sh.game.delta_time() * (state == 3 and 7 or -7))
        anim_status_set(c, ANIM.walk_forward, false)
        anim_state_reset(c)
        sh.entity.set(0, "player_heading_angle", 0)
        c.npc_move_distance = 0
    end

    local _, ground_type = sh.entity.ground_at(c.pos_x, c.pos_z)
    local _, pitch0, pitch1 = sh.entity.footstep_sfx(ground_type, c.chara_id)

    local dx = asr(c.pos_x - sh.entity.get(0, "pos_x"), 4)
    local dz = asr(c.pos_z - sh.entity.get(0, "pos_z"), 4)
    local distance_sq = s32(s32(dx * dx) + s32(dz * dz))

    state = c.npc_control_state
    local walk = anim_status(ANIM.walk_forward, true)
    local run = anim_status(ANIM.run_forward, true)
    if sh.entity.player_control_disabled() ~= 0 then
        if state == 1 then
            footstep_pair(c, walk, 16, 28, sound(c, "footstep", 1353), pitch0)
        elseif state == 2 then
            footstep_pair(c, run, 53, 42, sound(c, "footstep", 1353), pitch1)
        end
    else
        if state == 1 then
            footstep_by_distance(c, walk, 16, 28, distance_sq, pitch0)
        elseif state == 2 then
            footstep_by_distance(c, run, 53, 42, distance_sq, pitch1)
        end
    end

    c.rot_y = angle_abs(c.rot_y + asr(turn, 4))
    c.heading_angle = c.rot_y
    c.move_speed = c.npc_move_distance
    c.fall_speed = c.fall_speed + sh.game.gravity_speed()
    sh.entity.bone_flag_set(c.slot, 0, 0)
    sh.entity.bone_rotation_set(c.slot, 0, c.rot_x, c.rot_y, c.rot_z)
end

-- Walks with collision: the wall test gives the height change and the ground under the step.
local function movement_update(c)
    local dt = sh.game.delta_time()
    local dx, dz = step_along(q12_mul(c.move_speed, dt), c.heading_angle)
    local dy = q12_mul(c.fall_speed, dt)
    local _, wall_dy, _, ground = sh.entity.wall_detect(c.slot, dx, dy, dz)
    c.pos_x = c.pos_x + dx
    c.pos_y = c.pos_y + wall_dy
    c.pos_z = c.pos_z + dz
    if c.pos_y > ground then
        c.pos_y = ground
        c.fall_speed = 0
    end
    sh.entity.bone_translation_set(c.slot, 0, asr(c.pos_x, 4), asr(c.pos_y, 4), asr(c.pos_z, 4))
end

local function anim_update_cheryl(c)
    local speed = c.move_speed
    if speed > MAX_SPEED then
        speed = MAX_SPEED
    end
    c.move_speed = speed
    if c.npc_control_state == 2 then
        local duration
        if speed <= RUN_SPEED_LIMIT then
            duration = q12_mul(speed, RUN_CLIP_RATE)
        else
            duration = RUN_CLIP_TOP
        end
        sh.entity.clip_duration_set(c.slot, anim_status(ANIM.run_forward, true), duration)
    end
    if c.npc_freeze == 0 then
        sh.entity.anim_step(c.slot)
    end
end

function update(slot)
    local c = record(slot)
    if c.chara_id ~= CHARA_ID then
        collision_reset(c)
        turn = 0
    end
    control_update(c)
    movement_update(c)
    anim_update_cheryl(c)
end
