-- Harry's player controller: the pad of the tick into his 16 control flags. The engine calls
-- controller() once per Harry tick in Player_Update (after the force_hold_aim pin, before the
-- body state machines); sh.input.player_flag/set_player_flag read last tick's flags and write
-- this tick's. There is no other controller: if this file fails, the engine logs [C1LUA]
-- errors and the flags keep last tick's values. Write all flags at the end only, so a failing
-- call never leaves them half-updated. The bit operations and truncations are exact: recorded
-- replays depend on them byte for byte.
--
-- Kept PC behaviours: the forward history and the attack registers age at
-- 30 Hz, the rear look clears aim, the fire gate excludes only the two gun-raising states.
--
-- behavior.lua names this file (-- uses: Harry/controller ...): it runs first, before the
-- other Harry modules, in the same Lua state. Modules do not share locals; the `h` below is
-- this module's own view of Harry's state (sh.entity.harry / set_harry), like combat.lua's.
local h = setmetatable({}, {
    __index = function(_, k)
        return sh.entity.harry(k)
    end,
    __newindex = function(_, k, v)
        sh.entity.set_harry(k, v)
    end,
})

-- The game's constants: PlayerLowerBodyState_Aim = 20, PlayerUpperBodyState_AimStart
-- = 21, AimStartTargetLock = 22; SysState_Gameplay = 0; TAP =
-- AttackInputType_Tap = 0; NO_VALUE = -1.
local LOWER_BODY_AIM = 20
local UPPER_BODY_AIM_START = 21
local UPPER_BODY_AIM_START_LOCK = 22
local TAP = 0
local NO_VALUE = -1

local function mask(action, mode)
    return sh.input.pad_mask(action, mode)
end

local function dir(name, mode)
    return sh.input.pad_direction(name, mode)
end

function controller(_)
    local forward = sh.input.player_flag("forward")
    local back = sh.input.player_flag("back")
    local turn_left = sh.input.player_flag("turn_left")
    local turn_right = sh.input.player_flag("turn_right")
    local step_left = sh.input.player_flag("step_left")
    local step_right = sh.input.player_flag("step_right")
    local step_left_tap = sh.input.player_flag("step_left_tap")
    local step_right_tap = sh.input.player_flag("step_right_tap")
    local run = sh.input.player_flag("run")
    local aim = sh.input.player_flag("aim")
    local shoot = sh.input.player_flag("shoot")
    local attacking = sh.input.player_flag("attack")
    local hold_attack = sh.input.player_flag("hold_attack")
    local has_move = sh.input.player_flag("has_move")
    local has_action = sh.input.player_flag("has_action")
    local rock_drill = sh.input.player_flag("rock_drill_direction")

    -- Forward history aged at the 30 Hz tick, current input ORed per
    -- frame below: tick30 is the same 30 Hz gate as the engine's.
    if sh.input.tick30("forward") ~= 0 then
        forward = (forward * 2) & 0x3
    end
    -- The step taps age on EVERY call, not on the tick.
    step_left_tap = (step_left_tap * 2) & 0x3F
    step_right_tap = (step_right_tap * 2) & 0x3F

    local outside, left_excess, right_excess, stick_forward, stick_back = sh.input.stick_zone()
    if outside ~= 0 then
        turn_left = left_excess
        turn_right = right_excess
        forward = forward | stick_forward
        back = stick_back
        has_move = mask("stepLeft", 1) | dir("stick_up", 1) | dir("stick_right", 1) |
                   dir("stick_down", 1) | dir("stick_left", 1) | mask("stepRight", 1) |
                   mask("aim", 1)
    else
        turn_left = ((dir("left", 0) ~= 0 and dir("right", 0) == 0) and 1 or 0) << 6
        turn_right = ((dir("right", 0) ~= 0 and dir("left", 0) == 0) and 1 or 0) << 6
        forward = forward | ((dir("up", 0) ~= 0 and dir("down", 0) == 0) and 1 or 0)
        back = (dir("down", 0) ~= 0 and dir("up", 0) == 0) and 1 or 0
        has_move = mask("stepLeft", 1) | dir("up", 1) | dir("right", 1) | dir("down", 1) |
                   dir("left", 1) | mask("stepRight", 1) | mask("aim", 1)
    end

    step_left = (mask("stepLeft", 0) ~= 0 and mask("stepRight", 0) == 0) and 1 or 0
    step_right = (mask("stepRight", 0) ~= 0 and mask("stepLeft", 0) == 0) and 1 or 0
    step_left_tap = step_left_tap | (mask("stepLeft", 1) ~= 0 and 1 or 0)
    step_right_tap = step_right_tap | (mask("stepRight", 1) ~= 0 and 1 or 0)

    if h.option_walk_run ~= 0 then
        run = (mask("run", 0) == 0) and 1 or 0
    else
        run = mask("run", 0)
    end

    if h.option_weapon_control ~= 0 then
        aim = mask("aim", 0)
    else
        aim = mask("aim", 1)
    end
    if h.rear_look ~= 0 then
        aim = 0
    end

    local wa = h.weapon_attack
    local id = wa - idiv(wa, 10) * 10  -- WEAPON_ATTACK_ID_GET: C truncating remainder
    local handgun = sh.entity.weapon_number("Handgun")
    if wa >= weapon_attack(handgun, TAP) and h.lower_body >= LOWER_BODY_AIM
        and h.upper_body ~= UPPER_BODY_AIM_START and h.upper_body ~= UPPER_BODY_AIM_START_LOCK then
        -- Fire gate passed: the held action mask IS the shoot/attack value (not a boolean).
        shoot = mask("action", 0)
        attacking = shoot
    else
        if sh.input.tick30("attack") ~= 0 then
            local attack_btn = mask("action", 0)
            hold_attack = (hold_attack * 2) & 0x1F
            attacking = (attacking * 2) & 0x3
            shoot = (shoot * 2) & 0x3
            hold_attack = hold_attack | ((attack_btn & 0xFFFF) ~= 0 and 1 or 0)
            attacking = attacking | ((hold_attack & 0xF) == 0xF and 1 or 0)
            shoot = shoot | ((hold_attack ~= 0 and (hold_attack & 0x11) == 0) and 1 or 0)
            if shoot ~= 0 then
                hold_attack = 0
            end
        end
    end

    has_action = mask("run", 1) | mask("action", 1)

    if sh.entity.sys("sys_state") ~= 0 then
        shoot = 0
        attacking = 0
        hold_attack = 0
    end

    local hyper_blaster = sh.entity.weapon_number("HyperBlaster")
    if wa == weapon_attack(hyper_blaster, TAP) then
        local mode = sh.input.hyper_blaster_mode()
        if mode == 0 then
            aim = 0
        elseif mode == 1 then
            -- Konami gun controller: the second pad aims and fires
            aim = sh.input.pad_button("cross", 0, 1)
            shoot = sh.input.pad_button("square", 0, 1)
            attacking = shoot
        end
    end

    -- Aiming with the heavy weapons locks movement
    local rifle = sh.entity.weapon_number("HuntingRifle")
    local hammer = sh.entity.weapon_number("Hammer")
    local rock_drill_id = sh.entity.weapon_number("RockDrill")
    local katana = sh.entity.weapon_number("Katana")
    if h.aiming ~= 0 and (wa == weapon_attack(rifle, TAP)
        or (wa < weapon_attack(handgun, TAP) and (id == hammer or id == rock_drill_id or id == katana))) then
        if forward ~= 0 then
            rock_drill = 1
        elseif back ~= 0 then
            rock_drill = NO_VALUE
        else
            rock_drill = 0
        end
        run = 0
        step_right_tap = 0
        step_right = 0
        step_left_tap = 0
        step_left = 0
        back = 0
        forward = 0
    end

    sh.input.set_player_flag("forward", forward)
    sh.input.set_player_flag("back", back)
    sh.input.set_player_flag("turn_left", turn_left)
    sh.input.set_player_flag("turn_right", turn_right)
    sh.input.set_player_flag("step_left", step_left)
    sh.input.set_player_flag("step_right", step_right)
    sh.input.set_player_flag("step_left_tap", step_left_tap)
    sh.input.set_player_flag("step_right_tap", step_right_tap)
    sh.input.set_player_flag("run", run)
    sh.input.set_player_flag("aim", aim)
    sh.input.set_player_flag("shoot", shoot)
    sh.input.set_player_flag("attack", attacking)
    sh.input.set_player_flag("hold_attack", hold_attack)
    sh.input.set_player_flag("has_move", has_move)
    sh.input.set_player_flag("has_action", has_action)
    sh.input.set_player_flag("rock_drill_direction", rock_drill)
    return 0
end
