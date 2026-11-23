-- uses: flight
-- Air Screamer and Night Flutter (one behaviour).

-- Flight tables, one flat list in the order the game keeps them (0-based): pairs of (acceleration,
-- top speed) per motion, then from index 78 one word of flags per animation keyframe.
FLIGHT_TABLE = { [0] = 4096,
    16384, -3276, 16384, 0, 16384, 8192, 16384, 4096, 8192, 8192, 20480, -6144,
    20480, 0, 20480, 24576, 16384, 16384, 20480, 8192, 24576, 81920, 40140, 40960,
    819, 40960, 819, 16384, 24576, 0, 16384, -10240, 16384, -10240, 16384, 16384,
    24576, 16384, 12288, 1228, 2048, 16384, 28672, 0, 22528, -14336, 22528, -8192,
    16384, 16384, 28672, 16384, 12288, 1228, 2048, 796, 4778, 1024, 6144, 910,
    5461, 682, 4096, 0, 4096, 1137, 6826, 1365, 8192, 1251, 7509, 841,
    4778, 932, 5461, 1024, 6144, 28, 28, 28, 28, 28, 28, 28,
    28, 28, 28, 28, 28, 18, 18, 18, 18, 18, 18, 18,
    18, 146, 146, 146, 146, 146, 146, 66, 66, 66, 66, 194,
    194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194,
    194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194,
    194, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44,
    44, 44, 44, 44, 44, 44, 44, 44, 44, 33, 33, 33,
    33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33,
    33, 33, 33, 33, 33, 33, 33, 33, 33, 44, 44, 44,
    44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44,
    34, 34, 34, 162, 162, 162, 162, 162, 162, 162, 162, 162,
    162, 162, 162, 98, 98, 98, 98, 98, 98, 98, 98, 98,
    98, 98, 98, 98, 98, 98, 98, 226, 226, 226, 226, 226,
    226, 226, 226, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 146, 34, 34, 34,
    162, 162, 162, 162, 162, 162, 162, 162, 162, 162, 162, 162,
    162, 33, 33, 98, 98, 98, 98, 98, 98, 98, 226, 226,
    226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226,
    226, 226, 226, 226, 226, 33, 33, 33, 33, 33, 33, 33,
    33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33,
    161, 161, 161, 33, 33, 33, 161, 161, 44, 44, 44, 44,
    44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44,
    44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44,
    44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44,
    44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 44,
    44, 44, 44, 44, 44, 44, 44, 44, 82, 82, 82, 82,
    82, 210, 210, 210, 210, 18, 18, 18, 82, 82, 82, 82,
    82, 82, 82, 82, 82, 82, 82, 82, 82, 82, 82, 82,
    82, 82, 16, 16, 16, 16, 24, 24, 28, 66, 66, 66,
    66, 66, 66, 66, 194, 194, 194, 194, 194, 194, 18, 18,
    18, 18, 18, 146, 146, 146, 146, 82, 82, 82, 2, 2,
    2, 2, 2, 2, 2, 130, 130, 130, 130, 130, 34, 34,
    34, 34, 34, 34, 33, 146, 18, 18, 18, 146, 18, 18,
    146, 18, 18, 18, 146, 18, 18, 18, 146, 18, 146, 18,
    146, 18, 146, 18, 18, 28, 28, 28, 28, 28, 28, 28,
    28, 146, 146, 146, 146, 146, 146, 18, 18, 18, 18, 146,
    146, 146, 146, 146, 146, 146, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 28, 28, 28,
    20, 20, 20, 20, 16, 16, 16, 16, 16, 16, 16, 210,
    210, 210, 82, 82, 82, 82, 210, 210, 210, 82, 82, 82,
    82, 82, 210, 210, 210, 82, 82, 82, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8,
}
D = {}
-- The sound of each of the eleven sound bits (id, volume Q8).
D.sfx = { [0] = { id = 1313, volume = 128 }, { id = 1316, volume = 128 }, { id = 1376, volume = 128 }, { id = 1377, volume = 196 }, { id = 1378, volume = 128 }, { id = 1379, volume = 128 }, { id = 1380, volume = 196 }, { id = 1381, volume = 128 }, { id = 1382, volume = 128 }, { id = 1383, volume = 128 }, { id = 1316, volume = 196 } }
-- Sight range and half angle per flight mode.
D.sight = { [0] = { [0] = 32768, 853 }, { [0] = 40960, 682 }, { [0] = 8192, 1024 }, { [0] = 61440, 1024 } }
-- Reach (radius, forward offset) of the bite and of the swoop, per attack kind.
D.reach = { [0] = { [0] = { [0] = 12288, 0 }, { [0] = 12288, 4096 }, { [0] = 8192, 6144 } }, { [0] = { [0] = 4096, 3072 }, { [0] = 6144, 4096 }, { [0] = 8192, 5120 } } }
D.reach_index = { [0] = { [0] = 1, 2, 0, 0, 2, 2 }, { [0] = 1, 2, 2, 1, 2, 0 } }
-- Body cylinder per flight mode (bottom, radius); -1 is never reached in play.
D.body = { [-1] = { [0] = -21944, -32756 }, [0] = { [0] = 0, 0 }, { [0] = 0, 1966 }, { [0] = -327, 2293 }, { [0] = -1228, 2621 }, { [0] = 0, 1433 } }
D.hull = { [-1] = { [0] = 0, 1433 }, [0] = { [0] = 0, 0 }, { [0] = 0, 2457 }, { [0] = -327, 2662 }, { [0] = -1228, 2867 }, { [0] = 0, 1433 } }
-- Points on the bones (x, y, z, bone; bone -1 ends a list): 1 the fangs, 2 and 3 the claws, 4 the body hull.
D.anchors = { [0] = { { 0, 0, -51, 10 }, { 0, 0, 0, -1 } }, { { 0, 10, -25, 11 }, { 0, 30, -76, 11 }, { 0, 0, 0, -1 } }, { { 0, 0, 0, 8 }, { 0, 0, 64, 8 }, { 0, 0, 0, -1 } }, { { 0, 0, 0, 5 }, { 0, 0, -64, 5 }, { 0, 0, 0, -1 } }, { { 0, 0, 0, 1 }, { 0, 0, 0, 3 }, { 0, 0, 0, 6 }, { 0, 0, 0, 4 }, { 0, 0, 0, 7 }, { 0, 0, 0, 9 }, { 0, 0, 0, 12 }, { 0, 0, 0, 14 }, { 0, 0, 0, 16 }, { 0, 0, 0, -1 } } }
-- Turn clip offsets by quadrant and the two random picks.
D.turn_clip = { [0] = { [0] = { [0] = 9, 9, 9, 10 }, { [0] = 8, 8, 8, 9 }, { [0] = 8, 8, 8, 9 }, { [0] = 8, 8, 8, 9 } }, { [0] = { [0] = 9, 9, 10, 10 }, { [0] = 9, 9, 10, 10 }, { [0] = 8, 8, 9, 9 }, { [0] = 8, 8, 9, 9 } }, { [0] = { [0] = 9, 10, 10, 10 }, { [0] = 9, 10, 10, 10 }, { [0] = 9, 10, 10, 10 }, { [0] = 8, 9, 9, 9 } } }

-- the per-tick update, sound and keyframe flags, init, damage,
-- sight and reach tests, the control dispatch, flight physics, bones, attack points and the hull.

local E = sh.entity
local STAND_BITE = E.attack_number("AirScreamerStandBite")
local HOVER_BITE = E.attack_number("AirScreamerHoverBite")
local SCRATCH = E.attack_number("AirScreamerScratch")

-- The sound of each of the eleven sound bits, by the character's sound slot (the table's ids are the
-- fallbacks).
local function bit_sound(c, i)
    if i == 0 then return sound(c, "bite_hit", 1313)
    elseif i == 1 then return sound(c, "scratch_hit", 1316)
    elseif i == 2 then return sound(c, "flight_loop", 1376)
    elseif i == 3 then return sound(c, "cry", 1377)
    elseif i == 4 then return sound(c, "damage", 1378)
    elseif i == 5 then return sound(c, "attack", 1379)
    elseif i == 6 then return sound(c, "death_cry", 1380)
    elseif i == 7 then return sound(c, "wing_flap", 1381)
    elseif i == 8 then return sound(c, "wing_flap_2", 1382)
    elseif i == 9 then return sound(c, "glide", 1383)
    end
    return sound(c, "wing_beat", 1316)
end

local function bit_assign(v, bit, cond)
    if cond ~= 0 then
        return v | (1 << bit)
    end
    return v & ~(1 << bit)
end

local function mask_assign(v, mask, cond)
    if cond ~= 0 then
        return v | mask
    end
    return v & ~mask
end

local function bit_get(v, bit)
    return ((v & 0xFFFFFFFF) >> bit) & 1
end

local function u32(v)
    return v & 0xFFFFFFFF
end

-- Three consecutive words of S.dyn as a vector and back.
local function dyn_vec(i)
    return { x = S.dyn[i], y = S.dyn[i + 1], z = S.dyn[i + 2] }
end

local function dyn_store(i, v)
    S.dyn[i], S.dyn[i + 1], S.dyn[i + 2] = s32(v.x), s32(v.y), s32(v.z)
end

-- World points of `n` anchors of a list (from 0-based `first`) on one bone into S.points from `out`.
-- The engine call takes at most two points; each point is transformed on its own.
local function anchor_points(c, list, first, n, bone, out)
    local k = 0
    while k < n do
        local a = list[first + k + 1]
        if n - k >= 2 then
            local b = list[first + k + 2]
            local x0, y0, z0, x1, y1, z1 = E.bone_points(c.slot, bone, 2, a[1], a[2], a[3], b[1], b[2], b[3])
            local p0, p1 = S.points[out + k], S.points[out + k + 1]
            p0.x, p0.y, p0.z = x0, y0, z0
            p1.x, p1.y, p1.z = x1, y1, z1
            k = k + 2
        else
            local x0, y0, z0 = E.bone_points(c.slot, bone, 1, a[1], a[2], a[3])
            local p0 = S.points[out + k]
            p0.x, p0.y, p0.z = x0, y0, z0
            k = k + 1
        end
    end
end

function update(slot)
    MAP0_S01 = (sh.map_name() == "map0_s01")
    local c = record(slot)
    module_reset()
    ground_probe(c)

    init_once(c)

    flag_sounds_play(c)
    keyframe_flags_update(c)
    control_update(c)
    flight_update(c)
    animation_update(c)
    attack_points_update(c)
    hull_update(c)
end

function module_reset()
    S.flags = 0
    return true
end

function ground_probe(c)
    local count = ground_kind(c.pos_x, c.pos_z)
    if count == 7 then
        return
    end

    if count < 8 then
        if count ~= 0 then
            c.air_screamer_ground = ground_height(c.pos_x, c.pos_z)
        end
    elseif count ~= 12 then
        c.air_screamer_ground = ground_height(c.pos_x, c.pos_z)
    end
end

function flag_sounds_play(c)
    -- Works on a copy: the flags are only read to pick the sounds.
    local flags = u32(c.air_screamer_flags)

    if flags & (1 << 16) == 0 then
        if flags & ((1 << 3) | (1 << 6)) ~= 0 then
            flags = flags & ~((1 << 3) | (1 << 6))
            if c.health <= 0 then
                flags = flags | (1 << 6)
            else
                flags = flags | (1 << 3)
            end
        end

        if flags & (1 << 28) ~= 0 then
            flags = flags & ~((1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6))
        elseif flags & (1 << 6) ~= 0 then
            flags = flags & ~((1 << 2) | (1 << 3) | (1 << 4) | (1 << 5))
            sfx_stop(bit_sound(c, 2))
        end

        for i = 0, 10 do
            if flags & (1 << i) ~= 0 then
                sfx_3d(c, bit_sound(c, i), D.sfx[i].volume, 0)
            end
        end
    end

    return true
end

function crash_sound_play(c)
    sfx_3d(c, sound(c, "crash", 1590), 128, 0)
end

function keyframe_flags_update(c)
    local near_ground = 0

    local pos_x = c.pos_x
    local flags = c.air_screamer_flags
    local pos_z = c.pos_z

    local kf_flags = FLIGHT_TABLE[78 + c.anim_keyframe]

    local bit1 = bit_get(kf_flags, 1)
    local bit2 = bit_get(kf_flags, 2)
    local bit3 = bit_get(kf_flags, 3)
    local bit4 = bit_get(kf_flags, 4)
    local bit5 = bit_get(kf_flags, 5)
    local bit6 = bit_get(kf_flags, 6)
    local bit7 = bit_get(kf_flags, 7)
    local bit0 = bit_get(kf_flags, 0)

    local kind = ground_kind(pos_x, pos_z)

    local cond2 = kind == 7
    if c.pos_y >= s32(ground_height(pos_x, pos_z) - 614) then
        near_ground = cond2 and 0 or 1
    end

    local cond0 = false
    if kind == 12 or kind == 0 then
        cond0 = true
    end

    local bit27 = bit_get(flags, 27)

    local cond1 = false
    if flags & (1 << 13) ~= 0 or cond0 then
        cond1 = true
    end

    local bit21 = bit_get(flags, 21)
    local bit22 = bit_get(flags, 22)
    local bit14 = bit_get(flags, 14)
    local bit15 = bit_get(flags, 15)

    local next_bit28 = (flags & ((1 << 6) | (1 << 28))) ~= 0 and 1 or 0
    local next_bit19 = 0

    local kf_low_bits_clear = (bit0 == 0 and 1 or 0) & (bit1 == 0 and 1 or 0)

    if bit6 == 0 then
        next_bit19 = bit1 & 1
    end

    local next_bit21 = 0

    local next_bit20 = bit1 & bit6

    if bit2 == 0 then
        next_bit21 = kf_low_bits_clear & 1
    end

    local next_bit22 = 0

    if bit2 ~= 0 then
        next_bit22 = kf_low_bits_clear & 1
    end

    local next_bit9 = 0

    if c.pos_y < s32(ground_height(c.pos_x, c.pos_z) - 409) then
        local k = ground_kind(c.pos_x, c.pos_z)
        if not (k == 7 or k == 0 or k == 12) then
            local bit21_source
            if bit21 == 0 then
                bit21_source = next_bit21 & 1
            else
                bit21_source = 0
            end

            next_bit9 = kf_low_bits_clear & bit21_source

            if bit22 == 0 then
                next_bit9 = kf_low_bits_clear & (bit21_source | next_bit22)
            end
        end
    end

    flags = bit_assign(flags, 13, bit0 & near_ground)
    flags = bit_assign(flags, 17, bit0)
    flags = bit_assign(flags, 18, bit1 == 0 and 1 or 0)
    flags = bit_assign(flags, 19, next_bit19)
    flags = bit_assign(flags, 20, next_bit20)
    flags = bit_assign(flags, 21, next_bit21)
    flags = bit_assign(flags, 22, next_bit22)
    flags = bit_assign(flags, 23, bit3 == 0 and (kf_low_bits_clear & 1) or 0)
    flags = bit_assign(flags, 24, bit3 == 1 and (kf_low_bits_clear & 1) or 0)
    flags = bit_assign(flags, 25, bit4)
    flags = bit_assign(flags, 26, bit5)
    flags = bit_assign(flags, 27, bit7)
    flags = bit_assign(flags, 15, bit14) & ~(1 << 14)
    local flags_bit28 = bit_assign(flags, 28, next_bit28)

    local next_flags
    if bit27 == 0 then
        next_flags = bit7 & next_bit19
    else
        next_flags = 0
    end

    next_flags = mask_assign(flags_bit28, 1 << 7, next_flags)

    flags = mask_assign(next_flags, 1 << 8, bit27 == 0 and ((bit7 & 1) & next_bit20) or 0)
    next_flags = bit_assign(flags, 9, next_bit9)

    local next_bit10
    if not cond0 and not cond1 then
        next_bit10 = bit0 & near_ground
    else
        next_bit10 = 0
    end

    flags = mask_assign(next_flags, 1 << 10, next_bit10) & ~(1 << 4)
    flags = flags & ~(1 << 5)
    flags = flags & ~(1 << 6)
    flags = bit_assign(flags, 3, bit15 == 0 and (bit14 & 1) or 0) & ~(1 << 2)
    flags = flags & ~(1 << 0)
    flags = flags & ~(1 << 1)

    c.air_screamer_flags = s32(flags)
    return true
end

function init_once(c)
    if c.control_state ~= 0 then
        return false
    end

    if c.chara_id == 3 then
        c.health = s32(chara_param_q12(c.chara_id, "health_base", 1556480) +
                       rand_q12() * chara_param_int(3, "health_rand", 300))
    else
        c.health = s32(chara_param_q12(c.chara_id, "health_base", 1556480) +
                       rand_q12() * chara_param_int(2, "health_rand", 100))
    end

    local difficulty = sh.save.game_difficulty()
    if difficulty == -1 then
        c.health = s32(c.health + rand_q12() * -100)
    elseif difficulty == 1 then
        c.health = s32(c.health + rand_q12() * 100)
    end

    clip_table_attach(c)
    control_spawn_setup(c)
    keyframe_flags_update(c)
    keyframe_flags_update(c)
    tilt_reset(c)
    damage_reset(c)
    motion_reset(c)
    pose_settle(c)
    ground_warp(c)
    push_reset(c)

    return true
end

function tilt_reset(c)
    c.rot_x = 0
    c.rot_z = 0
    c.angle_to_target = 0
end

function damage_reset(c)
    c.damage_x = 0
    c.damage_y = 0
    c.damage_z = 0
    c.damage_amount = 0
    c.air_screamer_spare = 0
end

function motion_reset(c)
    c.move_speed = 0
    c.fall_speed = 0
    c.rot_speed_x = 0
    c.rot_speed_y = 0
    c.rot_speed_z = 0
    c.angle_to_target_speed = 0
    c.heading_angle = c.rot_y
end

function pose_settle(c)
    anim_play(c, true)
    root_bone_set(c)
    hull_from_bones(c)
end

function ground_warp(c)
    c.pos_y = ground_height(c.pos_x, c.pos_z)
end

function push_reset(c)
    c.air_screamer_push = 0
    c.air_screamer_push_pitch = 0
    c.air_screamer_push_heading = 0
end

function clip_table_attach(c)
    E.clip_table_attach(c.slot)
end

function damage_take(c, mult)
    local damage0 = c.damage_amount
    local anim_status = c.anim_status
    local attack = c.attack_received
    local damage_type = 0

    if role(c) == 3 then
        -- Steel pipe / hammer tap hit harder, rifle / shotgun softer.
        if attack == 1 or attack == 4 then
            damage0 = s32(damage0 + idiv(damage0, 2))
        elseif attack == 33 or attack == 34 then
            damage0 = idiv(damage0, 2)
        end
    end

    if c.health > 0 then
        if anim_status == 43 then -- HoverInjured, active
            local damage1 = q12_mul(dt(), 40960)
            if damage1 < c.health then
                c.health = s32(c.health - damage1)
            else
                c.health = 0
            end
        end

        c.damage_amount = 0

        damage0 = q12_mul(damage0, mult)
        if damage0 < c.health then
            c.health = s32(c.health - damage0)
        else
            c.health = 0
        end
    end

    if damage0 > 0 or c.health <= 0 then
        -- An attack id outside the weapon table (none recorded) counts as kind 0 instead of failing.
        local ok, _, kind = pcall(E.weapon_attack_stats, attack)
        local hit_kind = (ok and kind or 0) & 0xFF
        local angle = angle_norm(P.rot_y - c.rot_y)

        local finished
        if MAP0_S01 then
            finished = c.health <= 0
        else
            finished = c.health <= 491520 -- 120.0
        end

        if finished then
            if damage0 > 20480 then
                damage_type = 4
            else
                damage_type = 3
            end
        elseif role(c) ~= 3 and hit_kind == 2 and signed_range(angle, 1365) then
            damage_type = 2
        elseif role(c) == 3 then
            if damage0 > 184320 or hit_kind == 1 then
                damage_type = 1
            end
        else
            if damage0 > 81920 or hit_kind == 1 then
                damage_type = 1
            end
        end
    end

    return damage_type
end

-- Returns: visible (boolean), distance, angle to `vec` relative to the facing.
function point_visible(c, vec)
    local light = map_light() & 0xFF
    local idx
    if light & 2 ~= 0 then
        idx = (light & 1 ~= 0) and 2 or 3
    else
        idx = (flight_mode(c) == 3) and 1 or 0
    end

    local dx = s32(vec.x - c.pos_x)
    local dz = s32(vec.z - c.pos_z)

    local dist0 = D.sight[idx][0]
    local angle0 = D.sight[idx][1]

    local angle = angle_norm(sh.math.atan2(dx, dz) - (c.rot_y + c.angle_to_target))
    local dist = E.sqrt_q12(s32(q12_mul(dx, dx) + q12_mul(dz, dz)))

    local angle1 = angle + angle0
    if u32(angle0 * 2) >= u32(angle1) and dist0 >= dist then
        EYE.x = c.pos_x
        EYE.y = s32(c.pos_y + c.box_top)
        EYE.z = c.pos_z
        return not ray_trace(EYE, vec), dist, angle
    end

    return false, dist, angle
end

function in_bite_reach(c, player_pos, player_noise)
    local idx
    if c.air_screamer_flags & ((1 << 19) | (1 << 20)) ~= 0 then
        idx = 2
    else
        if (c.anim_status | 1) == 27 or (c.anim_status | 1) == 29 then
            idx = 0
        else
            idx = 1
        end
    end

    local reach = D.reach[0][idx][0]
    local dist = D.reach[0][idx][1]

    local difficulty = sh.save.game_difficulty()
    if difficulty == 0 then
        reach = reach + 2048
    elseif difficulty == 1 then
        reach = reach + 4096
    end

    local rot_y = c.rot_y
    local off_x = s32(player_pos.x - s32(c.pos_x + q12_mul(dist, sh.math.sin(rot_y))))
    local off_z = s32(player_pos.z - s32(c.pos_z + q12_mul(dist, sh.math.cos(rot_y))))

    -- Compared as unsigned 64-bit, the 32-bit sums sign-extended.
    local rough = s32(q12_mul(off_x, off_x) + q12_mul(off_z, off_z))
    local limit = q12_mul(reach, player_noise)
    if math.ult(q12_mul(limit, limit), rough) then
        return false
    end
    return true
end

function in_swoop_reach(c, pos, scale)
    local idx = D.reach_index[side(c)][attack_set(c)]
    local reach = D.reach[1][idx][0]
    local offset_dist = D.reach[1][idx][1]

    local difficulty = sh.save.game_difficulty()
    if difficulty == 0 then
        reach = reach + 2048
    elseif difficulty == 1 then
        reach = reach + 4096
    end

    if c.control_state == 3 or c.control_state == 9 then
        reach = reach + 4096
    end

    local heading = c.rot_y
    local off_x = s32(pos.x - s32(c.pos_x + q12_mul(offset_dist, sh.math.sin(heading))))
    local off_z = s32(pos.z - s32(c.pos_z + q12_mul(offset_dist, sh.math.cos(heading))))

    -- The limit is unsigned 64-bit, so the compare is too.
    local rough = s32(q12_mul(off_x, off_x) + q12_mul(off_z, off_z))
    local limit = q12_mul(reach, reach)
    limit = q12_mul(limit, scale)
    if math.ult(limit, rough) then
        return false
    end
    return true
end

-- Returns: Harry's head visible (boolean), distance, packed angles to the aim point.
function player_head_sight(c)
    PLAYER_HEAD.x = P.pos_x
    PLAYER_HEAD.y = s32(P.pos_y + P.box_top)
    PLAYER_HEAD.z = P.pos_z

    local cond = point_visible(c, PLAYER_HEAD)
    if cond then
        c.air_screamer_aim_x = PLAYER_HEAD.x
        c.air_screamer_aim_y = PLAYER_HEAD.y
        c.air_screamer_aim_z = PLAYER_HEAD.z
    end

    local aim_x, aim_y, aim_z = c.air_screamer_aim_x, c.air_screamer_aim_y, c.air_screamer_aim_z
    local dist = distance_2d_q12(c.pos_x, c.pos_z, aim_x, aim_z)
    local angle = angles_between(c.pos_x, c.pos_y, c.pos_z, aim_x, aim_y, aim_z)

    return cond, dist, angle
end

-- Returns: Harry within the bite reach (boolean), the reach scale from Harry's noise.
function bite_reach_aim(c)
    local offset_dist = s32(P.player_noise << 8)

    local cond = in_bite_reach(c, { x = P.pos_x, y = P.pos_y, z = P.pos_z }, offset_dist)
    if cond then
        local angle = heading_between(c.pos_x, c.pos_z, P.pos_x, P.pos_z)

        if map_light() & 2 == 0 then
            flight_mode(c)
        end

        local off_x = q12_mul(offset_dist, sh.math.sin(angle))
        local off_z = q12_mul(offset_dist, sh.math.cos(angle))

        c.air_screamer_aim_x = s32(c.pos_x + off_x)
        c.air_screamer_aim_y = s32(P.pos_y + P.box_offset_y)
        c.air_screamer_aim_z = s32(c.pos_z + off_z)
    end

    return cond, offset_dist
end

-- Returns: Harry within the swoop reach (boolean), the reach scale (before the 1.5 floor).
function swoop_reach_aim(c)
    local offset_dist = approach_score(c)
    local dist = offset_dist

    local cond = in_swoop_reach(c, { x = P.pos_x, y = P.pos_y, z = P.pos_z }, offset_dist)
    if cond then
        offset_dist = max(offset_dist, 6144)

        local angle = heading_between(c.pos_x, c.pos_z, P.pos_x, P.pos_z)
        local off_x = q12_mul(offset_dist, sh.math.sin(angle))
        local off_z = q12_mul(offset_dist, sh.math.cos(angle))

        c.air_screamer_aim_x = s32(c.pos_x + off_x)
        c.air_screamer_aim_y = s32(P.pos_y + P.box_offset_y)
        c.air_screamer_aim_z = s32(c.pos_z + off_z)
    end

    return cond, dist
end

-- Returns: sight flags (bit 2 seen, bit 1 swoop reach, bit 0 bite reach), distance, angle, swoop
-- scale, bite scale.
function senses_update(c)
    local cond, out_dist, out_angle = player_head_sight(c)
    if cond then
        SAVED_AIM.x = c.air_screamer_aim_x
        SAVED_AIM.y = c.air_screamer_aim_y
        SAVED_AIM.z = c.air_screamer_aim_z
    end

    local cond0, dist = swoop_reach_aim(c)
    local cond1, lunge = bite_reach_aim(c)

    -- The reach tests move the aim point; a seen Harry keeps his head as the aim.
    if cond then
        c.air_screamer_aim_x = SAVED_AIM.x
        c.air_screamer_aim_y = SAVED_AIM.y
        c.air_screamer_aim_z = SAVED_AIM.z
    end

    local flags = cond and 4 or 0
    if cond0 then
        flags = flags | (1 << 1)
    end

    return flags | (cond1 and 1 or 0), out_dist, out_angle, dist, lunge
end

function approach_score(c)
    local heading_to = angles_between(P.pos_x, P.pos_y, P.pos_z, c.pos_x, c.pos_y, c.pos_z)
    local heading_from = P.rot_y

    local inv_dist = s32(32768 - distance_2d_q12(P.pos_x, P.pos_z, c.pos_x, c.pos_z))
    if inv_dist < 0 then
        inv_dist = 0
    else
        inv_dist = asr(inv_dist, 3)
    end

    local delta = abs(angle_norm(heading_to - heading_from))
    local dist
    if inv_dist >= delta then
        dist = (inv_dist - delta) * 4
    else
        dist = 0
    end

    if player_aiming() then
        local aim = player_aim_distance()
        if aim > 8192 then
            dist = dist + 8192
        else
            dist = s32(dist + aim)
        end

        if (c.slot - 1) == E.sys("target_npc") then
            dist = dist + 4096
        end
    else
        dist = dist + 2048
    end

    return dist
end

function control_update(c)
    -- Dead for good: the map event drives what follows.
    if c.health == -1 then
        return false
    end

    -- map0_s01's fly-by owns the position; only the wing flap runs.
    if flyby_active() then
        local want = 39 -- HoverVariable, active
        if c.anim_status ~= want then
            c.anim_status = want
            c.anim_time = 0
            c.anim_keyframe = 0
        end
        c.anim_flags = c.anim_flags | 1 -- unlocked, or the clip never advances
        animation_update(c)
        return true
    end

    local delta = dt()
    if delta < 0 then
        delta = 0
    end

    local timer = c.air_screamer_timer
    if timer < delta then
        c.air_screamer_timer = 0
    else
        c.air_screamer_timer = s32(timer - delta)
    end

    S.sight, S.player_distance, S.player_angle, S.reach, S.lunge_reach = senses_update(c)

    if role(c) == 3 then
        set_attack_set(c, 0)
    end

    local fn = CONTROL_STATES[c.control_state]
    if fn ~= nil then
        fn(c)
    end
    return true
end

function flight_update(c)
    if flight_mode(c) ~= 0 and S.flags & (1 << 29) == 0 then
        flight_move(c)
        push_move(c)
        slope_floor(c)
    end

    root_bone_set(c)
    return true
end

function slope_floor(c)
    slope_force(c.pos_x, c.pos_z, 0, 0)

    local pos_y = s32(SLOPE[1] - c.box_top)
    if c.pos_y < pos_y then
        c.pos_y = pos_y
    end
end

function flight_move(c)
    speeds_update(c)

    local heading = c.rot_y
    local move_speed = c.move_speed
    local fall_speed = c.fall_speed
    c.heading_angle = heading

    VELOCITY.x = q12_mul(move_speed, sh.math.sin(heading))
    VELOCITY.y = fall_speed
    VELOCITY.z = q12_mul(move_speed, sh.math.cos(heading))

    STEP.x = q12_mul(dt(), VELOCITY.x)
    STEP.y = q12_mul(dt(), VELOCITY.y)
    STEP.z = q12_mul(dt(), VELOCITY.z)

    local step_velocity = dyn_vec(26)
    local result = step_apply(HIT, VELOCITY, c, step_collide(HIT, STEP, c), step_velocity)
    dyn_store(26, step_velocity)

    c.air_screamer_flags = s32(c.air_screamer_flags & ~(1 << 29))

    if flight_mode(c) == 3 then
        local x = S.dyn[26]
        local z = S.dyn[28]

        local mag_sqr = s32(q12_mul(x, x) + q12_mul(z, z))
        if mag_sqr < 2007 then -- 0.49
            c.air_screamer_flags = s32(c.air_screamer_flags | (1 << 29))
        end
    end

    c.fall_speed = S.dyn[27]
    local step_bounce = dyn_vec(32)
    impact_apply(STEP, c, result, step_bounce)
    dyn_store(32, step_bounce)
end

function push_move(c)
    local new_x = q12_mul(c.damage_x, 12288)
    local new_y = q12_mul(c.damage_y, 12288)
    local new_z = q12_mul(c.damage_z, 12288)

    c.damage_x = 0
    c.damage_y = 0
    c.damage_z = 0

    local hit_len = E.sqrt_q12(s32(q12_mul(new_x, new_x) + q12_mul(new_y, new_y) + q12_mul(new_z, new_z)))

    local angle2 = c.air_screamer_push
    local angle0 = c.air_screamer_push_pitch
    local angle1 = c.air_screamer_push_heading

    local pos_x, pos_y, pos_z
    if angle2 == 0 then
        pos_z = 0
        pos_y = 0
        pos_x = 0
    else
        pos_y = q12_mul(angle2, sh.math.sin(angle0))
        angle0 = q12_mul(angle2, sh.math.cos(angle0))

        if angle0 < 0 then
            angle0 = s32(-angle0)
            angle1 = angle1 ~ 0x800
        end

        pos_x = q12_mul(angle0, sh.math.sin(angle1))
        pos_z = q12_mul(angle0, sh.math.cos(angle1))
    end

    if hit_len > 0 then
        pos_x = s32(pos_x + new_x)
        pos_y = s32(pos_y + new_y)
        pos_z = s32(pos_z + new_z)
    end

    if (pos_x | pos_y | pos_z) == 0 then
        c.air_screamer_push = 0
        c.air_screamer_push_pitch = 0
        c.air_screamer_push_heading = c.rot_y
        return
    end

    PUSH_VELOCITY.x = pos_x
    PUSH_VELOCITY.y = pos_y
    PUSH_VELOCITY.z = pos_z

    PUSH_STEP.x = q12_mul(dt(), pos_x)
    PUSH_STEP.y = q12_mul(dt(), pos_y)
    PUSH_STEP.z = q12_mul(dt(), pos_z)

    local temp = step_collide(HIT, PUSH_STEP, c)
    local push_velocity = dyn_vec(29)
    temp = step_apply(HIT, PUSH_VELOCITY, c, temp, push_velocity)
    dyn_store(29, push_velocity)

    local push_bounce = dyn_vec(35)
    impact_apply(PUSH_STEP, c, temp, push_bounce)
    dyn_store(35, push_bounce)

    angle1 = sh.math.atan2(pos_x, pos_z)
    angle0 = s32(q12_mul(pos_x, pos_x) + q12_mul(pos_z, pos_z))
    angle2 = s32(angle0 + q12_mul(pos_y, pos_y))
    angle0 = E.sqrt_q12(angle0)
    angle2 = E.sqrt_q12(angle2)
    angle0 = sh.math.atan2(pos_y, angle0)
    local angle3 = u32(q12_mul(dt(), 24576)) -- 6.0 per second, unsigned

    if u32(angle2) < angle3 then
        angle2 = 0
    else
        angle2 = s32(angle2 - angle3)
    end

    c.air_screamer_push = angle2
    c.air_screamer_push_pitch = angle0
    c.air_screamer_push_heading = angle1
end

-- Moves the position by `offset` (HIT), turns the step back into a velocity in `vec2`, keeps the
-- remainder in `vec1` and lands on the ground; returns the blocked flag (1 also when landing).
function step_apply(offset, vec1, c, blocked, vec2)
    local flags = c.air_screamer_flags

    local off_x = offset.x
    local off_z = offset.z

    local ret = blocked

    c.pos_x = s32(c.pos_x + off_x)
    c.pos_z = s32(c.pos_z + off_z)

    local vec1_x, vec1_z
    if ret == 0 then
        flags = flags & ~(1 << 31)
        vec1_x = 0
        vec2.x = vec1.x
        vec1_z = 0
        vec2.z = vec1.z
    else
        flags = flags | (1 << 31)
        -- No time passed: no velocity from the step (and no division by zero).
        local time = (dt() ~= 0) and idiv(16777216, dt()) or 0

        local vec2_x, vec2_z
        if (off_x | off_z) == 0 then
            vec2_x = 0
            vec2_z = 0
            vec2.x = 0
            vec2.z = 0
        else
            vec2_x = q12_mul(off_x, time)
            vec2.x = vec2_x
            vec2_z = q12_mul(off_z, time)
            vec2.z = vec2_z
        end

        vec1_x = s32(vec2_x - vec1.x)
        vec1_z = s32(vec2_z - vec1.z)
    end

    vec1.x = vec1_x
    vec1.z = vec1_z
    local pos_y = c.pos_y
    local vec1_y = vec1.y
    local vec0_y = offset.y

    local ground = ground_height(c.pos_x, c.pos_z)
    S.dyn[24] = ground

    local new_vec2_y, new_vec1_y
    if E.collision_state(c.slot) == 0 then
        new_vec2_y = 0
        new_vec1_y = 0
    elseif pos_y >= ground and vec0_y >= 0 then
        flags = flags & ~(1 << 30)
        pos_y = ground
        new_vec2_y = 0
        new_vec1_y = 0
    else
        pos_y = s32(pos_y + vec0_y)
        if pos_y >= ground then
            flags = flags | (1 << 30)
            ret = 1
            new_vec2_y = 0
            new_vec1_y = s32(-vec1_y)
            pos_y = ground
        else
            flags = flags & ~(1 << 30)
            new_vec2_y = vec1_y
            new_vec1_y = 0
        end
    end

    c.pos_y = pos_y
    vec2.y = new_vec2_y
    vec1.y = new_vec1_y
    c.air_screamer_flags = s32(flags)
    return ret
end

-- The part of a blocked step beyond 5.0 hurts; beyond 6.0 it bounces back into the hit push.
function impact_apply(step, c, blocked, bounce_out)
    local off_x, off_y, off_z
    if blocked == 0 then
        off_x = 0
        off_y = 0
        off_z = 0
    else
        off_x = step.x
        off_y = step.y
        off_z = step.z

        local sqr = q12_mul(off_x, off_x)
        sqr = s32(sqr + q12_mul(off_y, off_y))
        sqr = s32(sqr + q12_mul(off_z, off_z))
        sqr = E.sqrt_q12(sqr)

        c.damage_amount = s32(c.damage_amount + preserved_sign_subtract(sqr, 20480))
        if sqr ~= 0 then
            sqr = idiv(s32(preserved_sign_subtract(sqr, 24576) * 4096), sqr)
        end

        off_x = q12_mul(off_x, sqr)
        off_y = q12_mul(off_y, sqr)
        off_z = q12_mul(off_z, sqr)
        if (off_x | off_y | off_z) ~= 0 then
            c.damage_x = s32(c.damage_x + q12_mul(off_x, 3276))
            c.damage_y = s32(c.damage_y + q12_mul(off_y, 3276))
            c.damage_z = s32(c.damage_z + q12_mul(off_z, 3276))
        end
    end

    bounce_out.x = off_x
    bounce_out.y = off_y
    bounce_out.z = off_z
end






-- Wall test of a step with the moving body shape; returns 1 when the step was cut.
function step_collide(cut_step, offset, c)
    shape_save(c)
    moving_shape_set(c, c.move_speed, c.heading_angle)
    shape_hook(c)

    local result = wall_detect(c, offset)
    if cut_step ~= HIT then
        cut_step.x, cut_step.y, cut_step.z = HIT.x, HIT.y, HIT.z
    end

    local pos_y = c.pos_y
    local ground = ground_height(s32(c.pos_x + cut_step.x), s32(c.pos_z + cut_step.z))

    if flight_mode(c) ~= 4 then
        if offset.y > 0 and ground < s32(pos_y - 2048) then
            cut_step.x = 0
            cut_step.z = 0
        end
    elseif ground < pos_y then
        cut_step.x = 0
        cut_step.z = 0
    end

    if asr(offset.x, 4) ~= asr(cut_step.x, 4) or asr(offset.z, 4) ~= asr(cut_step.z, 4) then
        result = 1
    end

    shape_restore(c)
    return result
end

function root_bone_set(c)
    E.bone_flag_set(c.slot, 0, 0)
    E.bone_rotation_set(c.slot, 0, c.rot_x, c.rot_y, c.rot_z)

    if role(c) == 3 then
        E.bone_scale_xyz(c.slot, 0, 4608, 4608, 4608) -- 1.125
    elseif c.air_screamer_flags & (1 << 12) ~= 0 then
        E.bone_scale_xyz(c.slot, 0, 3584, 3584, 3584) -- 0.875
    end

    local heading = c.rot_y
    local off_x
    local off_z = 0
    if S.flags < 0 then
        off_x = 0
    else
        off_x = q12_mul(sh.math.sin(heading), -696)
        off_z = q12_mul(sh.math.cos(heading), -696)
    end

    E.bone_translation_set(c.slot, 0, asr(s32(c.pos_x + off_x), 4), asr(c.pos_y, 4), asr(s32(c.pos_z + off_z), 4))
end

function walk_stand_switch(c)
    if c.anim_status == 47 or c.anim_status == 27 or c.anim_status == 29 then -- standing clips, active
        if flight_clip_speed(c) ~= 0 then
            c.anim_status = 50 -- WalkForward
        end
    elseif c.anim_status == 51 then -- WalkForward, active
        if flight_clip_speed(c) == 0 then
            c.anim_status = 46 -- StandIdle
        end
    end
end

function transition_clip_speed(c)
    local cond = c.control_state ~= 1
    local status = c.anim_status
    local ret

    if status == 47 then
        ret = 122880
        if not cond then
            ret = 61440
        end
    elseif status == 38 then
        ret = 98304
        if not cond then
            ret = 122880
        end
    elseif status == 34 then
        ret = 163840
        if not cond then
            ret = 245760
        end
    elseif status == 48 then
        ret = 294912
        if not cond then
            ret = 122880
        end
    elseif status == 49 then
        ret = 163840
        if not cond then
            ret = 122880
        end
    elseif status == 30 then
        ret = 294912
        if not cond then
            ret = 122880
        end
    elseif status == 31 then
        ret = 73728
        if not cond then
            ret = 81920
        end
    else
        ret = 40960
    end

    return ret
end

function flight_clip_speed(c)
    local status = c.anim_status
    local not_scripted = c.control_state ~= 1

    if status == 53 then -- Stun, active
        if c.anim_keyframe == 96 and c.health < 1 then
            return 0
        else
            return 20480
        end
    elseif status == 27 or status == 29 or status == 47 or status == 51 then
        local move = s32(c.move_speed * 40)
        local rot = s32(c.rot_speed_y * 180)

        if (move | rot) == 0 then
            return 0
        end

        -- Magnitudes are unsigned.
        local abs_speed = u32(move < 0 and -move or move)
        local abs_rot = u32(rot < 0 and -rot or rot)

        local speed1
        if abs_rot < abs_speed then
            if abs_speed < 81920 then
                speed1 = 81920
            else
                speed1 = move
            end
        else
            if abs_rot < 81920 then
                speed1 = 81920
            else
                speed1 = rot
            end
        end
        return speed1
    elseif status == 39 then -- HoverVariable, active
        if not_scripted then
            return 98304
        end

        return s32(q12_mul(c.fall_speed, -8192) + 81920)
    elseif status == 35 then -- Glide, active
        if not_scripted then
            return 163840
        end

        local fall = c.fall_speed
        local v = s32(c.move_speed - 16384)

        local ret = 81920
        ret = s32(ret + q12_mul(s32(abs(v)), 6144))
        ret = s32(ret + q12_mul(fall, -6144))
        return ret
    elseif status == 19 or status == 21 or status == 23 or status == 33 then
        local to_ground = s32(ground_height(c.pos_x, c.pos_z) - c.pos_y)
        local speed1 = 81920

        if to_ground > 0 then
            local fall = c.fall_speed
            local anim_time = c.anim_time

            local end_time
            if status == 33 then -- GlideToWalkForward
                end_time = 1363968
            elseif status == 21 then -- HoverToStun
                end_time = 774144
            elseif status == 23 then -- GlideToStun
                end_time = 880640
            else
                end_time = 700416
            end

            local t = idiv(s32((end_time - anim_time) << 12), to_ground)
            speed1 = q12_mul(fall, t)
        end

        if speed1 < 1228 then
            speed1 = 1228
        end
        if speed1 > 122880 then
            speed1 = 122880
        end
        return speed1
    end

    return 40960
end

-- Clip speed asked by the clip table: 0 transitions, 1 hover/stand clips.
function clip_duration(slot, which)
    if which == 0 then
        return transition_clip_speed(record(slot))
    elseif which == 1 then
        return flight_clip_speed(record(slot))
    end
end

function animation_update(c)
    walk_stand_switch(c)
    anim_play(c, false)
    head_turn_bend(c)
    return true
end

-- Spreads the head turn over the neck (bone 10) and body bones 9, 1 and 2.
function head_turn_bend(c)
    if c.anim_flags & 1 ~= 0 then
        local bend = c.angle_to_target
        local slot = c.slot

        E.bone_rotate_xyx(slot, 10, 0, s16(q12_mul(bend, 1228)), 0)

        local angle = q12_mul(bend, 2048)
        local cos = sh.math.cos(angle)
        local sin = sh.math.sin(angle)
        local m00 = s16(cos)
        local blend = s16(q12_mul(sin, 2896))
        local diag = s16(q12_mul(cos + 4096, 2048))
        local comp = s16(q12_mul(4096 - cos, 2048))
        E.bone_multiply(slot, 9, m00, s16(-blend), blend, blend, diag, comp, s16(-blend), comp, diag)

        angle = q12_mul(bend, 819)
        cos = sh.math.cos(angle)
        sin = sh.math.sin(angle)
        m00 = s16(cos)
        blend = s16(q12_mul(sin, 2896))
        diag = s16(q12_mul(cos + 4096, 2048))
        comp = s16(q12_mul(4096 - cos, 2048))
        E.bone_multiply(slot, 1, m00, s16(-blend), blend, blend, diag, comp, s16(-blend), comp, diag)

        angle = q12_mul(bend, 819)
        cos = sh.math.cos(angle)
        sin = sh.math.sin(angle)
        m00 = s16(cos)
        blend = s16(q12_mul(sin, 2896))
        diag = s16(q12_mul(cos + 4096, 2048))
        comp = s16(q12_mul(4096 - cos, 2048))
        E.bone_multiply(slot, 2, m00, blend, s16(-blend), s16(-blend), diag, comp, blend, comp, diag)
    end
end

function attack_points_update(c)
    local status = c.anim_status

    if (P.flags & 8) == 0 and
       event_npc(0) == -1 and event_npc(1) == -1 and
       c.control_state ~= 12 and c.control_state ~= 25 and
       c.control_state ~= 40 and c.control_state ~= 49 then
        c.attack_window = 0
    else
        local pts = S.points

        if status == 5 or status == 3 then -- hover / stand bite, active
            if c.attack_window == 0 then
                if status == 3 then
                    c.attack_performed = STAND_BITE
                else
                    c.attack_performed = HOVER_BITE
                end

                c.attack_window = 1
            end

            local fangs = D.anchors[1]
            anchor_points(c, fangs, 0, 2, fangs[1][4], 0)

            local angles = angles_between(pts[0].x, pts[0].y, pts[0].z, pts[1].x, pts[1].y, pts[1].z)
            local pitch = asr(angles, 16)
            c.attack_x, c.attack_y, c.attack_z = pts[0].x, pts[0].y, pts[0].z
            c.attack_angle_y = angles
            c.attack_angle_x = pitch

            if ranged_attack(c) ~= 0 then
                c.air_screamer_flags = s32(c.air_screamer_flags | (1 << 0))
            end
        elseif status == 7 then -- glide scratch, active
            if c.attack_window == 0 then
                c.attack_performed = SCRATCH
                c.attack_window = 1
            end

            local claw_a = D.anchors[2]
            local claw_b = D.anchors[3]
            anchor_points(c, claw_a, 0, 2, claw_a[1][4], 0)
            anchor_points(c, claw_b, 0, 2, claw_b[1][4], 2)

            pts[0].x = idiv(s32(pts[0].x + pts[2].x), 2)
            pts[0].y = idiv(s32(pts[0].y + pts[2].y), 2)
            pts[0].z = idiv(s32(pts[0].z + pts[2].z), 2)
            pts[1].x = idiv(s32(pts[1].x + pts[3].x), 2)
            pts[1].y = idiv(s32(pts[1].y + pts[3].y), 2)
            pts[1].z = idiv(s32(pts[1].z + pts[3].z), 2)

            local angles = angles_between(pts[0].x, pts[0].y, pts[0].z, pts[1].x, pts[1].y, pts[1].z)
            local pitch = asr(angles, 16)

            c.attack_x, c.attack_y, c.attack_z = pts[0].x, pts[0].y, pts[0].z
            c.attack_angle_y = angles
            c.attack_angle_x = pitch

            if ranged_attack(c) ~= 0 then
                c.air_screamer_flags = s32(c.air_screamer_flags | (1 << 1))
            end
        end
    end

    return true
end

function hull_update(c)
    hull_from_bones(c)
    return true
end

function shape_save(c)
    local s = S.saved
    s.box_top, s.box_bottom, s.box_height, s.box_offset_y = c.box_top, c.box_bottom, c.box_height, c.box_offset_y
    s.box_extent_x, s.box_extent_z = c.box_extent_x, c.box_extent_z
    s.cylinder_radius, s.cylinder_extent = c.cylinder_radius, c.cylinder_extent
    s.box_offset_x, s.box_offset_z = c.box_offset_x, c.box_offset_z
    s.cylinder_offset_x, s.cylinder_offset_z = c.cylinder_offset_x, c.cylinder_offset_z
end

function shape_restore(c)
    local s = S.saved
    c.box_top, c.box_bottom, c.box_height, c.box_offset_y = s.box_top, s.box_bottom, s.box_height, s.box_offset_y
    c.box_extent_x, c.box_extent_z = s.box_extent_x, s.box_extent_z
    c.cylinder_radius, c.cylinder_extent = s.cylinder_radius, s.cylinder_extent
    c.box_offset_x, c.box_offset_z = s.box_offset_x, s.box_offset_z
    c.cylinder_offset_x, c.cylinder_offset_z = s.cylinder_offset_x, s.cylinder_offset_z
end

-- Collision shape from the body bones: the widest point sets the cylinder, the lowest and highest
-- (pushed out by a quarter) the box.
function hull_from_bones(c)
    local kind = flight_mode(c)
    if kind ~= 0 then
        local idx0 = 0
        local idx1 = 1

        local list = D.anchors[4]
        local pts = S.points

        local bone = list[2][4]
        local group_bone = list[1][4]

        -- Runs of anchors on the same bone are transformed together.
        local i = 1
        while bone ~= -1 do
            if bone ~= group_bone then
                anchor_points(c, list, idx0, idx1 - idx0, group_bone, idx0)
                group_bone = bone
                idx0 = idx1
            end

            idx1 = idx1 + 1
            bone = list[idx1 + 1][4]
            i = i + 1
        end

        anchor_points(c, list, idx0, idx1 - idx0, group_bone, idx0)

        local width_sqr = 0
        local high = -0x80000000
        local low = 0x7FFFFFFF

        local pos_x = c.pos_x
        local pos_y = c.pos_y
        local pos_z = c.pos_z

        i = i - 1
        while i >= 0 do
            local dx = s32(pts[i].x - pos_x)
            local dy = s32(pts[i].y - pos_y)
            local dz = s32(pts[i].z - pos_z)

            local d = s32(q12_mul(dx, dx) + q12_mul(dz, dz))
            if width_sqr < d then
                width_sqr = d
            end

            if high < dy then
                high = dy
            end

            if dy < low then
                low = dy
            end
            i = i - 1
        end

        width_sqr = asr(E.sqrt_q12(width_sqr), 1)
        local mid = idiv(s32(high + low), 2)

        c.cylinder_extent = width_sqr

        high = s32(high + asr(s32(high - mid), 2))
        low = s32(low + asr(s32(low - mid), 2))

        c.box_offset_y = mid
        c.box_extent_x = mid

        c.box_height = high
        c.box_top = low

        local bottom = D.hull[kind][0]
        local radius = D.hull[kind][1]

        E.set_collision_state(c.slot, 3)
        c.box_bottom = bottom
        c.cylinder_radius = radius
    else
        local bottom = D.hull[0][0]
        local radius = D.hull[0][1]

        c.cylinder_radius = radius
        c.cylinder_extent = 0

        c.box_height = 0
        c.box_top = 0
        c.box_offset_y = 0
        c.box_extent_x = 0
        c.box_bottom = bottom
        E.set_collision_state(c.slot, 0)
    end

    if role(c) == 3 then
        c.box_bottom = q12_mul(c.box_bottom, 4608)
        c.cylinder_extent = q12_mul(c.cylinder_extent, 4608)
    end

    if c.air_screamer_flags & (1 << 12) ~= 0 then
        c.box_bottom = q12_mul(c.box_bottom, 3584)
        c.cylinder_extent = q12_mul(c.cylinder_extent, 3584)
    end

    c.box_offset_x = 0
    c.box_offset_z = 0
    c.cylinder_offset_x = 0
    c.cylinder_offset_z = 0
end

-- Body shape while moving: the cylinder shrinks to 0.4 and leads in the direction of travel.
function moving_shape_set(c, move_speed, heading)
    local adj = heading
    local idx = flight_mode(c)
    local base = D.body[idx][1]
    c.box_bottom = D.body[idx][0]

    if move_speed == 0 then
        c.cylinder_offset_x = 0
        c.cylinder_offset_z = 0
        c.cylinder_radius = base
        return
    end

    local dist = base - 1638
    if dist < 0 then
        dist = 0
    end

    if move_speed < 0 then
        adj = adj ~ 2048
    end

    local off_x = q12_mul(dist, sh.math.sin(adj))
    local off_z = q12_mul(dist, sh.math.cos(adj))

    c.cylinder_offset_x = off_x
    c.cylinder_offset_z = off_z
    c.cylinder_radius = 1638 -- 0.4
end

function shape_hook(c)
end

function unused_stub()
end

-- control states 0..9 and the spawn-record roll between them.

function control_spawn_setup(c)
    forget_spawn_position(c)
    local spawn_number = c.spawn_index
    local attack_set
    local m3 = math.fmod(spawn_number, 3)
    if m3 == 1 then
        attack_set = 1
    elseif m3 == 2 then
        attack_set = 2
    else
        attack_set = 0
    end

    local spawn_side
    if spawn_number - idiv(spawn_number, 2) * 2 == 1 then
        spawn_side = false
    else
        spawn_side = true
    end

    local spawn_role, anim_status, control_state
    local state_step = 0
    local step = c.state_step
    if MAP0_S01 then
        if step == 13 then
            anim_status = 39 -- HoverVariable, active
            if sh.save.map_idx() == 1 then -- map0_s01
                spawn_role = 2
                spawn_side = true
                attack_set = 2
                anim_status = 5 -- HoverBiteAttack, active
                control_state = 46
            else
                spawn_role = 2
                if c.anim_status == 39 then
                    anim_status = -1
                end
                control_state = 18
            end
        else
            spawn_role = 4
            spawn_side = true
            attack_set = 3
            anim_status = -1
            control_state = 1
        end
    else
        if step == 0 or step == 2 then
            spawn_role = 1
            anim_status = 51 -- WalkForward, active
            control_state = 5
        elseif step == 3 then
            spawn_role = 1
            anim_status = 39 -- HoverVariable, active
            control_state = 19
        elseif step == 4 then
            spawn_role = 0
            anim_status = 51
            control_state = 4
            state_step = 64
        elseif step == 5 then
            spawn_role = 0
            anim_status = 39
            control_state = 18
            state_step = 64
        elseif step == 6 then
            spawn_role = 0
            anim_status = 35 -- Glide, active
            attack_set = 2
            control_state = 33
            state_step = 64
        elseif step == 8 then
            spawn_role = 1
            anim_status = 29 -- StandBiteAttackEnd, active
            attack_set = 4
            control_state = 9
        elseif step == 9 then
            spawn_role = 3
            anim_status = 39
            control_state = 19
            attack_set = 2
            c.health = s32(c.health * 4)
        elseif step == 11 then
            spawn_role = 0
            anim_status = 35
            control_state = 33
            state_step = 65
            attack_set = 2
        elseif step == 7 then
            spawn_role = 0
            anim_status = 35
            control_state = 33
            state_step = 66
            attack_set = 2
        elseif step == 10 then
            spawn_role = 0
            anim_status = 35
            control_state = 33
            state_step = 67
            attack_set = 2
            c.health = c.health - asr(c.health, 2)
        else
            spawn_role = 1
            anim_status = 47 -- StandIdle, active
            control_state = 3
        end
    end

    c.control_state = control_state
    c.state_step = state_step
    c.air_screamer_flags = 0
    set_role(c, spawn_role)
    set_side(c, spawn_side and 1 or 0)
    set_attack_set(c, attack_set)

    if control_state == 1 then
        c.air_screamer_flags = (1 << 16)
    end

    if anim_status ~= -1 and anim_status ~= c.anim_status then
        c.anim_status = anim_status
        local kf = clip_start(c, c.anim_status)
        c.anim_alpha = 0
        c.anim_keyframe = kf
        c.anim_time = s32(kf << 12)
    end
end

function control_scripted_intro(c)
    S.flags = s32(S.flags | 0x80000000)
    local set_anim = false

    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            set_anim = true
            c.anim_status = 47 -- StandIdle, active
            c.state_step = 1
        end
        c.air_screamer_flags = c.air_screamer_flags | (1 << 16)
    elseif step == 2 or step == 3 then
        if step == 2 then
            set_anim = true
            c.anim_status = 49 -- StandIdleToGlide, active
            c.state_step = 3
        end
        c.air_screamer_flags = c.air_screamer_flags | (1 << 16)
    elseif step == 4 or step == 5 then
        if step == 4 then
            set_anim = true
            c.anim_status = 31 -- GlideToHover, active
            c.state_step = 5
        end
        c.air_screamer_flags = c.air_screamer_flags & ~(1 << 16)
    elseif step == 6 then
        if c.anim_status == 39 then -- HoverVariable, active
            c.state_step = 7
        end
    elseif step == 7 then
        c.control_state = 0
        c.state_step = 13
        control_spawn_setup(c)
    end

    damage_take(c, 0)

    if set_anim then
        local kf = clip_start(c, c.anim_status)
        c.anim_keyframe = kf
        c.anim_time = s32(kf * 4096)
    end
end

local function has_spawn_row()
    return true
end

function roll_enemy_defeated_record(c)
    if c.spawn_index < 32 and map_data(c, "defeat_record", has_spawn_row) then
        local flags = spawn_flags(c.spawn_index)
        local chance
        if flags == 1 or flags == 8 or flags == 9 then
            chance = 0
        elseif flags == 10 then
            chance = 4096 -- 1.0
        elseif flags == 2 or flags == 3 then
            chance = 1228 -- 0.3
        else
            chance = 2867 -- 0.7
        end

        if rand_q12() >= chance then
            enemy_defeated(c)
        end
    end
end

function control_dying(c)
    local anim_status = c.anim_status
    local anim_status12 = 25 -- DeathThroes, active

    flight_goal_hold(c)

    local active_anim_status = (anim_status >> 1) * 2 + 1

    local step = c.state_step
    if step == 0 then
        local landed = settle_on_ground(c)
        local is_below_ground = false
        local cond1 = c.pos_y >= 32768 -- 8.0

        if ground_kind(c.pos_x, c.pos_z) == 7 then
            is_below_ground = c.pos_y >= ground_height(c.pos_x, c.pos_z)
        end

        if cond1 then
            damage_take(c, 0)
            c.pos_x = P.pos_x + 409600 -- 100.0
            c.pos_z = P.pos_z + 409600
            roll_enemy_defeated_record(c)
            return
        end

        if is_below_ground then
            damage_take(c, 0)
            crash_sound_play(c)
            c.pos_x = P.pos_x + 409600
            c.pos_z = P.pos_z + 409600
            roll_enemy_defeated_record(c)
            return
        end

        if c.health <= 0 then
            damage_take(c, 0)

            if anim_status == 53 and landed == true then -- Stun, active
                c.health = -1
                anim_status_handover(c, 3, 9, 2) -- StandToStun, active
                c.state_step = 1
                roll_enemy_defeated_record(c)
                return
            end
        else
            c.flags = c.flags | (1 << 1)

            if damage_take(c, 4096) == 4 then
                if c.health <= 0 then
                    c.anim_status = 10 -- Death, inactive
                    c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
                    return
                end

                c.anim_status = 24 -- DeathThroes, inactive
                c.state_step = 2
                c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
                return
            end

            if MAP0_S01 then
                local damage = s32(dt() * 10)
                if damage < c.health then
                    c.health = c.health - damage
                elseif c.health > 0 then
                    c.health = 0
                end
            end
        end
    elseif step == 1 then
        damage_take(c, 0)
    elseif step == 2 then
        damage_take(c, 2048) -- 0.5

        if active_anim_status ~= anim_status12 then
            c.state_step = 0
        end
    end
end

function control_stand_idle(c)
    if MAP0_S01 then return end

    if c.state_step == 0 then
        target_ground_ahead(c, 409, c.rot_y) -- 0.1
        c.state_step = 1
    end

    flight_goal_hold_facing(c)

    local result = damage_take(c, 4096)
    if result == 0 then
        if S.sight & 4 ~= 0 then
            local cond
            if attack_set(c) == 0 then
                cond = approach_score(c) >= 8192 -- 2.0
            else
                cond = true
            end

            if cond then
                c.control_state = 10
                c.state_step = 0
                c.flags = c.flags | (1 << 2)
                c.air_screamer_flags = c.air_screamer_flags | (1 << 4)
            end
        else
            if c.air_screamer_flags & 0x80000000 ~= 0 then
                c.control_state = 7
                c.state_step = 0
            end
        end
    elseif result == 1 or result == 2 then
        c.control_state = 16 -- recoil
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif result == 3 or result == 4 then
        c.control_state = 17 -- stun
        c.state_step = 0
        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_walk_wander(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local transition = 0
    local cond = false
    local mode = flight_mode(c)

    local swoop_reach = S.sight & 2 ~= 0
    local player_seen = S.sight & 4 ~= 0
    local bite_reach = S.sight & 1 ~= 0

    local step = c.state_step
    if step == 64 then
        respawn_ground_ahead(c)
        c.state_step = 1
    elseif step == 0 then
        target_ground_ahead(c, 16384, c.rot_y) -- 4.0
        target_ground_around(c,
            { x = c.air_screamer_target_x, y = c.air_screamer_target_y, z = c.air_screamer_target_z }, 12288) -- 3.0
        c.state_step = 1
    elseif step == 1 or step == 2 then
        if step == 1 then
            cond = true
            c.air_screamer_timer = 32768 -- 8.0
            c.state_step = 2
        end

        local dist_to_target = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)
        local turn_to_target = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z,
            c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z) - c.rot_y)
        local takeoff = takeoff_kind(c)

        if takeoff == 1 then
            c.state_step = 3
        elseif takeoff == 2 then
            c.state_step = 5
        elseif (dist_to_target < 4096 and (turn_to_target > -57 and turn_to_target < 57)) or
               c.air_screamer_flags < 0 then
            c.state_step = 0
        elseif c.air_screamer_timer == 0 then
            c.state_step = 1
        elseif cond then
            if front_clear(c) and dist_to_target > 24576 then -- 6.0
                c.state_step = 5
            elseif slope_under_body(c) then
                if dist_to_target >= 6144 or rand_q12() >= 2048 then -- 1.5, 0.5
                    if signed_range(c.air_screamer_target_y - c.pos_y, 4096) then
                        c.state_step = 3
                    end
                else
                    c.state_step = 3
                end
            end
        end
    elseif step == 3 then
        if anim_status == 51 or anim_status == 47 then -- WalkForward / StandIdle, active
            transition = 1
            c.anim_status = 44 -- StandIdleToHover, inactive
            c.state_step = 4
        end
    elseif step == 4 then
        transition = 1
    elseif step == 5 then
        if anim_status == 51 or anim_status == 47 then
            transition = 2
            c.anim_status = 48 -- StandIdleToGlide, inactive
            c.state_step = 6
        end
    elseif step == 6 then
        transition = 2
    end

    flight_goal_walk_detour(c, false)

    local result = damage_take(c, 4096)
    if result == 0 then
        local seen = swoop_reach or bite_reach or player_seen
        if transition == 0 then
            if seen then
                c.control_state = 6
                c.state_step = 0
            end
        elseif transition == 1 then
            if mode == 2 then
                if seen then
                    c.control_state = 20
                else
                    c.control_state = 18
                end
                c.state_step = 0
            end
        elseif transition == 2 then
            if mode == 3 then
                if seen then
                    c.control_state = 35
                else
                    c.control_state = 33
                end
                c.state_step = 0
            end
        end
    elseif result == 1 or result == 2 then
        c.control_state = 16 -- recoil
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif result == 3 or result == 4 then
        c.control_state = 17 -- stun
        c.state_step = 0
        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_walk_patrol(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local switch_cond = 0
    local cond = false

    local mode = flight_mode(c)
    local bite_reach = S.sight & 1 ~= 0
    local swoop_reach = S.sight & 2 ~= 0
    local player_seen = S.sight & 4 ~= 0

    local step = c.state_step
    if step == 0 then
        target_near_home_close(c)
        c.state_step = 1
    elseif step == 1 or step == 2 then
        if step == 1 then
            cond = true
            c.air_screamer_timer = 24576 -- 6.0
            c.state_step = 2
        end

        local dist = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)
        local angle = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z,
            c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z) - c.rot_y)

        local takeoff = takeoff_kind(c)
        if takeoff == 1 then
            c.state_step = 3
        elseif takeoff == 2 then
            c.state_step = 5
        elseif (dist < 2048 and (angle >= -56 and angle <= 56)) or -- 0.5, +-5 deg
               (c.air_screamer_flags & 0x80000000) ~= 0 then
            c.state_step = 0
        elseif c.air_screamer_timer == 0 then
            c.state_step = 1
        elseif cond then
            if front_clear(c) and dist > 24576 then -- 6.0
                c.state_step = 5
            elseif slope_under_body(c) then
                if signed_range(c.air_screamer_target_y - c.pos_y, 4096) then
                    c.state_step = 3
                end
            end
        end
    elseif step == 3 then
        if anim_status == 51 or anim_status == 47 then
            switch_cond = 1
            c.anim_status = 44
            c.state_step = 4
        end
    elseif step == 4 then
        switch_cond = 1
    elseif step == 5 then
        if anim_status == 51 or anim_status == 47 then
            switch_cond = 2
            c.anim_status = 48
            c.state_step = 6
        end
    elseif step == 6 then
        switch_cond = 2
    end

    flight_goal_walk_detour(c, false)

    local result = damage_take(c, 4096)
    if result == 0 then
        local seen = swoop_reach or bite_reach or player_seen
        if switch_cond == 0 then
            if seen then
                c.control_state = 6
                c.state_step = 0
            end
        elseif switch_cond == 1 then
            if mode == 2 then
                if seen then
                    c.control_state = 20
                else
                    c.control_state = 19
                end
                c.state_step = 0
            end
        elseif switch_cond == 2 then
            if mode == 3 then
                if seen then
                    c.control_state = 35
                else
                    c.control_state = 34
                end
                c.state_step = 0
            end
        end
    elseif result == 1 or result == 2 then
        c.control_state = 16 -- recoil
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif result == 3 or result == 4 then
        c.control_state = 17 -- stun
        c.state_step = 0
        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_walk_search(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local switch_cond = 0
    local cond = false
    local mode = flight_mode(c)
    local lunge = S.lunge_reach
    local bite_reach = S.sight & 1 ~= 0
    local swoop_reach = S.sight & 2 ~= 0
    local player_seen = S.sight & 4 ~= 0

    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            c.air_screamer_timer = 24576 -- 6.0
        end

        target_ground_around(c,
            { x = c.air_screamer_aim_x, y = c.air_screamer_aim_y, z = c.air_screamer_aim_z }, 6144) -- 1.5

        if not slope_under_body(c) then
            c.state_step = 2
        elseif rand_q12() >= 1228 then -- 0.3
            c.state_step = 2
        else
            c.state_step = 4
        end
    elseif step == 2 or step == 3 then
        if step == 2 then
            cond = true
        end

        if takeoff_kind(c) ~= 0 then
            c.state_step = 4
        elseif distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z) < 4096 then
            c.state_step = 1
        elseif c.air_screamer_timer == 0 then
            if cond then
                switch_cond = 1
            end
            c.state_step = 0
        elseif bite_reach or swoop_reach or player_seen then
            c.state_step = 3
            switch_cond = 0

            if lunge > 32 then
                local turn_to_aim = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z,
                    c.air_screamer_aim_x, c.air_screamer_aim_y, c.air_screamer_aim_z) - c.rot_y)
                target_ground_ahead(c, 6144, idiv(turn_to_aim, 4) + c.rot_y)
                c.air_screamer_timer = 24576
            end
        end
    elseif step == 4 then
        if anim_status == 51 or anim_status == 47 then -- WalkForward / StandIdle, active
            switch_cond = 2
            c.anim_status = 44 -- StandIdleToHover, inactive

            if player_seen or swoop_reach then
                c.state_step = 7
            else
                c.state_step = 6
            end
        elseif player_seen or swoop_reach then
            c.state_step = 5
        end
    elseif step == 5 then
        if anim_status == 51 or anim_status == 47 then
            switch_cond = 3
            c.anim_status = 44
            c.state_step = 7
        end
    elseif step == 6 then
        if player_seen or swoop_reach then
            switch_cond = 3
            c.state_step = 7
        else
            switch_cond = 2
        end
    elseif step == 7 then
        switch_cond = 3
    end

    flight_goal_walk_detour(c, false)

    local result = damage_take(c, 4096)
    if result == 0 then
        if switch_cond == 0 or switch_cond == 1 then
            if player_seen or swoop_reach then
                c.control_state = 7
                c.state_step = 0
            elseif switch_cond == 1 and rand_q12() < 2048 then -- 0.5
                local rand_val = rand_q12()
                local attack_bias = 0
                if attack_set(c) == 1 then
                    attack_bias = 409 -- 0.1
                end

                if rand_val < 2867 - attack_bias * 2 then -- 0.7
                    c.control_state = 15
                    c.state_step = 0
                end
            end
        elseif switch_cond == 2 then
            if mode == 2 then
                c.control_state = 20
                c.state_step = 0
            end
        elseif switch_cond == 3 then
            if mode == 2 then
                c.control_state = 21
                c.state_step = 0
            end
        end
    elseif result == 1 or result == 2 then
        c.control_state = 16 -- recoil
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif result == 3 or result == 4 then
        c.control_state = 17 -- stun
        c.state_step = 0
        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_walk_chase(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local switch_cond = 0
    local step_started = false
    local mode = flight_mode(c)
    local lunge = S.lunge_reach
    local bite_reach = S.sight & 1 ~= 0
    local swoop_reach = S.sight & 2 ~= 0
    local player_seen = S.sight & 4 ~= 0

    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            c.air_screamer_timer = 24576 -- 6.0
        end

        target_ground_around(c,
            { x = c.air_screamer_aim_x, y = c.air_screamer_aim_y, z = c.air_screamer_aim_z }, 8192) -- 2.0

        if not slope_under_body(c) then
            c.state_step = 2
        elseif rand_q12() >= 2048 then -- 0.5
            c.state_step = 2
        else
            c.state_step = 4
        end
    elseif step == 2 or step == 3 then
        if step == 2 then
            step_started = true
        end

        if takeoff_kind(c) ~= 0 then
            c.state_step = 4
        elseif distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z) < 4096 then
            c.state_step = 1
        elseif c.air_screamer_timer == 0 then
            if step_started then
                switch_cond = 1
            end
            c.state_step = 0
        elseif player_seen or bite_reach or swoop_reach then
            c.state_step = 3

            if lunge > 32 then
                local turn_to_aim = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z,
                    c.air_screamer_aim_x, c.air_screamer_aim_y, c.air_screamer_aim_z) - c.rot_y)
                target_ground_ahead(c, 6144, idiv(turn_to_aim, 4) + c.rot_y)
                c.air_screamer_timer = 24576
            end
        end
    elseif step == 4 then
        if anim_status == 51 or anim_status == 47 then -- WalkForward / StandIdle, active
            switch_cond = 2
            c.anim_status = 44 -- StandIdleToHover, inactive

            if swoop_reach or bite_reach then
                c.state_step = 7
            else
                c.state_step = 6
            end
        elseif player_seen or swoop_reach or bite_reach then
            c.state_step = 5
        end
    elseif step == 5 then
        if anim_status == 51 or anim_status == 47 then
            switch_cond = 3
            c.anim_status = 44
            c.state_step = 7
        end
    elseif step == 6 then
        if player_seen or swoop_reach or bite_reach then
            switch_cond = 3
            c.state_step = 7
        else
            switch_cond = 2
        end
    elseif step == 7 then
        switch_cond = 3
    end

    flight_goal_walk_detour(c, false)

    local result = damage_take(c, 4096)
    if result == 0 then
        if switch_cond == 0 or switch_cond == 1 then
            if player_seen then
                c.control_state = 8
                c.state_step = 0
            elseif switch_cond == 1 then
                local rand_val = rand_q12()
                local attack_bias = 0
                if attack_set(c) == 1 then
                    attack_bias = 409 -- 0.1
                end

                if rand_val < 2867 - attack_bias * 2 then -- 0.7
                    c.control_state = 6
                    c.state_step = 0
                end
            end
        elseif switch_cond == 2 then
            if mode == 2 then
                if not player_seen then
                    c.control_state = 20
                else
                    c.control_state = 22
                end
                c.state_step = 0
            end
        elseif switch_cond == 3 then
            if mode == 2 then
                if player_seen then
                    c.control_state = 22
                else
                    c.control_state = 21
                end
                c.state_step = 0
            end
        end
    elseif result == 1 or result == 2 then
        c.control_state = 16 -- recoil
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif result == 3 or result == 4 then
        c.control_state = 17 -- stun
        c.state_step = 0
        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_walk_approach(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local switch_cond1 = 0
    local side_bias = 0
    local mode = flight_mode(c)
    local harry_dist = S.player_distance
    local harry_angle = S.player_angle
    local player_seen = S.sight & 4 ~= 0

    if side(c) ~= 0 then
        side_bias = 409 -- 0.1
    end

    local takeoff = takeoff_kind(c)
    local switch_cond0 = 0

    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            switch_cond1 = 1
            c.air_screamer_timer = 8192 -- 2.0
            c.flags = c.flags | (1 << 2)
        end

        target_ground_ahead(c, idiv(harry_dist, 2), harry_angle)

        if not slope_under_body(c) or rand_q12() >= 1228 then -- 0.3
            if rand_q12() < 819 + side_bias * 2 then -- 0.2
                c.state_step = 2
            else
                c.state_step = 3
            end
        else
            c.state_step = 4
        end
    elseif step == 2 then
        switch_cond0 = 1

        if takeoff ~= 0 then
            c.state_step = 4
        elseif c.air_screamer_timer ~= 0 then
            if distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z) < 4096 then
                c.state_step = 1
            end
        else
            c.state_step = 0
        end
    elseif step == 3 then
        target_ground_ahead(c, idiv(harry_dist, 2), harry_angle)

        if takeoff ~= 0 then
            c.state_step = 4
        elseif c.air_screamer_timer == 0 then
            c.state_step = 0
        else
            local angle_diff = angle_norm(harry_angle - c.rot_y)
            if angle_diff >= -113 and angle_diff < 113 then -- +-10 deg
                switch_cond0 = 2
            end
        end
    elseif step == 4 then
        if anim_status == 51 or anim_status == 47 then -- WalkForward / StandIdle, active
            switch_cond1 = 2
            c.anim_status = 44 -- StandIdleToHover, inactive
            c.state_step = 5
        end
    elseif step == 5 then
        switch_cond1 = 2
    end

    if switch_cond0 == 1 then
        flight_goal_walk_detour(c, false)
    elseif switch_cond0 == 2 then
        flight_goal_hold_facing(c)
    else
        flight_goal_stand_turn(c)
    end

    local result = damage_take(c, 4096)
    if result == 0 then
        if switch_cond1 == 0 then
            if not player_seen then
                c.control_state = 7
                c.state_step = 0
            end
        elseif switch_cond1 == 1 then
            local chance0 = attack_chance(c, harry_dist)
            local chance1 = retreat_chance(c, harry_dist)

            if rand_q12() < chance0 then
                if role(c) == 3 then
                    call_reinforcement(c, c.spawn_index + 1, 2457) -- 0.6
                end

                c.control_state = 10
                c.state_step = 0
                c.air_screamer_flags = c.air_screamer_flags | (1 << 4)
            elseif rand_q12() < chance1 then
                c.control_state = 14
                c.state_step = 0
                c.air_screamer_flags = c.air_screamer_flags | (1 << 4)
            end
        elseif switch_cond1 == 2 then
            if mode == switch_cond1 then
                c.control_state = 22
                c.state_step = 0
            end
        end
    elseif result == 1 or result == 2 then
        c.control_state = 16 -- recoil
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif result == 3 or result == 4 then
        c.control_state = 17 -- stun
        c.state_step = 0
        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_feeding(c)
    if MAP0_S01 then return end

    local cond0 = false
    local anim_status = c.anim_status

    local step = c.state_step
    if step == 0 then
        target_ground_ahead(c, 409, c.rot_y) -- 0.1
        c.state_step = 7
        c.air_screamer_timer = 229376 -- 56.0
    elseif step >= 1 and step <= 7 then
        if c.air_screamer_timer == 0 then
            cond0 = true
        elseif ((step - 1) * 32768) >= (c.air_screamer_timer & 0xFFFFFFFF) and -- unsigned compare
               anim_status == 29 then -- StandBiteAttackEnd, active
            c.anim_status = 26 -- StandBiteAttackStart, inactive
            c.state_step = 8
            c.air_screamer_flags = c.air_screamer_flags | (1 << 2)
        end
    elseif step == 8 then
        c.air_screamer_timer = c.air_screamer_timer + dt()

        if anim_status == 29 then
            local timer_cut = (rand_q12() * 12) & 0xFFFFFFFF
            if timer_cut < (c.air_screamer_timer & 0xFFFFFFFF) then
                c.air_screamer_timer = c.air_screamer_timer - timer_cut
            else
                c.air_screamer_timer = 0
            end

            c.state_step = (c.air_screamer_timer & 0xFFFFFFFF) // 32768 + 1
        end
    end

    flight_goal_hold_facing(c)

    local result = damage_take(c, 4096)
    if result == 0 then
        if not cond0 then
            if anim_status & 1 ~= 0 then
                if S.sight & 4 ~= 0 then
                    local cond1
                    if attack_set(c) == 0 then
                        cond1 = approach_score(c) >= 8192 -- 2.0
                    else
                        cond1 = true
                    end

                    if cond1 then
                        c.anim_status = 46 -- StandIdle, inactive
                        c.control_state = 10
                        c.state_step = 0
                        c.flags = c.flags | (1 << 2)
                        c.air_screamer_flags = c.air_screamer_flags | (1 << 4)
                        set_attack_set(c, 3)
                    end
                elseif (c.air_screamer_flags & 0x80000000) ~= 0 or
                       S.sight & 2 ~= 0 or
                       S.lunge_reach > 40960 then -- 10.0
                    c.anim_status = 46
                    c.control_state = 7
                    c.state_step = 0
                    set_attack_set(c, 3)
                end
            end
        else
            c.anim_status = 46
            c.control_state = 15
            c.state_step = 0
            roll_enemy_defeated_record(c)
        end
    elseif result == 1 or result == 2 then
        c.control_state = 16 -- recoil
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif result == 3 or result == 4 then
        c.control_state = 17 -- stun
        c.state_step = 0
        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

-- Air Screamer / Night Flutter control states 10..20 (ground walk decisions, ground recoil and stun,
-- hover wander and hover approach).

function control_ground_stalk(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local mode = flight_mode(c)
    local player_seen = (S.sight >> 2) & 1
    local harry_dist = S.player_distance

    target_player_edge(c)
    local switch_cond = 0

    local step = c.state_step
    if step == 0 then
        local rolled_dist = s32(harry_dist + rand_q12() * 4)

        if front_clear(c) and rolled_dist > 32768 then -- 8.0
            c.state_step = 4
        elseif not slope_under_body(c) or (rolled_dist <= 16384 and rand_q12() >= 2048) then
            c.state_step = 1
        else
            c.state_step = 2
        end

        c.air_screamer_timer = 16384 -- 4.0
    elseif step == 1 then
        local takeoff = takeoff_kind(c)
        local stop_dist = 2457 -- 0.6
        local target_dist = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)

        if takeoff == 1 then
            c.state_step = 2
        elseif takeoff == 2 then
            c.state_step = 4
        elseif target_dist < 4505 then -- 1.1
            local body_top = c.pos_y + c.box_top + 409
            local player_y = P.pos_y
            local player_height = P.box_height
            local player_top = P.box_top

            local turn_to_target = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z,
                c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z) - c.rot_y)

            local stop = false
            if slope_under_body(c) then
                if (player_y + player_height) < body_top or body_top < (player_y + player_top) then
                    c.state_step = 2
                    stop = true
                end
            end

            if not stop and target_dist < stop_dist then
                if turn_to_target > -0x101 and turn_to_target < 0x100 then
                    switch_cond = 1
                end
            end
        elseif c.air_screamer_timer == 0 then
            c.state_step = 0
        end
    elseif step == 2 then
        if anim_status == 47 or anim_status == 51 then
            switch_cond = 2
            c.anim_status = 44
            c.state_step = 3
        end
    elseif step == 3 then
        switch_cond = 2
    elseif step == 4 then
        if anim_status == 47 or anim_status == 51 then
            switch_cond = 3
            c.anim_status = 48
            c.state_step = 5
        end
    elseif step == 5 then
        switch_cond = 3
    end

    flight_goal_walk_detour(c, true)

    local damage = damage_take(c, 4096)
    if damage == 0 then
        if switch_cond == 0 then
            if not far_from_home(c) then
                if player_seen == 0 then
                    c.control_state = 13
                    c.state_step = 0
                end
            else
                c.control_state = 14
                c.state_step = 0
            end
        elseif switch_cond == 1 then
            if (P.flags & 8) == 0 and event_npc(0) == -1 and event_npc(1) == -1 then
                if anim_status == 51 or anim_status == 47 then
                    c.control_state = 12
                    c.state_step = 0
                end
            else
                c.control_state = 11
                c.state_step = 0
            end
        elseif switch_cond == 2 then
            if mode == 2 then
                c.control_state = 23
                c.state_step = 0
            end
        elseif switch_cond == 3 then
            if mode == 3 then
                c.control_state = 38
                c.state_step = 0
            end
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 16
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 17
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_ground_back_off(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local idx = flight_mode(c)
    local switch2_idx = 0

    local bit3 = (S.sight >> 2) & 1
    local step = c.state_step
    local player_dist = S.player_distance

    if step == 0 then
        if not slope_under_body(c) or rand_q12() >= 2867 then
            c.air_screamer_timer = 8192 -- 2.0
            c.state_step = 1
        else
            c.state_step = 2
        end
    elseif step == 1 then
        if takeoff_kind(c) ~= 0 then
            c.state_step = 2
        end
    elseif step == 2 then
        if anim_status == 51 or anim_status == 47 then
            switch2_idx = 1
            c.anim_status = 44
            c.state_step = 3
        end
    elseif step == 3 then
        switch2_idx = 1
    end

    target_aim_ground(c)
    flight_goal_stand_lift(c)

    local damage = damage_take(c, 4096)
    if damage == 0 then
        if switch2_idx == 0 then
            if c.air_screamer_timer == 0 or player_dist > 0x2000 then
                if bit3 == 0 then
                    c.control_state = 13
                else
                    c.control_state = 10
                end

                c.state_step = 0
            end
        elseif switch2_idx == 1 then
            if idx == 2 then
                if bit3 ~= 0 then
                    c.control_state = 24
                else
                    c.control_state = 26
                end

                c.state_step = 0
            end
        else
            return
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 16
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        return
    elseif damage == 3 or damage == 4 then
        c.control_state = 17
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_ground_bite(c)
    if MAP0_S01 then return end

    local cond0 = false
    local cond1 = false
    local anim_status = c.anim_status

    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            c.state_step = 1
        end

        if anim_status == 51 or anim_status == 47 then
            c.state_step = 2
            c.anim_status = 2
        end
    elseif step == 2 then
        if anim_status == 3 and c.anim_keyframe < 8192 then
            c.state_step = 3
            c.air_screamer_flags = c.air_screamer_flags | (1 << 5)
        end
    elseif step == 3 then
        if anim_status == 51 or anim_status == 47 then
            cond0 = true
        elseif c.attack_hit == -1 then
            cond0 = true
            cond1 = true
            c.state_step = 4
        end
    elseif step == 4 then
        if anim_status == 51 or anim_status == 47 then
            cond0 = true
            cond1 = true
        end
    end

    flight_goal_hold_bank(c)

    local damage = damage_take(c, 4915) -- 1.2
    if damage == 0 then
        if cond0 then
            if attack_set(c) == 3 then
                set_attack_set(c, 2)

                if cond1 and rand_q12() < 2048 then
                    c.control_state = 11
                else
                    c.control_state = 10
                end
            else
                if cond1 then
                    c.control_state = 11
                elseif rand_q12() < 2048 then
                    c.control_state = 11
                else
                    c.control_state = 10
                end
            end

            c.state_step = 0
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 16
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 17
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_ground_wander(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local switch_cond = 0
    local mode = flight_mode(c)
    local swoop_reach = (S.sight >> 1) & 1
    local player_seen = (S.sight >> 2) & 1

    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            target_aim_ground(c)
        end

        c.air_screamer_timer = 16384 -- 4.0
        if slope_under_body(c) and rand_q12() < 1638 then
            c.state_step = 3
        else
            c.state_step = 2
        end
    elseif step == 2 then
        if takeoff_kind(c) ~= 0 then
            c.state_step = 3
        elseif distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z) < 2048 then
            target_ground_around(c, { x = c.air_screamer_aim_x, y = c.air_screamer_aim_y, z = c.air_screamer_aim_z }, 8192)
        elseif c.air_screamer_timer == 0 then
            target_ground_around(c, { x = c.air_screamer_aim_x, y = c.air_screamer_aim_y, z = c.air_screamer_aim_z }, 8192)
            c.state_step = 1
        end
    elseif step == 3 then
        if anim_status == 51 or anim_status == 47 then
            switch_cond = 1
            c.anim_status = 44
            c.state_step = 4
        end
    elseif step == 4 then
        switch_cond = 1
    end

    flight_goal_walk_detour(c, false)

    local damage = damage_take(c, 4096)
    if damage == 0 then
        if switch_cond == 0 then
            if not far_from_home(c) then
                if player_seen == 0 then
                    if c.air_screamer_timer == 0 and swoop_reach == 0 and rand_q12() < 2048 then
                        c.control_state = 7
                        c.state_step = 0
                    end
                else
                    c.control_state = 10
                    c.state_step = 0
                end
            else
                c.control_state = 14
                c.state_step = 0
            end
        elseif switch_cond == 1 then
            if mode == 2 then
                if player_seen ~= 0 then
                    c.control_state = 23
                else
                    c.control_state = 26
                end

                c.state_step = 0
            end
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 16
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 17
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_ground_search(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local switch_cond = 0
    local mode = flight_mode(c)
    local dist = S.player_distance
    local angle = S.player_angle
    local player_seen = (S.sight >> 2) & 1
    local dist_target = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)
    local orbit_done = false

    local step = c.state_step
    if step == 0 then
        orbit_done = target_orbit_point_walk(c)
        dist_target = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)
        local angle_delta = angle_norm(angle - c.rot_y)

        if front_clear(c) and
            (signed_range(angle_delta, 1365) or (dist < 20480 and signed_range(angle_delta, 682))) then
            c.state_step = 4
        elseif not slope_under_body(c) or (dist >= 8192 and rand_q12() >= 1638) then
            c.air_screamer_timer = 8192 -- 2.0
            c.state_step = 1
        else
            c.state_step = 2
        end
    elseif step == 1 then
        local takeoff = takeoff_kind(c)
        if takeoff == 1 then
            c.state_step = 2
        elseif takeoff == 2 then
            c.state_step = 4
        elseif dist_target < 8192 or (c.air_screamer_flags & 0x80000000) ~= 0 then
            orbit_done = target_orbit_point_walk(c)
        elseif c.air_screamer_timer == 0 then
            c.state_step = 0
        end
    elseif step == 2 then
        if anim_status == 47 or anim_status == 51 then
            switch_cond = 1
            c.anim_status = 44
            c.state_step = 3
        end
    elseif step == 3 then
        switch_cond = 1
    elseif step == 4 then
        if anim_status == 47 or anim_status == 51 then
            switch_cond = 2
            c.anim_status = 48
            c.state_step = 5
        end
    elseif step == 5 then
        switch_cond = 2
    end

    flight_goal_walk_detour(c, true)

    local damage = damage_take(c, 4096)
    if damage == 0 then
        if switch_cond == 0 then
            dist_target = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)

            if orbit_done or ((((player_seen ~= 0) and dist < 61440) or dist < 16384) and dist_target < 6144) then
                c.control_state = 10
                c.state_step = 0
                set_attack_set(c, 3)
                c.air_screamer_flags = c.air_screamer_flags | (1 << 4)
            elseif dist > 122880 and dist_target < 4096 then -- 30.0, 1.0
                c.control_state = 7
                c.state_step = 0
            end
        elseif switch_cond == 1 then
            if mode == 2 then
                c.control_state = 27
                c.state_step = 0
            end
        elseif switch_cond == 2 then
            if mode == 3 then
                c.control_state = 42
                c.state_step = 0
            end
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 16
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 17
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_ground_settle(c)
    if MAP0_S01 then return end

    target_ground_ahead(c, 2048, c.rot_y)
    flight_goal_hold_facing(c)
    c.flags = c.flags & ~(1 << 2)

    local damage = damage_take(c, 4096)
    if damage == 0 then
        local r = role(c)
        if r == 0 then
            c.control_state = 4
            c.state_step = 0
        elseif r == 1 then
            c.control_state = 5
            c.state_step = 0
        elseif r == 3 then
            c.control_state = 5
            c.state_step = 0
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 16
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 17
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_ground_recoil(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local player_seen = (S.sight >> 2) & 1
    c.flags = c.flags | (1 << 2)
    local cond = false

    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            if player_seen ~= 0 then
                target_player_edge(c)
            else
                target_ground_ahead(c, 16384, angles_between(c.pos_x, c.pos_y, c.pos_z, P.pos_x, P.pos_y, P.pos_z))
                c.air_screamer_aim_x = c.air_screamer_target_x
                c.air_screamer_aim_y = c.air_screamer_target_y
                c.air_screamer_aim_z = c.air_screamer_target_z
            end

            c.state_step = 1
        end

        if (anim_status & 1) ~= 0 then
            c.anim_status = 12
            c.state_step = 2
        end
    elseif step == 2 then
        if anim_status ~= 12 then
            c.state_step = 3
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    elseif step == 3 then
        if anim_status ~= 13 then
            cond = true
        end
    end

    flight_goal_hold_glance(c)

    local damage = damage_take(c, 2457) -- 0.6
    if damage == 0 or damage == 1 or damage == 2 then
        if not cond then
            return
        end

        local side_bias = (side(c) ~= 0) and 409 or 0

        if role(c) == 3 then
            call_reinforcement(c, c.spawn_index + 1, 2867) -- 0.7
        end

        if player_seen ~= 0 then
            if attack_set(c) == 5 then
                c.control_state = 14
                c.state_step = 0

                if rand_q12() < idiv(s32(c.health << 12), 1556480) + side_bias * 2 then
                    set_attack_set(c, 3)
                end
            else
                c.control_state = 10
                c.state_step = 0

                if rand_q12() <= idiv(s32(c.health << 12), 1556480) + side_bias * 2 then
                    set_attack_set(c, 3)
                else
                    set_attack_set(c, 5)
                end
            end
            return
        end

        local a = attack_set(c)
        if a == 3 then
            c.control_state = 13
            c.state_step = 0

            if rand_q12() < 2048 - side_bias * 3 then
                set_attack_set(c, 2)
            end
        elseif a == 2 then
            c.control_state = 7
            c.state_step = 0

            if rand_q12() > idiv(s32(c.health << 12), 1556480) + side_bias * 2 then
                set_attack_set(c, 5)
            elseif rand_q12() < 1228 + side_bias * 3 then
                set_attack_set(c, 3)
            end
        elseif a == 5 then
            c.control_state = 14
            c.state_step = 0

            if rand_q12() < idiv(s32(c.health << 12), 1556480) then
                set_attack_set(c, 3)
            end
        elseif a == 1 or a == 4 then
            if rand_q12() < 819 + side_bias * 5 then
                c.control_state = 7
            else
                c.control_state = 14
            end

            c.state_step = 0

            local next_attack_set
            if rand_q12() < 2048 + side_bias * 3 then
                next_attack_set = 2
            else
                next_attack_set = 5
            end

            set_attack_set(c, next_attack_set)
        else
            if role(c) == 3 then
                side_bias = 4096
            end

            if rand_q12() < 1638 + side_bias * 3 then
                c.control_state = 7
            else
                c.control_state = 14
            end

            c.state_step = 0

            local next_attack_set
            if rand_q12() < 2457 + side_bias * 2 then
                next_attack_set = 2
            else
                next_attack_set = 5
            end

            set_attack_set(c, next_attack_set)
        end
    elseif damage == 3 or damage == 4 then
        c.control_state = 17
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_ground_stun(c)
    if MAP0_S01 then return end

    local cond = false
    local step = c.state_step
    local anim_status = c.anim_status
    c.flags = c.flags | (1 << 2)

    if step == 0 then
        if (anim_status & 1) ~= 0 then
            c.anim_status = 8
            c.state_step = 1
        end
    elseif step == 1 then
        if anim_status ~= 8 then
            c.state_step = 2
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    elseif step == 2 then
        if anim_status ~= 9 then
            cond = true
        end
    end

    flight_goal_stunned(c)
    damage_take(c, 2457) -- 0.6

    if cond then
        c.control_state = 2
        c.state_step = 0
        set_attack_set(c, 1)
    end
end

function control_hover_wander_far(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local switch_cond = 0
    local mode = flight_mode(c)
    local cond = false

    local bite_reach = S.sight & 1
    local swoop_reach = (S.sight >> 1) & 1
    local player_seen = (S.sight >> 2) & 1

    local can_land = hover_can_land(c)

    local step = c.state_step
    if step == 64 then
        respawn_air_ahead(c)
        c.state_step = 1
    elseif step == 0 then
        target_air_ahead(c, 20480, c.rot_y) -- 5.0
        target_air_around(c, { x = c.air_screamer_target_x, y = c.air_screamer_target_y, z = c.air_screamer_target_z }, 16384)
        c.state_step = 1
    elseif step == 1 or step == 2 then
        if step == 1 then
            cond = true
            c.air_screamer_timer = 24576 -- 6.0
            c.state_step = 2
        end

        local dist_to_target = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)
        local angle_delta = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z,
            c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z) - c.rot_y)

        if not can_land then
            if (dist_to_target < 4096 and (angle_delta >= -56 and angle_delta <= 56)) or
                (c.air_screamer_flags & 0x80000000) ~= 0 then
                c.state_step = 0
            else
                if c.air_screamer_timer == 0 then
                    c.state_step = 1
                elseif cond then
                    if front_clear(c) and dist_to_target > 28672 then -- 7.0
                        c.state_step = 5
                    elseif can_land_here(c) and dist_to_target < 12288 and rand_q12() < 2867 then
                        c.state_step = 3
                    end
                end
            end
        else
            c.state_step = 3
        end
    elseif step == 3 then
        if anim_status == 39 then
            switch_cond = 1
            c.anim_status = 54
            c.state_step = 4
        end
    elseif step == 4 then
        switch_cond = 1
    elseif step == 5 then
        if anim_status == 39 then
            switch_cond = 2
            c.anim_status = 36
            c.state_step = 6
        end
    elseif step == 6 then
        switch_cond = 2
    end

    flight_goal_fly_detour(c)

    local damage = damage_take(c, 4096)
    if damage == 0 then
        if switch_cond == 0 then
            if (swoop_reach | bite_reach | player_seen) ~= 0 then
                c.control_state = 20
                c.state_step = 0
            end
        elseif switch_cond == 1 then
            if anim_status == 47 or anim_status == 51 then
                if (swoop_reach | bite_reach | player_seen) == 0 then
                    c.control_state = 4
                else
                    c.control_state = 6
                end

                c.state_step = 0
            elseif near_ground(c) then
                c.anim_status = 46
            end
        elseif switch_cond == 2 then
            if mode == 3 then
                if (swoop_reach | bite_reach | player_seen) == 0 then
                    c.control_state = 33
                else
                    c.control_state = 35
                end

                c.state_step = 0
            end
        end
    elseif damage == 1 then
        c.control_state = 29
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 2 then
        c.control_state = 30
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 32
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_hover_wander_near(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local switch_cond = 0

    local mode = flight_mode(c)
    local cond = false
    local bite_reach = S.sight & 1
    local swoop_reach = (S.sight >> 1) & 1
    local player_seen = (S.sight >> 2) & 1

    local can_land = hover_can_land(c)

    local step = c.state_step
    if step == 0 then
        target_near_home_medium(c)
        c.state_step = 1
    elseif step == 1 or step == 2 then
        if step == 1 then
            cond = true
            c.air_screamer_timer = 24576 -- 6.0
            c.state_step = 2
        end

        local dist_target = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)
        local ang_target = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z,
            c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z) - c.rot_y)

        if not can_land then
            if (dist_target < 2048 and (ang_target >= -56 and ang_target <= 56)) or
                (c.air_screamer_flags & 0x80000000) ~= 0 then
                c.state_step = 0
            elseif c.air_screamer_timer == 0 then
                c.state_step = 1
            elseif cond then
                if front_clear(c) and dist_target > 24576 then -- 6.0
                    c.state_step = 5
                elseif can_land_here(c) and dist_target < 8192 and rand_q12() < 2048 then
                    c.state_step = 3
                end
            end
        else
            c.state_step = 3
        end
    elseif step == 3 then
        if anim_status == 39 then
            switch_cond = 1
            c.anim_status = 54
            c.state_step = 4
        end
    elseif step == 4 then
        switch_cond = 1
    elseif step == 5 then
        if anim_status == 39 then
            switch_cond = 2
            c.anim_status = 48
            c.state_step = 6
        end
    elseif step == 6 then
        switch_cond = 2
    end

    flight_goal_fly_detour(c)

    local damage = damage_take(c, 4096)
    if damage == 0 then
        if switch_cond == 0 then
            if (swoop_reach | bite_reach | player_seen) ~= 0 then
                c.control_state = 20
                c.state_step = 0
            end
        elseif switch_cond == 1 then
            if anim_status == 47 or anim_status == 51 then
                if (swoop_reach | bite_reach | player_seen) ~= 0 then
                    c.control_state = 6
                else
                    c.control_state = 5
                end

                c.state_step = 0
            elseif near_ground(c) then
                c.anim_status = 46
            end
        elseif switch_cond == 2 then
            if mode == 3 then
                if (swoop_reach | bite_reach | player_seen) ~= 0 then
                    c.control_state = 35
                else
                    c.control_state = 34
                end

                c.state_step = 0
            end
        end
    elseif damage == 1 then
        c.control_state = 29
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 2 then
        c.control_state = 30
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 32
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_hover_approach(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local angle0 = S.lunge_reach

    local bite_reach = S.sight & 1
    local swoop_reach = (S.sight >> 1) & 1
    local player_seen = (S.sight >> 2) & 1
    local can_land = hover_can_land(c)
    local switch_cond = 0
    local cond_step_is_2 = false

    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            c.air_screamer_timer = 24576 -- 6.0
        end

        target_air_around(c, { x = c.air_screamer_aim_x, y = c.air_screamer_aim_y, z = c.air_screamer_aim_z }, 6144)

        if not can_land_here(c) or rand_q12() >= 819 then
            c.state_step = 2
        else
            c.state_step = 4
        end
    elseif step == 2 or step == 3 then
        if step == 2 then
            cond_step_is_2 = true
        end

        if can_land then
            c.state_step = 4
        elseif distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z) < 4096 then
            c.state_step = 1
        elseif c.air_screamer_timer == 0 then
            if cond_step_is_2 then
                switch_cond = 1
            end

            c.state_step = 0
        elseif bite_reach ~= 0 or swoop_reach ~= 0 or player_seen ~= 0 then
            c.state_step = 3

            if angle0 > 32 then -- 2.9 degrees
                local angle1 = angles_between(c.pos_x, c.pos_y, c.pos_z,
                    c.air_screamer_aim_x, c.air_screamer_aim_y, c.air_screamer_aim_z) - c.rot_y
                angle1 = angle_norm(angle1)
                if angle1 < 0 then
                    angle1 = angle1 + 3
                end

                target_air_ahead(c, 6144, asr(angle1, 2) + c.rot_y)
                c.air_screamer_timer = 24576 -- 6.0
            end
        end
    elseif step == 4 then
        if anim_status == 39 then
            switch_cond = 2
            c.anim_status = 54

            if player_seen ~= 0 or swoop_reach ~= 0 then
                c.state_step = 7
            else
                c.state_step = 6
            end
        elseif player_seen ~= 0 or swoop_reach ~= 0 then
            c.state_step = 5
        end
    elseif step == 5 then
        if anim_status == 39 then
            switch_cond = 3
            c.anim_status = 54
            c.state_step = 7
        end
    elseif step == 6 then
        switch_cond = 3

        if player_seen ~= 0 or swoop_reach ~= 0 then
            c.state_step = 7
        else
            switch_cond = 2
        end
    elseif step == 7 then
        switch_cond = 3
    end

    flight_goal_fly_detour(c)

    local damage = damage_take(c, 4096)
    if damage == 0 then
        if switch_cond == 0 or switch_cond == 1 then
            if player_seen == 0 and swoop_reach == 0 then
                if switch_cond == 1 then
                    local rng_angle0 = rand_q12()
                    local angle_subtract = 0

                    if attack_set(c) == switch_cond then
                        angle_subtract = 409 -- 36 degrees
                    end

                    if rng_angle0 < (2867 - angle_subtract * 2) then
                        c.control_state = 28
                        c.state_step = 0
                    end
                end
            else
                c.control_state = 21
                c.state_step = 0
            end
        elseif switch_cond == 2 then
            if anim_status == 47 or anim_status == 51 then
                c.control_state = 6
                c.state_step = 0
            elseif near_ground(c) then
                c.anim_status = 46
            end
        elseif switch_cond == 3 then
            if anim_status == 47 or anim_status == 51 then
                c.control_state = 7
                c.state_step = 0
            elseif near_ground(c) then
                c.anim_status = 46
            end
        end
    elseif damage == 1 then
        c.control_state = 29
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 2 then
        c.control_state = 30
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 32
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

-- Air Screamer / Night Flutter control states 21..31 (hovering wander, approach, bite, retreat, recoil).

-- Their bool is an unsigned int: a callee may hand back any integer, and only 0 is false.
local function truthy(v)
    return v ~= nil and v ~= false and v ~= 0
end

function control_hover_wander(c)
    if MAP0_S01 then return end

    local switch3 = 0
    local cond_step2 = 0
    local anim_status = c.anim_status
    local angle0 = S.lunge_reach

    local sight0 = (S.sight & 1) ~= 0
    local sight1 = (S.sight & 2) ~= 0
    local sight2 = (S.sight & 4) ~= 0
    local blocked = truthy(hover_can_land(c))

    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            c.air_screamer_timer = 24576 -- 6.0
        end

        target_air_around(c, { x = c.air_screamer_aim_x, y = c.air_screamer_aim_y, z = c.air_screamer_aim_z }, 8192)

        if not truthy(can_land_here(c)) or rand_q12() >= 409 then
            c.state_step = 2
        else
            c.state_step = 4
        end
    elseif step == 2 or step == 3 then
        if step == 2 then
            cond_step2 = 1
        end

        if blocked then
            c.state_step = 4
        elseif distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z) < 4096 then
            if cond_step2 ~= 0 then
                switch3 = 1
            end

            c.state_step = 1
        elseif c.air_screamer_timer == 0 then
            c.state_step = 0
        elseif sight2 or sight0 or sight1 then
            c.state_step = 3

            if angle0 > 32 then
                local angle1 = s32(angles_between(c.pos_x, c.pos_y, c.pos_z,
                                                  c.air_screamer_aim_x, c.air_screamer_aim_y, c.air_screamer_aim_z) - c.rot_y)
                angle1 = angle_norm(angle1)
                if angle1 < 0 then
                    angle1 = angle1 + 3
                end

                target_air_ahead(c, 8192, asr(angle1, 2) + c.rot_y)
                c.air_screamer_timer = 24576 -- 6.0
            end
        end
    elseif step == 4 then
        if anim_status == 39 then
            switch3 = 2
            c.anim_status = 54

            if sight1 or sight0 then
                c.state_step = 7
            else
                c.state_step = 6
            end
        elseif sight2 or sight1 or sight0 then
            c.state_step = 5
        end
    elseif step == 5 then
        if anim_status == 39 then
            switch3 = 3
            c.anim_status = 54
            c.state_step = 7
        end
    elseif step == 6 then
        switch3 = 3
        if sight2 or sight1 or sight0 then
            c.state_step = 7
        else
            switch3 = 2
        end
    elseif step == 7 then
        switch3 = 3
    end

    flight_goal_fly_detour(c)

    local damage = damage_take(c, 4096)
    if damage == 0 then
        if switch3 == 0 or switch3 == 1 then
            if not sight2 then
                if switch3 == 1 then
                    local rng_angle = rand_q12()
                    local angle_sub = 0

                    if attack_set(c) == switch3 then
                        angle_sub = 409
                    end

                    if rng_angle < (2867 - (angle_sub * 2)) then
                        c.control_state = 20
                        c.state_step = 0
                    end
                end
            else
                c.control_state = 22
                c.state_step = 0
            end
        elseif switch3 == 2 then
            if anim_status == 47 or anim_status == 51 then
                if not sight2 then
                    c.control_state = 6
                else
                    c.control_state = 8
                end

                c.state_step = 0
            elseif truthy(near_ground(c)) then
                c.anim_status = 46
            end
        elseif switch3 == 3 then
            if anim_status == 47 or anim_status == 51 then
                if sight2 then
                    c.control_state = 8
                else
                    c.control_state = 7
                end

                c.state_step = 0
            elseif truthy(near_ground(c)) then
                c.anim_status = 46
            end
        end
    elseif damage == 1 then
        c.control_state = 29
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 2 then
        c.control_state = 30
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 32
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_hover_circle_player(c)
    if MAP0_S01 then return end

    local switch_cond = 0
    local angle_add = 0
    local anim_status = c.anim_status
    local dist0 = S.player_distance
    local angle0 = S.player_angle
    local sight2 = (S.sight & 4) ~= 0

    if side(c) ~= 0 then
        angle_add = 409 -- 0.1
    end

    local blocked = truthy(hover_can_land(c))
    local mode = 0

    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            switch_cond = 1
            c.air_screamer_timer = 8192 -- 2.0
            c.flags = c.flags | (1 << 2)
        end

        target_air_ahead(c, idiv(dist0, 2), angle0)

        if rand_q12() < ((angle_add * 2) + 819) then
            c.state_step = 2
        else
            c.state_step = 3
        end
    elseif step == 2 then
        mode = 1

        if not blocked then
            if c.air_screamer_timer ~= 0 then
                if distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z) < 4096 then
                    c.state_step = 1
                end
            else
                c.state_step = 0
            end
        else
            c.state_step = 4
        end
    elseif step == 3 then
        target_air_ahead(c, idiv(dist0, 2), angle0)

        if blocked then
            c.state_step = 4
        elseif c.air_screamer_timer == 0 then
            c.state_step = 0
        elseif angle_norm(angle0 - c.rot_y) >= -113 and angle_norm(angle0 - c.rot_y) < 113 then
            mode = 2
        end
    elseif step == 4 then
        if anim_status == 39 then
            switch_cond = 2
            c.anim_status = 54
            c.state_step = 5
        end
    elseif step == 5 then
        switch_cond = 2
    end

    if mode == 1 then
        flight_goal_fly_detour(c)
    else
        flight_goal_hover_target(c)
    end

    local damage = damage_take(c, 4096)
    if damage == 0 then
        if switch_cond == 0 then
            if not sight2 then
                c.control_state = 21
                c.state_step = 0
            end
        elseif switch_cond == 2 then
            if anim_status == 47 or anim_status == 51 then
                c.control_state = 8
                c.state_step = 0
            elseif truthy(near_ground(c)) then
                c.anim_status = 46
            end
        elseif switch_cond == 1 then
            local chance0 = attack_chance(c, dist0)
            local chance1 = retreat_chance(c, dist0)
            if rand_q12() < chance0 then
                if role(c) == 3 then
                    call_reinforcement(c, c.spawn_index + 1, 2457) -- 0.6
                end

                c.control_state = 23
                c.state_step = 0
                c.air_screamer_flags = c.air_screamer_flags | (1 << 4)
            elseif rand_q12() < chance1 then
                c.control_state = 27
                c.state_step = 0
                c.air_screamer_flags = c.air_screamer_flags | (1 << 4)
            end
        end
    elseif damage == 1 then
        c.control_state = 29
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 2 then
        c.control_state = 30
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 32
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_hover_close_in(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local switch_cond = 0

    local hover_kind = flight_mode(c)
    local dist0 = S.player_distance
    local sight2 = (S.sight & 4) ~= 0
    local blocked = truthy(hover_can_land(c))

    target_player_flank(c)

    local step = c.state_step
    if step == 0 then
        local dist_test = s32(dist0 + (rand_q12() * 4))
        if truthy(front_clear(c)) and dist_test > 32768 then
            c.state_step = 4
        elseif not truthy(can_land_here(c)) or dist_test >= 16384 or
               (angle_norm(P.rot_y - c.rot_y) < -341 or angle_norm(P.rot_y - c.rot_y) >= 341) or
               rand_q12() >= 819 then
            c.state_step = 1
        else
            c.state_step = 2
        end

        c.air_screamer_timer = 16384 -- 4.0
    elseif step == 1 then
        local dist_test = -1
        if c.air_screamer_target_y - c.pos_y > -819 and c.air_screamer_target_y - c.pos_y < 3276 then
            dist_test = c.cylinder_radius + 614
        end

        local dist1 = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)
        local diff_angle = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z,
                                                     c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z) - c.rot_y)

        if blocked then
            c.state_step = 2
        elseif dist1 < dist_test and (diff_angle >= -256 and diff_angle < 256) then
            switch_cond = 1
        elseif c.air_screamer_timer == 0 then
            c.state_step = 0
        end
    elseif step == 2 then
        if anim_status == 39 then
            switch_cond = 2
            c.anim_status = 54
            c.state_step = 3
        end
    elseif step == 3 then
        switch_cond = 2
    elseif step == 4 then
        if anim_status == 39 then
            switch_cond = 3
            c.anim_status = 36
            c.state_step = 5
        end
    elseif step == 5 then
        switch_cond = 3
    end

    flight_goal_fly_detour(c)

    local damage = damage_take(c, 4096)
    if damage == 0 then
        if switch_cond == 0 then
            if not truthy(far_from_home(c)) then
                if not sight2 then
                    c.control_state = 26
                    c.state_step = 0
                end
            else
                c.control_state = 27
                c.state_step = 0
            end
        elseif switch_cond == 1 then
            if (P.flags & (1 << 3)) == 0 and event_npc(0) == -1 and event_npc(1) == -1 then
                if anim_status == 39 then
                    c.control_state = 25
                    c.state_step = 0
                end
            else
                c.control_state = 24
                c.state_step = 0
            end
        elseif switch_cond == 2 then
            if anim_status == 47 or anim_status == 51 then
                target_player_edge(c)
                c.control_state = 10
                c.state_step = 0
            elseif truthy(near_ground(c)) then
                c.anim_status = 46
            end
        elseif switch_cond == 3 then
            if hover_kind == switch_cond then
                c.control_state = 38
                c.state_step = 0
            end
        end
    elseif damage == 1 then
        c.control_state = 29
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 2 then
        c.control_state = 30
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 32
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_hover_back_off(c)
    if MAP0_S01 then return end

    local switch_cond = 0
    local anim_status = c.anim_status
    local dist = S.player_distance
    local sight2 = (S.sight & 4) ~= 0
    local blocked = truthy(hover_can_land(c))

    local step = c.state_step
    if step == 0 then
        c.air_screamer_timer = 8192 -- 2.0
        if rand_q12() < 2867 then
            switch_cond = 2
            target_air_ahead(c, 32768, c.rot_y)
            c.state_step = 4
        else
            c.state_step = 1
        end
    elseif step == 1 then
        if blocked then
            c.state_step = 2
        end
    elseif step == 4 then
        switch_cond = 2

        if blocked then
            c.state_step = 2
        elseif c.move_speed > 6144 then
            c.state_step = 5
        elseif distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z) < 12288 or
               c.air_screamer_timer == 0 then
            c.state_step = 1
        end
    elseif step == 2 then
        if anim_status == 39 then
            switch_cond = 1
            c.anim_status = 54
            c.state_step = 3
        end
    elseif step == 3 then
        switch_cond = 1
    elseif step == 5 then
        if anim_status == 39 then
            switch_cond = 3
            c.anim_status = 36
            c.state_step = 6
        end
    elseif step == 6 then
        switch_cond = 3
    end

    if switch_cond >= 2 and switch_cond < 4 then
        flight_goal_fly_detour(c)
    else
        target_aim_air(c)
        flight_goal_fly_target(c)
    end

    local damage = damage_take(c, 4096)
    if damage == 0 then
        if switch_cond == 0 then
            if c.air_screamer_timer == 0 or dist > 8192 then
                if not sight2 then
                    c.control_state = 26
                else
                    c.control_state = 23
                end

                c.state_step = 0
            end
        elseif switch_cond == 1 then
            if anim_status == 47 or anim_status == 51 then
                if not sight2 then
                    c.control_state = 13
                else
                    c.control_state = 11
                end

                c.state_step = 0
            elseif truthy(near_ground(c)) then
                c.anim_status = 46
            end
        elseif switch_cond == 2 then
            -- nothing
        elseif switch_cond == 3 then
            if flight_mode(c) == switch_cond then
                c.control_state = 39
                c.state_step = 0
            end
        end
    elseif damage == 1 then
        c.control_state = 29
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 2 then
        c.control_state = 30
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 32
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_hover_bite(c)
    if MAP0_S01 then return end

    local done = false
    local missed = false
    local anim_status = c.anim_status

    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            c.state_step = 1
        end

        if anim_status == 39 then
            c.state_step = 2
            c.anim_status = 4
        end
    elseif step == 2 then
        if anim_status == 5 and c.anim_keyframe < 8192 then
            c.state_step = 3
            c.air_screamer_flags = c.air_screamer_flags | (1 << 5)
        end
    elseif step == 3 then
        if anim_status == 39 then
            done = true
        elseif c.attack_hit == -1 then
            done = true
            missed = true
            c.state_step = 4
        end
    elseif step == 4 then
        if anim_status == 39 then
            done = true
            missed = true
        end
    end

    target_player_flank(c)
    flight_goal_hover_bank(c)

    local damage = damage_take(c, 4915) -- 1.2
    if damage == 0 then
        if done then
            if attack_set(c) == 3 then
                set_attack_set(c, 2)

                if missed and rand_q12() < 2048 then
                    c.control_state = 24
                else
                    c.control_state = 23
                end
            else
                if missed then
                    c.control_state = 24
                elseif rand_q12() < 2048 then
                    c.control_state = 24
                else
                    c.control_state = 23
                end
            end
            c.state_step = 0
        end
    elseif damage == 1 then
        c.control_state = 29
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 2 then
        c.control_state = 30
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 32
        c.state_step = 0
        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_hover_search(c)
    if MAP0_S01 then return end

    local switch_cond = false
    local anim_status = c.anim_status

    local sight1 = (S.sight & 2) ~= 0
    local sight2 = (S.sight & 4) ~= 0

    local blocked = truthy(hover_can_land(c))

    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            target_aim_air(c)
        end

        c.air_screamer_timer = 16384 -- 4.0
        c.state_step = 2
    elseif step == 2 then
        if blocked then
            c.state_step = 3
        elseif distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z) < 2048 then
            target_air_around(c, { x = c.air_screamer_aim_x, y = c.air_screamer_aim_y, z = c.air_screamer_aim_z }, 10240)
        elseif c.air_screamer_timer == 0 then
            target_air_around(c, { x = c.air_screamer_aim_x, y = c.air_screamer_aim_y, z = c.air_screamer_aim_z }, 10240)
            c.state_step = 1
        end
    elseif step == 3 then
        if anim_status == 39 then
            switch_cond = true
            c.anim_status = 54
            c.state_step = 4
        end
    elseif step == 4 then
        switch_cond = true
    end

    flight_goal_fly_detour(c)

    local damage = damage_take(c, 4096)
    if damage == 0 then
        if not switch_cond then
            local change = true
            if truthy(far_from_home(c)) then
                c.control_state = 27
            elseif not sight2 then
                if c.air_screamer_timer == 0 and not sight1 and rand_q12() < 2048 then
                    c.control_state = 21
                else
                    change = false
                end
            else
                c.control_state = 23
            end

            if change then
                c.state_step = 0
            end
        else
            if anim_status == 47 or anim_status == 51 then
                if sight2 then
                    c.control_state = 10
                else
                    c.control_state = 13
                end

                c.state_step = 0
            elseif truthy(near_ground(c)) then
                c.anim_status = 46
            end
        end
    elseif damage == 1 then
        c.control_state = 29
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 2 then
        c.control_state = 30
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 32
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_hover_reposition(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local switch_cond = 0
    local hover_kind = flight_mode(c)
    local sight2 = (S.sight & 4) ~= 0
    local dist = S.player_distance
    local angle = S.player_angle

    local dist_target = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)
    local engaged = false
    local blocked = truthy(hover_can_land(c))

    local step = c.state_step
    if step == 0 then
        engaged = truthy(target_orbit_point_air(c))
        dist_target = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)
        local angle_diff = angle_norm(angle - c.rot_y)

        if truthy(front_clear(c)) and c.move_speed > 6144 and
           (truthy(signed_range(angle_diff, 1365)) or ((dist < 20480) and truthy(signed_range(angle_diff, 682)))) then
            c.state_step = 4
        else
            c.air_screamer_timer = 8192 -- 2.0

            c.state_step = 1
        end
    elseif step == 1 then
        if blocked then
            c.state_step = 2
        elseif dist_target < 8192 or (c.air_screamer_flags & (1 << 31)) ~= 0 then
            engaged = truthy(target_orbit_point_air(c))
        elseif c.air_screamer_timer == 0 then
            c.state_step = 0
        end
    elseif step == 2 then
        if anim_status == 39 then
            switch_cond = 1
            c.anim_status = 54
            c.state_step = 3
        end
    elseif step == 3 then
        switch_cond = 1
    elseif step == 4 then
        if anim_status == 39 then
            switch_cond = 2
            c.anim_status = 36
            c.state_step = 5
        end
    elseif step == 5 then
        switch_cond = 2
    end

    flight_goal_fly_detour(c)

    local damage = damage_take(c, 4096)
    if damage == 0 then
        if switch_cond == 0 then
            if engaged or (((sight2 and dist < 61440) or dist < 16384) and dist_target < 6144) then
                c.control_state = 23
                c.state_step = 0
                set_attack_set(c, 3)
                c.air_screamer_flags = c.air_screamer_flags | (1 << 4)
            elseif dist > 122880 and dist_target < 4096 then
                c.control_state = 21
                c.state_step = 0
            end
        elseif switch_cond == 1 then
            if anim_status == 47 or anim_status == 51 then
                c.control_state = 14
                c.state_step = 0
            elseif truthy(near_ground(c)) then
                c.anim_status = 46
            end
        elseif switch_cond == 2 then
            if hover_kind == 3 then
                c.control_state = 42
                c.state_step = 0
            end
        end
    elseif damage == 1 then
        c.control_state = 29
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 2 then
        c.control_state = 30
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 32
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_hover_settle(c)
    if MAP0_S01 then return end

    target_air_ahead(c, 2048, c.rot_y)
    flight_goal_hover_target(c)
    c.flags = c.flags & ~(1 << 2)

    local damage = damage_take(c, 4096)
    if damage == 0 then
        local r = role(c)
        if r == 0 then
            c.control_state = 18
            c.state_step = 0
        elseif r == 1 or r == 3 then
            c.control_state = 19
            c.state_step = 0
        end
    elseif damage == 1 then
        c.control_state = 29
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        return
    elseif damage == 2 then
        c.control_state = 30
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 32
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_hover_recoil(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local done = 0
    local sight2 = (S.sight & 4) ~= 0
    local step = c.state_step
    c.flags = c.flags | (1 << 2)

    if step == 0 then
        if (anim_status & 1) ~= 0 then
            c.anim_status = 14
            c.state_step = 1
        end
    elseif step == 1 then
        if anim_status ~= 14 then
            c.state_step = 2
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    elseif step == 2 then
        if anim_status ~= 15 then
            done = 1
        end
    end

    flight_goal_hover_glance(c)

    local damage = damage_take(c, 2457) -- 0.6
    if damage == 0 or damage == 1 then
        if done ~= 0 then
            local bias = 0
            if side(c) ~= 0 then
                bias = 409 -- 0.1
            end

            if role(c) == 3 then
                call_reinforcement(c, c.spawn_index + 1, 2867) -- 0.7
            end

            if sight2 then
                if attack_set(c) == 5 then
                    c.control_state = 27
                    c.state_step = 0

                    if rand_q12() < (idiv(s32(c.health << 12), 1556480) + (bias * 2)) then
                        set_attack_set(c, 3)
                    end
                    return
                end

                c.control_state = 23
                c.state_step = 0

                if rand_q12() > (idiv(s32(c.health << 12), 1556480) + (bias * 2)) then
                    set_attack_set(c, 5)
                else
                    set_attack_set(c, 3)
                end
                return
            end

            local set = attack_set(c)
            if set == 3 then
                c.control_state = 26
                c.state_step = 0

                if rand_q12() < (2048 - (bias * 3)) then
                    set_attack_set(c, 2)
                end
            elseif set == 2 then
                c.control_state = 21
                c.state_step = 0

                if rand_q12() > (idiv(s32(c.health << 12), 1556480) + (bias * 2)) then
                    set_attack_set(c, 5)
                elseif rand_q12() < ((bias * 3) + 1228) then
                    set_attack_set(c, 3)
                end
            elseif set == 5 then
                c.control_state = 27
                c.state_step = 0

                if rand_q12() < idiv(s32(c.health << 12), 1556480) then
                    set_attack_set(c, 3)
                end
            elseif set == 1 or set == 4 then
                if rand_q12() >= ((bias * 5) + 819) then
                    c.control_state = 27
                else
                    c.control_state = 21
                end

                c.state_step = 0

                local next_set
                if rand_q12() < ((bias * 3) + 2048) then
                    next_set = 2
                else
                    next_set = 5
                end

                set_attack_set(c, next_set)
            else
                if role(c) == 3 then
                    bias = 4096
                end

                if rand_q12() >= ((bias * 3) + 1638) then
                    c.control_state = 27
                else
                    c.control_state = 21
                end

                c.state_step = 0

                local next_set
                if rand_q12() < ((bias * 2) + 2457) then
                    next_set = 2
                else
                    next_set = 5
                end

                set_attack_set(c, next_set)
            end
        end
    elseif damage == 2 then
        c.control_state = 30
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 32
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_hover_injured(c)
    if MAP0_S01 then return end

    local done = false
    local anim_status = c.anim_status
    c.flags = c.flags | (1 << 2)

    local step = c.state_step
    if step == 0 then
        if (anim_status & 1) ~= 0 then
            c.anim_status = 18
            c.state_step = 1
        end
    elseif step == 1 then
        if anim_status ~= 18 then
            c.state_step = 2
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    elseif step == 2 then
        if anim_status ~= 19 then
            done = true
        end
    end

    flight_goal_hover_still(c)

    local damage = damage_take(c, 2457) -- 0.6
    if damage == 0 or damage == 1 or damage == 2 then
        if done then
            c.control_state = 31
            c.state_step = 0
        end
    elseif damage == 3 or damage == 4 then
        c.control_state = 32
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_injured_flight(c)
    if MAP0_S01 then return end

    local step = c.state_step
    if step == 0 then
        c.air_screamer_timer = (rand_q12() * 2) + 2048

        target_air_ahead(c, 16384, c.rot_y + q12_mul(rand_q12() - 2048, 512))

        c.state_step = 1
    elseif step == 1 then
        if c.air_screamer_timer == 0 or
           distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z) < 8192 then
            c.state_step = 0
        end
    end

    flight_goal_slow_bank(c)

    local damage = damage_take(c, 2457) -- 0.6
    if damage == 0 then
        -- nothing
    elseif damage == 1 or damage == 2 then
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 32
        c.state_step = 0
        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

-- Air Screamer / Night Flutter control states 32..41 (glide, circle, dive, scratch, retreat).

function control_wall_bump_recover(c)
    if MAP0_S01 then return end

    local cond = false
    local anim_status = c.anim_status

    c.flags = c.flags | (1 << 2) -- CharaFlag_Hit

    local step = c.state_step
    if step == 0 then
        if (anim_status & 1) ~= 0 then
            if anim_status == 43 or anim_status == 19 then
                c.anim_status = 40
            else
                c.anim_status = 20
            end

            c.state_step = 1
        end
    elseif step == 1 then
        if anim_status ~= 20 and anim_status ~= 40 then
            c.state_step = 2
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    elseif step == 2 then
        if anim_status ~= 21 and anim_status ~= 41 then
            cond = true
        end
    end

    flight_goal_hover_still(c)
    damage_take(c, 2457) -- 0.6

    if cond then
        c.control_state = 2
        c.state_step = 0

        set_attack_set(c, 1)
    end
end

function control_glide_wander(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local switch_cond = 0
    local mode = flight_mode(c)
    local cond = false

    local sight0 = (S.sight & 1) ~= 0
    local sight1 = (S.sight & 2) ~= 0
    local sight2 = (S.sight & 4) ~= 0

    local landing = glide_landing_step(c)

    local step = c.state_step
    if step == 64 then
        respawn_air_glide(c)
        c.state_step = 1
    elseif step == 0 or step == 1 then
        if step == 0 then
            cond = true

            c.air_screamer_timer = 20480 -- 5.0
            target_air_ahead_glide(c, 24576, c.rot_y) -- 6.0
            target_air_around_glide(c, { x = c.air_screamer_target_x, y = c.air_screamer_target_y, z = c.air_screamer_target_z }, 20480) -- 5.0

            c.state_step = 1
        end

        local dist = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)
        local angle_diff = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z,
            c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z) - c.rot_y)

        if landing == 2 then
            c.state_step = 2
        elseif landing == 1 or (slope_under_body(c) and
                                cond == true and
                                (c.air_screamer_flags & 0x80000000) ~= 0 and
                                (dist < 8192 or signed_range(angle_diff, 682))) then -- 2.0, 60 deg
            c.state_step = 4
        elseif can_land_here(c) and
               cond == true and
               dist < 16384 and -- 4.0
               (angle_diff >= -341 and angle_diff <= 341) and -- +-30 deg
               rand_q12() < 2867 then -- 0.7
            c.state_step = 2
        elseif c.air_screamer_timer == 0 or dist < 12288 then -- 3.0
            c.state_step = 0
        end
    elseif step == 2 then
        if anim_status == 35 then
            switch_cond = 1
            c.anim_status = 32
            c.state_step = 3
        end
    elseif step == 3 then
        switch_cond = 1
    elseif step == 4 then
        if anim_status == 35 then
            switch_cond = 2
            c.anim_status = 30
            c.state_step = 5
        end
    elseif step == 5 then
        switch_cond = 2
    elseif step == 65 then
        respawn_from_area_edge(c)
        c.state_step = 1
    elseif step == 66 then
        respawn_behind_player(c)
        c.state_step = 1
    elseif step == 67 then
        respawn_around_player(c)
        c.state_step = 1
        c.air_screamer_flags = c.air_screamer_flags | (1 << 12)
    end

    flight_goal_glide_detour(c, 0, 1)

    local damage = damage_take(c, 4096) -- 1.0
    if damage == 0 then
        if switch_cond == 0 then
            if sight1 or sight0 or sight2 then
                c.control_state = 35
                c.state_step = 0
            end
        elseif switch_cond == 1 then
            if mode == 1 then
                if not (sight1 or sight0 or sight2) then
                    c.control_state = 4
                    c.state_step = 0
                else
                    c.control_state = 6
                    c.state_step = 0
                end
            end
        elseif switch_cond == 2 then
            if mode == 2 then
                if not (sight1 or sight0 or sight2) then
                    c.control_state = 18
                    c.state_step = 0
                else
                    c.control_state = 20
                    c.state_step = 0
                end
            end
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 44
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 45
        c.state_step = 0
        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_glide_roam(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local switch_cond = 0
    local mode = flight_mode(c)
    local cond = false
    local sight0 = (S.sight & 1) ~= 0
    local sight1 = (S.sight & 2) ~= 0
    local sight2 = (S.sight & 4) ~= 0
    local landing = glide_landing_step(c)

    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            cond = true
            c.air_screamer_timer = 20480 -- 5.0
            target_near_home_far(c)
            c.state_step = 1
        end

        local dist = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)
        local angle = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z,
            c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z) - c.rot_y)
        if landing == 2 then
            c.state_step = landing
        elseif landing == 1 or
               (slope_under_body(c) and cond == true and
                (c.air_screamer_flags & 0x80000000) ~= 0 and
                (dist < 8192 or signed_range(angle, 682))) then -- 2.0, 60 deg
            c.state_step = 4
        elseif can_land_here(c) and cond == true and dist < 16384 and -- 4.0
               (angle >= -341 and angle <= 341) and rand_q12() < 2867 then -- +-30 deg, 0.7
            c.state_step = 2
        elseif c.air_screamer_timer == 0 or dist < 8192 then -- 2.0
            c.state_step = 0
        end
    elseif step == 2 then
        if anim_status == 35 then
            switch_cond = 1
            c.anim_status = 32
            c.state_step = 3
        end
    elseif step == 3 then
        switch_cond = 1
    elseif step == 4 then
        if anim_status == 35 then
            switch_cond = 2
            c.anim_status = 30
            c.state_step = 5
        end
    elseif step == 5 then
        switch_cond = 2
    end

    flight_goal_glide_detour(c, 0, 1)
    local damage = damage_take(c, 4096) -- 1.0
    if damage == 0 then
        if switch_cond == 0 then
            if sight1 or sight0 or sight2 then
                c.control_state = 35
                c.state_step = 0
            end
        elseif switch_cond == 1 then
            if mode == switch_cond then
                c.control_state = (sight1 or sight0 or sight2) and 6 or 5
                c.state_step = 0
            end
        elseif switch_cond == 2 then
            if mode == switch_cond then
                c.control_state = (sight1 or sight0 or sight2) and 20 or 19
                c.state_step = 0
            end
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 44
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 45
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_glide_search(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local switch_cond = 0
    local mode = flight_mode(c)
    local lunge = S.lunge_reach

    local swoop_reach = (S.sight & 2) ~= 0
    local player_seen = (S.sight & 4) ~= 0
    local bite_reach = (S.sight & 1) ~= 0

    local landing = glide_landing_step(c)
    local step_started = 0

    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            c.air_screamer_timer = 24576 -- 6.0
        end

        target_air_around_glide(c, { x = c.air_screamer_aim_x, y = c.air_screamer_aim_y, z = c.air_screamer_aim_z }, 8192) -- 2.0

        if slope_under_body(c) and rand_q12() < 409 then -- 0.1
            c.state_step = 8
        else
            c.state_step = 2
        end
    elseif step == 2 or step == 3 then
        if step == 2 then
            step_started = 1
        end

        if landing == 2 then
            c.state_step = 4
        else
            if landing == 1 then
                c.state_step = 8
            elseif c.air_screamer_timer == 0 then
                if step_started ~= 0 then
                    switch_cond = 1
                end
                c.state_step = 0
            elseif distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z) < 4096 then -- 1.0
                c.state_step = 1
            elseif bite_reach or swoop_reach or player_seen then
                c.state_step = 3
                if lunge >= 33 then
                    local angle_delta = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z,
                        c.air_screamer_aim_x, c.air_screamer_aim_y, c.air_screamer_aim_z) - c.rot_y)
                    if angle_delta < 0 then
                        angle_delta = angle_delta + 3 -- 0.3 deg
                    end

                    target_air_ahead_glide(c, 8192, asr(angle_delta, 2) + c.rot_y) -- 2.0
                    c.air_screamer_timer = 24576 -- 6.0
                end
            end
        end
    elseif step == 4 then
        if anim_status == 35 then
            switch_cond = 2
            c.anim_status = 32

            if not player_seen and not swoop_reach then
                c.state_step = 6
            else
                c.state_step = 7
            end
        elseif player_seen or swoop_reach then
            c.state_step = 5
        end
    elseif step == 5 then
        if anim_status == 35 then
            switch_cond = 3
            c.anim_status = 32
            c.state_step = 7
        end
    elseif step == 6 then
        if player_seen or swoop_reach then
            switch_cond = 3
            c.state_step = 7
        else
            switch_cond = 2
        end
    elseif step == 7 then
        switch_cond = 3
    elseif step == 8 then
        if anim_status == 35 then
            switch_cond = 4
            c.anim_status = 30

            if not player_seen and not swoop_reach then
                c.state_step = 10
            else
                c.state_step = 11
            end
        elseif player_seen or swoop_reach then
            c.state_step = 9
        end
    elseif step == 9 then
        if anim_status == 35 then
            switch_cond = 5
            c.anim_status = 30
            c.state_step = 11
        end
    elseif step == 10 then
        if player_seen or swoop_reach then
            switch_cond = 5
            c.state_step = 11
        else
            switch_cond = 4
        end
    elseif step == 11 then
        switch_cond = 5
    end

    flight_goal_glide_detour(c, 0, 2)

    local damage = damage_take(c, 4096) -- 1.0
    if damage == 0 then
        if switch_cond == 0 or switch_cond == 1 then
            if not player_seen and not swoop_reach then
                if switch_cond == 1 then
                    local roll = rand_q12()
                    local attack_bias
                    if attack_set(c) == switch_cond then
                        attack_bias = 409 -- 0.1
                    else
                        attack_bias = 0
                    end

                    if roll < (2867 - (attack_bias * 2)) then -- 0.7
                        c.control_state = 43
                        c.state_step = 0
                    end
                end
            else
                c.control_state = 36
                c.state_step = 0
            end
        elseif switch_cond == 2 then
            if mode == 1 then
                c.control_state = 6
                c.state_step = 0
            end
        elseif switch_cond == 3 then
            if mode == 1 then
                c.control_state = 7
                c.state_step = 0
            end
        elseif switch_cond == 4 then
            if mode == 2 then
                c.control_state = 20
                c.state_step = 0
            end
        elseif switch_cond == 5 then
            if mode == 2 then
                c.control_state = 21
                c.state_step = 0
            end
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 44
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 45
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_glide_approach(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local switch_cond = 0
    local mode = flight_mode(c)
    local lunge = S.lunge_reach
    local sight0 = (S.sight & 1) ~= 0
    local sight1 = (S.sight & 2) ~= 0
    local sight2 = (S.sight & 4) ~= 0

    local cond = false
    local landing = glide_landing_step(c)

    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            if slope_under_body(c) and rand_q12() < 409 then -- 0.1
                c.state_step = 8
            else
                c.state_step = 2
            end
        end

        -- Falls through from step 0: the step chosen above is always overwritten.
        c.air_screamer_timer = 24576 -- 6.0
        target_air_around_glide(c, { x = c.air_screamer_aim_x, y = c.air_screamer_aim_y, z = c.air_screamer_aim_z }, 12288) -- 3.0
        c.state_step = 2
    elseif step == 2 or step == 3 then
        if step == 2 then
            cond = true
        end

        if landing == 2 then
            c.state_step = 4
        elseif landing == 1 then
            c.state_step = 8
        elseif distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z) < 4096 then -- 1.0
            if cond then
                switch_cond = 1
            end

            c.state_step = 0
        elseif c.air_screamer_timer == 0 then
            c.state_step = 1
        elseif sight2 or sight0 or sight1 then
            c.state_step = 3

            if lunge > 32 then
                local angle_diff = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z,
                    c.air_screamer_aim_x, c.air_screamer_aim_y, c.air_screamer_aim_z) - c.rot_y)
                target_air_ahead_glide(c, 12288, idiv(angle_diff, 4) + c.rot_y) -- 3.0
                c.air_screamer_timer = 24576 -- 6.0
            end
        end
    elseif step == 4 then
        if anim_status == 35 then
            switch_cond = 2
            c.anim_status = 32

            if not sight1 and not sight0 then
                c.state_step = 6
            else
                c.state_step = 7
            end
        elseif sight2 or sight1 or sight0 then
            c.state_step = 5
        end
    elseif step == 5 then
        if anim_status == 35 then
            switch_cond = 3
            c.anim_status = 32
            c.state_step = 7
        end
    elseif step == 6 then
        switch_cond = 3

        if sight2 or sight1 or sight0 then
            c.state_step = 7
        else
            switch_cond = 2
        end
    elseif step == 7 then
        switch_cond = 3
    elseif step == 8 then
        if anim_status == 35 then
            switch_cond = 4
            c.anim_status = 30

            if not sight1 and not sight0 then
                c.state_step = 10
            else
                c.state_step = 11
            end
        elseif sight2 or sight1 or sight0 then
            c.state_step = 9
        end
    elseif step == 9 then
        if anim_status == 35 then
            switch_cond = 5
            c.anim_status = 30
            c.state_step = 11
        end
    elseif step == 10 then
        switch_cond = 5

        if sight2 or sight1 or sight0 then
            c.state_step = 11
        else
            switch_cond = 4
        end
    elseif step == 11 then
        switch_cond = 5
    end

    flight_goal_glide_detour(c, 1, 1)

    local damage = damage_take(c, 4096) -- 1.0
    if damage == 0 then
        if switch_cond == 0 or switch_cond == 1 then
            if sight2 then
                c.control_state = 37
                c.state_step = 0
            elseif switch_cond == 1 then
                local rand = rand_q12()
                local attack_bias = 0

                if attack_set(c) == 1 then
                    attack_bias = 409 -- 0.1
                end

                if rand < (2867 - (attack_bias * 2)) then -- 0.7
                    c.control_state = 35
                    c.state_step = 0
                end
            end
        elseif switch_cond == 2 then
            if mode == 1 then
                if sight2 then
                    c.control_state = 8
                    c.state_step = 0
                else
                    c.control_state = 6
                    c.state_step = 0
                end
            end
        elseif switch_cond == 3 then
            if mode == 1 then
                if sight2 then
                    c.control_state = 8
                    c.state_step = 0
                else
                    c.control_state = 7
                    c.state_step = 0
                end
            end
        elseif switch_cond == 4 then
            if mode == 2 then
                if sight2 then
                    c.control_state = 22
                    c.state_step = 0
                else
                    c.control_state = 20
                    c.state_step = 0
                end
            end
        elseif switch_cond == 5 then
            if mode == 2 then
                if sight2 then
                    c.control_state = 22
                    c.state_step = 0
                else
                    c.control_state = 21
                    c.state_step = 0
                end
            end
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 44
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 45
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_glide_attack_run(c)
    if MAP0_S01 then return end

    local switch_cond = 0
    local anim_status = c.anim_status
    local mode = flight_mode(c)
    local dist = S.player_distance
    local angle = S.player_angle
    local sight2 = (S.sight & 4) ~= 0
    local landing = glide_landing_step(c)

    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            switch_cond = 1
            c.air_screamer_timer = 8192 -- 2.0
            c.flags = c.flags | (1 << 2) -- CharaFlag_Hit
        end

        target_air_around_glide(c, { x = c.air_screamer_aim_x, y = c.air_screamer_aim_y, z = c.air_screamer_aim_z }, 16384) -- 4.0
        c.state_step = 2
    elseif step == 2 then
        if landing == 2 then
            c.state_step = 3
        elseif landing == 1 then
            c.state_step = 5
        elseif c.air_screamer_timer == 0 then
            c.state_step = 0
        elseif distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z) < 4096 then -- 1.0
            c.state_step = 1
        end
    elseif step == 3 then
        if anim_status == 35 then
            switch_cond = 2
            c.anim_status = 32
            c.state_step = 4
        end
    elseif step == 4 then
        switch_cond = 2
    elseif step == 5 then
        if anim_status == 35 then
            switch_cond = 3
            c.anim_status = 30
            c.state_step = 6
        end
    elseif step == 6 then
        switch_cond = 3
    end

    flight_goal_glide_detour(c, 2, 0)

    local damage = damage_take(c, 4096) -- 1.0
    if damage == 0 then
        if switch_cond == 0 then
            if not sight2 then
                c.control_state = 36
                c.state_step = 0
            end
        elseif switch_cond == 2 then
            if mode == 1 then
                c.control_state = 8
                c.state_step = 0
            end
        elseif switch_cond == 3 then
            if mode == 2 then
                c.control_state = 22
                c.state_step = 0
            end
        elseif switch_cond == 1 then
            local chance0 = attack_chance(c, dist)

            local chance1 = retreat_chance(c, dist)
            if angle_norm(angle - c.rot_y) >= -256 and -- -22.5 deg
               angle_norm(angle - c.rot_y) < 256 and -- 22.5 deg
               dist > 16384 and dist < 32768 then -- 4.0, 8.0
                chance0 = chance0 + 819 -- 0.2
            end

            if rand_q12() < chance0 then
                if role(c) == 3 then
                    call_reinforcement(c, c.spawn_index + 1, 2457) -- 0.6
                end

                c.control_state = 38
                c.state_step = 0
                c.air_screamer_flags = c.air_screamer_flags | (1 << 4)
            elseif rand_q12() < chance1 then
                c.control_state = 42
                c.state_step = 0
                c.air_screamer_flags = c.air_screamer_flags | (1 << 4)
            end
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 44
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 45
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_glide_dive(c)
    if MAP0_S01 then return end

    local anim = c.anim_status
    local switch_cond = 0

    local mode = flight_mode(c)

    local player_seen = (S.sight & 4) ~= 0
    local player_dist = S.player_distance

    local state_step = glide_landing_step(c)
    local intercept = target_intercept_player(c)

    local step = c.state_step
    if step == 0 then
        local rolled_dist = s32(player_dist + (rand_q12() * 4))

        if not slope_under_body(c) or rolled_dist >= 12288 or rand_q12() >= 0x333 then -- 3.0
            c.state_step = 1
        else
            c.state_step = 4
        end
        c.air_screamer_timer = 16384 -- 4.0
    elseif step == 1 then
        local height_diff = s32(c.air_screamer_target_y - c.pos_y)
        local target_dist = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)
        local turn_to_target = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z,
            c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z) - c.rot_y)

        if state_step == 2 then
            c.state_step = state_step
        elseif state_step == 1 then
            c.state_step = 4
        elseif (height_diff >= -0x333 and height_diff <= 0x333) and
               (intercept > 0x599 and intercept < 0x8CD) and
               (turn_to_target >= -0x80 and turn_to_target <= 0x80) then
            switch_cond = 1
        else
            if signed_range(turn_to_target, 0x400) and target_dist < 16384 then -- 4.0
                switch_cond = 2
            elseif c.air_screamer_timer == 0 then
                c.state_step = 0
            end
        end
    elseif step == 2 then
        if anim == 35 then
            switch_cond = 3
            c.anim_status = 32
            c.state_step = 3
        end
    elseif step == 3 then
        switch_cond = 3
    elseif step == 4 then
        if anim == 35 then
            switch_cond = 4
            c.anim_status = 30
            c.state_step = 5
        end
    elseif step == 5 then
        switch_cond = 4
    end

    flight_goal_glide_detour(c, 2, 0)

    local damage = damage_take(c, 4096) -- 1.0
    if damage == 0 then
        if switch_cond == 0 or switch_cond == 2 then
            local next_state
            local stay = false
            if not far_from_home(c) then
                next_state = 41
                if player_seen then
                    if switch_cond == 2 then
                        c.control_state = 39
                        c.state_step = 0
                    end
                    stay = true
                end
            else
                next_state = 42
            end

            if not stay then
                c.control_state = next_state
                c.state_step = 0
            end
        elseif switch_cond == 1 then
            if (P.flags & (1 << 3)) == 0 and -- CharaFlag_Unk4
               (event_npc(0) == -1 and event_npc(1) == -1) and
               anim == 0x23 then
                c.control_state = 40
                c.state_step = 0
            end
        elseif switch_cond == 3 then
            if mode == 1 then
                target_player_edge(c)
                c.control_state = 10
                c.state_step = 0
            end
        elseif switch_cond == 4 then
            if mode == 2 then
                c.control_state = 23
                c.state_step = 0
            end
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 44
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 45
        c.state_step = 0
        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_glide_pull_away(c)
    if MAP0_S01 then return end

    local anim = c.anim_status
    local switch_cond = 0
    local mode = flight_mode(c)
    local player_seen = (S.sight & 4) ~= 0

    local player_dist = S.player_distance
    local harry_angle = S.player_angle
    local start_mode = mode
    local glide_dist = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)
    local glide_heading = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z,
        c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z) - c.rot_y)
    local landing = glide_landing_step(c)

    local step = c.state_step
    if step == 0 then
        target_orbit_point_glide(c)
        distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)

        if player_dist > 24576 then -- 6.0
            glide_dist = 24576
        elseif player_dist > 8192 then -- 2.0
            glide_dist = 49152 - player_dist -- 12.0
        else
            glide_dist = 40960 -- 10.0
        end

        glide_dist = s32(glide_dist + rand_q12() * 2)
        glide_heading = idiv(angle_norm((harry_angle + 0x800) - c.rot_y), 2) + c.rot_y

        target_air_ahead_glide(c, glide_dist, glide_heading + idiv(rand_q12() - 0x800, 8))
        c.air_screamer_timer = 20480 -- 5.0
        c.state_step = 1
    elseif step == 1 then
        if landing == 2 then
            c.state_step = 2
        elseif landing == 1 then
            c.state_step = 4
        elseif glide_dist < 8192 or signed_range(glide_heading, 0x400) or -- 2.0
               c.air_screamer_timer == 0 then
            switch_cond = 1
        end
    elseif step == 2 then
        if anim == 35 then
            switch_cond = 2
            c.anim_status = 32
            c.state_step = 3
        end
    elseif step == 3 then
        switch_cond = 2
    elseif step == 4 then
        if anim == 35 then
            switch_cond = 3
            c.anim_status = 30
            c.state_step = 5
        end
    elseif step == 5 then
        switch_cond = 3
    end

    flight_goal_glide_detour(c, 2, 0)

    local damage = damage_take(c, 4096) -- 1.0
    if damage == 0 then
        if switch_cond == 0 or switch_cond == 1 then
            if player_seen and player_dist > 28672 then -- 7.0
                c.control_state = 38
                c.state_step = 0
            elseif switch_cond == 1 then
                if attack_set(c) ~= 3 then
                    set_attack_set(c, 2)
                end
                c.control_state = 41
                c.state_step = 0
            end
        elseif switch_cond == 2 then
            if start_mode == 1 then
                c.control_state = 13
                c.state_step = 0
            end
        elseif switch_cond == 3 then
            if start_mode == 2 then
                c.control_state = 26
                c.state_step = 0
            end
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 44
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 45
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_glide_scratch(c)
    if MAP0_S01 then return end

    local cond0 = false
    local cond1 = false

    local anim_status = c.anim_status
    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            c.state_step = 1
        end

        if anim_status == 35 then
            c.state_step = 2
            c.anim_status = 6
        end
    elseif step == 2 then
        if anim_status == 7 and c.anim_keyframe < 8192 then
            c.state_step = 3
            c.air_screamer_flags = c.air_screamer_flags | (1 << 5)
        end
    elseif step == 3 then
        if anim_status == 35 then
            cond0 = true
        elseif c.attack_hit == -1 then
            cond0 = true
            cond1 = true
            c.state_step = 4
        end
    elseif step == 4 then
        if anim_status == 35 then
            cond0 = true
            cond1 = true
        end
    end

    target_intercept_player(c)
    flight_goal_glide_target(c)

    local damage = damage_take(c, 4915) -- 1.2
    if damage == 0 then
        if cond0 then
            if attack_set(c) == 5 and cond1 then
                set_attack_set(c, 1)
                c.control_state = 42
            else
                c.control_state = 39
            end

            c.state_step = 0
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 44
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 45
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_glide_retreat(c)
    if MAP0_S01 then return end

    local anim_status = c.anim_status
    local switch_cond = 0

    local mode = flight_mode(c)
    local sight2 = (S.sight & 4) ~= 0
    local landing = glide_landing_step(c)

    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            target_aim_air_glide(c)
        end

        c.air_screamer_timer = 16384 -- 4.0
        c.state_step = 2
    elseif step == 2 then
        local dist = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)
        local delta_angle = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z,
            c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z) - c.rot_y)

        if landing == 2 then
            c.state_step = 3
        elseif landing == 1 then
            c.state_step = 5
        elseif dist < 2048 then -- 0.5
            if slope_under_body(c) and rand_q12() < 1228 then -- 0.3
                c.state_step = 5
            else
                switch_cond = 1
            end
        elseif dist < 16384 and signed_range(delta_angle, 1024) then -- 4.0, 90 deg
            switch_cond = 2
        end

        if c.air_screamer_timer == 0 then
            target_air_around_glide(c, { x = c.air_screamer_aim_x, y = c.air_screamer_aim_y, z = c.air_screamer_aim_z }, 10240) -- 2.5
            c.state_step = 1
        end
    elseif step == 3 then
        if anim_status == 35 then
            switch_cond = 3
            c.anim_status = 32
            c.state_step = 4
        end
    elseif step == 4 then
        switch_cond = 3
    elseif step == 5 then
        if anim_status == 35 then
            switch_cond = 4
            c.anim_status = 30
            c.state_step = 6
        end
    elseif step == 6 then
        switch_cond = 4
    end

    flight_goal_glide_detour(c, 1, 0)

    local damage = damage_take(c, 4096) -- 1.0
    if damage == 0 then
        if switch_cond == 0 or switch_cond == 1 or switch_cond == 2 then
            local next_state = nil
            if far_from_home(c) then
                next_state = 42
            elseif sight2 then
                next_state = 38
            elseif switch_cond == 2 then
                next_state = 39
            elseif switch_cond == 1 then
                next_state = 36
            end

            if next_state ~= nil then
                c.control_state = next_state
                c.state_step = 0
            end
        elseif switch_cond == 3 then
            if mode == 1 then
                if not sight2 then
                    c.control_state = 13
                else
                    c.control_state = 10
                end
                c.state_step = 0
            end
        elseif switch_cond == 4 then
            if mode == 2 then
                if sight2 then
                    c.control_state = 23
                else
                    c.control_state = 26
                end
                c.state_step = 0
            end
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 44
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 45
        c.state_step = 0

        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

-- glide states 42-45, the map0_s01 hover states 46-51 and the
-- target pickers (respawn placement, orbit/retreat points, interception of Harry).

-- idiv(s32(health << 12), 1556480) is health / 380.0 in Q12; the shift wraps in 32 bits first.

-- C bool here is a 32-bit unsigned int: a callee may hand back any integer, and 0 must read as false.
local function truthy(v)
    return v ~= nil and v ~= false and v ~= 0
end

function control_glide_to_target(c)
    if MAP0_S01 then return end

    local anim = c.anim_status
    local switch_cond = 0
    local flight = flight_mode(c)
    local dist = S.player_distance
    local angle = S.player_angle
    local sight2 = (S.sight & 4) ~= 0
    local mode = flight
    local target_dist = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)
    local landing = glide_landing_step(c)
    local orbit_done = false

    local step = c.state_step
    if step == 0 then
        orbit_done = target_orbit_point_glide(c)
        target_dist = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)
        local angle_delta = angle_norm(angle - c.rot_y)
        if not truthy(slope_under_body(c)) or (angle_delta < -341 or angle_delta >= 341) then
            c.air_screamer_timer = 8192 -- 2.0
            c.state_step = 1
        else
            c.state_step = 4
        end
    elseif step == 1 then
        if landing == 2 then
            c.state_step = 2
        elseif landing == 1 then
            c.state_step = 4
        elseif target_dist < 8192 or (c.air_screamer_flags & 0x80000000) ~= 0 then
            orbit_done = target_orbit_point_glide(c)
        elseif c.air_screamer_timer == 0 then
            c.state_step = 0
        end
    elseif step == 2 then
        if anim == 35 then -- Glide, active
            switch_cond = 1
            c.anim_status = 32 -- GlideToWalkForward, blend
            c.state_step = 3
        end
    elseif step == 3 then
        switch_cond = 1
    elseif step == 4 then
        if anim == 35 then -- Glide, active
            switch_cond = 2
            c.anim_status = 30 -- GlideToHover, blend
            c.state_step = 5
        end
    elseif step == 5 then
        switch_cond = 2
    end

    flight_goal_glide_detour(c, 2, 0)

    local damage = damage_take(c, 4096)
    if damage == 0 then
        if switch_cond == 0 then
            if truthy(orbit_done) or (((sight2 and dist < 61440) or dist < 16384) and target_dist < 6144) then
                c.control_state = 38
                c.state_step = 0
                set_attack_set(c, 3)
                c.air_screamer_flags = c.air_screamer_flags | (1 << 4)
            elseif dist > 122880 and target_dist < 4096 then
                c.control_state = 36
                c.state_step = 0
            end
            return
        elseif switch_cond == 1 then
            if mode == 1 then
                c.control_state = 14
                c.state_step = 0
            end
        elseif switch_cond == 2 then
            if mode == 2 then
                c.control_state = 27
                c.state_step = 0
            end
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 44
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 45
        c.state_step = 0
        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_glide_level_off(c)
    if MAP0_S01 then return end

    target_air_ahead_glide(c, 2048, c.rot_y)
    flight_goal_glide_detour(c, 1, 1)
    c.flags = c.flags & ~4 -- CharaFlag_Hit

    local damage = damage_take(c, 4096)
    if damage == 0 then
        local r = role(c)
        if r == 0 then
            c.control_state = 33 -- Glide
            c.state_step = 0
        elseif r == 1 or r == 3 then
            c.control_state = 34
            c.state_step = 0
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 44
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 45
        c.state_step = 0
        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_glide_recoil(c)
    if MAP0_S01 then return end

    local anim = c.anim_status
    local sight2 = (S.sight & 4) ~= 0
    c.flags = c.flags | 4 -- CharaFlag_Hit
    local cond = false

    local step = c.state_step
    if step == 0 then
        if (anim & 1) ~= 0 then
            c.anim_status = 16 -- GlideRecoil, blend
            c.state_step = 1
        end
    elseif step == 1 then
        if anim ~= 16 then
            c.state_step = 2
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    elseif step == 2 then
        if anim ~= 17 then
            cond = true
        end
    end

    flight_goal_glide_glance(c)

    local damage = damage_take(c, 2457)
    if damage == 0 or damage == 1 or damage == 2 then
        if not cond then
            return
        end

        local side_bias = (side(c) ~= 0) and 409 or 0

        if role(c) == 3 then
            call_reinforcement(c, c.spawn_index + 1, 2867)
        end

        if sight2 then
            if attack_set(c) == 5 then
                c.control_state = 42
                c.state_step = 0
                if rand_q12() < idiv(s32(c.health << 12), 1556480) + side_bias * 2 then
                    set_attack_set(c, 3)
                end
            else
                c.control_state = 38
                c.state_step = 0
                if rand_q12() <= idiv(s32(c.health << 12), 1556480) + side_bias * 2 then
                    set_attack_set(c, 3)
                else
                    set_attack_set(c, 5)
                end
            end
            return
        end

        local set = attack_set(c)
        if set == 3 then
            c.control_state = 41
            c.state_step = 0
            if rand_q12() < 2048 - side_bias * 3 then
                set_attack_set(c, 2)
            end
        elseif set == 2 then
            c.control_state = 36
            c.state_step = 0
            if rand_q12() > idiv(s32(c.health << 12), 1556480) + side_bias * 2 then
                set_attack_set(c, 5)
            elseif rand_q12() < 1228 + side_bias * 3 then
                set_attack_set(c, 3)
            end
        elseif set == 5 then
            c.control_state = 42
            c.state_step = 0
            if rand_q12() < idiv(s32(c.health << 12), 1556480) then
                set_attack_set(c, 3)
            end
        elseif set == 1 or set == 4 then
            if rand_q12() < 819 + side_bias * 5 then
                c.control_state = 36
            else
                c.control_state = 42
            end
            c.state_step = 0
            local next_set
            if rand_q12() < 2048 + side_bias * 3 then
                next_set = 2
            else
                next_set = 5
            end
            set_attack_set(c, next_set)
        else
            if role(c) == 3 then
                side_bias = 4096
            end
            if rand_q12() < 1638 + side_bias * 3 then
                c.control_state = 36
            else
                c.control_state = 42
            end
            c.state_step = 0
            local next_set
            if rand_q12() < 2457 + side_bias * 2 then
                next_set = 2
            else
                next_set = 5
            end
            set_attack_set(c, next_set)
        end
    elseif damage == 3 or damage == 4 then
        c.control_state = 45
        c.state_step = 0
        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_glide_stun(c)
    if MAP0_S01 then return end

    local cond = false
    local anim = c.anim_status
    c.flags = c.flags | 4 -- CharaFlag_Hit

    local step = c.state_step
    if step == 0 then
        if (anim & 1) ~= 0 then
            c.anim_status = 22 -- GlideToStun, blend
            c.state_step = 1
        end
    elseif step == 1 then
        if anim ~= 22 then
            c.state_step = 2
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    elseif step == 2 then
        if anim ~= 23 then
            cond = true
        end
    end

    if flight_mode(c) == 3 then
        flight_goal_glide_still(c)
    else
        flight_goal_hold(c)
    end

    damage_take(c, 2457)

    if cond then
        c.control_state = 2
        c.state_step = 0
        set_attack_set(c, 1)
    end
end

function control_cafe_hover_bite(c)
    if not MAP0_S01 then return end

    local anim = c.anim_status

    if c.state_step ~= 1 then
        if c.state_step == 0 and anim == 5 and c.anim_keyframe < 8204 then -- HoverBiteAttack, active
            c.air_screamer_timer = 16384 -- 4.0
            c.state_step = 1
            c.air_screamer_flags = c.air_screamer_flags | (1 << 5)
            c.flags = c.flags | 4 -- CharaFlag_Hit
        end
    elseif ((asr(anim, 1) << 1) | 1) == 39 then -- HoverVariable, either phase
        c.state_step = 2
        c.air_screamer_timer = 16384 -- 4.0
    end

    target_air_ahead(c, 4096, angles_between(c.pos_x, c.pos_y, c.pos_z, P.pos_x, P.pos_y, P.pos_z))
    flight_goal_hover_target(c)

    local damage = damage_take(c, 4096)
    if damage == 0 then
        if c.air_screamer_timer == 0 or
            (c.slot - 1) == sh.entity.sys("target_npc") or
            distance_2d_q12(c.pos_x, c.pos_z, P.pos_x, P.pos_z) > 26624 then
            c.control_state = 47
            c.state_step = 0
            c.air_screamer_flags = c.air_screamer_flags | (1 << 4)
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 50
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 51
        c.state_step = 0
        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_hover_circle(c)
    if not MAP0_S01 then return end

    local step = c.state_step
    if step == 0 then
        c.air_screamer_timer = 6144 -- 1.5
        c.state_step = 1
    elseif step == 1 then
        if distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z) < 2048 then
            c.state_step = 0
        end
    end

    target_player_flank(c)
    flight_goal_fly_detour(c)

    local damage = damage_take(c, 4096)
    if damage == 0 then
        local dist0 = -1
        local delta_y = c.air_screamer_target_y - c.pos_y
        if delta_y > -819 and delta_y < 3276 then
            dist0 = c.cylinder_radius + 614
        end

        local dist = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_target_x, c.air_screamer_target_z)
        if dist < dist0 then
            local angle_delta = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z,
                c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z) - c.rot_y)
            if angle_delta >= -91 and angle_delta < 91 then
                c.control_state = 49
                c.state_step = 0
            end
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 50
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3) -- PlayerFlag_WallStopRight
    elseif damage == 3 or damage == 4 then
        c.control_state = 51
        c.state_step = 0
        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6) -- PlayerFlag_Unk6
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3) -- PlayerFlag_WallStopRight
        end
    end
end

function control_hover_hold(c)
    if not MAP0_S01 then return end

    if c.state_step == 0 then
        c.air_screamer_timer = 8192 -- 2.0
        c.state_step = 1
    end

    target_player_flank(c)
    flight_goal_fly_target(c)

    local damage = damage_take(c, 4096)
    if damage == 0 then
        if c.air_screamer_timer == 0 or
            distance_2d_q12(c.pos_x, c.pos_z, P.pos_x, P.pos_z) > 8192 then
            c.control_state = 47
            c.state_step = 0
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 50
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3) -- PlayerFlag_WallStopRight
    elseif damage == 3 or damage == 4 then
        c.control_state = 51
        c.state_step = 0
        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6) -- PlayerFlag_Unk6
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3) -- PlayerFlag_WallStopRight
        end
    end
end

function control_hover_dive_attack(c)
    if not MAP0_S01 then return end

    local cond0 = false
    local anim = c.anim_status
    local cond1 = false

    local step = c.state_step
    if step == 0 or step == 1 then
        if step == 0 then
            c.state_step = 1
        end
        if anim == 39 then -- HoverVariable, active
            c.state_step = 2
            -- The bare anim index of StandToStun is stored as a status: 4 = HoverBiteAttack, blend.
            c.anim_status = 4
        end
    elseif step == 2 then
        if anim == 5 and c.anim_keyframe < 8192 then -- HoverBiteAttack, active
            c.state_step = 3
            c.air_screamer_flags = c.air_screamer_flags | (1 << 5) -- PlayerFlag_Unk5
        end
    elseif step == 3 then
        if anim == 39 then
            cond0 = true
        elseif c.attack_hit == -1 then
            cond0 = true
            cond1 = true
            c.state_step = 4
        end
    elseif step == 4 then
        if anim == 39 then
            cond0 = true
            cond1 = true
        end
    end

    target_player_flank(c)
    flight_goal_hover_bank(c)

    local damage = damage_take(c, 4915)
    if damage == 0 then
        if cond0 then
            if not cond1 then
                if rand_q12() >= 2048 then
                    c.control_state = 47
                else
                    c.control_state = 48
                end
            else
                c.control_state = 48
            end
            c.state_step = 0
        end
    elseif damage == 1 or damage == 2 then
        c.control_state = 50
        c.state_step = 0
        c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
    elseif damage == 3 or damage == 4 then
        c.control_state = 51
        c.state_step = 0
        if c.health <= 0 then
            c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
        else
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    end
end

function control_cafe_hover_recoil(c)
    if not MAP0_S01 then return end

    local step = c.state_step
    local anim = c.anim_status
    local cond = false

    if step == 0 then
        if (anim & 1) ~= 0 then
            c.anim_status = 14 -- HoverRecoil, blend
            c.state_step = 1
        end
    elseif step == 1 then
        if anim ~= 14 then
            c.state_step = 2
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    elseif step == 2 then
        if anim ~= 15 then
            cond = true
        end
    end

    flight_goal_hover_glance(c)

    local damage = damage_take(c, 2457)
    if damage >= 0 then
        if damage < 3 then
            if cond then
                c.control_state = 47
                c.state_step = 0
                set_attack_set(c, 3)
            end
        elseif damage < 5 then
            c.control_state = 51
            c.state_step = 0
            if c.health <= 0 then
                c.air_screamer_flags = c.air_screamer_flags | (1 << 6)
            else
                c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
            end
        end
    end
end

function control_hover_stun(c)
    if not MAP0_S01 then return end

    local step = c.state_step
    local anim = c.anim_status
    local cond = false

    if step == 0 then
        if (anim & 1) ~= 0 then
            c.anim_status = 20 -- HoverToStun, blend
            c.state_step = 1
        end
    elseif step == 1 then
        if anim ~= 20 then
            c.state_step = 2
            c.air_screamer_flags = c.air_screamer_flags | (1 << 3)
        end
    elseif step == 2 then
        if anim ~= 21 then
            cond = true
        end
    end

    flight_goal_hover_still(c)
    damage_take(c, 2457)

    if cond then
        c.control_state = 2
        c.state_step = 0
        set_attack_set(c, 1)
    end
end

-- Target position of the record as a vector table (read-only snapshot for the ray helpers).
local function target_vec(c)
    return { x = c.air_screamer_target_x, y = c.air_screamer_target_y, z = c.air_screamer_target_z }
end

function call_reinforcement(c, npc_slot, spawn_chance)
    -- Reinforcements come from the map's spawn rows (fallback: none, chara/fallbacks.lua).
    if not map_data(c, "reinforcements", has_spawn_row) then
        return
    end
    local bits_set = 0
    local flags = npc_mask()
    local counter = 0

    if spawn_chance < rand_q12() then
        return
    end

    if vblanks() >= 4 then
        return
    end

    -- Walks all 32 mask bits; only set bits touch a slot.
    for i = 0, 31 do
        if (flags & (1 << i)) ~= 0 then
            bits_set = bits_set + 1
            local other = record(i + 1)
            if other.chara_id == c.chara_id then
                local st = other.control_state
                if st == 2 or st == 17 or st == 30 or st == 32 or st == 45 then
                    counter = counter + 2
                else
                    counter = counter + 3
                end
            end
        end
    end

    if bits_set < 32 then
        counter = idiv(counter + 2, 3)
        if counter < 5 then
            local slot = npc_slot & 0x1F
            chara_spawn(c.chara_id, slot, P.pos_x + 81920, P.pos_z, 0, 10)
        end
    end
end

function place_on_ring_behind(c, height_offset, side_flag)
    local new_x = c.pos_x
    local new_z = c.pos_z
    local player_x = P.pos_x
    local player_z = P.pos_z

    local delta = angle_norm(c.rot_y - sh.math.atan2(new_x - player_x, new_z - player_z))
    local heading = c.rot_y + 2048
    local angle_step = (delta > 0) and 1 or 0
    angle_step = (side_flag ~= angle_step) and 512 or -512

    c.air_screamer_target_x = player_x
    c.air_screamer_target_z = player_z

    local i = 8
    while i > 0 do
        local dist = q12_mul(rand_q12(), 24576) + 77824
        new_x = player_x + q12_mul(dist, sh.math.sin(heading))
        new_z = player_z + q12_mul(dist, sh.math.cos(heading))

        local gh = ground_height(new_x, new_z)
        gh = min(gh, 0)

        c.pos_y = gh + height_offset
        c.pos_x = new_x
        c.pos_z = new_z
        c.air_screamer_target_y = c.pos_y

        if in_active_cell(new_x, new_z) and ground_kind(new_x, new_z) ~= 0 and
            truthy(path_clear(c, nil, target_vec(c), nil)) then
            break
        end

        i = i - 1
        heading = heading + angle_step
    end

    if i == 0 then
        c.pos_x = player_x + 204800
        c.pos_y = ceiling_height() * 2
        c.pos_z = player_z + 204800
    end
end

function respawn_ground_ahead(c)
    place_on_ring_behind(c, 0, 0)
    target_ground_ahead(c, 122880, c.rot_y)
    c.air_screamer_timer = 61440 -- 15.0
end

function respawn_air_ahead(c)
    place_on_ring_behind(c, -9011, 0)
    target_air_ahead(c, 122880, c.rot_y)
    c.air_screamer_timer = 49152 -- 12.0
end

function respawn_air_glide(c)
    place_on_ring_behind(c, -9011, 1)
    target_air_ahead_glide(c, 122880, c.rot_y)

    c.move_speed = FLIGHT_TABLE[18]
    c.air_screamer_timer = 40960 -- 10.0
end

function respawn_behind_player(c)
    local pos_x = P.pos_x
    local pos_y = P.pos_y
    local pos_z = P.pos_z
    local rot_y = P.rot_y

    local angle_step
    if rand_q12() < 2048 then
        angle_step = 256
    else
        angle_step = -256
    end

    local new_angle = rand_q12() - 2048
    new_angle = asr(new_angle, 5)
    new_angle = new_angle + 2048

    local cur_angle = rot_y + new_angle
    local i = 0
    while i < 16 do
        -- Only points behind Harry are probed.
        local skip = angle_norm(rot_y - cur_angle) >= -1024 and angle_norm(rot_y - cur_angle) < 1024
        if not skip then
            local cur_x = pos_x + q12_mul(sh.math.sin(cur_angle), 81920)
            local cur_z = pos_z + q12_mul(sh.math.cos(cur_angle), 81920)
            local cur_y = ground_height(cur_x, cur_z) - 8192

            if not (cur_y < pos_y - 8192 or pos_y < cur_y or cur_y < ceiling_height() or
                    not in_active_cell(cur_x, cur_z) or ground_kind(cur_x, cur_z) == 0) then
                c.pos_x = cur_x
                c.pos_y = cur_y
                c.pos_z = cur_z

                new_angle = asr(rand_q12() - 2048, 4) + 2048
                c.rot_y = cur_angle + new_angle
                local sin_rot = sh.math.sin(c.rot_y)

                c.air_screamer_target_y = cur_y
                c.air_screamer_target_x = cur_x + q12_mul(sin_rot, 122880)
                -- The z target is built from x, as the game does.
                c.air_screamer_target_z = cur_x + q12_mul(sh.math.cos(c.rot_y), 122880)

                if truthy(path_clear(c, nil, target_vec(c), nil)) then
                    break
                end
            end
        end

        i = i + 1
        cur_angle = cur_angle + angle_step
    end

    if i == 16 then
        c.pos_x = pos_x + 204800
        c.pos_y = ceiling_height() * 2
        c.pos_z = pos_z + 204800
        return
    end

    c.move_speed = FLIGHT_TABLE[18]
    c.air_screamer_timer = 40960 -- 10.0
end

function respawn_around_player(c)
    local pos_x = P.pos_x
    local pos_y = P.pos_y
    local pos_z = P.pos_z

    local cur_angle = rand_q12()
    local i = 0
    while i < 16 do
        local cur_x = pos_x + q12_mul(sh.math.sin(cur_angle), 81920)
        local cur_z = pos_z + q12_mul(sh.math.cos(cur_angle), 81920)
        local cur_y = ground_height(cur_x, cur_z) - 8192

        if not (cur_y < pos_y - 8192 or pos_y < cur_y or cur_y < ceiling_height() or
                not in_active_cell(cur_x, cur_z) or ground_kind(cur_x, cur_z) == 0) then
            c.pos_x = cur_x
            c.pos_y = cur_y
            c.pos_z = cur_z

            local new_angle = asr(rand_q12() - 2048, 4) + 2048
            c.rot_y = cur_angle + new_angle

            local sin_rot = sh.math.sin(c.rot_y)
            c.air_screamer_target_y = cur_y
            c.air_screamer_target_x = cur_x + q12_mul(sin_rot, 122880)
            -- The z target is built from x, as the game does.
            c.air_screamer_target_z = cur_x + q12_mul(sh.math.cos(c.rot_y), 122880)

            if truthy(path_clear(c, nil, target_vec(c), nil)) then
                break
            end
        end

        i = i + 1
        cur_angle = cur_angle + 256
    end

    if i == 16 then
        c.pos_x = pos_x + 204800
        c.pos_y = ceiling_height()
        c.pos_z = pos_z + 204800
        return
    end

    c.move_speed = FLIGHT_TABLE[18]
    c.air_screamer_timer = 40960 -- 10.0
end

function respawn_from_area_edge(c)
    -- Quadrants: 0 south, 1 north, 2 west, 3 east.
    local quadrant = 0

    local pos_x = c.pos_x
    local pos_z = c.pos_z
    local player_x = P.pos_x
    local player_z = P.pos_z

    local new_x = pos_x
    local new_z = pos_z

    local col
    if player_x > 65536 then
        col = 2
    else
        col = (player_x < -32768) and 0 or 1
    end

    local row
    if player_z <= 32768 then
        row = (player_z < -32768) and 0 or 1
    else
        row = 2
    end

    local cell = col + row * 3
    if cell == 8 then
        quadrant = ((player_x - 65536) > (player_z - 32768)) and 0 or 2
    elseif cell == 6 then
        if (-32768 - player_x) > (player_z - 32768) then
            quadrant = 0
        else
            quadrant = 3
        end
    elseif cell == 7 then
        quadrant = 0
    elseif cell == 5 then
        quadrant = 2
    elseif cell == 4 then
        local quadrant_angle = (P.rot_y - 512) & 0xFFF
        local q = idiv(quadrant_angle, 1024)
        if q == 1 then
            quadrant = 3
        elseif q == 2 then
            quadrant = 0
        elseif q == 3 then
            quadrant = 2
        else
            quadrant = 1
        end
    elseif cell == 3 then
        quadrant = 3
    elseif cell == 1 then
        quadrant = 1
    elseif cell == 2 then
        if (player_x - 65536) > (-32768 - player_z) then
            quadrant = 1
        else
            quadrant = 2
        end
    elseif cell == 0 then
        if (-32768 - player_x) > (-32768 - player_z) then
            quadrant = 1
        else
            quadrant = 3
        end
    end

    if quadrant == 0 or quadrant == 1 then
        new_x = idiv(pos_x + player_x, 2)
        if new_x > 65536 then
            new_x = 65536
        elseif new_x < -32768 then
            new_x = -32768
        end

        new_z = s32(1638400 - q12_mul(new_x - player_x, new_x - player_x))
        if new_z < 0 then
            new_z = player_z
            if new_x < player_x then
                quadrant = 2
            else
                quadrant = 3
            end
        else
            new_z = sh.entity.sqrt_q12(new_z)
            if quadrant == 0 then
                new_z = new_z + player_z
            else
                new_z = player_z - new_z
            end
        end
    elseif quadrant == 2 or quadrant == 3 then
        new_z = idiv(pos_z + player_z, 2)
        if new_z > 32768 then
            new_z = 32768
        elseif new_z < -32768 then
            new_z = -32768
        end

        new_x = s32(1638400 - q12_mul(new_z - player_z, new_z - player_z))
        if new_x < 0 then
            new_x = player_x
            quadrant = ((new_z < player_z) and 1 or 0) ~ 1
        else
            new_x = sh.entity.sqrt_q12(new_x)
            if quadrant == 2 then
                new_x = new_x + player_x
            else
                new_x = player_x - new_x
            end
        end
    end

    if quadrant == 0 then
        c.rot_y = 2048
        c.pos_y = -8192
    elseif quadrant == 2 then
        c.rot_y = 3072
        c.pos_y = -8192
    elseif quadrant == 3 then
        c.rot_y = 1024
        c.pos_y = -8192
    else
        if quadrant == 1 then
            c.rot_y = 0
        end
        c.pos_y = -8192
    end

    c.move_speed = FLIGHT_TABLE[18]
    c.pos_x = new_x
    c.pos_z = new_z
    c.air_screamer_timer = 40960 -- 10.0

    target_air_ahead_glide(c, 122880, c.rot_y + asr(rand_q12() - 2048, 7))
    detour_pick(c)
end

function target_near_home_close(c)
    local dist_max, dist_min
    if side(c) ~= 0 then
        dist_max = 20480
        dist_min = 12288
    else
        dist_max = 16384
        dist_min = 8192
    end

    HOME_B.x, HOME_B.y, HOME_B.z = spawn_point(c.spawn_index, c)

    local radius = distance_2d_q12(HOME_B.x, HOME_B.z, c.pos_x, c.pos_z)

    local angle = angles_between(HOME_B.x, HOME_B.y, HOME_B.z, c.pos_x, c.pos_y, c.pos_z)
    angle = angle + q12_mul(rand_q12() - 2048, 1365)

    radius = idiv(radius - dist_min, 2)

    HOME_B.x = HOME_B.x + q12_mul(radius, sh.math.sin(angle))
    HOME_B.z = HOME_B.z + q12_mul(radius, sh.math.cos(angle))

    target_ground_around(c, HOME_B, dist_max)
    detour_pick(c)
end

function target_ground_ahead(c, dist, heading)
    local new_x = c.pos_x
    local new_z = c.pos_z

    new_x = new_x + q12_mul(dist, sh.math.sin(heading))
    new_z = new_z + q12_mul(dist, sh.math.cos(heading))
    local new_y = ground_height(new_x, new_z)

    c.air_screamer_target_x = new_x
    c.air_screamer_target_y = new_y
    c.air_screamer_target_z = new_z
    detour_pick(c)
end

function target_ground_around(c, vec, dist)
    local scaled_radius = q12_mul(dist, rand_q12())
    local angle = rand_q12()
    local pos_x = vec.x + q12_mul(scaled_radius, sh.math.sin(angle))
    local pos_z = vec.z + q12_mul(scaled_radius, sh.math.cos(angle))

    local pos_y = ground_height(pos_x, pos_z)
    c.air_screamer_target_x = pos_x
    c.air_screamer_target_y = pos_y
    c.air_screamer_target_z = pos_z

    detour_pick(c)
end

function target_player_edge(c)
    local player_x = P.pos_x
    local player_z = P.pos_z
    local dist = P.cylinder_radius

    local heading = heading_between(P.pos_x, P.pos_z, c.pos_x, c.pos_z)
    local new_x = player_x + q12_mul(dist, sh.math.sin(heading))
    local new_z = player_z + q12_mul(dist, sh.math.cos(heading))
    local new_y = ground_height(new_x, new_z)

    c.air_screamer_target_x = new_x
    c.air_screamer_target_y = new_y
    c.air_screamer_target_z = new_z

    detour_pick(c)
end

function target_aim_ground(c)
    local new_x = c.air_screamer_aim_x
    local new_z = c.air_screamer_aim_z
    local new_y = ground_height(new_x, new_z)

    c.air_screamer_target_x = new_x
    c.air_screamer_target_y = new_y
    c.air_screamer_target_z = new_z
    detour_pick(c)
end

function target_orbit_point(c)
    local target_x = 0
    local target_z = 0
    local target_y = 0
    local ret = false
    local dist = -1

    local pos_x = c.pos_x
    local pos_z = c.pos_z
    local ground = c.air_screamer_ground

    local aim_x = c.air_screamer_aim_x
    local aim_z = c.air_screamer_aim_z

    local walking = flight_mode(c) == 1

    local delta_x = pos_x - aim_x
    local delta_z = pos_z - aim_z
    local angle_to_aim = sh.math.atan2(delta_x, delta_z)

    local aim_dist = sh.entity.sqrt_q12(s32(q12_mul(delta_x, delta_x) + q12_mul(delta_z, delta_z)))
    local probe_dist = rand_q12() * 4 + 16384

    local angle_step
    if rand_q12() < 2048 then
        angle_step = 256
    else
        angle_step = -256
    end

    local cur_angle = angle_to_aim
    for _ = 0, 15 do
        -- Unsigned compare: only directions within 150 degrees of the line away from the aim.
        local outside = ((angle_norm(cur_angle - angle_to_aim) + 1706) & 0xFFFFFFFF) > 3412
        if not outside then
            delta_x = q12_mul(probe_dist, sh.math.sin(cur_angle))
            local new_z = q12_mul(probe_dist, sh.math.cos(cur_angle))

            local new_x = pos_x + delta_x
            ORBIT_POINT.x = new_x

            new_z = new_z + pos_z
            ORBIT_POINT.z = new_z

            if not walking then
                local gh = ground_height(new_x, new_z)
                if ground < gh then
                    gh = ground
                end

                gh = gh - (6144 + idiv(rand_q12(), 2))
                if gh < ceiling_height() then
                    gh = ceiling_height()
                end
                ORBIT_POINT.y = gh
            end

            path_clear(c, nil, ORBIT_POINT, ORBIT_POINT)

            local dist_to_aim = distance_2d_q12(c.air_screamer_aim_x, c.air_screamer_aim_z, ORBIT_POINT.x, ORBIT_POINT.z)
            local score = dist_to_aim + distance_2d_q12(c.pos_x, c.pos_z, ORBIT_POINT.x, ORBIT_POINT.z) * 2

            if dist < score then
                ret = dist_to_aim < aim_dist
                dist = score + 409

                target_x = ORBIT_POINT.x
                target_y = ORBIT_POINT.y
                target_z = ORBIT_POINT.z
            end
        end

        cur_angle = cur_angle + angle_step
    end

    c.air_screamer_target_x = target_x
    c.air_screamer_target_z = target_z
    if walking then
        c.air_screamer_target_y = ground_height(target_x, target_z)
    else
        c.air_screamer_target_y = target_y
    end

    detour_pick(c)

    return ret
end

function target_orbit_point_walk(c)
    return target_orbit_point(c)
end

function target_air_ahead(c, radius, heading)
    local new_x = c.pos_x
    local new_z = c.pos_z

    new_x = new_x + q12_mul(radius, sh.math.sin(heading))
    new_z = new_z + q12_mul(radius, sh.math.cos(heading))

    local new_y = ground_height(new_x, new_z)
    new_y = min(c.air_screamer_ground, new_y)
    new_y = new_y - (6144 + idiv(rand_q12(), 2))
    if new_y < ceiling_height() then
        new_y = ceiling_height()
    end

    c.air_screamer_target_x = new_x
    c.air_screamer_target_y = new_y
    c.air_screamer_target_z = new_z

    detour_pick(c)
end

function target_air_around(c, vec, dist)
    local scaled_radius = q12_mul(dist, rand_q12())
    local heading = rand_q12()
    local new_x = vec.x + q12_mul(scaled_radius, sh.math.sin(heading))
    local new_z = vec.z + q12_mul(scaled_radius, sh.math.cos(heading))

    local new_y = ground_height(new_x, new_z)
    new_y = min(c.air_screamer_ground, new_y)
    new_y = new_y - (6144 + idiv(rand_q12(), 2))

    if new_y < ceiling_height() then
        new_y = ceiling_height()
    end

    c.air_screamer_target_x = new_x
    c.air_screamer_target_y = new_y
    c.air_screamer_target_z = new_z

    detour_pick(c)
end

function target_near_home_medium(c)
    local dist_max, dist_min
    if side(c) ~= 0 then
        dist_max = 24576
        dist_min = 16384
    else
        dist_max = 20480
        dist_min = 12288
    end

    HOME_C.x, HOME_C.y, HOME_C.z = spawn_point(c.spawn_index, c)

    local radius = distance_2d_q12(HOME_C.x, HOME_C.z, c.pos_x, c.pos_z)

    local angle = angles_between(HOME_C.x, HOME_C.y, HOME_C.z, c.pos_x, c.pos_y, c.pos_z)
    angle = angle + q12_mul(rand_q12() - 2048, 1365)

    radius = idiv(radius - dist_min, 2)

    HOME_C.x = HOME_C.x + q12_mul(radius, sh.math.sin(angle))
    HOME_C.z = HOME_C.z + q12_mul(radius, sh.math.cos(angle))

    target_air_around(c, HOME_C, dist_max)
    detour_pick(c)
end

function target_player_flank(c)
    local player_x = P.pos_x
    local player_y = P.pos_y
    local player_z = P.pos_z
    local player_heading = P.rot_y
    local player_radius = P.cylinder_radius

    local chara_x = c.pos_x
    local chara_z = c.pos_z
    local chara_heading = c.rot_y
    local chara_radius = c.cylinder_radius

    local delta_x = player_x - chara_x
    local delta_z = player_z - chara_z
    local dist_to_player = sh.entity.sqrt_q12(s32(q12_mul(delta_x, delta_x) + q12_mul(delta_z, delta_z)))
    local angle_to_player = sh.math.atan2(delta_x, delta_z)

    -- Far enough and Harry outside the forward 60 degrees: veer by 60; otherwise come from behind.
    local angle_delta = angle_norm(angle_to_player - chara_heading)
    if (player_radius + chara_radius * 2) < dist_to_player and truthy(signed_range(angle_delta, 682)) then
        angle_to_player = angle_to_player + ((angle_delta > 0) and -682 or 682)
    else
        angle_to_player = angle_to_player + 2048
    end

    local target_x, target_y, target_z
    if truthy(signed_range(angle_norm(angle_to_player - player_heading), 1024)) then
        target_x = player_x + q12_mul(player_radius, sh.math.sin(angle_to_player))
        target_y = player_y + P.box_offset_y
        target_z = player_z + q12_mul(player_radius, sh.math.cos(angle_to_player))
    else
        target_x = player_x + q12_mul(idiv(player_radius, 4), sh.math.sin(angle_to_player))
        target_y = player_y + P.box_top
        target_z = player_z + q12_mul(idiv(player_radius, 4), sh.math.cos(angle_to_player))
    end

    local ground = ground_height(target_x, target_z)
    ground = min(c.air_screamer_ground, ground)
    if ground < ceiling_height() then
        ground = ceiling_height()
    end
    target_y = max(ground - 8192, target_y)

    c.air_screamer_target_x = target_x
    c.air_screamer_target_y = target_y
    c.air_screamer_target_z = target_z

    detour_pick(c)
end

function target_aim_air(c)
    local new_x = c.air_screamer_aim_x
    local new_z = c.air_screamer_aim_z

    local ground = ground_height(new_x, new_z)
    ground = min(c.air_screamer_ground, ground)
    ground = ground - 6963
    if ground < ceiling_height() then
        ground = ceiling_height()
    end

    c.air_screamer_target_x = new_x
    c.air_screamer_target_y = ground
    c.air_screamer_target_z = new_z

    detour_pick(c)
end

function target_orbit_point_air(c)
    return target_orbit_point(c)
end

function target_air_ahead_glide(c, radius, angle)
    target_air_ahead(c, radius, angle)
end

function target_air_around_glide(c, vec, dist)
    target_air_around(c, vec, dist)
end

function target_near_home_far(c)
    local dist_max, dist_min
    if side(c) ~= 0 then
        dist_max = 32768
        dist_min = 20480
    else
        dist_max = 24576
        dist_min = 16384
    end

    HOME_D.x, HOME_D.y, HOME_D.z = spawn_point(c.spawn_index, c)

    local radius = distance_2d_q12(HOME_D.x, HOME_D.z, c.pos_x, c.pos_z)

    local angle = angles_between(HOME_D.x, HOME_D.y, HOME_D.z, c.pos_x, c.pos_y, c.pos_z)
    angle = angle + q12_mul(rand_q12() - 2048, 1365)

    radius = idiv(radius - dist_min, 2)

    HOME_D.x = HOME_D.x + q12_mul(radius, sh.math.sin(angle))
    HOME_D.z = HOME_D.z + q12_mul(radius, sh.math.cos(angle))

    target_air_around_glide(c, HOME_D, dist_max)
    detour_pick(c)
end

function target_intercept_player(c)
    local player_heading = P.heading_angle
    local player_speed = P.move_speed

    local player_x = P.pos_x
    local player_y = P.pos_y
    local player_z = P.pos_z

    local pos_x = c.pos_x
    local pos_z = c.pos_z

    local rot_y = c.rot_y
    local move_speed = c.move_speed

    local off_x = q12_mul(player_speed, sh.math.sin(player_heading))
    local off_z = q12_mul(player_speed, sh.math.cos(player_heading))

    PLAYER_STEP.y = 0
    PLAYER_STEP.x = q12_mul(dt(), off_x)
    PLAYER_STEP.z = q12_mul(dt(), off_z)

    -- Harry's step this frame clipped by walls gives his real velocity.
    if dt() ~= 0 and wall_detect(P, PLAYER_STEP) ~= 0 then
        off_x = HIT.x
        off_z = HIT.z

        player_speed = sh.entity.sqrt_q12(s32(q12_mul(off_x, off_x) + q12_mul(off_z, off_z)))
        player_speed = idiv(s32(player_speed << 12), dt())
        player_heading = sh.math.atan2(off_x, off_z)
    end

    local tmp = sh.math.cos(angle_norm(rot_y - player_heading))

    off_x = player_x - pos_x
    off_z = player_z - pos_z

    local time = move_speed - q12_mul(player_speed, tmp)
    tmp = angle_norm(sh.math.atan2(off_x, off_z) - rot_y)

    if tmp > -0x2AB and tmp < 0x2AA then
        off_z = sh.entity.sqrt_q12(s32(q12_mul(off_x, off_x) + q12_mul(off_z, off_z)))

        if time < 4096 then
            time = 4096
        end

        -- Closing speed -> time to reach Harry; lead him by that time.
        time = idiv(s32(off_z << 12), time)
        off_z = q12_mul(player_speed, time)
        off_x = q12_mul(off_z, sh.math.sin(player_heading))
        off_z = q12_mul(off_z, sh.math.cos(player_heading))
    else
        time = 40960 -- 10.0
        off_x = 0
        off_z = 0
    end

    local target_x = player_x + off_x
    local target_z = player_z + off_z
    off_x = player_y + P.box_top

    off_z = ground_height(target_x, target_z)
    local v = c.air_screamer_ground

    if v < off_z then
        off_z = v
    end

    if off_z < ceiling_height() then
        off_z = ceiling_height()
    end

    v = off_z - 8192
    if off_x < v then
        off_x = v
    end

    c.air_screamer_target_x = target_x
    c.air_screamer_target_y = off_x
    c.air_screamer_target_z = target_z

    detour_pick(c)

    return time
end

function target_aim_air_glide(c)
    target_aim_air(c)
end

function target_orbit_point_glide(c)
    return target_orbit_point(c)
end

-- Air Screamer / Night Flutter: flight state queries, path probing and the per-state flight targets
-- (S.dyn rows 0, 1, 3, 5: heading, height, roll/pitch and turn goals).

-- The kind of flight the screamer is in: 4, 1, 2, 3 by the movement flags, -1 none, 0 when the
-- module or the screamer is dead.
function flight_mode(c)
    local flags = c.air_screamer_flags
    local result = 0
    if S.flags >= 0 and c.health >= 0 then
        result = 4
        if flags & (1 << 17) == 0 then
            result = 1
            if flags & (1 << 18) == 0 then
                result = 2
                if flags & (1 << 19) == 0 then
                    result = -1
                    if flags & (1 << 20) ~= 0 then
                        result = 3
                    end
                end
            end
        end
    end
    return result
end

function slope_blocks_path(c, reach)
    local tx, ty, tz
    if S.flags & (1 << 27) ~= 0 then
        tx, ty, tz = c.air_screamer_detour_x, c.air_screamer_detour_y, c.air_screamer_detour_z
    else
        tx, ty, tz = c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z
    end

    local pos_x = c.pos_x
    local pos_y = c.pos_y
    local pos_z = c.pos_z

    local dx = s32(tx - pos_x)
    local dy = s32(ty - pos_y)
    local dz = s32(tz - pos_z)

    local target_y = ty
    local top = c.box_top

    slope_force(pos_x, pos_z, dx, dz)

    if reach >= SLOPE[0] and (ground_height(pos_x, pos_z) + top) >= SLOPE[1] then
        if dy < 0 then
            if (pos_y + top) < SLOPE[1] then
                return true
            end
        else
            if (target_y + top) < SLOPE[1] then
                return true
            end
        end
    end
    return false
end

function near_ground(c)
    return (ground_height(c.pos_x, c.pos_z) - 819) < c.pos_y
end

function low_ahead(c, speed_mult)
    local speed = c.move_speed
    local pos_x = c.pos_x
    local pos_z = c.pos_z
    local heading = c.heading_angle

    speed = q12_mul(speed, speed_mult)
    pos_x = pos_x + q12_mul(sh.math.sin(heading), speed)
    pos_z = pos_z + q12_mul(sh.math.cos(heading), speed)

    local kind = ground_kind(pos_x, pos_z)
    if kind == 0 or kind == 7 or kind == 12 then
        return true
    end

    return (ground_height(pos_x, pos_z) + 4915) > c.pos_y -- 1.2
end

function front_clear(c)
    if (map_light() & 1) ~= 0 and (sh.save.game_difficulty() <= 0 or c.chara_id == 2) then
        return false
    end

    FRONT_POINT.x = c.pos_x + q12_mul(sh.math.sin(c.rot_y), 8192)
    FRONT_POINT.y = c.pos_y + 2048
    FRONT_POINT.z = c.pos_z + q12_mul(sh.math.cos(c.rot_y), 8192)

    return path_clear(c, nil, FRONT_POINT, nil)
end

function slope_under_body(c)
    slope_force(c.pos_x, c.pos_z, 0, 0)

    local kind = ground_kind(c.pos_x, c.pos_z)
    if kind == 0 or kind == 7 or kind == 12 then
        return true
    end

    return (ground_height(c.pos_x, c.pos_z) + (c.box_top * 2)) >= SLOPE[1]
end

function can_land_here(c)
    local kind = ground_kind(c.pos_x, c.pos_z)
    if kind == 0 or kind == 7 or kind == 12 then
        return false
    end

    return ground_height(c.pos_x, c.pos_z) <= 12288 -- 3.0
end

function glide_landing_step(c)
    local step = 0
    if c.anim_status ~= 35 then -- Glide, active
        return step
    end

    if c.air_screamer_flags & (1 << 29) == 0 and
       ((map_light() & 1) == 0 or (sh.save.game_difficulty() > 0 and c.chara_id ~= 2)) then
        if can_land_here(c) and
           (near_ground(c) or slope_blocks_path(c, 8192)) then
            if low_ahead(c, 4096) then
                step = 1
            else
                step = 2
            end
        end
    else
        step = 1
    end

    return step
end

function hover_can_land(c)
    return (c.anim_status == 39 or c.anim_status == 55) and
           can_land_here(c) and
           ((c.fall_speed >= 0 and near_ground(c)) or slope_blocks_path(c, 4096))
end

function takeoff_kind(c)
    local ret = 0
    local status = c.anim_status & 0xFF

    if status == 51 or status == 47 then
        if c.pos_y < (ground_height(c.pos_x, c.pos_z) - 2867) then
            if front_clear(c) and c.move_speed > 6144 then
                ret = 2
            else
                slope_force(c.pos_x, c.pos_z, 0, 0)

                if SLOPE[1] < ((c.pos_y + c.box_top) - 8192) then
                    ret = 1
                end
            end
        end
    end

    return ret
end

function far_from_home(c)
    if role(c) == 1 then
        HOME_A.x, HOME_A.y, HOME_A.z = spawn_point(c.spawn_index, c)
        return distance_2d_q12(c.pos_x, c.pos_z, HOME_A.x, HOME_A.z) > 163840 -- 40.0
    end
    return false
end

function retreat_chance(c, dist)
    local mult = 0
    if side(c) ~= 0 then
        mult = 409
    end

    local chance = 0
    if attack_set(c) == 5 then
        chance = max(idiv(40960 - dist, 128), 0)
        chance = chance + (idiv(approach_score(c), 5) - (mult * 2))
    end

    if c.health > 1228800 then -- 300.0
        chance = idiv(chance, 2)
    end

    local r = role(c)
    if r == 3 then
        chance = 0
    elseif r == 1 then
        if ((c.slot - 1) & 1) == 0 then
            chance = 0
        else
            chance = idiv(chance, 2)
        end
    elseif r == 0 then
        if ((c.slot - 1) & 1) == 0 then
            chance = idiv(chance, 2)
        end
    end

    return chance
end

function attack_chance(c, dist)
    local extra = side(c) ~= 0 and 409 or 0

    local base = 0
    local set = attack_set(c)
    if set == 3 then
        base = 4096
    elseif set == 2 then
        base = 2867 + (extra * 2)
    elseif set == 0 then
        base = 1638 + (extra * 2)
    elseif set == 5 then
        base = 819 + (extra * 4)
    elseif set == 1 or set == 4 then
        base = max(idiv(40960 - dist, 64), 0)
        base = base + (1638 + (extra * 2))
    end

    return base + idiv(approach_score(c) - 4096, 8)
end

-- Line of sight from `from` (nil: the screamer) to `to`, kept off the ground slope. Returns true when
-- the way is clear. `out` gets the reached point; the hit point is then lifted for the next probe.
function path_clear(c, from, to, out)
    local src
    if from == nil then
        src = { x = c.pos_x, y = c.pos_y, z = c.pos_z }
    else
        src = from
    end

    local pos_x = src.x
    local pos_y = src.y
    local pos_z = src.z

    local offset_x = s32(to.x - pos_x)
    local offset_z = s32(to.z - pos_z)

    RAY_DELTA.x = offset_x
    RAY_DELTA.z = offset_z

    local dy
    if flight_mode(c) == 1 then
        dy = 0
        RAY_DELTA.y = 0
    else
        dy = s32(to.y - pos_y)
        RAY_DELTA.y = dy
    end

    -- Harry must not block the ray.
    local saved_state = sh.entity.collision_state(0)
    sh.entity.set_collision_state(0, 0)

    local hit = ray_los(src, RAY_DELTA, c)
    hit = hit ~= nil and hit ~= false and hit ~= 0

    sh.entity.set_collision_state(0, saved_state)
    offset_x = s32(RAY.x - pos_x)
    offset_z = s32(RAY.z - pos_z)

    slope_force(pos_x, pos_z, offset_x, offset_z)

    local slope_height = SLOPE[1]
    local top = c.box_top

    if dy < 0 and slope_height < (pos_y + top) then
        if (RAY.y + top) < slope_height then
            RAY.y = slope_height - top
        end
    elseif (RAY.y + top) < slope_height then
        local slope_reach = SLOPE[0]

        if slope_reach < RAY.distance then
            local scale
            if RAY.distance == 0 then
                scale = 0
            else
                scale = idiv(s32(slope_reach << 12), RAY.distance)
            end

            hit = true
            local px = q12_mul(offset_x, scale)
            local py = q12_mul(dy, scale)

            RAY.distance = slope_reach
            RAY.has_hit = 1
            RAY.x = s32(px + pos_x)
            RAY.y = s32(py + pos_y)
            RAY.ground_height = RAY.y

            RAY.z = s32(q12_mul(offset_z, scale) + pos_z)
        end
    end

    if out ~= nil then
        -- `out` takes the point before the lift below; only RAY.y is lifted.
        out.x = RAY.x
        out.y = RAY.y
        out.z = RAY.z

        if hit then
            if ceiling_height() < RAY.ground_height then
                local offset = idiv(rand_q12(), 2) + 6144
                RAY.y = RAY.ground_height - offset
            else
                local height = ground_height(c.pos_x, c.pos_z)
                local offset = idiv(rand_q12(), 2) + 6144
                height = height - offset

                offset = idiv(rand_q12(), 2) - 1024
                RAY.y = c.pos_y - offset

                if height < RAY.y then
                    RAY.y = height
                end
            end
        end
    end

    return not hit
end

-- Picks the detour point (air_screamer_detour_*) towards the target: straight when clear, under a
-- ceiling ahead, else the best of nine probes fanned over 180 degrees, its height blended by distance.
function detour_pick(c)
    local spread = 32768 -- 8.0

    local target = { x = c.air_screamer_target_x, y = c.air_screamer_target_y, z = c.air_screamer_target_z }

    local dist0 = distance_2d_q12(c.pos_x, c.pos_z, target.x, target.z)
    local heading = angles_between(c.pos_x, c.pos_y, c.pos_z, target.x, target.y, target.z)

    local rot_y = c.rot_y

    local pos_x = c.pos_x
    local pos_y = c.pos_y
    local pos_z = c.pos_z

    local angle = angle_norm(heading - rot_y)
    local target_y = target.y

    if path_clear(c, nil, target, PROBE) then
        c.air_screamer_detour_x = PROBE.x
        c.air_screamer_detour_y = PROBE.y
        c.air_screamer_detour_z = PROBE.z
        return
    end

    S.flags = s32(S.flags | (1 << 27))

    if angle > -129 and angle < 129 then -- within 11.34 degrees
        AHEAD.x = sh.math.sin(rot_y) * 2
        AHEAD.y = 0
        AHEAD.z = sh.math.cos(rot_y) * 2

        local blocked = ray_los({ x = c.pos_x, y = c.pos_y, z = c.pos_z }, AHEAD, c)
        if blocked ~= nil and blocked ~= false and blocked ~= 0 and
           ceiling_height() < RAY.ground_height then
            c.air_screamer_detour_x = RAY.x
            c.air_screamer_detour_y = RAY.ground_height - 6144
            c.air_screamer_detour_z = RAY.z
            return
        end
    end

    local slack = s32(dist0 - distance_2d_q12(c.pos_x, c.pos_z, PROBE.x, PROBE.z) + spread)

    local best_score = 0x7FFFFFFF
    local best_turn = 4096

    local cur_angle = (rot_y + idiv(angle, 2)) - 1024
    for i = 0, 8 do
        PROBE.x = pos_x + q12_mul(sh.math.sin(cur_angle), 32768)
        PROBE.y = target_y
        PROBE.z = pos_z + q12_mul(sh.math.cos(cur_angle), 32768)

        local reached
        if not path_clear(c, nil, PROBE, PROBE) then
            reached = distance_2d_q12(c.pos_x, c.pos_z, PROBE.x, PROBE.z)
        else
            reached = 32768
        end

        spread = distance_2d_q12(PROBE.x, PROBE.z, target.x, target.z)
        local score = q12_mul(s32(slack - reached), spread)

        local turn = abs(angle_norm(cur_angle - rot_y))

        if score < best_score or (score == best_score and turn < best_turn) then
            best_score = score
            best_turn = turn
            c.air_screamer_detour_x = PROBE.x
            c.air_screamer_detour_y = PROBE.y
            c.air_screamer_detour_z = PROBE.z
        end

        cur_angle = cur_angle + 256
    end

    local near = distance_2d_q12(c.air_screamer_detour_x, c.air_screamer_detour_z, c.pos_x, c.pos_z)
    local far = distance_2d_q12(c.air_screamer_detour_x, c.air_screamer_detour_z, target.x, target.z)

    local total = s32(near + far)
    if total == 0 then
        c.air_screamer_detour_y = target_y
        return
    end

    c.air_screamer_detour_y = idiv(s32(s32(q12_mul(target_y, near) + q12_mul(pos_y, far)) << 12), total)

    -- Result unused; the probe still updates RAY, SLOPE and the rng.
    path_clear(c, nil, PROBE, PROBE)
end

-- Highest a screamer may fly: 3 above the ground plane and at least 2 above Harry.
function ceiling_height()
    return min(-12288, P.pos_y - 8192)
end

function flight_goal_hold(c)
    S.dyn[14] = 0 -- [3][2]

    S.dyn[2] = FLIGHT_TABLE[4] -- [0][2] = [2][0]
    S.dyn[1] = FLIGHT_TABLE[5]

    S.dyn[6] = FLIGHT_TABLE[22] -- [1][2] = [11][0]
    S.dyn[5] = FLIGHT_TABLE[23]

    S.dyn[13] = FLIGHT_TABLE[61] -- [3][1] = [30][1]

    turn_goal_set(c, 0)
end

function flight_goal_hold_facing(c)
    local angle = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z,
        c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z) - c.rot_y)

    S.dyn[14] = 0

    S.dyn[2] = FLIGHT_TABLE[4]
    S.dyn[1] = FLIGHT_TABLE[5]

    S.dyn[6] = FLIGHT_TABLE[22]
    S.dyn[5] = FLIGHT_TABLE[23]

    S.dyn[13] = FLIGHT_TABLE[61]

    turn_goal_set(c, angle)
end

function flight_goal_hold_bank(c)
    local angle = angles_between(c.pos_x, c.pos_y, c.pos_z,
        c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z)
    angle = angle_norm(angle - c.rot_y)
    local e0 = idiv(FLIGHT_TABLE[60], 2) -- [30][0]
    local e1 = FLIGHT_TABLE[61]

    if angle <= 5 then
        e1 = -e1
        if angle < -5 then
            e0 = -e0
        else
            e1 = 0
            e0 = 0
        end
    end

    S.dyn[3] = 0
    S.dyn[0] = 1

    S.dyn[2] = FLIGHT_TABLE[4]
    S.dyn[1] = FLIGHT_TABLE[5]
    S.dyn[6] = FLIGHT_TABLE[22]
    S.dyn[5] = FLIGHT_TABLE[23]
    S.dyn[7] = 0
    S.dyn[4] = 1

    S.dyn[14] = e0
    S.dyn[13] = e1
    S.dyn[15] = angle
    S.dyn[12] = 1

    turn_goal_set(c, idiv(angle, 2))
end

function flight_goal_walk_detour(c, cond)
    local narrow = cond ~= nil and cond ~= false and cond ~= 0

    local angle = angles_between(c.pos_x, c.pos_y, c.pos_z,
        c.air_screamer_detour_x, c.air_screamer_detour_y, c.air_screamer_detour_z)
    local dist = distance_2d_q12(c.pos_x, c.pos_z, c.air_screamer_detour_x, c.air_screamer_detour_z)
    angle = angle_norm(angle - c.rot_y)

    local status = c.anim_status | 1

    local climb_row
    if status == 0x2D then
        climb_row = 0xC
    elseif status == 0x31 then
        climb_row = 0xD
    else
        climb_row = 0xB
    end

    local speed_row, tilt_row
    if status == 0x31 then
        speed_row = 3
        tilt_row = 0x1F
    elseif c.cylinder_radius + 0xCC >= dist then
        speed_row = 2
        tilt_row = 0x1E
    else
        local cone
        if narrow then
            cone = 0x155
        else
            cone = 0x2AA
        end

        -- Unsigned: an angle left of -cone wraps to a huge value.
        if ((angle + cone) & 0xFFFFFFFF) < cone * 2 then
            if narrow then
                speed_row = 3
                tilt_row = 0x1F
            else
                speed_row = 0
                tilt_row = 0x1C
            end
        else
            speed_row = 2
            tilt_row = 0x1E
        end
    end

    local tilt0 = FLIGHT_TABLE[tilt_row * 2]
    local tilt1 = FLIGHT_TABLE[tilt_row * 2 + 1]

    if angle < 6 then
        if angle < -5 then
            tilt1 = -tilt1
            tilt0 = -tilt0
        else
            tilt1 = 0
            tilt0 = 0
        end
    end

    local speed0 = FLIGHT_TABLE[speed_row * 2]
    local speed1 = FLIGHT_TABLE[speed_row * 2 + 1]

    S.dyn[1] = speed1
    S.dyn[2] = speed0

    if status ~= 0x31 then
        S.dyn[3] = dist
        S.dyn[0] = 1
    end

    local climb0 = FLIGHT_TABLE[climb_row * 2]
    local climb1 = FLIGHT_TABLE[climb_row * 2 + 1]

    S.dyn[14] = tilt0
    S.dyn[13] = tilt1
    S.dyn[15] = angle
    S.dyn[12] = 1

    S.dyn[6] = climb0
    S.dyn[5] = climb1

    turn_goal_set(c, idiv(angle, 2))
end

function flight_goal_stand_turn(c)
    local status = c.anim_status | 1
    local angle = angles_between(c.pos_x, c.pos_y, c.pos_z,
        c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z)
    angle = angle_norm(angle - c.rot_y)

    local climb_row
    if status == 45 then
        climb_row = 12
    else
        climb_row = 11
    end

    local tilt0 = FLIGHT_TABLE[60] -- [30][0]
    local tilt1 = FLIGHT_TABLE[61]

    if angle < 6 then
        if angle < -5 then
            tilt1 = -tilt1
            tilt0 = -tilt0
        else
            tilt1 = 0
            tilt0 = 0
        end
    end

    S.dyn[3] = 0
    S.dyn[0] = 1
    S.dyn[2] = FLIGHT_TABLE[4]
    S.dyn[1] = FLIGHT_TABLE[5]

    local climb0 = FLIGHT_TABLE[climb_row * 2]
    local climb1 = FLIGHT_TABLE[climb_row * 2 + 1]

    S.dyn[14] = tilt0
    S.dyn[13] = tilt1
    S.dyn[15] = angle
    S.dyn[12] = 1

    S.dyn[6] = climb0
    S.dyn[5] = climb1

    turn_goal_set(c, idiv(angle, 2))
end

function flight_goal_stand_lift(c)
    local status = c.anim_status | 1
    local angle = angles_between(c.pos_x, c.pos_y, c.pos_z,
        c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z)
    angle = angle_norm(angle - c.rot_y)

    local climb_row
    if status == 45 then -- StandIdleToHover, active
        climb_row = 12
    else
        climb_row = 11
    end

    local e0 = FLIGHT_TABLE[58] -- [29][0]
    local e1 = FLIGHT_TABLE[59]

    if angle <= 5 then
        e1 = -e1
        if angle < -5 then
            e0 = -e0
        else
            e1 = 0
            e0 = 0
        end
    end

    local e2 = FLIGHT_TABLE[2] -- [1][0]

    S.dyn[2] = e2
    S.dyn[1] = FLIGHT_TABLE[3]

    local e3 = FLIGHT_TABLE[climb_row * 2]
    local e4 = FLIGHT_TABLE[climb_row * 2 + 1]
    S.dyn[6] = e3
    S.dyn[5] = e4

    S.dyn[14] = e0
    S.dyn[13] = e1
    S.dyn[15] = angle
    S.dyn[12] = 1

    turn_goal_set(c, idiv(angle, 2))
end

function flight_goal_hold_glance(c)
    local heading = angles_between(c.pos_x, c.pos_y, c.pos_z,
        c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z)
    local angle = angle_norm(heading - c.rot_y)

    S.dyn[14] = 0

    S.dyn[2] = FLIGHT_TABLE[4]
    S.dyn[1] = FLIGHT_TABLE[5]

    S.dyn[6] = FLIGHT_TABLE[22]
    S.dyn[5] = FLIGHT_TABLE[23]

    S.dyn[13] = FLIGHT_TABLE[61]

    -- Rounds the quarter towards zero.
    if angle < 0 then
        angle = angle + 3
    end

    turn_goal_set(c, asr(angle, 2))
end

function flight_goal_stunned(c)
    local speed_row, tilt_row
    if c.anim_status == 9 and c.anim_time > 262144 then -- StandToStun, active, past keyframe 64
        speed_row = 4
        tilt_row = 32
    else
        speed_row = 2
        tilt_row = 30
    end

    local speed0 = FLIGHT_TABLE[speed_row * 2]
    local speed1 = FLIGHT_TABLE[speed_row * 2 + 1]

    S.dyn[2] = speed0
    S.dyn[1] = speed1

    S.dyn[6] = FLIGHT_TABLE[22]
    S.dyn[5] = FLIGHT_TABLE[23]

    S.dyn[13] = FLIGHT_TABLE[tilt_row * 2 + 1]
    S.dyn[14] = 0

    turn_goal_set(c, 0)
end

-- Flight height towards a point `dist` away whose height is `target_y`: far points pull down to
-- below the ground (or own height), not above the ceiling limit.
function cruise_height(c, target_y, dist)
    local y = target_y
    local pos_y = c.pos_y
    local d = dist
    local ground = ground_height(c.pos_x, c.pos_z)

    local prev = c.air_screamer_ground

    local vx = c.air_screamer_detour_x
    local vz = c.air_screamer_detour_z

    if prev < ground then
        ground = prev
    end

    ground = ground - 6963 -- 1.7
    prev = ground_kind(vx, vz)
    if prev == 12 or prev == 0 or prev == 7 then
        d = 0
    end

    if pos_y < ground then
        ground = pos_y
    end

    if d > 49152 then -- 12.0
        y = ground
    elseif d > 16384 then -- 4.0
        prev = idiv(s32((d - 16384) << 12), 32768)
        y = y + q12_mul(s32(ground - y), prev)
    end

    if y < ceiling_height() then
        y = ceiling_height()
    end

    return y
end

function flight_goal_fly_detour(c)
    local dx, dy, dz = c.air_screamer_detour_x, c.air_screamer_detour_y, c.air_screamer_detour_z
    local dist = distance_2d_q12(c.pos_x, c.pos_z, dx, dz)
    local angle = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z, dx, dy, dz) - c.rot_y)

    local tmp = cruise_height(c, dy, dist)
    local climb = false
    local rise = s32(tmp - c.pos_y)

    local speed_row, tilt_row
    if dist > (c.cylinder_radius + 204) and ((angle + 512) & 0xFFFFFFFF) < 1024 then
        speed_row = 5
        tilt_row = 33
    else
        speed_row = 7
        tilt_row = 35
    end

    local status = c.anim_status

    local climb_row
    if status == 45 then
        climb_row = 17
    elseif status < 45 or status >= 56 or status < 54 then
        climb = true
        climb_row = 14

        if rise <= 204 then
            climb_row = 15
            if rise < -204 then
                climb_row = 16
            end
        end
    else
        climb_row = 18
    end

    local e0 = FLIGHT_TABLE[tilt_row * 2]
    local e1 = FLIGHT_TABLE[tilt_row * 2 + 1]
    if angle <= 5 then
        e1 = -e1
        if angle < -5 then
            e0 = -e0
        else
            e1 = 0
            e0 = 0
        end
    end

    local e2 = FLIGHT_TABLE[speed_row * 2]
    local e3 = FLIGHT_TABLE[speed_row * 2 + 1]

    S.dyn[2] = e2
    S.dyn[1] = e3
    S.dyn[3] = dist
    S.dyn[0] = 1

    local e4 = FLIGHT_TABLE[climb_row * 2]
    local e5 = FLIGHT_TABLE[climb_row * 2 + 1]

    S.dyn[6] = e4
    S.dyn[5] = e5
    if climb then
        S.dyn[7] = rise
        S.dyn[4] = 1
    end

    S.dyn[14] = e0
    S.dyn[13] = e1
    S.dyn[15] = angle
    S.dyn[12] = 1

    turn_goal_set(c, idiv(angle, 2))
end

function flight_goal_hover_target(c)
    local tx, ty, tz = c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z
    local dist = distance_2d_q12(c.pos_x, c.pos_z, tx, tz)
    local angle = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z, tx, ty, tz) - c.rot_y)
    local tmp = cruise_height(c, c.air_screamer_target_y, dist)
    local climb = false
    local rise = s32(tmp - c.pos_y)

    local status = c.anim_status

    local climb_row
    if status == 45 then
        climb_row = 17
    elseif status < 45 or status >= 56 or status < 54 then
        climb = true
        climb_row = 14

        if rise <= 204 then
            climb_row = 15
            if rise < -204 then
                climb_row = 16
            end
        end
    else
        climb_row = 18
    end

    local e0 = FLIGHT_TABLE[70] -- [35][0]
    local e1 = FLIGHT_TABLE[71]

    if angle <= 5 then
        e1 = -e1
        if angle < -5 then
            e0 = -e0
        else
            e1 = 0
            e0 = 0
        end
    end

    local e2 = FLIGHT_TABLE[14] -- [7][0]
    local e3 = FLIGHT_TABLE[15]

    S.dyn[2] = e2
    S.dyn[1] = e3
    S.dyn[3] = 0
    S.dyn[0] = 1

    local e4 = FLIGHT_TABLE[climb_row * 2]
    local e5 = FLIGHT_TABLE[climb_row * 2 + 1]

    S.dyn[6] = e4
    S.dyn[5] = e5
    if climb then
        S.dyn[7] = rise
        S.dyn[4] = 1
    end

    S.dyn[14] = e0
    S.dyn[13] = e1
    S.dyn[15] = angle
    S.dyn[12] = 1

    turn_goal_set(c, idiv(angle, 2))
end

function flight_goal_fly_target(c)
    local tx, ty, tz = c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z
    local dist = distance_2d_q12(c.pos_x, c.pos_z, tx, tz)
    local angle = angle_norm(angles_between(c.pos_x, c.pos_y, c.pos_z, tx, ty, tz) - c.rot_y)

    local tmp = cruise_height(c, c.air_screamer_target_y, dist)
    local climb = false
    local rise = s32(tmp - c.pos_y)
    local status = c.anim_status

    local climb_row
    if status == 45 then
        climb_row = 17
    elseif status < 45 or status >= 56 or status < 54 then
        climb = true
        climb_row = 14

        if rise <= 204 then
            climb_row = 15
            if rise < -204 then
                climb_row = 16
            end
        end
    else
        climb_row = 18
    end

    local e0 = FLIGHT_TABLE[68] -- [34][0]
    local e1 = FLIGHT_TABLE[69]

    if angle <= 5 then
        e1 = -e1
        if angle < -5 then
            e0 = -e0
        else
            e1 = 0
            e0 = 0
        end
    end

    local e2 = FLIGHT_TABLE[12] -- [6][0]
    local e3 = FLIGHT_TABLE[13]

    S.dyn[2] = e2
    S.dyn[1] = e3

    local e4 = FLIGHT_TABLE[climb_row * 2]
    local e5 = FLIGHT_TABLE[climb_row * 2 + 1]

    S.dyn[6] = e4
    S.dyn[5] = e5
    if climb then
        S.dyn[7] = rise
        S.dyn[4] = 1
    end

    S.dyn[14] = e0
    S.dyn[13] = e1
    S.dyn[15] = angle
    S.dyn[12] = 1

    turn_goal_set(c, idiv(angle, 2))
end

function flight_goal_hover_bank(c)
    local heading = angles_between(c.pos_x, c.pos_y, c.pos_z,
        c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z)
    local angle = angle_norm(heading - c.rot_y)

    local e0 = idiv(FLIGHT_TABLE[70], 2) -- [35][0]
    local e1 = FLIGHT_TABLE[71]

    if angle <= 5 then
        e1 = -e1
        if angle < -5 then
            e0 = -e0
        else
            e1 = 0
            e0 = 0
        end
    end

    S.dyn[3] = 0
    S.dyn[0] = 1
    S.dyn[2] = FLIGHT_TABLE[14] -- [7][0]
    S.dyn[1] = FLIGHT_TABLE[15]

    S.dyn[7] = 0
    S.dyn[4] = 1
    S.dyn[6] = FLIGHT_TABLE[30] -- [15][0]
    S.dyn[5] = FLIGHT_TABLE[31]

    S.dyn[14] = e0
    S.dyn[13] = e1
    S.dyn[15] = angle
    S.dyn[12] = 1

    turn_goal_set(c, idiv(angle, 2))
end

function flight_goal_hover_glance(c)
    local heading = angles_between(c.pos_x, c.pos_y, c.pos_z,
        c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z)
    local angle = angle_norm(heading - c.rot_y)

    S.dyn[2] = FLIGHT_TABLE[14] -- [7][0]
    S.dyn[1] = FLIGHT_TABLE[15]

    S.dyn[6] = FLIGHT_TABLE[40] -- [20][0]
    S.dyn[5] = FLIGHT_TABLE[41]

    S.dyn[14] = 0
    S.dyn[13] = FLIGHT_TABLE[71] -- [35][1]

    if angle < 0 then
        angle = angle + 3
    end

    turn_goal_set(c, asr(angle, 2))
end

function flight_goal_hover_still(c)
    S.dyn[2] = FLIGHT_TABLE[14] -- [7][0]
    S.dyn[1] = FLIGHT_TABLE[15]

    S.dyn[6] = FLIGHT_TABLE[38] -- [19][0]
    S.dyn[5] = FLIGHT_TABLE[39]

    S.dyn[14] = 0
    S.dyn[13] = FLIGHT_TABLE[71] -- [35][1]

    turn_goal_set(c, 0)
end

function flight_goal_slow_bank(c)
    local angle = angles_between(c.pos_x, c.pos_y, c.pos_z,
        c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z)
    angle = angle_norm(angle - c.rot_y)

    local first = FLIGHT_TABLE[66] -- [33][0]
    local tilt1 = FLIGHT_TABLE[67]
    local tilt0 = idiv(first, 2)

    if angle <= 5 then
        if angle < -5 then
            tilt1 = -tilt1
            tilt0 = -tilt0
        else
            tilt1 = 0
            tilt0 = 0
        end
    end

    S.dyn[2] = idiv(FLIGHT_TABLE[10], 2) -- [5][0]
    S.dyn[1] = FLIGHT_TABLE[11]

    S.dyn[14] = tilt0
    S.dyn[13] = tilt1
    S.dyn[15] = angle
    S.dyn[12] = 1

    S.dyn[6] = FLIGHT_TABLE[38] -- [19][0]
    S.dyn[5] = FLIGHT_TABLE[39]

    turn_goal_set(c, idiv(angle, 2))
end

function flight_goal_glide_detour(c, turn_kind, clip_kind)
    local dx, dy, dz = c.air_screamer_detour_x, c.air_screamer_detour_y, c.air_screamer_detour_z

    local angle = angles_between(c.pos_x, c.pos_y, c.pos_z, dx, dy, dz)
    local dist = distance_2d_q12(c.pos_x, c.pos_z, dx, dz)
    local rise = cruise_height(c, dy, dist)
    angle = angle_norm(angle - c.rot_y)

    rise = s32(rise - c.pos_y)
    local status = c.anim_status | 1

    local climb = 0

    local sector
    if angle >= -512 and angle <= 512 then
        sector = 0
    else
        if angle >= -1536 and angle <= 1536 then
            sector = 1
        else
            sector = 2
        end
    end

    turn_kind = turn_kind & 3
    clip_kind = clip_kind & 3

    local speed_row = D.turn_clip[sector][turn_kind][clip_kind]
    local tilt_row = speed_row + 0x1C

    local climb_row
    if status == 33 then
        speed_row = 10
        climb_row = 25
    elseif status == 49 then
        climb_row = 24
    else
        climb = 1

        if rise >= 205 then
            climb_row = 21
        else
            if rise < -204 then
                climb_row = 23
            else
                climb_row = 22
            end
        end
    end

    local tilt0 = FLIGHT_TABLE[tilt_row * 2]
    local tilt1 = FLIGHT_TABLE[tilt_row * 2 + 1]

    if angle <= 5 then
        if angle < -5 then
            tilt1 = -tilt1
            tilt0 = -tilt0
        else
            tilt1 = 0
            tilt0 = 0
        end
    end

    local speed0 = FLIGHT_TABLE[speed_row * 2]
    local speed1 = FLIGHT_TABLE[speed_row * 2 + 1]

    S.dyn[2] = speed0
    S.dyn[1] = speed1

    local climb0 = FLIGHT_TABLE[climb_row * 2]
    local climb1 = FLIGHT_TABLE[climb_row * 2 + 1]

    S.dyn[6] = climb0
    S.dyn[5] = climb1

    if climb ~= 0 then
        S.dyn[7] = rise
        S.dyn[4] = 1
    end

    S.dyn[14] = tilt0
    S.dyn[13] = tilt1
    S.dyn[15] = angle
    S.dyn[12] = 1

    turn_goal_set(c, idiv(angle, 2))
end

function flight_goal_glide_glance(c)
    local heading = angles_between(c.pos_x, c.pos_y, c.pos_z,
        c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z)
    local angle = angle_norm(heading - c.rot_y)

    S.dyn[14] = 0

    S.dyn[2] = FLIGHT_TABLE[18] -- [9][0]
    S.dyn[1] = FLIGHT_TABLE[19]

    S.dyn[6] = FLIGHT_TABLE[54] -- [27][0]
    S.dyn[5] = FLIGHT_TABLE[55]

    S.dyn[13] = FLIGHT_TABLE[75] -- [37][1]

    if angle < 0 then
        angle = angle + 3
    end

    turn_goal_set(c, asr(angle, 2))
end

function flight_goal_glide_still(c)
    S.dyn[2] = FLIGHT_TABLE[18] -- [9][0]
    S.dyn[1] = FLIGHT_TABLE[19]

    S.dyn[14] = 0

    S.dyn[6] = FLIGHT_TABLE[52] -- [26][0]
    S.dyn[5] = FLIGHT_TABLE[53]

    S.dyn[13] = FLIGHT_TABLE[75] -- [37][1]

    turn_goal_set(c, 0)
end

function flight_goal_glide_target(c)
    local rise = s32(c.air_screamer_target_y - c.pos_y)

    local angle = angles_between(c.pos_x, c.pos_y, c.pos_z,
        c.air_screamer_target_x, c.air_screamer_target_y, c.air_screamer_target_z)
    angle = angle_norm(angle - c.rot_y)

    local first = FLIGHT_TABLE[74] -- [37][0]
    local tilt1 = FLIGHT_TABLE[75]
    local tilt0 = idiv(first, 2)

    if angle <= 5 then
        if angle < -5 then
            tilt1 = -tilt1
            tilt0 = -tilt0
        else
            tilt1 = 0
            tilt0 = 0
        end
    end

    S.dyn[7] = rise
    S.dyn[4] = 1

    S.dyn[14] = tilt0
    S.dyn[13] = tilt1
    S.dyn[15] = angle
    S.dyn[12] = 1

    S.dyn[2] = FLIGHT_TABLE[18] -- [9][0]
    S.dyn[1] = FLIGHT_TABLE[19]

    S.dyn[6] = FLIGHT_TABLE[44] -- [22][0]
    S.dyn[5] = FLIGHT_TABLE[45]

    turn_goal_set(c, idiv(angle, 2))
end

-- Turn goal (row 5): the wanted angle to the target clamped to +-60 degrees, with a dead zone of
-- half a degree.
function turn_goal_set(c, angle)
    local wanted = angle
    if wanted > 682 then
        wanted = 682
    elseif wanted < -682 then
        wanted = -682
    end

    local delta = wanted - c.angle_to_target
    if delta > 5 then
        delta = delta - 5
    elseif delta >= -5 then
        delta = 0
    else
        delta = delta + 5
    end

    local dir = 1024
    if delta <= 0 then
        dir = 0
        if delta < 0 then
            dir = -1024
        end
    end

    S.dyn[22] = dir -- [5][2]
    S.dyn[21] = 0x1800
    S.dyn[23] = delta
    S.dyn[20] = 1
end

-- Whether a grounded screamer has come to rest; otherwise slides it a little towards the highest
-- nearby ground (a random one among equals) and returns false.
function settle_on_ground(c)
    local pos_x = c.pos_x
    local pos_y = c.pos_y
    local pos_z = c.pos_z

    local ground = ground_height(pos_x, pos_z)

    -- A downed screamer counts as settled at once: its targets are re-seeded every frame, so the
    -- checks below would never let it die.
    if c.health <= 0 then
        return true
    end

    if c.move_speed ~= 0 then
        return false
    end

    if pos_y < ground then
        return false
    end

    if c.fall_speed < 0 then
        return false
    end

    if c.rot_speed_x ~= 0 then
        return false
    end

    if c.rot_speed_y ~= 0 then
        return false
    end

    if c.rot_speed_z ~= 0 then
        return false
    end

    if c.angle_to_target_speed ~= 0 then
        return false
    end

    if c.air_screamer_push ~= 0 then
        return false
    end

    local mask = 0
    local count = 0
    local ring = 1
    local dir = 0

    local radius = 0x200
    while radius <= 0x800 do
        local highest = ground + (ring << 7)

        local i = 0
        dir = 0
        while i < 8 do
            local x = pos_x + q12_mul(radius, sh.math.sin(dir))
            local h = ground_height(x, q12_mul(radius, sh.math.cos(dir)) + pos_z)

            if highest < h then
                mask = 1 << i
                count = 1
                highest = h
            elseif h == highest then
                mask = mask | (1 << i)
                count = count + 1
            end

            i = i + 1
            dir = dir + 0x200
        end

        if count ~= 0 then
            local pick = q12_mul32(rand_q12(), count)

            i = 0
            while i < 8 do
                if mask & (1 << i) ~= 0 then
                    if pick == 0 then
                        break
                    end
                    pick = pick - 1
                end
                i = i + 1
            end

            dir = i << 9
            break
        end

        radius = radius + 0x800
        ring = ring + 1
    end

    if count == 0 then
        return true
    end

    S.flags = s32(S.flags | 0x20000000)
    local step = q12_mul(dt(), 2048)

    SETTLE_STEP.x = q12_mul(step, sh.math.sin(dir))
    SETTLE_STEP.y = step
    SETTLE_STEP.z = q12_mul(step, sh.math.cos(dir))

    shape_save(c)

    c.cylinder_radius = 0

    wall_detect(c, SETTLE_STEP)

    shape_restore(c)

    pos_x = pos_x + HIT.x
    pos_y = pos_y + HIT.y
    pos_z = pos_z + HIT.z

    ground = ground_height(pos_x, pos_z)

    pos_y = min(ground, pos_y)

    c.pos_x = pos_x
    c.pos_y = pos_y
    c.pos_z = pos_z

    return false
end


-- The control states by number.
CONTROL_STATES = {
    [0] = control_spawn_setup,
    [1] = control_scripted_intro,
    [2] = control_dying,
    [3] = control_stand_idle,
    [4] = control_walk_wander,
    [5] = control_walk_patrol,
    [6] = control_walk_search,
    [7] = control_walk_chase,
    [8] = control_walk_approach,
    [9] = control_feeding,
    [10] = control_ground_stalk,
    [11] = control_ground_back_off,
    [12] = control_ground_bite,
    [13] = control_ground_wander,
    [14] = control_ground_search,
    [15] = control_ground_settle,
    [16] = control_ground_recoil,
    [17] = control_ground_stun,
    [18] = control_hover_wander_far,
    [19] = control_hover_wander_near,
    [20] = control_hover_approach,
    [21] = control_hover_wander,
    [22] = control_hover_circle_player,
    [23] = control_hover_close_in,
    [24] = control_hover_back_off,
    [25] = control_hover_bite,
    [26] = control_hover_search,
    [27] = control_hover_reposition,
    [28] = control_hover_settle,
    [29] = control_hover_recoil,
    [30] = control_hover_injured,
    [31] = control_injured_flight,
    [32] = control_wall_bump_recover,
    [33] = control_glide_wander,
    [34] = control_glide_roam,
    [35] = control_glide_search,
    [36] = control_glide_approach,
    [37] = control_glide_attack_run,
    [38] = control_glide_dive,
    [39] = control_glide_pull_away,
    [40] = control_glide_scratch,
    [41] = control_glide_retreat,
    [42] = control_glide_to_target,
    [43] = control_glide_level_off,
    [44] = control_glide_recoil,
    [45] = control_glide_stun,
    [46] = control_cafe_hover_bite,
    [47] = control_hover_circle,
    [48] = control_hover_hold,
    [49] = control_hover_dive_attack,
    [50] = control_cafe_hover_recoil,
    [51] = control_hover_stun,
}
