-- Dahlia (map2_s01, map3_s06, map6_s01, map6_s04, map7_s02, map7_s03; also the ending's
-- Dahlia): the cutscenes set npc_control_state, she plays the clip, walks with npc_move_speed
-- against the walls and the ground, and holds the item of the scene.

-- Clip names of the character data ("anims"), in clip order: A[i] resolves name -> number.
local NAMES = {
    "stand", "walk", "talk_pose", "turn_away", "talk_gesture", "talk_gesture_flauros", "stand_pose",
    "confront", "scene_talk_a", "scene_talk_b", "reveal", "revealed_stand", "chant",
    "walk_ending", "scene_pose_a", "scene_to_stand", "talk_scold", "pose_hold_a",
    "appear_flash", "pose_hold_b", "scene_pose_b", "pose_hold_c", "scene_pose_c",
    "scene_walk_off", "hold_frozen",
}
local A = {}
for i = 1, #NAMES do
    A[i] = sh.chara_anim("Dahlia", NAMES[i], i)
end

local DAHLIA = sh.chara_id("Dahlia") or 30
local ENDING_DAHLIA = sh.chara_id("EndingDahlia") or 31
local WALK_SPEED = 5120   -- Q12(1.25)
local ACCEL = 1638        -- Q12(0.4) per 30 fps tick
local DECEL = 2048        -- Q12(0.5) per 30 fps tick
local EFFECT_POINT = { 566476, -4505, -412876 } -- Q12(138.3), Q12(-1.1), Q12(-100.8): the flash of state 19

-- Turn per tick (Q8); only the init writes it.
local turn_rate = 0

local function on_map(...)
    local map = sh.map_name()
    for _, name in ipairs({ ... }) do
        if map == name then
            return true
        end
    end
    return false
end

local function hold(chara, variant, index)
    sh.held_item_attach(chara, variant | (index << 4))
end

local function play(c, clip, keyframe_start)
    if keyframe_start then
        anim_status_keyframe_set(c, A[clip], true)
    else
        anim_status_set(c, A[clip], false)
    end
end

-- The maps of the second half (the hospital and after) changed a few of her states.
local function late_map()
    return on_map("map6_s01", "map6_s04", "map7_s02", "map7_s03")
end

local PLAIN = { [7] = 3, [8] = 4, [13] = 9, [14] = 10, [15] = 15, [17] = 17, [18] = 18, [20] = 20,
                [23] = 23, [24] = 24, [26] = 12, [27] = 13, [28] = 25 }
local FROM_FIRST_KEYFRAME = { [12] = 8, [21] = 21, [22] = 22 }

local function control_update(c)
    local state = c.npc_control_state
    if state == 0 or state == 5 then
        if c.npc_move_speed ~= 0 then
            c.npc_move_speed = c.npc_move_speed - timestep_30fps(ACCEL) * 2
            if c.npc_move_speed < 0 then
                c.npc_move_speed = 0
            end
        end
        play(c, 1, false)
        anim_state_reset(c)
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
        play(c, 2, false)
        anim_state_reset(c)
    elseif state == 6 then
        play(c, 14, false)
        if on_map("map7_s03") then
            hold(ENDING_DAHLIA, 1, 0)
        end
        anim_state_reset(c)
    elseif state == 11 then
        play(c, 7, on_map("map3_s06") or late_map())
        anim_state_reset(c)
    elseif state == 9 then
        play(c, 5, false)
        anim_state_reset(c)
        if c.anim_keyframe < 144 then
            hold(DAHLIA, 1, 0)
        elseif not late_map() then
            hold(DAHLIA, on_map("map3_s06") and 2 or 3, 0)
        end
    elseif state == 10 then
        if not late_map() then
            play(c, 6, on_map("map3_s06"))
        end
        anim_state_reset(c)
        if c.anim_keyframe < 198 then
            if not late_map() then
                hold(DAHLIA, on_map("map3_s06") and 2 or 3, 0)
            end
        else
            hold(DAHLIA, 1, 0)
        end
    elseif state == 16 then
        play(c, 16, false)
        anim_state_reset(c)
        if c.anim_keyframe == 103 then
            c.npc_control_state = 5
            c.state_step = 0
        end
    elseif state == 19 then
        if c.anim_keyframe >= 40 and sh.entity.map_var("dahlia_effect_spawned") == 0 then
            sh.entity.effect_spawn(2, EFFECT_POINT[1], EFFECT_POINT[2], EFFECT_POINT[3], 3, 1)
            sh.entity.set_map_var("dahlia_effect_spawned", 1)
        end
        play(c, 19, true)
        anim_state_reset(c)
        if c.npc_control_state ~= 19 then
            sh.entity.set_map_var("dahlia_effect_spawned", 0)
        end
    elseif state == 25 then
        play(c, 11, false)
        hold(DAHLIA, 1, 0)
        if c.anim_keyframe == 349 then
            c.npc_control_state = 26
            c.state_step = 0
        end
    elseif PLAIN[state] ~= nil then
        play(c, PLAIN[state], false)
        anim_state_reset(c)
    elseif FROM_FIRST_KEYFRAME[state] ~= nil then
        play(c, FROM_FIRST_KEYFRAME[state], true)
        anim_state_reset(c)
    end

    local _, ground_type = sh.entity.ground_at(c.pos_x, c.pos_z)
    local _, pitch0 = sh.entity.footstep_sfx(ground_type, c.chara_id)
    local sfx = on_map("map6_s01") and sound(c, "footstep_map6_s01", 1606) or sound(c, "footstep", 1489)

    state = c.npc_control_state
    local keyframe = c.anim_keyframe
    if state == 1 then
        footstep_pair(c, anim_status(A[2], true), 24, 37, sfx, pitch0)
    elseif state == 8 then
        footstep_pair(c, anim_status(A[4], true), 107, 98, sfx, pitch0)
    elseif state == 11 then
        footstep_pair(c, anim_status(A[7], true), 235, 245, sfx, pitch0)
    elseif state == 7 then
        if keyframe < 47 then
            keyframe_sfx(c, c.anim_status, 46, sound(c, "cue", 1674))
        else
            keyframe_sfx(c, c.anim_status, 47, sound(c, "cue_2", 1675))
        end
    elseif state == 17 then
        if keyframe < 127 then
            keyframe_sfx(c, anim_status(A[17], true), 120, sound(c, "cue", 1674))
        else
            keyframe_sfx(c, anim_status(A[17], true), 127, sound(c, "cue_2", 1675))
        end
    elseif state == 10 then
        -- MapIdx_MAP2_S01 = 11
        keyframe_sfx(c, c.anim_status, 193, sh.save.map_idx() == 11 and sound(c, "cue_map2_s01", 1488) or sound(c, "cue_3", 1496))
    elseif state == 14 then
        if keyframe < 310 then
            footstep_pair(c, c.anim_status, 319, 309, sfx, pitch0)
        else
            footstep_pair(c, c.anim_status, 319, 329, sfx, pitch0)
        end
    elseif state == 19 then
        if keyframe < 175 then
            keyframe_sfx(c, anim_status(A[19], true), 170, sound(c, "cue", 1674))
        else
            keyframe_sfx(c, anim_status(A[19], true), 177, sound(c, "cue_2", 1675))
        end
    end

    control_tail(c, asr(turn_rate, 4))
end

-- Walks against the walls; the height is 0 unless the ground under her is higher (smaller Y).
local function movement_update(c)
    local dt = sh.game.delta_time()
    local dx, dz = step_along(q12_mul(c.move_speed, dt), c.heading_angle)
    local dy = q12_mul(c.fall_speed, dt)
    local _, _, _, ground = sh.entity.wall_detect(c.slot, dx, dy, dz)
    c.pos_x = c.pos_x + dx
    c.pos_y = 0
    c.pos_z = c.pos_z + dz
    if c.pos_y > ground then
        c.pos_y = ground
        c.fall_speed = 0
    end
    sh.entity.bone_translation_set(c.slot, 0, asr(c.pos_x, 4), asr(c.pos_y, 4), asr(c.pos_z, 4))
end

local function init(c)
    collision_reset(c)
    turn_rate = 0
    sh.entity.set_map_var("dahlia_effect_spawned", 0)
    if on_map("map7_s03") then
        hold(ENDING_DAHLIA, 1, 0)
    else
        hold(DAHLIA, 1, 0)
    end
    if on_map("map7_s02") then
        sh.npc.transparent_set(DAHLIA, true)
    end
end

function update(slot)
    local c = record(slot)
    if c.control_state == 0 then
        init(c)
    end
    control_update(c)
    movement_update(c)
    anim_update(c)
end
