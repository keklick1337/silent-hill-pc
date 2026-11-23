-- Creeper (the school basement, the hospital, the sewers): a fast insect that waits in a group,
-- chirps to its neighbours, runs at Harry when it sees or hears him and bites at his feet. A hit
-- stuns it; a hit while stunned or running kills it.

local MAP = sh.map_name()

local CREEPER = 11
local BITE = sh.entity.attack_number("CreeperBite")

-- Control states.
local IDLE, WALK, ATTACK, STUN, DAMAGE = 1, 2, 3, 4, 5

-- Flags of creeper_flags.
local HAS_ATTACKED = 0x1
local HIT = 0x2
local FALLING = 0x4
local DEFEATED = 0x8
local LEADER = 0x10
local GOING_HOME = 0x20
local IN_GROUP = 0x40
local ANSWERING = 0x80
local ALERTED = 0x100

-- Clips (index; status = clip * 2 + phase).
local A = clips("Creeper", {
    attack_to_walk_forward = 1, attack = 2, death_start = 3, run_forward_stun_cont = 4, recoil = 5,
    stun_loop = 7, idle_to_run_forward = 8, stun = 9, run_forward = 10, idle = 11, attack_start = 12,
    walk_forward = 13, death_end = 14, run_forward_stun_end = 15, stun_once = 16, run_forward_stun_start = 17,
})
local function st(clip, active)
    return anim_status(clip, active)
end

-- Collision shapes of the keyframes, laid out as the game keeps them (one block; some clips step
-- through a run of entries by keyframe).
local K = {
    [0] = { -819, 0, -450, -737, 409, 368, 40, -122, 40, -81 },      -- attack to walk (4)
    { -860, 0, -491, -819, 450, 368, 40, 0, 40, -40 },
    { -901, 0, -491, -860, 491, 368, 0, 122, 40, 0 },
    { -942, 0, -532, -901, 532, 368, 0, 245, 40, 0 },
    { -819, 0, -450, -737, 409, 368, 40, -122, 40, -81 },            -- 4 attack
    { -409, 0, 0, -409, 614, 368, 40, 122, 40, 81 },                 -- 5 dead
    { -573, 0, -327, -573, 573, 368, 40, 163, 40, 40 },              -- 6 dead after running
    { -942, 0, -532, -901, 532, 368, 40, 40, 40, 81 },               -- 7 stun (10)
    { -1024, 0, -614, -983, 532, 368, 40, 122, 40, 81 },
    { -1474, 0, -1064, -1392, 532, 286, 40, 122, 40, 81 },
    { -1761, 0, -1228, -1638, 532, 204, 40, -40, 81, 0 },
    { -2088, 0, -1351, -1884, 532, 204, 40, -81, 40, 0 },
    { -2088, 0, -1515, -1966, 532, 204, 40, -163, 40, 0 },
    { -1802, 0, -1146, -1720, 532, 204, 40, -204, 40, 81 },
    { -1269, 0, -860, -327, 532, 204, 40, -245, 40, 40 },
    { -860, 0, -368, -696, 532, 286, 40, -122, 40, 81 },
    { -491, 0, -81, -409, 532, 327, 81, -122, 81, 81 },
    { -409, 0, 0, -409, 532, 368, 40, -122, 81, 81 },                -- 17 stunned on the ground
    { -901, 0, -532, -819, 532, 368, 40, 163, 40, 40 },              -- 18 start running (6)
    { -901, 0, -532, -819, 532, 368, 40, 163, 40, 40 },
    { -901, 0, -532, -819, 532, 368, 40, 163, 40, 40 },
    { -860, 0, -532, -819, 532, 368, 40, 163, 40, 40 },
    { -778, 0, -491, -737, 532, 368, 40, 163, 40, 40 },
    { -696, 0, -368, -655, 532, 368, 40, 163, 40, 40 },
    { -696, 0, -368, -655, 532, 368, 40, 163, 40, 40 },              -- 24 running start
    { -409, 0, 0, -368, 532, 368, 40, 40, 40, 81 },                  -- 25 stun
    { -573, 0, -327, -532, 532, 368, 40, 163, 40, 40 },              -- 26 running
    { -942, 0, -532, -901, 532, 368, 40, 245, 40, 0 },               -- 27 idle
    { -819, 0, -450, -737, 409, 368, 40, -122, 40, -81 },            -- 28 attack start
    { -942, 0, -532, -901, 532, 368, 0, 245, 40, 0 },                -- 29 walk
}
local K_ATTACK_TO_WALK, K_ATTACK, K_DEAD, K_DEAD_RUN, K_STUN_RUN = 0, 4, 5, 6, 7
local K_STUNNED, K_START_RUN, K_RUN_START, K_STUN, K_RUN, K_IDLE, K_ATTACK_START, K_WALK = 17, 18, 24, 25, 26, 27, 28, 29

-- Chirp answers outstanding among the group (one counter for the map's creepers).
local answers = 0

local function player()
    return record(0)
end

local function sees_player(c, idle_count, walk)
    local light = map_light()
    if walk then
        return (light & 3 == 2 and sh.entity.player_visible(c.slot, 0, 49152, 32768) ~= 0)
            or (light & 2 ~= 0 and light & 1 ~= 0 and sh.entity.player_visible(c.slot, 2, 3276, 1638) ~= 0)
            or (light & 3 == 0 and sh.entity.player_visible(c.slot, 1, 16384, 49152) ~= 0)
    end
    if light & 3 == 2 then
        return sh.entity.player_visible(c.slot, idle_count, 49152, 32768) ~= 0
    elseif light & 2 ~= 0 and light & 1 ~= 0 then
        return sh.entity.player_visible(c.slot, idle_count, 3276, 1638) ~= 0
    end
    return sh.entity.player_visible(c.slot, idle_count, 16384, 49152) ~= 0
end

local function start(c)
    local others = false
    for i = 1, NPC_SLOTS do
        if i ~= c.slot and sh.entity.get(i, "chara_id") == CREEPER then
            others = true
            break
        end
    end
    if not others then
        answers = 0
    end

    c.health = sh.entity.param(c.slot, "health", 819200)
    c.creeper_flags = 0
    c.anim_alpha = 0
    c.move_speed = 0
    c.heading_angle = c.rot_y
    sh.entity.set_collision_state(c.slot, 2)
    sh.entity.props_clear(c.slot)

    local step = c.state_step
    if step == 3 or step == 12 or step == 13 then
        if step ~= 3 then
            c.creeper_flags = c.creeper_flags | LEADER
            c.creeper_home_x = c.pos_x
            c.creeper_home_z = c.pos_z
        end
        if step == 13 or step == 3 then
            c.creeper_flags = c.creeper_flags | IN_GROUP
        end
        c.control_state = IDLE
        anim_set(c, st(A.idle, true), 94)
    end

    sh.entity.clip_table_attach(c.slot)
    damage_clear(c)
    c.creeper_target_x = c.pos_x
    c.creeper_target_z = c.pos_z

    local difficulty = sh.save.game_difficulty()
    local base, spread
    if difficulty == -1 then
        base, spread = 2867, 819      -- 0.7 + [0, 0.2)
    elseif difficulty == 0 then
        base, spread = 3686, 819      -- 0.9 + [0, 0.2)
    else
        base, spread = 4915, 1638     -- 1.2 + [0, 0.4)
    end
    c.creeper_walk_speed = asr((base + sh.rng.rand16() % spread) * 8192, 12)

    if MAP == "map5_s00" then
        c.flags = c.flags | 0x100
    end
end

local function react_to_damage(c)
    local p = player()
    if c.damage_amount > 0 and c.health > 0 then
        sh.entity.sfx_flags(c.slot, sound(c, "damage", 1425), 128, 0)
        c.creeper_chirp_timer = 0

        c.damage_x = c.damage_x + asr(s32(c.move_speed * sh.math.sin(c.heading_angle)), 15)
        c.damage_z = c.damage_z + asr(s32(c.move_speed * sh.math.cos(c.heading_angle)), 15)
        local dx, dz = c.damage_x, c.damage_z
        local x, z = asr(dx, 6), asr(dz, 6)
        c.move_speed = idiv(s32(sh.math.sqrt(s32(s32(x * x) + s32(z * z))) << 18), 3276)
        c.fall_speed = idiv(s32(c.damage_y << 12), 3276)
        c.heading_angle = s32(sh.math.atan2(dx, dz) + (sh.rng.rand16() % 256) - 128)
        c.creeper_flags = c.creeper_flags | HIT

        local status = c.anim_status
        local clip = status >> 1
        if clip == A.stun_loop then
            c.health = 0
            c.creeper_last_status = st(A.stun_once, true)
            c.anim_status = status == st(A.stun_loop, true) and st(A.stun_once, true) or st(A.stun_once, false)
        elseif clip == A.idle_to_run_forward then
            c.health = 0
            c.creeper_last_status = st(A.run_forward_stun_start, true)
            c.anim_status = status == st(A.idle_to_run_forward, true) and st(A.run_forward_stun_start, true)
                or st(A.run_forward_stun_start, false)
        elseif clip == A.stun then
            c.health = 0
            c.anim_status = st(A.death_start, false)
            c.control_state = DAMAGE
        elseif clip == A.run_forward then
            c.health = 0
            c.anim_status = st(A.run_forward_stun_cont, false)
            c.control_state = DAMAGE
        else
            if c.control_state == ATTACK then
                set_group_flags(3, group_flags(3) & ~2)
            end
            c.control_state = STUN
            c.health = 81920
            c.flags = c.flags | 0x2
            local facing = angle_signed(heading_to(c.pos_x, c.pos_z, p.pos_x, p.pos_z) - c.rot_y)
            if abs(facing) < 682 then
                c.anim_status = st(A.stun_loop, false)
            else
                c.anim_status = st(A.idle_to_run_forward, false)
            end
        end
    end

    if c.damage_amount < 819200 or c.health <= 0 then
        damage_clear(c)
    end
end

local function chirp(c, slot_name, id)
    sh.entity.sfx_flags(c.slot, sound(c, slot_name, id), 128, 0)
end

local function idle(c)
    speed_toward(c, 16384, 0)
    local dt = sh.game.delta_time()
    c.creeper_chirp_timer = c.creeper_chirp_timer + dt

    local noticed = sees_player(c, 2, false)
    local heard = sh.entity.player_noise(c.slot, 0, 2048) ~= 0
    noticed = noticed or heard
    if noticed then
        c.flags = c.flags | 0x4
        if rng_uint(0, 7) == 0 and c.anim_status == st(A.idle, true) then
            c.control_state = WALK
            c.anim_status = st(A.walk_forward, false)
            c.creeper_attack_timer = 2048
            c.creeper_chirp_timer = 0
            return
        end
    end

    local flags = c.creeper_flags
    if flags & ALERTED ~= 0 then
        c.control_state = WALK
        c.anim_status = st(A.walk_forward, false)
        c.creeper_attack_timer = 2048
        c.creeper_chirp_timer = 0
    elseif flags & IN_GROUP ~= 0 then
        if flags & ANSWERING ~= 0 then
            if rng_uint(0, 7) == 0 then
                local t = c.creeper_chirp_timer
                if t > 2048 and t < 10240 then
                    chirp(c, "chirp", 1427)
                    c.creeper_chirp_timer = 0
                    answers = answers - 1
                    c.creeper_flags = c.creeper_flags & ~ANSWERING
                end
            end
        elseif answers == 0 then
            if rng_uint(0, 511) == 0 and c.creeper_chirp_timer > 16384 then
                chirp(c, "chirp", 1427)
                c.creeper_chirp_timer = 0
                -- The neighbours in the group answer the call.
                local spawn = c.spawn_index
                for i = 1, NPC_SLOTS do
                    if sh.entity.get(i, "chara_id") == CREEPER then
                        local n = record(i)
                        if n.spawn_index ~= spawn and n.creeper_flags & IN_GROUP ~= 0 and n.control_state == IDLE
                            and not far_2d(c.pos_x, c.pos_z, n.pos_x, n.pos_z, 65536) then
                            n.creeper_chirp_timer = 0
                            answers = answers + 1
                            n.creeper_flags = n.creeper_flags | ANSWERING
                        end
                    end
                end
            elseif rng_uint(0, 63) == 0 and c.creeper_chirp_timer > 8192 then
                chirp(c, "chirp_2", 1426)
                c.creeper_chirp_timer = rng_int(3276, 7371)
            end
        end
    end
end

local function turn_toward(c, delta, rate)
    if delta > 0 then
        c.rot_y = c.rot_y + q12_mul(sh.game.delta_time(), rate)
    else
        c.rot_y = c.rot_y - q12_mul(sh.game.delta_time(), rate)
    end
end

local function walk(c)
    local p = player()
    local dt = sh.game.delta_time()
    local delta = angle_signed(heading_to(c.pos_x, c.pos_z, p.pos_x, p.pos_z) - c.rot_y)

    if sees_player(c, 0, true) or sh.entity.player_noise(c.slot, 0, 2048) ~= 0
        or c.creeper_flags & ALERTED ~= 0 then
        c.creeper_target_x = p.pos_x
        c.creeper_target_z = p.pos_z
        c.creeper_flags = c.creeper_flags & ~GOING_HOME
    end

    local to_player = distance_2d(p.pos_x - c.pos_x, p.pos_z - c.pos_z)
    local to_target = distance_2d(c.creeper_target_x - c.pos_x, c.creeper_target_z - c.pos_z)

    if to_target < 4915 and c.creeper_flags & HAS_ATTACKED == 0 and sh.entity.player_blocked(c.slot) == 0 then
        if to_player > 4915 then
            if c.creeper_flags_word & (LEADER | GOING_HOME) == LEADER then
                c.creeper_flags = c.creeper_flags | GOING_HOME
                c.creeper_target_x = c.creeper_home_x
                c.creeper_target_z = c.creeper_home_z
            else
                c.control_state = IDLE
                c.anim_status = st(A.idle, false)
                c.creeper_attack_timer = 0
            end
        elseif group_flags(3) & 2 == 0 and p.flags & 0x8 == 0 and to_player < 2048 and abs(delta) < 113
            and p.health > 0 then
            c.control_state = ATTACK
            c.anim_status = st(A.attack_start, false)
            c.creeper_attack_timer = 0
            set_group_flags(3, group_flags(3) | 2)
        else
            if abs(delta) >= 1024 then
                speed_toward(c, 16384, 0)
            else
                speed_toward(c, 6144, asr(c.creeper_walk_speed * (1024 - abs(delta)), 10))
            end
            if turn_step(1, 2) < abs(delta) and rng_int(0, 7) ~= 0 then
                turn_toward(c, delta, 2048)
            end
        end
    else
        speed_toward(c, 6144, c.creeper_walk_speed)
        c.creeper_attack_timer = c.creeper_attack_timer + dt

        local attacked = c.creeper_flags & HAS_ATTACKED ~= 0
        if (abs(delta) > 113 and sh.entity.path_blocked(c.slot, 4096, c.creeper_heading_target) ~= 0)
            or (rng_int(0, 7) == 0 and ((not attacked and c.creeper_attack_timer > 2048)
                or (attacked and c.creeper_attack_timer > 8192))) then
            if c.creeper_attack_timer > 8192 then
                c.creeper_flags = c.creeper_flags & ~HAS_ATTACKED
            end
            c.creeper_heading_target = sh.entity.heading_toward(c.slot, 4915, c.creeper_target_x, c.creeper_target_z,
                4096, true)
            if c.creeper_heading_target == 4096 then
                c.creeper_heading_target = c.rot_y
            else
                c.creeper_attack_timer = 0
            end
        end

        local d = angle_signed(c.creeper_heading_target - c.rot_y)
        if turn_step(1, 2) < abs(d) then
            turn_toward(c, d, 2048)
        end
    end
end

local function attack(c)
    local p = player()
    local dt = sh.game.delta_time()
    if sh.entity.player_blocked(c.slot) ~= 0 then
        set_group_flags(3, group_flags(3) & ~2)
        c.control_state = WALK
        c.anim_status = st(A.walk_forward, false)
        return
    end

    local status = c.anim_status
    local keyframe = asr(c.anim_time, 12)
    if status >> 1 == A.attack_start or status == st(A.attack, false) or (keyframe >= 4 and keyframe <= 7) then
        local to_player = distance_2d(p.pos_x - c.pos_x, p.pos_z - c.pos_z)
        if to_player < 1638 then
            speed_toward(c, 65536, 0)
        else
            local half = c.creeper_walk_speed // 2
            local speed = c.move_speed
            local step = q12_mul(dt, 24576)
            if half < speed then
                c.move_speed = max(half, speed - step)
            else
                local v = speed + step
                c.move_speed = half >= v and v or half
            end
        end

        local heading = heading_to(c.pos_x, c.pos_z, p.pos_x, p.pos_z)
        local d = angle_signed(heading - c.rot_y)
        if turn_step(1, 2) < abs(d) then
            turn_toward(c, d, 2048)
        else
            c.rot_y = heading
        end
        c.attack_window = 1
        return
    end

    speed_toward(c, 8192, 0)
    if keyframe >= 9 and keyframe <= 10 then
        local y = c.pos_y + c.box_extent_x
        local pitch = sh.math.atan2(1638, s32((p.pos_y + p.box_bottom) - (c.pos_y + c.box_bottom)))
        sh.entity.attack(c.slot, 1, BITE, c.pos_x, y, c.pos_z, 0, c.rot_y, pitch)
        if c.creeper_flags & HAS_ATTACKED == 0 then
            sh.entity.sfx_flags(c.slot, sound(c, "attack", 1424), 128, 0)
        end
        c.creeper_flags = c.creeper_flags | HAS_ATTACKED

        -- Every character near the bite hears it.
        for i = 1, NPC_SLOTS do
            local n = record(i)
            if not far_2d(c.pos_x, c.pos_z, n.pos_x, n.pos_z, 65536) then
                n.creeper_flags = n.creeper_flags | ALERTED
            end
        end
    elseif status >> 1 == A.walk_forward then
        set_group_flags(3, group_flags(3) & ~2)
        c.control_state = WALK
        c.creeper_attack_timer = 0
        c.creeper_heading_target = sh.entity.heading_toward(c.slot, 19660, p.pos_x, p.pos_z, 4096, false)
        c.attack_window = 0
    end
end

local function stun(c)
    local dt = sh.game.delta_time()
    if c.anim_status == st(A.stun_loop, true) then
        local away = c.heading_angle + 2048
        if c.rot_y ~= angle_signed(away) then
            if angle_signed(away - c.rot_y) > 0 then
                c.rot_y = c.rot_y + q12_mul(dt, 341)
            else
                c.rot_y = c.rot_y - q12_mul(dt, 341)
            end
        end

        local keyframe = asr(c.anim_time, 12)
        local dist
        local step = q12_mul(sh.entity.clip_duration(c.slot, c.anim_status), dt)
        if keyframe >= 50 and keyframe <= 59 then
            dist = idiv(step, 9)
        else
            dist = idiv(s32(step * 409), 16384)
        end
        c.creeper_push_x = q12_mul32(dist, sh.math.sin(c.rot_y + 2048))
        c.creeper_push_z = q12_mul32(dist, sh.math.cos(c.rot_y + 2048))
    end

    local clip = c.anim_status >> 1
    if clip == A.death_start or clip == A.run_forward_stun_cont or clip == A.death_end
        or clip == A.run_forward_stun_end then
        c.control_state = DAMAGE
    end
end

local function damaged(c)
    c.flags = c.flags & ~0x2
    if c.health == 0 and npc_index(c.slot) ~= sh.entity.sys("target_npc") then
        c.health = -1
        sh.entity.set_collision_state(c.slot, 0)
    end
    if c.move_speed == 0 and c.creeper_flags_word & (FALLING | DEFEATED) == 0 then
        c.creeper_flags = c.creeper_flags | DEFEATED
        sh.entity.enemy_defeated(c.slot)
        if c.anim_status == st(A.death_end, true) then
            sh.entity.anim_status_set(c.slot, st(A.recoil, false), 5)
        else
            sh.entity.anim_status_set(c.slot, st(A.recoil, true), 5)
        end
    end
end

local CONTROL = { [IDLE] = idle, [WALK] = walk, [ATTACK] = attack, [STUN] = stun, [DAMAGE] = damaged }

local function physics(c)
    c.fall_speed = c.fall_speed + asr(sh.game.gravity_speed(), 2)
    if c.creeper_flags & HIT ~= 0 then
        speed_toward(c, 16384, 0)
    else
        c.heading_angle = c.rot_y
    end
    sh.entity.move(c.slot, c.creeper_push_x, c.creeper_push_z)
    c.creeper_push_z = 0
    c.creeper_push_x = 0
    if c.fall_speed ~= 0 then
        c.creeper_flags = c.creeper_flags | FALLING
    else
        c.creeper_flags = c.creeper_flags & ~FALLING
    end
    c.rot_y = angle_signed(c.rot_y)
end

local function collision(c)
    local status = c.anim_status
    local keyframe = asr(c.anim_time, 12)
    if status == st(A.attack_to_walk_forward, false) then
        collision_between(c, K[K_ATTACK], K[K_ATTACK_TO_WALK])
    elseif status == st(A.attack_to_walk_forward, true) then
        c.creeper_last_status = st(A.attack_to_walk_forward, true)
        collision_between(c, K[K_ATTACK_TO_WALK + keyframe], K[K_ATTACK_TO_WALK + keyframe + 1])
    elseif status == st(A.attack, false) then
        collision_between(c, K[K_ATTACK_START], K[K_ATTACK])
    elseif status == st(A.attack, true) then
        collision_from_keyframe(c, K[K_ATTACK])
        c.box_extent_x = -655
    elseif status == st(A.death_start, false) then
        if c.creeper_last_status == st(A.stun_once, true) then
            collision_between(c, K[K_STUNNED], K[K_DEAD])
        else
            collision_between(c, K[K_STUN], K[K_DEAD])
        end
    elseif status == st(A.death_start, true) or status == st(A.death_end, false) or status == st(A.death_end, true) then
        collision_from_keyframe(c, K[K_DEAD])
    elseif status == st(A.run_forward_stun_cont, false) then
        if c.creeper_last_status == st(A.run_forward_stun_start, true) then
            collision_between(c, K[K_RUN_START], K[K_DEAD_RUN])
        else
            collision_between(c, K[K_RUN], K[K_DEAD_RUN])
        end
    elseif status == st(A.run_forward_stun_cont, true) or status == st(A.run_forward_stun_end, false)
        or status == st(A.run_forward_stun_end, true) then
        collision_from_keyframe(c, K[K_DEAD_RUN])
    elseif status == st(A.stun_loop, false) or status == st(A.stun_once, false) then
        collision_from_keyframe(c, K[K_STUN_RUN])
    elseif status == st(A.stun_loop, true) or status == st(A.stun_once, true) then
        local k = keyframe - 50
        local b = function(v) return v and 1 or 0 end
        local k3 = k - b(k >= 10) - b(k >= 11)
        local k0 = k3 - b(k >= 12)
        local k1 = (k + 1) - b(k >= 9) - b(k >= 10) - b(k >= 11)
        collision_between(c, K[K_STUN_RUN + k0], K[K_STUN_RUN + k1])
    elseif status == st(A.idle_to_run_forward, false) or status == st(A.run_forward_stun_start, false) then
        collision_from_keyframe(c, K[K_START_RUN])
    elseif status == st(A.idle_to_run_forward, true) or status == st(A.run_forward_stun_start, true) then
        local k = keyframe - 64
        collision_between(c, K[K_START_RUN + k], K[K_START_RUN + k + 1])
    elseif status == st(A.stun, false) then
        collision_between(c, K[K_STUNNED], K[K_STUN])
    elseif status == st(A.stun, true) then
        collision_from_keyframe(c, K[K_STUN])
    elseif status == st(A.run_forward, false) then
        collision_between(c, K[K_RUN_START], K[K_RUN])
    elseif status == st(A.run_forward, true) then
        collision_from_keyframe(c, K[K_RUN])
    elseif status == st(A.idle, false) then
        collision_between(c, K[K_WALK], K[K_IDLE])
    elseif status == st(A.idle, true) then
        c.creeper_last_status = st(A.idle, true)
        collision_from_keyframe(c, K[K_IDLE])
    elseif status == st(A.attack_start, false) then
        collision_between(c, K[K_WALK], K[K_ATTACK_START])
    elseif status == st(A.attack_start, true) then
        collision_from_keyframe(c, K[K_ATTACK_START])
    elseif status == st(A.walk_forward, false) then
        if c.creeper_last_status == st(A.idle, true) then
            collision_between(c, K[K_IDLE], K[K_WALK])
        elseif c.creeper_last_status == st(A.attack_to_walk_forward, true) then
            collision_between(c, K[K_ATTACK_TO_WALK], K[K_WALK])
        else
            collision_from_keyframe(c, K[K_WALK])
        end
    elseif status == st(A.walk_forward, true) then
        collision_from_keyframe(c, K[K_WALK])
    end
    sh.entity.shape_offsets_update(c.slot)
end

function update(slot)
    local c = record(slot)
    if c.control_state == 0 then
        start(c)
    end
    if sh.game.delta_time() ~= 0 then
        react_to_damage(c)
        local control = CONTROL[c.control_state]
        if control then
            control(c)
        end
        c.creeper_flags = c.creeper_flags & ~ALERTED
        physics(c)
    end
    anim_play(c)
    collision(c)
end
