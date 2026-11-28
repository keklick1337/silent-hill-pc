-- Groaner (the dog of the town streets; the Wormhead of the nightmare uses the same body): walks
-- its rounds, runs at Harry when it sees or hears him, circles him and leaps at his throat. A hit
-- throws it back or onto its side; it growls while it lives and yelps when it is hurt.

local MAP = sh.map_name()
local TOWN = MAP == "map2_s00"
local JUMP_BITE = sh.entity.attack_number("GroanerJumpBite")

-- Control states.
local WALK, RUN, JUMP_ATTACK, CIRCLE, RECOIL, STUN_JUMP, STUN_RIGHT, STUN_LEFT, DEATH = 1, 2, 3, 4, 5, 6, 7, 8, 9

-- Flags of groaner_flags.
local TURNING = 0x1
local HIT = 0x2
local AIRBORNE = 0x4
local DOWN = 0x8
local SLIDING = 0x10
local AWAKE = 0x20
local DEATH_CLIP = 0x40
local SEES_PLAYER = 0x80
local STATE_STARTED = 0x100
local JUMPED = 0x200
local RUNNING = 0x400
local DYING = 0x1000

local A = clips("Groaner", {
    stun_from_jump_death_start = 1, stand_recoil_front = 2, jump_to_stun = 3, stun_from_jump = 4,
    stun_from_stand_right_death_start = 5, stand_recoil_right = 6, stand_to_stun_right = 7,
    stun_from_stand_right = 8, stand_idle = 9, jump_attack = 10, stun_from_stand_left_death_start = 11,
    stand_recoil_left = 12, stand_to_stun_left = 13, stun_from_stand_left = 14, stand_to_jump_attack = 15,
    run_forward = 16, walk_forward = 17, stun_from_jump_death_end = 18, stun_from_stand_right_death_end = 19,
    stun_from_stand_left_death_end = 20, stun_from_jump_recoil = 21, stun_from_stand_right_recoil = 22,
    stun_from_stand_left_recoil = 23,
})
local function st(clip, active)
    return anim_status(clip, active)
end

-- Sight of the area by its light (dark, lit, flashlight): distance and step of the sight cone.
local SIGHT = { [0] = { 4096, 2457 }, [1] = { 24576, 12288 }, [2] = { 12288, 16384 } }

-- Collision shapes of the keyframes as the game lays them out (one block in this order).
local K = {
    [0] = { -3194, 0, -2088, -3112, 983, 491, -40, 532, 0, 573 },  -- 0 EEE54 (30)
    { -3317, 0, -2170, -3235, 901, 491, -40, 532, 0, 573 },
    { -3440, 0, -2334, -3358, 819, 450, -40, 532, 0, 655 },
    { -3563, 0, -2416, -3481, 737, 450, -81, 573, -40, 696 },
    { -3686, 0, -2580, -3481, 655, 450, -40, 573, 0, 901 },
    { -3850, 0, -2744, -3522, 573, 491, -122, 696, 0, 819 },
    { -3973, -81, -2867, -3727, 573, 491, -40, 696, 0, 778 },
    { -4136, -163, -3031, -3850, 573, 450, 40, 696, 122, 778 },
    { -4259, -409, -3112, -4014, 573, 450, 163, 655, 204, 778 },
    { -4382, -696, -3235, -4096, 573, 450, 286, 532, 450, 737 },
    { -4423, -1024, -3235, -4096, 573, 450, 368, 532, 532, 655 },
    { -4423, -1064, -3317, -4096, 573, 450, 409, 532, 614, 655 },
    { -4382, -1187, -3317, -4096, 573, 450, 450, 532, 491, 450 },
    { -4382, -901, -3276, -4096, 573, 450, 409, 450, 450, 450 },
    { -4136, -737, -3072, -4055, 614, 450, 327, 409, 491, 450 },
    { -4055, -532, -2949, -3891, 655, 450, 327, 409, 491, 409 },
    { -3850, -245, -2826, -3727, 696, 450, 286, 368, 368, 327 },
    { -3686, 0, -2580, -3399, 737, 409, 327, 368, 368, 327 },
    { -3563, 0, -2457, -3276, 778, 409, 409, 368, 327, 204 },
    { -3440, 0, -2252, -3112, 819, 450, 327, 286, 245, 163 },
    { -3276, 0, -2129, -2908, 819, 450, 368, 286, 204, 40 },
    { -3276, 0, -2088, -2908, 860, 450, 409, 286, 245, 81 },
    { -3153, 0, -1925, -2908, 860, 450, 327, 204, 245, 81 },
    { -3112, 0, -1884, -2908, 901, 450, 327, 204, 163, 40 },
    { -3072, 0, -1884, -2908, 901, 450, 327, 204, 204, 40 },
    { -3072, 0, -1884, -2908, 942, 450, 368, 163, 204, 0 },
    { -3031, 0, -1884, -2908, 942, 450, 368, 204, 204, 0 },
    { -3031, 0, -1884, -2908, 983, 450, 368, 204, 245, 81 },
    { -3031, 0, -1884, -2908, 983, 450, 368, 204, 245, 81 },
    { -2990, 0, -1884, -2908, 983, 450, 368, 204, 245, 81 },
    { -2908, 0, -1802, -2785, 1064, 491, -40, 532, 40, 491 },  -- 30 EF0AC (12)
    { -2826, 0, -1720, -2744, 1064, 491, -40, 532, 40, 491 },
    { -2785, 0, -1638, -2703, 1064, 491, -40, 532, 40, 491 },
    { -2744, 0, -1556, -2703, 1064, 491, -40, 532, 40, 491 },
    { -2744, 0, -1556, -2703, 1064, 491, -40, 532, 40, 532 },
    { -2662, 0, -1515, -2621, 1064, 450, 0, 532, 40, 532 },
    { -2621, 0, -1474, -2580, 1064, 450, 0, 532, 40, 532 },
    { -2539, 0, -1474, -2498, 1064, 450, 0, 532, 40, 573 },
    { -2498, 0, -1433, -2457, 1064, 450, 0, 532, 40, 573 },
    { -2457, 0, -1392, -2416, 1064, 450, 0, 532, 40, 573 },
    { -2539, 0, -1474, -2498, 1024, 450, 0, 532, 40, 532 },
    { -2785, 0, -1720, -2703, 1024, 450, 0, 532, 40, 532 },
    { -2908, 0, -1802, -2826, 1024, 450, 0, 532, 40, 532 },  -- 42 EF19C
    { -3031, 0, -1679, -2949, 901, 573, 81, 491, 81, 491 },  -- 43 EF1B0 (8)
    { -3031, 0, -1679, -2949, 901, 573, 40, 491, 81, 491 },
    { -2867, 0, -1515, -2785, 901, 573, -40, 491, 0, 491 },
    { -2826, 0, -1474, -2744, 901, 573, -81, 450, -40, 491 },
    { -2785, 0, -1474, -2703, 901, 573, -81, 450, -81, 491 },
    { -2703, 0, -1392, -2621, 901, 573, -81, 450, -81, 491 },
    { -2703, 0, -1392, -2621, 901, 573, -40, 450, -40, 491 },
    { -2867, 0, -1556, -2785, 901, 573, 40, 491, 40, 491 },
    { -2785, 0, -1638, -2703, 1064, 532, -81, 532, -81, 778 },  -- 51 EF250 (3)
    { -2826, 0, -1679, -2744, 1064, 532, -81, 532, -81, 778 },
    { -2785, 0, -1638, -2703, 1064, 532, -81, 532, -81, 778 },
    { -2990, 0, -1679, -2908, 901, 532, -40, 614, 40, 942 },  -- 54 EF28C (11)
    { -2990, 0, -1679, -2908, 901, 532, -81, 614, 40, 942 },
    { -2990, 0, -1679, -2908, 901, 532, -40, 614, 40, 942 },
    { -2990, 0, -1679, -2908, 901, 532, -81, 614, 40, 942 },
    { -2990, 0, -1679, -2908, 901, 532, -40, 614, 40, 942 },
    { -2990, 0, -1679, -2908, 901, 532, 0, 614, 40, 942 },
    { -2990, 0, -1679, -2908, 901, 532, 40, 614, 40, 942 },
    { -2990, 0, -1679, -2908, 901, 532, 81, 614, 40, 942 },
    { -2990, 0, -1679, -2908, 901, 532, 40, 614, 40, 942 },
    { -2990, 0, -1679, -2908, 901, 532, 0, 614, 40, 942 },
    { -2990, 0, -1679, -2908, 901, 532, -40, 614, 40, 942 },
    { -2785, 0, -1638, -2662, 819, 491, -122, 532, -40, 327 },  -- 65 EF368 (17)
    { -2826, 0, -1679, -2703, 819, 491, -163, 532, -81, 327 },
    { -2867, 0, -1761, -2785, 819, 491, -245, 573, -122, 327 },
    { -2949, 0, -1884, -2867, 819, 491, -327, 614, -163, 327 },
    { -2990, 0, -1966, -2908, 819, 491, -409, 614, -204, 368 },
    { -3031, 0, -2048, -2949, 819, 491, -491, 573, -245, 368 },
    { -3112, 0, -2129, -3031, 819, 491, -573, 532, -327, 368 },
    { -3153, 0, -2170, -3072, 819, 491, -655, 491, -368, 368 },
    { -3194, 0, -2211, -3112, 819, 450, -737, 491, -368, 368 },
    { -3153, 0, -2170, -3072, 819, 450, -696, 491, -368, 368 },
    { -3112, 0, -2129, -3031, 819, 450, -696, 491, -327, 368 },
    { -3072, 0, -2088, -2990, 819, 450, -532, 532, -286, 368 },
    { -3072, 0, -2088, -2990, 819, 491, -450, 532, -245, 327 },
    { -2990, 0, -2007, -2908, 819, 491, -409, 532, -163, 327 },
    { -2949, 0, -1966, -2867, 819, 491, -327, 532, -122, 327 },
    { -2908, 0, -1925, -2826, 819, 491, -245, 532, -81, 327 },
    { -2867, 0, -1884, -2785, 819, 491, -204, 532, -40, 327 },
    { -2826, 0, -1843, -2744, 819, 491, -163, 532, -40, 327 },  -- 82 EF4BC
    { -2785, 0, -1638, -2744, 1064, 491, -122, 532, -40, 450 },  -- 83 EF4D0 (17)
    { -2867, 0, -1720, -2826, 1064, 491, -122, 532, -40, 450 },
    { -2867, 0, -1720, -2826, 1064, 491, -204, 532, -40, 450 },
    { -2908, 0, -1761, -2867, 1064, 491, -245, 532, -81, 450 },
    { -2990, 0, -1843, -2908, 1064, 491, -409, 532, -122, 450 },
    { -3072, 0, -1925, -2990, 1064, 491, -532, 532, -204, 450 },
    { -3112, 0, -1966, -3031, 1064, 491, -614, 532, -245, 450 },
    { -3153, 0, -2007, -3072, 1064, 491, -655, 491, -245, 450 },
    { -3112, 0, -1966, -3031, 1064, 491, -614, 491, -245, 450 },
    { -3112, 0, -1966, -3031, 1064, 491, -573, 491, -245, 450 },
    { -3072, 0, -1925, -2990, 1064, 491, -532, 491, -204, 450 },
    { -2990, 0, -1843, -2908, 1064, 491, -409, 532, -163, 450 },
    { -2990, 0, -1843, -2908, 1064, 491, -368, 532, -122, 450 },
    { -2949, 0, -1802, -2867, 1064, 491, -286, 532, -122, 450 },
    { -2867, 0, -1720, -2785, 1064, 491, -204, 532, -81, 450 },
    { -2826, 0, -1679, -2744, 1064, 491, -163, 532, -81, 450 },
    { -2826, 0, -1679, -2744, 1064, 491, -122, 491, -40, 491 },
    { -2785, 0, -1638, -2703, 1064, 491, -163, 491, -40, 491 },  -- 100 EF624
    { -2785, 0, -1597, -2703, 1064, 491, -122, 450, 0, 368 },  -- 101 EF638 (18)
    { -2785, 0, -1597, -2703, 1064, 491, -81, 450, 0, 368 },
    { -2826, 0, -1638, -2744, 1064, 491, 0, 450, 40, 368 },
    { -2908, 0, -1720, -2826, 1064, 491, 163, 450, 81, 368 },
    { -2949, 0, -1761, -2867, 1064, 491, 245, 491, 163, 409 },
    { -3031, 0, -1843, -2949, 1064, 491, 450, 491, 245, 409 },
    { -3072, 0, -1884, -2990, 1064, 491, 573, 491, 245, 409 },
    { -3112, 0, -1925, -3031, 1105, 450, 655, 532, 245, 450 },
    { -3153, 0, -1966, -3072, 1105, 450, 655, 532, 245, 450 },
    { -3153, 0, -1966, -3072, 1064, 450, 573, 532, 204, 450 },
    { -3072, 0, -1884, -2990, 1064, 491, 491, 532, 204, 450 },
    { -3031, 0, -1843, -2949, 1064, 491, 450, 532, 204, 450 },
    { -3031, 0, -1843, -2949, 1064, 491, 286, 532, 81, 450 },
    { -2990, 0, -1843, -2908, 1064, 491, 204, 532, 81, 450 },
    { -2949, 0, -1802, -2867, 1064, 491, 122, 532, 40, 450 },
    { -2908, 0, -1761, -2826, 1064, 491, 40, 532, 40, 450 },
    { -2867, 0, -1720, -2785, 1064, 491, -40, 532, 0, 450 },
    { -2826, 0, -1679, -2744, 1064, 491, -81, 532, -40, 450 },
    { -2826, 0, -1679, -2744, 1064, 491, -122, 532, -40, 450 },  -- 119 EF7A0
    { -3850, 0, -2744, -3768, 819, 450, -122, 778, 0, 573 },  -- 120 EF7B4 (36)
    { -4341, 0, -3153, -3932, 737, 450, -81, 778, -40, 655 },
    { -4546, 0, -3358, -4136, 655, 450, -81, 696, -40, 655 },
    { -4587, 0, -3358, -4382, 573, 450, -81, 409, 0, 368 },
    { -4136, 0, -2867, -3973, 491, 450, 40, -81, 0, 245 },
    { -3686, 0, -2416, -3522, 573, 450, 0, -286, 0, 655 },
    { -2867, 0, -1884, -2703, 655, 409, 0, -614, 0, 245 },
    { -1925, 0, -942, -1802, 737, 450, -81, -655, 40, -122 },
    { -1310, 0, -327, -1269, 819, 450, 81, -532, 81, -245 },
    { -1024, 0, 0, -983, 942, 491, 81, -450, 81, -245 },
    { -819, 122, 122, -819, 942, 491, 81, -532, 81, -245 },
    { -655, 245, 245, -655, 942, 491, 122, -409, 81, -245 },
    { -614, 286, 286, -614, 942, 532, 122, -409, 0, -245 },
    { -573, 286, 286, -573, 1024, 532, 122, -327, -81, -40 },
    { -573, 286, 286, -573, 1064, 532, 163, -286, -204, -40 },
    { -655, 204, 204, -655, 1064, 532, 122, -286, -245, 0 },
    { -737, 122, 122, -737, 1064, 532, 122, -286, -245, 0 },
    { -860, 0, 0, -819, 1105, 573, 81, -327, -409, 122 },
    { -860, 0, 0, -819, 1146, 573, 81, -327, -450, 204 },
    { -942, 0, 0, -901, 1187, 573, 0, -409, -491, 163 },
    { -942, 0, 0, -901, 1228, 573, -40, -409, -532, 81 },
    { -983, 0, 0, -942, 1228, 573, -81, -409, -532, 81 },
    { -1024, 0, 0, -983, 1228, 573, -122, -409, -532, 81 },
    { -1024, 0, 0, -983, 1228, 532, -122, -491, -573, 40 },
    { -1024, 0, 0, -983, 1228, 532, -122, -491, -614, 40 },
    { -942, 0, 0, -942, 1228, 532, -122, -614, -614, 163 },
    { -942, 0, 0, -942, 1228, 532, -163, -614, -655, 163 },
    { -942, 0, 0, -942, 1228, 532, -245, -614, -655, 245 },
    { -942, 0, 0, -942, 1228, 532, -245, -655, -655, 245 },
    { -942, 0, 0, -942, 1228, 532, -286, -655, -655, 245 },
    { -942, 0, 0, -942, 1228, 532, -286, -737, -696, 245 },
    { -942, 0, 0, -942, 1228, 573, -368, -696, -696, 245 },
    { -942, 0, 0, -942, 1228, 573, -409, -696, -696, 245 },
    { -942, 0, 0, -942, 1228, 573, -409, -696, -737, 163 },
    { -942, 0, 0, -942, 1228, 573, -409, -696, -778, 81 },
    { -942, 0, 0, -942, 1228, 573, -409, -655, -778, 81 },
    { -3112, 0, -1966, -3031, 1064, 491, -122, 614, 0, 450 },  -- 156 EFA84 (30)
    { -3235, 0, -2129, -3153, 1064, 491, -122, 573, 0, 450 },
    { -3440, 0, -2334, -3317, 1064, 491, -122, 532, 0, 409 },
    { -3686, 0, -2457, -3440, 1064, 491, -81, 450, 0, 368 },
    { -3768, 0, -2621, -3604, 1064, 491, -81, 327, 40, 286 },
    { -3768, 0, -2621, -3604, 1064, 491, -122, 491, 40, 286 },
    { -3686, 0, -2703, -3604, 1064, 450, -122, 450, 81, 286 },
    { -3522, 0, -2375, -3358, 1064, 450, -122, 532, 122, 286 },
    { -3358, 0, -2170, -3276, 1064, 450, -122, 532, 163, 245 },
    { -3194, 0, -2048, -3072, 1064, 450, -122, 532, 204, 204 },
    { -3072, -163, -1925, -3031, 1064, 450, -122, 696, 245, 204 },
    { -2826, 0, -1720, -2744, 1105, 450, -81, 655, 368, 204 },
    { -2539, -204, -1556, -2457, 1105, 450, -81, 655, 368, 204 },
    { -2375, -245, -1392, -2293, 1146, 450, -81, 737, 409, 204 },
    { -2048, -286, -1105, -2007, 1146, 450, -163, 655, 409, 245 },
    { -1843, -327, -983, -1802, 1146, 491, -122, 655, 409, 245 },
    { -1638, -409, -696, -1597, 1146, 491, -122, 655, 409, 245 },
    { -1515, -163, -614, -1474, 1146, 491, -204, 614, 409, 286 },
    { -1392, 0, -409, -1310, 1146, 491, -163, 573, 409, 286 },
    { -1187, 0, -327, -1146, 1146, 532, -204, 573, 409, 286 },
    { -1105, 0, -245, -1064, 1146, 532, -204, 573, 409, 286 },
    { -1105, 0, -163, -1064, 1187, 532, -204, 573, 409, 286 },
    { -1024, 0, -81, -983, 1228, 532, -245, 532, 409, 409 },
    { -1024, 0, 0, -983, 1269, 532, -245, 532, 409, 450 },
    { -1024, 0, 0, -983, 1310, 532, -327, 532, 409, 450 },
    { -1064, 0, 0, -1024, 1351, 532, -327, 532, 409, 450 },
    { -1105, 0, 0, -1064, 1351, 532, -327, 532, 409, 450 },
    { -1064, 0, 0, -1024, 1351, 532, -327, 532, 409, 450 },
    { -983, 0, 0, -942, 1351, 532, -368, 532, 409, 450 },
    { -942, 0, 0, -901, 1351, 532, -368, 491, 327, 450 },
    { -2826, 0, -1638, -2785, 1064, 491, -122, 532, -40, 368 },  -- 186 EFCDC (31)
    { -2908, 0, -1761, 0, 1064, 491, -40, 532, 0, 368 },
    { -2990, 0, -1925, 0, 1064, 491, 81, 532, 40, 368 },
    { -3194, 0, -2129, -3031, 1064, 491, 245, 532, 81, 368 },
    { -3276, 0, -2211, -3112, 1064, 491, 368, 532, 163, 368 },
    { -3440, 0, -2293, -3276, 1064, 491, 532, 450, 81, 327 },
    { -3440, 0, -2293, -3276, 1064, 491, 532, 409, 40, 327 },
    { -3399, 0, -2293, -3276, 1105, 491, 614, 368, 0, 204 },
    { -3276, 0, -2293, -3194, 1105, 491, 614, 368, 0, 204 },
    { -3194, 0, -2211, -3112, 1105, 491, 614, 368, -40, 245 },
    { -3194, 0, -2088, -3112, 1146, 491, 655, 491, -81, 286 },
    { -2949, 0, -1925, -2867, 1146, 491, 532, 532, -122, 368 },
    { -2826, 0, -1843, -2744, 1146, 491, 491, 573, -122, 327 },
    { -2580, 0, -1556, -2498, 1187, 491, 368, 491, -163, 368 },
    { -2416, 0, -1433, -2334, 1187, 491, 368, 573, -204, 368 },
    { -2252, 0, -1351, -2211, 1228, 532, 286, 532, -245, 450 },
    { -1966, 0, -1105, -1925, 1228, 532, 245, 573, -245, 450 },
    { -1843, 0, -983, -1802, 1228, 532, 245, 655, -286, 450 },
    { -1638, 0, -696, -1597, 1228, 532, 245, 655, -286, 450 },
    { -1515, 0, -573, -1474, 1228, 573, 204, 573, -286, 450 },
    { -1392, 0, -491, -1351, 1228, 573, 204, 573, -245, 491 },
    { -1351, 0, -204, -1310, 1269, 573, 204, 573, -286, 450 },
    { -1228, 0, 0, -1187, 1269, 573, 163, 573, -286, 450 },
    { -1146, 0, 0, -1105, 1310, 573, 204, 573, -327, 450 },
    { -1105, 0, 0, -1064, 1310, 573, 204, 573, -286, 450 },
    { -1064, 0, 0, -1024, 1351, 573, 204, 573, -286, 450 },
    { -1064, 0, 0, -1024, 1351, 573, 245, 532, -286, 450 },
    { -983, 0, 0, -942, 1351, 573, 245, 532, -286, 450 },
    { -983, 0, 0, -942, 1392, 573, 286, 532, -286, 450 },
    { -983, 0, 0, -942, 1392, 573, 286, 532, -245, 450 },
    { -942, 0, 0, -901, 1392, 573, 327, 532, -245, 450 },
    { -1269, 0, -286, -1187, 1392, 614, -450, -450, -942, -204 },  -- 217 EFF48 (12)
    { -1269, 0, -286, -1187, 1392, 614, -409, -450, -942, -204 },
    { -1351, 0, -368, -1269, 1392, 614, -368, -450, -860, -245 },
    { -1351, 0, -368, -1269, 1392, 614, -368, -450, -819, -286 },
    { -1351, 0, -368, -1269, 1392, 573, -327, -491, -819, -286 },
    { -1392, 0, -409, -1310, 1392, 573, -327, -491, -819, -286 },
    { -1392, 0, -409, -1310, 1392, 573, -327, -491, -901, -245 },
    { -1351, 0, -368, -1269, 1392, 573, -368, -491, -901, -245 },
    { -1351, 0, -368, -1269, 1392, 573, -409, -532, -901, -245 },
    { -1310, 0, -327, -1228, 1392, 573, -409, -532, -860, -204 },
    { -1269, 0, -286, -1187, 1392, 573, -409, -532, -860, -204 },
    { -1269, 0, -286, -1187, 1392, 573, -409, -532, -860, -163 },
    { -983, 0, -122, -901, 1392, 532, -327, 532, 327, 491 },  -- 229 F0038 (9)
    { -1024, 0, -163, -942, 1351, 532, -368, 532, 327, 491 },
    { -1064, 0, -204, -983, 1351, 532, -368, 614, 286, 532 },
    { -1105, 0, -245, -1024, 1351, 573, -327, 573, 286, 532 },
    { -1187, 0, -327, -1146, 1351, 573, -327, 573, 286, 532 },
    { -1187, 0, -327, -1146, 1310, 573, -327, 573, 245, 491 },
    { -1105, 0, -245, -1024, 1351, 573, -327, 573, 286, 491 },
    { -1105, 0, -245, -1024, 1351, 573, -327, 532, 327, 491 },
    { -1024, 0, -163, -942, 1351, 532, -368, 532, 327, 491 },
    { -983, 0, 0, -942, 1392, 573, 368, 532, -245, 491 },  -- 238 F00EC (19)
    { -1024, 0, -40, -983, 1392, 573, 327, 532, -245, 491 },
    { -1024, 0, -40, -983, 1392, 573, 286, 532, -286, 491 },
    { -1064, 0, -81, -1024, 1392, 573, 245, 532, -327, 491 },
    { -1105, 0, -122, -1064, 1392, 573, 245, 532, -327, 491 },
    { -1187, 0, -204, -1146, 1392, 573, 163, 532, -368, 491 },
    { -1187, 0, -204, -1146, 1392, 573, 122, 532, -368, 491 },
    { -1187, 0, -204, -1146, 1392, 573, 81, 573, -368, 491 },
    { -1187, 0, -204, -1146, 1392, 573, 81, 573, -409, 491 },
    { -1187, 0, -204, -1146, 1392, 573, 122, 532, -409, 491 },
    { -1146, 0, -163, -1105, 1392, 573, 122, 532, -409, 491 },
    { -1105, 0, -122, -1064, 1392, 573, 163, 532, -368, 491 },
    { -1105, 0, -122, -1064, 1392, 573, 204, 532, -368, 491 },
    { -1064, 0, -81, -1024, 1392, 573, 245, 532, -327, 491 },
    { -1064, 0, -81, -1024, 1392, 573, 245, 532, -245, 491 },
    { -1064, 0, -81, -1024, 1392, 573, 286, 491, -245, 491 },
    { -983, 0, 0, -942, 1392, 573, 286, 491, -245, 491 },
    { -983, 0, 0, -942, 1392, 573, 327, 491, -245, 491 },
    { -901, 0, 0, -860, 1392, 573, 327, 491, -204, 532 },
    { -1310, 0, -245, -1228, 1351, 614, -491, -491, -942, -286 },  -- 257 F0268 (16)
    { -1310, 0, -245, -1228, 1351, 614, -450, -491, -942, -286 },
    { -1310, 0, -245, -1228, 1351, 614, -409, -491, -942, -286 },
    { -1392, 0, -327, -1310, 1351, 614, -368, -450, -942, -327 },
    { -1392, 0, -327, -1310, 1310, 614, -368, -491, -942, -327 },
    { -1474, 0, -409, -1392, 1269, 614, -327, -491, -901, -327 },
    { -1474, 0, -409, -1392, 1228, 614, -327, -491, -819, -327 },
    { -1474, 0, -409, -1392, 1228, 614, -327, -450, -819, -204 },
    { -1556, 0, -491, -1474, 1228, 614, -327, -450, -860, -204 },
    { -1515, 0, -450, -1433, 1228, 614, -368, -450, -860, -163 },
    { -1474, 0, -409, -1392, 1228, 573, -368, -491, -860, -204 },
    { -1433, 0, -368, -1351, 1228, 573, -409, -491, -860, -204 },
    { -1310, 0, -245, -1228, 1269, 573, -450, -491, -942, -245 },
    { -1146, 0, -81, -1064, 1269, 573, -450, -491, -942, -245 },
    { -1105, 0, -40, -1024, 1269, 573, -450, -491, -942, -245 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { -942, 0, 0, -901, 1392, 573, -327, 532, 286, 532 },  -- 273 F03A8 (14)
    { -1024, 0, 0, -983, 1351, 573, -368, 532, 286, 491 },
    { -1228, 0, 0, -1187, 1310, 573, -491, 491, 204, 450 },
    { -1310, 0, 0, -1269, 1269, 573, -491, 450, 40, 450 },
    { -1310, 0, 0, -1269, 1269, 573, -491, 450, 81, 491 },
    { -1310, 0, 0, -1269, 1269, 573, -491, 450, 122, 491 },
    { -1269, 0, 0, -1228, 1269, 573, -491, 450, 163, 491 },
    { -1269, 0, 0, -1228, 1310, 573, -491, 450, 163, 573 },
    { -1228, 0, 0, -1187, 1310, 532, -491, 491, 204, 573 },
    { -1146, 0, 0, -1105, 1310, 532, -409, 491, 204, 573 },
    { -1105, 0, 0, -1064, 1310, 532, -409, 491, 204, 573 },
    { -1064, 0, 0, -1024, 1310, 532, -368, 491, 204, 573 },
    { -1024, 0, 0, -983, 1310, 532, -368, 491, 204, 573 },
    { -942, 0, 0, -901, 1310, 532, -327, 491, 245, 614 },
    { -942, 0, -122, -901, 1392, 573, 368, 491, -204, 450 },  -- 287 F04C0 (26)
    { -983, 0, -163, -942, 1392, 573, 368, 491, -204, 450 },
    { -942, 0, -122, -901, 1392, 532, 409, 491, -204, 450 },
    { -942, 0, -122, -901, 1351, 532, 409, 450, -122, 491 },
    { -983, 0, -163, -942, 1351, 532, 409, 450, -122, 491 },
    { -983, 0, -163, -942, 1310, 532, 409, 450, 0, 614 },
    { -1024, 0, -204, -983, 1310, 532, 491, 450, 0, 573 },
    { -1024, 0, -204, -983, 1269, 532, 491, 450, 81, 491 },
    { -983, 0, -163, -942, 1269, 532, 491, 450, 81, 491 },
    { -983, 0, -163, -942, 1310, 532, 491, 450, 0, 450 },
    { -1024, 0, -204, -983, 1310, 532, 491, 450, 0, 450 },
    { -983, 0, -163, -942, 1310, 532, 491, 450, 0, 450 },
    { -942, 0, -122, -901, 1310, 532, 491, 450, -40, 409 },
    { -942, 0, -122, -901, 1310, 532, 491, 450, -122, 327 },
    { 6912, 0, 0, 0, 0, 0, 0, 0, 6912, 0 },
    { 0, 0, 0, 6400, 25, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 6144 },
    { 5888, 8192, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 5632, 22, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 40, 0, 0, 0, 0 },
    { 0, 768, 0, 34, 0, 0, 0, 0, 7168, 768 },
    { 36, 0, 0, 0, 0, 0, 0, 0, 0, 7936 },
    { 0, 7424, 0, 0, 0, 9472, 9728, 0, 0, 1536 },
    { 6, 0, 0, 9472, 9728, 7680, 0, 8448, 33, 0 },
    { 0, 0, 0, 0, 0, 8448, 33, 0, 0, 8960 },
    { 0, 39, 1280, 1798, 264, 2569, 3083, 3330, 3584, 783 },
}
local K_JUMP_ATTACK, K_TO_JUMP, K_JUMP_START, K_RUN, K_IDLE, K_WALK, K_RECOIL_FRONT = 0, 30, 42, 43, 51, 54, 65
local K_RECOIL_FRONT_END, K_RECOIL_RIGHT, K_RECOIL_RIGHT_END, K_RECOIL_LEFT, K_RECOIL_LEFT_END = 82, 83, 100, 101, 119
local K_JUMP_TO_STUN, K_TO_STUN_RIGHT, K_TO_STUN_LEFT, K_STUN_JUMP, K_STUN_RIGHT, K_STUN_LEFT = 120, 156, 186, 217, 229, 238
local K_DEATH_JUMP, K_DEATH_RIGHT, K_DEATH_LEFT = 257, 273, 287

local function player()
    return record(0)
end

local function b(v)
    return v and 1 or 0
end

local function turn_by(c, delta, rate)
    if delta > 0 then
        c.rot_y = c.rot_y + q12_mul(sh.game.delta_time(), rate)
    else
        c.rot_y = c.rot_y - q12_mul(sh.game.delta_time(), rate)
    end
end

-- Speed towards `limit` at `rate` per second, the way the game's run code approaches it.
local function speed_toward_alt(c, rate, limit)
    local step = q12_mul(sh.game.delta_time(), rate)
    local speed = c.move_speed
    if speed > limit then
        c.move_speed = max(speed - step, limit)
    else
        local v = speed + step
        c.move_speed = v > limit and limit or v
    end
end

local function run_speed(c)
    return q12_mul(c.groaner_speed, 14745)
end

local function start(c)
    sh.entity.props_clear(c.slot)
    c.groaner_flags = 0
    c.anim_alpha = 0
    local base = sh.entity.param(c.slot, "health_base", 860160)
    local bonus = sh.entity.param(c.slot, "health_bonus_max", 286720)
    c.health = base + sh.rng.rand16() % bonus
    local difficulty = sh.save.game_difficulty()
    if difficulty == 0 and rng_int(0, 7) == 0 then
        c.health = c.health * 2
    end
    if difficulty == 1 and rng_int(0, 3) == 0 then
        c.health = c.health * 2
    end
    c.move_speed = 0
    sh.entity.set_collision_state(c.slot, 3)
    c.heading_angle = c.rot_y
    c.groaner_speed = rng_uint(3840, 4351)
    if difficulty == 1 then
        c.groaner_speed = c.groaner_speed + (c.groaner_speed & 0xFFFFFFFF) // 8
    end
    if difficulty == -1 then
        c.groaner_speed = c.groaner_speed - asr(s32((c.groaner_speed & 0xFFFF) << 16), 20)
    end
    sh.entity.clip_table_attach(c.slot)
    damage_clear(c)
    if c.state_step == 3 then
        c.control_state = WALK
        anim_set(c, st(A.walk_forward, true), 371)
        c.groaner_flags = c.groaner_flags | AWAKE
    end
    c.state_step = 0
    c.groaner_target_x = c.pos_x
    c.groaner_target_z = c.pos_z
    if TOWN and difficulty ~= 1 then
        if not sh.flag_get("M1S06_WokeUpAfterBoss") then
            c.groaner_speed = asr(c.groaner_speed * 3276, 12)
        else
            c.groaner_speed = asr(c.groaner_speed * 3686, 12)
        end
    end
end

local function react_to_damage(c)
    local p = player()
    if c.health > 0 then
        local _, ground_type = sh.entity.ground_at(c.pos_x, c.pos_z)
        if ground_type == 12 and rng_int(16384, 28671) < c.pos_y then
            c.damage_amount = 4096000
        end
    end
    if c.damage_amount == 0 or c.health < 0 then
        return
    end

    local prev = c.control_state
    if c.health > 0 then
        c.groaner_voice_request = 4
    end
    c.groaner_target_x = p.pos_x
    c.groaner_target_z = p.pos_z
    c.groaner_flags = c.groaner_flags | SEES_PLAYER
    c.health = max(c.health - c.damage_amount, 0)
    local dx, dz = c.damage_x, c.damage_z
    local dist = sh.math.sqrt(s32(s32(dx * dx) + s32(dz * dz)))
    local heading
    if dx ~= 0 or dz ~= 0 then
        heading = s16(sh.math.atan2(dx, dz))
    else
        heading = p.rot_y
    end
    local side = angle_signed((heading - c.rot_y) + 2048)
    c.groaner_spin = max(asr(dist, 2), 1024)
    local lift = s32(c.damage_y << 12)
    c.heading_angle = heading
    damage_clear(c)
    c.groaner_flags = c.groaner_flags | HIT
    c.move_speed = idiv(s32(dist << 12), 4915)
    c.fall_speed = c.fall_speed + idiv(lift, 4915)

    local clip = c.anim_status >> 1
    if c.groaner_flags & DOWN ~= 0 then
        c.groaner_spin = 0
        if c.health ~= 0 then
            if clip == st(A.stand_recoil_front, false) then
                if c.anim_status ~= st(A.stun_from_jump, true) then
                    c.groaner_recoil_keyframe = 0
                else
                    c.groaner_recoil_keyframe = asr(c.anim_time, 12) - 78
                end
                c.anim_status = st(A.stun_from_jump_recoil, false)
            elseif clip == A.stun_from_stand_right then
                if c.anim_status ~= st(A.stun_from_stand_right, true) then
                    c.groaner_recoil_keyframe = 0
                else
                    c.groaner_recoil_keyframe = asr(c.anim_time, 12) - 165
                end
                c.anim_status = st(A.stun_from_stand_right_recoil, false)
            elseif clip == A.stun_from_stand_left then
                if c.anim_status ~= st(A.stun_from_stand_left, true) then
                    c.groaner_recoil_keyframe = 0
                else
                    c.groaner_recoil_keyframe = asr(c.anim_time, 12) - 319
                end
                c.anim_status = st(A.stun_from_stand_left_recoil, false)
            end
        end
    elseif c.health > 409600 then
        if c.groaner_flags & RUNNING == 0 then
            if abs(side) < 512 then
                if clip ~= A.stand_recoil_front then
                    c.anim_status = st(A.stand_recoil_front, false)
                end
            elseif side > 0 then
                if clip ~= A.stand_recoil_right then
                    c.anim_status = st(A.stand_recoil_right, false)
                end
            elseif clip ~= A.stand_recoil_left then
                c.anim_status = st(A.stand_recoil_left, false)
            end
            c.control_state = RECOIL
        end
    else
        sh.entity.enemy_defeated(c.slot)
        c.flags = c.flags | 0x2
        c.groaner_flags = c.groaner_flags | DOWN | DYING
        if abs(side) < 512 then
            c.anim_status = st(A.jump_to_stun, false)
            c.control_state = STUN_JUMP
        elseif side > 0 then
            c.anim_status = st(A.stand_to_stun_right, false)
            c.control_state = STUN_RIGHT
        else
            c.anim_status = st(A.stand_to_stun_left, false)
            c.control_state = STUN_LEFT
        end
    end

    if prev ~= c.control_state and prev == JUMP_ATTACK then
        set_group_flags(3, group_flags(3) & ~2)
    end
end

local function walk(c)
    local dt = sh.game.delta_time()
    speed_toward_alt(c, 24576, q12_mul(c.groaner_speed, 2048))
    if c.groaner_flags & TURNING ~= 0 then
        local d = angle_signed(c.groaner_turn_target - c.rot_y)
        if turn_step(3, 2) < abs(d) then
            turn_by(c, d, 682)
        else
            c.rot_y = c.groaner_turn_target
            c.groaner_flags = c.groaner_flags & ~TURNING
        end
    else
        if sh.entity.path_hit(c.slot, 3481, c.rot_y) ~= 0 then
            c.groaner_turn_target = sh.entity.clear_heading(c.slot, 3481, c.rot_y)
            if c.groaner_turn_target == 4096 then
                c.groaner_turn_target = -c.rot_y
            end
            c.groaner_flags = c.groaner_flags | TURNING
        else
            local r = rng_int(8192, 24575)
            local dx = s32(c.pos_x - c.groaner_target_x)
            local dz = s32(c.pos_z - c.groaner_target_z)
            local far
            if dx > dz then
                far = abs(dx) + abs(asr(dz, 1))
            else
                far = abs(asr(dx, 1)) + abs(dz)
            end
            if r < far then
                local off = angle_signed(c.rot_y - sh.math.atan2(dx, dz))
                if abs(off) < rng_uint(512, 575) then
                    local tx = c.groaner_target_x + rng_int(-2048, 2047)
                    local tz = c.groaner_target_z + rng_int(-2048, 2047)
                    c.groaner_turn_target = sh.entity.heading_toward(c.slot, 3481, tx, tz, 4096, true)
                    if c.groaner_turn_target == 4096 then
                        c.groaner_turn_target = -c.rot_y
                    end
                    c.groaner_flags = c.groaner_flags | TURNING
                end
            end
        end
    end
    if c.anim_status == st(A.walk_forward, true) and c.groaner_flags_word & (AWAKE | SEES_PLAYER) == AWAKE | SEES_PLAYER then
        c.control_state = RUN
        c.anim_status = st(A.run_forward, false)
        c.groaner_flags = c.groaner_flags & ~TURNING
        c.groaner_flags = c.groaner_flags | RUNNING
    end
end

local function step_timer(c)
    local dt = sh.game.delta_time()
    if c.groaner_timer > 0 then
        c.groaner_timer = c.groaner_timer - dt
        if c.groaner_timer < 0 then
            c.groaner_timer = 0
        end
    else
        c.groaner_timer = c.groaner_timer + dt
        if c.groaner_timer > 0 then
            c.groaner_timer = 0
        end
    end
end

local function face_target(c, times)
    local dt = sh.game.delta_time()
    for _ = 1, times do
        local d = angle_signed(c.groaner_turn_target - c.rot_y)
        if asr(dt, 3) + 1 < abs(d) then
            turn_by(c, d, 1024)
        else
            c.rot_y = c.groaner_turn_target
        end
    end
end

local function can_leap(p)
    return p.flags & 0x8 == 0 and group_flags(3) & 2 == 0 and p.health > 0 and sh.entity.sys("sys_state") == 0
end

local function run(c)
    local p = player()
    local dt = sh.game.delta_time()
    if c.groaner_flags & HIT ~= 0 then
        return
    end
    if c.groaner_flags & STATE_STARTED ~= 0 then
        c.groaner_timer = 0
        c.groaner_flags = c.groaner_flags | STATE_STARTED
    end
    local dist = distance_2d(p.pos_x - c.pos_x, p.pos_z - c.pos_z)
    local dist_max = rng_int(16384, 32767)
    local blocked = sh.entity.player_blocked(c.slot) ~= 0
    if dist < dist_max and not blocked then
        c.groaner_turn_target = heading_to(c.pos_x, c.pos_z, p.pos_x, p.pos_z)
    else
        local probe = 3686 - rng_int(0, 920)
        local hit = sh.entity.path_hit(c.slot, probe, c.rot_y) ~= 0
        local timer = c.groaner_timer
        if (timer >= 0 and ((hit and (blocked or dist < probe)) or timer == 0)) or rng_uint(0, 31) == 0 then
            local reach
            if hit then
                reach = probe + rng_int(0, asr(probe, 2) - 1)
            else
                reach = rng_int(6144, 7679)
            end
            local heading = sh.entity.heading_toward(c.slot, reach, c.groaner_target_x, c.groaner_target_z, 4096, true)
            if heading == 4096 then
                heading = sh.entity.clear_heading(c.slot, asr(probe * 3, 2), c.rot_y)
            end
            if hit then
                c.groaner_timer = -rng_int(1536, 3071)
            else
                c.groaner_timer = rng_int(1536, 6143)
            end
            c.groaner_turn_target = heading
        end
    end
    step_timer(c)
    face_target(c, 3)

    local d = angle_signed(c.groaner_turn_target - c.rot_y)
    if d < 227 then
        speed_toward_alt(c, 38912, run_speed(c))
    else
        local scale = s16(4096 - asr((d - 227) * 3, 1))
        speed_toward_alt(c, 38912, q12_mul(run_speed(c), scale))
    end

    local leap
    if dist > 12288 then
        leap = false
    elseif dist < 6144 then
        leap = true
    else
        leap = (sh.rng.rand16() & 0xFFF) > asr(idiv(s32(dist << 12), 12288), 2)
    end
    if leap and not blocked then
        if can_leap(p) and (c.pos_y - p.pos_y) > -3276 then
            if c.pos_y - p.pos_y < 3276 then
                local a = angle_signed(sh.math.atan2(s32(p.pos_x - c.pos_x), s32(p.pos_z - c.pos_z)) - c.rot_y)
                if abs(a) < 341 then
                    set_group_flags(3, group_flags(3) | 2)
                    c.control_state = JUMP_ATTACK
                    c.anim_status = st(A.stand_to_jump_attack, false)
                    return
                end
            end
        end
    end
    if dist < 4915 then
        if p.flags & 0x8 ~= 0 or group_flags(3) & 2 ~= 0 or p.health <= 0 or sh.entity.sys("sys_state") ~= 0
            or rng_int(0, 15) == 0 then
            c.control_state = CIRCLE
            return
        end
    end
    if c.groaner_flags & SEES_PLAYER == 0 and (dist > 49152 or (rng_uint(0, 127) == 0 and dist > 24576)) then
        c.control_state = WALK
        c.anim_status = st(A.walk_forward, false)
        c.groaner_recoil_keyframe = 100
        c.groaner_flags = c.groaner_flags & ~RUNNING
    end
end

local function in_window(keyframe, from, count)
    local rel = keyframe - from
    return rel >= 0 and rel < count
end

local function jump_attack(c)
    local p = player()
    local dt = sh.game.delta_time()
    local dist = distance_2d(p.pos_x - c.pos_x, p.pos_z - c.pos_z)
    local to_player = angle_signed(heading_to(c.pos_x, c.pos_z, p.pos_x, p.pos_z) - c.rot_y)
    local clip = c.anim_status >> 1
    if clip == A.stand_to_jump_attack or in_window(asr(c.anim_time, 12), 214, 9) then
        local t = turn_step(1, 3)
        if (to_player >= 0 and t < to_player) or (to_player < 0 and t < -to_player) then
            turn_by(c, to_player, 1024)
            local to_player_now = angle_signed(heading_to(c.pos_x, c.pos_z, p.pos_x, p.pos_z) - c.rot_y)
            local r = turn_step(1, 3)
            if (to_player_now >= 0 and r < to_player_now) or (to_player_now < 0 and r < -to_player_now) then
                turn_by(c, to_player_now, 1024)
            else
                c.rot_y = heading_to(c.pos_x, c.pos_z, p.pos_x, p.pos_z)
            end
        else
            c.rot_y = heading_to(c.pos_x, c.pos_z, p.pos_x, p.pos_z)
        end
        c.heading_angle = c.rot_y
    else
        local limit = c.rot_y + 512
        local step = q12_mul(dt, 771)
        if c.heading_angle + step < limit then
            c.heading_angle = c.heading_angle + step
        else
            c.heading_angle = c.rot_y + 512
        end
    end

    clip = c.anim_status >> 1
    if clip == A.stand_to_jump_attack then
        speed_toward(c, 38912, 0)
    elseif clip == A.jump_attack then
        if c.groaner_flags & JUMPED == 0 then
            c.groaner_flags = c.groaner_flags | JUMPED
            local v = s32((dist - (p.cylinder_extent + 573)) << 1)
            c.move_speed = min(v, 18432)
            local dy = s32(p.pos_y - c.pos_y)
            c.attack_window = 1
            c.groaner_voice_request = 2
            c.fall_speed = q12_mul(dy - 5734, 9625)
        end
    end

    local keyframe = asr(c.anim_time, 12)
    if in_window(keyframe, 222, 2) then
        sh.entity.attack(c.slot, 1, JUMP_BITE, c.pos_x + c.cylinder_offset_x, c.pos_y - 3276,
            c.pos_z + c.cylinder_offset_z, 0, c.rot_y, 1024)
    end
    if in_window(keyframe, 222, 14) then
        c.fall_speed = c.fall_speed + sh.game.gravity_speed()
        speed_toward(c, 1228, 0)
    end
    if in_window(keyframe, 236, 7) then
        speed_toward(c, 4915, 0)
    end
    if c.anim_status >> 1 == A.run_forward then
        c.control_state = CIRCLE
        c.rot_y = c.rot_y + 512
        c.groaner_heading_target = c.groaner_heading_target + 512
        set_group_flags(3, group_flags(3) & ~2)
        anim_set(c, st(A.run_forward, true), 363)
        c.groaner_flags = c.groaner_flags & ~JUMPED
    end
end

local function circle(c)
    local p = player()
    local dt = sh.game.delta_time()
    if c.groaner_flags & HIT ~= 0 then
        return
    end
    if c.groaner_flags & STATE_STARTED == 0 then
        c.groaner_circle_timer = 0
        c.groaner_timer = 0
        c.groaner_flags = c.groaner_flags | STATE_STARTED
    end
    local hit = sh.entity.path_hit(c.slot, 3686 - rng_int(0, 920), c.rot_y) ~= 0
    if c.groaner_timer == 0 or rng_uint(0, 31) == 0 or (hit and c.groaner_timer >= 0) then
        local reach = 6144 - rng_int(0, 1535)
        c.groaner_turn_target = sh.entity.heading_toward(c.slot, reach, c.groaner_target_x, c.groaner_target_z, 4096, false)
        if c.groaner_turn_target == 4096 then
            c.groaner_turn_target = sh.entity.clear_heading(c.slot, asr(reach * 3, 2), c.rot_y)
        end
        if hit then
            c.groaner_timer = -rng_int(1536, 3071)
        else
            c.groaner_timer = rng_int(3072, 6143)
        end
    end
    step_timer(c)
    face_target(c, 2)

    local d = angle_signed(c.groaner_turn_target - c.rot_y)
    if c.groaner_timer >= 0 then
        speed_toward_alt(c, 38912, run_speed(c))
    else
        d = 4096 - asr((d - 227) * 3, 1)
        speed_toward_alt(c, 38912, q12_mul(d, q12_mul(c.groaner_speed, 0x3999)))
    end

    local urge = 0
    c.groaner_circle_timer = c.groaner_circle_timer + dt
    local t = c.groaner_circle_timer
    if t > 24576 then
        if t > 30720 then
            urge = 2048
        else
            urge = asr(q12_mul(t - 24576, 6144), 2) + 1024
        end
    end
    local dist = distance_2d(p.pos_x - c.pos_x, p.pos_z - c.pos_z)
    if dist > 14336 then
        if dist > 21504 then
            urge = urge + 2048
        else
            urge = urge + 1024 + asr(q12_mul(dist - 14336, 7168), 2)
        end
    end
    if rng_uint(0, 4095) < urge then
        c.control_state = RUN
    end
end

local function recoil(c)
    if c.groaner_flags & HIT == 0 and c.anim_status == st(A.stand_idle, true) and rng_int(0, 7) == 0 then
        c.control_state = WALK
        c.anim_status = st(A.walk_forward, false)
    end
end

local function stunned(c)
    local dt = sh.game.delta_time()
    if c.groaner_flags & HIT == 0 then
        speed_toward(c, 7372, 0)
    end
    if c.groaner_flags & AIRBORNE == 0 then
        c.groaner_flags = c.groaner_flags & ~SLIDING
    end
    local clip = c.anim_status >> 1
    if c.control_state == STUN_JUMP then
        local keyframe = asr(c.anim_time, 12)
        if keyframe >= 39 and keyframe <= 48 then
            c.groaner_flags = c.groaner_flags | SLIDING
        end
        if c.groaner_flags & SLIDING ~= 0 then
            local slide
            if c.groaner_flags & AWAKE ~= 0 then
                slide = s16(asr(dt * 21845, 12))
            else
                slide = s16(asr(dt * 13184, 12))
            end
            c.groaner_push_x = asr(s32(slide * sh.math.sin(c.rot_y + 2048)), 12)
            c.groaner_push_z = asr(s32(slide * sh.math.cos(c.rot_y + 2048)), 12)
        end
        if clip ~= A.stun_from_jump then
            return
        end
    else
        local keyframe = asr(c.anim_time, 12)
        if (keyframe >= 134 and keyframe < 157) or (keyframe >= 288 and keyframe < 311) then
            c.groaner_flags = c.groaner_flags | SLIDING
        end
        if c.groaner_flags & SLIDING ~= 0 then
            local slide
            if c.groaner_flags & AWAKE ~= 0 then
                slide = s16(asr(dt * 6944, 12))
            else
                slide = s16(asr(dt * 4006, 12))
            end
            if c.control_state == STUN_RIGHT then
                c.groaner_push_x = q12_mul32(slide, sh.math.sin(c.rot_y - 1024))
                c.groaner_push_z = q12_mul32(slide, sh.math.cos(c.rot_y - 1024))
                if clip ~= A.stun_from_stand_right then
                    return
                end
            else
                c.groaner_push_x = q12_mul32(slide, sh.math.sin(c.rot_y + 1024))
                c.groaner_push_z = q12_mul32(slide, sh.math.cos(c.rot_y + 1024))
                if clip ~= A.stun_from_stand_left then
                    return
                end
            end
        end
    end

    if c.groaner_flags & AIRBORNE == 0 and c.health == 0 then
        clip = c.anim_status >> 1
        local new = clip == A.stun_from_jump and st(A.stun_from_jump_death_start, false) or 0
        c.flags = c.flags & ~0x2
        if clip == A.stun_from_stand_right then
            new = st(A.stun_from_stand_right_death_start, false)
        end
        if clip == A.stun_from_stand_left then
            new = st(A.stun_from_stand_left_death_start, false)
        end
        if new ~= 0 then
            c.groaner_recoil_keyframe = 0
            c.anim_status = new
            c.control_state = DEATH
        end
    end
end

local function dead(c)
    if c.groaner_flags & HIT == 0 then
        speed_toward(c, 7372, 0)
    end
    if c.health == 0 then
        if sh.entity.sys("target_npc") ~= npc_index(c.slot) then
            c.health = -1
            sh.entity.set_collision_state(c.slot, 0)
        end
    end
    if c.groaner_flags_word & (AIRBORNE | DEATH_CLIP) == 0 and c.move_speed == 0 then
        local clip = c.anim_status >> 1
        local new = 0
        if clip == st(A.stand_idle, false) then
            new = st(A.stand_recoil_front, true)
        end
        if clip == st(A.stand_idle, true) then
            new = st(A.jump_to_stun, false)
        end
        if clip == st(A.jump_attack, false) then
            new = st(A.jump_to_stun, true)
        end
        if new ~= 0 then
            sh.entity.anim_status_set(c.slot, new, 6)
            c.groaner_flags = c.groaner_flags | DEATH_CLIP
        end
    end
end

local CONTROL = {
    [WALK] = walk, [RUN] = run, [JUMP_ATTACK] = jump_attack, [CIRCLE] = circle, [RECOIL] = recoil,
    [STUN_JUMP] = stunned, [STUN_RIGHT] = stunned, [STUN_LEFT] = stunned, [DEATH] = dead,
}

local function think(c)
    local p = player()
    local light = map_light() & 3
    local sight = SIGHT[b(light == 0) * 2 + b(light == 2)]
    local reach, step, loudness = sight[1], sight[2], TOWN and 2457 or 4096
    local sense = c.groaner_sense_scale
    if sense ~= 0 then
        reach, step, loudness = q12_mul(reach, sense), q12_mul(step, sense), q12_mul(loudness, sense)
    end
    local dist = distance_2d(p.pos_x - c.pos_x, p.pos_z - c.pos_z)
    if sh.entity.player_noise(c.slot, dist, loudness) ~= 0
        or sh.entity.player_visible(c.slot, 1, reach, step) ~= 0 then
        c.groaner_flags = c.groaner_flags | SEES_PLAYER
        if c.groaner_flags & AWAKE ~= 0 then
            c.groaner_target_x = p.pos_x
            c.groaner_target_z = p.pos_z
        end
    else
        c.groaner_flags = c.groaner_flags & ~SEES_PLAYER
    end
    if c.anim_status ~= st(A.jump_attack, true) then
        c.attack_window = 0
    end
    local control = CONTROL[c.control_state]
    if control then
        control(c)
    end
end

local function physics(c)
    local dt = sh.game.delta_time()
    if c.anim_status ~= st(A.jump_attack, true) then
        c.fall_speed = c.fall_speed + sh.game.gravity_speed()
    else
        c.fall_speed = c.fall_speed + asr(sh.game.gravity_speed(), 1)
    end
    if c.groaner_flags & HIT ~= 0 then
        if c.groaner_flags & AIRBORNE == 0 then
            if c.anim_status == st(A.run_forward, true) then
                speed_toward(c, 29488, 0)
            else
                speed_toward(c, 7372, 0)
            end
        end
        if c.groaner_spin > 0 then
            local side = angle_signed((c.heading_angle - c.rot_y) + 2048)
            if abs(side) < 512 then
                side = -2048
            elseif side > 0 then
                side = -1024
            else
                side = 1024
            end
            if angle_signed(c.heading_angle - (c.rot_y + side)) > 0 then
                c.rot_y = c.rot_y + q12_mul(dt, c.groaner_spin)
                if angle_signed(c.heading_angle - (c.rot_y + side)) <= 0 then
                    c.groaner_spin = 0
                    c.rot_y = c.heading_angle - side
                end
            else
                c.rot_y = c.rot_y - q12_mul(dt, c.groaner_spin)
                if angle_signed(c.heading_angle - (c.rot_y + side)) >= 0 then
                    c.groaner_spin = 0
                    c.rot_y = c.heading_angle - side
                end
            end
            c.groaner_spin = max(c.groaner_spin - q12_mul(dt, 1365), 0)
        elseif c.move_speed < 921 then
            c.groaner_flags = c.groaner_flags & ~HIT
        end
    elseif c.anim_status ~= st(A.jump_attack, true) then
        c.heading_angle = c.rot_y
    end
    local _, _, _, _, ground = sh.entity.move(c.slot, c.groaner_push_x, c.groaner_push_z)
    c.groaner_push_z = 0
    c.groaner_push_x = 0
    if c.pos_y == ground then
        c.groaner_flags = c.groaner_flags & ~AIRBORNE
    else
        c.groaner_flags = c.groaner_flags | AIRBORNE
    end
end

local function animate(c)
    local dt = sh.game.delta_time()
    local status = c.anim_status
    if status == st(A.jump_attack, true) then
        local keyframe = asr(c.anim_time, 12)
        if keyframe >= 207 and keyframe <= 219 then
            sh.entity.clip_duration_set(c.slot, status, 98304)
        end
        if keyframe >= 220 and keyframe <= 225 then
            sh.entity.clip_duration_set(c.slot, status, 98304)
        end
        if keyframe >= 226 and keyframe <= 243 then
            sh.entity.clip_duration_set(c.slot, status, 122880)
        end
    elseif status == st(A.jump_to_stun, true) then
        sh.entity.clip_duration_set(c.slot, status, c.groaner_flags & AWAKE ~= 0 and 131072 or 105472)
    elseif status == st(A.stand_to_stun_right, true) or status == st(A.stand_to_stun_left, true) then
        sh.entity.clip_duration_set(c.slot, status, c.groaner_flags & AWAKE ~= 0 and 122880 or 92160)
    elseif status == st(A.run_forward, true) then
        local speed
        if c.groaner_heading_target == c.rot_y then
            speed = c.move_speed
        else
            speed = c.move_speed >= 4096 and c.move_speed or 4096
        end
        sh.entity.clip_duration_set(c.slot, status, idiv(s32(s32(speed * 20) << 12), run_speed(c)))
    end

    anim_play(c)

    -- The spine and head follow the turn.
    status = c.anim_status
    local rate, lean
    if status == st(A.walk_forward, false) or status == st(A.walk_forward, true) then
        rate, lean = q12_mul(dt, 1024), 384
    elseif status == st(A.jump_attack, false) or status == st(A.jump_attack, true)
        or status == st(A.stand_to_jump_attack, false) or status == st(A.stand_to_jump_attack, true)
        or status == st(A.run_forward, false) or status == st(A.run_forward, true) then
        rate, lean = q12_mul(dt, 1536), 640
    else
        rate, lean = q12_mul(dt, 1536), 0
    end
    rate = s16(rate)
    local d = angle_signed(c.groaner_heading_target - c.rot_y)
    if d == 0 then
        lean = 0
    elseif d < 0 then
        lean = -lean
    end
    local flex = c.groaner_flex
    if flex > lean then
        c.groaner_flex = max(lean, flex - rate)
    else
        local v = flex + rate
        c.groaner_flex = lean > v and v or lean
    end
    flex = c.groaner_flex
    sh.entity.bone_rotate(c.slot, 3, 0, s16(asr(flex, 2)), 0)
    sh.entity.bone_rotate(c.slot, 4, 0, s16(asr(flex, 2)), 0)
    sh.entity.bone_rotate(c.slot, 1, 0, flex, 0)

    c.rot_y = angle_signed(c.rot_y)
    c.groaner_heading_target = c.rot_y
end

-- Index into a keyframe run: `offset` minus one past `count`, held below `limit`.
local function run_index(offset, keyframe, limit, count)
    local v = offset - b(keyframe > count)
    if v < limit then
        return v
    end
    return limit - 1
end

-- Index into the side-stun run (right) for a keyframe of the clip.
local function stun_right_index(k)
    local k0
    if k < 10 then
        if k < 6 then
            k0 = 1
            local k1 = asr(k, 1)
            if k1 < 2 then
                k0 = k1
            end
        else
            k0 = 4
            local k1 = k - 4
            if k1 < 5 then
                k0 = k1
            end
        end
    elseif k >= 18 then
        k0 = 8
        local k1 = asr(k, 1) - 3
        if k1 < 9 then
            k0 = k1
        end
    else
        k0 = 5
    end
    return k0
end

local function stun_left_index(k)
    local k0 = asr(k, 1)
    if k >= 7 then
        if k < 16 then
            k0 = run_index(k - 3, k, 9, 9)
        else
            k0 = (k - 7) - b(k >= 20)
        end
    end
    return k0
end

local function collision(c)
    local status = c.anim_status
    local keyframe = asr(c.anim_time, 12)
    local rel = c.groaner_recoil_keyframe
    local function mix(a, b2)
        collision_between(c, K[a], K[b2])
    end
    local function set(a)
        collision_from_keyframe(c, K[a])
    end

    if status == st(A.stand_idle, false) then
        if rel == 5 then
            mix(K_RECOIL_FRONT_END, K_IDLE)
        elseif rel == 13 then
            mix(K_RECOIL_RIGHT_END, K_IDLE)
        elseif rel == 25 then
            mix(K_RECOIL_LEFT_END, K_IDLE)
        end
    elseif status == st(A.stand_idle, true) then
        local k = keyframe - 191
        local k0 = b(k >= 5) + b(k >= 12)
        local k1 = b(k >= 4) + b(k >= 11)
        if k == 15 then
            k1 = k1 - 2
        end
        mix(K_IDLE + k0, K_IDLE + k1)
        c.groaner_recoil_keyframe = k
    elseif status == st(A.walk_forward, false) then
        if rel ~= 100 then
            local k0 = b(rel >= 5) + b(rel >= 12)
            mix(K_IDLE + k0, K_WALK)
        else
            set(K_WALK)
        end
    elseif status == st(A.walk_forward, true) then
        local k = keyframe - 371
        local k0, k1
        if k < 12 then
            if k < 9 then
                k0 = k < 4 and k or 3
            else
                k0 = 4
            end
        elseif k < 22 then
            k0 = (asr(k, 1) - 1 < 8) and asr(k, 1) - 1 or 7
        else
            k0 = k - 14 - b(k >= 24)
        end
        if k < 11 then
            if k < 8 then
                k1 = (k + 1 < 4) and k + 1 or 3
            else
                k1 = 4
            end
        elseif k < 21 then
            k1 = (asr(k - 1, 1) < 8) and asr(k - 1, 1) or 7
        elseif k ~= 25 then
            k1 = (k - 13) - b(k >= 23)
        else
            k1 = 0
        end
        mix(K_WALK + k0, K_WALK + k1)
    elseif status == st(A.stand_recoil_front, false) then
        set(K_RECOIL_FRONT + 2)
    elseif status == st(A.stand_recoil_front, true) then
        local k = keyframe - 16
        local k0 = (k - b(k > 0)) - b(k >= 10)
        local k1 = (k - b(k >= 9)) - b(k >= 19)
        mix(K_RECOIL_FRONT + k0, K_RECOIL_FRONT + k1)
        c.groaner_recoil_keyframe = 5
    elseif status == st(A.stand_recoil_right, false) then
        set(K_RECOIL_RIGHT + 2)
    elseif status == st(A.stand_recoil_right, true) then
        local k = keyframe - 111
        local k0 = (k - b(k > 0)) - b(k >= 11)
        local k1 = k - b(k >= 10)
        mix(K_RECOIL_RIGHT + k0, K_RECOIL_RIGHT + k1)
        c.groaner_recoil_keyframe = 13
    elseif status == st(A.stand_recoil_left, false) then
        set(K_RECOIL_LEFT + 3)
    elseif status == st(A.stand_recoil_left, true) then
        local k = keyframe - 265
        local k0 = k - b(k >= 9)
        local k1 = (keyframe - 264) - b(k >= 8)
        mix(K_RECOIL_LEFT + k0, K_RECOIL_LEFT + k1)
        c.groaner_recoil_keyframe = 25
    elseif status == st(A.jump_to_stun, false) then
        set(K_JUMP_TO_STUN)
    elseif status == st(A.jump_to_stun, true) then
        local k = keyframe - 37
        local k0 = ((keyframe - 39) - b(k >= 17)) - b(k >= 38)
        local k1 = (((keyframe - 38) - b(k >= 16)) - b(k >= 37)) - b(k >= 39)
        mix(K_JUMP_TO_STUN + k0, K_JUMP_TO_STUN + k1)
    elseif status == st(A.stand_to_stun_right, false) then
        set(K_TO_STUN_RIGHT)
    elseif status == st(A.stand_to_stun_right, true) then
        local k = keyframe - 131
        local k0 = (keyframe - 134) - b(k >= 31)
        local k1 = (keyframe - 133) - b(k >= 30)
        mix(K_TO_STUN_RIGHT + k0, K_TO_STUN_RIGHT + k1)
    elseif status == st(A.stand_to_stun_left, false) then
        set(K_TO_STUN_LEFT)
    elseif status == st(A.stand_to_stun_left, true) then
        local k = keyframe - 285
        local k0 = (k - b(k > 0)) - b(k >= 31)
        local k1 = (k - b(k >= 30)) - b(k >= 32)
        mix(K_TO_STUN_LEFT + k0, K_TO_STUN_LEFT + k1)
    elseif status == st(A.stun_from_jump, false) then
        set(K_STUN_JUMP)
    elseif status == st(A.stun_from_jump, true) then
        local k = keyframe - 78
        local k0 = keyframe - 82
        if k < 9 then
            if k <= 0 then
                k0 = (k < 6) and k - b(k > 0) or 5
            else
                k0 = (k - 1 < 6) and k - b(k > 0) or 5
            end
        end
        local k1
        if k ~= 15 then
            if k < 8 then
                k1 = k < 6 and k or 5
            else
                k1 = k - 3
            end
        else
            k1 = 0
        end
        mix(K_STUN_JUMP + k0, K_STUN_JUMP + k1)
    elseif status == st(A.stun_from_stand_right, false) then
        set(K_STUN_RIGHT)
    elseif status == st(A.stun_from_stand_right, true) then
        local k = keyframe - 165
        local k0 = stun_right_index(k)
        local k1
        if k < 17 then
            if k < 5 then
                k1 = k < 2 and k or 1
            else
                k1 = run_index(k - 3, k, 6, 7)
            end
        elseif k ~= 25 then
            k1 = (asr(k - 1, 1) - 2 < 9) and asr(k - 1, 1) - 2 or 8
        else
            k1 = 0
        end
        mix(K_STUN_RIGHT + k0, K_STUN_RIGHT + k1)
    elseif status == st(A.stun_from_stand_left, false) then
        set(K_STUN_LEFT)
    elseif status == st(A.stun_from_stand_left, true) then
        local k = keyframe - 319
        local k0 = stun_left_index(k)
        local k1
        if k < 9 then
            k1 = k - b(k >= 2) - b(k >= 4)
        elseif k < 15 then
            k1 = (k - 5 < 9) and k - 5 or 8
        elseif k ~= 26 then
            k1 = (k - 6) - b(k >= 19)
        else
            k1 = 0
        end
        mix(K_STUN_LEFT + k0, K_STUN_LEFT + k1)
    elseif status == st(A.stun_from_jump_death_start, false) or status == st(A.stun_from_jump_recoil, false) then
        local k0
        if rel < 9 then
            k0 = run_index(rel, rel, 6, 0)
        else
            k0 = rel - 4
        end
        mix(K_STUN_JUMP + k0, K_DEATH_JUMP)
    elseif status == st(A.stun_from_jump_death_start, true) or status == st(A.stun_from_jump_recoil, true) then
        mix(K_DEATH_JUMP + keyframe, K_DEATH_JUMP + keyframe + 1)
    elseif status == st(A.stun_from_jump_death_end, false) or status == st(A.stun_from_jump_death_end, true) then
        set(K_DEATH_JUMP + 15)
    elseif status == st(A.stun_from_stand_right_death_start, false)
        or status == st(A.stun_from_stand_right_recoil, false) then
        mix(K_STUN_RIGHT + stun_right_index(rel), K_DEATH_RIGHT)
    elseif status == st(A.stun_from_stand_right_death_start, true)
        or status == st(A.stun_from_stand_right_recoil, true) then
        local k = keyframe - 94
        local k0 = (k - b(k >= 13)) - b(k >= 15)
        local k1 = (((keyframe - 93) - b(k >= 12)) - b(k >= 14)) - b(k >= 15)
        mix(K_DEATH_RIGHT + k0, K_DEATH_RIGHT + k1)
    elseif status == st(A.stun_from_stand_right_death_end, false)
        or status == st(A.stun_from_stand_right_death_end, true) then
        set(K_DEATH_RIGHT + 13)
    elseif status == st(A.stun_from_stand_left_death_start, false)
        or status == st(A.stun_from_stand_left_recoil, false) then
        mix(K_STUN_LEFT + stun_left_index(rel), K_DEATH_LEFT)
    elseif status == st(A.stun_from_stand_left_death_start, true)
        or status == st(A.stun_from_stand_left_recoil, true) then
        local k = keyframe - 244
        local k0, k1
        if k < 12 then
            k0 = (k - b(k > 0)) - b(k >= 9)
        else
            k0 = asr(k, 1) + (k >= 16 and 3 or 4)
        end
        if k < 12 then
            k1 = k - b(k >= 8)
        else
            k1 = asr(k - 1, 1) + (k >= 15 and 4 or 5)
        end
        mix(K_DEATH_LEFT + k0, K_DEATH_LEFT + k1)
    elseif status == st(A.stun_from_stand_left_death_end, false)
        or status == st(A.stun_from_stand_left_death_end, true) then
        set(K_DEATH_LEFT + 13)
    elseif status == st(A.run_forward, false) then
        set(K_RUN)
    elseif status == st(A.run_forward, true) then
        mix(K_RUN + (keyframe - 363), K_RUN + ((keyframe - 362) & 0x7))
    elseif status == st(A.stand_to_jump_attack, false) then
        set(K_TO_JUMP)
    elseif status == st(A.stand_to_jump_attack, true) then
        local k = keyframe - 346
        local k0 = (((k - b(k >= 3)) - b(k >= 9)) - b(k >= 12)) - b(k >= 13)
        local k1 = ((((keyframe - 345) - b(k >= 2)) - b(k >= 8)) - b(k >= 11)) - b(k >= 12)
        mix(K_TO_JUMP + k0, K_TO_JUMP + k1)
    elseif status == st(A.jump_attack, false) then
        mix(K_JUMP_START, K_JUMP_ATTACK)
    elseif status == st(A.jump_attack, true) then
        mix(K_JUMP_ATTACK + (keyframe - 214), K_JUMP_ATTACK + (keyframe - 213))
        c.box_extent_x = -3276
    end
    sh.entity.shape_offsets_update(c.slot)
end

local function voices(c)
    local dt = sh.game.delta_time()
    if c.groaner_voice_timer > 0 then
        c.groaner_voice_timer = c.groaner_voice_timer - dt
        if c.groaner_voice_timer <= 0 then
            c.groaner_voice_timer = 0
            c.groaner_voice = 0
        end
    elseif c.groaner_voice_timer < 0 then
        c.groaner_voice_timer = c.groaner_voice_timer + dt
        if c.groaner_voice_timer >= 0 then
            sh.entity.sfx_3d(c.slot, sound(c, "attack", 1411), 128, 0)
            c.groaner_voice_timer = 8192
        end
    end

    local done = false
    if c.groaner_voice ~= 4 then
        local request = c.groaner_voice_request
        if request == 4 then
            c.groaner_voice_timer = 4096
            c.groaner_voice = request
            if c.health > 0 and c.groaner_flags_word & (DOWN | DYING) == DOWN then
                sh.entity.sfx_3d(c.slot, sound(c, "damage", 1409), 128, 0)
            else
                sh.entity.sfx_3d(c.slot, sound(c, "death", 1414), 128, 0)
            end
            c.groaner_flags = c.groaner_flags & ~DYING
            done = true
        elseif c.groaner_voice ~= 2 and request == 2 then
            c.groaner_voice_timer = -1228
            c.groaner_voice = request
            done = true
        end
    end
    if not done then
        if c.groaner_voice == 0 and c.groaner_voice_request == 0 and c.health > 0 then
            local clip = c.anim_status >> 1
            if c.anim_status == 33 then
                sh.entity.sfx_3d(c.slot, sound(c, "run", 1410), 64, 0)
                c.groaner_voice_timer = rng_int(2048, 2456)
                c.groaner_voice = 3
            elseif clip ~= A.jump_attack and clip ~= A.stand_to_jump_attack and rng_uint(0, 15) == 0 then
                local health_max = sh.entity.param(c.slot, "health_max", 1146880)
                sh.entity.sfx_3d(c.slot, sound(c, "idle", 1406), asr(idiv(s32(c.health << 12), health_max), 7) + 32, 0)
                c.groaner_voice_timer = rng_int(19660, 32766)
                c.groaner_voice = 1
            end
        end
    end
    c.groaner_voice_request = 0

    local keyframe = asr(c.anim_time, 12)
    local pace = idiv(s32(c.move_speed << 12), run_speed(c))
    local volume = asr(pace, 6) + 64
    local pitch = asr(pace, 7) + rng_uint(0, 7)
    local step = sound(c, "footstep", 1413)
    if (keyframe > 365 and keyframe < 370) or (keyframe > 374 and keyframe < 382) then
        if c.groaner_step_left == 0 then
            sh.entity.sfx_at(c.slot, step, volume, pitch)
            c.groaner_step_left = c.groaner_step_left + 1
        end
    else
        c.groaner_step_left = 0
    end
    if (keyframe > 367 and keyframe < 373) or (keyframe > 390 and keyframe < 397) then
        if c.groaner_step_right == 0 then
            sh.entity.sfx_at(c.slot, step, volume, pitch)
            c.groaner_step_right = c.groaner_step_right + 1
        end
    else
        c.groaner_step_right = 0
    end
end

function update(slot)
    local c = record(slot)
    if c.control_state == 0 then
        start(c)
    end
    local prev = c.control_state
    local moving = sh.game.delta_time() ~= 0
    if moving then
        react_to_damage(c)
        think(c)
        physics(c)
    end
    animate(c)
    if moving then
        collision(c)
        voices(c)
        if c.control_state ~= prev then
            c.groaner_flags = c.groaner_flags & ~STATE_STARTED
        end
        if c.groaner_flags & SEES_PLAYER ~= 0 then
            c.flags = c.flags | 0x4
        end
    end
end
