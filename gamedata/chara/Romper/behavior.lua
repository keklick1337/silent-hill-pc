-- Romper (the hunched ape of the town streets and the resort: map2_s02, map4_s02, map5_s01, map6_s00;
-- elsewhere when spawned): wanders around its spawn point (control state 2), runs at Harry when it sees or
-- hears him (3), creeps around him when it gets close (4), leaps at him (5), pins him down (10) and bites
-- while it holds him (11); 1 crouches and waits, 6 is the recoil of a hit, 7 the stun jump of a heavy hit,
-- 8 the stun or the death on the ground; 9 does nothing.

-- Control states.
local IDLE, WALK, CHASE, CREEP, LEAP, RECOIL, STUN, DOWN, NONE, PIN, BITE = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11

-- Clip statuses (clip * 2, +1 once the clip plays) of the clips the character data names ("anims"): C[i] is
-- the data's number for the game's clip i.
local C = clip_numbers("Romper", {
    "invalid", "still", "run_to_jump", "jump_to_idle_start", "jump_to_run_forward", "jump_to_idle_end",
    "stun_to_idle_start", "walk_to_run_forward_right_end", "walk_to_run_forward_left_end", "bite", "jump_stun_end",
    "jump_stun_start", "grab_attack", "run_forward_loop", "creep_forward", "walk_forward", "stun_to_idle_end",
    "walk_to_run_forward_right_start", "walk_to_run_forward_left_start", "run_forward_start", "creep_to_run",
})
local RUN_TO_JUMP = anim_status(C[2], false)            -- clip 2: the leap
local RUN_TO_JUMP_ON = anim_status(C[2], true)
local JUMP_TO_IDLE_START = anim_status(C[3], false)     -- 3: landing on Harry
local JUMP_TO_IDLE_START_ON = anim_status(C[3], true)
local JUMP_TO_RUN_ON = anim_status(C[4], true)          -- 4: landing a missed leap
local JUMP_TO_IDLE_END_ON = anim_status(C[5], true)     -- 5: holding Harry down
local STUN_TO_IDLE_START = anim_status(C[6], false)     -- 6
local STUN_TO_IDLE_START_ON = anim_status(C[6], true)
local RECOIL_RIGHT_END = anim_status(C[7], false)       -- 7
local RECOIL_RIGHT_END_ON = anim_status(C[7], true)
local RECOIL_LEFT_END = anim_status(C[8], false)        -- 8
local RECOIL_LEFT_END_ON = anim_status(C[8], true)
local BITE_START = anim_status(C[9], false)             -- 9
local BITE_ON = anim_status(C[9], true)
local STUN_END = anim_status(C[10], false)              -- 10
local STUN_END_ON = anim_status(C[10], true)
local STUN_JUMP = anim_status(C[11], false)             -- 11
local STUN_JUMP_ON = anim_status(C[11], true)
local CROUCH = anim_status(C[12], false)                -- 12
local CROUCH_ON = anim_status(C[12], true)
local RUN = anim_status(C[13], false)                   -- 13
local RUN_ON = anim_status(C[13], true)
local CREEP_START = anim_status(C[14], false)           -- 14
local CREEP_ON = anim_status(C[14], true)
local WALK_START = anim_status(C[15], false)            -- 15
local WALK_ON = anim_status(C[15], true)
local STUN_IDLE_END = anim_status(C[16], false)         -- 16
local STUN_IDLE_END_ON = anim_status(C[16], true)
local RECOIL_RIGHT_START_ON = anim_status(C[17], true)  -- 17
local RECOIL_LEFT_START_ON = anim_status(C[18], true)   -- 18
local RUN_START = anim_status(C[19], false)             -- 19
local RUN_START_ON = anim_status(C[19], true)
local CREEP_TO_RUN = anim_status(C[20], false)          -- 20: the creep breaks into the run
local CREEP_TO_RUN_ON = anim_status(C[20], true)        -- romper_flags
local F_AIRBORNE = 0x1
local F_DOWN_CLIP = 0x2      -- the stun clip on the ground was started
local F_STUNNED = 0x4        -- a heavy hit already threw it once
local F_STEERED = 0x8        -- the chase picked its heading this cycle
local F_LEAPT = 0x10         -- leap cooldown
local F_HURT = 0x80          -- took damage this tick
local F_CYCLE = 0x100        -- the chase is past the first tick of its cycle
local F_LEAP_SOUND = 0x200
local F_PINNED = 0x400       -- landed on Harry: holds his height
local F_HOLDING = 0x800      -- owns the group's attack flags

-- Record flags.
local CHARA_STUN_POSE = 0x2
local CHARA_SEES_PLAYER = 0x4
local PLAYER_BUSY = 0x8

-- Group 3 of the NPC group flags: a Romper is leaping at or holding Harry.
local GROUP = 3
local GROUP_BUSY = 0x3

local DIFFICULTY_EASY, DIFFICULTY_HARD = -1, 1
local GROUND_NONE = 12
local COLLISION_IGNORE, COLLISION_NPC = 0, 3
local SYS_GAMEPLAY = 0

-- The leap's attack and the bite's damage row of the attack table.
local LEAP_ATTACK = sh.entity.attack_number("RomperPin")
local BITE_DAMAGE = sh.entity.attack_number("RomperPinDrain")

-- Turn rates (Q12 angle per second).
local TURN_WALK, TURN_CREEP, TURN_RUN = 341, 512, 1024

-- Speed of the run clip (statuses 27, 35, 37), shared by every Romper of the map as the game's one
-- variable: the run curve sets it every tick of the chase.
local run_clip_speed = 98304

-- Speed curve of the walk cycle: {cycle time, acceleration per second}.
local WALK_CURVE = {
    [0] = { 0, 152 }, { 4096, -48 }, { 28672, 120 }, { 32768, 143 }, { 36864, -52 }, { 40960, -45 },
    { 61440, 154 }, { 65536, 0 },
}

-- Speed curve of the run cycle, one row per keyframe: {acceleration, speed change per second}.
local RUN_CURVE = {
    [0] = { 1548, 0 }, { -1244, 32768 }, { -226, 0 }, { -12, 0 }, { 259, 0 }, { -301, 0 }, { 656, -32768 },
    { -92, 0 }, { -93, 0 }, { -31, 0 }, { 63, 0 }, { -71, 19660 }, { -124, 0 }, { -105, 0 }, { 19, 0 },
    { -215, 0 }, { 517, 0 }, { -76, -19660 }, { -69, 0 }, { 350, 0 }, { -381, 0 }, { -367, 0 },
}

-- Collision shapes of the keyframes (ten values each, see collision_from_keyframe in combat.lua) in one
-- block in the PS1 memory order: some clips index past the end of their run into the next one.
local KF = {
    [0] = { -8192, 0, 0, -4096, 1228, 819, 0, 0, 0, 0 },
    { -3727, 0, -2375, -3645, 1351, 1064, 40, 901, 40, 737 },
    { -3604, 0, -2129, -3440, 1228, 1064, 122, 901, 81, 860 },
    { -3932, 0, -2539, -3645, 1146, 1064, 163, 1187, 163, 1269 },
    { -4300, 0, -2703, -4300, 1064, 983, 163, 1228, 163, 1310 },
    { -4341, -573, -2867, -4259, 1064, 983, 163, 1228, 163, 1228 },
    { -4096, 0, -2048, -3891, 1064, 983, 163, 1105, 163, 1187 },
    { -4259, -573, -2867, -4136, 1064, 983, 204, 1228, 163, 1228 },
    { -3727, -327, -2334, -3604, 1146, 983, 163, 1187, 163, 1146 },
    { -3112, 0, -1720, -2990, 1228, 1064, 163, 983, 122, 901 },
    { -3440, 0, -1638, -3276, 1310, 1064, 81, 983, 81, 819 },
    { -3686, 0, -2334, -3604, 1310, 1064, 40, 983, 81, 778 },
    { -3727, 0, -2375, -3604, 1351, 1064, 40, 942, 0, 737 },
    { -4055, 0, -2580, -3932, 1351, 1064, 81, 1228, 40, 1105 },
    { -3563, 0, -2334, -3440, 1351, 1064, 163, 1269, 163, 1146 },
    { -2990, 0, -1720, -2908, 1351, 1064, 327, 1187, 286, 983 },
    { -2990, 0, -1761, -2908, 1351, 1064, 327, 1105, 286, 942 },
    { -3031, 0, -1679, -2908, 1351, 1064, 204, 1064, 163, 860 },
    { -3112, 0, -1720, -2908, 1351, 1064, 122, 1024, 122, 860 },
    { -3153, 0, -1720, -3031, 1351, 1064, 122, 1064, 122, 860 },
    { -3317, 0, -1843, -3112, 1351, 1064, 122, 1064, 122, 860 },
    { -3399, 0, -2129, -3112, 1351, 1064, 163, 1187, 122, 942 },
    { -3399, 0, -1966, -3276, 1351, 1064, 122, 1310, 81, 942 },
    { -3399, 0, -2048, -3276, 1351, 1064, 122, 1146, 0, 1064 },
    { -3932, 0, -2580, -3850, 1351, 1064, 40, 1064, 0, 860 },
    { -3522, 0, -2334, -3399, 1351, 1064, -122, 1024, -40, 819 },
    { -3031, 0, -1433, -2744, 1351, 1064, -245, 901, -245, 778 },
    { -3031, 0, -1433, -2785, 1351, 1064, -286, 860, -286, 778 },
    { -3072, 0, -1515, -2785, 1351, 1064, -286, 901, -327, 696 },
    { -3235, 0, -1638, -3072, 1351, 1064, -245, 860, -286, 655 },
    { -3276, 0, -1884, -3153, 1351, 1064, -122, 901, -163, 655 },
    { -3276, 0, -1884, -3153, 1351, 1064, -81, 983, -122, 655 },
    { -3358, 0, -1843, -3235, 1351, 1064, -163, 1351, -163, 1146 },
    { -3522, 0, -2007, -3317, 1351, 1064, -122, 1351, -163, 1187 },
    { -3563, 0, -2252, -3399, 1351, 1064, -163, 1351, -163, 1187 },
    { -4423, 0, -2785, -4218, 1351, 1187, 163, 942, 163, 819 },
    { -4587, 0, -2621, -4259, 1351, 1228, 204, 983, 163, 819 },
    { -3399, 0, -1515, -3235, 1351, 1269, 286, 1105, 286, 1024 },
    { -2007, 0, -614, -1679, 1351, 1310, 286, 1105, 204, 1024 },
    { -1187, 0, 0, -1105, 1351, 1351, 368, 1064, 327, 1024 },
    { -3768, 0, -2375, -3645, 1351, 1064, 40, 860, 40, 737 },
    { -3645, 0, -2252, -3563, 1351, 1064, 81, 901, 81, 778 },
    { -3440, 0, -2129, -3317, 1351, 1064, 286, 1024, 204, 819 },
    { -3399, 0, -2007, -3276, 1351, 1064, 327, 1024, 204, 819 },
    { -3399, 0, -2007, -3276, 1351, 1064, 204, 1105, 122, 901 },
    { -3522, 0, -2170, -3358, 1351, 1064, 122, 1146, 81, 942 },
    { -3645, 0, -2252, -3563, 1351, 1064, -40, 1187, -40, 1064 },
    { -3686, 0, -2293, -3563, 1351, 1064, 0, 1024, -40, 778 },
    { -3727, 0, -2375, -3604, 1351, 1064, 40, 942, 0, 737 },
    { -3768, 0, -2375, -3522, 1351, 1064, 0, 1064, 0, 819 },
    { -3399, 0, -2048, -3276, 1351, 1064, 122, 1146, 0, 1064 },
    { -3645, 0, -2334, -3522, 1351, 1064, 163, 1392, 122, 1064 },
    { -3973, 0, -2744, -3850, 1351, 1064, 163, 1433, 122, 1187 },
    { -4341, 0, -2785, -4136, 1351, 1064, 163, 1433, 122, 1187 },
    { -4300, 0, -2867, -4136, 1351, 1064, 163, 1351, 122, 1064 },
    { -4136, 0, -2785, -4014, 1351, 1064, 81, 1187, 40, 1024 },
    { -3932, 0, -2580, -3850, 1351, 1064, 40, 1064, 0, 860 },
    { -3809, 0, -2334, -3727, 1351, 1064, 0, 901, -40, 655 },
    { -3645, 0, -2252, -3522, 1351, 1064, -122, 1024, -163, 778 },
    { -3358, 0, -1966, -3235, 1351, 1064, -163, 1228, -163, 1146 },
    { -3563, 0, -2252, -3399, 1351, 1064, -163, 1351, -163, 1187 },
    { -3891, 0, -2580, -3686, 1351, 1064, -122, 1392, -122, 1269 },
    { -4014, 0, -2662, -3809, 1351, 1064, -122, 1433, -122, 1310 },
    { -4259, 0, -2662, -4055, 1351, 1064, -81, 1433, -81, 1310 },
    { -4136, 0, -2744, -3973, 1351, 1064, -122, 1474, -122, 1187 },
    { -4096, 0, -2662, -3891, 1351, 1064, -40, 1351, -40, 1228 },
    { -4136, 0, -2662, -3932, 1351, 1064, 40, 1310, 0, 1146 },
    { -4055, 0, -2580, -3932, 1351, 1064, 81, 1228, 40, 1105 },
    { -3932, 0, -2580, -3850, 1351, 1064, 40, 1105, 40, 942 },
    { -3932, 0, -2457, -3809, 1351, 1064, 81, 1064, 122, 901 },
    { -3891, 0, -2539, -3809, 1351, 1064, 122, 983, 122, 819 },
    { -3727, 0, -2129, -3522, 1351, 1064, 40, 819, 40, 778 },
    { -3768, 0, -2129, -3563, 1351, 1064, 40, 819, 163, 778 },
    { -3850, 0, -2129, -3645, 1351, 1064, 40, 819, 163, 778 },
    { -3768, 0, -2129, -3563, 1351, 1064, 40, 819, 122, 778 },
    { -3686, 0, -2129, -3481, 1351, 1064, 0, 819, 81, 778 },
    { -3686, 0, -2129, -3481, 1351, 1064, -40, 819, 40, 778 },
    { -3686, 0, -2129, -3481, 1351, 1064, -81, 819, -122, 778 },
    { -3727, 0, -2129, -3522, 1351, 1064, -81, 819, -122, 778 },
    { -3809, 0, -2129, -3604, 1351, 1064, -163, 819, -163, 778 },
    { -3809, 0, -2129, -3604, 1351, 1064, -81, 819, -122, 778 },
    { -3768, 0, -2129, -3563, 1351, 1064, 0, 819, 0, 778 },
    { -3727, 0, -2129, -3522, 1351, 1064, 0, 819, 0, 778 },
    { -5160, 0, -3891, -4997, 1146, 778, 40, -163, 0, 122 },
}

-- Where each clip's shapes start in KF.
local K_STAND = 0
local K_LEAP = 1           -- 5, clip 2
local K_LANDED = 6
local K_LAND_RUN = 7       -- 5, clip 4
local K_BITE = 12
local K_RECOIL_RIGHT = 13  -- 11, clip 7
local K_RECOIL_LEFT = 24   -- 11, clip 8
local K_STUN = 35          -- 5, clip 11
local K_CROUCH = 40        -- 9, clip 12
local K_RUN = 49           -- 22, clip 13
local K_WALK = 71          -- 12, clip 15
local K_CREEP = 83

local slot, c, dt

local function key()
    return asr(c.anim_time, 12)
end

local function keys_in(low, high)
    local k = key()
    return k >= low and k <= high
end

local function player()
    return record(0)
end

-- The original's 1-in-n roll of a 30 fps tick: at a higher frame rate it rolls only on the frame that ends
-- a 30 Hz game tick, so the random number generator is called as often per tick as on the console.
local function tick_chance(n)
    if sh.game.tick30() == 0 then
        return false
    end
    return rng_uint(0, n - 1) == 0
end

local function slow_down(rate)
    c.move_speed = max(c.move_speed - q12_mul(dt, rate), 0)
end

-- Airborne: the speed bleeds off faster the faster it goes.
local function slow_down_airborne()
    c.move_speed = approach(c.move_speed, 0, q12_mul(dt, q12_mul(c.move_speed, 12288) + 12288))
end

local function turn_by(rate, positive)
    if positive then
        c.rot_y = c.rot_y + q12_mul(dt, rate)
    else
        c.rot_y = c.rot_y - q12_mul(dt, rate)
    end
end

-- Sight of Harry by the light of the area; `roaming`: the wide, short sight of the walk and the crouch
-- (it also notices him through walls), else the chase's.
local function sees_player(roaming)
    local light = map_light() & 3
    if roaming then
        if light == 0 then
            return sh.entity.player_visible(slot, 1, 7372, 16384)
        elseif light == 2 then
            return sh.entity.player_visible(slot, 1, 12288, 28672)
        end
        return sh.entity.player_visible(slot, 1, 4096, 4096)
    end
    if light == 0 then
        return sh.entity.player_visible(slot, 0, 10240, 24576)
    elseif light == 2 then
        return sh.entity.player_visible(slot, 0, 12288, 32768)
    end
    return sh.entity.player_visible(slot, 0, 3276, 8192)
end

local function difficulty()
    return sh.save.game_difficulty()
end

-- Speed added by the walk cycle since the last tick (the curve integrated over the cycle time).
local function walk_curve()
    local time = c.anim_time
    if time < 602112 or time >= 667648 then
        return 0
    end
    local rel = time - 602112
    if rel < c.romper_cycle_time then
        c.romper_accel = 6
        c.romper_cycle_time = 0
        c.move_speed = 89
    end
    local acc = 0
    for i = 0, 6 do
        local at = c.romper_cycle_time
        if not (WALK_CURVE[i][1] > at or at >= WALK_CURVE[i + 1][1]) then
            if rel < WALK_CURVE[i + 1][1] then
                acc = acc + q12_mul(rel - at, WALK_CURVE[i][2])
                break
            end
            acc = acc + q12_mul(WALK_CURVE[i + 1][1] - at, WALK_CURVE[i][2])
            c.romper_cycle_time = WALK_CURVE[i + 1][1]
        end
    end
    c.romper_cycle_time = rel
    acc = s32(acc)
    return s32(acc + (acc < 0 and 1 or 0))
end

-- The same for the run cycle; its keyframes also set the hops (keyframes 1 and 11) and the speed of the
-- two strides (7 and 18), and the clip's speed for the next frame.
local function run_curve()
    local time = c.anim_time
    if time < 446464 or time >= 536576 then
        return 0
    end
    local rel = time - 446464
    if rel < c.romper_cycle_time then
        c.romper_accel = -404
        c.romper_cycle_time = 0
        c.move_speed = -1048
    end
    local acc = 0
    for i = 0, 21 do
        local at = c.romper_cycle_time
        local from, to = i * 4096, (i + 1) * 4096
        if not (at < from or at >= to) then
            if rel >= to then
                local n = i + 1
                if n == 1 then
                    if c.fall_speed == 0 then
                        c.fall_speed = -10871
                    end
                elseif n == 11 then
                    if c.fall_speed == 0 then
                        c.fall_speed = -12543
                    end
                elseif n == 7 then
                    acc = 0
                    c.romper_accel = 274
                    c.move_speed = 5193
                elseif n == 18 then
                    acc = 0
                    c.romper_accel = 63
                    c.move_speed = 3056
                else
                    acc = acc + q12_mul(to - at, RUN_CURVE[i][1])
                    c.move_speed = c.move_speed + q12_mul(to - at, RUN_CURVE[i][2])
                end
                c.romper_cycle_time = to
            else
                acc = acc + q12_mul(rel - at, RUN_CURVE[i][1])
                c.move_speed = c.move_speed + q12_mul(rel - at, RUN_CURVE[i][2])
                break
            end
        end
    end
    c.romper_cycle_time = rel
    if (rel >= 4096 and rel < 28672) or (rel >= 45056 and rel < 73728) then
        run_clip_speed = 49152
    else
        run_clip_speed = 98304
    end
    acc = s32(acc)
    return s32(acc + (acc < 0 and 1 or 0))
end

-- The pin: where the slide onto Harry ends and how fast it slows down.
local function pin_target()
    local p = player()
    local speed = c.move_speed
    local t = idiv(s32(speed << 12), 32768)
    local rest = speed - q12_mul(t, 16384)
    local heading = c.rot_y
    local dist = q12_mul(rest, t)
    local x, z
    if dist >= distance_2d(p.pos_x - c.pos_x, p.pos_z - c.pos_z) then
        x = c.pos_x + q12_mul32(dist, sh.math.sin(heading))
        z = c.pos_z + q12_mul32(dist, sh.math.cos(heading))
    else
        x = p.pos_x
        z = p.pos_z
    end
    x, z = sh.entity.pin_point(x, z, heading)
    t = idiv(s32(s32(distance_2d(x - c.pos_x, z - c.pos_z) << 12) << 1), speed)
    local brake = idiv(s32(speed << 12), t)
    if brake < 0 then
        brake = 16384
    end
    if brake > 131072 then
        brake = 131072
    end
    c.romper_heading_target = heading_to(c.pos_x, c.pos_z, p.pos_x, p.pos_z)
    c.romper_pin_brake = brake
    c.romper_target_x = x
    c.romper_target_z = z
end

local function start()
    local base = sh.entity.param(slot, "health_base", 1843200)
    c.romper_flags = 0
    c.anim_alpha = 0
    local diff = difficulty()
    if diff == DIFFICULTY_EASY then
        c.health = base - math.fmod(sh.rng.rand16(), sh.entity.param(slot, "health_bonus_max", 460800))
    elseif diff == 0 then
        c.health = base + math.fmod(sh.rng.rand16(), sh.entity.param(slot, "health_bonus_max", 460800))
    else
        local r = sh.rng.rand16()
        c.health = q12_mul(math.fmod(r, 2048) + 6144, sh.entity.param(slot, "health_base", 1843200))
    end
    c.move_speed = 0
    sh.entity.set_collision_state(slot, COLLISION_NPC)
    c.heading_angle = c.rot_y
    sh.entity.props_clear(slot)
    c.control_state = WALK
    anim_set(c, WALK_ON, 147)
    c.romper_accel = 6
    c.romper_heading_target = c.rot_y
    c.move_speed = 89
    sh.entity.clip_table_attach(slot)
    damage_clear(c)
    c.romper_home_x = c.pos_x
    c.romper_target_x = c.pos_x
    c.romper_home_z = c.pos_z
    c.romper_target_z = c.pos_z
end

-- Damage of the tick: a hit turns it on Harry and makes it recoil, a heavy one (health down to the
-- difficulty's threshold) throws it into the stun jump. Off the walkable ground it falls to its death.
local function take_damage()
    if c.health > 0 then
        local _, ground_type = sh.entity.ground_at(c.pos_x, c.pos_z)
        if ground_type == GROUND_NONE and sh.rng.int(16384, 32767) < c.pos_y then
            c.damage_amount = 12288000
        end
    end
    if c.damage_amount == 0 or c.health <= 0 then
        damage_clear(c)
        return
    end
    local prev = c.control_state
    c.health = max(c.health - c.damage_amount, 0)
    c.romper_flags = c.romper_flags | F_HURT
    local threshold = difficulty() == DIFFICULTY_HARD and 1638400 or 819200
    if threshold < c.health then
        local p = player()
        c.romper_target_x = p.pos_x
        c.romper_target_z = p.pos_z
        c.romper_lost_timer = 0
        c.romper_heading_target = heading_to(c.pos_x, c.pos_z, p.pos_x, p.pos_z)
        local s = c.anim_status
        if s == RUN then
            c.anim_time = 109 * 4096
        end
        if s == RUN or s == RUN_ON then
            local k = key() - 109
            if k > 0 then
                if k < 7 then
                    c.anim_status = RECOIL_LEFT_START_ON
                elseif k < 11 then
                    c.anim_status = RECOIL_LEFT_END
                elseif k > 17 then
                    c.anim_status = RECOIL_RIGHT_END
                else
                    c.anim_status = RECOIL_RIGHT_START_ON
                end
            else
                c.anim_status = RECOIL_RIGHT_END
            end
            if c.anim_status & 1 == 0 then
                c.control_state = RECOIL
            end
        elseif s == WALK_START or s == WALK_ON then
            if s == WALK_START then
                c.anim_time = 147 * 4096
            end
            local k = key() - 147
            if k < 2 or k >= 10 then
                c.anim_status = RECOIL_RIGHT_END
            else
                c.anim_status = RECOIL_LEFT_END
            end
            c.control_state = RECOIL
        elseif s == CROUCH or s == CROUCH_ON or s == CREEP_START or s == CREEP_ON then
            c.control_state = RECOIL
            c.anim_status = RECOIL_RIGHT_END
        end
    elseif c.romper_flags & F_STUNNED == 0 then
        c.control_state = STUN
        c.anim_status = STUN_JUMP
        c.romper_flags = c.romper_flags | F_STUNNED
    end
    c.romper_lost_timer = 0
    c.romper_heading_target = 4096
    damage_clear(c)
    if prev ~= c.control_state and c.romper_flags & F_HOLDING ~= 0 then
        c.romper_flags = c.romper_flags & ~F_HOLDING
        set_group_flags(GROUP, group_flags(GROUP) & ~GROUP_BUSY)
    end
end

local function idle()
    local senses = sees_player(true)
    if difficulty() ~= DIFFICULTY_EASY then
        senses = senses + sh.entity.player_noise(slot, 0, 1638)
    end
    slow_down(61440)
    if c.anim_status == CROUCH_ON and (tick_chance(4096) or senses ~= 0) then
        c.control_state = WALK
        c.anim_status = WALK_START
    end
end

local function walk()
    local d = angle_signed(c.romper_heading_target - c.rot_y)
    if turn_step(3, 3) < abs(d) then
        turn_by(TURN_WALK, d > 0)
    else
        c.rot_y = c.romper_heading_target
    end
    local tx, tz = c.romper_target_x, c.romper_target_z
    if c.rot_y == c.romper_heading_target or sh.entity.path_hit(slot, 4096, c.rot_y) ~= 0 then
        d = angle_signed(c.rot_y - heading_to(c.pos_x, c.pos_z, tx, tz))
        if abs(d) > 170 and tick_chance(16) then
            c.romper_heading_target = sh.entity.heading_toward(slot, 4096, tx, tz, 4096, 1)
        end
    end
    if distance_2d(c.pos_x - tx, c.pos_z - tz) < 4096 then
        local dist = sh.rng.int(0, 20479)
        local heading = sh.rng.uint(0, 4095)
        c.romper_target_x = c.romper_home_x + q12_mul32(dist, sh.math.sin(heading))
        c.romper_target_z = c.romper_home_z + q12_mul32(dist, sh.math.cos(heading))
    end
    local r = walk_curve()
    c.romper_accel = c.romper_accel + r
    c.move_speed = c.move_speed + idiv(c.romper_accel * dt, 136)

    local run = sees_player(true)
    if difficulty() ~= DIFFICULTY_EASY then
        run = run + sh.entity.player_noise(slot, 0, 614)
    end
    if run ~= 0 and c.anim_status == WALK_ON then
        c.anim_status = RUN_START
        local k = key()
        if k > 148 and k < 157 then
            c.romper_start_keyframe = 7
        else
            c.romper_start_keyframe = 18
        end
        c.flags = c.flags | CHARA_SEES_PLAYER
    end
end

local function chase()
    local r = run_curve()
    c.romper_accel = c.romper_accel + r
    c.move_speed = c.move_speed + idiv(c.romper_accel * dt, 136)

    local p = player()
    local seen = sees_player(false)
    if difficulty() ~= DIFFICULTY_EASY then
        seen = seen | sh.entity.player_noise(slot, 0, 819)
    end
    if seen ~= 0 then
        c.romper_target_x = p.pos_x + asr(q12_mul32(p.move_speed, sh.math.sin(p.heading_angle)), 1)
        c.romper_target_z = p.pos_z + asr(q12_mul32(p.move_speed, sh.math.cos(p.heading_angle)), 1)
        c.romper_lost_timer = 0
    else
        c.romper_lost_timer = c.romper_lost_timer + dt
    end

    if keys_in(112, 114) or keys_in(121, 126) then
        c.romper_flags = c.romper_flags & ~F_STEERED & ~F_CYCLE
        return
    end
    if c.romper_flags & F_CYCLE == 0 then
        c.romper_flags = c.romper_flags | F_CYCLE
        return
    end

    local tx, tz = c.romper_target_x, c.romper_target_z
    local dist = distance_2d(c.pos_x - tx, c.pos_z - tz)
    local blocked = sh.entity.player_blocked(slot) ~= 0

    if c.romper_flags & F_STEERED == 0 then
        c.romper_flags = c.romper_flags | F_STEERED
        local to_target = s16(heading_to(c.pos_x, c.pos_z, tx, tz))
        local off = abs(angle_signed(to_target - c.rot_y))
        local reach = q12_mul(off * 2, 16384)
        if c.romper_flags & F_LEAPT == 0 and reach < dist then
            if off < 682 and dist < 24576 and not blocked then
                c.romper_heading_target = to_target
            else
                local around = q12_mul(rng_uint(3840, 4351), max(min(dist - 819, 10240), 819))
                c.romper_heading_target = sh.entity.heading_toward(slot, around, tx, tz, 4096, 1)
            end
        else
            local around = q12_mul(rng_uint(3840, 4351), 10240)
            c.romper_heading_target = sh.entity.heading_toward(slot, around, tx, tz, 4096, 0)
        end
        local wall
        if c.romper_heading_target == to_target then
            wall = sh.entity.path_hit(slot, min(dist, 0x1AAA), c.rot_y)
        else
            wall = sh.entity.path_hit(slot, 0x1AAA, c.rot_y)
        end
        if c.romper_lost_timer > 4096 and tick_chance(16) then
            c.control_state = IDLE
            c.anim_status = CROUCH
            c.romper_heading_target = c.rot_y
        elseif wall ~= 0 or c.romper_heading_target == 4096
            or dist < q12_mul(4096 - sh.math.cos(asr(off, 1)), 10240) then
            if c.romper_heading_target == 4096 then
                c.romper_heading_target = c.rot_y + 2048
            end
            local k = key()
            if k < 115 or k > 120 then
                c.control_state = CREEP
                c.anim_status = CREEP_START
                c.romper_start_keyframe = 0
                c.move_speed = 0
                c.romper_timer = 0
            end
        end
    end

    for _ = 1, 3 do
        local d = angle_signed(c.romper_heading_target - c.rot_y)
        if asr(dt, 3) + 1 < abs(d) then
            turn_by(TURN_RUN, d > 0)
        else
            c.rot_y = c.romper_heading_target
            break
        end
    end

    if c.romper_flags & F_LEAPT ~= 0 then
        c.romper_timer = c.romper_timer + dt
        if dist > 7372 or c.romper_timer > 7372 then
            c.romper_timer = 0
            c.romper_flags = c.romper_flags & ~F_LEAPT
        end
    end

    if p.health <= 0 or p.flags & PLAYER_BUSY ~= 0 then
        return
    end
    if sh.entity.sys("sys_state") ~= SYS_GAMEPLAY or group_flags(GROUP) & GROUP_BUSY ~= 0 then
        return
    end
    local dy = c.pos_y - p.pos_y
    if c.fall_speed ~= 0 or dist <= 1228 or dist >= 13107 or dy >= 4096 or dy <= -4915 then
        return
    end
    if c.romper_flags & F_LEAPT ~= 0 then
        return
    end
    if abs(angle_signed(heading_to(c.pos_x, c.pos_z, p.pos_x, p.pos_z) - c.rot_y)) < 341 then
        c.control_state = LEAP
        c.anim_status = RUN_TO_JUMP
        c.attack_window = 1
        set_group_flags(GROUP, group_flags(GROUP) | GROUP_BUSY)
        c.romper_flags = c.romper_flags | F_LEAPT | F_HOLDING
    end
end

local function creep()
    local p = player()
    local seen = sees_player(false)
    if difficulty() == DIFFICULTY_EASY then
        seen = seen | sh.entity.player_noise(slot, 0, 819)
    end
    if seen ~= 0 then
        c.romper_target_x = p.pos_x
        c.romper_target_z = p.pos_z
        c.romper_lost_timer = 0
    else
        c.romper_lost_timer = c.romper_lost_timer + dt
    end

    local wall = sh.entity.path_hit(slot, 10240, c.rot_y) ~= 0
    if c.romper_timer == 0 or (wall and c.romper_timer >= 0) then
        c.romper_heading_target = 0
        local tx, tz = c.romper_target_x, c.romper_target_z
        if distance_2d(p.pos_x - c.pos_x, p.pos_z - c.pos_z) >= 2048 then
            c.romper_heading_target = sh.entity.heading_toward(slot, 10240, tx, tz, 4096, 1)
        else
            c.romper_heading_target = sh.entity.heading_toward(slot, 10240, tx, tz, 4096, 0)
        end
        if c.romper_heading_target == 4096 then
            c.romper_heading_target = sh.entity.clear_heading(slot, 10240, c.rot_y)
        end
        if wall then
            c.romper_timer = -(math.fmod(sh.rng.rand16(), 4096) + 4096)
        else
            c.romper_timer = math.fmod(sh.rng.rand16(), 8192) + 4096
        end
    end

    if c.romper_timer > 0 then
        c.romper_timer = c.romper_timer - dt
        if c.romper_timer < 0 then
            c.romper_timer = 0
        end
    else
        c.romper_timer = c.romper_timer + dt
        if c.romper_timer > 0 then
            c.romper_timer = 0
        end
    end

    for _ = 1, 3 do
        local d = angle_signed(c.romper_heading_target - c.rot_y)
        if turn_step(1, 4) < abs(d) then
            turn_by(TURN_CREEP, d > 0)
        else
            c.rot_y = c.romper_heading_target
        end
    end

    if sh.entity.path_hit(slot, 10240, c.rot_y) == 0 and keys_in(131, 132) then
        c.anim_status = CREEP_TO_RUN
        c.move_speed = 0
        return
    end

    local d = angle_signed(c.romper_heading_target - c.rot_y)
    local facing
    if d >= 0 then
        facing = 2048 - d
    else
        facing = d + 2048
    end
    speed_toward(c, 4915, q12_mul(facing * 2, 9830))
end

local function leap()
    local p = player()
    local lead
    if c.anim_status == RUN_TO_JUMP_ON then
        if c.anim_time > 20480 then
            lead = 0
        else
            lead = s16(2048 - asr(idiv(s32((c.anim_time - 8192) << 12), 12288), 1))
        end
    else
        lead = 2048
    end
    local ahead_x = q12_mul32(p.move_speed, sh.math.sin(p.heading_angle))
    c.romper_target_x = p.pos_x + q12_mul(ahead_x, lead)
    local ahead_z = q12_mul32(p.move_speed, sh.math.cos(p.heading_angle))
    c.romper_target_z = p.pos_z + q12_mul(ahead_z, lead)
    local tx, tz = c.romper_target_x, c.romper_target_z
    local to_target = heading_to(c.pos_x, c.pos_z, tx, tz)

    if c.anim_status == RUN_TO_JUMP then
        slow_down(61440)
        for _ = 1, 4 do
            local d = s16(angle_signed(to_target - c.rot_y))
            if asr(asr(dt, 3) + 1, 1) < abs(d) then
                if d > 0 then
                    c.rot_y = c.rot_y + asr(q12_mul(dt, TURN_RUN), 1)
                else
                    c.rot_y = c.rot_y - asr(q12_mul(dt, TURN_RUN), 1)
                end
            else
                c.rot_y = to_target
            end
        end
    elseif c.anim_status == RUN_TO_JUMP_ON then
        local d = angle_signed(to_target - c.rot_y)
        if turn_step(1, 4) < abs(d) then
            turn_by(TURN_CREEP, d > 0)
        end
        if c.anim_time == 8192 then
            local dist = distance_2d(tx - c.pos_x, tz - c.pos_z)
            local speed = s16(max(dist, 1228))
            c.move_speed = speed << 1
            local ground = sh.entity.ground_at(p.pos_x, p.pos_z)
            local height = s16(ground - 3276)
            c.fall_speed = (height << 1) - 10035
            c.romper_flags = c.romper_flags & ~F_LEAP_SOUND
        else
            local k = key()
            if k == 5 or k == 6 then
                if c.romper_flags & F_LEAP_SOUND == 0 then
                    sh.entity.sfx_flags(slot, sound(c, "attack", 1403), 128, 0)
                    c.romper_flags = c.romper_flags | F_LEAP_SOUND
                end
                local hit = sh.entity.attack(slot, 1, LEAP_ATTACK, c.pos_x, c.pos_y, c.pos_z, 0, c.rot_y, 1024)
                if hit ~= -1 then
                    c.anim_status = JUMP_TO_IDLE_START
                    c.control_state = PIN
                    pin_target()
                    c.romper_flags = c.romper_flags | F_PINNED
                end
            end
        end
    elseif c.romper_flags & F_AIRBORNE ~= 0 then
        slow_down_airborne()
    else
        slow_down(61440)
    end
end

-- A slide of the body along rot_y at `speed` of a clip (keyframes per second), scaled to a distance.
local function slide(speed, mul, div)
    local dist = s16(idiv(q12_mul(speed, dt) * mul, div))
    c.romper_offset_x = q12_mul32(dist, sh.math.sin(c.rot_y))
    c.romper_offset_z = q12_mul32(dist, sh.math.cos(c.rot_y))
end

local function recoil()
    slow_down(61440)
    local clip = c.anim_status >> 1
    if clip == RECOIL_RIGHT_END >> 1 or clip == RECOIL_LEFT_END >> 1 then
        slide(sh.entity.clip_duration(slot, RECOIL_RIGHT_END_ON), 2457, 40960)
    end
end

local function stun()
    if c.romper_flags & F_AIRBORNE ~= 0 then
        slow_down_airborne()
    else
        slow_down(61440)
    end
    if c.anim_status == STUN_JUMP or keys_in(93, 96) then
        slide(sh.entity.clip_duration(slot, STUN_JUMP_ON), 5734, 28672)
    end
    if c.anim_status >> 1 == STUN_END >> 1 then
        c.flags = c.flags | CHARA_STUN_POSE
    end
    if c.health == 0 and c.anim_status == STUN_END_ON then
        c.control_state = DOWN
        c.anim_status = STUN_TO_IDLE_START
        c.flags = c.flags & ~CHARA_STUN_POSE
        sh.entity.enemy_defeated(slot)
    end
end

local function down()
    if c.health == 0 and sh.entity.sys("target_npc") ~= sh.entity.npc_index(slot) then
        c.health = -1
        sh.entity.set_collision_state(slot, COLLISION_IGNORE)
    end
    if c.move_speed == 0 and c.romper_flags & (F_AIRBORNE | F_DOWN_CLIP) == 0 then
        sh.entity.anim_status_set(slot, STUN_TO_IDLE_START_ON, 9)
        c.romper_flags = c.romper_flags | F_DOWN_CLIP
    end
end

local function pin()
    local tx, tz = c.romper_target_x, c.romper_target_z
    if c.pos_x ~= tx or c.pos_z ~= tz then
        c.romper_heading_target = heading_to(c.pos_x, c.pos_z, tx, tz)
    end
    if c.anim_status == JUMP_TO_IDLE_END_ON then
        slow_down(c.romper_pin_brake * 4)
    else
        slow_down(c.romper_pin_brake)
    end
    sh.entity.set_collision_state(slot, COLLISION_IGNORE)
    if c.anim_status == JUMP_TO_IDLE_END_ON then
        c.romper_pin_time = c.anim_time - 81920
    else
        c.romper_pin_time = 0
    end
    local p = player()
    if p.attack_received == -1 then
        c.anim_status = BITE_START
        c.control_state = BITE
        sh.entity.set_collision_state(slot, COLLISION_NPC)
    elseif c.anim_time < 98304 then
        c.romper_flags = c.romper_flags | 0x40
    elseif c.romper_flags & 0x40 ~= 0 then
        c.romper_flags = c.romper_flags & ~0x40
        local damage = s32(sh.entity.weapon_damage(BITE_DAMAGE) << 12)
        p.damage_amount = s32(p.damage_amount + idiv(s32(damage * rng_uint(85, 116)), 100))
        local x = c.pos_x + asr(sh.math.sin(c.rot_y), 1)
        local y = c.pos_y - 409
        local z = c.pos_z + asr(sh.math.cos(c.rot_y), 1)
        sh.entity.effect_spawn(0, x, y, z, 4, 9)
        c.romper_bite_sound = 1
    end
end

local function bite()
    if c.anim_status == CROUCH_ON and tick_chance(8) then
        set_group_flags(GROUP, group_flags(GROUP) & ~GROUP_BUSY)
        c.control_state = IDLE
        c.romper_flags = c.romper_flags & ~F_HOLDING
    end
end

local CONTROL = { idle, walk, chase, creep, leap, recoil, stun, down, function() end, pin, bite }

local function control()
    if c.control_state ~= LEAP then
        c.attack_window = 0
    end
    local state = c.control_state
    CONTROL[state]()
    if c.control_state ~= state then
        c.romper_timer = 0
    end
end

-- One step along the heading at move_speed plus the slide, falling at fall_speed, against the walls.
-- Pinned on Harry it keeps his height while it holds him and sinks slowly to the ground after.
local function move()
    c.fall_speed = c.fall_speed + sh.game.gravity_speed()
    local clip = c.anim_status >> 1
    if clip == JUMP_TO_IDLE_START >> 1 or clip == JUMP_TO_IDLE_END_ON >> 1 then
        c.heading_angle = c.romper_heading_target
    else
        c.heading_angle = c.rot_y
    end
    if dt ~= 0 then
        local dx, dz = step_along(q12_mul(dt, c.move_speed), c.heading_angle)
        local dy = q12_mul(dt, c.fall_speed)
        dx = dx + c.romper_offset_x
        dz = dz + c.romper_offset_z
        local ox, oy, oz, ground = sh.entity.wall_detect(slot, dx, dy, dz)
        c.pos_x = c.pos_x + ox
        c.pos_z = c.pos_z + oz
        if c.romper_flags & F_PINNED ~= 0 then
            if c.control_state ~= PIN and c.pos_y <= ground then
                c.romper_flags = c.romper_flags & ~F_PINNED
            end
        end
        if c.romper_flags & F_PINNED ~= 0 then
            if c.control_state == PIN then
                c.pos_y = c.pos_y + oy
                local py = player().pos_y
                if py < c.pos_y then
                    c.pos_y = py
                    c.fall_speed = 0
                end
            else
                c.pos_y = c.pos_y - q12_mul(dt, 614)
                c.fall_speed = 0
                if ground >= c.pos_y then
                    c.pos_y = ground
                end
            end
        else
            c.pos_y = c.pos_y + oy
            if ground < c.pos_y then
                c.pos_y = ground
                c.fall_speed = 0
            end
        end
        c.romper_offset_z = 0
        c.romper_offset_x = 0
    end
    c.rot_y = angle_signed(c.rot_y)
    if c.fall_speed ~= 0 then
        c.romper_flags = c.romper_flags | F_AIRBORNE
    else
        c.romper_flags = c.romper_flags & ~F_AIRBORNE
    end
end

local function shape_between(base, i0, i1)
    collision_between(c, KF[base + i0], KF[base + i1])
end

local function collision()
    local s = c.anim_status
    local k = key()
    if s == RUN_TO_JUMP_ON then
        local i0, i1 = k - 2, k - 1
        shape_between(K_LEAP, i0 - (i0 >= 5 and 1 or 0), i1 - (i1 >= 5 and 1 or 0))
    elseif s == JUMP_TO_IDLE_START or s == JUMP_TO_IDLE_START_ON or s == JUMP_TO_IDLE_END_ON - 1
        or s == JUMP_TO_IDLE_END_ON then
        collision_from_keyframe(c, KF[K_LANDED])
    elseif s == JUMP_TO_RUN_ON then
        shape_between(K_LAND_RUN, k - 15, k - 14)
    elseif s == RECOIL_RIGHT_END_ON then
        shape_between(K_RECOIL_RIGHT, k - 39, k - 38)
    elseif s == RECOIL_LEFT_END_ON then
        shape_between(K_RECOIL_LEFT, k - 50, k - 49)
    elseif s == STUN_TO_IDLE_START or s == STUN_TO_IDLE_START_ON or s == STUN_END or s == STUN_END_ON
        or s == STUN_IDLE_END or s == STUN_IDLE_END_ON then
        collision_from_keyframe(c, KF[K_STUN + 4])
    elseif s == STUN_JUMP then
        collision_from_keyframe(c, KF[K_STUN])
    elseif s == STUN_JUMP_ON then
        local i0 = k - 93
        shape_between(K_STUN, min(i0, 4), min(i0 + 1, 4))
    elseif s == CROUCH_ON then
        local i0 = k - 100
        shape_between(K_CROUCH, i0, math.fmod(i0 + 1, 9))
    elseif s == RUN_ON then
        local i0 = k - 109
        shape_between(K_RUN, i0, math.fmod(i0 + 1, 22))
    elseif s == CREEP_ON then
        collision_from_keyframe(c, KF[K_CREEP])
    elseif s == WALK_ON then
        local function walk_key(i)
            return i - (i > 0 and 1 or 0) - (i >= 4 and 1 or 0) - (i >= 8 and 1 or 0) - (i >= 12 and 1 or 0)
        end
        local i0 = k - 147
        shape_between(K_WALK, walk_key(i0), walk_key(math.fmod(i0 + 1, 16)))
    elseif s == BITE_ON then
        collision_from_keyframe(c, KF[K_BITE])
        c.cylinder_radius = idiv(c.anim_time - 249856, 100)
    else
        collision_from_keyframe(c, KF[K_STAND])
    end
    sh.entity.shape_offsets_update(slot)
    if c.anim_status >> 1 ~= RUN_TO_JUMP >> 1 then
        c.attack_window = 0
    end
end

-- The clip of the tick: the run clip's speed, the creep's start keyframe and speed, the hand-over
-- between the recoil, run and creep clips; the body twists into a turn (bone 2).
local function animate()
    local s = c.anim_status
    if s == RUN_ON or s == RECOIL_RIGHT_START_ON or s == RECOIL_LEFT_START_ON then
        sh.entity.clip_duration_set(slot, s, run_clip_speed)
    elseif s == CREEP_ON then
        sh.entity.clip_start_set(slot, CREEP_ON, c.romper_start_keyframe + 131)
        c.romper_start_keyframe = 0
        sh.entity.clip_duration_set(slot, s, q12_mul(max(c.move_speed, 6144), 34078))
    end

    anim_play(c)

    s = c.anim_status
    if s == RECOIL_RIGHT_END then
        c.control_state = RECOIL
        if c.anim_time == 127 * 4096 then
            c.anim_status = RECOIL_RIGHT_END_ON
            c.anim_time = 39 * 4096
        end
    elseif s == RECOIL_LEFT_END then
        c.control_state = RECOIL
        if c.anim_time == 116 * 4096 then
            c.anim_status = RECOIL_LEFT_END_ON
            c.anim_time = 50 * 4096
        end
    elseif s == RUN then
        if c.control_state == LEAP then
            c.romper_flags = c.romper_flags & ~F_HOLDING
            set_group_flags(GROUP, group_flags(GROUP) & ~GROUP_BUSY)
        end
        c.control_state = CHASE
        c.romper_cycle_time = 22 * 4096 - 1
        if c.anim_time == 49 * 4096 then
            c.anim_status = RUN_ON
            c.romper_cycle_time = 4095
            c.anim_time = 110 * 4096
            c.romper_accel = 1143
            c.move_speed = 0
        elseif c.anim_time == 60 * 4096 then
            c.anim_status = RUN_ON
            c.romper_cycle_time = 11 * 4096 - 1
            c.romper_accel = 121
            c.anim_time = 120 * 4096
            c.move_speed = 4915
        end
    elseif s == RUN_START_ON or s == CREEP_TO_RUN_ON then
        c.control_state = CHASE
        c.anim_status = RUN_ON
        c.romper_cycle_time = c.anim_time - (109 * 4096 + 1)
    end

    local d = angle_signed(c.romper_prev_rot_y - c.rot_y)
    local twist = d == 0 and 0 or 196
    if d < 0 then
        twist = -twist
    end
    local step = asr(dt * 256, 12)
    if twist < c.romper_twist then
        c.romper_twist = max(twist, c.romper_twist - step)
    else
        c.romper_twist = min(twist, c.romper_twist + step)
    end
    sh.entity.bone_turn(slot, 2, 0, c.romper_twist, 0)
    c.romper_prev_rot_y = c.rot_y
end

-- Volume and pitch of a footstep by the clip's first keyframe.
local STEP = {
    [2] = { 196, 32 }, [8] = { 128, -32 }, [15] = { 128, -32 }, [93] = { 128, -32 }, [109] = { 128, 0 },
}

local function footstep(first, offset, latch)
    local k = key()
    local at = first + offset
    if k >= at and k < at + 2 then
        if c[latch] == 0 then
            local v = STEP[first] or { 32, -64 }
            sh.entity.sfx_pitched(slot, sound(c, "footstep", 1405), v[1], v[2] + sh.rng.uint(-7, 8))
            c[latch] = 1
        end
    else
        c[latch] = 0
    end
end

local function voices()
    local s = c.anim_status
    if s == RUN_TO_JUMP_ON then
        footstep(2, 2, "romper_step_latch")
    elseif s == JUMP_TO_IDLE_START_ON then
        footstep(8, 5, "romper_step_latch")
    elseif s == JUMP_TO_RUN_ON then
        footstep(15, 2, "romper_step_latch")
    elseif s == STUN_JUMP_ON then
        footstep(93, 2, "romper_step_latch_2")
        footstep(93, 3, "romper_step_latch")
    elseif s == RUN_ON then
        footstep(109, 7, "romper_step_latch_2")
        footstep(109, 20, "romper_step_latch")
    elseif s == CREEP_ON then
        footstep(131, 8, "romper_step_latch_2")
        footstep(131, 15, "romper_step_latch")
    else
        c.romper_step_latch_2 = 0
        c.romper_step_latch = 0
    end

    if c.romper_hurt_timer ~= 0 then
        c.romper_hurt_timer = c.romper_hurt_timer - dt
        if c.romper_hurt_timer < 0 then
            c.romper_hurt_timer = 0
        end
    elseif c.romper_flags & F_HURT ~= 0 then
        c.romper_hurt_timer = 3276
        sh.entity.sfx_flags(slot, sound(c, "damage", 1400), 128, 0)
    end
    c.romper_flags = c.romper_flags & ~F_HURT

    if c.anim_status == JUMP_TO_IDLE_END_ON then
        if c.romper_land_timer == 0 then
            sh.entity.sfx_flags(slot, sound(c, "jump_land", 1401), 128, 0)
            c.romper_land_timer = 13107
        else
            c.romper_land_timer = c.romper_land_timer - dt
            if c.romper_land_timer < 0 then
                c.romper_land_timer = 0
            end
        end
    elseif c.romper_land_timer > 0 then
        c.romper_land_timer = 0
        sh.sfx_stop(sound(c, "jump_land", 1401))
    end

    if c.romper_bite_sound ~= 0 then
        sh.entity.sfx_flags(slot, sound(c, "attack_hit", 1402), rng_uint(120, 135), 0)
        c.romper_bite_sound = 0
    end

    if c.anim_status ~= JUMP_TO_IDLE_END_ON and c.romper_hurt_timer == 0 and c.health > 0 then
        if c.romper_idle_timer ~= 0 then
            c.romper_idle_timer = c.romper_idle_timer - dt
            if c.romper_idle_timer < 0 then
                c.romper_idle_timer = 0
            end
        else
            local base
            if c.health <= 921600 then
                base = asr(idiv(s32(c.health << 12), 1843200), 6) + 56
            else
                base = 120
            end
            sh.entity.sfx_flags(slot, sound(c, "idle", 1404), asr(base + rng_uint(0, 15), 1), 0)
            c.romper_idle_timer = sh.rng.int(18432, 26623)
        end
    end
end

function update(s)
    slot = s
    c = record(s)
    dt = sh.game.delta_time()
    if c.control_state == 0 then
        start()
    elseif dt ~= 0 then
        take_damage()
        control()
        move()
        collision()
    end
    animate()
    voices()
end
