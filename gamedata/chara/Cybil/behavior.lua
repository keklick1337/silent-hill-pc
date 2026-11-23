-- Cybil (map0_s01, map4_s01, map6_s01, map7_s03; also the ending's Cybil): the cutscenes set
-- npc_control_state; she plays the clip, walks or runs with npc_move_distance (no collision)
-- and holds her gun. State 22 is the ending's shot: effect, sounds, once.

-- Clip names of the character data ("anims"), in clip order: A[i] resolves name -> number.
local NAMES = {
    "stand", "unused_idle_sway", "walk", "walk_turn_right", "walk_turn_left", "unused_pose_loop",
    "hand_over_gun", "talk_pose", "talk_gesture", "stand_with_gun", "stand_up", "stand_ready",
    "collapse", "sit_slump", "scene_pose_a", "head_loll", "twitch", "scene_pose_b", "walk_back",
    "walk_out", "run", "ending_walk_in", "ending_approach", "ending_stand", "ending_hit",
    "ending_walk_up", "ending_shot", "ending_shot_fall", "ending_stand_hold", "ending_kneel",
    "ending_hold",
}
local A = {}
for i = 1, #NAMES do
    A[i] = sh.chara_anim("Cybil", NAMES[i], i)
end

local CYBIL = sh.chara_id("Cybil") or 26
local ENDING_CYBIL = sh.chara_id("EndingCybil") or 27
local WALK_SPEED = 5120   -- Q12(1.25)
local RUN_SPEED = 16384   -- Q12(4.0)
local ACCEL = 1638        -- Q12(0.4) per 30 fps tick
local DECEL = 2048        -- Q12(0.5) per 30 fps tick
local RUN_ACCEL = 3072    -- Q12(0.75) per 30 fps tick
local WEAPON_CYBIL_SHOT = sh.entity.attack_number("CybilHandgun")
local PAD_EFFECT_HIT = 6

-- Turn per tick (Q12 * 16): set by the special walks 3 and 4 and kept until the next init.
local turn = 0
-- The ending shot of state 22: 0 not yet, 1 fired (the echo plays next tick), 2 done.
local shot = 0

local function on_map(name)
    return sh.map_name() == name
end

local function hold(chara, variant, index)
    sh.held_item_attach(chara, variant | (index << 4))
end

local function slow_down(c)
    if c.npc_move_distance ~= 0 then
        c.npc_move_distance = c.npc_move_distance - timestep_30fps(ACCEL) * 2
        if c.npc_move_distance < 0 then
            c.npc_move_distance = 0
        end
    end
end

-- Towards `target` (Q12 speed): down by `down`, up by `up` per 30 fps tick.
local function approach(c, target, down, up)
    if c.npc_move_distance > target then
        c.npc_move_distance = c.npc_move_distance - timestep_30fps(down)
        if c.npc_move_distance < target then
            c.npc_move_distance = target
        end
    elseif c.npc_move_distance < target then
        c.npc_move_distance = c.npc_move_distance + timestep_30fps(up)
        local v = c.npc_move_distance
        c.npc_move_distance = v < 0 and 0 or (v > target and target or v)
    end
end

local PLAIN = { [13] = 13, [14] = 14, [15] = 15, [16] = 16, [17] = 17, [18] = 18, [19] = 19, [20] = 20,
                [27] = 24, [29] = 30 }
local FROM_FIRST_KEYFRAME = { [7] = 8, [8] = 9, [11] = 12, [25] = 22, [26] = 23, [28] = 25 }

-- The ending states 21..24: the clip (both setters; the second one only runs when the first
-- did not), the gun in hand.
local function ending_clip(c, clip)
    anim_status_set(c, A[clip], false)
    anim_status_keyframe_set(c, A[clip], true)
    anim_state_reset(c)
    hold(ENDING_CYBIL, 2, 1)
end

local function anim_state_update(c)
    local state = c.npc_control_state
    if state == 0 or state == 12 then
        slow_down(c)
        anim_status_set(c, A[1], false)
        anim_state_reset(c)
    elseif state == 1 then
        approach(c, WALK_SPEED, DECEL, ACCEL)
        if on_map("map0_s01") then
            anim_status_keyframe_set(c, A[3], true)
        else
            anim_status_set(c, A[3], false)
        end
        anim_state_reset(c)
    elseif state == 2 then
        approach(c, RUN_SPEED, ACCEL, RUN_ACCEL)
        -- Blend status of clip 21, starting at the first keyframe of its playing entry.
        anim_status_keyframe_set(c, A[21], false, 1)
        anim_state_reset(c)
    elseif state == 3 or state == 4 then
        slow_down(c)
        turn = s32(sh.game.delta_time() * (state == 3 and 7 or -7))
        anim_status_set(c, A[state == 3 and 4 or 5], false)
        anim_state_reset(c)
    elseif state == 5 then
        slow_down(c)
        anim_status_keyframe_set(c, A[1], true)
        anim_state_reset(c)
    elseif state == 6 then
        anim_status_keyframe_set(c, A[7], true)
        anim_state_reset(c)
        if c.anim_keyframe < 91 then
            hold(CYBIL, 1, 1)
        elseif c.anim_keyframe < 105 then
            hold(CYBIL, 2, 1)
        else
            hold(CYBIL, 1, 2)
        end
    elseif state == 9 then
        anim_status_keyframe_set(c, A[10], true)
        anim_state_reset(c)
        hold(CYBIL, 1, 1)
    elseif state == 10 then
        anim_status_set(c, A[11], true)
        anim_state_reset(c)
    elseif state == 21 then
        ending_clip(c, 26)
        if c.anim_keyframe == 295 then
            c.npc_control_state = 30
            c.state_step = 0
        end
    elseif state == 22 then
        ending_clip(c, 27)
    elseif state == 23 then
        ending_clip(c, 28)
        if c.npc_control_state ~= 23 then
            c.npc_flags = c.npc_flags & ~0x2000
        end
    elseif state == 24 then
        ending_clip(c, 29)
    elseif state == 30 then
        anim_status_set(c, A[31], false)
        anim_state_reset(c)
        hold(ENDING_CYBIL, 2, 1)
    elseif PLAIN[state] ~= nil then
        anim_status_set(c, A[PLAIN[state]], false)
        anim_state_reset(c)
    elseif FROM_FIRST_KEYFRAME[state] ~= nil then
        anim_status_keyframe_set(c, A[FROM_FIRST_KEYFRAME[state]], true)
        anim_state_reset(c)
    end

    local _, ground_type = sh.entity.ground_at(c.pos_x, c.pos_z)
    local sfx, pitch0, pitch1 = sh.entity.footstep_sfx(ground_type, c.chara_id)
    if on_map("map6_s01") then
        sfx = sound(c, "footstep_map6_s01", 1607)
    end

    state = c.npc_control_state
    local keyframe = c.anim_keyframe
    local function steps(clip, k0, k1, pitch)
        return footstep_pair(c, anim_status(A[clip], true), k0, k1, sfx, pitch or pitch0)
    end
    if state == 1 then
        steps(3, 32, 45)
    elseif state == 2 then
        steps(21, 18, 7, pitch1)
    elseif state == 3 then
        steps(4, 61, 57)
    elseif state == 4 then
        steps(5, 70, 74, pitch1)
    elseif state == 8 then
        keyframe_sfx(c, c.anim_status, 128, sound(c, "cue", 1368))
    elseif state == 6 then
        steps(7, 83, 86)
        keyframe_sfx(c, anim_status(A[7], true), 92, sound(c, "cue_2", 1369))
    elseif state == 13 then
        if keyframe <= 185 then
            steps(13, 185, 189)
        elseif keyframe <= 192 then
            steps(13, 192, 189)
        else
            steps(13, 227, 222)
        end
    elseif state == 15 then
        steps(15, 260, 260)
    elseif state == 14 then
        steps(14, 239, 239)
    elseif state == 19 then
        steps(19, 362, 357)
    elseif state == 20 then
        if keyframe <= 373 then
            steps(20, 373, 380)
        elseif keyframe <= 391 then
            steps(20, 391, 380)
        else
            steps(20, 403, 398)
        end
    elseif state == 10 then
        keyframe_sfx(c, c.anim_status, 167, sound(c, "cue_3", 1370))
    elseif state == 25 then
        if keyframe <= 63 then
            if keyframe <= 30 then
                steps(22, 30, 39)
            elseif keyframe <= 46 then
                steps(22, 46, 39)
            else
                steps(22, 63, 56)
            end
        else
            if keyframe <= 81 then
                steps(22, 81, 74)
            else
                steps(22, 97, 91)
            end
        end
    elseif state == 28 then
        steps(25, 201, 198)
        if keyframe_sfx(c, c.anim_status, 228, sound(c, "hit", 1699)) then
            sh.pad.effect(PAD_EFFECT_HIT)
        end
    elseif state == 22 then
        if shot == 1 then
            sh.entity.sfx_flags(2, sound(c, "shot_echo", 1673), 192, 0) -- at the second NPC
            shot = 2
        end
        if c.anim_keyframe == 299 and shot == 0 then
            sh.entity.weapon_effect(WEAPON_CYBIL_SHOT, 1024, c.rot_y, 0)
            sh.entity.sfx_flags(c.slot, sound(c, "shot", 1622), 192, 0)
            shot = 1
        end
    elseif state == 24 then
        keyframe_sfx(c, anim_status(A[29], true), 327, sound(c, "cue_4", 1672))
    elseif state == 21 then
        keyframe_sfx(c, anim_status(A[26], true), 282, sound(c, "cue_5", 1539))
    end

    c.rot_y = angle_abs(c.rot_y + asr(turn, 4))
    c.heading_angle = c.rot_y
    c.move_speed = c.npc_move_distance
    c.fall_speed = c.fall_speed + sh.game.gravity_speed()
    sh.entity.bone_flag_set(c.slot, 0, 0)
    sh.entity.bone_rotation_set(c.slot, 0, c.rot_x, c.rot_y, c.rot_z)
end

local function init(c)
    collision_reset(c)
    turn = 0
    shot = 0
    if on_map("map7_s03") then
        hold(ENDING_CYBIL, 1, 1)
    else
        hold(CYBIL, 1, 1)
    end
end

function update(slot)
    local c = record(slot)
    if c.control_state == 0 then
        init(c)
    end
    anim_state_update(c)
    move_flat(c, nil)
    -- She plays her clip unless frozen.
    if c.npc_freeze == 0 then
        sh.entity.anim_step(slot)
    end
end
