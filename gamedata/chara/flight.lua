-- Flying characters (Air Screamer, Night Flutter): engine calls, integer helpers, module state and the flight
-- dynamics. Loaded before the behaviour that names it ("-- uses: flight").

local E = sh.entity

MAP0_S01 = false
P = record(0)

function dt()
    return sh.game.delta_time()
end

-- Module state of the type (one per Lua state, like the overlay's globals).
S = { flags = 0, sight = 0, player_distance = 0, player_angle = 0, reach = 0, lunge_reach = 0, dyn = {}, points = {},
      saved = {} }
for i = 0, 37 do
    S.dyn[i] = 0
end
for i = 0, 11 do
    S.points[i] = { x = 0, y = 0, z = 0 }
end

local function v3()
    return { x = 0, y = 0, z = 0 }
end
EYE, PLAYER_HEAD, SAVED_AIM, RAY_DELTA, PROBE, AHEAD, SETTLE_STEP, STEP, VELOCITY, PUSH_STEP, PUSH_VELOCITY =
    v3(), v3(), v3(), v3(), v3(), v3(), v3(), v3(), v3(), v3(), v3()
FRONT_POINT, HOME_A, HOME_B, ORBIT_POINT, HOME_C, HOME_D, PLAYER_STEP = v3(), v3(), v3(), v3(), v3(), v3(), v3()
RAY = { has_hit = 0, ground_type = 0, x = 0, y = 0, z = 0, distance = 0, ground_height = 0 }
HIT = { x = 0, y = 0, z = 0 }
SLOPE = { [0] = 0, 0 }

-- The three 4-bit fields at the start of the type's record block.
function role(c)
    return c.air_screamer_modes & 0xF
end
function side(c)
    return (c.air_screamer_modes >> 4) & 0xF
end
function attack_set(c)
    return (c.air_screamer_modes >> 8) & 0xF
end
local function set_nibble(c, shift, v)
    c.air_screamer_modes = (c.air_screamer_modes & ~(0xF << shift)) | ((v & 0xF) << shift)
end
function set_role(c, v)
    set_nibble(c, 0, v)
end
function set_side(c, v)
    set_nibble(c, 4, v)
end
function set_attack_set(c, v)
    set_nibble(c, 8, v)
end

-- The angle's low 12 bits as a signed value in [-2048, 2047].
function angle_norm(a)
    a = a & 0xFFF
    if a >= 0x800 then
        return a - 0x1000
    end
    return a
end

-- True when v is outside [-r, r) (one unsigned compare).
function signed_range(v, r)
    return ((v + r) & 0xFFFFFFFF) > ((r * 2) & 0xFFFFFFFF)
end

-- |v| reduced by s, not past zero, with v's sign.
function preserved_sign_subtract(v, s)
    local sign = asr(v, 31)
    local d = s32(((v ~ sign) - sign) - s)
    return s32(((d & ~asr(d, 31)) ~ sign) - sign)
end

function rand_q12()
    local r = sh.rng.rand16()
    return asr((r * 2) ~ r, 3) & 0xFFF
end

function distance_2d_q12(ax, az, bx, bz)
    local dx = s32(bx - ax)
    local dz = s32(bz - az)
    return E.sqrt_q12(s32(q12_mul(dx, dx) + q12_mul(dz, dz)))
end

-- Packed: low 16 bits the heading from a to b, the rest the pitch.
function angles_between(ax, ay, az, bx, by, bz)
    return E.angles_between(ax, ay, az, bx, by, bz)
end

function heading_between(ax, az, bx, bz)
    return sh.math.atan2(s32(bx - ax), s32(bz - az))
end

function ground_height(x, z)
    return E.ground_height(x, z)
end

function ground_kind(x, z)
    return E.ground_kind(x, z)
end

function slope_force(x, z, dx, dz)
    SLOPE[0], SLOPE[1] = E.slope_force(x, z, dx, dz)
end

local function ray_store(blocked, has_hit, ground_type, x, y, z, distance, height)
    RAY.has_hit, RAY.ground_type, RAY.x, RAY.y, RAY.z = has_hit, ground_type, x, y, z
    RAY.distance, RAY.ground_height = distance, height
    return blocked ~= 0
end

function ray_trace(from, to)
    return ray_store(E.ray_trace(from.x, from.y, from.z, to.x, to.y, to.z))
end

function ray_los(from, delta, c)
    return ray_store(E.ray_los(c.slot, from.x, from.y, from.z, delta.x, delta.y, delta.z))
end

function wall_detect(c, offset)
    local result
    result, HIT.x, HIT.y, HIT.z = E.collide(c.slot, offset.x, offset.y, offset.z)
    return result
end

function in_active_cell(x, z)
    return E.in_active_cell(x, z) ~= 0
end

-- The home of the spawn row; a character without one (console, menu, a script's create) gets the "home"
-- fallback (chara/fallbacks.lua: where it was spawned).
function spawn_point(index, c)
    return map_data(c, "home", E.spawn_point, index)
end

function spawn_flags(index)
    return E.spawn_flags(index)
end

function chara_spawn(chara_id, slot_index, x, z, rot, step)
    return E.chara_spawn(chara_id, slot_index, x, z, rot, step)
end

function npc_mask()
    return E.npc_mask()
end

function vblanks()
    return E.vblanks()
end

function event_npc(i)
    return E.event_npc(i)
end

function player_aiming()
    return (E.player_aim()) ~= 0
end

function player_aim_distance()
    return select(2, E.player_aim())
end

function enemy_defeated(c)
    E.enemy_defeated(c.slot)
end

function anim_status_handover(c, kind, status, kf)
    E.anim_status_handover(c.slot, kind, status, kf)
end

function clip_start(c, status)
    return E.clip_start(c.slot, status)
end

function flyby_active()
    return E.flyby_active() ~= 0
end

function chara_param_q12(chara_id, name, fallback)
    return E.chara_param_q12(chara_id, name, fallback)
end

function chara_param_int(chara_id, name, fallback)
    return E.chara_param_int(chara_id, name, fallback)
end

function sfx_3d(c, sfx, vol, kind)
    E.sfx_3d(c.slot, sfx, vol, kind)
end

function sfx_stop(sfx)
    E.sfx_stop(sfx)
end

-- One tick of the current clip (frozen: no time passes).
function anim_play(c, frozen)
    E.anim_play(c.slot, frozen and 1 or 0)
end

function ranged_attack(c)
    return E.ranged_attack(c.slot)
end

-- Flight dynamics: every speed of the body (forward, fall, turn, pitch, roll, head) moves towards
-- its goal (S.dyn: per axis a pending flag, acceleration, goal speed, distance left).

-- Speed moved towards `target` by `accel` per second, not past it.
function speed_approach(speed, accel, target)
    local a = accel
    local adj = speed
    local diff = s32(target - adj)

    if a ~= 0 and diff ~= 0 then
        local below = (diff < 1) and 1 or 0
        local above = (diff >= 1) and 1 or 0

        if ((diff ~= 0) and 1 or 0) & ((a > 0) and below or above) ~= 0 then
            a = s32(-a)
        end

        if a > 0 then
            if diff > 0 then
                adj = s32(adj + q12_mul(dt(), a))
                if target < adj then
                    adj = target
                end
            end
        elseif diff < 0 then
            adj = s32(adj + q12_mul(dt(), a))
            if adj < target then
                adj = target
            end
        end
    end

    return adj
end

-- As above, with the target speed capped so that it can still stop within `dist`.
function speed_approach_stopping(speed, accel, target, dist)
    if accel == 0 then
        return speed
    end

    if target ~= 0 then
        if accel < 0 then
            accel = s32(-accel)
        end

        local stop_dist = s32(q12_mul(target, target) << 12)
        stop_dist = idiv(stop_dist, s32(accel * 2))

        if dist <= 0 then
            dist = s32(-dist)
        end

        if dist < stop_dist then
            local v = E.sqrt_q12(q12_mul(dist, s32(accel * 2)))
            if v == 0 then
                v = 1
            end

            if target > 0 then
                if v < target then
                    target = v
                end
            else
                if target < -v then
                    target = -v
                end
            end
        end
    end

    return speed_approach(speed, accel, target)
end

-- Goal row `idx` of S.dyn: turn by `angle` (half-degree dead zone) at 120 degrees per second.
function tilt_goal_set(angle, idx)
    if angle > 5 then
        angle = angle - 5
    else
        if angle < -5 then
            angle = angle + 5
        else
            angle = 0
        end
    end

    local angle1
    if angle > 0 then
        angle1 = 1365
    elseif angle < 0 then
        angle1 = -1365
    else
        angle1 = 0
    end

    S.dyn[idx * 4 + 2] = angle1
    S.dyn[idx * 4 + 1] = 5461 -- 1.3333
    S.dyn[idx * 4 + 3] = angle
    S.dyn[idx * 4 + 0] = 1
end

function bank_goals_set(c, angle0, rot_speed_y)
    local angle1 = q12_mul(angle0, -81)
    if angle1 > 512 then
        angle1 = 512
    elseif angle1 < -512 then
        angle1 = -512
    end
    tilt_goal_set(angle1 - c.rot_x, 2)

    angle1 = q12_mul(rot_speed_y, 614)
    if angle1 > 512 then
        angle1 = 512
    elseif angle1 < -512 then
        angle1 = -512
    end
    tilt_goal_set(angle1 - c.rot_z, 4)
end

function speeds_update(c)
    S.flags = s32(S.flags & ~0x2B)

    local accel = S.dyn[1]
    local limit = S.dyn[2]
    local speed = c.move_speed
    if S.dyn[0] ~= 0 then
        S.dyn[0] = 0
        speed = speed_approach_stopping(speed, accel, limit, S.dyn[3])
    else
        speed = speed_approach(speed, accel, limit)
    end
    c.move_speed = speed

    speed = c.fall_speed
    accel = S.dyn[5]
    limit = S.dyn[6]
    if S.dyn[4] ~= 0 then
        S.dyn[4] = 0
        speed = speed_approach_stopping(speed, accel, limit, S.dyn[7])
    else
        speed = speed_approach(speed, accel, limit)
    end
    c.fall_speed = speed

    speed = c.rot_speed_y
    accel = S.dyn[13]
    limit = S.dyn[14]
    -- The turn row's request stays set (it is not cleared like the others).
    if S.dyn[12] ~= 0 then
        speed = speed_approach_stopping(speed, accel, limit, S.dyn[15])
    else
        speed = speed_approach(speed, accel, limit)
    end
    c.rot_speed_y = speed

    c.rot_y = c.rot_y + q12_mul(dt(), speed)
    if flight_mode(c) == 3 then
        bank_goals_set(c, c.fall_speed, c.rot_speed_y)
    else
        bank_goals_set(c, 0, 0)
    end

    local pitch_speed = speed_approach_stopping(c.rot_speed_x, S.dyn[9], S.dyn[10], S.dyn[11])
    c.rot_speed_x = pitch_speed
    c.rot_x = c.rot_x + q12_mul(dt(), pitch_speed)
    local roll_speed = speed_approach_stopping(c.rot_speed_z, S.dyn[17], S.dyn[18], S.dyn[19])
    c.rot_speed_z = roll_speed

    c.rot_z = c.rot_z + q12_mul(dt(), roll_speed)
    local target_turn_speed = speed_approach_stopping(c.angle_to_target_speed, S.dyn[21], S.dyn[22], S.dyn[23])
    c.angle_to_target_speed = target_turn_speed
    c.angle_to_target = c.angle_to_target + q12_mul(dt(), target_turn_speed)
end
