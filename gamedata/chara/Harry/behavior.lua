-- uses: Harry/controller Harry/state Harry/combat Harry/damage Harry/map_states Harry/cutscene Harry/scripted
-- Harry's legs: walking, running, turning on the spot, side steps, the quick turn, the jump back,
-- stopping, the hand-over between the walk and the run cycles, the skid into a wall and the
-- stumble, footsteps and getting out of breath.
--
-- The engine calls update(0) once per tick where the lower body is updated; the upper body
-- (aiming, attacks, items, damage) meets the legs through the body
-- states (h.lower_body, h.upper_body, h.state) and the upper body's clip. footsteps(0) is the
-- sound and tiredness part alone (the PC's alternate cameras move Harry themselves).
--
-- Integer rules: common.lua. Harry's record is `c` (record fields), the movement
-- state outside it is `h` (sh.entity.harry / set_harry), his controls of the tick come from
-- sh.input.control. A mod changing how Harry moves changes the game: "gameplay": true in mod.json.

local c = record(0)
local h = setmetatable({}, {
    __index = function(_, k)
        return sh.entity.harry(k)
    end,
    __newindex = function(_, k, v)
        sh.entity.set_harry(k, v)
    end,
})

-- Lower body states (h.lower_body). The aimed variants are the plain ones + AIMED.
local NONE, WALK_FORWARD, RUN_FORWARD, RUN_FORWARD_WALL_STOP, WALK_BACKWARD = 0, 1, 2, 3, 4
local SIDESTEP_RIGHT, SIDESTEP_LEFT, RUN_RIGHT, RUN_LEFT, QUICK_TURN_RIGHT, QUICK_TURN_LEFT = 5, 6, 7, 8, 9, 10
local JUMP_BACKWARD, STUMBLE, RUN_LEFT_WALL_STOP, RUN_RIGHT_WALL_STOP = 11, 12, 13, 14
local RUN_LEFT_STUMBLE, RUN_RIGHT_STUMBLE = 15, 16
local AIMED = 20
local AIM, AIM_WALK_FORWARD, AIM_RUN_FORWARD, AIM_WALK_BACKWARD = 20, 21, 22, 24
local AIM_SIDESTEP_RIGHT, AIM_SIDESTEP_LEFT, AIM_RUN_RIGHT, AIM_RUN_LEFT = 25, 26, 27, 28
local AIM_QUICK_TURN_RIGHT, AIM_QUICK_TURN_LEFT, JUMP_BACKWARD_LAND = 29, 30, 31
local ATTACK, RELOAD = 34, 35

-- Upper body states (h.upper_body).
local UPPER_NONE, UPPER_WALK_FORWARD, UPPER_TURN_RIGHT, UPPER_TURN_LEFT, UPPER_RUN_JUMP_BACKWARD = 0, 1, 11, 12, 13
local UPPER_AIM, UPPER_AIM_START_TARGET_LOCK, UPPER_AIM_STOP, UPPER_ATTACK = 19, 22, 24, 25

-- Harry's states (h.state).
local STATE_COMBAT, STATE_FALL_BACKWARD = 1, 4

-- harry_flags bits.
local WALL_STOP_RIGHT = 0x8      -- the skid into the wall starts on the right foot
local FOOT_SECOND = 0x10         -- the second footstep of the cycle has sounded
local FOOT_FIRST = 0x20          -- the first one has (also: the jump back's landing step)
local AIM_RESUME = 0x40          -- the aim pose comes back after a melee swing
local MELEE_SWING = 0x400        -- a melee swing carries on in the attack state
local GUN_ATTACK = 0x800         -- the gun attack plays its alternative clip
local MOVING = 0x8000            -- moving: the step into the idle sounds

-- h.stop_flags: forward or back was let go during this walk.
local STOPPED_FORWARD, STOPPED_BACK = 0x1, 0x2

-- sh.world.speed_zone.
local ZONE_SLOW, ZONE_FAST = 0, 2

local A = clips("Harry", {
    still = 0, walk_forward = 2, run_forward = 3, walk_backward = 4, sidestep_left = 5, sidestep_right = 6,
    run_left = 7, run_right = 8, run_forward_wall_stop_right = 9, run_forward_wall_stop_left = 10,
    run_forward_stumble = 11, turn_left = 12, turn_right = 13, quick_turn_right = 14, quick_turn_left = 15,
    jump_backward = 16, run_left_wall_stop = 18, run_left_stumble = 19, run_right_wall_stop = 20,
    run_right_stumble = 21, idle = 26, idle_exhausted = 27, handgun_aim = 28, handgun_aim_start = 29,
    weapon_attack_2 = 30, handgun_recoil = 31, aim_target_lock_start = 32, powered_weapon_idle = 33,
    aim_resume = 34,
})
local function st(clip, active)
    return anim_status(clip, active)
end
-- The weapon's own clips start here (statuses at and above are the equipped weapon's).
local WEAPON_CLIPS = st(A.handgun_aim_start, false)

local W = {}
for _, name in ipairs({ "KitchenKnife", "SteelPipe", "RockDrill", "Hammer", "Chainsaw", "Katana", "Axe",
                        "Handgun", "HuntingRifle", "Shotgun", "HyperBlaster" }) do
    W[name] = sh.entity.weapon_number(name)
end
local TAP, HOLD = 0, 1
local GUNS = weapon_attack(W.Handgun, TAP)   -- packed attacks below this are melee (or none)

-- The weapon of a packed attack (a mod's weapon's row through the engine, else C remainder: -1 stays -1).
local weapon_of = attack_weapon

local function gun(attack)
    return gun_attack(attack, GUNS)
end

-- Speeds (Q12 m/s) and the quantities of the speed changes.
local SPEED_STEP = 1638          -- 0.4: change per 30 fps tick
local WALK_SLOW, WALK_TOP, BACK_TOP, SIDESTEP_TOP = 3072, 5734, 4710, 5120
local RUN_STRAFE_TOP, RUN_SKID = 13000, 12800

local dt = 0
local input = {}
local stick_x, stick_y, stick_up, stick_down = 0, 0, 0, 0

local function ts(x)
    return idiv(x * dt, 136)
end

-- Slows the legs by `d`; a speed that would go below zero stops.
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

-- How far the move stick is pushed: |y| as the original; with stick_radius (GAMEPLAY CHANGE) the
-- radius of both axes, capped at 127.
local function stick_deflection()
    if sh.game.stick_radius() == 0 then
        return stick_y < 0 and -stick_y or stick_y
    end
    local mag = sh.math.sqrt(stick_x * stick_x + stick_y * stick_y)
    return mag > 127 and 127 or mag
end

local function weapon_ready_keyframe(i)
    local _, ready = sh.entity.harry_weapon_clip(i)
    return ready
end

-- A new lower body state starts its clip and steps over.
local function state_reset(state)
    if h.lower_body ~= state then
        c.state_step = 0
        c.control_state = 0
        c.harry_run_steps = 0
        c.player_afk_timer = 0
        h.stop_flags = 0
    end
end

-- A tap of both step buttons turns Harry round (left when the stick turns left).
local function quick_turn(aimed)
    if h.upper_body == UPPER_ATTACK or input.step_left_tap == 0 or input.step_right_tap == 0 then
        return
    end
    if input.turn_left ~= 0 then
        h.lower_body = aimed + QUICK_TURN_LEFT
    else
        h.lower_body = aimed + QUICK_TURN_RIGHT
    end
end

-- The same from the left side step: towards the right unless the stick turns right... the other way.
local function quick_turn_from_left(aimed)
    if h.upper_body == UPPER_ATTACK or input.step_left_tap == 0 or input.step_right_tap == 0 then
        return
    end
    if input.turn_right ~= 0 then
        h.lower_body = aimed + QUICK_TURN_RIGHT
    else
        h.lower_body = aimed + QUICK_TURN_LEFT
    end
end

-- Turn of this tick from the stick (`rate`: degrees-ish per 30 fps tick at full push). Walking
-- backward steers the other way unless the retreat turn option reverses it.
local function steer(rate)
    local right, left = input.turn_right, input.turn_left
    local backward = h.option_retreat_turn == 0 and
        (h.lower_body == WALK_BACKWARD or h.lower_body == AIM_WALK_BACKWARD)
    if backward then
        if right ~= 0 then
            h.turn_speed = asr(s32(-rate * dt * right), 6)
        elseif left ~= 0 then
            h.turn_speed = asr(s32(rate * dt * left), 6)
        end
    else
        if right ~= 0 then
            h.turn_speed = asr(s32(rate * dt * right), 6)
        elseif left ~= 0 then
            h.turn_speed = asr(s32(-rate * dt * left), 6)
        end
    end
end

-- Walk speed towards a target between `slow` and `top` by the stick (the eased-out stick keeps
-- the last push); it only picks up once the cycle is past `keyframe`.
local function walk_speed(slow, top, keyframe)
    local push = h.stick_hold
    if push == 0 then
        push = stick_deflection()
    end
    local target = slow + idiv((top - slow) * (push - 64), 64)
    if target < c.harry_move_speed then
        c.harry_move_speed = c.harry_move_speed - ts(SPEED_STEP) * 2
        if c.harry_move_speed < target then
            c.harry_move_speed = target
        end
    elseif c.harry_move_speed < target then
        if c.anim_keyframe >= keyframe then
            c.harry_move_speed = c.harry_move_speed + ts(SPEED_STEP)
        end
        c.harry_move_speed = clamp(c.harry_move_speed, 0, target)
    end
end

-- Run speed towards `top`; during the push-off keyframes the stick sets it between `slow` and `top`.
local function run_speed(slow, top, up, down)
    local kf = c.anim_keyframe
    if (kf >= 40 and kf < 46) or (kf >= 30 and kf < 36) then
        local push = h.stick_hold
        if push == 0 then
            push = stick_deflection()
        end
        top = slow + idiv((top - slow) * (push - 64), 64)
    end
    if top < c.harry_move_speed then
        c.harry_move_speed = c.harry_move_speed - down
        if c.harry_move_speed < top then
            c.harry_move_speed = top
        end
    elseif c.harry_move_speed < top then
        c.harry_move_speed = c.harry_move_speed + up
        c.harry_move_speed = clamp(c.harry_move_speed, 0, top)
    end
end

local function run_top(zone)
    if zone == ZONE_FAST then
        return 20480
    elseif zone == ZONE_SLOW then
        return 14336
    end
    return 16384
end

-- Speed gained per tick in the run: quick up to 3.5 m/s, slow above.
local function run_gain(speed)
    if speed < 14336 then
        return ts(3072)
    end
    return asr(dt + (dt < 0 and 3 or 0), 2)
end

-- Looks ahead along the heading for a wall: RUN_FORWARD when one blocks the run (a wall stop
-- ahead), WALK_FORWARD when a ledge or a step down is ahead (a stumble), NONE when the way is
-- free. Two rays, from each side of the body.
local function look_ahead()
    local speed = c.harry_move_speed
    local reach = asr(speed, 3) + 3072
    local height = -2457 - asr(speed, 4)
    local heading = c.heading_angle
    local sin, cos = sh.math.sin(heading), sh.math.cos(heading)
    local side_x = asr(s32(cos * 819), 12)
    local side_z = asr(s32(sin * 819), 12)
    local ahead_x = asr(s32(reach * sin), 12)
    local ahead_z = asr(s32(reach * cos), 12)
    height = height - 1638
    local x, y, z = c.pos_x, c.pos_y, c.pos_z
    local to_y = y + height
    local from_y = y - 1638
    local hit0, dist0, ground0, angle0, height0 =
        sh.world.ray_surface(x + side_x, from_y, z - side_z, x + side_x + ahead_x, to_y, z - side_z + ahead_z)
    if hit0 == 0 then
        return NONE
    end
    local hit1, dist1, _, angle1 =
        sh.world.ray_surface(x - side_x, from_y, z + side_z, x - side_x + ahead_x, to_y, z + side_z + ahead_z)
    if hit1 == 0 then
        return NONE
    end
    h.wall_distance = asr(dist0 + dist1, 1)
    h.wall_ground_type = ground0
    local angle = (asr(angle0 + angle1, 1) + 4096) & 4095
    local delta = s16(abs(angle - heading))
    if delta > 1820 and delta < 2275 then
        if y - 5324 < height0 or ground0 == 0 or ground0 == 12 then
            if y - 1228 >= height0 then
                return RUN_FORWARD
            end
        else
            return WALK_FORWARD
        end
    end
    return NONE
end

-- The blend clip (Still) carries the legs into the next cycle at `keyframe`.
local function blend_into(keyframe, clip)
    sh.entity.clip_link_set(0, st(A.still, false), keyframe, st(clip, true))
end

local function start_clip(status)
    if c.state_step == 0 then
        c.anim_status = status
        c.state_step = c.state_step + 1
    end
end

local function unsigned(x)
    return x & 0xFFFFFFFF
end

-- Stopping a walk or a side step while a melee weapon is up: a swing in progress carries on as
-- the attack state.
local function melee_swing_carries(attack)
    local upper = h.upper_anim_status
    return ((upper == st(A.handgun_aim_start, true) or upper == st(A.weapon_attack_2, true)) and
               attack ~= weapon_attack(W.Chainsaw, TAP) and attack ~= weapon_attack(W.RockDrill, TAP)) or
        upper == st(A.handgun_recoil, true)
end

local function stop_walk(aimed, clear_swing)
    local attack = h.weapon_attack
    if not gun(attack) and aimed ~= 0 and melee_swing_carries(attack) then
        c.harry_flags = c.harry_flags | MELEE_SWING
        c.state_step = 0
        c.control_state = 0
        h.lower_body = ATTACK
    else
        h.lower_body = aimed
        if clear_swing then
            c.harry_flags = c.harry_flags & ~MELEE_SWING
        end
    end
end

-- Jump back: the run button (or its reverse) with back.
local function run_pressed()
    if h.option_walk_run ~= 0 then
        return input.run == 0
    end
    return input.run ~= 0
end

local function jump_back()
    h.lower_body = JUMP_BACKWARD
    h.upper_body = UPPER_RUN_JUMP_BACKWARD
    h.upper_state_step = 0
    h.upper_control_state = 0
end

local function face(angle)
    c.player_heading_angle = angle
    h.heading_offset = angle
end

-- -----------------------------------------------------------------------------------------
-- States.

local function standing(aimed, ahead)
    if c.anim_status == st(A.walk_forward, true) then
        c.state_step = 0
    end
    if aimed ~= 0 then
        brake(ts(SPEED_STEP))
    else
        brake(ts(SPEED_STEP) * 2)
    end

    local attack = h.weapon_attack
    if h.lower_body == NONE then
        if h.upper_body == UPPER_AIM_STOP then
            if h.aiming == 0 and c.state_step == 0 then
                c.anim_status = h.weapon_anim_aim_stop
                c.state_step = c.state_step + 1
            end
        elseif c.harry_exhaustion < 40960 and c.health >= 122880 then
            start_clip(st(A.idle, false))
        else
            start_clip(st(A.idle_exhausted, false))
        end
    else
        if h.upper_body == UPPER_AIM_STOP then
            if h.aiming == 0 and c.state_step == 0 then
                c.anim_status = h.weapon_anim_aim_stop
                c.state_step = c.state_step + 1
            end
        elseif not gun(attack) then
            if (attack == weapon_attack(W.Chainsaw, TAP) or attack == weapon_attack(W.RockDrill, TAP)) and
                c.harry_gas_weapon_timer ~= 0 then
                start_clip(st(A.powered_weapon_idle, false))
            else
                start_clip(st(A.handgun_aim, false))
            end
        elseif c.harry_flags & AIM_RESUME ~= 0 then
            start_clip(st(A.aim_resume, false))
        elseif h.upper_body == UPPER_AIM_START_TARGET_LOCK then
            start_clip(st(A.aim_target_lock_start, false))
        elseif attack == weapon_attack(W.HuntingRifle, TAP) then
            start_clip(st(A.handgun_aim_start, false))
        else
            start_clip(st(A.handgun_aim, false))
        end
        c.harry_flags = c.harry_flags & ~AIM_RESUME
    end

    if h.state == STATE_COMBAT then
        if c.anim_status & 1 ~= 0 and h.upper_anim_status & 1 ~= 0 then
            local kf = c.anim_keyframe
            if c.anim_status >= WEAPON_CLIPS or kf >= weapon_ready_keyframe(0) or kf >= weapon_ready_keyframe(5) then
                if input.forward ~= 0 then
                    h.lower_body = AIM_WALK_FORWARD
                elseif input.back ~= 0 then
                    h.lower_body = AIM_WALK_BACKWARD
                end
            end
        end
        state_reset(aimed)
    else
        if c.anim_status & 1 ~= 0 and h.upper_anim_status & 1 ~= 0 then
            local kf = c.anim_keyframe
            if (aimed == 0 and c.harry_move_speed == 0) or c.anim_status >= WEAPON_CLIPS or
                kf >= weapon_ready_keyframe(0) or kf >= weapon_ready_keyframe(5) then
                if input.forward ~= 0 then
                    -- Running from a stand-still: not with a weapon up (except the steel pipe held
                    -- the other way).
                    local aim_held = (h.option_weapon_control ~= 0 and input.aim == 0) or
                        (h.option_weapon_control == 0 and input.aim ~= 0)
                    if input.run ~= 0 and ahead == NONE and
                        (aimed == 0 or (aim_held and weapon_of(attack) == W.SteelPipe)) then
                        h.lower_body = RUN_FORWARD
                    else
                        h.lower_body = aimed + WALK_FORWARD
                    end
                elseif input.back ~= 0 then
                    h.lower_body = aimed + WALK_BACKWARD
                elseif input.step_right ~= 0 then
                    c.heading_angle = c.heading_angle + 1024
                    ahead = look_ahead()
                    if input.run ~= 0 and aimed == 0 and ahead == NONE then
                        h.lower_body = RUN_RIGHT
                    else
                        h.lower_body = aimed + SIDESTEP_RIGHT
                    end
                elseif input.step_left ~= 0 then
                    c.heading_angle = c.heading_angle - 1024
                    ahead = look_ahead()
                    if input.run ~= 0 and aimed == 0 and ahead == NONE then
                        h.lower_body = RUN_LEFT
                    else
                        h.lower_body = aimed + SIDESTEP_LEFT
                    end
                end
                if aimed == 0 and h.aiming == 0 and run_pressed() and input.back ~= 0 then
                    jump_back()
                end
                if h.lower_body == aimed and h.walk_run_blend == 0 then
                    quick_turn(aimed)
                end
            end
        end
        state_reset(aimed)
        if input.run ~= 0 then
            steer(10)
        else
            steer(7)
        end

        if aimed ~= 0 and h.upper_body == UPPER_AIM then
            local turn = h.turn_speed
            if turn ~= 0 then
                -- Turning with a weapon up shuffles the feet: a speed from the turn, per weapon (a mod's weapon as
                -- the base weapon it is like).
                local attack = attack
                local row = mod_attack(attack)
                if row and weapon_like(row.weapon) >= 0 then
                    attack = weapon_attack(weapon_like(row.weapon), row.input)
                end
                if attack == weapon_attack(W.KitchenKnife, TAP) then
                    c.harry_move_speed = unsigned(s32(turn * 0x465)) >> 9
                elseif attack == weapon_attack(W.Chainsaw, TAP) or attack == weapon_attack(W.Katana, TAP) or
                    attack == weapon_attack(W.Axe, TAP) then
                    c.harry_move_speed = unsigned(s32(turn * 0x15F9)) >> 11
                elseif attack == weapon_attack(W.SteelPipe, TAP) or attack == weapon_attack(W.Hammer, TAP) then
                    c.harry_move_speed = unsigned(s32(turn * 0xD2F)) >> 10
                elseif attack == weapon_attack(W.RockDrill, TAP) or attack == weapon_attack(W.Handgun, TAP) or
                    attack == weapon_attack(W.HuntingRifle, TAP) or attack == weapon_attack(W.Shotgun, TAP) or
                    attack == weapon_attack(W.HyperBlaster, TAP) then
                    c.harry_move_speed = asr(s32(-s32(turn * 0x87F0)), 14)
                end
                if dt ~= 0 then
                    c.harry_move_speed = idiv(c.harry_move_speed * 0x88, dt)
                end
                if h.turn_speed ~= 0 then
                    c.player_afk_timer = 0
                end
            end
        elseif h.turn_speed ~= 0 then
            c.player_afk_timer = 0
        end

        -- Turning on the spot.
        local idle = c.anim_status == st(A.idle, true) or c.anim_status == st(A.idle_exhausted, true)
        if input.turn_left ~= 0 and c.state_step == 1 and idle then
            c.state_step = 2
            c.anim_status = st(A.turn_left, false)
        elseif input.turn_right ~= 0 and c.state_step == 1 and idle then
            c.state_step = 2
            c.anim_status = st(A.turn_right, false)
        end
        if input.turn_left == 0 and input.turn_right == 0 and c.state_step == 2 and
            (c.anim_status == st(A.turn_right, true) or c.anim_status == st(A.turn_left, true)) then
            c.anim_status = st(A.idle, false)
            c.state_step = 0
        end
    end

    if c.harry_move_speed == 0 or input.turn_left ~= 0 or input.turn_right ~= 0 then
        face(0)
    end
end

local function walking_forward(aimed, ahead)
    if input.forward == 0 then
        h.stop_flags = h.stop_flags | STOPPED_FORWARD
    end
    if h.stop_flags & STOPPED_FORWARD ~= 0 and h.lower_body < AIMED and h.upper_body ~= UPPER_AIM_STOP then
        brake(ts(SPEED_STEP) * 2)
    else
        if stick_y <= -64 then
            h.stick_hold = stick_deflection()
            walk_speed(WALK_SLOW, WALK_TOP, 2)
        else
            if h.stick_hold ~= 0 then
                walk_speed(WALK_SLOW, WALK_TOP, 2)
            elseif c.harry_move_speed > WALK_TOP then
                c.harry_move_speed = c.harry_move_speed - ts(SPEED_STEP) * 2
                if c.harry_move_speed < WALK_TOP then
                    c.harry_move_speed = WALK_TOP
                end
            elseif c.harry_move_speed < WALK_TOP then
                if c.anim_keyframe >= 2 then
                    c.harry_move_speed = c.harry_move_speed + ts(SPEED_STEP)
                end
                c.harry_move_speed = clamp(c.harry_move_speed, 0, WALK_TOP)
            end
            if stick_up ~= 0 then
                h.stick_hold = 0
            end
        end
    end
    start_clip(st(A.walk_forward, false))

    local kf = c.anim_keyframe
    local step_down = (kf >= 10 and kf <= 11) or kf == 22 or kf == 21
    if h.state == STATE_COMBAT then
        if h.stop_flags & STOPPED_FORWARD ~= 0 then
            if (h.lower_body < AIMED and h.upper_body ~= UPPER_AIM_STOP) or step_down then
                h.lower_body = AIM
            end
        end
        state_reset(aimed | 1)
        if h.weapon_attack ~= weapon_attack(W.HyperBlaster, TAP) then
            steer(5)
        end
    else
        if h.stop_flags & STOPPED_FORWARD == 0 then
            if input.run ~= 0 and aimed == 0 and ahead == NONE and
                (h.upper_body == UPPER_WALK_FORWARD or h.upper_body == UPPER_AIM_STOP) then
                -- Into the run on a foot that lands: the blend joins the run cycle there.
                if kf >= 10 and kf <= 11 then
                    h.lower_body = RUN_FORWARD
                    blend_into(36, A.run_forward)
                    c.harry_flags = c.harry_flags | FOOT_FIRST
                elseif kf >= 21 and kf <= 22 then
                    h.lower_body = RUN_FORWARD
                    blend_into(26, A.run_forward)
                end
            end
        elseif (h.lower_body < AIMED and h.upper_body ~= UPPER_AIM_STOP) or step_down then
            stop_walk(aimed, true)
        end
        if h.lower_body == aimed + WALK_FORWARD and h.walk_run_blend == 0 then
            quick_turn(aimed)
        end
        state_reset(aimed | 1)
        steer(5)
    end
    face(0)
    if h.lower_body == RUN_FORWARD then
        c.anim_status = st(A.still, false)
        c.state_step = c.state_step + 1
        h.walk_run_blend = 1
    end
end

-- Running forward into a wall or a drop ahead (`ahead`, look_ahead) ends in the wall stop or the
-- stumble once the way has been free for 10 m; otherwise letting go of run or forward walks.
local function run_to_walk(kf)
    if kf >= 30 and kf <= 31 then
        h.lower_body = WALK_FORWARD
        blend_into(8, A.walk_forward)
    elseif kf >= 41 and kf <= 42 then
        h.lower_body = WALK_FORWARD
        blend_into(20, A.walk_forward)
    end
end

local function running_forward(ahead, travelled)
    c.harry_exhaustion = c.harry_exhaustion + dt
    local zone = sh.world.speed_zone(c.pos_x, c.pos_z)
    if stick_y <= -64 then
        h.stick_hold = stick_deflection()
        run_speed(8192, run_top(zone), run_gain(c.harry_move_speed), ts(SPEED_STEP))
    else
        if h.stick_hold ~= 0 then
            run_speed(8192, run_top(zone), run_gain(c.harry_move_speed), ts(SPEED_STEP))
        elseif c.harry_move_speed > run_top(zone) then
            c.harry_move_speed = c.harry_move_speed - ts(SPEED_STEP)
            if c.harry_move_speed < run_top(zone) then
                c.harry_move_speed = run_top(zone)
            end
        elseif c.harry_move_speed < run_top(zone) then
            c.harry_move_speed = c.harry_move_speed + run_gain(c.harry_move_speed)
            c.harry_move_speed = clamp(c.harry_move_speed, 0, run_top(zone))
        end
        if stick_up ~= 0 then
            h.stick_hold = 0
        end
    end
    start_clip(st(A.run_forward, false))

    local kf = c.anim_keyframe
    if (kf == 43 or kf == 33) and c.pos_y == c.player_ground_height then
        c.fall_speed = -5120
    end

    if h.upper_body ~= UPPER_AIM_START_TARGET_LOCK and c.anim_status == st(A.run_forward, true) then
        local free_run = unsigned(c.harry_run_distance) >= 40960
        if ahead == WALK_FORWARD then
            if free_run then
                h.lower_body = STUMBLE
            else
                run_to_walk(kf)
            end
        elseif ahead == RUN_FORWARD then
            -- Only when the wall really stopped him (moved under half the step he meant to make).
            if free_run and travelled < asr(h.intended_step, 1) then
                h.lower_body = RUN_FORWARD_WALL_STOP
            else
                run_to_walk(kf)
            end
        elseif input.run == 0 or input.forward == 0 then
            if input.forward ~= 0 then
                run_to_walk(kf)
            elseif c.harry_run_steps >= 5 and c.harry_move_speed >= RUN_SKID then
                -- Let go at speed: the skid, on the foot that is down.
                if kf >= 33 and kf <= 34 then
                    h.lower_body = RUN_FORWARD_WALL_STOP
                    c.harry_flags = c.harry_flags & ~WALL_STOP_RIGHT
                elseif kf >= 43 and kf <= 44 then
                    h.lower_body = RUN_FORWARD_WALL_STOP
                    c.harry_flags = c.harry_flags | WALL_STOP_RIGHT
                end
            else
                run_to_walk(kf)
            end
        end
    end

    if h.lower_body ~= RUN_FORWARD then
        state_reset(RUN_FORWARD)
    end
    steer(4)
    h.heading_offset = 0
    c.player_heading_angle = 0
    if h.lower_body == WALK_FORWARD then
        c.anim_status = st(A.still, false)
        c.state_step = c.state_step + 1
        h.walk_run_blend = 1
    end
end

local function wall_stop()
    if c.harry_move_speed ~= 0 then
        brake(asr(ts(SPEED_STEP), 1))
    end
    if c.harry_flags & WALL_STOP_RIGHT ~= 0 then
        start_clip(st(A.run_forward_wall_stop_right, false))
    else
        start_clip(st(A.run_forward_wall_stop_left, false))
    end
    if h.upper_body ~= UPPER_AIM_START_TARGET_LOCK then
        local s, kf = c.anim_status, c.anim_keyframe
        if (s == st(A.run_forward_wall_stop_left, true) and kf >= 168) or
            (s == st(A.run_forward_wall_stop_right, true) and kf >= 158) then
            h.lower_body = NONE
            state_reset(RUN_FORWARD_WALL_STOP)
        end
        steer(4)
    end
end

local function walking_backward(aimed)
    if input.back == 0 then
        h.stop_flags = h.stop_flags | STOPPED_BACK
    end
    if h.stop_flags & STOPPED_BACK ~= 0 and h.lower_body < AIMED and h.upper_body ~= UPPER_AIM_STOP then
        brake(ts(SPEED_STEP) * 2)
    elseif stick_y >= 64 then
        h.stick_hold = stick_deflection()
        walk_speed(WALK_SLOW, BACK_TOP, 2)
    else
        if h.stick_hold ~= 0 then
            walk_speed(WALK_SLOW, BACK_TOP, 2)
        elseif c.harry_move_speed > BACK_TOP then
            c.harry_move_speed = c.harry_move_speed - ts(SPEED_STEP) * 2
            if c.harry_move_speed < BACK_TOP then
                c.harry_move_speed = BACK_TOP
            end
        elseif c.harry_move_speed < BACK_TOP then
            if c.anim_keyframe >= 2 then
                c.harry_move_speed = c.harry_move_speed + ts(SPEED_STEP)
            end
            c.harry_move_speed = clamp(c.harry_move_speed, 0, BACK_TOP)
        end
        if stick_down ~= 0 then
            h.stick_hold = 0
        end
    end
    start_clip(st(A.walk_backward, false))

    local kf = c.anim_keyframe
    local step_down = (kf >= 56 and kf <= 57) or kf == 67 or kf == 66
    if h.state == STATE_COMBAT then
        if h.stop_flags & STOPPED_BACK ~= 0 then
            if (h.lower_body < AIMED and h.upper_body ~= UPPER_AIM_STOP) or step_down then
                h.lower_body = AIM
            end
        end
        state_reset(aimed + WALK_BACKWARD)
        if h.weapon_attack ~= weapon_attack(W.HyperBlaster, TAP) then
            steer(5)
        end
    else
        if h.stop_flags & STOPPED_BACK == 0 then
            if ((kf >= 66 and kf <= 67) or kf == 46 or kf == 47) and aimed == 0 and h.upper_body ~= UPPER_AIM_STOP then
                if run_pressed() and input.back ~= 0 then
                    jump_back()
                end
            end
        elseif (h.lower_body < AIMED and h.upper_body ~= UPPER_AIM_STOP) or step_down then
            stop_walk(aimed, true)
        end
        if h.lower_body == aimed + WALK_BACKWARD and h.walk_run_blend == 0 then
            quick_turn(aimed)
        end
        state_reset(aimed + WALK_BACKWARD)
        steer(4)
    end
    face(2048)
end

-- One side step; `side` describes which (the clip, its keyframes, the run it turns into).
local function side_step(aimed, ahead, side)
    if c.harry_move_speed > SIDESTEP_TOP then
        c.harry_move_speed = c.harry_move_speed - ts(SPEED_STEP)
        if c.harry_move_speed < SIDESTEP_TOP then
            c.harry_move_speed = SIDESTEP_TOP
        end
    else
        local kf = c.anim_keyframe
        if kf >= side.push and kf <= side.push + 11 then
            c.harry_move_speed = c.harry_move_speed + ts(SPEED_STEP)
        elseif kf >= side.push + 12 then
            c.harry_move_speed = c.harry_move_speed - ts(SPEED_STEP)
        end
        c.harry_move_speed = clamp(c.harry_move_speed, 0, SIDESTEP_TOP)
    end
    start_clip(st(side.clip, false))

    if c.anim_status == st(side.clip, true) and c.anim_keyframe >= side.done then
        if input[side.button] == 0 then
            stop_walk(aimed, false)
        elseif input.run ~= 0 and aimed == 0 and ahead == NONE then
            if h.upper_body ~= UPPER_AIM_STOP then
                h.lower_body = side.run
            end
        end
    end
    side.turn(aimed)
    state_reset(aimed + side.state)
    steer(3)
    face(side.heading)
end

local STEP_RIGHT = { clip = A.sidestep_right, push = 100, done = 117, button = "step_right", run = RUN_RIGHT,
                     state = SIDESTEP_RIGHT, heading = 1024, turn = quick_turn }
local STEP_LEFT = { clip = A.sidestep_left, push = 75, done = 92, button = "step_left", run = RUN_LEFT,
                    state = SIDESTEP_LEFT, heading = -1024, turn = quick_turn_from_left }

-- Running sideways; ends at the wall or the drop ahead, or when run or the step is let go.
local function run_sideways(ahead, side)
    c.harry_exhaustion = c.harry_exhaustion + dt
    if c.harry_move_speed > RUN_STRAFE_TOP then
        c.harry_move_speed = c.harry_move_speed - ts(SPEED_STEP)
        if c.harry_move_speed < RUN_STRAFE_TOP then
            c.harry_move_speed = RUN_STRAFE_TOP
        end
    elseif c.harry_move_speed < RUN_STRAFE_TOP then
        c.harry_move_speed = c.harry_move_speed + ts(3072)
        c.harry_move_speed = clamp(c.harry_move_speed, 0, RUN_STRAFE_TOP)
    end
    start_clip(st(side.clip, false))

    local kf = c.anim_keyframe
    if (kf == side.land[1] or kf == side.land[2]) and c.pos_y == c.player_ground_height then
        c.fall_speed = -4096
    end

    if h.upper_body ~= UPPER_AIM_START_TARGET_LOCK then
        local done = c.anim_status == st(side.clip, true) and kf >= side.done
        local free_run = unsigned(c.harry_run_distance) >= 40960
        local held = input.run ~= 0 and input[side.button] ~= 0
        if ahead == WALK_FORWARD then
            if free_run then
                h.lower_body = side.stumble
            elseif done then
                h.lower_body = NONE
            end
        elseif ahead == RUN_FORWARD then
            if free_run then
                h.lower_body = side.wall_stop
            elseif done then
                h.lower_body = NONE
            end
        elseif c.harry_run_steps >= 5 and c.harry_move_speed >= RUN_SKID then
            if side.skid(kf) and not held then
                h.lower_body = side.wall_stop
            end
        elseif done and not held then
            h.lower_body = NONE
        end
    end
    state_reset(side.state)
    steer(4)
    face(side.heading)
end

local RUN_TO_RIGHT = { clip = A.run_right, land = { 139, 145 }, done = 147, button = "step_right",
                       stumble = RUN_RIGHT_STUMBLE, wall_stop = RUN_RIGHT_WALL_STOP, state = RUN_RIGHT,
                       heading = 1024, skid = function(kf) return kf >= 144 end }
local RUN_TO_LEFT = { clip = A.run_left, land = { 125, 132 }, done = 132, button = "step_left",
                      stumble = RUN_LEFT_STUMBLE, wall_stop = RUN_LEFT_WALL_STOP, state = RUN_LEFT,
                      heading = -1024, skid = function(kf) return kf > 128 end }

-- The quick turn: half a turn at the turn's own pace, then on into whatever is held.
local function quick_turning(aimed, side)
    h.heading_offset = 0
    brake(ts(2048))
    if c.control_state == 0 then
        c.harry_quick_turn_heading = c.rot_y
    end
    start_clip(st(side.clip, false))
    if c.control_state == 0 then
        c.control_state = c.control_state + 1
    end
    if c.anim_status == st(side.clip, true) and c.anim_keyframe >= side.spin then
        h.turn_speed = side.sign * dt * 24
    else
        h.turn_speed = 0
    end

    local margin = asr(s32(dt * 24), 4)
    local turned = abs(c.harry_quick_turn_heading - c.rot_y)
    if turned > 2048 - margin and turned < margin + 2048 then
        c.rot_y = c.harry_quick_turn_heading + 2048
        c.harry_move_speed = WALK_TOP
        h.turn_speed = 0
        if c.anim_keyframe >= side.ready then
            local run = input.run ~= 0 and aimed == 0
            local moved = true
            if input.forward ~= 0 then
                h.lower_body = run and RUN_FORWARD or aimed + WALK_FORWARD
            elseif input.back ~= 0 then
                h.lower_body = run and JUMP_BACKWARD or aimed + WALK_BACKWARD
            elseif input.step_right ~= 0 then
                c.heading_angle = c.heading_angle + 1024
                h.lower_body = run and RUN_RIGHT or aimed + SIDESTEP_RIGHT
            elseif input.step_left ~= 0 then
                c.heading_angle = c.heading_angle - 1024
                h.lower_body = run and RUN_LEFT or aimed + SIDESTEP_LEFT
            elseif c.anim_keyframe >= side.done then
                h.lower_body = aimed
            else
                moved = false
            end
            if moved then
                c.state_step = 0
                c.control_state = 0
            end
        end
    end
    face(0)
end

local TURN_RIGHT = { clip = A.quick_turn_right, spin = 206, ready = 213, done = 216, sign = 1 }
local TURN_LEFT = { clip = A.quick_turn_left, spin = 219, ready = 226, done = 229, sign = -1 }

local function jumping_backward(aimed)
    start_clip(st(A.jump_backward, false))
    local s = c.anim_status
    if s >= st(A.jump_backward, false) and s <= st(A.jump_backward, true) and c.anim_keyframe < 245 then
        if c.control_state == 0 then
            c.fall_speed = -8192
        end
        c.control_state = c.control_state + 1
        c.harry_move_speed = 9216
        h.step_speed = 9216
    else
        brake(ts(SPEED_STEP) * 2)
        h.step_speed = c.harry_move_speed
    end
    if c.anim_status == st(A.jump_backward, true) and c.anim_keyframe == 246 then
        -- Landed lower than he left (0.65 m and more): off a ledge, he falls.
        if c.player_ground_height - c.pos_y >= 2662 then
            h.state = STATE_FALL_BACKWARD
            c.state_step = 0
            c.control_state = 0
            h.upper_state_step = 0
            h.upper_control_state = 0
            h.upper_body = UPPER_NONE
            h.lower_body = NONE
            c.harry_move_speed = 5120
        else
            h.lower_body = aimed
            c.state_step = 0
            c.control_state = 0
            c.fall_speed = 0
        end
    end
    face(2048)
end

local function stumbling()
    brake(idiv(ts(SPEED_STEP) * 2, 3))
    if h.wall_distance <= 2048 then
        brake(ts(SPEED_STEP) * 4)
    end
    start_clip(st(A.run_forward_stumble, false))
    if c.anim_status == st(A.run_forward_stumble, true) and c.anim_keyframe == 179 then
        h.lower_body = NONE
        state_reset(STUMBLE)
    end
end

-- The skid of a sideways run into a wall.
local function side_wall_stop(aimed, clip, keyframe, state, heading)
    brake(asr(ts(SPEED_STEP), 1))
    start_clip(st(clip, false))
    if h.upper_body ~= UPPER_AIM_START_TARGET_LOCK then
        if c.anim_status == st(clip, true) and c.anim_keyframe >= keyframe then
            h.lower_body = aimed
            state_reset(state)
        end
        steer(2)
    end
    if h.lower_body == NONE then
        face(0)
    else
        face(heading)
    end
end

-- The stumble of a sideways run over a drop.
local function side_stumble(clip, keyframe, state, heading)
    brake(asr(ts(SPEED_STEP), 2))
    if h.wall_distance < 1393 then
        c.harry_move_speed = 0
    end
    start_clip(st(clip, false))
    if c.anim_status == st(clip, true) and c.anim_keyframe == keyframe then
        h.lower_body = NONE
        state_reset(state)
    end
    if h.lower_body ~= NONE then
        face(heading)
    else
        face(0)
    end
end

-- The legs under an attack: the katana lunges, the clip follows the weapon, and walking resumes
-- (aimed) once the swing allows it.
local function attacking()
    local attack = h.weapon_attack
    local katana = not gun(attack) and weapon_of(attack) == W.Katana
    if katana then
        if attack == weapon_attack(W.Katana, HOLD) then
            local start = sh.entity.harry_weapon_clip(h.weapon_clip)
            if c.harry_move_speed == 0 and h.upper_anim_keyframe >= start + 7 then
                c.harry_move_speed = 20480
                h.heading_offset = 0
            end
        elseif c.state_step == 0 and input.attack == 0 then
            c.harry_move_speed = 20480
            h.heading_offset = 0
        end
    end
    if katana then
        brake(ts(0x444))
    else
        brake(ts(SPEED_STEP))
    end

    if h.target_npc == -1 or not gun(attack) then
        local clip = h.weapon_anim_attack
        if gun(attack) then
            if c.harry_flags & GUN_ATTACK ~= 0 then
                start_clip(clip - 12)
            else
                start_clip(clip)
            end
        elseif c.harry_flags & MELEE_SWING ~= 0 then
            if attack == weapon_attack(W.Chainsaw, TAP) or attack == weapon_attack(W.RockDrill, TAP) then
                start_clip(clip)
            else
                start_clip(st(A.handgun_aim, false))
            end
            if c.anim_keyframe >= weapon_ready_keyframe(0) or c.anim_keyframe >= weapon_ready_keyframe(5) then
                c.anim_status = h.upper_anim_status
                c.anim_keyframe = h.upper_anim_keyframe
                c.anim_time = h.upper_anim_time
                c.state_step = c.state_step + 1
            end
        elseif attack == weapon_attack(W.RockDrill, TAP) then
            local direction = h.rock_drill_direction
            if direction == 1 then
                start_clip(clip + 2)
            elseif direction == -1 then
                start_clip(clip + 4)
            else
                start_clip(clip)
            end
        else
            local upper = h.upper_anim_status
            if upper == st(A.weapon_attack_2, true) then
                c.anim_status = upper
                c.anim_keyframe = h.upper_anim_keyframe
                c.anim_time = h.upper_anim_time
                c.state_step = c.state_step + 1
            elseif input.attack ~= 0 or upper == st(A.handgun_aim_start, true) then
                start_clip(clip - 4)
            elseif input.shoot ~= 0 or upper == st(A.handgun_recoil, true) then
                start_clip(clip)
            end
        end
    else
        start_clip(h.weapon_anim_attack_hold - 1)
    end

    local id = weapon_of(attack)
    if gun(attack) or (id ~= W.SteelPipe and id ~= W.Hammer and id ~= W.RockDrill and id ~= W.Katana) then
        if c.anim_status & 1 ~= 0 and h.upper_anim_status & 1 ~= 0 and
            (c.anim_status >= WEAPON_CLIPS or c.anim_keyframe >= weapon_ready_keyframe(0)) then
            if input.forward == 0 then
                if input.back ~= 0 then
                    if c.harry_move_speed == 0 then
                        h.lower_body = AIM_WALK_BACKWARD
                    end
                elseif h.state ~= STATE_COMBAT then
                    if input.step_right == 0 then
                        if input.step_left ~= 0 then
                            h.lower_body = AIM_SIDESTEP_LEFT
                        end
                    else
                        h.lower_body = AIM_SIDESTEP_RIGHT
                    end
                end
            else
                h.lower_body = AIM_WALK_FORWARD
            end
            if h.lower_body ~= ATTACK then
                state_reset(AIM)
            end
        end
    end
end

local function reloading()
    start_clip(st(A.handgun_recoil, false))
    if c.anim_status & 1 ~= 0 and h.upper_anim_status & 1 ~= 0 and
        (c.anim_status >= WEAPON_CLIPS or c.anim_keyframe >= weapon_ready_keyframe(0)) then
        if input.forward ~= 0 then
            h.lower_body = AIM_WALK_FORWARD
        elseif input.back ~= 0 then
            if c.harry_move_speed == 0 then
                h.lower_body = AIM_WALK_BACKWARD
            end
        elseif h.state ~= STATE_COMBAT then
            if input.step_right ~= 0 then
                h.lower_body = AIM_SIDESTEP_RIGHT
            elseif input.step_left ~= 0 then
                h.lower_body = AIM_SIDESTEP_LEFT
            end
        end
        if h.lower_body ~= RELOAD then
            state_reset(AIM)
        end
    end
end

-- -----------------------------------------------------------------------------------------
-- Footsteps and breath.

local QUIET_STEPS = {
    [st(A.sidestep_right, true)] = true, [st(A.sidestep_left, true)] = true,
    [st(A.turn_left, true)] = true, [st(A.turn_right, true)] = true,
}

local function step_sound_play(status, sfx, pitch)
    if pitch < 32 then
        if QUIET_STEPS[status] then
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

-- A footstep of the clip `status` when it reaches `second` and one when it reaches `first`
-- (once each per cycle); true when one sounded.
local function step_sound(status, first, second, sfx, pitch)
    if c.anim_status ~= status then
        return false
    end
    local kf = c.anim_keyframe
    if kf >= second then
        if c.harry_flags & FOOT_SECOND == 0 then
            step_sound_play(status, sfx, pitch)
            c.harry_flags = c.harry_flags | FOOT_SECOND
            return true
        end
    else
        c.harry_flags = c.harry_flags & ~FOOT_SECOND
    end
    if kf >= first then
        if c.harry_flags & FOOT_FIRST == 0 then
            step_sound_play(status, sfx, pitch)
            c.harry_flags = c.harry_flags | FOOT_FIRST
            return true
        end
    else
        c.harry_flags = c.harry_flags & ~FOOT_FIRST
    end
    return false
end

local function run_step(clip, first, second, sfx, pitch)
    if step_sound(st(clip, true), first, second, sfx, pitch) then
        c.harry_run_steps = c.harry_run_steps + 1
    end
end

local function moving()
    c.harry_flags = c.harry_flags | MOVING
end

local function halted()
    c.harry_flags = c.harry_flags & ~MOVING
end

local EXHAUSTION_MAX = 143360

function footsteps(_)
    dt = sh.game.delta_time()
    local sfx, walk_pitch, run_pitch = sh.entity.footstep_sfx(h.floor_ground_type)
    local lower = h.lower_body
    if lower ~= JUMP_BACKWARD and lower ~= RELOAD then
        h.step_speed = c.harry_move_speed
    end

    -- Running tires him; standing still or a stop recovers twice as fast as walking.
    if lower == RUN_FORWARD or lower == RUN_RIGHT or lower == RUN_LEFT or lower == AIM_RUN_FORWARD or
        lower == AIM_RUN_RIGHT or lower == AIM_RUN_LEFT then
        if c.anim_status & 1 ~= 0 and c.anim_status >= st(A.run_forward, true) then
            c.harry_exhaustion = c.harry_exhaustion + dt
        end
    elseif lower == NONE or lower == RUN_FORWARD_WALL_STOP or lower == STUMBLE or lower == RUN_LEFT_STUMBLE or
        lower == RUN_RIGHT_STUMBLE or lower == AIM then
        c.harry_exhaustion = c.harry_exhaustion - dt * 2
    else
        c.harry_exhaustion = c.harry_exhaustion - dt
    end
    c.harry_exhaustion = clamp(c.harry_exhaustion, 0, EXHAUSTION_MAX)

    if c.anim_status == st(A.idle_exhausted, true) then
        if c.harry_exhaustion < 40960 and c.health >= 122880 then
            c.state_step = 0
            c.control_state = 0
            h.upper_state_step = 0
            h.upper_control_state = 0
        end
    end

    local kf = c.anim_keyframe
    if lower == NONE or lower == AIM then
        if h.upper_body == UPPER_TURN_RIGHT then
            step_sound(st(A.turn_right, true), 204, 200, sfx, walk_pitch)
        elseif h.upper_body == UPPER_TURN_LEFT then
            step_sound(st(A.turn_left, true), 187, 191, sfx, walk_pitch)
        end
        local s = c.anim_status
        if c.harry_flags & MOVING ~= 0 and
            ((s >= st(A.idle, true) and s <= st(A.idle_exhausted, false)) or s == st(A.handgun_aim, true)) then
            sh.entity.sfx_at(0, sfx, 24, walk_pitch)
            c.harry_step_sound = walk_pitch + 16
            c.harry_flags = c.harry_flags & ~MOVING
        end
        if c.anim_keyframe == 246 and c.harry_flags & FOOT_FIRST == 0 then
            sh.entity.sfx_at(0, sfx, 128, run_pitch)
            c.harry_step_sound = run_pitch + 32
            c.harry_flags = c.harry_flags | FOOT_FIRST
        end
    elseif lower == WALK_BACKWARD or lower == AIM_WALK_BACKWARD then
        step_sound(st(A.walk_backward, true), 52, 63, sfx, walk_pitch)
        moving()
    elseif lower == AIM_WALK_FORWARD or lower == WALK_FORWARD then
        step_sound(st(A.walk_forward, true), 18, 6, sfx, walk_pitch)
        moving()
    elseif lower == AIM_RUN_FORWARD or lower == AIM_RUN_RIGHT or lower == AIM_RUN_LEFT or
        lower == AIM_SIDESTEP_RIGHT or lower == AIM_SIDESTEP_LEFT then
        -- Moving with a weapon up: silent on the classic camera, as on the PS1.
        if h.alt_camera ~= 0 then
            if lower == AIM_RUN_FORWARD then
                run_step(A.run_forward, 31, 41, sfx, run_pitch)
            elseif lower == AIM_RUN_RIGHT then
                run_step(A.run_right, 145, 139, sfx, run_pitch)
            elseif lower == AIM_RUN_LEFT then
                run_step(A.run_left, 131, 125, sfx, run_pitch)
            elseif lower == AIM_SIDESTEP_RIGHT then
                step_sound(st(A.sidestep_right, true), 118, 108, sfx, walk_pitch)
            else
                step_sound(st(A.sidestep_left, true), 93, 83, sfx, walk_pitch)
            end
            moving()
        end
    elseif lower == RUN_FORWARD then
        run_step(A.run_forward, 31, 41, sfx, run_pitch)
        moving()
    elseif lower == SIDESTEP_RIGHT then
        step_sound(st(A.sidestep_right, true), 118, 108, sfx, walk_pitch)
        moving()
    elseif lower == SIDESTEP_LEFT then
        step_sound(st(A.sidestep_left, true), 93, 83, sfx, walk_pitch)
        moving()
    elseif lower == RUN_RIGHT then
        run_step(A.run_right, 145, 139, sfx, run_pitch)
        moving()
    elseif lower == RUN_LEFT then
        run_step(A.run_left, 131, 125, sfx, run_pitch)
        moving()
    elseif lower == RUN_FORWARD_WALL_STOP then
        if c.harry_flags & WALL_STOP_RIGHT ~= 0 then
            local clip = st(A.run_forward_wall_stop_right, true)
            if kf < 152 then
                step_sound(clip, 151, 154, sfx, run_pitch)
            else
                step_sound(clip, 156, 154, sfx, run_pitch)
            end
        else
            local clip = st(A.run_forward_wall_stop_left, true)
            if kf < 162 then
                step_sound(clip, 164, 161, sfx, run_pitch)
            else
                step_sound(clip, 164, 166, sfx, run_pitch)
            end
        end
        halted()
    elseif lower == RUN_LEFT_WALL_STOP then
        local clip = st(A.run_left_wall_stop, true)
        if kf < 323 then
            step_sound(clip, 322, 324, sfx, run_pitch)
        else
            step_sound(clip, 327, 324, sfx, run_pitch)
        end
        halted()
    elseif lower == RUN_RIGHT_WALL_STOP then
        local clip = st(A.run_right_wall_stop, true)
        if kf < 352 then
            step_sound(clip, 353, 351, sfx, run_pitch)
        else
            step_sound(clip, 353, 356, sfx, run_pitch)
        end
        halted()
    elseif lower == STUMBLE then
        local clip = st(A.run_forward_stumble, true)
        if kf < 172 then
            step_sound(clip, 171, 174, sfx, run_pitch)
        else
            step_sound(clip, 176, 174, sfx, run_pitch)
        end
        halted()
    elseif lower == RUN_LEFT_STUMBLE then
        local clip = st(A.run_left_stumble, true)
        if kf < 338 then
            step_sound(clip, 337, 341, sfx, walk_pitch)
        elseif kf < 344 then
            step_sound(clip, 343, 341, sfx, walk_pitch)
        else
            step_sound(clip, 356, 346, sfx, run_pitch)
        end
        halted()
    elseif lower == RUN_RIGHT_STUMBLE then
        local clip = st(A.run_right_stumble, true)
        if kf < 367 then
            step_sound(clip, 366, 370, sfx, walk_pitch)
        elseif kf < 373 then
            step_sound(clip, 372, 370, sfx, walk_pitch)
        else
            step_sound(clip, 385, 375, sfx, run_pitch)
        end
        halted()
    elseif lower == QUICK_TURN_LEFT or lower == AIM_QUICK_TURN_LEFT then
        step_sound(st(A.quick_turn_left, true), 222, 224, sfx, walk_pitch)
        halted()
    elseif lower == QUICK_TURN_RIGHT or lower == AIM_QUICK_TURN_RIGHT then
        step_sound(st(A.quick_turn_right, true), 209, 211, sfx, walk_pitch)
        halted()
    elseif lower == JUMP_BACKWARD or lower == JUMP_BACKWARD_LAND then
        if kf < 243 then
            c.harry_flags = c.harry_flags & ~FOOT_FIRST
        end
        if c.pos_y == h.floor_height then
            step_sound(st(A.jump_backward, true), 243, 245, sfx, run_pitch)
        end
        halted()
    end
    return 0
end

-- -----------------------------------------------------------------------------------------
-- The tick.

local WALK_STATES = {
    [WALK_FORWARD] = true, [WALK_BACKWARD] = true, [SIDESTEP_RIGHT] = true, [SIDESTEP_LEFT] = true,
    [AIM_WALK_FORWARD] = true, [AIM_WALK_BACKWARD] = true, [AIM_SIDESTEP_RIGHT] = true, [AIM_SIDESTEP_LEFT] = true,
}

-- The results screen's walking and running totals (unsigned, capped).
local function add_distance(name, step)
    local total = (h[name] + step) & 0xFFFFFFFF
    if total < 1 then
        total = 1
    elseif total > 0x80000000 then
        total = 0x80000000
    end
    h[name] = total
end

local CONTROLS = { "forward", "back", "turn_left", "turn_right", "step_left", "step_right", "step_left_tap",
                   "step_right_tap", "run", "aim", "attack", "shoot" }

function update(_)
    dt = sh.game.delta_time()
    for _, name in ipairs(CONTROLS) do
        input[name] = sh.input.control(name)
    end
    stick_x, stick_y, stick_up, stick_down = sh.input.move_stick()

    local lower = h.lower_body
    local aimed = lower < AIMED and 0 or AIMED
    local ahead = look_ahead()
    local dx = c.pos_x - h.prev_x
    local dz = c.pos_z - h.prev_z
    local travelled = sh.math.sqrt(s32(s32(dx * dx) + s32(dz * dz)))
    h.distance_counted = 1

    if lower == NONE or lower == AIM or lower == ATTACK then
        -- standing: nothing walked
    elseif WALK_STATES[lower] then
        add_distance("walk_distance", travelled)
    else
        add_distance("run_distance", travelled)
    end

    if lower == NONE or lower == AIM then
        standing(aimed, ahead)
    elseif lower == WALK_FORWARD or lower == AIM_WALK_FORWARD then
        walking_forward(aimed, ahead)
    elseif lower == RUN_FORWARD then
        running_forward(ahead, travelled)
    elseif lower == RUN_FORWARD_WALL_STOP then
        wall_stop()
    elseif lower == WALK_BACKWARD or lower == AIM_WALK_BACKWARD then
        walking_backward(aimed)
    elseif lower == SIDESTEP_RIGHT or lower == AIM_SIDESTEP_RIGHT then
        side_step(aimed, ahead, STEP_RIGHT)
    elseif lower == SIDESTEP_LEFT or lower == AIM_SIDESTEP_LEFT then
        side_step(aimed, ahead, STEP_LEFT)
    elseif lower == RUN_RIGHT then
        run_sideways(ahead, RUN_TO_RIGHT)
    elseif lower == RUN_LEFT then
        run_sideways(ahead, RUN_TO_LEFT)
    elseif lower == QUICK_TURN_RIGHT or lower == AIM_QUICK_TURN_RIGHT then
        quick_turning(aimed, TURN_RIGHT)
    elseif lower == QUICK_TURN_LEFT or lower == AIM_QUICK_TURN_LEFT then
        quick_turning(aimed, TURN_LEFT)
    elseif lower == JUMP_BACKWARD then
        jumping_backward(aimed)
    elseif lower == STUMBLE then
        stumbling()
    elseif lower == RUN_LEFT_WALL_STOP then
        side_wall_stop(aimed, A.run_left_wall_stop, 335, RUN_LEFT_WALL_STOP, -1024)
    elseif lower == RUN_RIGHT_WALL_STOP then
        side_wall_stop(aimed, A.run_right_wall_stop, 364, RUN_RIGHT_WALL_STOP, 1024)
    elseif lower == RUN_LEFT_STUMBLE then
        side_stumble(A.run_left_stumble, 349, RUN_LEFT_STUMBLE, -1024)
    elseif lower == RUN_RIGHT_STUMBLE then
        side_stumble(A.run_right_stumble, 378, RUN_RIGHT_STUMBLE, 1024)
    elseif lower == ATTACK then
        attacking()
    elseif lower == RELOAD then
        reloading()
    end

    footsteps(0)
end
