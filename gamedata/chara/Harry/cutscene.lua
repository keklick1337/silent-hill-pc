-- Harry's cutscene states: what he does while the player has no control (pickups, interactions, the
-- scenes of a map, the walk and the turns a scene asks for). The original keeps one copy of this
-- machine in every map overlay; the copies
-- differ only in the states they handle and the keyframes their sounds count from, which
-- Harry/map_states.lua lists per map.
--
-- The engine calls cutscene_update(0) once per tick while Harry's control is off, on every map;
-- it returns the turn of the tick (Q12 angle * 16 per second, the map's skeleton update
-- reads it for the turn speed). Harry's record is `c`, the state outside it `h` (same locals as the
-- other Harry modules); extra_state_set and keyframe_sfx come from Harry/state.lua.

local c = record(0)
local h = setmetatable({}, {
    __index = function(_, k)
        return sh.entity.harry(k)
    end,
    __newindex = function(_, k, v)
        sh.entity.set_harry(k, v)
    end,
})

local MOVING = 0x8000      -- harry_flags: he was moving, the stop sounds a step
local FOOT_SECOND = 0x10   -- harry_flags: the second footstep of the cycle has sounded
local FOOT_FIRST = 0x20    -- harry_flags: the first one has

local A = clips("Harry", { walk_forward = 2, run_forward = 3, walk_backward = 4, sidestep_left = 5,
                           sidestep_right = 6, turn_left = 12, turn_right = 13, idle = 26 })
local function st(clip, active)
    return anim_status(clip, active)
end

local function state(name, fallback)
    return sh.chara_state("Harry", name, fallback)
end
local RESET = state("reset", 52)
local WALK_FORWARD, RUN_FORWARD = state("walk_forward", 53), state("run_forward", 54)
local TURN_RIGHT, TURN_LEFT = state("turn_right", 56), state("turn_left", 57)

-- The cutscene's move request (0 run, 1 walk, 3 turn right, 4 turn left) as the state it starts.
local MOVE_TO = { [0] = RUN_FORWARD, [1] = WALK_FORWARD, [3] = TURN_RIGHT, [4] = TURN_LEFT }

local W = {}
for _, name in ipairs({ "KitchenKnife", "SteelPipe", "RockDrill", "Hammer", "Chainsaw", "Katana", "Axe",
                        "Handgun", "HuntingRifle", "Shotgun", "HyperBlaster" }) do
    W[name] = sh.entity.weapon_number(name)
end
local TAP, HOLD, MULTITAP = 0, 1, 2

-- Where the held weapon goes back to by the attack equipped: the hand bone of the melee weapons, of
-- the pistols and of the rifle (model part variants).
local HELD_PART = {}
for _, name in ipairs({ "KitchenKnife", "SteelPipe", "RockDrill", "Hammer", "Chainsaw", "Katana", "Axe" }) do
    for _, input in ipairs({ TAP, HOLD, MULTITAP }) do
        HELD_PART[weapon_attack(W[name], input)] = 2
    end
end
for _, name in ipairs({ "Handgun", "Shotgun", "HyperBlaster" }) do
    HELD_PART[weapon_attack(W[name], TAP)] = 3
end
HELD_PART[weapon_attack(W.HuntingRifle, TAP)] = 4

-- Q12 quantities.
local SPEED_STEP = 1638          -- 0.4 m/s per 30 fps tick
local WALK_TOP, RUN_TOP = 5734, 16384
local CREEP_TOP = 1177           -- 0.2875 m/s: the vine-burning crawl
local RUN_UP = 3072              -- 0.75
local LEAN = 320                 -- 28.2 degrees: the upper body leans into a turn on the spot
local HALF_TURN = 2048

-- This tick: the speed the record moves at, the turn, the heading offset, the clip's first keyframe.
local dt, speed_out, turn, heading, keyframe = 0, 0, 0, 0, 0

local function ts(x)
    return idiv(x * dt, 136)
end

local function brake(d)
    if c.harry_move_speed ~= 0 then
        c.harry_move_speed = c.harry_move_speed - d
        if c.harry_move_speed < 0 then
            c.harry_move_speed = 0
        end
    end
end

local function clamp(x, lo, hi)
    if x < lo then
        return lo
    elseif x > hi then
        return hi
    end
    return x
end

-- The legs and the upper body start `clip` (once per state).
local function clip_both(clip)
    if h.upper_state_step == 0 then
        h.upper_anim_status = st(clip, false)
        h.upper_state_step = h.upper_state_step + 1
    end
    if c.state_step == 0 then
        c.anim_status = st(clip, false)
        c.state_step = c.state_step + 1
    end
end

local function upper_started()
    if h.upper_control_state == 0 then
        h.upper_control_state = 1
    end
end

-- The scene asked Harry to stop: back to the standing state.
local function stop_request()
    if h.cutscene_stop ~= 0 then
        extra_state_set(RESET)
        h.cutscene_stop = 0
    end
end

-- The cutscene's move request starts the state listed for it in `to` (move request -> state).
local function move_request(allowed)
    local target = allowed[h.cutscene_move]
    if target ~= nil then
        extra_state_set(target)
    end
end

local function face_ahead(angle)
    c.player_heading_angle = angle
    heading = c.player_heading_angle
end

-- The held item follows the state: shown at the weapon's hand bone once Harry stands again, else
-- hidden at the empty hand (`lowered`: the locker scene's second part keeps it lower).
local function held_item_follow(lowered)
    if h.state == RESET then
        sh.entity.harry_held_item_show()
        local part = HELD_PART[h.weapon_attack]
        if part ~= nil then
            sh.entity.harry_held_item_attach(part, 1)
        end
    else
        sh.entity.harry_held_item_hide()
        if lowered and h.upper_anim_keyframe >= 958 then
            sh.entity.harry_held_item_attach(5, 1)
        else
            sh.entity.harry_held_item_attach(1, 1)
        end
    end
end

-- A standing state: the speed runs out, the cutscene may turn him or stop him.
local function standing(start_clip)
    brake(ts(SPEED_STEP) * 2)
    turn = 0
    speed_out = c.harry_move_speed
    start_clip()
    upper_started()
    move_request({ [3] = TURN_RIGHT, [4] = TURN_LEFT })
    stop_request()
    face_ahead(0)
end

-- A clip of the map's own (its row id in the map's table of Harry's clips), the stop request after.
local function scene(row, once)
    if once then
        sh.entity.overlay_anim_start(row)
    else
        sh.entity.overlay_anim_set(row)
    end
    stop_request()
end

local START, SET = true, false

-- The most common shape below: a map scene row with the held item following the state (the
-- original's `Switch` helper: shown at the weapon's hand bone when he stands, else hidden).
local function scene_follow(row, once)
    scene(row, once)
    held_item_follow(false)
end

local EFFECT_ONCE = 0x40        -- player flags: a one-shot scene effect has played (PlayerFlag_Unk6)
local SCENE_ENDING_B = state("scene_ending_b", 189)
-- World points of the one-shot scene effects (Q12, computed in C float as the original).
local LIGHTNING_AT = { 61194, -204, 417792 }   -- pos: the alley confrontation
local GUNSHOT_AT = { 575037, -2252, 93224 }    -- pos0: the bar fight

-- The first part of the tick per state: clips, speed, requests. `e` is the map's entry of the state.
local RUN = {
    cutscene_stand = function()
        standing(function() sh.entity.overlay_anim_set(400) end)
    end,
    scene_lighthouse_2 = function()
        standing(function() sh.entity.overlay_anim_start(400) end)
    end,
    reset = function()
        standing(function() clip_both(A.idle) end)
    end,
    walk_forward = function()
        local speed = c.harry_move_speed
        if speed > WALK_TOP then
            c.harry_move_speed = speed - ts(SPEED_STEP) * 2
            if c.harry_move_speed < WALK_TOP then
                c.harry_move_speed = WALK_TOP
            end
        elseif speed < WALK_TOP then
            if c.anim_keyframe >= 2 then
                c.harry_move_speed = c.harry_move_speed + ts(SPEED_STEP)
            end
            c.harry_move_speed = clamp(c.harry_move_speed, 0, WALK_TOP)
        end
        speed_out = c.harry_move_speed
        clip_both(A.walk_forward)
        upper_started()
        move_request({ [0] = RUN_FORWARD, [3] = TURN_RIGHT, [4] = TURN_LEFT })
        stop_request()
        face_ahead(0)
    end,
    run_forward = function()
        local speed = c.harry_move_speed
        if speed > RUN_TOP then
            c.harry_move_speed = speed - ts(SPEED_STEP)
            if c.harry_move_speed < RUN_TOP then
                c.harry_move_speed = RUN_TOP
            end
        elseif speed < RUN_TOP then
            c.harry_move_speed = c.harry_move_speed + ts(RUN_UP)
            c.harry_move_speed = clamp(c.harry_move_speed, 0, RUN_TOP)
        end
        speed_out = c.harry_move_speed
        clip_both(A.run_forward)
        upper_started()
        move_request({ [1] = WALK_FORWARD, [3] = TURN_RIGHT, [4] = TURN_LEFT })
        stop_request()
        face_ahead(0)
    end,
    walk_backward = function()
        speed_out = c.harry_move_speed
        clip_both(A.walk_backward)
        upper_started()
        stop_request()
        face_ahead(HALF_TURN)
    end,
    turn_right = function()
        brake(ts(SPEED_STEP) * 2)
        speed_out = c.harry_move_speed
        turn = dt * 7
        clip_both(A.turn_right)
        upper_started()
        move_request({ [0] = RUN_FORWARD, [1] = WALK_FORWARD, [4] = TURN_LEFT })
        stop_request()
        face_ahead(0)
        h.flex_rot_y = -LEAN
    end,
    turn_left = function()
        brake(ts(SPEED_STEP) * 2)
        speed_out = c.harry_move_speed
        turn = dt * -7
        clip_both(A.turn_left)
        upper_started()
        move_request({ [0] = RUN_FORWARD, [1] = WALK_FORWARD, [3] = TURN_RIGHT })
        stop_request()
        face_ahead(0)
        h.flex_rot_y = LEAN
    end,
    pickup_crouch = function() scene(300, START) end,
    pickup_stand_up = function() scene(301, START) end,
    scene_bar_fight_1 = function(e)
        scene(342, START)
        if e.variant == "hide_held_item" then
            sh.entity.harry_held_item_hide()
        end
    end,
    scene_courtyard_intro = function() scene(350, START) end,
    scene_locker_room = function()
        scene(355, START)
        held_item_follow(false)
    end,
    -- The match: its sound when the clip's playback first reports a step, the item hidden late in it.
    scene_locker_alt = function()
        sh.entity.overlay_anim_start(356)
        c.harry_move_speed = 0
        if sh.entity.harry_anim_playback_state() == 1 and h.match_anim == 0 then
            h.match_anim = 1
            sh.entity.sfx_3d(0, 1356, 128, 0)
        end
        if c.anim_keyframe >= 983 then
            sh.entity.harry_held_item_hide()
        end
    end,
    scene_locker_cat_1 = function(e)
        sh.entity.overlay_anim_start(361)
        if e.variant == "hide_held_item" then
            sh.entity.harry_held_item_hide()
            sh.entity.harry_held_item_attach(1, 1)
        else
            keyframe = 949
            held_item_follow(true)
        end
        stop_request()
    end,
    hatch_climb_up = function()
        scene(358, SET)
        held_item_follow(false)
    end,
    hatch_drop_down = function()
        scene(359, SET)
        held_item_follow(false)
    end,
    scene_cybil_stairs = function() scene(385, START) end,
    scene_descend = function() scene(387, START) end,
    scene_dialogue_1 = function() scene(389, START) end,
    scene_drain_ambush = function() scene(390, START) end,
    scene_hospital_dahlia = function() scene(393, SET) end,
    scene_locker_cat_3 = function(e) scene(398, e.variant == "start_once") end,
    scene_hospital_lisa = function() scene(402, SET) end,

    -- The one-off scenes of the remaining maps, in the states' numeric order.
    push_box = function() scene_follow(312, START) end,
    push_scene_c = function(e)
        scene_follow(331, SET)
        keyframe = e.keyframe or 984
    end,
    push_object = function(e)
        scene_follow(331, SET)
        keyframe = e.keyframe or 984
    end,
    scene_lighthouse_1 = function() scene(343, SET) end,
    cutscene_react = function() scene(340, START) end,
    scene_bar_fight_2 = function() scene(341, START) end,
    cafe_react = function()
        scene(351, SET)
        keyframe = 827
        if c.anim_keyframe > keyframe + 33 and c.anim_keyframe < keyframe + 46 then
            sh.entity.harry_held_item_hide()
        else
            sh.entity.harry_held_item_show()
        end
    end,
    cafe_wake_lie = function() scene(352, SET) end,
    cafe_sit_up = function() scene(353, SET) end,
    burn_vines = function()
        keyframe = 998
        local kf = c.anim_keyframe
        if kf >= keyframe + 3 then
            if kf <= keyframe + 24 then
                if c.harry_move_speed > CREEP_TOP then
                    c.harry_move_speed = c.harry_move_speed - ts(SPEED_STEP) * 2
                    if c.harry_move_speed < CREEP_TOP then
                        c.harry_move_speed = CREEP_TOP
                    end
                elseif c.harry_move_speed < CREEP_TOP then
                    if kf > 998 then
                        c.harry_move_speed = c.harry_move_speed + ts(SPEED_STEP)
                    end
                    c.harry_move_speed = clamp(c.harry_move_speed, 0, CREEP_TOP)
                end
            elseif c.harry_move_speed ~= 0 then
                brake(ts(SPEED_STEP) * 2)
            end
        end
        speed_out = c.harry_move_speed
        sh.entity.overlay_anim_set(360)
        -- Only the local heading turns him here: unlike walk_backward the original does not
        -- write the headingAngle property in this state.
        heading = HALF_TURN
        stop_request()
    end,
    scene_bar_fight_3 = function() scene(439, SET) end,
    scene_locker_cat_2 = function() scene_follow(362, START) end,
    scene_pose_c = function()
        scene(363, SET)
        keyframe = 948
    end,
    cafe_watch_leave = function()
        scene(344, SET)
        sh.entity.harry_held_item_hide()
    end,
    scene_pose_d = function()
        scene(365, START)
        keyframe = 1027
    end,
    scene_pose_e = function() scene(366, START) end,
    cafe_lean_in = function()
        scene(367, START)
        keyframe = 1055
    end,
    scene_picture_card_1 = function() scene_follow(356, START) end,
    scene_picture_card_2 = function() scene_follow(368, START) end,
    scene_phone_call_1 = function()
        if c.anim_keyframe >= 1065 then
            sh.flag_set("M1S02_PhoneReceiverInHand", true)
        end
        if c.anim_keyframe < 1090 then
            sh.entity.harry_held_item_hide()
        else
            sh.entity.harry_held_item_show()
        end
        scene(370, START)
        keyframe = 1062
    end,
    scene_phone_call_2 = function()
        scene(371, START)
        keyframe = 1062
        if c.anim_keyframe >= 1147 then
            sh.flag_set("M1S02_PhoneReceiverInHand", false)
        end
    end,
    interact_scene_a = function() scene_follow(373, START) end,
    interact_scene_b = function() scene_follow(372, START) end,
    scene_lift_platform = function() scene(382, START) end,
    push_object_alt = function() scene_follow(384, START) end,
    lift_platform_alt = function() scene_follow(383, START) end,
    cafe_sit_forward = function() scene(386, START) end,
    held_at_gunpoint = function()
        scene(391, START)
        keyframe = 699
    end,
    scene_alessa_confrontation = function() scene(392, SET) end,
    scene_antique_shop_1 = function()
        scene(394, START)
        keyframe = 750
    end,
    scene_antique_shop_2 = function() scene(395, START) end,
    scene_antique_shop_3 = function()
        scene(396, START)
        keyframe = 779
    end,
    scene_hole_room = function()
        scene(397, START)
        keyframe = 807
    end,
    scene_twinfeeler_1 = function(e)
        scene(399, START)
        keyframe = e.keyframe or 693
    end,
    scene_sqsu_1 = function()
        scene(401, SET)
        keyframe = 846
    end,
    scene_otherworld_resort_a = function()
        scene(403, SET)
        keyframe = 980
    end,
    scene_otherworld_resort_b = function()
        scene(404, SET)
        keyframe = 1002
    end,
    scene_twinfeeler_2 = function() scene(405, SET) end,
    scene_twinfeeler_3 = function() scene(406, SET) end,
    scene_antique_shop_4 = function()
        scene(407, SET)
        keyframe = 967
    end,
    scene_lighthouse_3 = function() scene(343, START) end,
    scene_otherworld_resort_c = function()
        scene(408, START)
        keyframe = 1039
    end,
    turn_around_once = function()
        scene_follow(409, START)
        if c.anim_status & 1 ~= 0 then
            -- The clip mirrors the NPC slot's own animation, offset to its start in the map's
            -- clip table (the original syncs both anim clocks to it, minus its 261 Q12 frames).
            local synced = sh.entity.map_clip_start(c.anim_status - 76) * 4096 + record(1).anim_time
                         - 1069056
            h.upper_anim_time = synced
            c.anim_time = synced
            stop_request()
            c.anim_keyframe = asr(synced, 12)
            h.upper_anim_keyframe = asr(synced, 12)
        end
    end,
    scene_lisa_hospital_1 = function() scene_follow(410, SET) end,
    scene_lisa_hospital_2 = function() scene_follow(411, START) end,
    scene_lisa_hospital_3 = function() scene_follow(412, START) end,
    scene_lisa_hospital_4 = function()
        scene_follow(413, START)
        keyframe = 728
    end,
    scene_lisa_hospital_5 = function() scene_follow(414, SET) end,
    scene_antique_shop_5 = function()
        scene(415, SET)
        keyframe = 982
    end,
    throw_alcohol = function()
        scene_follow(416, START)
        keyframe = 1066
    end,
    throw_alcohol_alt = function()
        scene_follow(417, START)
        keyframe = 1016
    end,
    scene_jelly_beans = function() scene_follow(418, START) end,
    scene_bloodsucker_chains = function() scene_follow(419, SET) end,
    scene_faucet_jets = function() scene_follow(420, SET) end,
    scene_videotape_1 = function(e)
        scene_follow(421, START)
        keyframe = e.keyframe or 1074
    end,
    scene_videotape_2 = function(e)
        scene(422, SET)
        keyframe = e.keyframe or 1108
    end,
    scene_arrive_hospital = function() scene_follow(423, START) end,
    ending_pose_a = function()
        scene(424, START)
        keyframe = 728
    end,
    ending_pose_b = function()
        scene_follow(425, SET)
        keyframe = 793
    end,
    ending_pose_c = function()
        keyframe = 1106
        if c.anim_keyframe == keyframe + 11 or c.anim_keyframe == keyframe + 7 then
            if c.harry_flags & EFFECT_ONCE == 0 then
                sh.entity.effect_spawn(2, LIGHTNING_AT[1], LIGHTNING_AT[2], LIGHTNING_AT[3], 3, 3)
                c.harry_flags = c.harry_flags | EFFECT_ONCE
            end
        end
        if c.anim_keyframe == keyframe + 8 or c.anim_keyframe == keyframe + 19 then
            c.harry_flags = c.harry_flags & ~EFFECT_ONCE
        end
        scene(426, START)
    end,
    ending_pose_d = function() scene_follow(427, SET) end,
    scene_sqsu_2 = function(e)
        scene(428, START)
        keyframe = e.keyframe or 1036
    end,
    scene_blood_pack = function()
        keyframe = 1006
        if c.anim_keyframe > keyframe + 72 then
            sh.entity.harry_held_item_hide()
        else
            sh.entity.harry_held_item_show()
        end
        scene(429, START)
    end,
    scene_lisa_room = function()
        scene_follow(430, START)
        keyframe = 678
    end,
    scene_bloody_lisa_1 = function()
        scene_follow(431, START)
        keyframe = 733
    end,
    scene_bloody_lisa_2 = function()
        scene_follow(432, START)
        keyframe = 793
    end,
    scene_bloody_lisa_3 = function()
        scene_follow(433, START)
        keyframe = 834
    end,
    scene_garage_kaufmann_1 = function()
        scene(434, SET)
        keyframe = 170
    end,
    scene_garage_a = function()
        scene(435, SET)
        keyframe = 1126
    end,
    scene_garage_b = function()
        scene(436, SET)
        keyframe = 1207
        if c.anim_keyframe == 1236 then
            extra_state_set(SCENE_ENDING_B)
        end
    end,
    ending_scene_a = function(e)
        scene_follow(437, START)
        keyframe = e.keyframe or 1237
    end,
    scene_garage_c = function() scene(438, SET) end,
    scene_bar_fight_4 = function()
        keyframe = 953
        if c.anim_keyframe == keyframe + 5 then
            if c.harry_flags & EFFECT_ONCE == 0 then
                sh.entity.harry_cutscene_weapon_effect(W.Handgun, 0x340, c.rot_y)
                sh.entity.effect_spawn(2, GUNSHOT_AT[1], GUNSHOT_AT[2], GUNSHOT_AT[3], 3, 3)
                c.harry_flags = c.harry_flags | EFFECT_ONCE
            end
        end
        if c.anim_keyframe == keyframe + 22 then
            c.harry_flags = c.harry_flags & ~EFFECT_ONCE
        end
        scene(440, SET)
    end,
    scene_bar_fight_5 = function() scene(441, START) end,
    scene_nowhere_a = function(e)
        scene(442, START)
        keyframe = e.keyframe or 1070
    end,
    scene_nowhere_b = function()
        scene(443, START)
        keyframe = 1363
    end,
    scene_garage_kaufmann_2 = function()
        keyframe = 864
        if c.anim_keyframe <= keyframe + 19 then
            sh.entity.harry_held_item_show()
        else
            sh.entity.harry_held_item_hide()
        end
        scene(444, START)
    end,
    ending_scene_b = function()
        scene(445, START)
        keyframe = 1411
        if c.anim_keyframe < 1417 then
            sh.entity.harry_held_item_show()
        else
            sh.entity.harry_held_item_hide()
        end
    end,
    ending_boss_intro = function()
        scene(446, START)
        keyframe = 937
    end,
    ending_scene_c = function()
        scene(447, SET)
        keyframe = 961
    end,
    ending_scene_d = function() scene(448, SET) end,
    ending_scene_e = function() scene(449, SET) end,
    ending_scene_f = function() scene(450, START) end,
    ending_watch_hold = function()
        keyframe = 1087
        sh.entity.harry_held_item_show()
        scene(451, START)
    end,
    ending_scene_g = function()
        sh.entity.harry_held_item_show()
        scene(452, START)
    end,
    ending_scene_h = function()
        sh.entity.harry_held_item_show()
        scene(453, START)
    end,
    ending_scene_i = function()
        keyframe = 1198
        if c.anim_keyframe >= keyframe + 23 then
            sh.entity.harry_held_item_show()
        else
            sh.entity.harry_held_item_hide()
        end
        scene(454, START)
    end,
    ending_scene_j = function()
        sh.entity.harry_held_item_show()
        scene(455, START)
    end,
    ending_scene_k = function()
        sh.entity.harry_held_item_show()
        scene(456, SET)
    end,
    ending_scene_l = function() scene(457, SET) end,
    ending_scene_m = function()
        scene(458, SET)
        keyframe = 1358
    end,
    ending_scene_n = function()
        sh.entity.harry_held_item_show()
        scene(459, START)
    end,
    ending_scene_o = function()
        scene(460, SET)
        keyframe = 1363
    end,
    -- Only the map5_s02 copy counts its sounds from a keyframe (map_states gives it); the
    -- others leave it 0, as their original copies do.
    scene_bar_fight_6 = function() scene(461, START) end,
    motorcycle_key_take = function() scene_follow(462, START) end,
    scene_ending_a = function() scene_follow(463, START) end,
    scene_ending_b = function() scene(464, SET) end,
}

-- Footsteps of the clip `status`: one when it reaches `second`, one at `first`, once each per cycle
-- (the walk's and the turns' quieter steps at the side steps and the turns). True when one sounded.
local QUIET = {
    [st(A.sidestep_right, true)] = true, [st(A.sidestep_left, true)] = true,
    [st(A.turn_left, true)] = true, [st(A.turn_right, true)] = true,
}

local function step_play(status, sfx, pitch)
    if pitch < 32 then
        if QUIET[status] then
            sh.entity.sfx_at(0, sfx, 24, pitch)
            c.harry_step_sound = pitch
        else
            sh.entity.sfx_at(0, sfx, 64, pitch)
            c.harry_step_sound = pitch + 16
        end
    else
        sh.entity.sfx_at(0, sfx, 128, pitch)
        c.harry_step_sound = pitch + 64
    end
end

local function steps(status, first, second, sfx, pitch)
    if c.anim_status ~= status then
        return false
    end
    if c.anim_keyframe >= second then
        if c.harry_flags & FOOT_SECOND == 0 then
            step_play(status, sfx, pitch)
            c.harry_flags = c.harry_flags | FOOT_SECOND
            return true
        end
    else
        c.harry_flags = c.harry_flags & ~FOOT_SECOND
    end
    if c.anim_keyframe >= first then
        if c.harry_flags & FOOT_FIRST == 0 then
            step_play(status, sfx, pitch)
            c.harry_flags = c.harry_flags | FOOT_FIRST
            return true
        end
    else
        c.harry_flags = c.harry_flags & ~FOOT_FIRST
    end
    return false
end

-- The clip playing now, whatever it is (the scenes' own clips).
local function here_steps(first, second, sfx, pitch)
    return steps(c.anim_status, first, second, sfx, pitch)
end

local function halted()
    c.harry_flags = c.harry_flags & ~MOVING
end

local function moving()
    c.harry_flags = c.harry_flags | MOVING
end

local HATCH_STEP = 1423   -- the hatch ladder's rung

-- Forward: the per-map state table is built below, but a sound of the endings needs the
-- state's variant (the map7_s03 copy of ending_scene_a counts another sound).
local map_states

-- The second part per state: sounds. A state without an entry here only clears MOVING.
local SOUNDS = {
    reset = function(sfx, pitch)
        if c.harry_flags & MOVING ~= 0 and c.anim_status == st(A.idle, false) then
            sh.entity.sfx_at(0, sfx, 64, pitch)
            c.harry_step_sound = pitch + 16
            halted()
        end
    end,
    walk_forward = function(sfx, pitch)
        steps(st(A.walk_forward, true), 18, 6, sfx, pitch)
        moving()
    end,
    run_forward = function(sfx, _, run_pitch)
        if steps(st(A.run_forward, true), 31, 41, sfx, run_pitch) then
            c.harry_run_steps = c.harry_run_steps + 1
        end
        moving()
    end,
    turn_right = function(sfx, pitch)
        steps(st(A.turn_right, true), 204, 200, sfx, pitch)
        halted()
    end,
    turn_left = function(sfx, pitch)
        steps(st(A.turn_left, true), 187, 191, sfx, pitch)
        halted()
    end,
    pickup_crouch = function(sfx, pitch)
        here_steps(keyframe + 5, 0, sfx, pitch)
    end,
    pickup_stand_up = function(sfx, pitch)
        here_steps(keyframe + 7, 0, sfx, pitch)
    end,
    hatch_climb_up = function(_, pitch)
        here_steps(keyframe + 35, keyframe + 17, HATCH_STEP, pitch)
        halted()
    end,
    hatch_drop_down = function(_, pitch)
        here_steps(keyframe + 15, keyframe + 31, HATCH_STEP, pitch)
        halted()
    end,
    scene_descend = function(sfx, pitch)
        if c.anim_status & 1 ~= 0 then
            here_steps(keyframe + 11, keyframe + 21, sfx, pitch)
        end
        halted()
    end,
    scene_dialogue_1 = function(sfx, pitch)
        if keyframe + 5 >= c.anim_keyframe then
            here_steps(keyframe + 14, keyframe + 5, sfx, pitch)
        else
            here_steps(keyframe + 14, keyframe + 21, sfx, pitch)
        end
        halted()
    end,
    scene_drain_ambush = function(sfx, pitch)
        if keyframe + 8 >= c.anim_keyframe then
            here_steps(keyframe + 13, keyframe + 8, sfx, pitch)
        else
            here_steps(keyframe + 13, keyframe + 18, sfx, pitch)
        end
        halted()
    end,
    scene_hospital_dahlia = function(sfx, pitch)
        here_steps(keyframe + 34, 0, sfx, pitch)
    end,
    scene_locker_cat_3 = function(sfx, pitch)
        if keyframe + 9 >= c.anim_keyframe then
            here_steps(keyframe + 14, keyframe + 9, sfx, pitch)
        else
            here_steps(keyframe + 14, keyframe + 19, sfx, pitch)
        end
    end,
    push_object = function(sfx, pitch)
        here_steps(keyframe + 5, keyframe + 12, sfx, pitch)
        halted()
    end,
    cafe_react = function()
        if c.anim_status & 1 ~= 0 then
            if c.anim_keyframe < keyframe + 31 then
                keyframe_sfx(c.anim_status, keyframe + 1, 1386)
            else
                keyframe_sfx(c.anim_status, keyframe + 33, 1369)
            end
        end
    end,
    burn_vines = function(sfx, pitch)
        if keyframe + 19 >= c.anim_keyframe then
            here_steps(keyframe + 31, keyframe + 19, sfx, pitch)
        else
            here_steps(keyframe + 31, keyframe + 40, sfx, pitch)
        end
        halted()
    end,
    scene_pose_c = function(sfx, pitch)
        if keyframe + 5 >= c.anim_keyframe then
            here_steps(keyframe + 5, keyframe + 16, sfx, pitch)
        else
            here_steps(keyframe + 74, keyframe + 77, sfx, pitch)
        end
        halted()
    end,
    scene_pose_d = function(sfx, pitch)
        here_steps(keyframe + 6, keyframe + 6, sfx, pitch)
        halted()
    end,
    scene_pose_e = function(sfx, pitch)
        here_steps(keyframe + 9, keyframe + 9, sfx, pitch)
        halted()
    end,
    cafe_lean_in = function(sfx, pitch)
        if c.anim_status & 1 ~= 0 then
            keyframe_sfx(c.anim_status, keyframe + 6, 1385)
            here_steps(keyframe + 18, 0, sfx, pitch)
            halted()
        end
    end,
    scene_phone_call_1 = function(sfx, pitch)
        if keyframe + 4 >= c.anim_keyframe then
            here_steps(keyframe + 10, keyframe + 4, sfx, pitch)
        else
            here_steps(keyframe + 10, keyframe + 13, sfx, pitch)
        end
        keyframe_sfx(c.anim_status, keyframe + 26, 1456)
    end,
    held_at_gunpoint = function(sfx, pitch)
        if keyframe + 9 >= c.anim_keyframe then
            here_steps(keyframe + 9, keyframe + 33, sfx, pitch)
        elseif keyframe + 39 >= c.anim_keyframe then
            here_steps(keyframe + 39, keyframe + 33, sfx, pitch)
        else
            here_steps(keyframe + 46, keyframe + 42, sfx, pitch)
        end
    end,
    scene_antique_shop_1 = function(sfx, pitch)
        if keyframe + 8 >= c.anim_keyframe then
            here_steps(keyframe + 12, keyframe + 8, sfx, pitch)
        else
            here_steps(keyframe + 12, keyframe + 16, sfx, pitch)
        end
        if c.anim_status & 1 ~= 0 then
            keyframe_sfx(c.anim_status, keyframe + 9, 1539)
        end
    end,
    scene_antique_shop_3 = function(sfx, pitch)
        if keyframe + 5 >= c.anim_keyframe then
            here_steps(keyframe + 17, keyframe + 5, sfx, pitch)
        else
            here_steps(keyframe + 17, keyframe + 25, sfx, pitch)
        end
    end,
    scene_hole_room = function(sfx, pitch)
        if keyframe + 5 >= c.anim_keyframe then
            here_steps(keyframe + 5, keyframe + 12, sfx, pitch)
        elseif keyframe + 23 >= c.anim_keyframe then
            here_steps(keyframe + 23, keyframe + 12, sfx, pitch)
        else
            here_steps(keyframe + 5, keyframe + 29, sfx, pitch)
        end
    end,
    scene_twinfeeler_1 = function(sfx, pitch)
        if keyframe + 9 >= c.anim_keyframe then
            here_steps(keyframe + 14, keyframe + 9, sfx, pitch)
        else
            here_steps(keyframe + 14, keyframe + 19, sfx, pitch)
        end
    end,
    scene_sqsu_1 = function(sfx, _, run_pitch)
        if here_steps(80, 87, sfx, run_pitch) then
            c.harry_run_steps = c.harry_run_steps + 1
        end
    end,
    scene_otherworld_resort_a = function(sfx, pitch)
        if keyframe + 9 >= c.anim_keyframe then
            here_steps(keyframe + 16, keyframe + 9, sfx, pitch)
        else
            here_steps(keyframe + 16, keyframe + 22, sfx, pitch)
        end
    end,
    scene_otherworld_resort_b = function(sfx, pitch)
        if keyframe + 5 >= c.anim_keyframe then
            here_steps(keyframe + 17, keyframe + 5, sfx, pitch)
        else
            here_steps(keyframe + 17, keyframe + 26, sfx, pitch)
        end
    end,
    scene_antique_shop_4 = function(sfx, pitch)
        here_steps(keyframe + 14, keyframe + 14, sfx, pitch)
    end,
    scene_otherworld_resort_c = function(sfx, pitch)
        here_steps(keyframe + 5, keyframe + 5, sfx, pitch)
    end,
    scene_lisa_hospital_4 = function(sfx, pitch)
        if c.anim_status & 1 ~= 0 then
            here_steps(keyframe + 18, keyframe + 14, sfx, pitch)
        end
    end,
    scene_antique_shop_5 = function(sfx, pitch)
        here_steps(keyframe + 25, keyframe + 25, sfx, pitch)
    end,
    throw_alcohol = function(sfx, pitch)
        here_steps(keyframe + 46, keyframe + 5, sfx, pitch)
    end,
    throw_alcohol_alt = function(sfx, pitch)
        here_steps(keyframe + 10, keyframe + 10, sfx, pitch)
    end,
    scene_videotape_1 = function(sfx, pitch)
        here_steps(keyframe + 16, keyframe + 10, sfx, pitch)
    end,
    scene_videotape_2 = function(sfx, pitch)
        here_steps(keyframe + 11, keyframe + 11, sfx, pitch)
    end,
    ending_pose_a = function(sfx, pitch)
        if c.anim_keyframe < keyframe + 2 then
            keyframe_sfx(c.anim_status, keyframe, 1626)
        elseif c.anim_keyframe < keyframe + 8 then
            if keyframe_sfx(c.anim_status, keyframe + 6, 1638) then
                sh.pad.effect_level(8, 0x30)
            end
        elseif c.anim_keyframe < keyframe + 41 then
            keyframe_sfx(c.anim_status, keyframe + 3, 1627)
        else
            keyframe_sfx(c.anim_status, keyframe + 42, 1628)
        end
        if keyframe + 43 >= c.anim_keyframe then
            here_steps(keyframe + 55, keyframe + 43, sfx, pitch)
        else
            here_steps(keyframe + 55, keyframe + 59, sfx, pitch)
        end
    end,
    ending_pose_b = function(sfx, pitch)
        if keyframe_sfx(c.anim_status, keyframe + 21, 1638) then
            sh.pad.effect_level(8, 0x30)
        end
        if keyframe + 8 >= c.anim_keyframe then
            here_steps(keyframe + 8, keyframe + 11, sfx, pitch)
        elseif keyframe + 14 >= c.anim_keyframe then
            here_steps(keyframe + 14, keyframe + 11, sfx, pitch)
        else
            here_steps(keyframe + 8, keyframe + 16, sfx, pitch)
        end
    end,
    ending_pose_c = function(sfx, pitch)
        if c.anim_status & 1 ~= 0 then
            if keyframe + 7 >= c.anim_keyframe then
                keyframe_sfx(c.anim_status, keyframe + 7, 1619)
            else
                keyframe_sfx(c.anim_status, keyframe + 11, 1619)
            end
            here_steps(keyframe + 16, keyframe + 16, sfx, pitch)
        end
    end,
    scene_sqsu_2 = function(sfx, pitch)
        here_steps(keyframe + 5, keyframe + 12, sfx, pitch)
    end,
    scene_blood_pack = function(sfx, pitch)
        here_steps(keyframe + 7, keyframe + 21, sfx, pitch)
    end,
    scene_lisa_room = function(sfx, pitch)
        if c.anim_keyframe < 34 then
            if keyframe + 10 >= c.anim_keyframe then
                here_steps(keyframe + 10, keyframe + 16, sfx, pitch)
            elseif keyframe + 21 >= c.anim_keyframe then
                here_steps(keyframe + 21, keyframe + 16, sfx, pitch)
            else
                here_steps(keyframe + 28, keyframe + 25, sfx, pitch)
            end
        elseif keyframe + 16 >= c.anim_keyframe then
            here_steps(keyframe + 34, keyframe + 38, sfx, pitch)
        else
            here_steps(keyframe + 42, keyframe + 46, sfx, pitch)
        end
    end,
    scene_bloody_lisa_1 = function(sfx, pitch, run_pitch)
        if c.anim_keyframe < 59 then
            if keyframe + 9 >= c.anim_keyframe then
                here_steps(keyframe + 9, keyframe + 22, sfx, pitch)
            elseif keyframe + 30 >= c.anim_keyframe then
                here_steps(keyframe + 30, keyframe + 22, sfx, pitch)
            else
                here_steps(keyframe + 52, keyframe + 42, sfx, pitch)
            end
        else
            here_steps(keyframe + 59, keyframe + 59, sfx, run_pitch)
        end
    end,
    scene_bloody_lisa_2 = function(sfx, _, run_pitch)
        here_steps(keyframe + 33, keyframe + 25, sfx, run_pitch)
    end,
    scene_bloody_lisa_3 = function(sfx, _, run_pitch)
        here_steps(keyframe + 12, keyframe + 5, sfx, run_pitch)
    end,
    scene_garage_kaufmann_1 = function(sfx, _, run_pitch)
        here_steps(keyframe + 11, 0, sfx, run_pitch)
    end,
    scene_garage_a = function(sfx, pitch)
        if keyframe + 33 >= c.anim_keyframe then
            here_steps(keyframe + 42, keyframe + 33, sfx, pitch)
        else
            here_steps(keyframe + 42, keyframe + 55, sfx, pitch)
        end
    end,
    scene_garage_b = function(sfx, pitch)
        here_steps(keyframe + 24, keyframe + 22, sfx, pitch)
    end,
    ending_scene_a = function()
        if c.anim_status & 1 ~= 0 then
            -- map7_s03's copy counts the final boss's own sound from the same keyframe
            local sound = map_states()[h.state].variant == "final_boss_sound" and 1671 or 1623
            keyframe_sfx(c.anim_status, keyframe + 60, sound)
        end
    end,
    scene_garage_c = function(sfx, pitch)
        here_steps(keyframe + 6, 0, sfx, pitch)
    end,
    scene_bar_fight_4 = function()
        keyframe_sfx(c.anim_status, keyframe + 5, 1283)
    end,
    scene_nowhere_a = function(sfx, pitch)
        here_steps(keyframe + 10, 0, sfx, pitch)
    end,
    scene_nowhere_b = function(sfx, pitch)
        here_steps(keyframe + 38, keyframe + 22, sfx, pitch)
    end,
    ending_scene_b = function()
        keyframe_sfx(c.anim_status, keyframe + 7, 1629)
    end,
    ending_boss_intro = function(sfx, pitch)
        here_steps(keyframe + 9, keyframe + 14, sfx, pitch)
    end,
    ending_scene_c = function(sfx, pitch)
        here_steps(keyframe + 29, 0, sfx, pitch)
    end,
    ending_scene_f = function(sfx, pitch)
        here_steps(18, 6, sfx, pitch)
    end,
    ending_watch_hold = function(sfx, pitch)
        here_steps(keyframe + 8, keyframe + 19, sfx, pitch)
    end,
    ending_scene_i = function(sfx, pitch)
        here_steps(keyframe + 8, keyframe + 44, sfx, pitch)
    end,
    ending_scene_m = function(sfx, pitch)
        if keyframe + 11 >= c.anim_keyframe then
            here_steps(keyframe + 28, keyframe + 11, sfx, pitch)
        else
            here_steps(keyframe + 28, keyframe + 55, sfx, pitch)
        end
    end,
    ending_scene_o = function(sfx, pitch)
        here_steps(keyframe + 38, keyframe + 22, sfx, pitch)
    end,
    scene_bar_fight_6 = function(sfx, pitch)
        here_steps(0, keyframe + 12, sfx, pitch)
        moving()
    end,
}

-- The map's states by number: {name, keyframe, variant}. Built once per map from Harry/map_states.lua;
-- a map missing there (a mod's) still has the standing state.
local loaded_map, by_number = nil, {}

function map_states()
    local map = sh.map_name()
    if map ~= loaded_map then
        loaded_map = map
        by_number = {}
        local list = harry_map_states[map] or { reset = {} }
        for name, entry in pairs(list) do
            local number = state(name, -1)
            if number < 0 then
                error("Harry/map_states.lua: " .. map .. ": unknown state " .. name)
            end
            by_number[number] = { name = name, keyframe = entry.keyframe, variant = entry.variant }
        end
        by_number[RESET] = by_number[RESET] or { name = "reset" }
    end
    return by_number
end

-- The map's entry of a state number (nil: the map does not handle the state); Harry/scripted.lua reads it.
function cutscene_map_state(number)
    return map_states()[number]
end

-- Whether this module knows every state of the map (a mod's map missing from Harry/map_states.lua
-- or naming an unknown state: Harry stands still there, the engine logs it).
function cutscene_handles(_)
    for _, entry in pairs(map_states()) do
        if RUN[entry.name] == nil then
            return 0
        end
    end
    return 1
end

function cutscene_update(_)
    dt = sh.game.delta_time()
    speed_out, turn, heading, keyframe = 0, 0, 0, 0

    c.harry_step_sound = c.harry_step_sound >> 1
    c.harry_gas_weapon_timer = 0
    sh.entity.weapon_model_set(0, -1)
    c.player_afk_timer = 0
    c.harry_exhaustion = 0

    local entry = map_states()[h.state]
    if entry ~= nil then
        keyframe = entry.keyframe or 0
        RUN[entry.name](entry)
    end

    local _, ground = sh.entity.ground_at(c.pos_x, c.pos_z)
    local sfx, pitch, run_pitch = sh.entity.footstep_sfx(ground)
    entry = map_states()[h.state]
    local sounds = entry ~= nil and SOUNDS[entry.name] or nil
    if sounds ~= nil then
        sounds(sfx, pitch, run_pitch)
    else
        halted()
    end

    c.rot_y = (c.rot_y + asr(turn, 4) + 4096) & 0xFFF
    c.heading_angle = (c.rot_y + heading) & 0xFFF
    c.move_speed = speed_out
    c.fall_speed = c.fall_speed + sh.game.gravity_speed()
    sh.entity.harry_cutscene_pose_update()
    return turn
end
