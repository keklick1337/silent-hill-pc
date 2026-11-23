-- Split Head, the boss of the school (map1_s05; map1_s06 after the fight). It waits until the
-- opening cutscene sets flag M1S05_SplitHeadIntroDone, then circles Harry (control 2), charges him (3), closes in for a
-- bite (5, 1) and swallows him whole when the bite lands. A hit inside the open mouth hurts it 16
-- times more (32 with the shotgun's tap); under 24000 health it stops to roar (4), at 0 it dies (6).
-- The collision is built from bone points every tick: a box over the head, nine points around the
-- body that push Harry out.

local A = clips("SplitHead", {
    roar = 1, bite_attack = 2, lunge = 3, bite_approach = 4, die = 7, splayed = 8, flinch_back = 9,
    stand_idle = 10, walk_forward = 11, lying = 12, splayed_rise = 13, swallow_hold = 14,
})

local function S(clip, active)
    return anim_status(clip, active)
end

local WALK = S(A.walk_forward, true)
local WALK_IN = S(A.walk_forward, false)
local IDLE = S(A.stand_idle, true)
local IDLE_IN = S(A.stand_idle, false)
local ROAR = S(A.roar, true)
local ROAR_IN = S(A.roar, false)
local BITE = S(A.bite_attack, true)

-- split_head_flags bits.
local F_MOUTH_OPEN = 0x1    -- Harry is in front of the open jaws: the head box is the jaws
local F_SWALLOWED = 0x2     -- the bite landed, Harry is inside
local F_STEER_SIDE = 0x4    -- the side the next free-ray search starts on
local F_WALK_STEP = 0x8     -- this tick's control wants the walk cycle to run at full speed
local F_ROARED = 0x10       -- the 24000 health roar happened
local F_FORWARD = 0x20      -- the last walk went forward
local F_HURT = 0x40         -- hit in the mouth: flinch at the next idle
local F_VOCAL_2 = 0x80      -- ask for the short vocal
local F_VOCAL = 0x100       -- ask for the long vocal
local F_BITTEN = 0x200      -- the bite already caught Harry

local FLAG_FIGHT_STARTED = 130
local FLAG_DOWN = 131

-- Map data (chara/fallbacks.lua): on its own map the fight starts with the opening cutscene (story flag M1S05_SplitHeadIntroDone)
-- and the body going down is recorded in flag M1S05_SplitHeadKilled. A spawn of the engine fights at once and keeps its own
-- "down" per slot: the story flags of another map are not its own.
local downed = {}
fallbacks = {
    fight_started = true,
    body_down = function(c) return downed[c.slot] == true end,
}
local WEAPON_SHOTGUN_TAP = 34
local ATTACK_SWALLOW = sh.entity.attack_number("SplitHeadSwallow")
local DIFFICULTY_EASY, DIFFICULTY_NORMAL, DIFFICULTY_HARD = -1, 0, 1
local TICK_30HZ = 136

-- Body points (bone-local, geometry units): their bones are picked per point in body_points().
local BODY = {
    { 31, 30, 93 }, { -169, 83, 327 }, { -36, 5, 113 }, { 69, 65, 69 }, { 209, 190, -114 },
}

-- Blood drop sources: bone and bone-local point. The first 22 are the jaws, the rest the body.
local BLOOD = {
    { 19, -23, 51, -39 }, { 19, -13, 1, -7 }, { 19, -25, 83, 0 }, { 19, -30, 105, 9 }, { 19, -21, 33, 13 },
    { 19, -27, 70, 21 }, { 19, -31, 4, 85 }, { 19, -23, 3, 45 }, { 19, -25, -8, 46 }, { 19, -33, -53, 45 },
    { 19, -27, -52, 18 }, { 23, 31, 87, -31 }, { 23, 28, 66, -45 }, { 23, 23, 76, -2 }, { 23, 23, 53, -42 },
    { 23, 30, 51, 49 }, { 23, 24, 47, 19 }, { 23, 17, 27, -3 }, { 23, 13, 2, -6 }, { 23, 20, 3, 31 },
    { 23, 32, 4, 90 }, { 23, 32, -48, 45 }, { 18, -26, 108, 160 }, { 18, -33, 127, 149 }, { 18, -37, 108, 78 },
    { 18, -32, 68, 27 }, { 18, -34, 23, 210 }, { 18, -36, -16, 146 }, { 18, -40, -47, 119 }, { 22, 31, 115, 141 },
    { 22, 38, 126, 117 }, { 22, 38, 97, 42 }, { 22, 24, 48, 58 }, { 22, 37, 2, 194 }, { 22, 36, -33, 107 },
    { 22, 42, -68, 83 }, { 17, -35, 25, 29 }, { 21, 35, 25, 29 },
}

-- Blood strands while the jaws are open: two BLOOD entries (0-based) and the strand kind.
local STRANDS = { { 2, 13, 2 }, { 10, 21, 1 }, { 24, 31, 0 }, { 26, 33, 1 }, { 36, 37, 0 }, { 1, 18, 2 } }

-- Statuses that show the open-jaw head mesh.
local JAWS_OPEN = {
    [ROAR_IN] = true, [ROAR] = true, [S(A.bite_attack, false)] = true, [BITE] = true,
    [S(A.bite_approach, false)] = true, [S(A.die, false)] = true, [S(A.splayed, false)] = true,
    [S(A.splayed, true)] = true, [S(A.splayed_rise, false)] = true, [S(A.splayed_rise, true)] = true,
    [S(A.swallow_hold, false)] = true, [S(A.swallow_hold, true)] = true,
}

local LYING = {
    [S(A.splayed, false)] = true, [S(A.splayed, true)] = true, [S(A.splayed_rise, false)] = true,
    [S(A.splayed_rise, true)] = true,
}

local STRANDS_SHOWN = {
    [S(A.splayed, false)] = true, [S(A.splayed, true)] = true, [S(A.splayed_rise, false)] = true,
    [S(A.splayed_rise, true)] = true, [ROAR] = true, [BITE] = true, [S(A.swallow_hold, false)] = true,
    [S(A.swallow_hold, true)] = true,
}

local DYING = {
    [S(A.die, false)] = true, [S(A.die, true)] = true, [S(A.splayed, false)] = true,
    [S(A.splayed, true)] = true, [S(A.lying, false)] = true, [S(A.lying, true)] = true,
    [S(A.splayed_rise, false)] = true, [S(A.splayed_rise, true)] = true,
}

local COLLAPSE = { [ROAR_IN] = true, [ROAR] = true, [IDLE_IN] = true, [IDLE] = true, [WALK_IN] = true }

-- Module state, shared by every Split Head of the map.
local walk_rate = 0         -- duration of the walk clips, set from the move speed (Q12)
local roar_length = 26214   -- s16
local idle_timer = 0        -- s16
local edge_first = 0        -- first body point of the outline (2 while the jaws are closed)
local grab_blocked = 0
local points = {}
local edge_kind, edge_side = {}, {}
for i = 0, 8 do
    points[i] = { x = 0, y = 0, z = 0 }
    edge_kind[i] = 0
    edge_side[i] = 0
end
local push_x, push_z = 0, 0
-- Effects that are per tick of the game, not per frame.
local swallow_tick, drop_tick = 0, 0

local player = record(0)

local function tick_30hz(acc)
    acc = acc + sh.game.delta_time()
    if acc >= TICK_30HZ then
        acc = acc - TICK_30HZ
        if acc > TICK_30HZ then
            acc = 0
        end
        return acc, true
    end
    return acc, false
end

-- A bone-local point (geometry units) in the world, Q12.
local function bone_point(c, bone, x, y, z)
    local wx, wy, wz = sh.entity.bone_point(c.slot, bone, x, y, z)
    return s32(wx * 16), s32(wy * 16), s32(wz * 16)
end

local function bone_vec(c, bone, x, y, z)
    local wx, wy, wz = bone_point(c, bone, x, y, z)
    return { x = wx, y = wy, z = wz }
end

local function has(c, bit)
    return c.split_head_flags & bit ~= 0
end

local function set(c, bit)
    c.split_head_flags = c.split_head_flags | bit
end

local function clear(c, bit)
    c.split_head_flags = c.split_head_flags & ~bit
end

local function speed_to(c, rate, limit)
    speed_toward(c, rate, limit or 0)
end

local function turn_limit(denom, shift)
    return asr(idiv(sh.game.delta_time(), denom), shift) + 1
end

local function heading_to_player(c)
    return sh.math.atan2(player.pos_x - c.pos_x, player.pos_z - c.pos_z)
end

local function player_distance(c)
    return distance_2d(player.pos_x - c.pos_x, player.pos_z - c.pos_z)
end

-- Turns by `rate` per second towards the side of `delta`, or snaps to `target` once close.
local function turn(c, delta, limit, rate, target)
    if limit < math.abs(delta) then
        if delta > 0 then
            c.rot_y = c.rot_y + q12_mul(sh.game.delta_time(), rate)
        else
            c.rot_y = c.rot_y - q12_mul(sh.game.delta_time(), rate)
        end
    else
        c.rot_y = target()
    end
end

-- The walk cycle wrapped since the previous tick (one stride done).
local function stride_done(c)
    local t = c.split_head_walk_time
    local now = c.anim_time
    return ((t < 0 or t > 198 * 4096) and now < 175 * 4096) or ((t >= 0 and t < 175 * 4096) and now > 198 * 4096)
end

local function ray_free(c, dx, dz)
    local hit, chara = sh.entity.ray_hit_chara(c.slot, c.pos_x, c.pos_y, c.pos_z, dx, 0, dz)
    return hit == 0 or chara ~= 0
end

local function vocal_sound(c, name, fallback)
    sh.entity.sfx_flags(c.slot, sound(c, name, fallback), 128, 0)
end

local function init(c)
    c.health = 25500
    c.split_head_flags = 0
    downed[c.slot] = nil
    c.anim_alpha = 0
    c.move_speed = 0
    c.heading_angle = c.rot_y
    sh.entity.set_collision_state(c.slot, 4)
    sh.entity.props_clear(c.slot)
    c.control_state = 8
    c.anim_status = IDLE
    c.anim_time = 162 * 4096
    c.anim_keyframe = 162
    sh.entity.clip_table_attach(c.slot, 0)
    sh.entity.split_head_move_offset(c.slot)
    damage_clear(c)
    c.split_head_blood_alpha = 196
    c.flags = c.flags | 0x104
end

local function damage_take(c)
    local dead = sh.entity.player_damage_disable(c.health == 0 and 1 or 0)
    if c.damage_amount == 0 or c.health == 0 then
        return
    end
    local damage = asr(c.damage_amount, 12)
    local difficulty = sh.save.game_difficulty()
    if difficulty == DIFFICULTY_HARD then
        damage = asr(damage, 1)
    end
    if damage == 0 then
        damage = 1
    end
    if has(c, F_MOUTH_OPEN) then
        if difficulty ~= DIFFICULTY_HARD and sh.player.weapon_attack() == WEAPON_SHOTGUN_TAP then
            damage = s32(damage << 5)
        else
            damage = s32(damage << 4)
        end
    end
    local health = s32(c.health - damage)
    if dead ~= 0 then
        if health <= 0 then
            health = 1
        end
    elseif health < 0 then
        health = 0
    end
    c.health = health
    -- A hit that crosses 24000 and 0 at once kills: the roar would keep it fighting at 0 health.
    if c.health < 24000 and c.health ~= 0 and not has(c, F_ROARED) then
        c.control_state = 4
        set(c, F_ROARED)
    elseif c.health == 0 then
        if c.anim_status >> 1 == A.bite_attack then
            c.anim_status = S(A.splayed, false)
        elseif c.move_speed > 0 then
            set(c, F_FORWARD)
        elseif c.move_speed < 0 then
            clear(c, F_FORWARD)
        end
        c.control_state = 6
    elseif has(c, F_MOUTH_OPEN) then
        set(c, F_HURT | F_VOCAL_2)
    end
    damage_clear(c)
    sh.entity.player_damage_disable(c.health == 0 and 1 or 0)
end

-- Control 1: close in and bite; inside, chew.
local function control_bite(c)
    local dist = player_distance(c)
    local delta = angle_signed(heading_to_player(c) - c.rot_y)
    local status = c.anim_status
    local clip = status >> 1
    local target = function() return heading_to_player(c) end
    if status == WALK then
        if dist > 14336 then
            speed_to(c, 9830, 9830)
        elseif math.abs(delta) > 1024 or dist > 20480 then
            c.control_state = 5
        else
            set(c, F_WALK_STEP)
            speed_to(c, 19660)
            if stride_done(c) then
                c.anim_status = S(A.bite_attack, false)
                vocal_sound(c, "bite_attack", 1475)
            end
        end
        turn(c, delta, turn_limit(1, 4), 512, target)
    elseif clip == A.bite_approach then
        speed_to(c, 19660)
    elseif clip == A.stand_idle then
        speed_to(c, 19660)
        if has(c, F_HURT) then
            clear(c, F_HURT)
            if sh.rng.rand16() & 3 == 0 then
                c.control_state = 7
                c.anim_status = S(A.flinch_back, false)
            else
                c.anim_status = S(A.bite_approach, false)
            end
        elseif c.anim_status == IDLE and sh.rng.uint(0, 31) == 0 then
            c.anim_status = WALK_IN
            c.control_state = 5
        end
    elseif has(c, F_SWALLOWED) then
        if c.anim_status == BITE then
            c.anim_status = S(A.swallow_hold, false)
        end
        speed_to(c, 4915, 0)
        turn(c, delta, turn_limit(3, 4), 170, target)
        local ready
        swallow_tick, ready = tick_30hz(swallow_tick)
        if ready and asr(c.anim_time, 12) == 35 then
            local x = player.pos_x + sh.rng.uint(-1024, -1)
            local y = (player.pos_y - sh.rng.uint(0, 2047)) - 4096
            local z = player.pos_z + sh.rng.uint(-1024, -1)
            sh.entity.effect_spawn(0, x, y, z, 2, 7)
        end
    else
        if dist < 11468 then
            speed_to(c, 4915)
        else
            speed_to(c, 4915, 2457)
        end
        turn(c, delta, turn_limit(3, 4), 170, target)
    end
end

-- Looks for a free heading near Harry: rays fanned out from him, alternating sides, `near` (Q12)
-- long or out to Harry; `side_reach` gives the sideways part of the far rays from their sine.
local function steer_search(c, dist, near, side_reach)
    local angle = 0
    local to_player = heading_to_player(c)
    local mult
    if has(c, F_STEER_SIDE) then
        mult = 1
        clear(c, F_STEER_SIDE)
    else
        mult = -1
        set(c, F_STEER_SIDE)
    end
    local i = 0
    while i < 16 do
        local jitter
        if i ~= 0 and i ~= 15 then
            local r = sh.rng.uint(-64, 63)
            if i & 1 ~= 0 then
                jitter = s16(r * mult)
            else
                jitter = s16(r * -mult)
            end
        else
            jitter = 0
        end
        local reach
        if angle < 1024 then
            reach = near
        else
            reach = asr(s32(s32(dist * sh.math.cos(angle)) + side_reach(sh.math.sin(angle))), 12)
        end
        if dist >= reach then
            reach = dist
        end
        local heading = to_player + angle + jitter
        local dx = q12_mul32(reach, sh.math.sin(heading))
        local dz = q12_mul32(reach, sh.math.cos(heading))
        if ray_free(c, dx, dz) then
            c.split_head_steer_angle = jitter + (to_player + angle)
            break
        end
        if i & 1 == 0 then
            angle = angle + 256
        end
        i = i + 1
    end
    if i ~= 16 then
        c.split_head_steer_timer = 0
    end
end

local function steer_turn(c, limit, rate)
    local delta = angle_signed(c.split_head_steer_angle - c.rot_y)
    turn(c, delta, limit, rate, function() return c.split_head_steer_angle end)
end

local function player_in_sight(c, dist)
    local heading = heading_to_player(c)
    return ray_free(c, q12_mul32(dist, sh.math.sin(heading)), q12_mul32(dist, sh.math.cos(heading)))
end

local function random_vocals(c)
    if sh.rng.uint(0, 127) == 0 then
        set(c, F_VOCAL)
    end
    if sh.rng.uint(0, 63) == 0 then
        set(c, F_VOCAL_2)
    end
end

-- Control 2: circle Harry at a distance, switch to the charge when he is in line.
local function control_circle(c)
    local dist = player_distance(c)
    if c.split_head_steer_timer > 2457 then
        steer_search(c, dist, 7372, function(sin) return s32(sin * 7372) end)
    end
    c.split_head_steer_timer = c.split_head_steer_timer + sh.game.delta_time()
    steer_turn(c, turn_limit(3, 3), 341)

    local off = math.abs(angle_signed(c.split_head_steer_angle - c.rot_y))
    local aim = math.abs(angle_signed(heading_to_player(c) - c.rot_y))
    if math.abs(off - aim) < 170 then
        if dist <= 23552 and off < 682 then
            speed_to(c, 3276, -4915)
        elseif (dist <= 23552 and off > 682) or (dist > 23552 and off > 1024) then
            speed_to(c, 3276)
        else
            speed_to(c, 3276, 4915)
        end
    elseif off < 341 then
        speed_to(c, 3276, 4915)
    else
        speed_to(c, 3276)
    end

    local clear_line = player_in_sight(c, dist)
    if dist < 26214 and (aim < 85 or (dist > 13107 and aim < 170)) and clear_line then
        c.control_state = 3
    end
    random_vocals(c)
end

-- Control 3: charge.
local function control_charge(c)
    local dist = player_distance(c)
    local delta = angle_signed(heading_to_player(c) - c.rot_y)
    local status = c.anim_status
    local target = function() return heading_to_player(c) end
    if status == WALK then
        if dist > 16384 then
            speed_to(c, 16384, 9830)
        elseif math.abs(delta) > 1024 or dist > 32768 or (dist < 13107 and math.abs(delta) > 256) then
            c.control_state = 2
        else
            set(c, F_WALK_STEP)
            speed_to(c, 65536)
            if stride_done(c) then
                c.anim_status = S(A.lunge, false)
                vocal_sound(c, "attack", 1471)
            end
        end
        turn(c, delta, turn_limit(3, 3), 341, target)
    elseif status >> 1 == A.stand_idle then
        speed_to(c, 6552)
        idle_timer = s16(idle_timer + sh.game.delta_time())
        if c.anim_status == IDLE and (sh.rng.uint(0, 63) == 0 or idle_timer > 16384) then
            idle_timer = 0
            c.anim_status = WALK_IN
            c.control_state = 2
        end
    else
        if dist < 26214 then
            speed_to(c, 16384)
        else
            speed_to(c, 16384, 9830)
        end
        turn(c, delta, turn_limit(3, 4), 170, target)
    end
end

-- Control 4: stop and roar, the breath swelling and fading over roar_length.
local function control_roar(c)
    speed_to(c, 6552)
    local status = c.anim_status
    local start = false
    if status == WALK_IN or status == WALK then
        set(c, F_WALK_STEP)
        start = stride_done(c)
    elseif status == IDLE then
        start = true
    elseif status == ROAR_IN or status == ROAR then
        local breath = sound(c, "breath_loop", 1477)
        local t = c.split_head_roar_timer
        local length = roar_length
        local fade = s16(idiv(length, 6))
        local vol
        if fade == 0 then
            vol = 128
        elseif t < fade then
            vol = idiv(t << 7, fade)
        elseif idiv(length * 5, 6) < t then
            vol = idiv((roar_length - t) << 7, fade)
        else
            vol = 128
        end
        sh.sfx_falloff_pitch_play(breath, c.pos_x, c.pos_y, c.pos_z, vol, 65536, 0)
        if c.split_head_roar_timer > roar_length then
            roar_length = 15564
            c.control_state = 5
            c.split_head_roar_timer = 0
            c.anim_status = WALK_IN
            sh.sfx_stop(breath)
            return
        end
        c.split_head_roar_timer = c.split_head_roar_timer + sh.game.delta_time()
    end
    if start then
        c.anim_status = ROAR_IN
        sh.sd_call(sound(c, "breath_loop", 1477))
    end
end

-- Control 5: approach on a free heading until in biting range.
local function control_approach(c)
    local dist = player_distance(c)
    if c.split_head_steer_timer > 1638 then
        steer_search(c, dist, 8192, function(sin) return s32(sin << 13) end)
    end
    c.split_head_steer_timer = c.split_head_steer_timer + sh.game.delta_time()
    steer_turn(c, turn_limit(1, 4), 512)

    local off = math.abs(angle_signed(c.split_head_steer_angle - c.rot_y))
    local aim = math.abs(angle_signed(heading_to_player(c) - c.rot_y))
    if math.abs(off - aim) < 170 then
        if dist <= 19660 and aim > 42 and off < 682 then
            speed_to(c, 9830, -9830)
        elseif (dist <= 19660 and off > 682) or (dist > 19660 and off > 1024) then
            speed_to(c, 9830)
        else
            speed_to(c, 9830, 9830)
        end
    elseif off < 341 then
        speed_to(c, 9830, 9830)
    else
        speed_to(c, 9830)
    end

    local clear_line = player_in_sight(c, dist)
    if dist > 11673 and dist < 16384 and aim < 170 and clear_line then
        c.control_state = 1
        if sh.rng.uint(0, 7) == 0 then
            set(c, F_VOCAL)
        end
    end
    random_vocals(c)
end

-- Control 6: dead.
local function control_death(c)
    local status = c.anim_status
    local collapse = COLLAPSE[status]
    if status == WALK then
        speed_to(c, 2048)
        collapse = stride_done(c)
    end
    if collapse then
        c.anim_status = S(A.die, false)
        speed_to(c, 4096)
    elseif DYING[status] then
        speed_to(c, 4096)
        if not map_data(c, "body_down", sh.flag_get, FLAG_DOWN) then
            vocal_sound(c, "move", 1479)
        end
        if map_provides(c) then
            sh.flag_set(FLAG_DOWN, 1)
        else
            downed[c.slot] = true
        end
    end
end

-- Control 7: back off after a hit in the mouth.
local function control_flinch(c)
    if c.anim_status >> 1 == A.flinch_back then
        speed_to(c, 3276, -4915)
        local delta = angle_signed(heading_to_player(c) - c.rot_y)
        turn(c, delta, turn_limit(3, 3), 341, function() return heading_to_player(c) end)
    else
        c.control_state = 4
    end
end

-- Control 8: wait for the opening cutscene.
local function control_wait(c)
    if not map_data(c, "fight_started", sh.flag_get, FLAG_FIGHT_STARTED) then
        return
    end
    c.control_state = 2
    c.anim_status = WALK_IN
    set(c, F_VOCAL)
end

local CONTROL = {
    control_bite, control_circle, control_charge, control_roar, control_approach, control_death, control_flinch,
    control_wait,
}

local function control_update(c)
    clear(c, F_WALK_STEP)
    CONTROL[c.control_state](c)
    if c.anim_status == WALK then
        c.split_head_walk_time = c.anim_time
    elseif c.anim_status == WALK_IN then
        c.split_head_walk_time = 201 * 4096
    end
end

local function movement_update(c)
    c.heading_angle = c.rot_y
    local x, y, z = bone_point(c, 2, 0, -57, 3)
    c.cylinder_offset_x = x - c.pos_x
    c.box_top = y
    c.cylinder_offset_z = z - c.pos_z
    local clip = c.anim_status >> 1
    if clip == A.bite_attack or clip == A.swallow_hold then
        c.cylinder_radius = 5324
    else
        c.cylinder_radius = 6963
    end
    sh.entity.set_collision_state(c.slot, 4)
    local dx, dz = sh.entity.split_head_move_offset(c.slot)
    sh.entity.move(c.slot, dx, dz)
end

local function walk_rate_update(c)
    local speed = c.move_speed
    local state = c.control_state
    if state == 2 or state == 7 or state == 5 then
        local floor = state == 5 and 40960 or 32768
        walk_rate = q12_mul(speed, 38092)
        if c.rot_y ~= c.split_head_prev_rot_y then
            if speed >= 0 then
                walk_rate = math.max(walk_rate, floor)
            else
                walk_rate = math.min(walk_rate, -floor)
            end
        end
    elseif state == 1 or state == 3 then
        if not has(c, F_WALK_STEP) then
            walk_rate = math.max(q12_mul(speed, 38092), 49152)
            if speed > 0 then
                set(c, F_FORWARD)
            elseif speed < 0 then
                clear(c, F_FORWARD)
            end
        elseif has(c, F_FORWARD) then
            walk_rate = 98304
        else
            walk_rate = -98304
        end
    elseif state == 4 then
        if not has(c, F_WALK_STEP) then
            walk_rate = math.max(q12_mul(speed, 38092), 49152)
        else
            local keyframe = asr(c.anim_time, 12)
            walk_rate = (keyframe > 187 and keyframe < 203) and 98304 or -98304
        end
    elseif state == 6 then
        walk_rate = q12_mul(speed, 38092)
        if has(c, F_FORWARD) then
            walk_rate = math.max(walk_rate, 49152)
        else
            walk_rate = math.min(walk_rate, -49152)
        end
    end
end

local function anim_update(c)
    c.rot_y = angle_signed(c.rot_y)
    local status = c.anim_status
    sh.held_item_attach(c.chara_id, JAWS_OPEN[status] and 2 or 1)

    if status == S(A.flinch_back, true) or status == WALK then
        walk_rate_update(c)
        sh.entity.clip_duration_set(c.slot, status, walk_rate)
    elseif status == BITE then
        local difficulty = sh.save.game_difficulty()
        local duration
        if difficulty == DIFFICULTY_NORMAL then
            duration = 49152
        elseif difficulty == DIFFICULTY_EASY then
            duration = 32768
        else
            duration = 65536
        end
        sh.entity.clip_duration_set(c.slot, BITE, duration)
    end

    sh.entity.root_transform(c.slot)
    sh.entity.anim_step(c.slot)

    local step = q12_mul(sh.game.delta_time(), 160)
    local twist = c.split_head_twist
    local prev = c.split_head_prev_rot_y
    local rot = c.rot_y
    if c.anim_status == WALK and rot ~= prev then
        if (prev < rot and c.move_speed >= 0) or (rot < prev and c.move_speed < 0) then
            twist = math.max(twist - step, -192)
        else
            twist = math.min(twist + step, 192)
        end
    elseif twist > 0 then
        twist = math.max(twist - step, 0)
    else
        twist = math.min(twist + step, 0)
    end
    c.split_head_twist = twist
    twist = c.split_head_twist
    if twist ~= 0 then
        sh.entity.bone_rotate(c.slot, 1, 0, twist, 0)
        for bone = 18, 24 do
            sh.entity.bone_rotate(c.slot, bone, 0, asr(twist, 3), 0)
        end
    end
    c.split_head_prev_rot_y = c.rot_y
end

-- Distance of a point to a triangle side (Math_TriangleCrossCheck): the distance and where along
-- the side its foot falls (Q12, s16).
local function side_distance(x0, y0, x1, y1, x2, y2)
    local angle_from_first = angle_signed(sh.math.atan2(x0 - x1, y0 - y1))
    local angle_from_second = angle_signed(sh.math.atan2(x0 - x2, y0 - y2))
    local side_angle = angle_signed(sh.math.atan2(x2 - x1, y2 - y1))
    local m0 = distance_2d(x0 - x1, y0 - y1)
    local m1 = distance_2d(x0 - x2, y0 - y2)
    local sin0 = sh.math.sin(angle_from_first - side_angle)
    local dist
    if s32(m0 * sin0) < 0 then
        dist = s32(-q12_mul32(m0, sin0))
    else
        dist = q12_mul32(m0, sin0)
    end
    local far = m1 < m0 and m0 or m1
    local near = (not (m1 < m0)) and m0 or m1
    far = s32(asr(far, 6) * asr(far, 6))
    near = s32(asr(near, 6) * asr(near, 6))
    local side = s32(s32(asr(x2 - x1, 6) * asr(x2 - x1, 6)) + s32(asr(y2 - y1, 6) * asr(y2 - y1, 6)))
    if far < s32(near + side) then
        local cos0 = sh.math.cos(angle_from_first - side_angle)
        local cos1 = sh.math.cos(angle_from_second - side_angle)
        local p, q
        if s32(m0 * cos0) < 0 then
            p = s32(-q12_mul32(m0, cos0))
        else
            p = q12_mul32(m0, cos0)
        end
        if s32(m1 * cos1) < 0 then
            q = s32(-q12_mul32(m1, cos1))
        else
            q = q12_mul32(m1, cos1)
        end
        return math.abs(dist), s16(idiv(s32(p << 12), s32(p + q)))
    end
    if m0 < m1 then
        return math.abs(m0), 0
    end
    return math.abs(m1), 4096
end

-- The head box: over the jaws when Harry is in front of them, else between the head and the part
-- of the body he is closest to.
local function shape_update(c)
    local top = { [0] = bone_vec(c, 2, 0, -69, 251), bone_vec(c, 19, -36, 5, 113), bone_vec(c, 23, 36, 5, 113) }
    local bottom = { [0] = bone_vec(c, 2, 0, 237, 166) }
    local px, pz = player.pos_x, player.pos_z
    local dx, dz = asr(px - c.pos_x, 6), asr(pz - c.pos_z, 6)
    local dist = s32(sh.math.sqrt(s32(s32(dx * dx) + s32(dz * dz))) << 6)

    local p0 = top[0]
    local base = s16(sh.math.atan2(px - p0.x, pz - p0.z))
    local left = s16(angle_signed(sh.math.atan2(top[1].x - p0.x, top[1].z - p0.z) - base))
    local right = s16(angle_signed(sh.math.atan2(top[2].x - p0.x, top[2].z - p0.z) - base))
    if left < 1024 and right > -1024 then
        if (left >= 0 and right <= 0) or (dist > 11264 and left >= -64 and right <= 64) then
            if math.abs(left - right) >= 114 then
                set(c, F_MOUTH_OPEN)
                c.box_top = p0.y
                c.box_height = bottom[0].y
                c.cylinder_extent = 2457
                local ox = s32((p0.x - c.pos_x) * 3)
                if ox < 0 then
                    ox = ox + 3
                end
                c.box_offset_x = asr(ox, 2)
                local oz = s32((p0.z - c.pos_z) * 3)
                if oz < 0 then
                    oz = oz + 3
                end
                c.box_offset_z = asr(oz, 2)
                return
            end
        end
    end
    if dist > 11264 then
        clear(c, F_MOUTH_OPEN)
    end
    top[3] = bone_vec(c, 1, 0, -70, -183)

    local cross, along = {}, {}
    cross[0], along[0] = side_distance(px, pz, p0.x, p0.z, asr(top[1].x + top[2].x, 1), asr(top[1].z + top[2].z, 1))
    local best = 1
    for i = 1, 3 do
        cross[i], along[i] = side_distance(px, pz, p0.x, p0.z, top[i].x, top[i].z)
        if cross[i] < cross[best] then
            best = i
        end
    end
    if best < 3 then
        if along[0] > 3481 then
            along[best] = 3481
        else
            along[best] = along[0]
        end
        if best == 1 then
            bottom[1] = bone_vec(c, 19, -36, 136, 22)
        else
            bottom[2] = bone_vec(c, 23, 36, 136, 22)
        end
    else
        if along[3] > 3686 then
            along[3] = 3686
        end
        bottom[3] = bone_vec(c, 1, 0, 149, -319)
    end
    local t = along[best]
    local u = 4096 - t
    local tb, bb = top[best], bottom[best]
    c.box_top = asr(s32(s32(p0.y * u) + s32(tb.y * t)), 12)
    c.box_height = asr(s32(s32(bottom[0].y * u) + s32(bb.y * t)), 12)
    c.box_offset_y = asr(c.box_top + c.box_height, 1)
    if best == 3 then
        c.cylinder_extent = 2457
    else
        c.cylinder_extent = asr(s32(s32(u * 2457) + s32(t * 1433)), 12)
    end
    c.box_offset_x = asr(s32(s32(p0.x * u) + s32(tb.x * t)), 12) - c.pos_x
    c.box_offset_z = asr(s32(s32(p0.z * u) + s32(tb.z * t)), 12) - c.pos_z
end

-- Distance of (px, pz) to the segment edge_start-edge_end: d, the nearest point, which part is
-- nearest (0 edge_start, 1 edge_end, 2 the segment) and whether the point is on the inner side.
local function edge_distance(px, pz, edge_start, edge_end)
    local angle = s16(sh.math.atan2(edge_end.x - edge_start.x, edge_end.z - edge_start.z) + 1024)
    local ax = s32(px + sh.math.sin(angle))
    local az = sh.math.cos(angle)
    local r = {}
    r.side = (s32(s32(asr(px - edge_start.x, 2) * asr(edge_end.z - edge_start.z, 2)) - s32(asr(pz - edge_start.z, 2) * asr(edge_end.x - edge_start.x, 2))) < 1)
        and 1 or 0
    local function len(v)
        local a, b = asr(px - v.x, 3), asr(pz - v.z, 3)
        return s32(sh.math.sqrt(s32(s32(a * a) + s32(b * b))) << 3)
    end
    if s32(s32(asr(edge_start.x - px, 2) * asr(az, 2)) - s32(asr(edge_start.z - pz, 2) * asr(ax - px, 2))) < 0 then
        r.d, r.x, r.z, r.kind = len(edge_start), edge_start.x, edge_start.z, 0
    elseif s32(s32(asr(edge_end.x - px, 2) * asr(az, 2)) - s32(asr(edge_end.z - pz, 2) * asr(ax - px, 2))) > 0 then
        r.d, r.x, r.z, r.kind = len(edge_end), edge_end.x, edge_end.z, 1
    else
        local heading = s16(sh.math.atan2(edge_end.x - edge_start.x, edge_end.z - edge_start.z))
        local l = len(edge_start)
        local to_point = s16(sh.math.atan2(px - edge_start.x, pz - edge_start.z) - heading)
        r.d = math.abs(asr(s32(l * sh.math.sin(to_point)), 12))
        l = asr(s32(l * sh.math.cos(to_point)), 12)
        r.x = asr(s32(l * sh.math.sin(heading)), 12) + edge_start.x
        r.z = asr(s32(l * sh.math.cos(heading)), 12) + edge_start.z
        r.kind = 2
    end
    return r
end

local function copy_row(dst, src)
    dst.d, dst.x, dst.z, dst.kind, dst.side, dst.edge = src.d, src.x, src.z, src.kind, src.side, src.edge
end

local function next_edge(e)
    if e == 8 then
        return edge_first
    end
    return e + 1
end

local function prev_edge(e)
    if e ~= edge_first then
        return e - 1
    end
    return 8
end

-- How far Harry at his position plus (ox, oz) sits inside the body outline: the offset that pushes
-- him out of it.
local function body_push(c, ox, oz)
    if has(c, F_SWALLOWED) then
        return 0, 0
    end
    local radius = player.cylinder_radius
    local nx, nz = s32(player.pos_x + ox), s32(player.pos_z + oz)
    local rows, angles = {}, {}
    local kinds, sides = {}, {}
    for i = 0, 8 do
        kinds[i], sides[i] = edge_kind[i], edge_side[i]
    end
    local k = 0
    for i = edge_first, 8 do
        local r = edge_distance(nx, nz, points[i], points[next_edge(i)])
        kinds[i], sides[i] = r.kind, r.side
        r.edge = i
        rows[k] = r
        if radius >= r.d then
            k = k + 1
        end
    end
    local count = k
    if k ~= 0 then
        k = 0
        local j = 0
        while j < 5 and k == 0 do
            local i = 0
            while i < count do
                local ri = rows[i]
                if j < 3 then
                    local nxt = rows[(i + 1) % count]
                    if (j == 0 and ri.side ~= 0 and ri.kind == 2) or
                        (j == 1 and ri.kind == 1 and nxt.kind == 0 and next_edge(ri.edge) == nxt.edge) or
                        (j == 2 and ri.side ~= 0) then
                        copy_row(rows[k], ri)
                        rows[k].d = s32(radius - rows[k].d)
                        angles[k] = s16(sh.math.atan2(nx - ri.x, nz - ri.z))
                        k = k + 1
                        if j == 1 then
                            i = i + 1
                        end
                    end
                else
                    local e = ri.edge
                    local before, after
                    if j == 3 then
                        before, after = prev_edge(e), next_edge(e)
                    elseif edge_kind[e] ~= 0 then
                        before, after = next_edge(e), 0
                    else
                        before, after = prev_edge(e), 0
                    end
                    if (j == 3 and edge_kind[e] == 2 and
                            (edge_side[e] ~= 0 or (edge_kind[before] == 1 and edge_side[before] ~= 0) or
                                (edge_kind[after] == 0 and edge_side[after] ~= 0))) or
                        (j == 4 and edge_kind[e] ~= 2 and (edge_side[e] ~= 0 or edge_side[before] ~= 0)) then
                        copy_row(rows[k], ri)
                        rows[k].d = s32(radius + rows[k].d)
                        angles[k] = s16(sh.math.atan2(ri.x - nx, ri.z - nz))
                        k = k + 1
                        sides[ri.edge] = edge_side[ri.edge]
                        kinds[ri.edge] = edge_kind[ri.edge]
                    end
                end
                i = i + 1
            end
            j = j + 1
        end
        if k == 0 then
            for i = 0, count - 1 do
                rows[i].d = s32(radius + rows[i].d)
                angles[i] = s16(sh.math.atan2(rows[i].x - nx, rows[i].z - nz))
            end
            k = count
        end
    end

    ox, oz = 0, 0
    for i = 0, k - 1 do
        ox = s32(ox + q12_mul32(rows[i].d, sh.math.sin(angles[i])))
        oz = s32(oz + q12_mul32(rows[i].d, sh.math.cos(angles[i])))
        local keyframe = asr(c.anim_time, 12)
        if keyframe > 13 and keyframe < 34 and (rows[i].edge < 2 or rows[i].edge > 6) then
            grab_blocked = 1
        end
    end
    for i = 0, 8 do
        edge_kind[i], edge_side[i] = kinds[i] & 0xFF, sides[i] & 0xFF
    end
    return ox, oz
end

-- The body outline: nine points around the jaws and body (the first two only while the jaws face
-- Harry).
local function body_points(c)
    local function set_point(i, bone, p, mirror)
        local x, y, z = bone_point(c, bone, mirror and -p[1] or p[1], p[2], p[3])
        points[i].x, points[i].y, points[i].z = x, y, z
    end
    set_point(0, 17, BODY[1])
    set_point(1, 21, BODY[1], true)
    points[0].x = asr(points[0].x + points[1].x, 1)
    points[0].y = asr(points[0].y + points[1].y, 1)
    points[0].z = asr(points[0].z + points[1].z, 1)
    set_point(1, 19, BODY[3])
    set_point(2, 19, BODY[4])
    set_point(3, 2, BODY[2], true)
    set_point(4, 3, BODY[5])
    set_point(5, 7, BODY[5], true)
    set_point(6, 2, BODY[2])
    set_point(7, 23, BODY[4], true)
    set_point(8, 23, BODY[3], true)
end

-- The body against Harry: the outline pushes him (with a knock while he is not being eaten) and
-- the jaws catch him.
local function bite_update(c)
    c.box_bottom = 0
    c.cylinder_radius = 0
    c.cylinder_offset_x = 0
    c.cylinder_offset_z = 0
    sh.entity.set_collision_state(c.slot, 4)

    body_points(c)
    if not has(c, F_MOUTH_OPEN) then
        edge_first = 2
        points[8].x = asr(points[8].x + points[1].x, 1)
        points[8].y = asr(points[8].y + points[1].y, 1)
        points[8].z = asr(points[8].z + points[1].z, 1)
    else
        edge_first = 0
    end

    grab_blocked = 0
    local ox, oz = body_push(c, push_x, push_z)
    local vx, vz = 0, 0
    if grab_blocked == 0 then
        -- The knock is tuned for one 30 Hz tick: scale this frame's step to one.
        local dt = sh.game.delta_time()
        local function knock(o)
            local step_30hz = dt > 0 and s32(idiv(o * TICK_30HZ, dt)) or o
            local t = math.abs(step_30hz) - 32
            local v = t < 0 and 0 or asr(t, 4)
            if step_30hz <= 0 then
                return s32(v * -v)
            end
            return s32(v * v)
        end
        vx = knock(ox)
        vz = knock(oz)
    end
    sh.entity.split_head_push_player(c.slot, s32(ox + push_x), s32(oz + push_z), vx, 0, vz)

    if has(c, F_MOUTH_OPEN) then
        local dx, dz = asr(player.pos_x - c.pos_x, 6), asr(player.pos_z - c.pos_z, 6)
        if s32(sh.math.sqrt(s32(s32(dx * dx) + s32(dz * dz))) << 6) < 13516 then
            local px, pz = player.pos_x, player.pos_z
            local upper = edge_distance(px, pz, points[0], points[1])
            local lower = edge_distance(px, pz, points[0], points[8])
            local t = c.anim_time
            local reach
            if t < 31 * 4096 then
                reach = 0
            elseif t < 33 * 4096 then
                reach = s16(asr(q12_mul(player.cylinder_radius, t - 31 * 4096), 1))
            else
                reach = 0
                if t < 37 * 4096 then
                    reach = player.cylinder_radius
                end
            end
            if upper.d < reach and lower.d < reach and not has(c, F_BITTEN) then
                player.attack_received = ATTACK_SWALLOW
                set(c, F_SWALLOWED | F_BITTEN)
                sh.sfx_flags_play(sound(c, "bite_hit", 1473), player.pos_x, player.pos_y, player.pos_z, 255, 2)
            end
        end
    end
end

local function body_push_update(c)
    push_x, push_z = 0, 0
    push_x, push_z = body_push(c, push_x, push_z)
end

-- Blood: drops from the jaws and body now and then (more while roaring or backing off), strands
-- between the jaws while they are open.
local function blood_update(c)
    local dt = sh.game.delta_time()
    if dt ~= 0 then
        local ready
        drop_tick, ready = tick_30hz(drop_tick)
        local clip = c.anim_status >> 1
        if ready and clip ~= A.lying and clip ~= A.splayed_rise then
            local keyframe = asr(c.anim_time, 12)
            local drop = false
            if keyframe < 20 or keyframe > 35 then
                if keyframe > 14 and keyframe < 20 then
                    drop = true
                elseif (clip == A.roar or clip == A.flinch_back) and sh.rng.uint(0, 3) == 0 then
                    drop = true
                elseif sh.rng.uint(0, 31) == 0 then
                    drop = true
                end
            end
            if drop then
                local i
                if c.anim_status == S(A.lunge, true) then
                    i = sh.rng.int(0, 37)
                else
                    i = sh.rng.int(0, 21)
                end
                local e = BLOOD[i + 1]
                local x, y, z = bone_point(c, e[1], e[2], e[3], e[4])
                local speed, heading
                if keyframe >= 15 and keyframe <= 19 then
                    speed = 8192
                    if e[1] < 20 then
                        heading = s16(c.rot_y + 1024)
                    else
                        heading = s16(c.rot_y - 1024)
                    end
                else
                    local ms = c.move_speed
                    if ms > 0 then
                        heading = c.rot_y
                        speed = s16(asr(ms, 1))
                    else
                        speed = s16(asr(-ms, 1))
                        heading = s16(c.rot_y + 2048)
                    end
                end
                sh.entity.split_head_blood_drop(c.slot, asr(x, 4), asr(y, 4), asr(z, 4), speed, heading & 0xFFF)
            end
        end
    end

    local status = c.anim_status
    if LYING[status] then
        c.split_head_blood_alpha = math.max(0, c.split_head_blood_alpha - idiv(dt, 68))
    end
    if STRANDS_SHOWN[status] then
        local alpha = c.split_head_blood_alpha
        for _, s in ipairs(STRANDS) do
            local a, b = BLOOD[s[1] + 1], BLOOD[s[2] + 1]
            local ax, ay, az = bone_point(c, a[1], a[2], a[3], a[4])
            local bx, by, bz = bone_point(c, b[1], b[2], b[3], b[4])
            sh.entity.split_head_blood_strand(c.slot, s[3], alpha, asr(ax, 4), asr(ay, 4), asr(az, 4),
                asr(bx, 4), asr(by, 4), asr(bz, 4))
        end
    end
end

-- Leg steps (two of the four legs sound) and the vocals the controls asked for.
local LEGS = {
    { field = "split_head_leg_1", angle = 512, sound = true,
      ranges = { { 147, 162 }, { 187, 201 } } },
    { field = "split_head_leg_2", angle = 1536, sound = false,
      ranges = { { 132, 148 }, { 161, 162 }, { 172, 185 }, { 200, 202 } } },
    { field = "split_head_leg_3", angle = -512, sound = true,
      ranges = { { 132, 147 }, { 160, 162 }, { 172, 187 }, { 199, 202 } } },
    { field = "split_head_leg_4", angle = -1536, sound = false,
      ranges = { { 146, 161 }, { 186, 200 } } },
}

local function sounds_update(c)
    local keyframe = asr(c.anim_time, 12)
    local rate = idiv(s32(walk_rate << 12), 98304)
    local vol = asr(rate, 7) + 96
    local pitch = asr(rate, 6) - 64 + sh.rng.uint(0, 7)
    for _, leg in ipairs(LEGS) do
        local inside = false
        for _, r in ipairs(leg.ranges) do
            if keyframe >= r[1] and keyframe < r[2] then
                inside = true
                break
            end
        end
        if inside then
            if c[leg.field] == 0 then
                if leg.sound then
                    local x = c.pos_x + asr(s32(sh.math.sin(c.rot_y + leg.angle) * 4915), 12)
                    local z = c.pos_z + asr(s32(sh.math.cos(c.rot_y + leg.angle) * 4915), 12)
                    sh.sfx_pitch_play(sound(c, "footstep", 1472), x, c.pos_y, z, vol, pitch)
                end
                c[leg.field] = c[leg.field] + 1
            end
        else
            c[leg.field] = 0
        end
    end

    if c.split_head_vocal_timer == 0 then
        if has(c, F_VOCAL) then
            vocal_sound(c, "vocal", 1422)
            c.split_head_vocal_timer = sh.rng.int(12288, 20479)
        elseif has(c, F_VOCAL_2) then
            vocal_sound(c, "vocal_2", 1474)
            c.split_head_vocal_timer = sh.rng.int(6144, 10239)
        end
    else
        c.split_head_vocal_timer = c.split_head_vocal_timer - sh.game.delta_time()
        if c.split_head_vocal_timer < 0 then
            c.split_head_vocal_timer = 0
        end
    end
    clear(c, F_VOCAL | F_VOCAL_2)
end

function update(slot)
    local c = record(slot)
    if c.control_state == 0 then
        init(c)
    end
    local dt = sh.game.delta_time()
    if dt ~= 0 then
        damage_take(c)
        control_update(c)
        movement_update(c)
        body_push_update(c)
    end
    anim_update(c)
    if dt ~= 0 then
        shape_update(c)
        bite_update(c)
    end
    blood_update(c)
    sounds_update(c)
end
