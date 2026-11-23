-- Larval Stalker (the school, the nowhere of the last nightmare): the crawling grey child. It
-- wanders, turns towards Harry when it sees him, creeps after him and grabs his legs; it trips
-- when he runs past. Hits knock it back or down; it fades out after it has fallen enough times.

-- Control states.
local GONE, WANDER, LOOK_AROUND, CREEP, GRAB, CIRCLE, WATCH, STAGGER, TRIP, STUN, DOWN_FRONT, DOWN_BACK, DEATH =
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13

-- Flags of larval_flags.
local HIT = 0x2
local SAW_PLAYER = 0x4
local GRAB_ATTACK = sh.entity.attack_number("LarvalStalkerGrab")
local DRAIN = sh.entity.attack_number("LarvalStalkerDrain")
local STUCK = 0x8
local DEATH_CLIP = 0x10
local FALLING = 0x20
local GRABBING = 0x40
local HURT = 0x80

local A = clips("LarvalStalker", {
    grab_attack = 1, stun_back_end = 2, death_back_start = 3, death_front_start = 4, stun_back_to_idle = 5,
    stun_front_to_idle = 6, stun_back_start = 7, stun_front_start = 8, stun_front_end = 9, idle = 10,
    walk_forward = 11, walk_forward_alt = 12, death_front_end = 13, death_back_cont = 14,
    stun_back_to_idle_alt = 15, stun_front_to_idle_alt = 16, stun_front_cont = 17, stun_back_recoil = 18,
})
local function st(clip, active)
    return anim_status(clip, active)
end

-- Collision shapes of the keyframes as the game lays them out: one block; the recoil clips index
-- runs of it by keyframe and may step into the next table.
local K = {
    [0] = { -1146, 0, -327, -983, 942, 491, 0, -1187, 0, -1187 },  -- 0 DA928 (1)
    { -1064, 0, 0, -983, 942, 491, 0, -1187, 0, -1187 },  -- 1 DA93C
    { -901, 0, 0, -860, 942, 491, -40, 901, 0, 1024 },  -- 2 DA950
    { -2785, 0, -819, -2334, 655, 368, -40, 163, 0, 0 },  -- 3 DA964 (36)
    { -2867, 0, -737, -2416, 696, 409, 0, -40, 0, 0 },
    { -2867, 0, -1310, -2621, 737, 409, -40, -450, 0, -409 },
    { -2621, 0, -1269, -2457, 860, 368, -40, -860, 0, -532 },
    { -2580, 0, -1105, -2334, 942, 368, -40, -1064, 0, -491 },
    { -2088, 0, -942, -1802, 942, 409, 0, -1187, 0, -655 },
    { -1556, 0, -655, -1474, 942, 409, 0, -1024, 0, -778 },
    { -1392, 0, -491, -1310, 942, 450, 0, -1024, 0, -901 },
    { -1269, 0, -245, -1146, 942, 491, 0, -1064, 0, -1105 },
    { -1187, 0, -245, -1105, 942, 491, 0, -983, 0, -1105 },
    { -1064, 0, -204, -942, 983, 491, 0, -1064, 0, -1146 },
    { -1187, 0, -286, -1064, 983, 491, 0, -1146, 0, -1146 },
    { -1228, 0, -409, -1146, 1024, 491, 0, -1187, 0, -1187 },
    { -1146, 0, -286, -1024, 1064, 491, 0, -1187, 0, -1187 },
    { -1064, 0, -245, -983, 1105, 491, 0, -1187, 0, -1187 },
    { -1187, 0, -327, -1105, 1064, 491, -122, -1146, 0, -1105 },
    { -1228, 0, -368, -1146, 1024, 491, -163, -1146, -81, -1064 },
    { -1310, 0, -614, -1228, 1024, 491, -286, -1146, -204, -1024 },
    { -1392, 0, -614, -1310, 983, 450, -368, -1105, -286, -942 },
    { -1515, 0, -614, -1392, 983, 450, -409, -983, -327, -696 },
    { -1638, 0, -532, -1433, 983, 409, -532, -819, -368, -614 },
    { -1638, 0, -532, -1433, 942, 409, -532, -655, -491, -491 },
    { -1638, 0, -532, -1433, 901, 368, -614, -573, -532, -409 },
    { -1679, 0, -532, -1474, 901, 368, -614, -491, -532, -286 },
    { -1761, 0, -573, -1556, 860, 368, -614, -368, -491, -245 },
    { -1884, 0, -901, -1679, 819, 368, -532, -286, -409, -81 },
    { -2211, 0, -1310, -1843, 819, 327, -655, -122, -368, -81 },
    { -2498, 0, -1310, -2088, 819, 327, -614, -81, -368, 40 },
    { -2539, 0, -1392, -2252, 737, 327, -573, 245, -327, 286 },
    { -2580, 0, -1269, -2375, 737, 327, -450, 409, -245, 409 },
    { -2703, 0, -1843, -2416, 737, 327, -450, 614, -163, 573 },
    { -2703, 0, -1884, -2498, 696, 327, -245, 819, -122, 614 },
    { -2744, 0, -1925, -2498, 655, 327, -204, 819, -81, 655 },
    { -2744, 0, -1925, -2498, 655, 327, -163, 737, -122, 491 },
    { -2990, 0, -1925, -2498, 655, 327, -81, 450, -81, 368 },
    { -2908, 0, -1884, -2416, 655, 368, -40, 286, 0, 286 },
    { -2785, 0, -983, -2416, 696, 368, -40, 286, -40, 122 },  -- 39 DAC34 (25)
    { -2867, 0, -1024, -2498, 696, 368, -40, 286, -40, 163 },
    { -2867, 0, -1105, -2662, 737, 368, -40, 286, -40, 245 },
    { -2703, 0, -1146, -2539, 778, 327, -81, 491, -40, 491 },
    { -2457, 0, -1515, -2293, 778, 327, -122, 983, -81, 655 },
    { -1843, 0, -983, -1761, 819, 368, -122, 1269, -40, 860 },
    { -1187, 0, -286, -1105, 860, 409, -81, 983, -40, 1105 },
    { -901, 0, 0, -778, 860, 409, -81, 983, -40, 1146 },
    { -942, 0, 0, -901, 860, 409, -81, 983, -40, 1146 },
    { -901, 0, 0, -860, 860, 409, -81, 983, -40, 1146 },
    { -942, 0, -81, -901, 860, 409, -81, 983, -40, 1064 },
    { -1064, 0, -286, -1024, 819, 368, -81, 983, -40, 1064 },
    { -1187, 0, -409, -1146, 819, 368, -81, 1146, -40, 1024 },
    { -1310, 0, -532, -1269, 819, 368, -81, 1146, -40, 1024 },
    { -1474, 0, -696, -1433, 778, 368, -81, 1228, -40, 1024 },
    { -1638, 0, -819, -1556, 778, 368, -81, 1392, -40, 1024 },
    { -1679, 0, -860, -1597, 778, 368, -81, 1392, -40, 1024 },
    { -1802, 0, -983, -1720, 737, 368, -245, 1269, -122, 1024 },
    { -2007, 0, -1269, -1884, 737, 368, -368, 1064, -286, 860 },
    { -2498, 0, -1679, -2457, 696, 368, -327, 819, -204, 573 },
    { -2580, 0, -1720, -2498, 696, 368, -327, 614, -163, 409 },
    { -2744, 0, -1843, -2498, 696, 368, -245, 573, -163, 286 },
    { -2785, 0, -1884, -2457, 655, 368, -245, 491, -163, 286 },
    { -2949, 0, -1966, -2457, 655, 368, -204, 491, -122, 286 },
    { -2949, 0, -1966, -2375, 655, 368, -122, 450, -40, 286 },
    { -2949, 0, -1966, -2498, 655, 368, -81, 409, -40, 204 },  -- 64 DAE28 (15)
    { -2949, 0, -1966, -2580, 655, 368, -40, 122, -40, 81 },
    { -2867, 0, -1433, -2580, 655, 368, -40, -450, -40, -204 },
    { -2621, 0, -1269, -2416, 696, 368, -40, -778, -40, -450 },
    { -2580, 0, -1269, -2375, 696, 327, -40, -1105, -40, -737 },
    { -2334, 0, -1269, -2129, 737, 327, 0, -1310, -40, -942 },
    { -2129, 0, -819, -1966, 778, 327, 0, -1392, -40, -983 },
    { -1638, 0, -819, -1556, 819, 327, 0, -1556, 0, -1187 },
    { -1269, 0, -491, -1187, 860, 368, 0, -1351, 0, -1187 },
    { -1146, 0, -245, -1105, 901, 409, 0, -1351, 0, -1187 },
    { -1024, 0, -163, -983, 942, 409, 0, -1351, 0, -1187 },
    { -1187, 0, -409, -1146, 942, 409, 0, -1351, 0, -1187 },
    { -1228, 0, -450, -1187, 942, 409, 0, -1351, 0, -1187 },
    { -1105, 0, -245, -1064, 983, 450, 0, -1351, 0, -1187 },
    { -942, 0, -81, -901, 983, 450, 0, -1351, 0, -1187 },
    { -942, 0, -81, -901, 983, 450, 0, -1351, 0, -1187 },  -- 79 DAF54
    { -2785, 0, -819, -2334, 655, 368, -40, 163, 0, 204 },  -- 80 DAF68 (7)
    { -2785, 0, -1064, -2334, 655, 368, -40, 245, 0, 245 },
    { -2908, 0, -2088, -2457, 696, 368, -81, 532, 0, 409 },
    { -2621, 0, -1761, -2416, 737, 368, -81, 901, -40, 614 },
    { -2252, 0, -1351, -2007, 778, 409, -81, 1024, -40, 655 },
    { -1433, 0, -532, -1269, 860, 450, -81, 1024, -40, 1024 },
    { -942, 0, 0, -819, 983, 450, -122, 1024, -40, 1064 },
    { -983, 0, 0, -860, 983, 491, -40, 983, 0, 1024 },  -- 87 DAFF4
    { -983, 0, 0, -901, 942, 491, 0, 901, 0, 983 },  -- 88 DB008
    { -2785, 0, -819, -2334, 655, 368, -40, 204, 0, 0 },  -- 89 DB01C
    { -2785, 0, -819, -2334, 655, 409, 0, 204, 0, 0 },  -- 90 DB030
}
local K_GRAB, K_BACK_END, K_FRONT_DEAD, K_BACK_RECOIL, K_FRONT_RECOIL, K_BACK_START = 0, 1, 2, 3, 39, 64
local K_BACK_START_END, K_FRONT_START, K_FRONT_START_END, K_FRONT_END, K_IDLE, K_WALK = 79, 80, 87, 88, 89, 90

local function player()
    return record(0)
end

local function in_range(keyframe, low, high)
    return keyframe >= low and keyframe <= high
end

local function stumble(c)
    sh.entity.sfx_3d(c.slot, sound(c, "stumble", 1429), 128, 0)
end

local function start(c)
    c.control_state = LOOK_AROUND
    c.anim_time = 0
    anim_set(c, st(A.idle, false), 162)
    c.larval_flags = 0
    c.anim_alpha = 0
    c.state_step = 0
    sh.entity.clip_table_attach(c.slot)
    c.health = sh.entity.param(c.slot, "health", 1228800)
    c.fall_speed = 0
    c.move_speed = 0
    damage_clear(c)
    sh.entity.set_collision_state(c.slot, 0)
    c.heading_angle = c.rot_y
    sh.entity.props_clear(c.slot)
    local p = player()
    c.larval_target_x = p.pos_x
    c.larval_target_z = p.pos_z
end

local function react_to_damage(c)
    if c.damage_amount <= 0 or c.health <= 0 then
        return
    end
    local p = player()
    sh.entity.sfx_3d(c.slot, 0, 128, 0)
    c.larval_flags = c.larval_flags | HURT
    c.health = max(c.health - c.damage_amount, 0)
    c.damage_x = c.damage_x + asr(s32(c.move_speed * sh.math.sin(c.heading_angle)), 12)
    c.damage_z = c.damage_z + asr(s32(c.move_speed * sh.math.cos(c.heading_angle)), 12)
    local dx, dz = c.damage_x, c.damage_z
    c.move_speed = idiv(s32(sh.math.sqrt(s32(s32(dx * dx) + s32(dz * dz))) << 12), 9830)
    c.fall_speed = idiv(s32(c.damage_y << 12), 9830)
    c.heading_angle = sh.math.atan2(dx, dz)
    c.larval_flags = c.larval_flags | HIT
    c.larval_target_x = p.pos_x
    c.larval_target_z = p.pos_z

    local clip = c.anim_status >> 1
    if c.control_state == DOWN_FRONT then
        if clip == A.stun_front_end then
            c.larval_recoil_keyframe = -1
            c.anim_status = st(A.stun_front_cont, false)
        end
    elseif c.control_state == DOWN_BACK then
        if clip == A.stun_back_end then
            c.larval_recoil_keyframe = -1
            c.anim_status = st(A.stun_back_recoil, false)
        end
    else
        local angle = angle_signed(heading_to(c.pos_x, c.pos_z, p.pos_x, p.pos_z) - c.rot_y)
        if angle < 0 then
            angle = -angle
        end
        local keyframe = asr(c.anim_time, 12)
        if c.health > 409600 then
            c.control_state = STUN
            if angle < 1024 then
                local offset = s16(keyframe - 41)
                if in_range(keyframe, 41, 65) then
                elseif in_range(keyframe, 66, 72) then
                    c.larval_recoil_keyframe = offset
                    if offset == 30 then
                        c.anim_time = c.anim_time - 4096
                    end
                    c.anim_status = st(A.stun_back_to_idle, false)
                    c.larval_recoil_keyframe = 76 - asr(c.anim_time, 12)
                elseif in_range(keyframe, 73, 74) then
                    c.larval_recoil_keyframe = offset
                    c.anim_status = st(A.stun_back_to_idle, false)
                    c.larval_recoil_keyframe = 5
                elseif in_range(keyframe, 75, 78) then
                    c.larval_recoil_keyframe = offset
                    if offset == 36 then
                        c.anim_time = c.anim_time - 4096
                    end
                    c.anim_status = st(A.stun_back_to_idle, false)
                    c.larval_recoil_keyframe = 79 - asr(c.anim_time, 12)
                else
                    c.larval_recoil_keyframe = 0
                    c.anim_status = st(A.stun_back_to_idle, false)
                end
            else
                local offset = s16(keyframe - 86)
                if in_range(keyframe, 86, 104) then
                elseif in_range(keyframe, 105, 118) then
                    c.anim_status = st(A.stun_front_to_idle, false)
                    c.larval_recoil_keyframe = offset
                    c.larval_recoil_keyframe = 7 - asr(asr(c.anim_time, 12) - 104, 1)
                else
                    c.larval_recoil_keyframe = 0
                    c.anim_status = st(A.stun_front_to_idle, false)
                end
            end
        else
            if rng_int(0, 7) == 0 then
                sh.entity.enemy_defeated(c.slot)
            end
            if angle < 1024 then
                local offset = s16(keyframe - 41)
                if in_range(keyframe, 41, 51) then
                    c.anim_status = st(A.stun_back_start, false)
                    c.larval_recoil_keyframe = s16(asr(c.anim_time, 12) - 41)
                elseif in_range(keyframe, 52, 65) then
                    c.larval_recoil_keyframe = offset
                    c.anim_status = st(A.stun_back_end, false)
                elseif in_range(keyframe, 66, 72) then
                    c.larval_recoil_keyframe = offset
                    if offset == 30 then
                        c.anim_time = c.anim_time - 4096
                    end
                    c.anim_status = st(A.stun_back_start, false)
                    c.larval_recoil_keyframe = 76 - asr(c.anim_time, 12)
                elseif in_range(keyframe, 73, 74) then
                    c.anim_status = st(A.stun_back_start, false)
                    c.larval_recoil_keyframe = offset
                    c.anim_time = 5
                elseif in_range(keyframe, 75, 78) then
                    c.larval_recoil_keyframe = offset
                    if offset == 36 then
                        c.anim_time = c.anim_time - 4096
                    end
                    c.anim_status = st(A.stun_back_start, false)
                    c.anim_time = 79 - asr(c.anim_time, 12)
                else
                    if c.anim_status >> 1 ~= A.stun_back_start then
                        c.larval_recoil_keyframe = 0
                        c.anim_status = st(A.stun_back_start, false)
                    end
                end
                c.control_state = DOWN_BACK
            else
                local offset = s16(keyframe - 86)
                if in_range(keyframe, 86, 96) then
                    c.anim_status = st(A.stun_front_start, false)
                    c.larval_recoil_keyframe = s16(asr(c.anim_time, 12) - 86)
                elseif in_range(keyframe, 97, 104) then
                    c.larval_recoil_keyframe = offset
                    c.anim_status = st(A.stun_front_end, false)
                elseif in_range(keyframe, 105, 118) then
                    c.anim_status = st(A.stun_front_start, false)
                    c.larval_recoil_keyframe = offset
                    c.larval_recoil_keyframe = 7 - asr(asr(c.anim_time, 12) - 104, 1)
                else
                    if c.anim_status >> 1 ~= A.stun_front_start then
                        c.larval_recoil_keyframe = 0
                    end
                    c.anim_status = st(A.stun_front_start, false)
                end
                c.control_state = DOWN_FRONT
            end
        end
    end
    c.larval_turn = 0
    c.larval_timer = 0
end

-- Turns by `rate` per second towards the side of `delta` once it is at least `limit`.
local function turn(c, delta, limit, rate)
    if limit <= abs(delta) then
        if delta > 0 then
            c.rot_y = c.rot_y + q12_mul(sh.game.delta_time(), rate)
        else
            c.rot_y = c.rot_y - q12_mul(sh.game.delta_time(), rate)
        end
    end
end

-- The same, away from that side.
local function turn_away(c, delta, limit, rate)
    if limit <= abs(delta) then
        if delta < 0 then
            c.rot_y = c.rot_y + q12_mul(sh.game.delta_time(), rate)
        else
            c.rot_y = c.rot_y - q12_mul(sh.game.delta_time(), rate)
        end
    end
end

-- A step of the clip's own motion along the body (or backwards, `back`), `mult` / `div` of it.
local function clip_push(c, mult, div, back, precise)
    local step = idiv(s32(q12_mul(sh.entity.clip_duration(c.slot, c.anim_status), sh.game.delta_time()) * mult), div)
    local heading = back and c.rot_y + 2048 or c.rot_y
    c.larval_push_x = asr(s32(step * sh.math.sin(heading)), 12)
    c.larval_push_z = asr(s32(step * sh.math.cos(heading)), 12)
end

local function start_stagger(c)
    c.control_state = STAGGER
    stumble(c)
    c.larval_timer = 0
    c.anim_status = st(A.walk_forward_alt, false)
    c.larval_flags = c.larval_flags & ~STUCK
end

local function start_trip(c)
    c.anim_status = st(A.stun_front_to_idle, false)
    c.larval_timer = 0
    c.larval_recoil_keyframe = 0
    c.control_state = TRIP
    stumble(c)
end

local function control(c)
    local p = player()
    local dt = sh.game.delta_time()
    local delta = angle_signed(heading_to(c.pos_x, c.pos_z, p.pos_x, p.pos_z) - c.rot_y)
    local to_target = distance_2d(c.larval_target_x - c.pos_x, c.larval_target_z - c.pos_z)
    c.rot_y = angle_signed(c.rot_y)

    if c.anim_status >> 1 ~= A.grab_attack then
        c.attack_window = 0
    end

    local light = map_light() & 3
    local sight, sight_step
    if light == 0 then
        sight, sight_step = 10240, 32768
    elseif light == 2 then
        sight, sight_step = 14336, 40960
    else
        sight, sight_step = 2048, 6144
    end
    if distance_2d(p.pos_x - c.pos_x, p.pos_z - c.pos_z) < sight * 2 + sight_step then
        c.larval_target_x = p.pos_x
        c.larval_target_z = p.pos_z
    end

    local trip = c.larval_trip_count
    local state = c.control_state
    if state == GONE then
        c.move_speed = 0
        if rng_int(0, 31) == 0 then
            c.health = -1
        end
    elseif state == WANDER then
        speed_toward(c, 6144, 1228)
        if sh.entity.path_blocked(c.slot, 4096, c.rot_y) ~= 0 or rng_int(0, 63) == 0 then
            c.anim_status = st(A.idle, false)
            c.control_state = LOOK_AROUND
        end
        if sh.entity.player_visible(c.slot, 3, sight, sight_step) ~= 0 then
            c.control_state = WATCH
            c.anim_status = st(A.idle, false)
            c.larval_flags = c.larval_flags | SAW_PLAYER
        end
    elseif state == LOOK_AROUND then
        speed_toward(c, 6144, 0)
        if c.anim_status == st(A.walk_forward, true) then
            local step = q12_mul(dt, 341)
            if c.larval_turn > 0 then
                c.rot_y = c.rot_y + step
                c.larval_turn = c.larval_turn - step
                if c.larval_turn <= 0 then
                    c.larval_turn = 0
                    c.anim_status = st(A.idle, false)
                end
            elseif c.larval_turn < 0 then
                c.rot_y = c.rot_y - step
                c.larval_turn = c.larval_turn + step
                if not (c.larval_turn < 0) then
                    c.larval_turn = 0
                    c.anim_status = st(A.idle, false)
                end
            else
                c.anim_status = st(A.idle, false)
            end
        end
        if c.anim_status == st(A.idle, true) then
            local blocked = sh.entity.path_blocked(c.slot, 2457, c.rot_y) ~= 0
            if (blocked and rng_int(0, 3) == 0) or rng_int(0, 31) == 0 then
                c.larval_turn = rng_uint(-1024, 1023)
                c.anim_status = st(A.walk_forward, false)
            elseif rng_int(0, 15) == 0 then
                if not blocked then
                    c.control_state = WANDER
                    c.anim_status = st(A.walk_forward, false)
                end
            end
        end
        if sh.entity.player_visible(c.slot, 3, sight, sight_step) ~= 0 then
            c.control_state = WATCH
            c.anim_status = st(A.idle, false)
            c.larval_turn = 0
            c.larval_flags = c.larval_flags | SAW_PLAYER
        end
    elseif state == CREEP then
        speed_toward(c, 6144, 1228)
        if c.larval_flags & SAW_PLAYER ~= 0 and c.anim_status >> 1 == A.walk_forward
            and (to_target < 14336 - (trip << 12) or rng_int(0, 31) == 0) then
            c.control_state = WATCH
            c.anim_status = st(A.idle, false)
            c.larval_flags = c.larval_flags & ~STUCK
            return
        end
        if to_target >= (trip << 12) + 8192 or p.move_speed <= trip * 6144 + 2048 then
            if 512 > abs(delta) and to_target < (c.cylinder_radius + 204) + p.cylinder_radius then
                if rng_int(0, 7) == 0 then
                    local tx = c.larval_target_x + rng_int(0, 6143)
                    local tz = c.larval_target_z + rng_int(0, 6143)
                    c.larval_turn = sh.entity.heading_toward(c.slot, 8192, tx, tz, 4096, true)
                    turn(c, angle_signed(c.larval_turn - c.rot_y), 17, 341)
                else
                    turn_away(c, delta, 17, 341)
                end
                return
            end
            if asr(sight_step, 1) < to_target then
                if rng_int(0, 31) == 0 then
                    c.larval_turn = sh.entity.heading_toward(c.slot, 8192, c.larval_target_x, c.larval_target_z, 4096, true)
                end
                turn(c, angle_signed(c.larval_turn - c.rot_y), 17, 341)
                return
            end
            if c.larval_flags & STUCK ~= 0 then
                turn(c, angle_signed(c.larval_turn - c.rot_y), 17, 341)
                if rng_int(0, 15) == 0 then
                    c.larval_flags = c.larval_flags & ~STUCK
                end
                return
            end
            if group_flags(3) & 1 == 0 then
                turn(c, delta, 17, 341)
            else
                c.control_state = WATCH
                c.anim_status = st(A.idle, false)
            end
        else
            start_stagger(c)
        end
    elseif state == GRAB then
        turn(c, delta, 17, 341)
        if c.larval_flags & GRABBING ~= 0 then
            c.larval_anim_time = c.anim_time
            c.move_speed = 0
            c.larval_timer = c.larval_timer + dt
            if c.larval_timer > 7372 then
                if rng_int(0, 7) == 0 then
                    sh.entity.sfx_3d(c.slot, 0, 128, 0)
                    c.larval_timer = 0
                    if p.attacker_index == -1 then
                        p.attacker_index = npc_index(c.slot)
                    end
                    local attack = DRAIN
                    p.attack_received = attack
                    p.damage_amount = p.damage_amount
                        + idiv(s32((sh.entity.weapon_damage(attack) << 12) * ((sh.rng.rand16() & 0x1F) + 0x55)), 100)
                end
            end
            local facing = angle_signed(p.rot_y - c.rot_y)
            local s, co
            if 1024 < abs(facing) then
                if to_target >= 1188 then
                    s = sh.math.sin(c.rot_y)
                    c.larval_push_x = (to_target - 1146 <= 81) and q12_mul32(to_target - 1146, s) or q12_mul32(81, s)
                    co = sh.math.cos(c.rot_y)
                    c.larval_push_z = (to_target - 1146 <= 81) and q12_mul32(to_target - 1146, co) or q12_mul32(81, co)
                end
                if to_target < 1105 then
                    local d = (to_target - 1146 < -81) and -81 or (to_target - 1146)
                    c.larval_push_x = q12_mul32(d, sh.math.sin(c.rot_y))
                    c.larval_push_z = q12_mul32(d, sh.math.cos(c.rot_y))
                end
            else
                if to_target > 737 then
                    s = sh.math.sin(c.rot_y)
                    c.larval_push_x = (to_target - 696 <= 81) and q12_mul32(to_target - 696, s) or q12_mul32(81, s)
                    co = sh.math.cos(c.rot_y)
                    c.larval_push_z = (to_target - 696 <= 81) and q12_mul32(to_target - 696, co) or q12_mul32(81, co)
                end
                if to_target < 655 then
                    local d = (to_target - 696 < -81) and -81 or (to_target - 696)
                    c.larval_push_x = q12_mul32(d, sh.math.sin(c.rot_y))
                    c.larval_push_z = q12_mul32(d, sh.math.cos(c.rot_y))
                end
            end
            if p.attack_received == -1 then
                set_group_flags(3, group_flags(3) & ~1)
                c.anim_status = st(A.stun_back_to_idle, false)
                c.control_state = STUN
                c.larval_flags = c.larval_flags & ~GRABBING
                sh.entity.set_collision_state(c.slot, 3)
                c.larval_recoil_keyframe = 0
                stumble(c)
            else
                sh.entity.set_collision_state(c.slot, 1)
            end
        else
            if to_target < (c.cylinder_radius + 81) + p.cylinder_radius then
                speed_toward(c, 6144, 0)
            end
            local y = c.pos_y + c.box_extent_x
            if sh.entity.attack(c.slot, 1, GRAB_ATTACK, c.pos_x, y, c.pos_z, 0, c.rot_y, 1024) ~= -1 then
                c.larval_timer = 0
                c.larval_flags = c.larval_flags | GRABBING
            elseif to_target > 2048 then
                c.anim_status = st(A.walk_forward, false)
                c.control_state = CREEP
            end
        end
    elseif state == CIRCLE then
        speed_toward(c, 6144, 4096)
        if c.larval_timer == 0 then
            c.larval_turn = sh.entity.heading_toward(c.slot, 8192, c.larval_target_x, c.larval_target_z, 4096, false)
            if c.larval_turn == 4096 then
                c.larval_turn = rng_uint(0, 4095)
            end
        end
        c.larval_timer = c.larval_timer + dt
        if c.larval_timer > 3276 then
            c.larval_timer = 0
        end
        turn(c, angle_signed(c.larval_turn - c.rot_y), 33, 682)
        if c.move_speed >= 3688 and rng_uint(0, 255) == 0 then
            start_trip(c)
        else
            if 19660 - trip * 4915 < to_target and rng_int(0, 31) == 0 then
                c.control_state = WATCH
                c.anim_status = st(A.idle, false)
                c.larval_timer = 0
                c.larval_turn = 0
                c.larval_flags = c.larval_flags | SAW_PLAYER
            end
        end
    elseif state == WATCH then
        if sh.entity.player_blocked(c.slot) ~= 0 or sight_step * 2 < to_target then
            c.control_state = LOOK_AROUND
            if c.anim_status >> 1 ~= A.idle then
                c.anim_status = st(A.idle, false)
            end
        else
            if 17 <= abs(delta) and c.anim_status >> 1 ~= A.idle then
                speed_toward(c, 1536, 0)
                if c.anim_status >> 1 ~= st(A.stun_back_to_idle, true) then
                    c.anim_status = st(A.walk_forward, false)
                end
                if delta > 0 then
                    c.rot_y = c.rot_y + q12_mul(dt, 341)
                else
                    c.rot_y = c.rot_y - q12_mul(dt, 341)
                end
            else
                speed_toward(c, 6144, 0)
                if c.anim_status >> 1 ~= A.idle then
                    c.anim_status = st(A.idle, false)
                end
            end
            c.larval_timer = c.larval_timer + dt
            if group_flags(3) & 1 == 0 and 20480 - (trip << 12) < c.larval_timer then
                c.control_state = CREEP
                c.anim_status = st(A.walk_forward, false)
                c.larval_timer = 0
                c.larval_flags = c.larval_flags & ~SAW_PLAYER
                if trip < 2 then
                    c.larval_trip_count = trip + 1
                end
            else
                if to_target < (trip << 12) + 8192 and p.move_speed > trip * 6144 + 2048 then
                    start_stagger(c)
                end
            end
        end
    elseif state == STAGGER then
        c.larval_fade_timer = c.larval_fade_timer + q12_mul(dt, sh.rng.rand16() & 0xFFF)
        if c.larval_fade_timer > 14336 then
            c.larval_fade_timer = 14336
        end
        speed_toward(c, 6144, 4096)
        if c.larval_timer == 0 then
            c.larval_turn = sh.entity.heading_toward(c.slot, 6144, c.larval_target_x, c.larval_target_z, 4096, false)
            if c.larval_turn == 4096 then
                c.larval_turn = sh.rng.rand16() & 0xFFF
            end
        end
        c.larval_timer = c.larval_timer + dt
        if c.larval_timer > 3276 then
            c.larval_timer = 0
        end
        turn(c, angle_signed(c.larval_turn - c.rot_y), 17, 682)
        if c.move_speed >= 3688 and rng_int(0, 63) == 0 then
            start_trip(c)
        end
        if c.larval_flags & HURT == 0 then
            c.larval_wander_timer = c.larval_wander_timer + dt
            if c.larval_wander_timer > 20480 and rng_int(0, 63) == 0 then
                c.larval_wander_timer = 0
                c.larval_timer = 0
                c.larval_turn = 0
                c.control_state = CIRCLE
            end
        end
    elseif state == TRIP then
        c.larval_fade_timer = c.larval_fade_timer + q12_mul(dt, (sh.rng.rand16() & 0x7FF) + 2048)
        if c.larval_fade_timer > 14336 then
            c.larval_fade_timer = 14336
        end
        speed_toward(c, 2048, 0)
        local keyframe = asr(c.anim_time, 12)
        if in_range(keyframe, 86, 92) then
            clip_push(c, 614, 24576, false)
        end
        if in_range(keyframe, 95, 105) then
            c.move_speed = 0
        end
        if c.anim_status >> 1 == A.idle then
            c.anim_status = st(A.walk_forward_alt, false)
            c.control_state = STAGGER
        end
    elseif state == STUN then
        speed_toward(c, 6144, 0)
        local keyframe = asr(c.anim_time, 12)
        if in_range(keyframe, 86, 92) then
            clip_push(c, 614, 24576, false)
        end
        if in_range(keyframe, 41, 49) then
            clip_push(c, 1228, 36864, true)
        end
        if in_range(asr(c.anim_time, 12), 95, 96) and c.move_speed ~= 0 then
            c.anim_status = st(A.stun_front_to_idle_alt, true)
        end
        if c.anim_status == st(A.stun_front_to_idle_alt, true) and c.move_speed == 0 and rng_int(0, 7) == 0 then
            c.anim_status = st(A.stun_front_to_idle, true)
        end
        if in_range(asr(c.anim_time, 12), 61, 62) and c.move_speed ~= 0 then
            c.anim_status = st(A.stun_back_to_idle_alt, true)
        end
        if c.anim_status == st(A.stun_back_to_idle_alt, true) and c.move_speed == 0 and rng_int(0, 7) == 0 then
            c.anim_status = st(A.stun_back_to_idle, true)
        end
        if c.anim_status >> 1 == A.idle and rng_int(0, 7) == 0 then
            c.anim_status = st(A.walk_forward_alt, false)
            c.control_state = STAGGER
            c.larval_flags = c.larval_flags & ~HIT
        end
    elseif state == DOWN_FRONT then
        if in_range(asr(c.anim_time, 12), 143, 151) then
            clip_push(c, 1228, 36864, false)
        end
        if c.health == 0 and c.anim_status >> 1 == A.stun_front_end then
            c.control_state = DEATH
            c.anim_status = st(A.death_front_start, false)
            sh.entity.enemy_defeated(c.slot)
        end
        if c.larval_flags & HIT ~= 0 then
            speed_toward(c, 6144, 0)
            if c.move_speed == 0 then
                c.larval_flags = c.larval_flags & ~HIT
            end
        else
            speed_toward(c, 6144, 409)
            if c.larval_timer == 0 then
                c.larval_turn = sh.entity.heading_toward(c.slot, 2048, c.larval_target_x, c.larval_target_z, 4096, false)
            end
            c.larval_timer = c.larval_timer + dt
            if c.larval_timer > 4096 then
                c.larval_timer = 0
            end
            turn(c, angle_signed(c.larval_turn - c.rot_y), 17, 170)
        end
    elseif state == DOWN_BACK then
        if in_range(asr(c.anim_time, 12), 121, 127) then
            clip_push(c, 614, 24576, true)
        end
        c.flags = c.flags | 0x2
        if c.health <= 0 and c.anim_status >> 1 == A.stun_back_end then
            c.flags = c.flags & ~0x2
            c.control_state = DEATH
            c.anim_status = st(A.death_back_start, false)
            sh.entity.enemy_defeated(c.slot)
        end
        speed_toward(c, 6144, 0)
    elseif state == DEATH then
        c.flags = c.flags & ~0x2
        if c.health <= 0 and npc_index(c.slot) ~= sh.entity.sys("target_npc") then
            c.health = -1
            sh.entity.set_collision_state(c.slot, 0)
        end
        if c.move_speed == 0 and c.larval_flags & (DEATH_CLIP | FALLING) == 0 then
            local clip = c.anim_status >> 1
            if clip == A.death_front_end then
                sh.entity.anim_status_set(c.slot, 3, 3)
                c.larval_flags = c.larval_flags | DEATH_CLIP
            elseif clip == A.death_back_cont then
                sh.entity.anim_status_set(c.slot, 4, 3)
                c.larval_flags = c.larval_flags | DEATH_CLIP
            end
        end
        speed_toward(c, 6144, 0)
    end
end

local function physics(c)
    c.fall_speed = c.fall_speed + asr(sh.game.gravity_speed(), 1)
    if c.larval_flags & HIT == 0 then
        c.heading_angle = c.rot_y
    end
    local moved_x, moved_z = 0, 0
    if sh.game.delta_time() ~= 0 then
        local _, ox, _, oz = sh.entity.move(c.slot, c.larval_push_x, c.larval_push_z)
        moved_x, moved_z = ox, oz
    end
    c.larval_push_z = 0
    c.larval_push_x = 0
    if c.fall_speed ~= 0 then
        c.larval_flags = c.larval_flags | FALLING
    else
        c.larval_flags = c.larval_flags & ~FALLING
    end
    if c.move_speed > 0 and moved_x == 0 and moved_z == 0 then
        c.larval_turn = sh.entity.heading_toward(c.slot, 4096, c.larval_target_x, c.larval_target_z, 4096, false)
        c.larval_flags = c.larval_flags | STUCK
    end
    sh.entity.root_transform(c.slot)
end

-- The recoil clips resume from the keyframe the hit interrupted; the alternate walk runs at the
-- creature's speed.
local function animate(c)
    local status = c.anim_status
    local k = c.larval_recoil_keyframe
    if status == st(A.stun_back_to_idle, false) then
        sh.entity.clip_end_set(c.slot, status, k + 41)
    elseif status == st(A.stun_back_to_idle, true) then
        sh.entity.clip_start_set(c.slot, status, k + 41)
    elseif status == st(A.stun_back_start, false) then
        sh.entity.clip_end_set(c.slot, status, k + 121)
    elseif status == st(A.stun_back_start, true) then
        sh.entity.clip_start_set(c.slot, status, k + 121)
    elseif status == st(A.stun_front_to_idle, false) then
        sh.entity.clip_end_set(c.slot, status, k + 86)
    elseif status == st(A.stun_front_to_idle, true) then
        sh.entity.clip_start_set(c.slot, status, k + 86)
    elseif status == st(A.stun_front_start, false) then
        sh.entity.clip_end_set(c.slot, status, k + 143)
    elseif status == st(A.stun_front_start, true) then
        sh.entity.clip_start_set(c.slot, status, k + 143)
    elseif status == st(A.walk_forward_alt, true) then
        sh.entity.clip_duration_set(c.slot, status, asr(c.move_speed * 131072, 12))
    end
    sh.entity.anim_step(c.slot)
end

local function b(v)
    return v and 1 or 0
end

-- Index of the back recoil run for a recoil keyframe.
local function back_recoil_index(rel)
    if rel < 15 then
        return rel - b(rel >= 11)
    elseif rel - 24 >= 0 then
        return rel - 10
    end
    return 14
end

local function collision(c)
    local status = c.anim_status
    local keyframe = asr(c.anim_time, 12)
    local k = c.larval_recoil_keyframe
    if status == st(A.stun_back_end, false) then
        if k ~= -1 then
            if k == -2 then
                collision_between(c, K[K_BACK_START_END], K[K_BACK_END])
            else
                collision_between(c, K[K_BACK_RECOIL + back_recoil_index(k)], K[K_BACK_END])
            end
        else
            collision_from_keyframe(c, K[K_GRAB + 1])
        end
    elseif status == st(A.death_back_start, true) or status == st(A.stun_back_recoil, true) then
        local k0 = (keyframe > 24 and keyframe < 30) and 0 or 1
        local k1 = (keyframe > 23 and keyframe < 29) and 0 or 1
        collision_between(c, K[K_GRAB + k0], K[K_GRAB + k1])
    elseif status == st(A.stun_back_end, true) or status == st(A.death_back_start, false)
        or status == st(A.death_back_cont, false) or status == st(A.death_back_cont, true)
        or status == st(A.stun_back_recoil, false) then
        collision_from_keyframe(c, K[K_GRAB + 1])
    elseif status == st(A.death_front_start, false) then
        collision_between(c, K[K_FRONT_END], K[K_FRONT_DEAD])
    elseif status == st(A.death_front_start, true) or status == st(A.death_front_end, false)
        or status == st(A.death_front_end, true) then
        collision_from_keyframe(c, K[K_FRONT_DEAD])
    elseif status == st(A.stun_back_to_idle, false) then
        if k == 0 then
            collision_from_keyframe(c, K[K_BACK_RECOIL])
        else
            local k0 = (k - 24 >= 0) and k - 10 or 14
            collision_between(c, K[K_BACK_RECOIL + k0], K[K_BACK_RECOIL + k])
        end
    elseif status == st(A.stun_back_to_idle, true) or status == st(A.stun_back_to_idle_alt, true) then
        local rel = keyframe - 41
        local k0, k1
        if rel < 15 then
            k0 = rel - b(rel >= 11)
            k1 = (keyframe - 40) - b(rel >= 10)
        else
            k0 = (keyframe - 65 >= 0) and keyframe - 51 or 14
            k1 = (keyframe - 64 >= 0) and keyframe - 50 or 14
        end
        collision_between(c, K[K_BACK_RECOIL + k0], K[K_BACK_RECOIL + k1])
    elseif status == st(A.stun_front_to_idle, false) then
        if k == 0 then
            collision_from_keyframe(c, K[K_FRONT_RECOIL])
        else
            local k0 = ((k - 7) - b(k >= 23)) - b(k >= 24)
            collision_between(c, K[K_FRONT_RECOIL + k0], K[K_FRONT_RECOIL + k])
        end
    elseif status == st(A.stun_front_to_idle, true) or status == st(A.stun_front_to_idle_alt, true) then
        local rel = keyframe - 86
        local k0, k1
        if rel < 15 then
            k0 = min(8, rel)
            k1 = min(8, keyframe - 85)
        else
            k0 = (keyframe - 93) - b(rel >= 23) - b(rel >= 24)
            k1 = (keyframe - 92) - b(rel >= 22) - b(rel >= 23) - b(rel >= 33)
        end
        collision_between(c, K[K_FRONT_RECOIL + k0], K[K_FRONT_RECOIL + k1])
    elseif status == st(A.stun_back_start, false) then
        if k == 0 then
            collision_from_keyframe(c, K[K_BACK_START])
        else
            collision_between(c, K[K_BACK_RECOIL + back_recoil_index(k)], K[K_BACK_START + k])
        end
    elseif status == st(A.stun_back_start, true) then
        c.larval_recoil_keyframe = -2
        local rel = keyframe - 121
        local k0 = rel - b(rel > 10)
        if not (k0 < 16) then
            k0 = 15
        end
        local k1 = rel + b(rel < 10)
        if not (k1 < 16) then
            k1 = 15
        end
        collision_between(c, K[K_BACK_START + k0], K[K_BACK_START + k1])
    elseif status == st(A.stun_front_start, false) then
        if k == 0 then
            collision_from_keyframe(c, K[K_FRONT_START])
        else
            local k0
            if k < 15 then
                k0 = min(8, k)
            else
                k0 = ((k - 7) - b(k >= 23)) - b(k >= 24)
            end
            collision_between(c, K[K_FRONT_RECOIL + k0], K[K_FRONT_START + k])
        end
    elseif status == st(A.stun_front_start, true) then
        c.larval_recoil_keyframe = -2
        local rel = keyframe - 143
        collision_between(c, K[K_FRONT_START + min(7, rel)], K[K_FRONT_START + min(7, keyframe - 142)])
    elseif status == st(A.stun_front_end, false) then
        if k == -1 then
            collision_from_keyframe(c, K[K_FRONT_END])
        elseif k == -2 then
            collision_between(c, K[K_FRONT_START_END], K[K_FRONT_END])
        else
            local k0 = k < 15 and min(8, k) or k - 7
            collision_between(c, K[K_FRONT_RECOIL + k0], K[K_FRONT_END])
        end
    elseif status == st(A.stun_front_end, true) or status == st(A.stun_front_cont, false)
        or status == st(A.stun_front_cont, true) then
        collision_from_keyframe(c, K[K_FRONT_END])
    elseif status == st(A.idle, false) or status == st(A.idle, true) then
        collision_from_keyframe(c, K[K_IDLE])
    elseif status == st(A.walk_forward, false) or status == st(A.walk_forward, true)
        or status == st(A.walk_forward_alt, false) or status == st(A.walk_forward_alt, true) then
        collision_from_keyframe(c, K[K_WALK])
    elseif status == st(A.grab_attack, false) or status == st(A.grab_attack, true) then
        c.box_top = -2949
        c.box_height = -819
        c.box_offset_y = -2703
        c.cylinder_radius = 491
        c.box_offset_z = 81
        c.cylinder_extent = 450
        c.box_bottom = 0
        c.cylinder_offset_x = 0
        c.cylinder_offset_z = 0
        c.box_offset_x = 0
        c.box_extent_x = -2416
    end
    sh.entity.shape_offsets_update(c.slot)
end

function update(slot)
    local c = record(slot)
    if c.control_state == 0 then
        start(c)
    end
    react_to_damage(c)
    control(c)
    damage_clear(c)
    physics(c)
    animate(c)
    collision(c)

    -- It fades out once it has stumbled long enough, then leaves for good.
    if c.larval_fade_timer < 14336 then
        return
    end
    c.aux_timer = c.aux_timer + asr(sh.game.delta_time() * 1024, 12)
    if c.aux_timer <= 4096 then
        return
    end
    c.aux_timer = 4096
    c.control_state = GONE
    if sh.save.map_idx() == 37 or rng_int(0, 3) == 0 then
        sh.entity.enemy_defeated(slot)
    end
end
