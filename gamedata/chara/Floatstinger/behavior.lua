-- Floatstinger, the giant moth fought in the Otherworld streets (map4_s05). In the opening cutscene it
-- sinks down from above the stairs (control 5); when the cutscene sets flag M4S05_CybilStairsCutsceneDone it fights (control 2): it
-- hovers near Harry, stings him from close up and spits acid from further away. Heavy damage makes it
-- recoil with its wings flailing (control 3); 1200 points of damage enrage it (faster flight, three acid
-- drops at once) for 24 to 40 seconds. At 0 health it sets flag M4S05_FloatstingerKilled, the death cutscene sets flag M4S05_FloatstingerLandingTriggered and
-- it lands for good (control 6).
-- The arena edges are three floor boxes the body is kept out of; the collision shape follows bone points.

local A = clips("Floatstinger", { sting = 1, descend = 5, hover = 9, spit = 12 })

local function S(clip, active)
    return anim_status(clip, active)
end

local STING = S(A.sting, true)
local STING_IN = S(A.sting, false)
local SPIT = S(A.spit, true)
local SPIT_IN = S(A.spit, false)
local HOVER = S(A.hover, true)
local LAND_IN = S(A.descend, false)

-- floatstinger_flags bits.
local F_FAST = 0x1          -- hard difficulty or enraged: faster flight and three acid drops
local F_RAGE = 0x2          -- 1200 damage points: a longer recoil, then fast for a while
local F_STING_COUNTED = 0x4 -- the sting of this clip counted in attack_window
local F_SPIT_COOLDOWN = 0x8
local F_VOCAL = 0x10        -- the vocal of the current clip window played

local FLAG_FIGHT = "M4S05_CybilStairsCutsceneDone"
local FLAG_DEAD = "M4S05_FloatstingerKilled"
local FLAG_LANDED = "M4S05_FloatstingerLandingTriggered"
local DIFFICULTY_EASY, DIFFICULTY_HARD = -1, 1
local CHARA_FLAGS_START = 0x104  -- no radio static, hit reactions
local PLAYER_FLAG_SPARED = 0x8      -- enemies hold their attacks on Harry
local ATTACK_STING = sh.entity.attack_number("FloatstingerSting")

-- Bones whose rotation the recoil bends (the wings), and the axis of each: the first three around z, then
-- x and y in turn.
local WING_BONES = { 6, 7, 8, 14, 15, 17, 18, 20, 21, 23, 24, 26, 27, 29, 30 }
-- How far each wing bone bends in the recoil (Q12 angle).
local WING_BEND = { 64, -256, -384, 192, 320, -64, -320, 320, -448, -192, 448, 0, 704, 256, -704 }

-- The floor boxes around the arena (x and z extents, top height; Q12); the body is pushed out of the first
-- box its bottom is below the top of.
local BOXES = {
    { x0 = -507904, x1 = -473088, z0 = 393216, z1 = 458752, top = 6144 },
    { x0 = -495616, x1 = -471040, z0 = 393216, z1 = 421888, top = -5324 },
    { x0 = -485376, x1 = -477184, z0 = 405504, z1 = 413696, top = -20480 },
}

-- Module state, shared by every Floatstinger of the map.
local push_x, push_z = 0, 0  -- s16: the push out of the floor boxes found by the last box check
local wing = {}              -- s16 bend of each wing bone
for i = 1, #WING_BONES do
    wing[i] = 0
end
local recoil_timer = 0
local spit_delay = 0         -- s16: the base wait between two spits

local player = record(0)

local function dt()
    return sh.game.delta_time()
end

local function has(c, bit)
    return c.floatstinger_flags & bit ~= 0
end

local function set(c, bit)
    c.floatstinger_flags = c.floatstinger_flags | bit
end

local function clear(c, bit)
    c.floatstinger_flags = c.floatstinger_flags & ~bit
end

local function keyframe(c)
    return asr(c.anim_time, 12)
end

-- A bone-local point (geometry units) in the world, Q12.
local function bone_point(c, bone, x, y, z)
    local wx, wy, wz = sh.entity.bone_point(c.slot, bone, x, y, z)
    return s32(wx * 16), s32(wy * 16), s32(wz * 16)
end

local function sfx_at(c, name, fallback, x, y, z, vol)
    sh.entity.sfx_flags_at(sound(c, name, fallback), x, y, z, vol, 0)
end

-- Keeps the point (x, y, z) of the body out of the floor boxes: true when it is too close to the box under
-- it, with push_x/push_z set to the way out.
local function box_push(c, x, y, z)
    push_x, push_z = 0, 0
    local bottom = y + c.box_bottom
    local box
    for i = 1, 3 do
        if bottom >= BOXES[i].top then
            box = BOXES[i]
            break
        end
    end
    if box == nil then
        return false
    end
    -- Where the point is around the box: 0..8, x in thirds, z in rows of three (4: inside).
    local side
    if box.x0 <= x then
        side = (box.x1 < x) and 2 or 1
    else
        side = 0
    end
    if box.z0 <= z then
        side = side + ((box.z1 < z) and 6 or 3)
    end
    local dist, angle = 0, 0
    if side == 0 or side == 2 or side == 6 or side == 8 then
        local cx = (side % 6 ~= 0) and box.x1 or box.x0
        local cz = (side > 2) and box.z1 or box.z0
        dist = distance_2d(x - cx, z - cz)
        angle = sh.math.atan2(s32(x - cx), s32(z - cz))
    elseif side == 1 then
        angle, dist = 2048, box.z0 - z
    elseif side == 3 then
        angle, dist = 3072, box.x0 - x
    elseif side == 5 then
        angle, dist = 1024, x - box.x1
    elseif side == 7 then
        angle, dist = 0, z - box.z1
    end
    -- The body is longer than it is wide: the clearance depends on how it faces the edge.
    local facing = abs(angle_signed(angle - c.rot_y))
    local across = (1024 - facing < 0) and (2048 - facing) or facing
    local clearance
    if facing > 1024 then
        clearance = asr(s32(across << 13), 10) + 4096
    else
        clearance = asr(s32(across << 12), 10) + 8192
    end
    if dist < clearance then
        push_x = s16(q12_mul32(clearance - dist, sh.math.sin(angle)))
        push_z = s16(q12_mul32(clearance - dist, sh.math.cos(angle)))
        return true
    end
    return false
end

-- A heading near `heading` along which `dist` is free of characters: tried alternately left and right,
-- further out each time; `heading` itself when none is free.
local function free_heading(c, dist, heading)
    local first = (sh.rng.rand16() & 0xFF) >= 0x80 and 1 or 0
    local x, y, z = c.pos_x, c.pos_y + c.box_bottom, c.pos_z
    for i = first, first + 7 do
        local offset
        if i & 1 ~= 0 then
            offset = (256 << ((i + 1) >> 1)) + (sh.rng.rand16() & 0x3F)
        else
            offset = -((256 << ((i + 2) >> 1)) + (sh.rng.rand16() & 0x3F))
        end
        offset = s16(offset)
        local angle = offset + heading
        local dx = q12_mul32(dist, sh.math.sin(angle))
        local dz = q12_mul32(dist, sh.math.cos(angle))
        if sh.entity.ray_chara_blocked(c.slot, x, y, z, dx, 0, dz) == 0 then
            return angle_signed(heading + offset)
        end
    end
    return heading
end

-- The wing-flap speed follows the flight: faster when it flies fast, climbs or turns.
local function flap_update(c)
    local heading = c.heading_angle
    local vx = s16(q12_mul32(c.move_speed, sh.math.sin(heading)))
    local vy = s16(c.fall_speed)
    local vz = s16(q12_mul32(c.move_speed, sh.math.cos(heading)))
    local function q6sq(v)
        local q = asr(v, 6)
        return s32(q * q)
    end
    local speed = s16(s32(sh.math.sqrt(s32(q6sq(vx) + q6sq(vy) + q6sq(vz))) << 6))
    local by_speed = asr(q12_mul(speed, 61440 - speed), 1) + 53248
    local turn = c.floatstinger_turn_rate
    local by_turn
    if turn == 0 then
        by_turn = 53248
    elseif turn >= 0 then
        by_turn = q12_mul(turn, 98304) + 86016
    else
        by_turn = q12_mul(-turn, 98304) + 86016
    end
    -- The game eases towards the target by at most 6 per second, but both of its branches settle on the
    -- target at once.
    sh.entity.floatstinger_set_flap_speed((by_turn < by_speed) and by_speed or by_turn)
end

-- One tick of flight towards (x, y, z), facing `face` once within 6.5 m.
local function fly_to(c, x, y, z, face)
    local fast = has(c, F_FAST)
    local step = dt()
    face = s16(face)
    local dist = distance_2d(x - c.pos_x, z - c.pos_z)
    local toward = s16(sh.math.atan2(s32(x - c.pos_x), s32(z - c.pos_z)))

    -- Turn.
    local turn_to
    if dist > 26624 then
        turn_to = s16(toward - c.rot_y)
    else
        turn_to = s16(face - c.rot_y)
    end
    local off = angle_signed(turn_to)
    local turn = c.floatstinger_turn_rate
    local small
    if fast then
        small = abs(asr(q12_mul(turn, step) * 3, 4) + 1)
    else
        small = abs(asr(q12_mul(turn, step), 1) + 1)
    end
    local limit = fast and 1536 or 1024
    local accel = q12_mul(step, limit)
    if abs(off) > small then
        if off > 0 then
            c.floatstinger_turn_rate = min(limit, turn + accel)
        else
            c.floatstinger_turn_rate = max(-limit, turn - accel)
        end
        c.rot_y = angle_signed(q12_mul(step, c.floatstinger_turn_rate) + c.rot_y)
    else
        c.rot_y = face
        local ease = accel * 2
        if turn > 0 then
            c.floatstinger_turn_rate = max(turn - ease, 0)
        else
            c.floatstinger_turn_rate = min(0, turn + ease)
        end
    end

    -- Steering: straight at the target, or around Harry when the way runs into a floor box.
    if c.floatstinger_drift_timer == 0 or dist < 16384 then
        local reach = min(dist, 14336)
        local nx = c.pos_x + q12_mul32(reach, sh.math.sin(toward))
        local ny = c.pos_y
        local nz = c.pos_z + q12_mul32(reach, sh.math.cos(toward))
        if box_push(c, nx, ny, nz) then
            if c.floatstinger_detour_timer ~= 0 then
                c.floatstinger_steer_angle = c.floatstinger_detour_angle
                c.floatstinger_detour_timer = max(c.floatstinger_detour_timer - step, 0)
            else
                c.floatstinger_steer_angle = free_heading(c, reach, toward)
                c.floatstinger_detour_angle = c.floatstinger_steer_angle
                c.floatstinger_detour_timer = rng_int(1638, 4094)
            end
        else
            c.floatstinger_steer_angle = toward
            c.floatstinger_detour_timer = 0
        end
    else
        c.floatstinger_drift_timer = c.floatstinger_drift_timer + step
        if c.floatstinger_drift_timer > 3276 and rng_uint(0, 15) == 0 then
            c.floatstinger_drift_timer = 0
        end
    end

    -- Velocity: a push along the steer heading on top of the current speed (kept along the push, mostly
    -- dropped against it).
    local thrust = q12_mul(step, fast and 10240 or 6144)
    local steer = c.floatstinger_steer_angle
    local vx = s16(q12_mul32(sh.math.sin(steer), thrust))
    local vz = s16(q12_mul32(sh.math.cos(steer), thrust))
    local hs = sh.math.sin(c.heading_angle)
    if vx * hs > 0 then
        vx = s16(vx + q12_mul32(c.move_speed, hs))
    else
        local keep = q12_mul32(c.move_speed, hs)
        vx = s16(vx + (fast and asr(keep, 3) or asr(keep, 2)))
    end
    local hc = sh.math.cos(c.heading_angle)
    if vz * hc > 0 then
        vz = s16(vz + q12_mul32(c.move_speed, hc))
    else
        local keep = q12_mul32(c.move_speed, hc)
        vz = s16(vz + (fast and asr(keep, 3) or asr(keep, 2)))
    end
    c.heading_angle = sh.math.atan2(vx, vz)
    local floor_speed = fast and 14336 or 8192
    local speed = distance_2d(vx, vz)
    if speed > floor_speed then
        c.move_speed = distance_2d(vx, vz)
    else
        c.move_speed = floor_speed
    end
    if dist < 16384 then
        if dist < 204 then
            c.move_speed = 0
            c.heading_angle = c.rot_y
            c.pos_x = x
            c.pos_z = z
        else
            c.move_speed = asr(c.move_speed + idiv(s32(c.move_speed * dist), 16384), 1)
            local miss = abs(angle_signed(toward - c.heading_angle))
            if miss > 512 then
                c.move_speed = q12_mul(c.move_speed, (miss << 1) + 512)
            end
        end
    end

    -- Height.
    local dy = s32(y - c.pos_y)
    local ady = abs(dy)
    if ady > 81 then
        if dy < 0 then
            c.fall_speed = max(fast and -20480 or -4096, c.fall_speed + q12_mul(step, fast and -12288 or -3276))
        else
            c.fall_speed = min(fast and 36864 or 12288, c.fall_speed + q12_mul(step, fast and 18432 or 7372))
        end
        if ady < 4096 then
            local fall = c.fall_speed
            c.fall_speed = asr(fall + idiv(s32(fall * ady), 4096), 1)
        end
    else
        c.fall_speed = 0
        c.pos_y = y
    end

    flap_update(c)
end

local function init(c)
    local difficulty = sh.save.game_difficulty()
    c.floatstinger_flags = 0
    c.anim_alpha = 0
    c.health = sh.entity.param(c.slot, "health", 16384000)
    if difficulty == DIFFICULTY_HARD then
        c.health = sh.entity.param(c.slot, "health_hard", 24576000)
    end
    c.move_speed = 0
    c.fall_speed = 0
    sh.entity.set_collision_state(c.slot, 4)
    sh.entity.props_clear(c.slot)
    local step = c.state_step
    if step == 3 then
        -- Above the stairs, waiting for the opening cutscene.
        c.control_state = 5
        anim_set(c, HOVER, 191)
        c.floatstinger_vocal_volume = -0x90
    elseif step == 4 then
        -- The fight already started (the map was entered again).
        c.pos_x = -468992
        c.pos_y = 8192
        c.pos_z = 442368
        c.control_state = 2
        anim_set(c, HOVER, 191)
        c.rot_y = -1024
    end
    c.state_step = 0
    sh.entity.clip_table_attach(c.slot, 0)
    damage_clear(c)
    push_z, push_x = 0, 0
    c.heading_angle = c.rot_y
    for i = 1, #WING_BONES do
        wing[i] = 0
    end
    c.floatstinger_prev_rot_y = c.rot_y
    c.flags = c.flags | CHARA_FLAGS_START
    recoil_timer = 0
    if difficulty == DIFFICULTY_HARD then
        spit_delay = 9830
        set(c, F_FAST)
    elseif difficulty == DIFFICULTY_EASY then
        spit_delay = 16384
    else
        spit_delay = 13107
    end
end

local function damage_react(c)
    c.floatstinger_damage_window = max(c.floatstinger_damage_window - q12_mul(dt(), 614400), 0)
    if c.damage_amount > 0 then
        local cap = (sh.save.game_difficulty() == DIFFICULTY_EASY) and 2048000 or 1228800
        c.floatstinger_damage_window = s32(c.floatstinger_damage_window + c.damage_amount)
        if cap < c.floatstinger_damage_window then
            c.damage_amount = c.damage_amount - (c.floatstinger_damage_window - cap)
            c.floatstinger_damage_window = cap
            if c.damage_amount < 0 then
                c.damage_amount = 0
            end
        end
        local dead = sh.entity.player_damage_disable(0)
        local health = s32(c.health - c.damage_amount)
        if dead ~= 0 then
            if health <= 0 then
                health = 1
            end
        elseif health < 0 then
            health = 0
        end
        c.health = health
        local points = asr(c.damage_amount, 12)
        c.floatstinger_stagger_damage = c.floatstinger_stagger_damage + points
        c.floatstinger_rage_damage = c.floatstinger_rage_damage + points
        -- The game plays the hit at a point it never sets (stack contents on PS1); the world origin keeps it
        -- out of earshot as it is with every weapon.
        sfx_at(c, "damage", 1570, 0, 0, 0, 128)
        if c.health == 0 or ((c.floatstinger_stagger_damage > 1000 or c.floatstinger_rage_damage > 1200) and
                c.control_state == 2 and c.anim_status == HOVER) then
            c.floatstinger_stagger_damage = 0
            c.control_state = 3
            if c.floatstinger_rage_damage > 1200 then
                c.floatstinger_rage_damage = 0
                set(c, F_RAGE)
            end
        end
    end
    if c.floatstinger_rage_damage > 1200 and c.control_state == 2 and c.anim_status == HOVER then
        c.floatstinger_stagger_damage = 0
        c.floatstinger_rage_damage = 0
        c.control_state = 3
        set(c, F_RAGE)
    end
    damage_clear(c)
    sh.entity.player_damage_disable(c.health == 0 and 1 or 0)
end

-- The ahead-of-Harry point the sting and spit aim from.
local function attack_target(c, px, py, pz)
    -- Harry on the stairs landing: a point on the circle around the stairwell, or beside the stairs.
    if px > -491520 and px < -471040 and pz > 393216 and pz < 421888 then
        local r = distance_2d(px + 481280, pz - 409600)
        local angle = s16(sh.math.atan2(s32(px + 481280), s32(pz - 409600)))
        local reach = (c.anim_status == STING) and (r + 2457) or (r + 4915)
        local x = q12_mul32(reach, sh.math.sin(angle)) - 481280
        local z = q12_mul32(reach, sh.math.cos(angle)) + 409600
        return 0, x, -4096, z
    end
    if px > -495616 and px < -491520 and pz > 393216 and pz < 421888 then
        return 1, px - 10240, py + 3276, pz
    end
    -- In the street: just short of Harry on the Floatstinger's side.
    local angle = s16(sh.math.atan2(s32(c.pos_x - px), s32(c.pos_z - pz)))
    local s = sh.math.sin(angle)
    local x = (c.anim_status == STING) and (px + q12_mul32(s * 39, 63)) or (px + q12_mul32(s, 4915))
    local y = 8192
    local co = sh.math.cos(angle)
    local z = (c.anim_status == STING) and (pz + q12_mul32(co * 39, 63)) or (pz + q12_mul32(co, 4915))
    box_push(c, x, y, z)
    return 2, x + push_x, y, z + push_z
end

local function spit(c, px, py, pz)
    local mx, my, mz = bone_point(c, 9, 20, -8, 0)
    -- Aimed where Harry is heading: his move over the last seven ticks, stretched by 1 to 2.
    local tx, ty, tz = sh.entity.floatstinger_trail(7)
    local lead_angle = s16(sh.math.atan2(s32(px - tx), s32(pz - tz)))
    local qx, qz = asr(tx - px, 6), asr(tz - pz, 6)
    local lead = sh.math.sqrt(s32(s32(qx * qx) + s32(qz * qz)))
    local stretch = rng_uint(4096, 8191)
    lead = q12_mul(s32(lead << 6), stretch)
    local hx = px + player.box_offset_x + q12_mul32(lead, sh.math.sin(lead_angle))
    local hz = pz + player.box_offset_z + q12_mul32(lead, sh.math.cos(lead_angle))
    local rise = s16(q12_mul(py - ty, rng_uint(4096, 8191)))
    local base = player.box_offset_y
    local hy = max(py + rise, -4096)
    if hy <= 8192 then
        hy = base + hy
    else
        hy = base + 8192
    end
    sfx_at(c, "attack_2", 1572, c.pos_x, c.pos_y - 14336, c.pos_z, 128)
    sh.entity.floatstinger_acid(mx, my, mz, hx, hy, hz)
    if has(c, F_FAST) then
        -- Two more drops scattered left and right of the first.
        for i = 0, 1 do
            local spread = rng_uint(0, 2047)
            local angle = rng_uint(0, 4095)
            local side = (i ~= 0) and 1024 or -1024
            local x = hx + q12_mul32(spread, sh.math.sin(angle)) + sh.math.sin(c.rot_y + side)
            local z = hz + q12_mul32(spread, sh.math.cos(angle)) + sh.math.cos(c.rot_y + side)
            local y = hy + s16(rng_uint(-1024, 1023))
            local oy = player.box_offset_y
            local low = max(y, oy - 12288)
            y = (oy < low) and oy or low
            sh.entity.floatstinger_acid(mx, my, mz, x, y, z)
        end
    end
    c.floatstinger_spit_timer = q12_mul(spit_delay, rng_uint(3072, 4095))
    set(c, F_SPIT_COOLDOWN)
    if sh.save.game_difficulty() ~= DIFFICULTY_HARD and c.floatstinger_rage_timer == 0 then
        clear(c, F_FAST)
    end
end

-- Control 2: the fight.
local function control_fight(c)
    local px, py, pz = player.pos_x, player.pos_y, player.pos_z
    local zone, tx, ty, tz = attack_target(c, px, py, pz)
    local face = sh.math.atan2(s32(px - tx), s32(pz - tz))
    local status = c.anim_status

    if status == HOVER then
        fly_to(c, tx, ty, tz, face)
        c.floatstinger_rage_timer = max(c.floatstinger_rage_timer - dt(), 0)
        if has(c, F_SPIT_COOLDOWN) then
            c.floatstinger_spit_timer = max(0, c.floatstinger_spit_timer - dt())
            if c.floatstinger_spit_timer == 0 then
                clear(c, F_SPIT_COOLDOWN)
            end
        end
        local angle = s16(sh.math.atan2(s32(px - c.pos_x), s32(pz - c.pos_z)))
        if abs(py - c.pos_y) < 204 and distance_2d(px - c.pos_x, pz - c.pos_z) < 6144 and
                ((zone == 0 and rng_uint(0, 3) ~= 0) or (zone == 2 and rng_uint(0, 3) ~= 0)) then
            -- Close and level with Harry: sting (1 in 8 when facing him).
            if player.health > 0 and abs(angle_signed(angle - c.heading_angle)) < 1024 and
                    abs(angle_signed(angle - c.rot_y)) < 341 and player.flags & PLAYER_FLAG_SPARED == 0 and
                    rng_uint(0, 7) == 0 then
                c.anim_status = STING_IN
                clear(c, F_VOCAL)
            end
        elseif not has(c, F_SPIT_COOLDOWN) and distance_2d(px - c.pos_x, pz - c.pos_z) < 26624 then
            -- Within 6.5 m and facing him: spit (1 in 4).
            if abs(angle_signed(angle - c.heading_angle)) < 1024 and abs(angle_signed(angle - c.rot_y)) < 170 then
                if abs(c.pos_y) < 32768 and player.flags & PLAYER_FLAG_SPARED == 0 and rng_uint(0, 3) == 0 then
                    c.anim_status = SPIT_IN
                end
            end
        end
    elseif status == STING then
        local k = keyframe(c)
        if k > 0 and k < 18 then
            fly_to(c, tx, ty, tz, face)
            c.floatstinger_sting_sound = 0
        elseif k > 17 and k < 25 then
            c.move_speed = 0
            if not has(c, F_STING_COUNTED) then
                c.attack_window = c.attack_window + 1
                set(c, F_STING_COUNTED)
            end
            -- The stinger: from its root to its tip.
            local ax, ay, az = bone_point(c, 5, -30, -10, -5)
            local bx, by, bz = bone_point(c, 5, 34, -2, 0)
            if c.floatstinger_sting_sound == 0 then
                sfx_at(c, "attack", 1571, c.pos_x, c.pos_y - 4096, c.pos_z, 128)
                c.floatstinger_sting_sound = c.floatstinger_sting_sound + 1
            end
            sh.entity.attack(c.slot, 1, ATTACK_STING, ax, ay, az, 0,
                sh.math.atan2(s32(bx - ax), s32(bz - az)),
                sh.math.atan2(distance_2d(bx - ax, bz - az), s32(by - ay)))
            if sh.save.game_difficulty() ~= DIFFICULTY_HARD and c.floatstinger_rage_timer == 0 then
                clear(c, F_FAST)
            end
        end
    elseif status == SPIT then
        local k = keyframe(c)
        if k > 0 and k < 18 then
            fly_to(c, tx, ty, tz, face)
        else
            c.move_speed = 0
            if not has(c, F_SPIT_COOLDOWN) then
                spit(c, px, py, pz)
            end
        end
    end

    sh.entity.floatstinger_trail_push(px, py, pz)
end

-- Control 3: recoil. The wings bend out and back, the flight slows; then the fight goes on (enraged after a
-- rage recoil) or, at 0 health, the boss goes down.
local function control_recoil(c)
    if c.health == 0 then
        sh.flag_set(FLAG_DEAD, 1)
    end
    sh.entity.floatstinger_set_flap_speed(53248)
    local rage = has(c, F_RAGE)
    local bend_time = rage and 2457 or 1638
    if (rage and bend_time + 4096 < recoil_timer) or (not rage and bend_time + 2867 < recoil_timer) then
        recoil_timer = 0
        for i = 15, 1, -1 do
            wing[i] = 0
        end
        if rage then
            c.floatstinger_rage_timer = rng_int(98304, 163839)
            clear(c, F_RAGE)
            set(c, F_FAST)
        end
        if c.health ~= 0 then
            c.control_state = 2
        else
            c.control_state = 6
        end
        return
    end
    recoil_timer = recoil_timer + dt()
    local fast = has(c, F_FAST)
    c.move_speed = max(c.move_speed - q12_mul(dt(), fast and 10240 or 6144), 0)
    if c.fall_speed > 0 then
        c.fall_speed = max(c.fall_speed - q12_mul(dt(), fast and 18432 or 7372), 0)
    else
        c.fall_speed = min(c.fall_speed - q12_mul(dt(), fast and -12288 or -3276), 0)
    end
    local t = recoil_timer
    if (rage and t < 2457) or (not rage and t < 1638) then
        -- Bending out.
        for i = 1, 15 do
            local b = WING_BEND[i]
            local ramp = b * recoil_timer
            local phase = s32(recoil_timer << 10)
            local linear = rage and idiv(ramp, 2457) or idiv(ramp, 1638)
            local eased = q12_mul32(b, 4096 - sh.math.cos(rage and idiv(phase, 2457) or idiv(phase, 1638)))
            wing[i] = s16(asr(linear + eased, 1))
        end
    else
        -- And back.
        for i = 1, 15 do
            local b = WING_BEND[i]
            local back = rage and (recoil_timer - 2457) or (recoil_timer - 1638)
            local ramp = b * back
            local phase = s32(back << 10)
            local eased = q12_mul32(b, 4096 - sh.math.cos(rage and idiv(phase, 4096) or idiv(phase, 2867)))
            local linear = rage and idiv(ramp, 4096) or idiv(ramp, 2867)
            wing[i] = s16(b - asr(linear + eased, 1))
        end
    end
end

-- Control 5: the opening cutscene. It waits above the stairs (step 0), sinks to the street when the cutscene
-- asks (step 1), and fights when it ends (step 2). Entered again after the cutscene, it starts at the fight.
local function control_descend(c)
    local step = c.state_step
    if step == 0 then
        if sh.flag_get(FLAG_FIGHT) then
            c.pos_x = -468992
            c.pos_y = 8192
            c.pos_z = 442368
            c.state_step = 4
            c.control_state = 0
            c.floatstinger_vocal_volume = 64
        else
            c.pos_x = -464896
            c.pos_y = 32768
            c.pos_z = 442368
            c.rot_y = -1024
            c.anim_flags = c.anim_flags & ~0x2
        end
    elseif step == 1 or step == 2 then
        if step == 2 then
            c.control_state = 0
            c.state_step = 4
        end
        c.anim_flags = c.anim_flags | 0x2
        fly_to(c, c.pos_x, 8192, c.pos_z, -1024)
    end
    c.floatstinger_vocal_volume = min(c.floatstinger_vocal_volume + 1, 64)
end

-- Control 6: down. Once the death cutscene sets flag M4S05_FloatstingerLandingTriggered it lands.
local function control_down(c)
    sh.entity.floatstinger_set_flap_speed(53248)
    c.floatstinger_turn_rate = 0
    c.move_speed = 0
    if c.anim_status >> 1 ~= A.descend and sh.flag_get(FLAG_LANDED) then
        c.anim_status = LAND_IN
        sfx_at(c, "death", 1573, c.pos_x, c.pos_y - 14336, c.pos_z, 128)
    end
end

local CONTROLS = { [2] = control_fight, [3] = control_recoil, [5] = control_descend, [6] = control_down }

local function control_update(c)
    if c.anim_status ~= STING then
        c.attack_window = 0
        clear(c, F_STING_COUNTED)
    end
    local control = CONTROLS[c.control_state]
    if control ~= nil then
        control(c)
    end
end

-- The move of the tick against the walls, kept out of the floor boxes.
local function move_update(c)
    local dx, dz = step_along(q12_mul(dt(), c.move_speed), c.heading_angle)
    local dy = q12_mul(dt(), c.fall_speed)
    local ox, oy, oz = sh.entity.wall_offset(c.slot, dx, dy, dz)
    c.pos_x = c.pos_x + ox
    c.pos_z = c.pos_z + oz
    box_push(c, c.pos_x, c.pos_y, c.pos_z)
    local wx, _, wz = sh.entity.wall_offset(c.slot, push_x, 0, push_z)
    c.pos_x = c.pos_x + wx
    c.pos_z = c.pos_z + wz
    if not box_push(c, c.pos_x, c.pos_y + oy, c.pos_z) then
        c.pos_y = c.pos_y + oy
    end
end

local function vocal_update(c)
    local status = c.anim_status
    if status == STING then
        local k = keyframe(c)
        if k > 0 and k < 4 then
            if not has(c, F_VOCAL) then
                sfx_at(c, "vocal", 1569, c.pos_x, c.pos_y - 16384, c.pos_z, 32)
                set(c, F_VOCAL)
            end
        else
            clear(c, F_VOCAL)
        end
        local difficulty = sh.save.game_difficulty()
        local speed
        if difficulty == DIFFICULTY_EASY then
            speed = 106496
        elseif difficulty == DIFFICULTY_HARD then
            speed = 155648
        else
            speed = 131072
        end
        sh.entity.clip_duration_set(c.slot, status, speed)
    elseif status == HOVER then
        sh.entity.clip_duration_set(c.slot, status, sh.entity.floatstinger_flap_speed())
        local k = keyframe(c)
        if (k > 190 and k < 194) or (k > 206 and k < 210) then
            if not has(c, F_VOCAL) and c.floatstinger_vocal_volume > 0 then
                sfx_at(c, "vocal_loop", 1568, c.pos_x, c.pos_y - 16384, c.pos_z, c.floatstinger_vocal_volume >> 1)
                set(c, F_VOCAL)
            end
        else
            clear(c, F_VOCAL)
        end
    end
end

local function bones_update(c)
    vocal_update(c)
    anim_play(c)
    for i = 1, 15 do
        local w = wing[i]
        local bone = WING_BONES[i]
        if i <= 3 then
            sh.entity.bone_rotate(c.slot, bone, 0, 0, w)
        elseif (i - 1) & 1 ~= 0 then
            sh.entity.bone_rotate(c.slot, bone, w, 0, 0)
        else
            sh.entity.bone_rotate(c.slot, bone, 0, w, 0)
        end
    end
end

-- The collision shape from the head, the tail and the abdomen; at Harry's height it is the part level with
-- him. The sting narrows the body while it strikes.
local function shape_update(c)
    local hx, hy, hz = bone_point(c, 1, 0, 0, 0)
    local tx, ty, tz = bone_point(c, 6, 138, -53, 0)
    local ax, ay, az = bone_point(c, 5, -28, 0, 0)
    local py = player.pos_y
    if py - 5529 < hy then
        c.box_top = ty - c.pos_y
        c.box_height = hy - c.pos_y
        c.cylinder_radius = 1638
        c.cylinder_extent = 1638
        c.box_offset_x = asr(hx + tx, 1) - c.pos_x
        c.cylinder_offset_x = c.box_offset_x
        c.box_offset_z = asr(hz + tz, 1) - c.pos_z
        c.cylinder_offset_z = c.box_offset_z
    else
        c.box_top = hy - c.pos_y
        c.box_height = ay - c.pos_y
        c.cylinder_radius = 2048
        c.cylinder_extent = 2048
        c.box_offset_x = asr(hx + ax, 1) - c.pos_x
        c.cylinder_offset_x = c.box_offset_x
        c.box_offset_z = asr(hz + az, 1) - c.pos_z
        c.cylinder_offset_z = c.box_offset_z
    end
    local upper = asr(hy + ty, 1)
    local lower = asr(hy + ay, 1)
    if upper > py - 5734 then
        c.box_offset_y = upper - c.pos_y
    elseif lower < py - 5734 then
        c.box_offset_y = lower - c.pos_y
    else
        c.box_offset_y = py - s16(c.pos_y + 5734)
    end
    if c.anim_status == STING then
        local k = keyframe(c)
        if k > 15 and k < 27 then
            c.cylinder_radius = q12_mul(c.cylinder_radius, 1228)
        elseif k > 11 and k < 16 then
            c.cylinder_radius = q12_mul(c.cylinder_radius, 4096 - q12_mul(asr(c.anim_time - 49152, 2), 2867))
        elseif k > 26 and k < 31 then
            c.cylinder_radius = q12_mul(c.cylinder_radius, q12_mul(asr(126976 - c.anim_time, 2), 2867) + 1228)
        end
    end
    if player.health <= 0 then
        c.box_bottom = -8192
    else
        c.box_bottom = -5980
    end
end

function update(slot)
    local c = record(slot)
    if c.control_state == 0 then
        init(c)
    end
    if dt() ~= 0 then
        damage_react(c)
        control_update(c)
        move_update(c)
    end
    bones_update(c)
    shape_update(c)
    c.floatstinger_prev_rot_y = c.rot_y
end
