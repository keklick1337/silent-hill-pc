-- Monster Cybil: Cybil possessed by the parasite, the boss of the amusement park carousel (map6_s04).
--
-- She is posed in two halves over one skeleton: the record's model plays the legs and a second model
-- (sh.entity.monster_cybil_upper) the torso, arms and head. Each half runs its own state machine:
-- monster_cybil_legs_state and monster_cybil_arms_state. npc_control_state is the phase of the fight:
-- 0 she keeps her distance and shoots, 1 (after the map's cutscene, flag M6S04_CybilBossPhaseChangeCutsceneDone) she closes in and grabs or
-- strikes, 3.. the cutscene poses the map hands her (4 the cutscene between the phases, 5..12 the death
-- and the red liquid scene).
--
-- The fight state lives in the first NPC's record (the boss is always NPC 1; a copy spawned elsewhere
-- still reads and writes that record's fight fields, as the game does).

local CHARA = 24 -- Chara_MonsterCybil

-- Bones the two halves play on (Harry's skeleton layout: 0..10 upper body, 11..17 legs).
local UPPER_BONES = 0x7FF
local LOWER_BONES = 0x3F800
local AIM_BONE = 10 -- the gun hand
local ARM_BONE = 8 -- the upper arm the aim pitch turns
local HIPS_BONE = 1

-- The carousel's centre (20, 100); the points below are relative to it.
local CENTRE_X = 81920
local CENTRE_Z = 409600

-- Walk cycle speeds (keyframes per second, Q12) by the clip's first keyframe: while she wanders off and
-- while she fights.
local WALK_DURATION_WANDER = { 73728, 73728, 73728, 73728, 73728, 65536, 65536, 69632, 69632, 73728, 73728,
    73728, 73728, 73728, 73728, 73728, 65536, 65536, 69632, 69632, 73728, 73728, 73728, 73728 }
local WALK_DURATION_FIGHT = { 114688, 114688, 114688, 114688, 114688, 98304, 98304, 106496, 106496, 114688,
    114688, 114688, 114688, 114688, 114688, 114688, 98304, 98304, 106496, 106496, 114688, 114688, 114688, 114688 }
-- Step speed (Q12 per second) on each keyframe of the walk cycle: wandering off and fighting.
local STEP_WANDER = { 3276, 2867, 2457, 2048, 2048, 2867, 2867, 2867, 2867, 2867, 2867, 2867, 2457, 2457, 2048,
    2048, 3276, 2867, 2867, 3276, 3276, 3276, 3276, 3276 }
local STEP_FIGHT = { 4915, 4096, 3276, 2457, 2457, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 3276, 3276, 2457,
    2457, 4915, 4096, 4096, 4915, 4915, 4915, 4915, 4915 }
-- While she holds Harry her clip follows his neck-grab clip: his clip's first keyframe (the map's row of
-- reaction 125 active, 923 on map6_s04; another map's when she was spawned there) is her
-- keyframe 91.
local GRAB_NECK = 251
local function hold_offset()
    local row, first = sh.entity.harry_reaction_row(GRAB_NECK)
    if row ~= nil and row >= 0 then
        return first - 91
    end
    return 832
end

-- Hip twist while she holds Harry, by keyframe of the grab clip (91..101).
local GRAB_TWIST = { 0, -128, -160, -64, -24, -24, -24, -64, -48, 0, 0 }
-- The points around the carousel she must keep away from to wander off (relative to the centre).
local RING_X = { 16384, 26624, 10240, -16384, -26624, -10240, 3276, 12288, -3276, -12288 }
local RING_Z = { 21299, -3481, -24780, -21299, 3481, 24780, 12492, -3276, -12492, 3276 }
-- The horses she drops from (relative to the centre) and her heading there.
local AMBUSH_HEADING = { 427, 1109, 1792, 2475, 3157, 3840, 171, 1195, 2219, 3243 }
local AMBUSH_X = { 15468, 25178, 9715, -15469, -25179, -9716, 3717, 13846, -3717, -13846 }
local AMBUSH_Z = { 20137, -3305, -23461, -20138, 3304, 23460, 13846, -3717, -13846, 3717 }
-- Where she reappears out of sight: the outer, middle and inner ring (relative to the centre).
local REPOSITION = {
    { { 0, 28672 }, { 16384, 24576 }, { 24576, 16384 }, { 28672, 2048 }, { 24576, -16384 }, { 16384, -24576 },
      { 0, -28672 }, { -16384, -24576 }, { -24576, -16384 }, { -28672, -2048 }, { -24576, 16384 },
      { -16384, 24576 } },
    { { 0, 16384 }, { 16384, 16384 }, { 16384, 0 }, { 16384, -16384 }, { 0, -16384 }, { -16384, -16384 },
      { -16384, 0 }, { -16384, 16384 } },
    { { 0, 10240 }, { 10240, 0 }, { 0, -10240 }, { -10240, 0 } },
}

-- Rows of the attack table: her shot, the strike, the rifle strike, the grab.
local ATTACK_SHOT = sh.entity.attack_number("CybilHandgun")
local ATTACK_STRIKE = sh.entity.attack_number("MonsterCybilStrike")
local ATTACK_RIFLE_STRIKE = sh.entity.attack_number("MonsterCybilRifleStrike")
local ATTACK_GRAB = sh.entity.attack_number("MonsterCybilNeckGrab")

local LATCH = "dahlia_effect_spawned" -- the gun sound latch the map overlay keeps

-- Harry's animation states the grab and the strike look at.
local function player_state()
    return sh.entity.player_state()
end

-- Module state (one boss): the aim point at the gun hand (Q12), the random aim and burst delays, the
-- keyframe of the previous tick of the cutscene shot.
local aim_x, aim_y, aim_z = 0, 0, 0
local aim_delay = 0
local burst_delay = 0
local shot_prev_keyframe = -1

-- The upper body model as fields.
local upper = setmetatable({}, {
    __index = function(_, k)
        return sh.entity.monster_cybil_upper(k)
    end,
    __newindex = function(_, k, v)
        sh.entity.set_monster_cybil_upper(k, v)
    end,
})

-- Map data (chara/fallbacks.lua). On map6_s04 the fight lives in the first NPC's record and the map's scenes start
-- it, turn phase one into phase two and end it, by story flags. An engine spawn (console, debug menu, create) fights
-- from its own record on any map: it starts shooting at once, the phase change goes straight to phase two without
-- the scene, and at no health she falls and stays down; it writes no flag of map6_s04.
fallbacks = {
    fight_record = function(c)
        return c
    end,
    fight_start = function(_, p)
        p.npc_control_state = 0
        p.monster_cybil_legs_state = 1
        p.monster_cybil_arms_state = 1
    end,
    phase_change = function(c, p)
        p.npc_control_state = 1
        p.monster_cybil_second_phase = 1
        p.monster_cybil_legs_state = 1
        p.monster_cybil_arms_state = 1
        c.state_step = 0
        upper.state_step = 0
    end,
    defeated = function(c, p)
        if p.npc_control_state == 5 then
            return
        end
        p.npc_control_state = 5
        p.npc_move_speed = 0
        c.state_step = 0
        upper.state_step = 0
        sh.entity.set_collision_state(c.slot, 0)
    end,
}

local function clip_start(c, status)
    return sh.entity.clip_start(c.slot, status)
end

local function clip_end(c, status)
    return sh.entity.clip_end(c.slot, status)
end

local function upper_status_set(status)
    if upper.state_step == 0 then
        upper.anim_status = status
        upper.state_step = upper.state_step + 1
    end
end

-- The clip `status` from its first keyframe, once per state (state_step 0).
local function record_keyframe_set(c, status)
    if c.state_step == 0 then
        c.anim_status = status
        c.state_step = c.state_step + 1
        local start = clip_start(c, status)
        c.anim_time = start * 4096
        c.anim_keyframe = start
    end
end

local function upper_keyframe_set(c, status)
    if upper.state_step == 0 then
        upper.anim_status = status
        upper.state_step = upper.state_step + 1
        local start = clip_start(c, status)
        upper.anim_time = start * 4096
        upper.anim_keyframe = start
    end
end

local function held_item(variant)
    sh.held_item_attach(CHARA, variant)
end

local function sign16(x)
    return s16(x)
end

-- The shortest turn (an angle in [-2048, 2048)) from one heading to another.
local function shortest_angle(from, to)
    from = s16(from)
    to = s16(to)
    local r
    if to > from then
        if to - from < 2048 then
            r = to - from
        else
            r = (to - 4096) - from
        end
    elseif from - to < 2048 then
        r = to - from
    else
        r = to - (from - 4096)
    end
    return s16(r)
end

-- Ground distance (Q12) with the components in Q8.
local function distance_q8(dx, dz)
    local x = asr(s32(dx), 4)
    local z = asr(s32(dz), 4)
    return s32(sh.math.sqrt(s32(s32(x * x) + s32(z * z))) << 4)
end

local function abs16(x)
    return x < 0 and -x or x
end

-- She is on screen at this point (a body-sized box around it).
local function on_screen(x, y, z)
    return sh.entity.aabb_visible(s32(x - 2048), s32(x + 2048), s32(y - 6963), s32(y + 409), s32(z - 2048),
        s32(z + 2048)) ~= 0
end

-- Turns her towards Harry: straight onto him within 11.3 degrees, else by 11.3 degrees per 30 fps tick.
local function face_player(c, pl)
    local to = angle_abs(sh.math.atan2(asr(s32(pl.pos_x - c.pos_x), 4), asr(s32(pl.pos_z - c.pos_z), 4)))
    local turn = shortest_angle(c.rot_y, to)
    if abs16(turn) < 128 then
        c.rot_y = to
    elseif turn > 0 then
        c.rot_y = c.rot_y + timestep_30fps(128)
    else
        c.rot_y = c.rot_y - timestep_30fps(128)
    end
end

-- The horse nearest to Harry that is off screen (index into the ambush points), -1 when none.
local function ambush_point(pl)
    local nearest = 81920
    local found = -1
    for i = 1, 10 do
        local x = AMBUSH_X[i] + CENTRE_X
        local z = AMBUSH_Z[i] + CENTRE_Z
        if not on_screen(x, -2048, z) then
            local d = distance_q8(pl.pos_x - x, pl.pos_z - z)
            if d < nearest then
                nearest = d
                found = i - 1
            end
        end
    end
    return found
end

-- Out of sight she jumps to a point of the ring that matches Harry's distance from the centre. The
-- nearest-point test never lowers its bound, so the last point off screen is taken, and the distance
-- compared is the last one measured.
local function reposition(c, p, pl)
    local from_centre = distance_q8(pl.pos_x - CENTRE_X, pl.pos_z - CENTRE_Z)
    local ring
    if from_centre > 26624 then
        ring = 0
    elseif from_centre > 13107 then
        ring = 1
    else
        ring = 2
    end
    local bound = 81920
    local dist = 81920
    local found = -1
    local limit = p.npc_distance_to_player
    local points = REPOSITION[ring + 1]
    for i = 0, (3 - ring) * 4 - 1 do
        local x = points[i + 1][1] + CENTRE_X
        local z = points[i + 1][2] + CENTRE_Z
        if not on_screen(x, 0, z) then
            dist = distance_q8(pl.pos_x - x, pl.pos_z - z)
            if dist < bound then
                found = i
            end
        end
    end
    if found == -1 then
        return 0
    end
    if dist < limit then
        c.pos_x = points[found + 1][1] + CENTRE_X
        c.pos_z = points[found + 1][2] + CENTRE_Z
        face_player(c, pl)
        return 1
    end
    return 2
end

-- Harry is close and she is nearer to a ring point than to him: time to wander off.
local function near_ring_point(c, p)
    if p.npc_distance_to_player > 8192 then
        return false
    end
    local nearest = 24576
    for i = 1, 10 do
        local px = c.pos_x - CENTRE_X
        local pz = c.pos_z - CENTRE_Z
        local d = distance_q8(RING_X[i] - px, RING_Z[i] - pz)
        if nearest > d then
            nearest = d
        end
    end
    return nearest < p.npc_distance_to_player
end

-- The line of her shot is clear of characters but hits the world.
local function shot_blocked(c, p)
    local reach = sh.entity.weapon_attack_reach(ATTACK_SHOT)
    local pitch = p.monster_cybil_aim_pitch
    local dir_y = q12_mul32(reach, sh.math.cos(pitch))
    local dir_x = q12_mul32(q12_mul32(reach, sh.math.sin(pitch)), sh.math.sin(c.rot_y))
    local dir_z = q12_mul32(q12_mul32(reach, sh.math.sin(pitch)), sh.math.cos(c.rot_y))
    local hit, chara = sh.entity.ray_trace_chara(c.slot, aim_x, aim_y, aim_z, dir_x, dir_y, dir_z)
    return hit ~= 0 and chara == 0
end

local function aim_point_update(c)
    local x, y, z = sh.entity.bone_world_translation(c.slot, AIM_BONE)
    aim_x = s32(x << 4)
    aim_y = s32(y << 4)
    aim_z = s32(z << 4)
end

-- Something in front: she turns away (evade 2 right, 3 left) until she has turned 135 degrees.
local function obstacle_turn(c, p)
    local evade = p.monster_cybil_evade
    if evade == 2 then
        p.monster_cybil_turn_timer = p.monster_cybil_turn_timer + timestep_30fps(32)
        local t = p.monster_cybil_turn_timer
        p.monster_cybil_turn_timer = t < 0 and 0 or (t > 1536 and 1536 or t)
        local rot = c.rot_y
        if p.monster_cybil_turn_timer < 1024 then
            rot = rot + timestep_30fps(32)
        end
        c.rot_y = rot
        if p.monster_cybil_turn_timer == 1536 then
            p.monster_cybil_turn_timer = 0
            p.monster_cybil_evade = 0
        end
    elseif evade == 3 then
        p.monster_cybil_turn_timer = p.monster_cybil_turn_timer - timestep_30fps(32)
        local t = p.monster_cybil_turn_timer
        p.monster_cybil_turn_timer = t < -1536 and -1536 or (t > 0 and 0 or t)
        local rot = c.rot_y
        if p.monster_cybil_turn_timer > -1024 then
            rot = rot - timestep_30fps(32)
        end
        c.rot_y = rot
        if p.monster_cybil_turn_timer == -1536 then
            p.monster_cybil_turn_timer = 0
            p.monster_cybil_evade = 0
        end
    else
        local x, y, z = c.pos_x, c.pos_y, c.pos_z
        local tx = x + q12_mul32(sh.math.sin(c.rot_y), 1228)
        local tz = z + q12_mul32(sh.math.cos(c.rot_y), 1228)
        if sh.entity.ray_trace(x, y, z, s32(tx), y, s32(tz)) ~= 0 then
            p.monster_cybil_evade = (sh.rng.rand16() & 1) + 2
            p.monster_cybil_turn_timer = 0
        end
    end
end

-- Wandering off: away from the centre at a slant, with a random sway, for five seconds.
local function wander(c, p)
    local slant
    if p.npc_control_state == 1 then
        slant = ((sh.rng.rand16() & 1) * 2048) - 1024
    else
        slant = ((sh.rng.rand16() & 1) * 2048) - 512
    end
    local target = s16((slant + sh.math.atan2(asr(s32(c.pos_x - CENTRE_X), 4), asr(s32(c.pos_z - CENTRE_Z), 4))
        + 4096) & 0xFFF)
    if c.anim_status == 3 then
        local k = c.anim_keyframe
        if k >= 0 and k <= 23 then
            p.npc_move_speed = STEP_WANDER[k + 1]
        end
    end
    local turn = shortest_angle(c.rot_y, target)
    if abs16(turn) < 64 then
        c.rot_y = target
    elseif turn > 0 then
        c.rot_y = c.rot_y + timestep_30fps(64)
    else
        c.rot_y = c.rot_y - timestep_30fps(64)
    end
    c.rot_y = c.rot_y + timestep_30fps((sh.rng.rand16() % 128) - 64)
    p.monster_cybil_wander_timer = p.monster_cybil_wander_timer + sh.game.delta_time()
    if p.monster_cybil_wander_timer > 20480 then
        p.monster_cybil_evade = 0
        p.monster_cybil_wander_timer = 0
    end
end

local function fight_step(c, p)
    if c.anim_status == 3 and (c.anim_keyframe & 0xFFFF) < 24 then
        p.npc_move_speed = STEP_FIGHT[c.anim_keyframe + 1]
    end
end

-- Legs state 9: she staggers, then walks on.
local function legs_stagger(c, p)
    if c.npc_move_speed ~= 0 then
        c.npc_move_speed = c.npc_move_speed - 1638
        if c.npc_move_speed < 0 then
            c.npc_move_speed = 0
        end
    end
    if c.state_step == 0 then
        p.npc_move_speed = 6144
        anim_status_set(c, 9, false)
    end
    if c.anim_keyframe == clip_end(c, 19) then
        p.monster_cybil_legs_state = 1
        c.state_step = 0
        p.monster_cybil_heading_offset = 0
    end
end

-- Arms state 9: the stagger of the upper body.
local function arms_stagger(c, p)
    if upper.state_step == 0 then
        upper.anim_status = 18
        upper.state_step = upper.state_step + 1
    end
    if upper.anim_keyframe == clip_end(c, 19) then
        p.monster_cybil_arms_state = 1
        upper.state_step = 0
    end
end

local function enter_phase_three(c, p)
    map_data(c, "phase_change", function()
        sh.flag_set("M6S04_CybilBossPhaseThreeReached", true)
        p.npc_control_state = 3
        c.state_step = 0
        upper.state_step = 0
        p.monster_cybil_second_phase = 0
    end, p)
end

-- Strike or rifle strike at Harry (the arms states 8): the hit knocks him back.
local function strike(c, p, pl, attack, ends_legs)
    face_player(c, pl)
    aim_point_update(c)
    upper_status_set(16)
    if upper.anim_keyframe > 169 and upper.anim_keyframe < 173 then
        sh.entity.clip_table_set(c.slot)
        c.attack_window = 1
    else
        c.attack_window = 0
    end
    if sh.entity.attack(c.slot, c.attack_window, attack, aim_x, aim_y, aim_z, 0, c.rot_y, 1024) ~= -1 then
        pl.damage_z = 4096
        if sh.entity.map_var(LATCH) == 0 then
            sh.entity.sfx_flags(c.slot, sound(c, "bullet_hit", 1630), 64, 0)
        end
        sh.entity.set_map_var(LATCH, 1)
        p.monster_cybil_evade = 1
    end
    local k = upper.anim_keyframe
    if k & 1 ~= 0 and k == clip_end(c, 17) then
        p.monster_cybil_arms_state = 1
        if ends_legs then
            p.monster_cybil_legs_state = 1
        end
        upper.state_step = 0
        if ends_legs then
            c.state_step = 0
        end
        sh.entity.set_map_var(LATCH, 0)
    end
end

-- Phase one (npc_control_state 0): she circles, aims, shoots from range and drops from the horses.
local function phase_one(c, p, pl)
    local legs = p.monster_cybil_legs_state
    if legs == 1 then
        if c.state_step == 0 then
            c.anim_status = 2
            c.state_step = c.state_step + 1
        end
        obstacle_turn(c, p)
        if pl.health > 0 and p.monster_cybil_evade == 0 then
            if p.npc_distance_to_player > 2457 then
                face_player(c, pl)
            end
            p.npc_move_speed = 0
            fight_step(c, p)
            if p.monster_cybil_on_screen == 0 then
                p.npc_move_speed = p.npc_move_speed << 1
                if p.monster_cybil_turn_timer > 60 then
                    p.monster_cybil_turn_timer = 0
                    if (sh.rng.rand16() % 10) > 4 and p.npc_distance_to_player > 16384 then
                        p.monster_cybil_ambush_point = ambush_point(pl)
                        if p.monster_cybil_ambush_point ~= -1 then
                            p.monster_cybil_legs_state = 12
                            p.monster_cybil_arms_state = 12
                            c.state_step = 0
                            upper.state_step = 0
                        end
                    else
                        reposition(c, p, pl)
                    end
                end
            end
            if p.monster_cybil_shots_left <= 0 or c.health < 8192000 then
                enter_phase_three(c, p)
                return
            end
        elseif p.monster_cybil_evade == 1 then
            wander(c, p)
            if p.monster_cybil_shots_left <= 0 or c.health < 8192000 then
                enter_phase_three(c, p)
                return
            end
        else
            p.npc_move_speed = 0
            fight_step(c, p)
        end
        p.monster_cybil_heading_offset = 0
    elseif legs == 2 then
        anim_status_set(c, 2, false)
        p.npc_move_speed = 0
    elseif legs == 3 then
        anim_status_set(c, 3, false)
        p.npc_move_speed = 0
    elseif legs == 8 then
        p.npc_move_speed = 0
        anim_status_set(c, 8, false)
        if c.anim_keyframe == clip_end(c, 17) then
            p.monster_cybil_legs_state = 1
            c.state_step = 0
        end
    elseif legs == 9 then
        legs_stagger(c, p)
    elseif legs == 10 then
        anim_status_set(c, 10, false)
        p.npc_move_speed = 0
    elseif legs == 11 then
        -- Back down from the horse (the drop clip plays backwards).
        local last = clip_end(c, 23)
        if c.state_step == 0 then
            c.anim_status = 23
            c.state_step = c.state_step + 1
            c.anim_time = last * 4096
            c.anim_keyframe = last
        end
        if c.npc_move_speed ~= 0 then
            c.npc_move_speed = c.npc_move_speed - 327
            if c.npc_move_speed < 0 then
                c.npc_move_speed = 0
            end
        end
        local k = c.anim_keyframe
        if k == last - 4 or k == last - 5 then
            p.npc_move_speed = 2457
        end
        if k <= last - 7 then
            c.pos_y = c.pos_y + asr(sh.game.gravity_speed(), 2)
        elseif last - 4 >= k then
            c.pos_y = c.pos_y + asr(sh.game.gravity_speed(), 4)
        else
            c.pos_y = p.monster_cybil_base_height - 1843
        end
        if c.pos_y > 0 then
            c.pos_y = 0
        end
        local pt = p.monster_cybil_ambush_point
        c.rot_y = pt < 6 and AMBUSH_HEADING[pt + 1] - 2048 or AMBUSH_HEADING[pt + 1]
        if c.anim_keyframe == clip_start(c, 23) then
            p.monster_cybil_legs_state = 1
            c.state_step = 0
        end
    elseif legs == 12 then
        -- Waiting on a horse.
        local pt = p.monster_cybil_ambush_point
        c.pos_x = AMBUSH_X[pt + 1] + CENTRE_X
        c.pos_y = p.monster_cybil_base_height - 1843
        c.pos_z = AMBUSH_Z[pt + 1] + CENTRE_Z
        c.rot_y = pt < 6 and AMBUSH_HEADING[pt + 1] - 2048 or AMBUSH_HEADING[pt + 1]
        if c.anim_keyframe >= clip_start(c, 25) and clip_end(c, 25) >= c.anim_keyframe then
            c.box_offset_x = q12_mul32(sh.math.sin(c.rot_y), -1884)
            c.box_offset_z = q12_mul32(sh.math.cos(c.rot_y), -1884)
        end
        anim_status_set(c, 12, false)
        p.npc_move_speed = 0
    end

    local arms = p.monster_cybil_arms_state
    if arms == 1 then
        upper_status_set(2)
        if upper.anim_status == 3 then
            upper.anim_time = c.anim_time
            upper.anim_keyframe = c.anim_keyframe
        end
        if pl.health > 0 and p.monster_cybil_evade == 0 then
            local ps = player_state()
            if p.npc_distance_to_player < 3276 and (ps > 50 or ps < 47) then
                if p.monster_cybil_arms_state == 1 then
                    p.monster_cybil_legs_state = 8
                    p.monster_cybil_arms_state = 8
                    c.state_step = 0
                    upper.state_step = 0
                end
            elseif p.monster_cybil_on_screen_timer >= 12288 and (not shot_blocked(c, p) or ps == 47 or ps == 48) then
                if p.npc_distance_to_player <= 16384 then
                    p.monster_cybil_legs_state = 2
                    c.state_step = 0
                end
                p.monster_cybil_arms_state = 2
                upper.state_step = 0
            end
        end
    elseif arms == 2 then
        -- Raising the gun.
        if upper.state_step == 0 then
            p.monster_cybil_aim_timer = 0
            aim_delay = sign16(sh.rng.rand16() % 4096)
            upper_status_set(4)
        end
        face_player(c, pl)
        p.monster_cybil_aim_timer = p.monster_cybil_aim_timer + sh.game.delta_time()
        aim_point_update(c)
        if upper.anim_keyframe >= 29 then
            held_item(2)
        end
        if upper.anim_keyframe == clip_end(c, 5) and aim_delay < p.monster_cybil_aim_timer then
            if p.monster_cybil_on_screen ~= 0 and pl.flags & 8 == 0 then
                p.monster_cybil_burst_count = 0
                p.monster_cybil_arms_state = 3
                upper.state_step = 0
                if p.monster_cybil_legs_state == 2 then
                    p.monster_cybil_legs_state = 3
                    c.state_step = 0
                end
            else
                p.monster_cybil_arms_state = 10
                upper.state_step = 0
                if p.monster_cybil_legs_state ~= 1 then
                    p.monster_cybil_legs_state = 10
                    c.state_step = 0
                end
            end
        end
    elseif arms == 3 then
        -- Shooting.
        if upper.state_step == 0 then
            p.monster_cybil_aim_timer = 0
            p.monster_cybil_on_screen_timer = 0
            p.monster_cybil_shots_left = p.monster_cybil_shots_left - 1
            p.monster_cybil_burst_count = p.monster_cybil_burst_count + 1
            burst_delay = sign16(sh.rng.rand16() % 4096)
        end
        aim_point_update(c)
        local first = clip_start(c, 7)
        local k = upper.anim_keyframe
        if k >= first + 2 and first + 4 >= k and p.monster_cybil_shot_hit == 0 then
            sh.entity.clip_table_set(c.slot)
            c.attack_window = 1
            sh.entity.weapon_effect(ATTACK_SHOT, p.monster_cybil_aim_pitch, c.rot_y, 0)
            if sh.entity.attack(c.slot, c.attack_window, ATTACK_SHOT, aim_x, aim_y, aim_z, 0, c.rot_y,
                    p.monster_cybil_aim_pitch) ~= -1 then
                p.monster_cybil_shot_hit = 1
                p.monster_cybil_evade = 1
            end
        else
            c.attack_window = 0
        end
        if upper.anim_keyframe >= first + 4 then
            face_player(c, pl)
        end
        p.monster_cybil_aim_timer = p.monster_cybil_aim_timer + sh.game.delta_time()
        upper_status_set(6)
        if c.anim_status & 1 ~= 0 and upper.anim_keyframe == clip_end(c, 7) then
            if p.monster_cybil_on_screen == 0 or p.monster_cybil_shots_left == 0 or p.monster_cybil_burst_count == 3
                    or p.monster_cybil_shot_hit ~= 0 or shot_blocked(c, p) then
                p.monster_cybil_shot_hit = 0
                p.monster_cybil_arms_state = 10
                upper.state_step = 0
                if p.monster_cybil_legs_state ~= 1 then
                    p.monster_cybil_legs_state = 10
                    c.state_step = 0
                end
            elseif burst_delay + 6144 < p.monster_cybil_aim_timer then
                upper.state_step = 0
                p.monster_cybil_aim_timer = 0
                if p.monster_cybil_legs_state == 3 then
                    c.state_step = 0
                end
            end
        end
    elseif arms == 8 then
        strike(c, p, pl, ATTACK_STRIKE, false)
    elseif arms == 9 then
        arms_stagger(c, p)
        if upper.anim_keyframe == clip_end(c, 19) and p.monster_cybil_legs_state == 12 then
            p.monster_cybil_legs_state = 11
            p.monster_cybil_arms_state = 11
            c.state_step = 0
            upper.state_step = 0
            c.box_offset_z = 0
            c.box_offset_x = 0
        end
    elseif arms == 10 then
        -- Lowering the gun.
        upper_status_set(20)
        if upper.anim_status & 1 ~= 0 and upper.anim_keyframe < 209 then
            held_item(1)
        end
        if upper.anim_keyframe == clip_start(c, upper.anim_status) then
            p.monster_cybil_arms_state = 1
            upper.state_step = 0
            if p.monster_cybil_legs_state == 10 then
                p.monster_cybil_legs_state = 1
                c.state_step = 0
            end
        end
    elseif arms == 11 then
        local last = clip_end(c, 23)
        if upper.state_step == 0 then
            upper.anim_status = 23
            upper.state_step = upper.state_step + 1
            upper.anim_time = last * 4096
            upper.anim_keyframe = last
        end
        if upper.anim_keyframe == clip_start(c, 23) then
            p.monster_cybil_arms_state = 1
            upper.state_step = 0
        end
    elseif arms == 12 then
        -- On the horse: she waits up to ten seconds, six on screen.
        p.monster_cybil_aim_timer = p.monster_cybil_aim_timer + sh.game.delta_time()
        if c.state_step == 0 then
            sh.entity.sfx_flags(c.slot, sound(c, "freeze", 1618), 64, 0)
        end
        upper_status_set(24)
        if p.monster_cybil_aim_timer >= 40960 or p.monster_cybil_on_screen_timer >= 24576 then
            p.monster_cybil_legs_state = 11
            p.monster_cybil_arms_state = 11
            c.state_step = 0
            upper.state_step = 0
            p.monster_cybil_aim_timer = 0
        end
    end
end

-- Phase two (npc_control_state 1): she closes in, grabs Harry or strikes.
local function phase_two(c, p, pl)
    local legs = p.monster_cybil_legs_state
    if legs == 1 then
        if c.state_step == 0 then
            local first = clip_start(c, 3)
            c.anim_status = 3
            c.state_step = c.state_step + 1
            c.anim_time = first + 12288
            c.anim_keyframe = first + 3
        end
        obstacle_turn(c, p)
        if pl.health > 0 and p.monster_cybil_evade == 0 then
            p.npc_move_speed = 0
            if p.npc_distance_to_player > 2457 then
                face_player(c, pl)
            end
            fight_step(c, p)
            if p.monster_cybil_on_screen == 0 then
                p.npc_move_speed = p.npc_move_speed << 1
                if p.monster_cybil_turn_timer > 60 then
                    p.monster_cybil_turn_timer = 0
                    reposition(c, p, pl)
                end
            end
            if near_ring_point(c, p) then
                p.monster_cybil_wander_timer = p.monster_cybil_wander_timer + sh.game.delta_time()
            else
                p.monster_cybil_wander_timer = 0
            end
            if p.monster_cybil_wander_timer > 12288 then
                p.monster_cybil_evade = 1
                p.monster_cybil_wander_timer = 0
            end
        elseif p.monster_cybil_evade == 1 then
            wander(c, p)
        else
            p.npc_move_speed = 0
            fight_step(c, p)
        end
        p.monster_cybil_heading_offset = 0
    elseif legs == 5 then
        anim_status_set(c, 5, false)
        face_player(c, pl)
        if c.anim_keyframe >= clip_start(c, 11) + 10 then
            p.npc_move_speed = 2867
        end
    elseif legs == 6 then
        -- Holding Harry: her legs follow his clip and she slides to his side.
        local tx, tz = 0, 0
        anim_status_set(c, 6, false)
        if c.anim_status & 1 ~= 0 then
            local offset = hold_offset()
            c.anim_time = pl.anim_time - offset * 4096
            c.anim_keyframe = pl.anim_keyframe - offset
        end
        p.npc_move_speed = 0
        local angle = ((c.rot_y & 0xFFFF) + 6144) & 0xFFF
        local ps = player_state()
        if ps == 32 then
            tx = s32(pl.pos_x + q12_mul32(sh.math.sin(angle), 2539))
            tz = s32(pl.pos_z + q12_mul32(sh.math.cos(angle), 2539))
        elseif ps == 33 then
            tx = s32(pl.pos_x + q12_mul32(sh.math.sin(angle), 3276))
            tz = s32(pl.pos_z + q12_mul32(sh.math.cos(angle), 3276))
        end
        if ps > 31 and ps < 34 then
            if tx < c.pos_x then
                c.pos_x = c.pos_x - 32
            elseif c.pos_x < tx then
                c.pos_x = c.pos_x + 32
            end
            if tz < c.pos_z then
                c.pos_z = c.pos_z - 32
            elseif c.pos_z < tz then
                c.pos_z = c.pos_z + 32
            end
        end
    elseif legs == 7 then
        anim_status_set(c, 7, false)
        if c.anim_keyframe == clip_end(c, 15) then
            p.monster_cybil_legs_state = 1
            c.state_step = 0
            c.pos_x = c.pos_x + q12_mul32(sh.math.sin(c.rot_y + 384), 409)
            c.pos_z = c.pos_z + q12_mul32(sh.math.cos(c.rot_y + 384), 409)
        end
    elseif legs == 8 then
        p.npc_move_speed = 0
        anim_status_set(c, 8, false)
    elseif legs == 9 then
        legs_stagger(c, p)
    end

    local arms = p.monster_cybil_arms_state
    if arms == 1 then
        if upper.state_step == 0 then
            local first = clip_start(c, 3)
            upper.anim_status = 3
            upper.state_step = upper.state_step + 1
            upper.anim_time = first + 12288
            upper.anim_keyframe = first + 3
        end
        if upper.anim_status == 3 then
            upper.anim_time = c.anim_time
            upper.anim_keyframe = c.anim_keyframe
        end
        if pl.health > 0 and p.monster_cybil_evade == 0 and p.npc_distance_to_player < 3276
                and p.monster_cybil_turn_timer > 30 then
            if (sh.rng.rand16() % 8) >= 5 then
                if p.monster_cybil_arms_state == 1 then
                    p.monster_cybil_legs_state = 8
                    p.monster_cybil_arms_state = 8
                    c.state_step = 0
                    upper.state_step = 0
                end
            elseif p.monster_cybil_arms_state == 1 then
                p.monster_cybil_legs_state = 5
                p.monster_cybil_arms_state = 5
                c.state_step = 0
                upper.state_step = 0
            end
        end
    elseif arms == 5 then
        -- The grab.
        aim_point_update(c)
        upper_status_set(10)
        if upper.anim_keyframe == clip_end(c, 11) then
            sh.entity.clip_table_set(c.slot)
            c.attack_window = 1
        else
            c.attack_window = 0
        end
        local hit = sh.entity.attack(c.slot, c.attack_window, ATTACK_GRAB, aim_x, aim_y, aim_z, 0, c.rot_y, 1024)
        if hit ~= -1 then
            pl.attacker_index = c.attacker_index
        end
        if upper.anim_keyframe == clip_end(c, 11) then
            if hit ~= -1 then
                p.monster_cybil_arms_state = 6
                p.monster_cybil_legs_state = 6
            else
                p.monster_cybil_arms_state = 7
                p.monster_cybil_legs_state = 7
            end
            upper.state_step = 0
            c.state_step = 0
            p.npc_move_speed = 0
            p.monster_cybil_aim_timer = 0
        end
    elseif arms == 6 then
        -- Holding Harry: his health drains while he struggles.
        upper_status_set(12)
        if upper.anim_status & 1 ~= 0 then
            local offset = hold_offset()
            upper.anim_time = pl.anim_time - offset * 4096
            upper.anim_keyframe = pl.anim_keyframe - offset
        end
        pl.damage_amount = sh.game.delta_time() * 4
        if pl.attacker_index == -1 or pl.health <= 0 or pl.attack_received == -1 then
            p.monster_cybil_arms_state = 7
            p.monster_cybil_legs_state = 7
            upper.state_step = 0
            c.state_step = 0
            pl.damage_amount = 0
        end
        if upper.anim_status & 1 ~= 0 then
            local k = upper.anim_keyframe
            if k == 101 then
                p.monster_cybil_grab_twist = 0
            else
                local a = GRAB_TWIST[k - 91 + 1] or 0
                local b = GRAB_TWIST[k - 90 + 1] or 0
                p.monster_cybil_grab_twist = a + idiv((b - a) * (upper.anim_time & 0xFFF), 4096)
            end
        end
    elseif arms == 7 then
        upper_status_set(14)
        if upper.anim_keyframe == clip_end(c, 15) then
            p.monster_cybil_arms_state = 1
            upper.state_step = 0
            p.monster_cybil_turn_timer = 0
            p.monster_cybil_evade = 1
        end
    elseif arms == 8 then
        strike(c, p, pl, ATTACK_RIFLE_STRIKE, true)
    elseif arms == 9 then
        arms_stagger(c, p)
    elseif arms == 12 then
        upper_status_set(24)
    end
end

-- Damage: halved unless she is on a horse, ignored while she staggers; enough of it staggers her, none of it
-- is taken while Harry cannot be hurt.
local function take_damage(c, p, pl)
    if p.monster_cybil_stagger_damage ~= 0 then
        p.monster_cybil_stagger_damage = p.monster_cybil_stagger_damage - sh.game.delta_time() * 15
    end
    if p.monster_cybil_stagger_damage < 0 then
        p.monster_cybil_stagger_damage = 0
    end
    if sh.entity.player_damage_disable(0) ~= 0 then
        return
    end
    local st = p.npc_control_state
    if st < 13 and st > 2 then
        return
    end
    local arms = p.monster_cybil_arms_state
    if arms == 9 or arms == 11 then
        c.damage_amount = 0
        return
    end
    if arms ~= 12 then
        c.damage_amount = asr(c.damage_amount, 1)
    end
    if c.damage_amount == 0 then
        return
    end
    p.monster_cybil_stagger_damage = p.monster_cybil_stagger_damage + c.damage_amount
    c.health = c.health - c.damage_amount
    c.damage_amount = 0
    p.monster_cybil_heading_offset = angle_abs(sh.math.atan2(asr(s32(c.pos_x - pl.pos_x), 4),
        asr(s32(c.pos_z - pl.pos_z), 4)))
    p.monster_cybil_heading_offset = shortest_angle(c.rot_y, p.monster_cybil_heading_offset)
    if c.health <= 0 then
        map_data(c, "defeated", function()
            sh.entity.player_damage_disable(1)
            sh.flag_set("M6S04_CybilBossKilled", true)
            sh.entity.player_weapon_model_set(0, -1)
        end, p)
    else
        if p.monster_cybil_stagger_damage >= 3276800 and p.monster_cybil_legs_state ~= 12 then
            p.monster_cybil_legs_state = 9
            c.state_step = 0
        end
        if p.monster_cybil_stagger_damage >= 3276800 or p.monster_cybil_arms_state == 12 then
            p.monster_cybil_arms_state = 9
            upper.state_step = 0
            p.monster_cybil_stagger_damage = 0
        end
    end
    p.monster_cybil_evade = 0
    p.monster_cybil_wander_timer = 0
end

-- The phase, the poses of the cutscenes, the sounds; then heading and speed for the move.
local function control(c, p, pl)
    p.npc_distance_to_player = distance_q8(pl.pos_x - c.pos_x, pl.pos_z - c.pos_z)
    p.monster_cybil_on_screen = on_screen(c.pos_x, c.pos_y, c.pos_z) and 1 or 0
    if p.monster_cybil_on_screen ~= 0 then
        p.monster_cybil_on_screen_timer = p.monster_cybil_on_screen_timer + sh.game.delta_time()
    else
        p.monster_cybil_on_screen_timer = 0
    end
    p.monster_cybil_turn_timer = p.monster_cybil_turn_timer + 1

    local st = p.npc_control_state
    if st == 0 then
        phase_one(c, p, pl)
    elseif st == 1 then
        phase_two(c, p, pl)
    elseif st == 4 then
        -- The cutscene between the phases; the second starts with flag M6S04_CybilBossPhaseChangeCutsceneDone.
        p.monster_cybil_aim_pitch = 1024
        record_keyframe_set(c, 9)
        upper_keyframe_set(c, 9)
        if c.anim_keyframe >= 66 then
            held_item(1)
        else
            held_item(2)
        end
        p.npc_move_speed = 0
        if sh.flag_get("M6S04_CybilBossPhaseChangeCutsceneDone") then
            p.monster_cybil_second_phase = 1
            p.npc_control_state = 1
            p.monster_cybil_legs_state = 1
            p.monster_cybil_arms_state = 1
            c.state_step = 0
            upper.state_step = 0
        end
    elseif st == 5 then
        if c.state_step == 0 then
            upper.state_step = 0
            record_keyframe_set(c, 29)
        end
        upper_keyframe_set(c, 29)
        p.npc_move_speed = 0
        anim_state_reset(c)
    elseif st == 6 then
        if c.state_step == 0 then
            upper.state_step = 0
            anim_status_set(c, 15, false)
        end
        upper_status_set(30)
        p.npc_move_speed = 0
        anim_state_reset(c)
        if c.anim_keyframe == 343 then
            p.npc_control_state = 12
            c.state_step = 0
            upper.state_step = 0
        end
    elseif st == 12 then
        record_keyframe_set(c, 43)
        upper_keyframe_set(c, 43)
        p.npc_move_speed = 0
        anim_state_reset(c)
    elseif st == 9 then
        if c.state_step == 0 then
            upper.state_step = 0
            record_keyframe_set(c, 41)
        end
        upper_keyframe_set(c, 41)
        held_item(1)
        anim_state_reset(c)
    elseif st == 10 then
        if c.state_step == 0 then
            upper.state_step = 0
            record_keyframe_set(c, 37)
        end
        upper_keyframe_set(c, 37)
        if upper.anim_keyframe >= 29 then
            held_item(2)
        else
            held_item(1)
        end
        anim_state_reset(c)
    elseif st == 11 then
        if c.state_step == 0 then
            upper.state_step = 0
            record_keyframe_set(c, 39)
        end
        upper_keyframe_set(c, 39)
        held_item(2)
        anim_state_reset(c)
        if c.state_step == 0 then
            upper.state_step = 0
        end
    elseif st == 7 then
        if c.state_step == 0 then
            upper.state_step = 0
            record_keyframe_set(c, 33)
        end
        upper_keyframe_set(c, 33)
    elseif st == 8 then
        if c.state_step == 0 then
            upper.state_step = 0
            record_keyframe_set(c, 35)
        end
        upper_keyframe_set(c, 35)
    end

    local sfx, pitch = sh.entity.footstep_sfx(11, CHARA)
    st = p.npc_control_state
    if st == 9 then
        footstep_pair(c, 41, 5, 16, sfx, pitch)
    elseif st == 10 then
        keyframe_sfx(c, 37, 29, sound(c, "gun_raise", 1620))
    elseif st == 11 then
        -- The cutscene shot: the effect once when the clip enters keyframe 38 (a fractional frame time keeps a
        -- keyframe for several ticks).
        local k = c.anim_keyframe
        if k == 38 and shot_prev_keyframe ~= 38 then
            sh.entity.weapon_effect(ATTACK_SHOT, 1024, c.rot_y, 0)
        end
        shot_prev_keyframe = k
        keyframe_sfx(c, 39, 38, sound(c, "gunshot", 1622))
    elseif st == 4 then
        keyframe_sfx(c, 9, 69, sound(c, "gun_lower", 1621))
    elseif st == 5 then
        if c.anim_keyframe < 312 then
            keyframe_sfx(c, 29, 289, sound(c, "scene_sound", 1632))
        else
            keyframe_sfx(c, 29, 313, sound(c, "scene_sound_2", 1631))
        end
    end
    if p.monster_cybil_legs_state == 1 then
        footstep_pair(c, 3, 5, 16, sfx, pitch)
    end

    local arms = p.monster_cybil_arms_state
    if arms == 2 or arms == 3 then
        local at = arms == 2 and 29 or 38
        local name = arms == 2 and "gun_raise" or "gunshot"
        local fallback = arms == 2 and 1620 or 1622
        if upper.anim_status & 1 ~= 0 and upper.anim_keyframe >= at and sh.entity.map_var(LATCH) == 0 then
            sh.entity.sfx_flags(c.slot, sound(c, name, fallback), 64, 0)
            sh.entity.set_map_var(LATCH, 1)
        elseif upper.anim_keyframe < at then
            sh.entity.set_map_var(LATCH, 0)
        end
    end

    local speed = p.npc_move_speed
    local offset = p.monster_cybil_heading_offset
    c.heading_angle = angle_abs(offset + c.rot_y)
    c.move_speed = speed
    c.fall_speed = c.fall_speed + sh.game.gravity_speed()
    sh.entity.bone_flag_set(c.slot, 0, 0)
    sh.entity.bone_rotation_set(c.slot, 0, c.rot_x, c.rot_y, c.rot_z)
end

-- The step of this tick: free on a horse, against the walls otherwise.
local function move(c, p)
    local dt = sh.game.delta_time()
    local dx, dz = step_along(q12_mul(c.move_speed, dt), c.heading_angle)
    local dy = q12_mul(c.fall_speed, dt)
    local legs = p.monster_cybil_legs_state
    if legs == 11 then
        if c.anim_status & 1 ~= 0 then
            c.cylinder_radius = 1228 - idiv((c.anim_keyframe - 216) * 12288, 100)
        end
        local ox, _, oz = sh.entity.wall_offset(c.slot, dx, dy, dz)
        c.pos_x = c.pos_x + ox
        c.pos_z = c.pos_z + oz
        c.fall_speed = 0
    elseif legs == 12 then
        c.cylinder_radius = 0
        c.pos_x = c.pos_x + dx
        c.pos_z = c.pos_z + dz
        c.fall_speed = 0
    else
        c.cylinder_radius = 1228
        c.cylinder_offset_z = 0
        c.cylinder_offset_x = 0
        c.box_offset_z = 0
        c.box_offset_x = 0
        local ox, _, oz = sh.entity.wall_offset(c.slot, dx, dy, dz)
        c.pos_x = c.pos_x + ox
        c.pos_y = 0
        c.fall_speed = 0
        c.pos_z = c.pos_z + oz
    end
end

local function animate(c)
    sh.entity.bone_translation_set(c.slot, 0, asr(c.pos_x, 4), asr(c.pos_y, 4), asr(c.pos_z, 4))
    sh.entity.monster_cybil_anim_step(c.slot, 0, LOWER_BONES)
    sh.entity.monster_cybil_anim_step(c.slot, 1, UPPER_BONES)
end

-- The gun arm follows the aim pitch; on a horse the hips sit on the saddle; holding Harry the hips twist.
local function pose(c, p, pl)
    local delta = timestep_30fps(32)
    local arms = p.monster_cybil_arms_state
    if arms == 9 or arms == 11 then
        local legs = p.monster_cybil_legs_state
        if legs == 12 then
            sh.entity.bone_translation_set(c.slot, HIPS_BONE, 0, -224, -136)
        end
        if legs == 11 and upper.anim_status == 19 then
            sh.entity.bone_translation_set(c.slot, HIPS_BONE, 0, -224, -136)
        end
    elseif arms == 10 then
        local range = asr(delta, 1)
        local d = p.monster_cybil_aim_pitch - 1024
        if range >= d then
            if d >= -range then
                p.monster_cybil_aim_pitch = 1024
            else
                p.monster_cybil_aim_pitch = p.monster_cybil_aim_pitch + range
            end
        else
            p.monster_cybil_aim_pitch = p.monster_cybil_aim_pitch - delta
        end
        sh.entity.bone_rotate(c.slot, ARM_BONE, 0, 0, p.monster_cybil_aim_pitch - 1024)
    elseif arms == 2 or arms == 3 then
        local fx = asr(s32(pl.pos_x - aim_x), 4)
        local fz = asr(s32(pl.pos_z - aim_z), 4)
        local level = sh.math.sqrt(s32(s32(fx * fx) + s32(fz * fz)))
        local pitch = s16(sh.math.atan2(level, asr(s32(pl.pos_y + pl.box_offset_y - aim_y), 4)))
        if pitch + delta < p.monster_cybil_aim_pitch then
            p.monster_cybil_aim_pitch = p.monster_cybil_aim_pitch - delta
        else
            p.monster_cybil_aim_pitch = pitch
        end
        sh.entity.bone_rotate(c.slot, ARM_BONE, 0, 0, p.monster_cybil_aim_pitch - 1024)
    elseif arms == 6 then
        sh.entity.bone_rotate(c.slot, HIPS_BONE, 0, -p.monster_cybil_grab_twist, 0)
    end
end

local function start(c, p)
    for k = 0, 23 do
        sh.entity.monster_cybil_walk_durations(k, WALK_DURATION_WANDER[k + 1], WALK_DURATION_FIGHT[k + 1])
    end
    c.control_state = c.control_state + 1
    upper.control_state = upper.control_state + 1
    c.state_step = 0
    upper.state_step = 0
    sh.entity.set_collision_state(c.slot, 3)
    p.monster_cybil_aim_pitch = 1024
    c.anim_flags = c.anim_flags | 3
    c.flags = c.flags | 0x104
    sh.entity.set_map_var(LATCH, 0)
    upper.anim_flags = upper.anim_flags | 3
    c.health = sh.chara_param_q12(CHARA, "health", 16384000)
    p.monster_cybil_shots_left = 10
    p.monster_cybil_ambush_point = 0
    p.monster_cybil_turn_timer = 0
    p.monster_cybil_aim_timer = 0
    p.monster_cybil_on_screen_timer = 0
    p.monster_cybil_wander_timer = 0
    p.monster_cybil_stagger_damage = 0
    p.monster_cybil_evade = 0
    p.monster_cybil_second_phase = 0
    held_item(17)
    map_data(c, "fight_start", function() end, p)
end

function update(slot)
    local c = record(slot)
    local p = map_data(c, "fight_record", record, 1)
    local pl = record(0)
    if c.control_state == 0 then
        start(c, p)
    end
    if sh.game.delta_time() ~= 0 then
        take_damage(c, p, pl)
        control(c, p, pl)
        move(c, p)
        animate(c)
        pose(c, p, pl)
    end
end
