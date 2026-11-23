-- Unknown23, the final boss of the bad endings (map7_s03). The game never spawns the type itself: the
-- map hands its update to the Incubator when the fight starts, so this runs on her record (its type
-- stays the Incubator's) with Unknown23's clips and sounds. The cutscene lifts her (control 4, then 2);
-- she floats turning towards Harry (2), then gathers lightning in both hands and throws it (3). Up
-- close she shocks him. At 0 health she falls (5) and sets flag M7S03_IncubatorKilled for the map's last cutscene.
-- Without long gun ammo a fight timer runs out and she falls on her own. The lightning is the map's
-- effects (sh.entity.boss_fx_*), the shock's sparks the map's drawing (sh.entity.unknown23_fx_*).

local TYPE = sh.chara_id("Unknown23") or 23

local LIFT = 4                 -- anim_status(2, false)
local FLOAT = 8                -- anim_status(4, false)

local CONTROL_WAIT = 1
local CONTROL_FLOAT = 2
local CONTROL_ATTACK = 3
local CONTROL_LIFT = 4
local CONTROL_FALL = 5
local CONTROL_HOLD = 6

local FLAG_DEFEATED = "M7S03_IncubatorKilled"
local DIFFICULTY_EASY, DIFFICULTY_HARD = -1, 1

local FLAG_HIT = 0x4           -- flags: damage is taken at full value
local FLAG_TARGETED = 0x2      -- flags: Harry takes no damage while set
local FLAG_NO_RADIO = 0x100    -- flags: the radio stays silent

local LEFT_HAND, RIGHT_HAND, CHEST_BONE = 6, 10, 2
local TURN_RATE = 1024         -- 90 degrees per second
local TURN_DEADZONE = 113      -- 10 degrees
local AIM_CONE = 136           -- 12 degrees
local SHOCK_RANGE = 10240      -- 2.5 m
local SHOCK_ATTACK = sh.entity.attack_number("Unknown23Shock") -- the attack Harry receives from the shock
local SHOCK_VOLUME = 128

local player = record(0)

-- Her sounds are Unknown23's, not her record's type's.
local function sound(_, name, fallback)
    return sh.chara_sfx("Unknown23", name, fallback)
end

local function dt()
    return sh.game.delta_time()
end

local function rand_hit(mask)
    return (sh.rng.rand16() & mask) == 0
end

local function hard()
    return sh.save.game_difficulty() == DIFFICULTY_HARD
end

local function bone(c, b)
    local x, y, z = sh.entity.bone_point(c.slot, b, 0, 0, 0)
    return s32(x * 16), s32(y * 16), s32(z * 16)
end

local function angle_to_player(c)
    return angle_signed(sh.math.atan2(s32(player.pos_x - c.pos_x), s32(player.pos_z - c.pos_z)) - c.rot_y)
end

local function turn_to_player(c)
    local delta = angle_to_player(c)
    if abs(delta) > TURN_DEADZONE then
        local step = q12_mul(dt(), TURN_RATE)
        if delta > 0 then
            c.rot_y = c.rot_y + step
        else
            c.rot_y = c.rot_y - step
        end
    end
end

local function set_control(c, state)
    c.control_state = state
    c.state_step = 0
end

local function tick_timer(c)
    c.incubus_timer = c.incubus_timer - dt()
end

local function init(c)
    c.anim_alpha = 0
    if sh.save.game_difficulty() == DIFFICULTY_EASY then
        c.health = 73710000
    else
        c.health = 122880000
    end
    c.move_speed = 0
    c.cylinder_radius = 1228
    c.cylinder_offset_x = 0
    c.cylinder_offset_z = 0
    sh.entity.set_collision_state(c.slot, 4)
    c.heading_angle = c.rot_y
    c.flags = c.flags | FLAG_HIT
    c.incubus_defeats = 0
    -- The cutscene may have asked for a control state before the first tick (in state_step).
    local asked = c.state_step
    if asked == 0 then
        set_control(c, CONTROL_WAIT)
    else
        set_control(c, asked)
    end
    anim_set(c, LIFT, 115)
    sh.entity.type_clip_table_attach(c.slot, TYPE)
    damage_clear(c)
    c.incubus_fight_timer = hard() and 1228800 or 122880
    sh.entity.boss_fx_attach(c.slot)
    sh.entity.unknown23_fx_clear()
    c.flags = c.flags | FLAG_NO_RADIO
end

local function take_damage(c)
    if c.incubus_defeats == 0 then
        if c.incubus_fight_timer < 0 then
            c.health = 0
            c.damage_amount = 1
        end
        if sh.entity.player_long_gun_ammo() == 0 then
            c.incubus_fight_timer = c.incubus_fight_timer - dt()
        end
        if c.flags & FLAG_HIT == 0 then
            c.damage_amount = s32(c.damage_amount * 10)
        end
        if c.damage_amount > 0 then
            c.health = max(0, s32(c.health - c.damage_amount))
            if c.health <= 0 and sh.entity.player_damage_disable(0) == 0 then
                sh.flag_set(FLAG_DEFEATED, true)
                set_control(c, CONTROL_FALL)
                c.health = -1
                c.flags = c.flags | FLAG_TARGETED
                c.incubus_defeats = c.incubus_defeats + 1
            end
        end
    end
    damage_clear(c)
end

local function lift(c)
    if c.state_step == 0 then
        c.state_step = c.state_step + 1
        return
    end
    if c.anim_status == FLOAT then
        set_control(c, CONTROL_FLOAT)
    end
end

local function first_step(c)
    if c.state_step == 0 then
        c.state_step = c.state_step + 1
    end
end

-- Lightning gathered in both hands: sparks (step 1), arcs while she aims (2), bolts at Harry led by his
-- movement (3, then fewer in 4). Without bones (the control pass) only the turn and the last step run.
local function attack(c, has_bones)
    if c.state_step == 0 then
        c.state_step = c.state_step + 1
        c.incubus_timer = 6144
        c.flags = c.flags & ~FLAG_HIT
        return
    end
    if not has_bones then
        turn_to_player(c)
    end
    local step = c.state_step
    if step == 5 then
        set_control(c, CONTROL_FLOAT)
        c.flags = c.flags | FLAG_HIT
        return
    end
    if not has_bones or step < 1 or step > 4 then
        return
    end
    if step == 1 then
        local x, y, z = bone(c, LEFT_HAND)
        if rand_hit(0xF0) then
            sh.entity.boss_fx_spark(x, y, z, LEFT_HAND)
        end
        x, y, z = bone(c, RIGHT_HAND)
        if rand_hit(0xF0) then
            sh.entity.boss_fx_spark(x, y, z, RIGHT_HAND)
        end
        if c.incubus_timer < 0 then
            c.incubus_timer = hard() and 8192 or 10240
            c.state_step = c.state_step + 1
        end
        tick_timer(c)
    elseif step == 2 then
        local x, y, z = bone(c, LEFT_HAND)
        if rand_hit(0xFC) then
            sh.entity.boss_fx_spark(x, y, z, LEFT_HAND)
        end
        if rand_hit(0x30) then
            sh.entity.boss_fx_arc(x, y, z, x, y, z)
        end
        x, y, z = bone(c, RIGHT_HAND)
        if rand_hit(0xFC) then
            sh.entity.boss_fx_spark(x, y, z, RIGHT_HAND)
        end
        if rand_hit(0x30) then
            sh.entity.boss_fx_arc(x, y, z, x, y, z)
        end
        if rand_hit(0xFC) then
            sh.entity.boss_fx_flash(x, y, z)
        end
        if c.incubus_timer < 0 and abs(angle_to_player(c)) < AIM_CONE then
            c.incubus_timer = 1228
            c.state_step = c.state_step + 1
            return
        end
        tick_timer(c)
    else
        -- The volley (3) sparks too; the last bolts (4) come less often.
        local volley = step == 3
        local mask = volley and 0x30 or 0x70
        local lead_x, lead_z = sh.entity.boss_aim_lead()
        for _, hand in ipairs({ LEFT_HAND, RIGHT_HAND }) do
            local x, y, z = bone(c, hand)
            if volley and rand_hit(0xFC) then
                sh.entity.boss_fx_spark(x, y, z, hand)
            end
            if rand_hit(mask) then
                sh.entity.boss_fx_bolt_player(x, y, z, lead_x, 0, lead_z)
            end
            if hand == RIGHT_HAND and rand_hit(0xFC) then
                sh.entity.boss_fx_flash(x, y, z)
            end
        end
        if c.incubus_timer < 0 then
            c.state_step = c.state_step + 1
            if volley then
                c.incubus_timer = hard() and 10240 or 6144
            end
        end
        tick_timer(c)
    end
end

local function float(c)
    if c.state_step == 0 then
        local base = hard() and 8192 or 20480
        c.incubus_timer = base + sh.rng.int(0, 8191)
        c.state_step = c.state_step + 1
        return
    end
    turn_to_player(c)
    local step = c.state_step
    if step == 1 then
        if c.incubus_timer <= 0 then
            c.state_step = 2
        end
    elseif step == 2 then
        set_control(c, CONTROL_ATTACK)
    end
    tick_timer(c)
end

local function control(c)
    local state = c.control_state
    if state == CONTROL_LIFT then
        lift(c)
    elseif state == CONTROL_FALL or state == CONTROL_HOLD then
        first_step(c)
    elseif state == CONTROL_ATTACK then
        attack(c, false)
    elseif state == CONTROL_FLOAT then
        float(c)
    end
end

-- The shock: Harry is hit and thrown away from her.
local function shock(c)
    player.attack_received = SHOCK_ATTACK
    local angle = sh.math.atan2(s32(player.pos_x - c.pos_x), s32(player.pos_z - c.pos_z))
    player.damage_amount = 1
    player.damage_y = angle
    sh.entity.unknown23_shock_fx(player.pos_x, player.pos_y, player.pos_z, angle)
    sh.entity.sfx_flags(0, sound(c, "hit", 1673), SHOCK_VOLUME, 0)
end

local function effects(c)
    sh.entity.boss_fx_track()
    if c.control_state == CONTROL_ATTACK then
        attack(c, true)
    end
    local dist = distance_2d(s32(player.pos_x - c.pos_x), s32(player.pos_z - c.pos_z))
    if dist < SHOCK_RANGE and sh.entity.player_damage_disable(0) == 0 then
        shock(c)
    end
end

local function move(c)
    c.fall_speed = c.fall_speed + sh.game.gravity_speed()
    sh.entity.movement_update(c.slot)
    c.rot_y = angle_signed(c.rot_y)
end

local function animate(c)
    sh.entity.root_transform(c.slot)
    if c.anim_status ~= 0 then
        sh.entity.type_anim_step(c.slot, TYPE)
    end
end

-- The collision follows the chest while the model is shown; hidden, a box at the feet.
local function collision(c)
    if c.anim_flags & 0x2 ~= 0 then
        local x, y, z = bone(c, CHEST_BONE)
        local off_x = s32(x - c.pos_x)
        local off_y = s32(y - c.pos_y)
        local off_z = s32(z - c.pos_z)
        c.cylinder_radius = 2048
        c.cylinder_extent = 2048
        c.box_top = off_y - 1024
        c.box_bottom = off_y
        c.box_height = off_y + 1024
        c.box_offset_y = off_y
        shape_follow(c, off_x, off_z, 4096)
        c.box_offset_x = c.cylinder_offset_x
        c.box_offset_z = c.cylinder_offset_z
        return
    end
    local y = c.pos_y
    c.box_bottom = y
    c.box_height = y
    c.box_top = y - 4096
    c.box_offset_y = y - 2048
end

-- The effects are drawn armed while she fights; the sparks of the shock every tick.
local function draw(c)
    local state = c.control_state
    sh.entity.boss_fx_draw((state == CONTROL_FLOAT or state == CONTROL_ATTACK) and 1 or 0)
    sh.entity.unknown23_fx_update()
end

function update(slot)
    local c = record(slot)
    if c.control_state == 0 then
        init(c)
    end
    if c.control_state == CONTROL_WAIT then
        return
    end
    if dt() ~= 0 then
        take_damage(c)
        control(c)
        if c.state_step == 0 then
            control(c)
        end
        move(c)
        animate(c)
        collision(c)
        effects(c)
        draw(c)
        sh.entity.player_damage_disable((c.flags & FLAG_TARGETED) ~= 0 and 1 or 0)
    else
        sh.entity.boss_fx_track()
        draw(c)
        animate(c)
    end
end
