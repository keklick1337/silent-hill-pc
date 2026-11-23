-- Twinfeeler, the larva boss of the mall (map4_s03). It lives under the floor: underground its health is
-- -1 (nothing can hit it) and kept aside. It picks a free floor cell near where Harry is heading (3),
-- bursts out there (5, or 6 spitting acid at him), inches forward (10) until the edge of the arena and
-- dives back in (4). A hit makes it rear up (2) and dive; at 20 health it dies (9): it stands up or
-- stuns, lies still for five seconds, crawls to the wall and dives in for good, and the map's cutscene
-- takes over (12 and 13 are the cutscenes' own bursts, 7 and 8 a standing acid attack).
-- Before the fight the map hands the type its second update (variant 1): the small worms in the grating
-- above the arena, which only crawl and die to any hit.
-- The ground effects (the holes, dust where the body crosses the floor, the acid) are the engine's,
-- reached through sh.entity.twinfeeler_*; only a map whose overlay.json lists the type in
-- "character_effects" draws them (map4_s03).

local A = clips("Twinfeeler", {
    resurface = 1, burrow = 2, walk_forward = 6, death_start = 7, inch_forward = 8, stun_to_idle = 11,
    stand_acid_attack = 12, stand_recoil = 13, stun_start = 16, idle_to_stand = 17, idle = 18,
    stun_idle_end = 20, stun_end = 22, death_resurface = 23,
})

local function S(clip, active)
    return anim_status(clip, active)
end

local IDLE = S(A.idle, false)
local RESURFACE = S(A.resurface, false)
local BURROW = S(A.burrow, false)
local BURROWING = S(A.burrow, true)
local INCH = S(A.inch_forward, false)

-- Control states.
local C_WALK = 1        -- first tick: waits for its files, then starts
local C_RECOIL = 2
local C_SEEK = 3
local C_DIVE = 4
local C_BURST = 5
local C_BURST_SPIT = 6
local C_STAND = 7
local C_SPIT = 8
local C_DEATH = 9
local C_CRAWL = 10
local C_UNDERGROUND = 11
local C_CUTSCENE_BURST = 12
local C_CUTSCENE_DIVE = 13

-- twinfeeler_flags bits.
local F_ATTACK_HIT = 0x1
local F_UNDERGROUND = 0x4

-- Record flags.
local CHARA_PLAYER_SAFE = 0x2   -- Harry cannot be hurt (the boss is dying)
local CHARA_HIT = 0x4
local CHARA_NO_RADIO_STATIC = 0x100

local FLAG_BOSS_DOWN = "M4S03_TwinfeelerDefeated"
local ATTACK_SPIT = sh.entity.attack_number("TwinfeelerStrike")
local BONE_TAIL = 6
local BONE_HEAD = 14

-- Sounds by the map's table index: slot name, game id, volume (Q8).
local SFX = {
    [0] = { "stand_up", 1558, 255 }, { "dig_loop", 1559, 255 }, { "unused_sound", 1560, 255 },
    { "acid_loop", 1561, 255 }, { "crawl_loop", 1567, 255 }, { "resurface", 1566, 255 },
}
local SFX_DIG, SFX_ACID, SFX_CRAWL, SFX_RESURFACE, SFX_STAND = 1, 3, 4, 5, 0

local function sfx(c, i)
    local e = SFX[i]
    sh.entity.sfx_3d(c.slot, sound(c, e[1], e[2]), e[3], 0)
end

local function sfx_stop(c, i)
    local e = SFX[i]
    sh.sfx_stop(sound(c, e[1], e[2]))
end

-- Per clip (status >> 1): bit 0 the clip ignores hits, bit 1 the death skips the stun.
local CLIP_FLAGS = {
    [0] = 0, 7, 7, 3, 3, 3, 3, 3, 3, 0, 0, 3, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 3, 7,
}

-- The arena floor in cells of 2 m (9 by 5 from (113, 135) m): 1 = no room to burst out.
local GRID = {
    [0] = 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
}

-- The three cells (of the 3 by 3 around a cell) tried first for each of the eight directions Harry can face;
-- each row is shuffled in place every time it is used.
local CELL_ORDER = {
    [0] = { [0] = 0, 1, 2 }, { [0] = 1, 2, 5 }, { [0] = 2, 5, 8 }, { [0] = 5, 8, 7 }, { [0] = 8, 7, 6 },
    { [0] = 7, 6, 3 }, { [0] = 6, 3, 0 }, { [0] = 3, 0, 1 },
}

-- The arena box (geometry units): outside it the body is at the edge.
local ARENA_X, ARENA_Z, ARENA_W, ARENA_D = 30080, 35840, 1536, 819

-- Model bone nodes (1-based) by body segment, head first; the burrow clips hide the segments underground.
local SEGMENTS = {
    [0] = { 0x12, 0x13, 0x14, 0x15, 0x16, 0x17 }, { 0x10, 0x11 }, { 0x0F, 0x20, 0x21, 0x22, 0x23 },
    { 0x0D, 0x0E, 0x1C, 0x1D, 0x1E, 0x1F }, { 0x01, 0x18, 0x19, 0x1A, 0x1B }, { 0x02, 0x03, 0x24, 0x25, 0x26, 0x27 },
    { 0x04, 0x28, 0x29, 0x2A, 0x2B }, { 0x05, 0x06, 0x2C, 0x2D, 0x2E, 0x2F }, { 0x07, 0x30, 0x31, 0x32, 0x33 },
    { 0x08, 0x34, 0x35, 0x36, 0x37 }, { 0x09, 0x0A, 0x0B, 0x0C },
}
-- By keyframe: below 32 the first segment still underground (the resurface), from 32 the count of segments
-- already underground (the burrow).
local HIDDEN_FROM = {
    [0] = 1, 2, 3, 3, 4, 4, 5, 5, 6, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11, 11,
}
local HIDDEN_UPTO = {
    [0] = 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 7, 7, 7, 8, 8, 8,
    9, 9, 10, 10, 11, 11, 11,
}

-- Bones whose height against the floor (geometry units) raises dust when it changes side.
local DUST_BONES = { [0] = 0x1E, 0x20, 0x1C, 0x1A, 0x18, 0x16, 0x24, 0x22, 0x28, 0x26, 0x2C, 0x2A, 0x30, 0x2E, 0x34, 0x32 }
local DUST_FLOOR = { [0] = -25, -25, -30, -30, -40, -40, -66, -66, -30, -30, -20, -20, -23, -23, -28, -28 }

-- The bones of the four extra collision cylinders.
local COLLIDER_BONES = { [0] = 11, 2, 4, 6 }

-- ---------------------------------------------------------------------------------------------
-- Before the fight (variant 1): the worms in the grating. Their spawn's state step (19, 20) picks the
-- pose; the tables are read as the map's data lies in memory (the first tick reads with the spawn's step).

local INTRO_Y = {
    [0] = -62, -409, -696, 0, 0, -91, 80, 0, 1877, -1365, 341, 0, 0x616, 0xFF, 0x617, 0xFF, 0x618, 0xFF, 0x619,
    0xFF, 0x61F, 0xFF, 0x61E, 0xFF,
}
local INTRO_ROT_X = { [0] = 0, -91, 80, 0 }
local INTRO_ROT_Y = { [0] = 1877, -1365, 341, 0 }
local INTRO_SCALE = { [0] = 614, 1228, 819, 0 }

local WALK = S(A.walk_forward, true)
local DEATH = S(A.death_start, false)
local DEAD = S(A.stun_idle_end, true)

-- A spawn that did not come from the map table (a console SPAWN has step 0) indexes past the tables: they read as zero there.
local function intro(c)
    c.pos_y = INTRO_Y[c.state_step] or 0
    if c.control_state == 0 then
        c.control_state = C_WALK
        c.anim_status = WALK
        c.anim_keyframe = 163
        c.health = 1
        c.anim_time = 163 * 4096
        c.anim_alpha = 0
        sh.entity.set_collision_state(c.slot, 3)
        c.state_step = c.state_step - 18
        c.rot_x = INTRO_ROT_X[c.state_step] or 0
        c.rot_y = INTRO_ROT_Y[c.state_step] or 0
    end

    local light = c.state_step == 1 and 3072 or 4096
    local t = light - sh.entity.darkness_at(c.pos_x, c.pos_y, c.pos_z) * 16
    if t < 0 then
        t = 0
    elseif t > 4096 then
        t = 4096
    end
    c.aux_timer = t

    -- Any hit kills it.
    if c.health > 0 and c.damage_amount > 0 then
        c.health = 0
        c.anim_status = DEATH
    end

    if c.health > 0 and c.twinfeeler_vocal_timer == 0 then
        local id
        if rng_uint(0, 3) ~= 0 then
            id = sound(c, "vocal", 1553)
        else
            id = sound(c, "vocal_2", 1552)
        end
        sh.entity.sfx_3d(c.slot, id, 128, 0)
        c.twinfeeler_vocal_timer = rng_int(4096, 7371)
    else
        local timer = c.twinfeeler_vocal_timer - sh.game.delta_time()
        if timer < 0 then
            timer = 0
        end
        c.twinfeeler_vocal_timer = timer
    end

    if c.anim_status == DEAD and c.health == 0 and sh.entity.npc_index(c.slot) ~= sh.entity.sys("target_npc") then
        c.health = -1
        sh.entity.set_collision_state(c.slot, 0)
        sh.entity.enemy_defeated(c.slot)
    end

    sh.entity.root_transform(c.slot)
    sh.entity.anim_step(c.slot)

    local scale = INTRO_SCALE[c.state_step] or 0
    sh.entity.bone_scale(c.slot, 0, scale, scale, scale)

    c.box_height = 0
    c.box_bottom = 0
    c.cylinder_offset_z = 0
    c.cylinder_offset_x = 0
    c.cylinder_radius = 0

    -- A point on the neck.
    local x, y, z = sh.entity.bone_point(c.slot, 11, 64, 89, 0)
    c.box_top = q12_mul(s32(y << 4), scale)
    c.box_offset_y = c.box_top + 122
    c.box_offset_x = q12_mul(s32(c.pos_x - s32(x << 4)), scale)
    c.box_offset_z = q12_mul(s32(c.pos_z - s32(z << 4)), scale)
    c.cylinder_extent = 204
end

-- ---------------------------------------------------------------------------------------------
-- The boss.

local function underground(c)
    c.pos_y = 40960
    c.twinfeeler_flags = c.twinfeeler_flags | F_UNDERGROUND
end

-- Health aside while underground.
local function hide(c)
    if c.health >= 0 then
        c.twinfeeler_saved_health = c.health
        c.health = -1
    end
end

local function show(c)
    c.health = c.twinfeeler_saved_health
end

-- Out of the floor: on the ground a metre ahead (or where it is, or the arena's middle).
local function floor_set(c)
    local a = c.rot_y
    local d = q12_mul(sh.math.sin(a), 4096)
    c.pos_y = sh.entity.ground_at(s32(c.pos_x + d), s32(q12_mul(sh.math.cos(a), 4096) + c.pos_z))
    if c.pos_y < -4096 then
        local x, z = c.pos_x, c.pos_z
        c.pos_y = sh.entity.ground_at(x, z)
        if c.pos_y < -4096 then
            c.pos_x = 485376
            c.pos_z = 564838
            c.pos_y = sh.entity.ground_at(x, z)
        end
    end
    c.twinfeeler_flags = c.twinfeeler_flags & ~F_UNDERGROUND
end

local function wall_hit(x, z)
    local dx = s32(x - ARENA_X)
    local dz = s32(z - ARENA_Z)
    return not (dx > 0 and dx < ARENA_W and dz > 0 and dz < ARENA_D)
end

-- The point a metre ahead is outside the arena.
local function edge_ahead(c)
    local x = s32(c.pos_x + sh.math.sin(c.rot_y))
    local z = s32(c.pos_z + sh.math.cos(c.rot_y))
    return wall_hit(asr(x, 4), asr(z, 4))
end

-- The distance the map's cylinder ray reports ahead along `angle` from (x, z) at knee height.
local function ray_ahead(angle, x, z)
    local hit, tx = sh.entity.ray_cylinder(x, -1228, z, sh.math.sin(angle), 0, sh.math.cos(angle))
    if hit ~= 0 then
        -- The map's code takes the low half of the hit point's x for the distance.
        return s16(tx)
    end
    return 0x7FFFFFFF
end

-- Turns the head towards Harry, fast while the clip is young.
local function face_player(c, keyframe)
    local rate = keyframe < 15 and 512 or 170
    local p = record(0)
    local d = angle_signed(sh.math.atan2(s32(p.pos_x - c.pos_x), s32(p.pos_z - c.pos_z)) - c.rot_y)
    if abs(d) > 56 then
        local step = q12_mul(sh.game.delta_time(), rate)
        if d > 0 then
            c.rot_y = c.rot_y + step
        else
            c.rot_y = c.rot_y - step
        end
    end
end

local function cell_free(x, z)
    if x >= 0 and x < 9 and z >= 0 and z < 5 then
        return GRID[x * 5 + z] & 1 == 0
    end
    return false
end

-- Shuffles `cells` (0..n-1, offsets in the 3 by 3 around (x, z)) and returns the first free one packed
-- (x << 8) + z, or 0xFFFF.
local function cell_pick(cells, n, x, z)
    for i = n - 1, 1, -1 do
        local j = ((i + 1) * sh.rng.rand16()) >> 16
        cells[i], cells[j] = cells[j], cells[i]
    end
    for i = 0, n - 1 do
        local t = cells[i]
        local cx = x + (t % 3) - 1
        local cz = z + (t // 3) - 1
        if cell_free(cx, cz) then
            return ((cz & 0xFFFF) + (cx << 8)) & 0xFFFF
        end
    end
    return 0xFFFF
end

-- The cell to burst out of: around where Harry will be in 2.2 s, the cells he faces first.
local function cell_ahead_of_player()
    local p = record(0)
    local reach = q12_mul(p.move_speed, 9011)
    local heading = p.heading_angle
    local ox = q12_mul(sh.math.sin(heading), reach)
    local oz = q12_mul(sh.math.cos(heading), reach)
    local z = s32(oz + p.pos_z)
    local x = s32(p.pos_x + ox)
    local cx = asr(asr(x, 4) - 0x7100 >= 0 and asr(x, 4) - 0x7100 or asr(x, 4) - 0x6F01, 9)
    if cx < 0 then
        cx = 0
    end
    if cx >= 9 then
        cx = 8
    end
    local cz = idiv(asr(z, 4) - 0x8700, 0x200)
    if cz < 0 then
        cz = 0
    end
    if cz >= 5 then
        cz = 4
    end
    local facing = asr((p.rot_y - 256) & 0xFFF, 9)
    local cell = cell_pick(CELL_ORDER[7 - facing], 3, cx, cz)
    if cell == 0xFFFF then
        cell = cell_pick({ [0] = 0, 1, 2, 3, 5, 6, 7, 8 }, 8, cx, cz)
    end
    return cell
end

-- Moves under the cell (a random point in it, a metre short towards Harry); the heading to Harry.
local function go_under(c, cell)
    local cx = (cell >> 8) & 0xFF
    local cz = cell & 0xFF
    local x = s32(((cx << 9) + asr(s32(sh.rng.rand16() << 8), 15) + 0x7200) * 16)
    local z = s32(((cz << 9) + asr(s32(sh.rng.rand16() << 8), 15) + 0x8800) * 16)
    local p = record(0)
    local angle = sh.math.atan2(s32(p.pos_x - x), s32(p.pos_z - z))
    local s = sh.math.sin(angle)
    local zz = s32(z - sh.math.cos(angle))
    c.pos_x = s32(x - s)
    c.pos_z = zz
    return angle
end

local function head_point(c)
    local x, y, z = sh.entity.bone_point(c.slot, BONE_HEAD, 0, 0, 0)
    return s32(x << 4), s32(y << 4), s32(z << 4)
end

local function control_underground(c)
    if c.state_step == 0 then
        hide(c)
        c.move_speed = 0
        c.state_step = c.state_step + 1
    end
    underground(c)
end

-- The opening cutscene's burst in the middle of the arena.
local function control_cutscene_burst(c)
    if c.state_step == 0 then
        c.rot_y = 0
        c.heading_angle = 0
        c.pos_x = 485376
        c.pos_z = 564838
        sh.entity.twinfeeler_hole(c.slot, 0)
        c.twinfeeler_dig_timer = 2867
        c.state_step = c.state_step + 1
    end
    local step = c.state_step
    if step == 1 then
        if c.twinfeeler_dig_timer < 0 then
            floor_set(c)
            c.move_speed = 7372
            c.anim_status = RESURFACE
            sfx(c, SFX_RESURFACE)
            c.state_step = c.state_step + 1
        end
    end
    if step == 1 or step == 2 then
        if c.anim_status == IDLE then
            c.twinfeeler_dig_timer = 4096
            c.move_speed = 4915
            c.anim_status = INCH
            sfx(c, SFX_CRAWL)
            c.state_step = c.state_step + 1
        end
    elseif step == 3 then
        if c.twinfeeler_dig_timer < 0 then
            c.move_speed = 3636
            sfx_stop(c, SFX_CRAWL)
            sfx(c, SFX_DIG)
            c.anim_status = BURROW
            sh.entity.twinfeeler_hole(c.slot, 1)
            c.state_step = c.state_step + 1
        end
    elseif step == 4 then
        if c.anim_status == IDLE then
            c.control_state = C_UNDERGROUND
            c.state_step = 0
            sfx_stop(c, SFX_DIG)
        end
    end
    c.twinfeeler_dig_timer = c.twinfeeler_dig_timer - sh.game.delta_time()
end

-- The closing cutscene: out at the wall and into it.
local function control_cutscene_dive(c)
    if c.state_step == 0 then
        c.pos_x = 536576
        c.pos_z = 573440
        c.rot_y = 1024
        c.heading_angle = 1024
        sh.entity.twinfeeler_hole(c.slot, 0)
        c.twinfeeler_dig_timer = 2867
        sh.entity.enemy_defeated(c.slot)
        c.state_step = c.state_step + 1
    end
    local step = c.state_step
    if step == 1 then
        if c.twinfeeler_dig_timer < 0 then
            floor_set(c)
            c.anim_status = S(A.death_resurface, false)
            c.move_speed = 5529
            sfx(c, SFX_RESURFACE)
            c.state_step = c.state_step + 1
        end
    elseif step == 2 then
        if c.anim_status == IDLE then
            c.move_speed = 3636
            sfx(c, SFX_DIG)
            c.anim_status = BURROW
            c.state_step = c.state_step + 1
        end
    elseif step == 3 then
        if c.anim_status == IDLE then
            sfx_stop(c, SFX_DIG)
            c.control_state = C_UNDERGROUND
            c.state_step = 1
        end
    end
    c.twinfeeler_dig_timer = c.twinfeeler_dig_timer - sh.game.delta_time()
end

local function control_burst(c)
    if c.state_step == 0 then
        c.twinfeeler_dig_timer = 2867
        sh.entity.twinfeeler_hole(c.slot, 0)
        c.state_step = c.state_step + 1
    end
    local step = c.state_step
    if step == 1 then
        if c.twinfeeler_dig_timer < 0 then
            show(c)
            floor_set(c)
            c.move_speed = 7372
            c.anim_status = RESURFACE
            sfx(c, SFX_RESURFACE)
            c.state_step = c.state_step + 1
        end
    elseif step == 2 then
        face_player(c, asr(c.anim_time, 12))
    end
    c.twinfeeler_dig_timer = c.twinfeeler_dig_timer - sh.game.delta_time()
    if c.anim_status == IDLE then
        ray_ahead(c.rot_y, c.pos_x, c.pos_z)
        c.control_state = C_CRAWL
        c.state_step = 0
    end
end

local function control_burst_spit(c)
    if c.state_step == 0 then
        c.twinfeeler_dig_timer = 2867
        sh.entity.twinfeeler_hole(c.slot, 0)
        c.state_step = c.state_step + 1
    end
    local keyframe = asr(c.anim_time, 12)
    local step = c.state_step
    if step == 1 then
        if c.twinfeeler_dig_timer < 0 then
            show(c)
            floor_set(c)
            c.move_speed = 7372
            c.anim_status = RESURFACE
            sfx(c, SFX_RESURFACE)
            c.state_step = c.state_step + 1
        end
    elseif step == 2 or step == 3 then
        if step == 2 and keyframe > 5 then
            sfx(c, SFX_ACID)
            local x, y, z = head_point(c)
            local p = record(0)
            sh.entity.twinfeeler_acid_throw(x, y, z, p.pos_x, s32(p.pos_y - 4096), p.pos_z)
            c.state_step = c.state_step + 1
        end
        face_player(c, keyframe)
    end
    if c.anim_status == IDLE then
        c.control_state = C_CRAWL
        c.state_step = 0
    end
    c.twinfeeler_dig_timer = c.twinfeeler_dig_timer - sh.game.delta_time()
end

local function control_crawl(c)
    if c.state_step == 0 then
        sfx_stop(c, SFX_ACID)
        c.move_speed = 4915
        c.anim_status = INCH
        sfx(c, SFX_CRAWL)
        c.state_step = c.state_step + 1
    elseif edge_ahead(c) then
        c.control_state = C_DIVE
        c.state_step = 0
    end
end

local function control_stand(c)
    if c.state_step == 0 then
        c.anim_status = S(A.idle_to_stand, false)
        c.move_speed = 0
        c.state_step = c.state_step + 1
    end
    if c.anim_status == IDLE then
        c.control_state = C_SPIT
        c.state_step = 0
    end
end

local function control_spit(c)
    if c.state_step == 0 then
        sfx(c, SFX_ACID)
        c.anim_status = S(A.stand_acid_attack, false)
        local x, y, z = head_point(c)
        local p = record(0)
        sh.entity.twinfeeler_acid_throw(x, y, z, p.pos_x, p.pos_y, p.pos_z)
        c.move_speed = 0
        c.state_step = c.state_step + 1
    end
    if c.anim_status == IDLE then
        c.control_state = C_CRAWL
        c.state_step = 0
    end
end

local function control_dive(c)
    if c.state_step == 0 then
        sh.entity.twinfeeler_hole(c.slot, 1)
        c.twinfeeler_dig_timer = 0
        sfx(c, SFX_DIG)
        c.anim_status = BURROW
        c.move_speed = 3636
        c.state_step = c.state_step + 1
    end
    if c.state_step == 1 and c.twinfeeler_dig_timer >= 4096 then
        hide(c)
        c.state_step = c.state_step + 1
    end
    if c.anim_status == IDLE then
        sfx_stop(c, SFX_DIG)
        c.control_state = C_SEEK
        c.state_step = 0
    end
    c.twinfeeler_dig_timer = c.twinfeeler_dig_timer + sh.game.delta_time()
end

local function control_seek(c)
    if c.state_step == 0 then
        c.twinfeeler_dig_timer = 8192
        c.anim_status = INCH
        c.state_step = c.state_step + 1
    end
    underground(c)
    if c.twinfeeler_dig_timer < 0 then
        local angle = s16(go_under(c, cell_ahead_of_player()))
        c.heading_angle = angle
        c.rot_y = angle
        local p = record(0)
        local d = abs(angle_signed(sh.math.atan2(s32(p.pos_x - c.pos_x), s32(p.pos_z - c.pos_z)) - angle))
        if d >= 512 then
            c.control_state = C_BURST
        else
            c.control_state = C_BURST_SPIT
        end
        c.state_step = 0
    end
    c.twinfeeler_dig_timer = c.twinfeeler_dig_timer - sh.game.delta_time()
end

local function control_recoil(c)
    if c.state_step == 0 then
        c.anim_status = S(A.stand_recoil, false)
        c.state_step = c.state_step + 1
    end
    if c.anim_status == IDLE then
        c.control_state = C_DIVE
        c.state_step = 0
        c.move_speed = 0
    end
end

local function control_death(c)
    local STUN = S(A.stun_start, false)
    if c.state_step == 0 then
        sfx_stop(c, SFX_DIG)
        if c.twinfeeler_stun == 0 then
            c.anim_status = S(A.idle_to_stand, false)
            c.state_step = 1
        else
            sfx(c, SFX_STAND)
            c.anim_status = STUN
            c.state_step = 2
        end
    end
    local step = c.state_step
    if step == 1 then
        if c.anim_status == IDLE then
            sfx(c, SFX_STAND)
            c.anim_status = STUN
            c.state_step = c.state_step + 1
        end
    elseif step == 2 then
        if c.anim_status == S(A.stun_end, false) then
            c.twinfeeler_dig_timer = 20480
            c.state_step = c.state_step + 1
        end
    elseif step == 3 then
        if c.twinfeeler_dig_timer < 0 then
            c.anim_status = S(A.stun_to_idle, false)
            c.state_step = c.state_step + 1
        end
    elseif step == 4 then
        if c.anim_status == IDLE then
            c.move_speed = 4915
            c.anim_status = INCH
            c.state_step = c.state_step + 1
        end
    elseif step == 5 then
        local ahead = ray_ahead(c.rot_y, c.pos_x, c.pos_z)
        if edge_ahead(c) or ahead < 8192 then
            sh.entity.twinfeeler_hole(c.slot, 1)
            sfx(c, SFX_DIG)
            c.anim_status = BURROW
            c.move_speed = 3636
            c.state_step = c.state_step + 1
        end
    elseif step == 6 then
        if c.anim_status == IDLE then
            sfx_stop(c, SFX_DIG)
            sh.flag_set(FLAG_BOSS_DOWN, true)
            -- The map's code stores a clip status here; it is the underground state's number.
            c.control_state = S(5, true)
            c.state_step = 0
        end
    end
    if c.state_step < 5 then
        c.move_speed = approach(c.move_speed, 0, q12_mul(sh.game.delta_time(), 4096))
    end
    c.twinfeeler_dig_timer = c.twinfeeler_dig_timer - sh.game.delta_time()
end

local CONTROL = {
    [C_UNDERGROUND] = control_underground,
    [C_CUTSCENE_BURST] = control_cutscene_burst,
    [C_CUTSCENE_DIVE] = control_cutscene_dive,
    [C_BURST] = control_burst,
    [C_BURST_SPIT] = control_burst_spit,
    [C_DIVE] = control_dive,
    [C_SEEK] = control_seek,
    [C_STAND] = control_stand,
    [C_SPIT] = control_spit,
    [C_RECOIL] = control_recoil,
    [C_DEATH] = control_death,
    [C_CRAWL] = control_crawl,
}

local function control(c)
    local f = CONTROL[c.control_state]
    if f ~= nil then
        f(c)
    end
end

local function init(c)
    if sh.entity.file_queue_ready() == 0 then
        return false
    end
    local x, z = c.pos_x, c.pos_z
    c.health = sh.chara_param_q12(c.chara_id, "health", 12288000)
    c.twinfeeler_saved_health = -1
    c.cylinder_radius = 1228
    c.anim_alpha = 0
    c.move_speed = 0
    c.heading_angle = c.rot_y
    sh.entity.set_collision_state(c.slot, 4)
    c.twinfeeler_push_x = 0
    c.twinfeeler_push_y = 0
    c.twinfeeler_push_z = 0
    c.twinfeeler_damage_taken = 0
    c.twinfeeler_unused = 0
    c.twinfeeler_flags = 0
    c.twinfeeler_spawn_x = x
    c.twinfeeler_spawn_z = z
    underground(c)
    c.cylinder_offset_x = 0
    c.cylinder_offset_z = 0
    c.flags = c.flags | CHARA_HIT
    if c.state_step ~= 0 then
        c.control_state = c.state_step
        c.state_step = 0
    else
        c.control_state = C_UNDERGROUND
        c.state_step = 0
    end
    c.twinfeeler_ground_bits = 0xFFFF
    hide(c)
    anim_set(c, INCH, 258)
    sh.entity.clip_table_attach(c.slot, 0)
    damage_clear(c)
    sh.entity.twinfeeler_effects_reset(c.slot)
    c.flags = c.flags | CHARA_NO_RADIO_STATIC
    return true
end

-- A hit while above ground: recoil, or the death at 20 health.
local function damage_take(c)
    local amount = c.damage_amount
    if amount > 0 and c.health >= 0 then
        c.twinfeeler_push_x = c.damage_x
        c.twinfeeler_push_y = c.damage_y
        c.twinfeeler_push_z = c.damage_z
        c.twinfeeler_push_amount = amount
        c.twinfeeler_damage_taken = s32(c.twinfeeler_damage_taken + amount)
        c.health = max(s32(c.health - amount), 0)
        local clip_flags = CLIP_FLAGS[c.anim_status >> 1]
        if c.health > 81920 then
            if c.twinfeeler_damage_taken > 0 and c.control_state ~= C_RECOIL and clip_flags & 1 == 0 then
                c.twinfeeler_damage_taken = 0
                c.control_state = C_RECOIL
                c.state_step = 0
            end
        elseif sh.entity.player_damage_disable(0) == 0 and c.control_state ~= C_DEATH then
            c.twinfeeler_stun = clip_flags & 2 == 0 and 1 or 0
            c.health = -1
            c.control_state = C_DEATH
            c.state_step = 0
            c.flags = c.flags | CHARA_PLAYER_SAFE
        end
    end
    damage_clear(c)
end

-- The step of the control state plus the decaying push of the last hit.
local function move(c)
    local px, py, pz = c.twinfeeler_push_x, c.twinfeeler_push_y, c.twinfeeler_push_z
    local speed = c.move_speed
    c.twinfeeler_walk_speed = speed
    local sx = sh.math.sin(c.rot_y)
    local sz = sh.math.cos(c.rot_y)
    local vx = s32(q12_mul(px, 14336) + q12_mul(speed, sx))
    local vz = s32(q12_mul(pz, 14336) + q12_mul(speed, sz))
    c.move_speed = sh.entity.sqrt_q12(s32(q12_mul(vx, vx) + q12_mul(vz, vz)))
    c.heading_angle = sh.math.atan2(vx, vz)
    c.fall_speed = s32(c.fall_speed + sh.game.gravity_speed())
    if c.twinfeeler_flags & F_UNDERGROUND == 0 then
        sh.entity.movement_update(c.slot)
    end
    c.move_speed = c.twinfeeler_walk_speed
    local vblanks = sh.game.vblanks()
    local function decay(v)
        local r = sh.entity.sqrt_q12(asr(q12_mul(v, v), vblanks))
        if v <= 0 then
            r = -r
        end
        return r
    end
    c.twinfeeler_push_x = decay(c.twinfeeler_push_x)
    c.twinfeeler_push_y = decay(c.twinfeeler_push_y)
    c.twinfeeler_push_z = decay(c.twinfeeler_push_z)
    c.rot_y = angle_signed(c.rot_y)
end

local function anim_update(c)
    sh.entity.root_transform(c.slot)
    if c.anim_status ~= 0 then
        sh.entity.anim_step(c.slot)
    end
end

-- The cylinder offset follows the head, at most 1/32 m per tick when it is off.
local function cylinder_follow(c, x, z)
    local ex = c.cylinder_offset_x - x
    local ez = c.cylinder_offset_z - z
    local sq = s32(q12_mul(ex, ex) + q12_mul(ez, ez))
    if sq > 4 then
        local root = sh.entity.sqrt_q12(sq)
        local scale = idiv(s32((root - 128) * 4096), root)
        local sx = s32(scale * ex)
        if sx < 0 then
            sx = sx + 4095
        end
        ex = asr(sx, 12)
        local sz = s32(scale * ez)
        if sz < 0 then
            sz = sz + 4095
        end
        ez = asr(sz, 12)
        x = x + ex
        z = z + ez
    end
    c.cylinder_offset_x = x
    c.cylinder_offset_z = z
end

-- The collision box: around the head (the tail while burrowing) when the model is shown, else a metre
-- above the floor point.
local function shape_update(c)
    if c.anim_flags & 2 == 0 then
        local y = c.pos_y
        c.box_bottom = y
        c.box_height = y
        c.box_top = y - 4096
        c.box_offset_y = y - 2048
        return
    end
    local bone = c.anim_status == BURROWING and BONE_TAIL or BONE_HEAD
    local bx, by, bz = sh.entity.bone_point(c.slot, bone, 0, 0, 0)
    local y = c.pos_y
    local ox = s32(s32(bx << 4) - c.pos_x)
    local oz = s32(s32(bz << 4) - c.pos_z)
    local base = s32(s32(by << 4) - y)
    local top = base - 1024
    c.box_top = top
    if top >= y then
        c.box_bottom = y
    else
        c.box_bottom = top
    end
    c.box_height = base + 1024
    c.cylinder_radius = 2048
    c.box_offset_y = base
    c.cylinder_extent = 1638
    cylinder_follow(c, ox, oz)
    c.box_offset_x = c.cylinder_offset_x
    c.box_offset_z = c.cylinder_offset_z
end

-- The head is a weapon while it bursts out and spits.
local function attack(c)
    local s = c.control_state
    if s ~= C_BURST and s ~= C_BURST_SPIT and s ~= C_SPIT then
        return
    end
    local bx, by, bz = sh.entity.bone_point(c.slot, BONE_HEAD, 0, 0, 0)
    local y = s32(by << 4)
    c.box_extent_x = c.pos_y - y
    if sh.entity.attack(c.slot, 1, ATTACK_SPIT, s32(bx << 4), y, s32(bz << 4), 0, c.rot_y, 1024) ~= -1 then
        c.twinfeeler_flags = c.twinfeeler_flags | F_ATTACK_HIT
    end
end

-- Dust where a body bone crosses the floor, and the ground effects' tick.
local function ground_effects(c)
    if sh.game.delta_time() ~= 0 then
        local above = ((c.twinfeeler_flags >> 2) ~ 1) & 1
        local bits = c.twinfeeler_ground_bits
        for i = 0, 15 do
            local bx, by, bz = sh.entity.bone_point(c.slot, DUST_BONES[i], 0, 0, 0)
            local was = (bits >> i) & 1
            local now = DUST_FLOOR[i] < by and 1 or 0
            if now ~= 0 then
                bits = bits | (1 << i)
            else
                bits = bits & ~(1 << i)
            end
            c.twinfeeler_ground_bits = bits
            if was ~ now ~= 0 then
                local x, z = s32(bx << 4), s32(bz << 4)
                local kind = (i >> 2) + (i & 1)
                sh.entity.twinfeeler_dust(x, z, kind)
                if wall_hit(bx, bz) and above ~= 0 then
                    sh.entity.twinfeeler_dust_trail(x, z, kind)
                end
            end
        end
    end
    sh.entity.twinfeeler_effects_draw(c.slot)
end

-- The body segments under the floor are not drawn.
local function segments_hide(c)
    sh.entity.model_bones_show(c.slot)
    local keyframe = asr(c.anim_time, 12) & 0xFFFFFFFF
    local from, upto
    if keyframe < 32 then
        from, upto = HIDDEN_FROM[keyframe], 10
    elseif keyframe - 32 < 41 then
        from, upto = 0, HIDDEN_UPTO[keyframe - 32] - 1
    else
        return
    end
    for i = from, upto do
        for _, node in ipairs(SEGMENTS[i]) do
            sh.entity.model_bone_hide(c.slot, node - 1)
        end
    end
end

-- Four cylinders along the body push Harry away.
local function colliders(c)
    sh.entity.colliders_attach(c.slot, 4)
    for i = 0, 3 do
        local bx, by, bz = sh.entity.bone_point(c.slot, COLLIDER_BONES[i], 0, 0, 0)
        local y = c.pos_y
        local top = s32(by << 4)
        -- The map's code sets the radius to 0.45 m, then the same field to 3 (meant for the state).
        sh.entity.collider_set(c.slot, i, s32(bx << 4), top, s32(bz << 4), y, top - 1228, 3)
    end
end

local function boss(c)
    if c.control_state == 0 then
        sh.entity.twinfeeler_textures_load()
        c.control_state = C_WALK
        c.state_step = 0
    end
    if c.control_state == C_WALK and not init(c) then
        return
    end
    if sh.game.delta_time() ~= 0 then
        damage_take(c)
        control(c)
        move(c)
        if c.state_step == 0 then
            control(c)
        end
        anim_update(c)
        shape_update(c)
        attack(c)
        ground_effects(c)
        segments_hide(c)
        colliders(c)
        sh.entity.player_damage_disable(c.flags & CHARA_PLAYER_SAFE ~= 0 and 1 or 0)
        if c.control_state ~= C_CRAWL then
            sfx_stop(c, SFX_CRAWL)
        end
    else
        anim_update(c)
        ground_effects(c)
    end
end

function update(slot)
    local c = record(slot)
    if sh.entity.update_variant() == 1 then
        intro(c)
    else
        boss(c)
    end
end
