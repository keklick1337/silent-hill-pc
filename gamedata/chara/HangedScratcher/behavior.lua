-- Hanged Scratcher (map5_s00 sewers, map6_s03; elsewhere when spawned): hangs from the ceiling (control
-- states 1-4), runs along it, drops onto Harry or when hit; on the ground it walks (5-7, 10), slashes (15),
-- falls, crawls and dies (8, 9, 11-13); 14 is the entrance of the ambush (jump from a wall, out of the
-- water, from the ceiling), 16 waits until woken.

local NAMES = {
    [0] = "still", "jump_down_from_wall", "emerge_from_water", "jump_down_from_ceiling",
    "drop_grab", "slash", "die_back", "die_crawl", "hit_hang", "stagger", "fall_off", "fall_to_crawl",
    "lie_back", "crawl", "hang", "stand_idle", "drop", "run_forward", "walk_forward", "die_back_hold",
    "die_crawl_hold", "knocked_back", "knocked_crawl", "knock_down", "hang_wall_pose",
    "hang_water_pose", "hang_ceiling_pose",
}
local A = {}
for i = 0, 26 do
    A[i] = sh.chara_anim("HangedScratcher", NAMES[i], i)
end

-- Packed status of clip `i`: blending in (false) or playing (true).
local function st(i, active)
    return A[i] * 2 + (active and 1 or 0)
end

local RUN, WALK = 17, 18

-- hanged_scratcher_flags
local F_AIRBORNE = 0x1
local F_DROP = 0x2          -- drops from above (out of its ceiling area, or knocked off)
local F_BODY_LEFT = 0x4     -- the dead body left its blood
local F_HIT = 0x10          -- took damage this tick
local F_ATTACKED = 0x20     -- attacked this tick
local F_ENTRANCE_STEP = 0x40
local F_RELEASED = 0x80     -- set by the map when the ambush starts
local F_ENTRANCE_ATTACK = 0x100
local CEILING_SLASH = sh.entity.attack_number("HangedScratcherCeilingSlash")
local SLASH = sh.entity.attack_number("HangedScratcherSlash")
local F_DEAD = 0x200

-- Record flags.
local CHARA_TARGETED = 0x2
local CHARA_HIT = 0x4
local CHARA_PLAYER_BUSY = 0x8
local CHARA_NO_RADIO_STATIC = 0x100

-- Group 3 of the NPC group flags: one scratcher is attacking Harry.
local GROUP = 3
local GROUP_ATTACKING = 0x2

local GROUND_WATER = 7

local DIFFICULTY_EASY, DIFFICULTY_NORMAL, DIFFICULTY_HARD = -1, 0, 1

-- Clip speed of the fall clips (fall_off, drop); shared by every scratcher of the map, as the
-- original's one variable.
local fall_clip_speed = 0

-- Collision keyframes (ten values each, see collision_keyframes in common.lua) in one list in the
-- original's memory order: some clips index past the end of their block into the next one.
local KF = {
    { 737, 1843, 1843, 1433, 2293, 696, -40, 1024, -40, 819 },
    { 1228, 2580, 2416, 2334, 2293, 696, 81, 1024, -40, 819 },
    { 2703, 4382, 3850, 3768, 2293, 778, 204, 696, -40, 819 },
    { 4628, 5857, 5857, 5775, 2293, 491, 614, 1433, -40, 819 },
    { 4710, 5939, 5939, 5857, 2293, 491, 327, 1474, -40, 819 },
    { 4628, 6021, 6021, 5939, 2293, 491, 122, 1351, -40, 819 },
    { 4628, 6062, 6062, 5980, 2293, 491, 0, 1064, -40, 819 },
    { 4628, 6103, 6103, 6021, 2293, 491, -163, 901, -40, 819 },
    { 4628, 6144, 6144, 6062, 2293, 491, -286, 614, -40, 819 },
    { 4628, 6184, 6184, 6103, 2293, 491, -450, 532, -40, 819 },
    { 4628, 6184, 6184, 6103, 2293, 491, -491, 491, -40, 819 },
    { 4628, 6184, 6184, 6103, 2293, 491, -532, 1024, -40, 819 },
    { 3522, 5406, 4505, 4423, 2293, 778, -163, 819, -40, 819 },
    { 2498, 3604, 3604, 3522, 2293, 819, -163, 942, -40, 819 },
    { 1679, 2785, 2785, 2703, 2293, 901, -204, 860, -40, 819 },
    { 1310, 2375, 2375, 2293, 2293, 901, -163, 860, -40, 819 },
    { 1228, 2088, 2088, 2007, 2293, 901, -163, 655, -40, 819 },
    { 983, 1761, 1761, 1679, 2293, 819, -204, 491, -40, 819 },
    { -1024, 0, 0, -983, 1392, 860, -122, 573, -163, 368 },
    { 737, 1843, 1843, 1638, 2293, 778, -163, 573, -40, 819 },
    { 819, 2211, 2007, 1925, 2293, 778, -409, 491, -40, 819 },
    { 819, 2580, 2088, 1925, 2293, 778, -450, 286, -40, 819 },
    { 819, 2211, 1966, 1802, 2293, 778, -368, 368, -40, 819 },
    { 819, 2048, 1925, 1761, 2293, 778, -368, 450, -40, 819 },
    { 819, 1925, 1843, 1720, 2293, 778, -327, 532, -40, 819 },
    { 819, 1843, 1843, 1720, 2293, 778, -163, 655, -40, 819 },
    { 737, 1843, 1843, 1638, 2293, 778, -163, 573, -40, 819 },
    { 819, 1925, 1925, 1720, 2129, 696, -286, 573, -245, 778 },
    { 1024, 2293, 2048, 1843, 2007, 655, -368, 573, -204, 696 },
    { 1024, 2580, 2048, 1843, 1925, 655, -532, 368, -286, 532 },
    { 819, 2293, 1843, 1638, 1925, 655, -532, 327, -327, 532 },
    { 450, 2088, 1720, 1433, 1843, 655, -491, 327, -450, 327 },
    { -163, 1720, 1024, 737, 1761, 655, -409, 327, -450, 327 },
    { -491, 1187, 573, 204, 1679, 655, -368, 327, -450, 327 },
    { -1146, 819, 163, -409, 1638, 655, -327, 327, -327, 327 },
    { -1351, 368, -163, -1024, 1556, 655, -327, 327, -327, 327 },
    { -1597, 0, -573, -1474, 1515, 655, -327, 327, -327, 327 },
    { -1269, 0, -204, -1187, 1474, 655, -163, 368, -163, 327 },
    { -1024, 0, 0, -942, 1474, 655, -163, 368, -163, 327 },
    { 737, 1884, 1843, 1433, 2293, 696, -40, 1024, -40, 819 },
    { 737, 1884, 1843, 1515, 2293, 655, -40, 983, -40, 614 },
    { 737, 1884, 1843, 1515, 2293, 655, -40, 983, -40, 614 },
    { 737, 1925, 1884, 1392, 2293, 655, 573, 983, -40, 614 },
    { 655, 1966, 1884, 1310, 2293, 655, 1024, 983, -40, 614 },
    { 286, 1802, 1433, 819, 2293, 655, 1392, 983, -40, 614 },
    { -245, 1310, 860, 122, 2293, 655, 1597, 983, -40, 614 },
    { -778, 983, 491, -368, 2293, 655, 1761, 983, -40, 614 },
    { -1187, 573, -122, -819, 2293, 655, 1802, 860, -40, 614 },
    { -1884, -40, -614, -1269, 2293, 655, 1802, 860, -40, 614 },
    { -2334, -409, -1269, -1884, 2293, 655, 1515, 860, -40, 614 },
    { -2867, -655, -1802, -2334, 2293, 655, 1351, 860, -40, 614 },
    { -3153, -778, -2170, -2334, 2211, 655, 614, 860, -40, 614 },
    { -3317, -409, -2457, -3112, 2088, 655, -40, 860, -40, 614 },
    { -3522, 0, -2580, -3276, 1966, 655, -286, 860, -245, 614 },
    { -3522, 0, -2580, -3276, 1802, 655, -286, 860, -245, 614 },
    { -3604, 0, -2744, -3358, 1679, 655, -204, 860, -245, 491 },
    { -3563, 0, -2621, -3399, 1556, 655, -204, 860, -163, 491 },
    { -3153, 0, -2211, -3031, 1556, 655, -122, 860, 40, 450 },
    { -2621, 0, -1638, -2457, 1556, 655, -40, 860, 40, 450 },
    { -2088, 0, -942, -1966, 1556, 655, -40, 860, 40, 450 },
    { -2088, 0, -819, -1966, 1556, 655, 40, 860, 40, 450 },
    { -2621, 0, -1638, -2457, 1556, 655, 122, 860, 163, 450 },
    { -2908, 0, -1761, -2744, 1556, 655, 122, 860, 163, 450 },
    { -1187, 0, 0, -1105, 1392, 860, 163, 573, 163, 737 },
    { -2826, 0, -1720, -2744, 1474, 737, 122, 655, 81, 737 },
    { -2662, 0, -1556, -2580, 1474, 737, 327, 491, 81, 737 },
    { -2621, 0, -1515, -2539, 1474, 737, 368, 450, 204, 532 },
    { -2580, 0, -1474, -2498, 1474, 737, 327, 532, 245, 532 },
    { -2580, 0, -1474, -2498, 1474, 737, 245, 573, 245, 532 },
    { -2826, 0, -1638, -2744, 1474, 737, 122, 614, 122, 573 },
    { -2826, 0, -1720, -2744, 1474, 737, 122, 655, 81, 737 },
    { -3031, 0, -1720, -2949, 1474, 737, 122, 655, 163, 573 },
    { -2621, 0, -1515, -2539, 1474, 737, 122, 655, 122, 573 },
    { -2170, 0, -983, -2088, 1474, 737, 122, 655, 163, 573 },
    { -1720, 0, -696, -1638, 1474, 737, 122, 655, 163, 573 },
    { -1515, 0, -450, -1433, 1474, 737, 122, 655, 163, 573 },
    { -1269, 0, -204, -1187, 1474, 737, 122, 655, 122, 573 },
    { -2826, 0, -1720, -2662, 1474, 737, 0, 819, 0, 860 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { -2826, 0, -1720, -2662, 1474, 737, 0, 819, 0, 860 },
    { -2949, 0, -1761, -2744, 1474, 737, 81, 901, 40, 942 },
    { -2990, 0, -1884, -2785, 1474, 737, 81, 901, 40, 942 },
    { -3194, 0, -2129, -3031, 1474, 737, 81, 901, 40, 942 },
    { -3358, 0, -2252, -3194, 1474, 737, -40, 983, 0, 1024 },
    { -3358, 0, -2252, -3194, 1474, 737, -40, 983, 0, 1024 },
    { -3481, 0, -2375, -3317, 1474, 655, 0, 1064, -81, 1064 },
    { -3481, 0, -2375, -3317, 1474, 655, 0, 1064, -81, 1064 },
    { -3358, 0, -2252, -3235, 1474, 655, 81, 1064, 0, 1064 },
    { -3358, 0, -2252, -3235, 1474, 655, 81, 1064, 0, 1064 },
    { -3235, 0, -2129, -3112, 1474, 655, 81, 1064, 0, 1064 },
    { -3112, 0, -2007, -2990, 1474, 696, 81, 1024, 0, 1064 },
    { -3031, 0, -1802, -2908, 1474, 737, 81, 942, 40, 1064 },
    { -2908, 0, -1761, -2785, 1474, 737, 81, 901, 40, 942 },
    { -2908, 0, -1761, -2785, 1474, 737, 81, 901, 40, 901 },
    { -2908, 0, -1761, -2785, 1474, 737, 81, 901, 40, 860 },
    { -2908, 0, -1761, -2785, 1474, 737, 81, 901, 40, 860 },
    { -2908, 0, -1761, -2785, 1474, 737, 81, 819, 40, 860 },
    { -2908, 0, -1761, -2785, 1474, 737, 81, 778, 40, 860 },
    { -2908, 0, -1761, -2785, 1474, 737, 122, 737, 40, 860 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
}
local K_ATTACK = 0         -- 17, drop_grab
local K_ATTACK_END = 17
local K_LANDED = 18
local K_HANG_HIT = 19      -- 6, hit_hang
local K_HANG_HIT_END = 25
local K_FALL = 26          -- 12, fall_off
local K_FALL_END = 38
local K_HANG = 39
local K_DROP = 40          -- 22, drop
local K_DROP_END = 62
local K_LANDED_BACK = 63
local K_HIT = 64           -- 5, stagger / knock_down
local K_HIT_END = 69
local K_CRAWL = 70         -- 6, fall_to_crawl
local K_CRAWL_END = 76
local K_STAND = 77
local K_NONE = 78
local K_SLASH = 79         -- 20, slash

local slot, c, P, p, dt

-- The properties block (hanged_scratcher_* record fields) by short name.
local props_meta = {
    __index = function(t, k)
        return sh.entity.get(rawget(t, "slot"), "hanged_scratcher_" .. k)
    end,
    __newindex = function(t, k, v)
        sh.entity.set(rawget(t, "slot"), "hanged_scratcher_" .. k, v)
    end,
}

local function sfx_id(name, fallback)
    return sound(c, name, fallback)
end

-- The step sound of the floor from the character's footsteps table (metal grating and the rest).
local function step_sfx(ground_type)
    return sh.entity.footstep_sound(slot, ground_type, 1581)
end

local function key()
    return asr(c.anim_time, 12)
end

local function key_in(lo, hi)
    local k = asr(c.anim_time, 12)
    return k >= lo and k <= hi
end

local function clip_of_status()
    return c.anim_status >> 1
end

local function anim_set(status, keyframe)
    c.anim_status = status
    c.anim_time = keyframe * 4096
    c.anim_keyframe = keyframe
end

local function player_distance()
    return distance_2d(p.pos_x - c.pos_x, p.pos_z - c.pos_z)
end

local function player_angle()
    return heading_to(c.pos_x, c.pos_z, p.pos_x, p.pos_z)
end

local function player_angle_delta()
    return angle_signed(player_angle() - c.rot_y)
end

local function turn_step(shift)
    return asr(dt, shift) + 1
end

local function turn_towards(delta, rate)
    if delta > 0 then
        c.rot_y = c.rot_y + q12_mul(dt, rate)
    else
        c.rot_y = c.rot_y - q12_mul(dt, rate)
    end
end

local function group_set(on)
    local g = sh.entity.sys("chara_group_flags", GROUP)
    sh.entity.set_sys("chara_group_flags", on and (g | GROUP_ATTACKING) or (g & ~GROUP_ATTACKING), GROUP)
end

local function group_attacking()
    return sh.entity.sys("chara_group_flags", GROUP) & GROUP_ATTACKING ~= 0
end

local function set_control(state, status)
    c.control_state = state
    if status then
        c.anim_status = status
    end
end

-- Map data (chara/fallbacks.lua). The scratcher's code knows the sewers of map5_s00 and map6_s03: their lighting
-- (the flashlight is always allowed there), the height of their ceiling and the areas it covers. On another map
-- (a spawn of the engine) it sees Harry as in the lit sewers when the map has no flashlight, hangs 3.6 m above the
-- floor it was spawned on, and the ceiling covers the floor of that height within CEILING_REACH of its spawn point.
local HOME_MAPS = { map5_s00 = true, map6_s03 = true }
local CEILING_Y = -14745             -- Q12(-3.6)
local CEILING_REACH = 40960          -- Q12(10.0)
local CEILING_STEP = 2048            -- Q12(0.5): floor height change that ends the ceiling

local function player_seen_lit(dark_range, dark_cone, lit_range, lit_cone)
    local env = sh.entity.sys("map_effects")
    return ((env & 3) == 2 and sh.entity.player_visible(slot, 1, dark_range, dark_cone) ~= 0)
        or ((env & 2) ~= 0 and (env & 1) ~= 0 and sh.entity.player_visible(slot, 1, lit_range, lit_cone) ~= 0)
end

-- Where the engine spawn stood when spawned: x, z and the floor height under it, per slot.
local spawn_floor = {}

fallbacks = {
    player_seen = function(_, dark_range, dark_cone, lit_range, lit_cone)
        if sh.entity.sys("map_effects") & 2 ~= 0 then
            return player_seen_lit(dark_range, dark_cone, lit_range, lit_cone)
        end
        return sh.entity.player_visible(slot, 1, dark_range, dark_cone) ~= 0
    end,
    ceiling_y = function(cc)
        local floor = sh.entity.ground_at(cc.pos_x, cc.pos_z)
        spawn_floor[cc.slot] = { cc.pos_x, cc.pos_z, floor }
        return floor + CEILING_Y
    end,
    out_of_ceiling = function(cc)
        local home = spawn_floor[cc.slot]
        if home == nil then
            local floor = sh.entity.ground_at(P.spawn_x, P.spawn_z)
            home = { P.spawn_x, P.spawn_z, floor }
            spawn_floor[cc.slot] = home
        end
        if distance_2d(cc.pos_x - home[1], cc.pos_z - home[2]) > CEILING_REACH then
            return true
        end
        local floor = sh.entity.ground_at(cc.pos_x, cc.pos_z)
        return math.abs(floor - home[3]) > CEILING_STEP
    end,
}

-- Harry is seen in the light: in the dark with the flashlight, or lit up close.
local function player_seen(dark_range, dark_cone, lit_range, lit_cone)
    return map_area_data(c, "player_seen", HOME_MAPS, player_seen_lit, dark_range, dark_cone, lit_range, lit_cone)
end

-- Out of the area the ceiling covers (the scratcher must drop down).
local function sewer_ceiling_gone()
    local x, z = c.pos_x, c.pos_z
    if sh.map_name() == "map5_s00" then
        if x > -163840 then
            return z > -36864
        elseif x > -327680 then
            if x <= -233472 and x >= -253952 then
                if z > -40960 then
                    return true
                end
                return z < -57344
            end
        elseif z <= -364544 then
            return x >= -364544
        end
        return false
    end
    if x < 0 then
        if z < 155648 then
            return true
        end
        if z < 233472 then
            return false
        end
        if z <= 258048 then
            if x >= -90112 then
                return x > -73728
            end
            return true
        end
        return false
    elseif z > 163840 then
        if z <= 258048 and z >= 233472 then
            if x >= 73728 then
                return x > 90112
            end
            return true
        end
        return false
    elseif z <= 94208 then
        if z >= 69632 then
            if x >= 73728 then
                return x > 90112
            end
            return true
        end
        return false
    end
    return false
end

local function out_of_ceiling()
    local _, ground_type = sh.entity.ground_at(c.pos_x, c.pos_z)
    if ground_type == GROUND_WATER then
        return false
    end
    return map_area_data(c, "out_of_ceiling", HOME_MAPS, sewer_ceiling_gone)
end

local function init()
    spawn_floor[slot] = nil
    P.flags = 0
    sh.entity.props_clear(slot)
    c.health = sh.chara_param_q12(c.chara_id, "health", 1433600)
    local difficulty = sh.save.game_difficulty()
    if difficulty == DIFFICULTY_HARD then
        c.health = q12_mul(c.health, sh.chara_param_q12(c.chara_id, "health_hard_scale", 5529))
    end
    c.move_speed = 0
    c.fall_speed = 0
    c.heading_angle = c.rot_y
    c.rot_x = 0
    sh.entity.clip_table_attach(slot, 0)
    damage_clear(c)

    local step = c.state_step
    if step == 3 or step == 9 then
        c.control_state = step == 3 and 2 or 1
        anim_set(st(14, true), 222)
        c.pos_y = map_area_data(c, "ceiling_y", HOME_MAPS, function() return CEILING_Y end)
        c.rot_z = 2048
    elseif step == 7 or step == 14 then
        c.control_state = 5
        anim_set(st(15, true), 232)
        c.pos_y = 0
        c.rot_z = 0
    elseif step == 17 or step == 6 then
        if step == 17 then
            P.timer = 4915
        end
        c.control_state = 6
        anim_set(st(15, true), 232)
        c.pos_y = 0
        c.rot_z = 0
    elseif step >= 18 and step <= 20 then
        c.control_state = 14
        c.pos_y = 0
        c.rot_z = 0
        if step == 18 then
            anim_set(st(1, true), 0)
        elseif step == 19 then
            anim_set(st(2, true), 51)
        else
            anim_set(st(3, true), 87)
        end
    end

    P.spawn_x = c.pos_x
    P.spawn_z = c.pos_z
    local run_max, walk_max
    if difficulty == DIFFICULTY_EASY then
        run_max, walk_max = 11059, 9830
    elseif difficulty == DIFFICULTY_NORMAL then
        run_max, walk_max = 12288, 11468
    else
        run_max, walk_max = 15564, 14745
    end
    P.run_speed = sh.rng.rand16() % (run_max >> 2) + ((run_max * 3) >> 2)
    P.walk_speed = sh.rng.rand16() % (walk_max >> 2) + ((walk_max * 3) >> 2)
    c.flags = c.flags | CHARA_NO_RADIO_STATIC | CHARA_HIT
end

local function damage_update()
    if c.damage_amount > 0 and c.health > 0 then
        c.health = math.max(0, c.health - c.damage_amount)
        c.damage_x = c.damage_x + asr(s32(c.move_speed * sh.math.sin(c.heading_angle)), 14)
        c.damage_z = c.damage_z + asr(s32(c.move_speed * sh.math.cos(c.heading_angle)), 14)

        local prev_speed = c.move_speed
        c.move_speed = idiv(s32(distance_2d(c.damage_x, c.damage_z) << 12), 16384)
        local prev_heading = c.heading_angle
        c.heading_angle = sh.math.atan2(c.damage_x, c.damage_z)

        local state = c.control_state
        if state == 4 or state == 1 or state == 2 or state == 3 or state == 9 then
            if state == 4 then
                group_set(false)
            end
            local _, ground_type = sh.entity.ground_at(c.pos_x, c.pos_z)
            -- Over water a killing hit leaves it at 1 health, and mostly does not knock it off.
            if c.health == 0 and ground_type == GROUND_WATER then
                c.health = 1
                if sh.rng.uint(0, 15) < 12 then
                    c.move_speed = prev_speed
                    c.heading_angle = prev_heading
                    damage_clear(c)
                    return
                end
            end
            if math.abs(c.pos_x - p.pos_x) > 819 and math.abs(c.pos_z - p.pos_z) > 819 then
                if ground_type ~= GROUND_WATER and (sh.rng.rand16() & 0xF) < 4 then
                    P.flags = P.flags | F_DROP
                end
            end
            set_control(9, st(8, false))
        elseif state == 15 or state == 5 or state == 6 or state == 7 or state == 10 then
            if state == 15 then
                group_set(false)
            end
            if c.health > 409600 then
                set_control(10, st(9, false))
            else
                set_control(12, st(23, false))
            end
        elseif state == 11 or state == 8 then
            if state == 11 and c.anim_status == st(12, true) then
                c.anim_status = st(21, false)
            end
            c.fall_speed = c.fall_speed + idiv(s32(c.damage_y << 12), 16384)
        elseif state == 12 then
            if c.anim_status == st(13, true) then
                c.anim_status = st(22, false)
            end
        end

        P.timer = 0
        P.flags = P.flags | F_HIT
    end
    damage_clear(c)
end

local control = {}

-- Hanging, waiting for Harry right below.
control[1] = function()
    if player_distance() < 4096 and sh.entity.player_blocked(slot) == 0
        and player_seen(8192, 6144, 6144, 1228) then
        set_control(3, st(RUN, false))
    end
end

-- Hanging, slowing down until Harry comes near.
control[2] = function()
    local dist = player_distance()
    speed_toward(c, 16384, 0)
    if dist < 20480 or (dist < 81920 and sh.rng.rand16() == 0) then
        set_control(3, st(RUN, false))
    end
end

-- Running along the ceiling after Harry.
control[3] = function()
    local dist = player_distance()
    local delta = math.abs(player_angle_delta())

    if delta < turn_step(2) then
        speed_toward(c, 16384, P.run_speed)
        P.timer = 0
    else
        if delta < 682 and dist > 8192 then
            speed_toward(c, 16384, P.run_speed)
        elseif delta < 1365 and dist > 16384 then
            speed_toward(c, 16384, P.run_speed // 2)
        else
            speed_toward(c, 16384, 0)
        end

        P.timer = P.timer + dt
        if dist > 30720 and P.timer > 7372 then
            P.target_heading = sh.entity.heading_toward(slot, 20480, p.pos_x, p.pos_z, 4096, 1)
            if P.target_heading == 4096 then
                P.target_heading = c.rot_y
            end
            P.timer = 0
        elseif dist > 12288 and P.timer > 2457 then
            P.target_heading = sh.entity.heading_toward(slot, 6144, p.pos_x, p.pos_z, 4096, 1)
            if P.target_heading == 4096 then
                P.target_heading = c.rot_y
            end
            P.timer = 0
        else
            P.target_heading = player_angle()
            P.timer = 0
        end

        local turn = angle_signed(P.target_heading - c.rot_y)
        if math.abs(turn) > turn_step(2) then
            turn_towards(turn, 2048)
        end
    end

    if out_of_ceiling() then
        set_control(8, st(16, false))
        c.move_speed = 0
        P.flags = P.flags | F_DROP
        return
    end
    if p.flags & CHARA_PLAYER_BUSY == 0 and not group_attacking() then
        if p.health > 0 and dist < 4096 and delta < 341 then
            local dy = c.pos_y - p.pos_y
            if dy < -14336 and dy > -15155 then
                set_control(4, st(4, false))
                P.timer = 0
                group_set(true)
                return
            end
        end
    end
    if dist > 81920 and sh.rng.int(0, 31) == 0 then
        set_control(2, st(14, false))
    end
end

-- Grabbing Harry from above.
control[4] = function()
    local dist = player_distance()
    local delta = player_angle_delta()

    if key_in(98, 103) then
        if math.abs(delta) > turn_step(2) then
            turn_towards(delta, 2048)
        end
        c.move_speed = dist > 819 and 4096 or 0
        c.attack_window = 1
        return
    end
    c.move_speed = 0
    if key_in(104, 105) then
        local y = -5939
        P.flags = P.flags | F_ATTACKED
        sh.entity.attack(slot, 1, CEILING_SLASH, c.pos_x, y, c.pos_z, 0, c.rot_y,
            sh.math.atan2(dist, (p.pos_y + p.box_offset_y) - y))
    elseif clip_of_status() == A[14] then
        if dist > 24576 and sh.rng.int(0, 15) == 0 then
            c.control_state = 3
            P.timer = 0
            c.anim_status = st(RUN, false)
            group_set(false)
        else
            P.timer = P.timer + dt
            if P.timer > 12288 and sh.rng.int(0, 7) == 0 then
                P.timer = 0
                c.control_state = 2
                group_set(false)
            end
        end
    end
end

-- Standing still until Harry is seen.
control[5] = function()
    local x, y, z = c.pos_x, c.pos_y, c.pos_z
    if sh.entity.aabb_visible(x - 2048, x + 2048, y - 4096, y, z - 2048, z + 2048) ~= 0 or player_distance() < 4096 then
        if sh.entity.player_blocked(slot) ~= 0 then
            return
        end
        if player_seen(6144, 2048, 4915, 1228) then
            set_control(7, st(WALK, false))
        end
    end
end

-- Standing, now and then starting to walk to Harry.
control[6] = function()
    local dist = player_distance()
    speed_toward(c, 16384, 0)
    if P.timer == 0 and c.anim_status == st(15, true) then
        if dist < 49152 and sh.rng.int(0, 127) == 0 then
            set_control(7, st(WALK, false))
        elseif dist < 24576 and sh.rng.int(0, 15) == 0 then
            set_control(7, st(WALK, false))
        end
    end
    P.timer = math.max(0, P.timer - dt)
end

-- Walking to Harry.
control[7] = function()
    local dist = player_distance()
    local delta = math.abs(player_angle_delta())
    local angle = player_angle()

    if dist > 65536 then
        c.control_state = 6
        P.timer = 2048
        c.anim_status = st(15, false)
        P.collision_clip = 37
        return
    end

    local blocked = sh.entity.player_blocked(slot) ~= 0
    if delta < turn_step(3) and not blocked then
        speed_toward(c, 8192, P.walk_speed)
        P.timer = 0
    else
        if not blocked then
            if delta < 512 and dist > 8192 then
                speed_toward(c, 8192, P.walk_speed)
            elseif delta < 1024 and dist > 16384 then
                speed_toward(c, 8192, P.walk_speed // 2)
            else
                speed_toward(c, 16384, 0)
            end
        else
            speed_toward(c, 8192, P.walk_speed)
        end

        P.timer = P.timer + dt
        if dist < 8192 and not blocked then
            P.timer = 0
            P.target_heading = angle
        elseif P.timer > 2457 or (P.timer > 1228 and blocked and sh.rng.int(0, 7) == 0) then
            P.timer = 0
            P.target_heading = sh.entity.heading_toward(slot, 4096, p.pos_x, p.pos_z, 4096, 1)
            if P.target_heading == 4096 then
                P.target_heading = c.rot_y - 2048
            end
        end

        local turn = angle_signed(P.target_heading - c.rot_y)
        if math.abs(turn) > turn_step(3) then
            turn_towards(turn, 1024)
        else
            c.rot_y = P.target_heading
        end
    end

    if not blocked and not group_attacking() and p.flags & CHARA_PLAYER_BUSY == 0 and p.health > 0 then
        if player_distance() < 4915 and math.abs(player_angle_delta()) < 227 then
            c.control_state = 15
            group_set(true)
            P.timer = 0
            c.anim_status = st(5, false)
        end
    end
end

-- Dropping from the ceiling.
control[8] = function()
    if P.flags & F_AIRBORNE ~= 0 then
        c.fall_speed = c.fall_speed + asr(sh.game.gravity_speed(), 1)
        fall_clip_speed = key() == 259 and 0 or 61440
    else
        fall_clip_speed = 49152
    end
    if key_in(245, 256) then
        c.rot_z = math.max(0, c.rot_z - idiv(45 * dt, 68))
    end
    if clip_of_status() == A[15] then
        c.move_speed = 0
        if c.health > 409600 then
            c.control_state = 6
            P.timer = 2048
        else
            set_control(12, st(11, false))
            P.collision_clip = 31
        end
    end
end

-- Hit while hanging.
control[9] = function()
    speed_toward(c, 16384, 0)
    if clip_of_status() == A[14] and c.move_speed == 0 then
        local _, ground_type = sh.entity.ground_at(c.pos_x, c.pos_z)
        if c.health > 409600 or ground_type == GROUND_WATER then
            if P.flags & F_DROP ~= 0 then
                set_control(8, st(16, false))
            elseif sh.rng.int(0, 7) == 0 then
                set_control(3, st(RUN, false))
            end
        else
            set_control(11, st(10, false))
        end
    end
end

-- Hit while standing.
control[10] = function()
    speed_toward(c, 16384, 0)
    if clip_of_status() == A[15] and c.move_speed == 0 and sh.rng.int(0, 7) == 0 then
        set_control(7, st(WALK, false))
    end
end

-- Falling off the ceiling, then down on its back.
control[11] = function()
    if P.flags & F_AIRBORNE ~= 0 then
        c.fall_speed = c.fall_speed + asr(sh.game.gravity_speed(), 1)
        fall_clip_speed = key_in(176, 184) and 40960 or 0
    else
        speed_toward(c, 16384, 0)
        if clip_of_status() == A[10] then
            if key_in(176, 185) then
                c.anim_time = 186 * 4096
                c.anim_keyframe = 186
            end
            fall_clip_speed = 32768
        end
    end

    if key_in(191, 193) then
        anim_set(st(12, true), 202)
        c.rot_z = 0
        return
    end
    if c.anim_status == st(12, true) then
        c.flags = c.flags | CHARA_TARGETED
        if c.health <= 0 then
            set_control(13, st(6, false))
        end
    end
end

-- Crawling on the ground.
control[12] = function()
    speed_toward(c, 16384, 0)
    if key_in(192, 199) then
        local duration = sh.entity.clip_duration(slot, c.anim_status)
        local scaled = idiv(s32(q12_mul(duration, dt) * 2867), 28672)
        P.offset_x = P.offset_x + asr(s32(scaled * sh.math.sin(c.rot_y)), 12)
        P.offset_z = P.offset_z + asr(s32(scaled * sh.math.cos(c.rot_y)), 12)
    end
    if c.anim_status == st(13, true) then
        c.flags = c.flags | CHARA_TARGETED
        if c.health <= 0 then
            set_control(13, st(7, false))
        end
    end
end

-- Dying.
control[13] = function()
    speed_toward(c, 16384, 0)
    if c.health == 0 then
        if sh.entity.sys("target_npc") ~= sh.entity.npc_index(slot) then
            c.health = -1
            c.flags = c.flags & ~CHARA_TARGETED
            sh.entity.set_collision_state(slot, 0)
            P.flags = P.flags | F_DEAD
        end
    end
    if c.move_speed == 0 and P.flags & (F_AIRBORNE | F_BODY_LEFT) == 0 then
        sh.entity.anim_status_set(slot, st(4, false), 8)
        P.flags = P.flags | F_BODY_LEFT
        sh.entity.enemy_defeated(slot)
    end
end

-- The ambush entrance.
control[14] = function()
    local status = c.anim_status
    if P.flags & F_RELEASED ~= 0 and status < st(4, false) then
        sh.entity.clip_duration_set(slot, status, 39321)
    else
        sh.entity.clip_duration_set(slot, status, 0)
    end

    if key() == 51 then
        c.anim_flags = c.anim_flags & ~0x2
    else
        c.anim_flags = c.anim_flags | 0x2
    end

    local x, y, z = c.pos_x, c.pos_y, c.pos_z
    if P.flags & F_ENTRANCE_STEP == 0 then
        if key_in(42, 44) then
            P.flags = P.flags | F_ENTRANCE_STEP
            local _, ground_type = sh.entity.ground_at(x, z)
            sh.entity.sfx_flags(slot, step_sfx(ground_type), 128, 0)
        end
        if key_in(84, 86) then
            P.flags = P.flags | F_ENTRANCE_STEP
            local _, ground_type = sh.entity.ground_at(x, z + 2457)
            sh.entity.sfx_flags_at(step_sfx(ground_type), x, y, z + 2457, 128, 0)
        end
        if key_in(94, 96) then
            P.flags = P.flags | F_ENTRANCE_STEP
            local _, ground_type = sh.entity.ground_at(x, z - 4096)
            sh.entity.sfx_flags_at(step_sfx(ground_type), x, y, z - 4096, 128, 0)
        end
    end

    if P.flags & F_ENTRANCE_ATTACK == 0 and key_in(52, 54) then
        P.flags = P.flags | F_ENTRANCE_ATTACK
        sh.entity.sfx_flags_at(sfx_id("attack", 1577), x, y + 2048, z + 6144, 128, 0)
    end
end

-- Slashing at Harry on the ground.
control[15] = function()
    local dist = player_distance()
    local delta = player_angle_delta()

    if key_in(116, 123) and dist > 4096 and delta < 227 then
        c.move_speed = 4096
    else
        c.move_speed = 0
    end

    if key_in(116, 128) then
        if math.abs(delta) > turn_step(4) then
            turn_towards(delta, 512)
        else
            c.rot_y = sh.math.atan2(p.pos_x - c.pos_x, p.pos_z - c.pos_z)
        end
    end

    if key_in(124, 130) then
        if c.attack_window == 0 then
            c.attack_window = 1
        end
        P.flags = P.flags | F_ATTACKED
        sh.entity.attack(slot, 1, SLASH, c.pos_x, c.pos_y - 3276, c.pos_z, 0, c.rot_y, 1024)
    end

    if c.anim_status == st(15, true) then
        if P.timer == 0 then
            group_set(false)
        end
        P.timer = P.timer + dt
        if P.timer > 10240 or (P.timer > 3276 and sh.rng.int(0, 31) == 0) then
            set_control(7, st(WALK, false))
        end
    else
        P.timer = 0
    end
end

-- Waiting until woken.
control[16] = function()
    if P.wake ~= 0 then
        set_control(7, st(WALK, false))
    end
end

local FALLING = { [5] = true, [6] = true, [7] = true, [10] = true, [12] = true, [13] = true }

local function move_update()
    c.rot_y = angle_signed(c.rot_y)
    c.heading_angle = c.rot_y
    if FALLING[c.control_state] then
        c.fall_speed = c.fall_speed + sh.game.gravity_speed()
    end

    local _, _, _, _, ground = sh.entity.move(slot, P.offset_x, P.offset_z)
    P.offset_z = 0
    P.offset_x = 0

    local was_airborne = P.flags & F_AIRBORNE
    if c.pos_y == ground then
        P.flags = P.flags & ~F_AIRBORNE
        if was_airborne ~= 0 then
            local status = c.anim_status
            if status == st(10, true) or status == st(16, true) then
                local _, ground_type = sh.entity.ground_at(c.pos_x, c.pos_z)
                sh.entity.sfx_flags(slot, step_sfx(ground_type), status == st(10, true) and 128 or 64, 0)
            end
        end
    else
        P.flags = P.flags | F_AIRBORNE
    end
end

local function anim_update()
    c.rot_y = angle_signed(c.rot_y)
    local status = c.anim_status
    local duration
    local difficulty = sh.save.game_difficulty()
    if status == st(4, true) then
        duration = (difficulty == DIFFICULTY_NORMAL or difficulty == DIFFICULTY_EASY) and 79872 or 106496
    elseif status == st(5, false) then
        duration = difficulty == DIFFICULTY_NORMAL and 49152 or difficulty == DIFFICULTY_EASY and 32768 or 73728
    elseif status == st(5, true) then
        duration = difficulty == DIFFICULTY_NORMAL and 94208 or difficulty == DIFFICULTY_EASY and 61440 or 139264
    elseif status == st(9, true) then
        duration = key() ~= 173 and 49152 or 18432
    elseif status == st(23, true) then
        duration = key() == 173 and 12288 or 36864
    elseif status == st(11, true) then
        duration = key_in(192, 193) and 10240 or 18432
    elseif status == st(10, true) or status == st(16, true) then
        duration = fall_clip_speed
    elseif status == st(14, true) or status == st(15, true) then
        local state = c.control_state
        duration = state == 1 and 4915 or state == 5 and 9830 or 14336
    elseif status == st(RUN, true) then
        duration = math.max(8192, q12_mul(c.move_speed, 18022))
    elseif status == st(WALK, true) then
        duration = q12_mul(c.move_speed, 24576)
        if c.rot_y ~= P.prev_rot_y then
            duration = math.max(36864, duration)
        end
    end
    if duration then
        sh.entity.clip_duration_set(slot, status, duration)
    end

    sh.entity.root_transform(slot)
    sh.entity.anim_step(slot)
    P.prev_rot_y = c.rot_y
end

local function kf(i)
    return KF[i + 1]
end

local function blend(i0, i1)
    collision_between(c, kf(i0), kf(i1))
end

local function shape(i)
    collision_from_keyframe(c, kf(i))
end

local function b(x)
    return x and 1 or 0
end

-- The collision shapes of the current clip; the playing clips blend between their keyframes.
local collision = {}
local function on(statuses, fn)
    for _, s in ipairs(statuses) do
        collision[s] = fn
    end
end

local function build_collision()
    on({ st(4, false) }, function() blend(K_HANG, K_ATTACK) end)
    on({ st(4, true) }, function()
        P.collision_clip = 9
        local rel = key() - 98
        blend(K_ATTACK + rel, K_ATTACK + rel + 1)
    end)
    on({ st(10, false) }, function() blend(K_HANG, K_FALL) end)
    on({ st(10, true) }, function()
        P.collision_clip = 21
        local rel = key() - 176
        local k0 = rel - b(rel >= 12) - b(rel >= 14)
        local k1 = rel + 1 - b(rel >= 11) - b(rel >= 13) - b(rel >= 14)
        blend(K_FALL + k0, K_FALL + k1)
    end)
    on({ st(12, false) }, function()
        if P.collision_clip == 21 then
            blend(K_FALL_END, K_LANDED)
        else
            shape(K_LANDED)
        end
    end)
    on({ st(21, true) }, function()
        P.collision_clip = 43
        shape(K_LANDED)
    end)
    on({ st(6, false), st(6, true), st(12, true), st(19, false), st(19, true), st(21, false) },
        function() shape(K_LANDED) end)
    on({ st(8, false) }, function() shape(K_HANG_HIT) end)
    on({ st(8, true) }, function()
        P.collision_clip = 17
        local k = key()
        local rel = k - 154
        local k0, k1
        if rel < 7 then
            if rel < 3 then
                k0 = rel == 2 and 1 or 0
            else
                k0 = 2
            end
        else
            k0 = k - 158
        end
        if rel < 6 then
            k1 = rel >= 3 and 2 or rel
        else
            k1 = (rel - 3) - b(rel == 10)
        end
        blend(K_HANG_HIT + k0, K_HANG_HIT + k1)
    end)
    on({ st(14, false) }, function()
        if P.collision_clip == 9 then
            blend(K_ATTACK_END, K_HANG)
        elseif P.collision_clip == 17 then
            blend(K_HANG_HIT_END, K_HANG)
        else
            shape(K_HANG)
        end
    end)
    on({ st(RUN, true) }, function()
        P.collision_clip = 35
        shape(K_HANG)
    end)
    on({ st(14, true), st(RUN, false) }, function() shape(K_HANG) end)
    on({ st(16, false) }, function() blend(K_HANG, K_DROP) end)
    on({ st(16, true) }, function()
        P.collision_clip = 33
        local rel = key() - 244
        blend(K_DROP + rel, K_DROP + rel + 1)
    end)
    on({ st(13, false) }, function()
        if P.collision_clip == 23 then
            blend(K_CRAWL_END, K_LANDED_BACK)
        else
            shape(K_LANDED_BACK)
        end
    end)
    on({ st(22, true) }, function()
        P.collision_clip = 45
        shape(K_LANDED_BACK)
    end)
    on({ st(7, false), st(7, true), st(13, true), st(20, false), st(20, true), st(22, false) },
        function() shape(K_LANDED_BACK) end)
    on({ st(9, false), st(23, false) }, function() shape(K_HIT) end)
    on({ st(9, true), st(23, true) }, function()
        P.collision_clip = 19
        local rel = key() - 166
        local k0 = rel - b(rel > 0) - b(rel >= 4) - b(rel >= 5)
        local k1 = rel - b(rel >= 3) - b(rel >= 4) - b(rel >= 8)
        blend(K_HIT + k0, K_HIT + k1)
    end)
    on({ st(11, false) }, function()
        if P.collision_clip == 31 then
            blend(K_STAND, K_CRAWL)
        else
            blend(K_HIT_END, K_CRAWL)
        end
    end)
    on({ st(11, true) }, function()
        P.collision_clip = 23
        local rel = key() - 192
        local k0 = rel - b(rel >= 2) - b(rel >= 8)
        local k1 = rel + 1 - b(rel > 0) - b(rel >= 7) - b(rel >= 8)
        blend(K_CRAWL + k0, K_CRAWL + k1)
    end)
    on({ st(15, false) }, function()
        if P.collision_clip == 33 then
            blend(K_DROP_END, K_STAND)
        elseif P.collision_clip == 19 then
            blend(K_HIT_END, K_STAND)
        else
            shape(K_STAND)
        end
    end)
    on({ st(15, true), st(WALK, false), st(WALK, true) }, function() shape(K_STAND) end)
    on({ st(1, false), st(1, true), st(2, false), st(2, true), st(3, false), st(3, true), st(24, false),
         st(24, true), st(25, false), st(25, true), st(26, false), st(26, true) }, function() shape(K_NONE) end)
    on({ st(5, false) }, function() shape(K_SLASH) end)
    on({ st(5, true) }, function()
        local rel = key() - 116
        blend(K_SLASH + rel, K_SLASH + rel + 1)
    end)
end
build_collision()

local function collision_update()
    local fn = collision[c.anim_status]
    if fn then
        fn()
    end
    sh.entity.shape_offsets_update(slot)

    if P.flags & F_DEAD == 0 then
        if c.flags & CHARA_TARGETED == 0 and c.health > 0 and P.flags & F_DROP ~= 0 and P.flags & F_AIRBORNE ~= 0 then
            sh.entity.set_collision_state(slot, 4)
        else
            sh.entity.set_collision_state(slot, 2)
        end
    end
end

local function idle_cry(chance_max, unsigned, min_distance, timer_max)
    if P.idle_timer == 0 then
        local roll = unsigned and sh.rng.uint(0, chance_max) or sh.rng.int(0, chance_max)
        if roll == 0 and player_distance() > min_distance then
            P.idle_timer = sh.rng.int(14336, timer_max)
            sh.entity.sfx_flags(slot, sfx_id("idle", 1583), 128, 0)
        end
    else
        P.idle_timer = P.idle_timer - dt
        if P.idle_timer < 0 then
            P.idle_timer = 0
        end
    end
end

local function scrape(vol, divisor, base)
    local pitch = sh.rng.uint(0, 15) + asr(idiv(s32(c.move_speed << 12), divisor), 5) + base
    sh.entity.sfx_pitched(slot, sfx_id("scrape", 1578), vol, pitch)
end

local function sound_update()
    local vocal = P.vocal_timer
    if vocal ~= 0 then
        if vocal > 0 then
            P.vocal_timer = vocal - dt
            if P.vocal_timer <= 0 then
                P.vocal_timer = 0
            end
        else
            P.vocal_timer = vocal + dt
            if P.vocal_timer >= 0 then
                P.vocal_timer = 3072
                sh.entity.sfx_flags(slot, sfx_id("vocal", 1580), 128, 0)
            end
        end
    else
        if P.flags & F_ATTACKED ~= 0 then
            if P.flags & F_AIRBORNE ~= 0 then
                sh.entity.sfx_flags(slot, sfx_id("damage_airborne", 1584), 128, 0)
            else
                sh.entity.sfx_flags(slot, sfx_id("damage", 1579), 128, 0)
            end
            P.vocal_timer = 2048
        elseif P.flags & F_HIT ~= 0 then
            P.vocal_timer = -1228
        end

        local state = c.control_state
        if state == 1 or state == 16 then
            idle_cry(511, false, 26624, 32767)
        elseif state == 2 or state == 3 or state == 5 or state == 6 or state == 7 then
            idle_cry(255, true, 18432, 30719)
        end
    end

    P.flags = P.flags & ~(F_HIT | F_ATTACKED)

    local status = c.anim_status
    if status == st(RUN, true) then
        if key_in(272, 273) then
            if P.scrape_count == 0 then
                scrape(32, 12288, 88)
                P.scrape_count = 1
            end
        else
            P.scrape_count = 0
        end
        local k = key()
        if k == 276 or k == 267 then
            if P.scrape_latch == 0 then
                scrape(32, 12288, 88)
                P.scrape_latch = 1
            end
        else
            P.scrape_latch = 0
        end
    elseif status == st(WALK, true) then
        if key_in(285, 286) then
            if P.scrape_count == 0 then
                scrape(128, 11468, 120)
                P.scrape_count = 1
            end
        else
            P.scrape_count = 0
        end
        if key_in(277, 278) then
            if P.scrape_latch == 0 then
                scrape(128, 11468, 120)
                P.scrape_latch = 1
            end
        else
            P.scrape_latch = 0
        end
    elseif status == st(1, true) then
        local count = P.scrape_count
        if count < 3 then
            local at = (count + 1) * 11
            local k = key()
            if k >= at and k < at + 2 then
                sh.entity.sfx_pitched(slot, sfx_id("scrape", 1578), 128, -16)
                P.scrape_count = count + 1
            end
        end
    else
        P.scrape_latch = 0
        P.scrape_count = 0
    end
end

function update(s)
    slot = s
    c = record(s)
    P = setmetatable({ slot = s }, props_meta)
    p = record(0)
    dt = sh.game.delta_time()

    if c.control_state == 0 then
        init()
    end
    damage_update()
    -- The attack window stays open only while an attack clip plays.
    local status = c.anim_status
    if status ~= st(4, true) and status ~= st(5, true) then
        c.attack_window = 0
    end
    local fn = control[c.control_state]
    if fn then
        fn()
    end
    move_update()
    anim_update()
    collision_update()
    sound_update()
end
