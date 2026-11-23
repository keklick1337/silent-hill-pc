-- Kaufmann (map3_s00, map5_s02, map5_s03, map7_s02, map7_s03; also the ending's Kaufmann): the
-- cutscenes set npc_control_state, he plays the matching clip, walks with npc_move_speed and
-- holds the item of the scene (WorldGfx_HeldItemAttach: chara id, packed mesh variant).

-- Clip names of the character data ("anims"), in clip order: A[i] resolves name -> number.
local NAMES = {
    "stand", "burst_in", "walk", "turn_with_gun", "point_gun", "lower_gun", "talk_gesture",
    "leave", "push_free", "shoot", "draw_gun", "snatch_item", "unused_walk",
    "unused_stand_loop", "walk_up", "take_out_vial", "hold_vial", "throw_vial", "back_away",
    "stand_ready", "walk_in", "wrestle_monster",
}
local A = {}
for i = 1, #NAMES do
    A[i] = sh.chara_anim("Kaufmann", NAMES[i], i)
end

local KAUFMANN = sh.chara_id("Kaufmann") or 38
local ENDING_KAUFMANN = sh.chara_id("EndingKaufmann") or 39
local WALK_SPEED = 5120   -- Q12(1.25)
local ACCEL = 1638        -- Q12(0.4) per 30 fps tick
local DECEL = 2048        -- Q12(0.5) per 30 fps tick
local WEAPON_KAUFMANN_ATTACK = 70 -- EquippedWeaponId_Unk70
local FLAG_ITEM_GIVEN = 198       -- EventFlag_198

-- Turn per tick (Q8); only the init writes it.
local turn_rate = 0

-- Packed held-item mesh: variant | (index << 4) .
local function mesh(variant, index)
    return variant | (index << 4)
end

local function on_map(name)
    return sh.map_name() == name
end

local function hold(chara, variant, index)
    sh.held_item_attach(chara, mesh(variant, index))
end

-- A clip per control state, then the cutscene's reset request.
local function play(c, clip, keyframe_start)
    if keyframe_start then
        anim_status_keyframe_set(c, A[clip], true)
    else
        anim_status_set(c, A[clip], false)
    end
    anim_state_reset(c)
end

local function control_update(c)
    local state = c.npc_control_state
    if state == 0 then
        if c.npc_move_speed ~= 0 then
            c.npc_move_speed = c.npc_move_speed - timestep_30fps(ACCEL) * 2
            if c.npc_move_speed < 0 then
                c.npc_move_speed = 0
            end
        end
        play(c, 1, false)
    elseif state == 1 then
        if c.npc_move_speed > WALK_SPEED then
            c.npc_move_speed = c.npc_move_speed - timestep_30fps(DECEL)
            if c.npc_move_speed < WALK_SPEED then
                c.npc_move_speed = WALK_SPEED
            end
        elseif c.npc_move_speed < WALK_SPEED then
            c.npc_move_speed = c.npc_move_speed + timestep_30fps(ACCEL)
            local v = c.npc_move_speed
            c.npc_move_speed = v < 0 and 0 or (v > WALK_SPEED and WALK_SPEED or v)
        end
        play(c, 3, false)
        if on_map("map5_s03") then
            hold(KAUFMANN, 4, 3)
        end
    elseif state == 5 then
        play(c, 1, true)
        hold(KAUFMANN, 1, 0)
    elseif state == 24 then
        play(c, 20, true)
        if on_map("map5_s03") then
            hold(KAUFMANN, 4, 3)
        else
            hold(ENDING_KAUFMANN, 1, 3)
        end
    elseif state == 6 then
        play(c, 4, true)
        if on_map("map3_s00") then
            hold(KAUFMANN, 3, 1)
        end
    elseif state == 7 then
        -- The shot: once, when the clip reaches keyframe 95 (latched in the first NPC's record).
        if c.anim_keyframe >= 95 and sh.entity.get(1, "npc_shot_fired") == 0 then
            sh.entity.set(1, "npc_shot_fired", 1)
            sh.entity.weapon_effect(WEAPON_KAUFMANN_ATTACK, 0, 0, 0)
        end
        play(c, 5, false)
        if on_map("map3_s00") then
            hold(KAUFMANN, 3, 1)
        end
    elseif state == 8 then
        play(c, 6, false)
        if on_map("map3_s00") then
            hold(KAUFMANN, 3, 1)
        end
    elseif state == 9 then
        play(c, 7, false)
        if c.anim_keyframe < 139 or c.anim_status & 1 == 0 then
            hold(KAUFMANN, 1, 1)
        else
            sh.flag_set(FLAG_ITEM_GIVEN, true)
            hold(KAUFMANN, 1, 3)
        end
    elseif state == 10 then
        play(c, 8, false)
    elseif state == 11 or state == 12 or state == 13 or state == 14 then
        local clip = ({ [11] = 9, [12] = 22, [13] = 10, [14] = 11 })[state]
        play(c, clip, state == 11 or state == 12)
        if on_map("map5_s02") then
            hold(KAUFMANN, 1, 1)
        end
    elseif state == 15 then
        play(c, 12, false)
        if c.anim_keyframe >= 364 and c.anim_status & 1 ~= 0 then
            hold(KAUFMANN, 4, 0)
        end
    elseif state == 16 then
        play(c, 2, false)
        if on_map("map5_s03") then
            hold(KAUFMANN, 1, 3)
        end
    elseif state == 25 then
        play(c, 21, false)
        hold(ENDING_KAUFMANN, 3, 3)
    elseif state == 17 then
        play(c, 13, false)
    elseif state == 18 then
        play(c, 16, false)
        if on_map("map7_s03") then
            if c.anim_keyframe < 78 or c.anim_status & 1 == 0 then
                hold(ENDING_KAUFMANN, 1, 3)
            else
                hold(ENDING_KAUFMANN, 4, 3)
            end
        end
    elseif state == 19 then
        play(c, 17, false)
        if on_map("map7_s03") then
            hold(ENDING_KAUFMANN, 4, 3)
        end
    elseif state == 20 then
        play(c, 18, true)
        if on_map("map7_s03") then
            if c.anim_keyframe < 141 then
                hold(ENDING_KAUFMANN, 4, 3)
            else
                hold(ENDING_KAUFMANN, 1, 3)
            end
        end
    elseif state == 21 then
        play(c, 15, false)
        if on_map("map7_s03") then
            hold(ENDING_KAUFMANN, 1, 1)
        end
    elseif state == 22 then
        play(c, 19, true)
        if on_map("map7_s03") then
            hold(ENDING_KAUFMANN, 1, 3)
        end
    elseif state == 23 then
        play(c, 14, false)
    end

    local _, ground_type = sh.entity.ground_at(c.pos_x, c.pos_z)
    local sfx, pitch0 = sh.entity.footstep_sfx(ground_type, c.chara_id)

    state = c.npc_control_state
    local keyframe = c.anim_keyframe
    local function steps(clip, k0, k1)
        footstep_pair(c, anim_status(A[clip], true), k0, k1, sfx, pitch0)
    end
    if state == 1 then
        steps(3, 53, 41)
    elseif state == 6 then
        steps(4, 80, 0)
    elseif state == 7 then
        keyframe_sfx(c, c.anim_status, 95, sound(c, "shot", 1492))
    elseif state == 9 then
        steps(7, 132, 174)
    elseif state == 10 then
        steps(8, 191, 0)
    elseif state == 17 then
        steps(13, 393, 0)
    elseif state == 13 then
        keyframe_sfx(c, anim_status(A[10], true), 307, sound(c, "cue", 1594))
    elseif state == 15 then
        if keyframe <= 353 then
            steps(12, 357, 353)
        else
            steps(12, 357, 361)
        end
    elseif state == 16 then
        if keyframe <= 21 then
            steps(2, 28, 21)
        else
            steps(2, 28, 34)
        end
    elseif state == 25 then
        if keyframe <= 247 then
            steps(21, 254, 247)
        else
            steps(21, 254, 260)
        end
    elseif state == 22 then
        if keyframe <= 211 then
            steps(19, 217, 211)
        else
            steps(19, 217, 221)
        end
    end

    control_tail(c, asr(turn_rate, 4))
end

local function init(c)
    collision_reset(c)
    sh.entity.set(1, "npc_shot_fired", 0)
    turn_rate = 0
    if on_map("map7_s03") then
        hold(ENDING_KAUFMANN, 0, 1)
    else
        hold(KAUFMANN, 0, 1)
    end
    if on_map("map7_s02") then
        sh.npc.transparent_set(KAUFMANN, true)
    end
end

function update(slot)
    local c = record(slot)
    if c.control_state == 0 then
        init(c)
    end
    control_update(c)
    move_flat(c, 0)
    anim_update(c)
end
