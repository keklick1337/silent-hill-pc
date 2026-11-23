-- Incubus, the final boss of the good endings (map7_s03, the last room of Nowhere). The cutscene
-- raises it (control 2), calls the lightning (3) and holds it (4, 5); when the fight starts the map
-- arms it (10): it hovers turning towards Harry (11), then charges a storm from its chest bone and
-- throws bolts at him (7). Each defeat counts once: under 20 health it sinks (12), sets flag M7S03_IncubusKilled for
-- the map's last cutscene and stays down (13). Without long gun ammo a fight timer runs out and it
-- falls on its own. Its lightning, sparks and fire are the map's effects (sh.entity.boss_fx_*).

local A = clips("Incubus", { still = 0, rise = 1, lightning_call = 2, hover = 3, hold_lightning = 4 })

local RISE = anim_status(A.rise, false)
local LIGHTNING = anim_status(A.lightning_call, false)
local HOVER = anim_status(A.hover, false)
local HOVER_LOOP = anim_status(A.hover, true)
local HOLD = anim_status(A.hold_lightning, false)

local CONTROL_WAIT = 1
local CONTROL_RISE = 2
local CONTROL_LIGHTNING = 3
local CONTROL_HOLD = 4
local CONTROL_HOLD_2 = 5
local CONTROL_STORM = 6
local CONTROL_STORM_FIRE = 7
local CONTROL_STORM_LEAD = 8
local CONTROL_ARM = 10
local CONTROL_HOVER = 11
local CONTROL_DOWN = 12
local CONTROL_DEAD = 13

local FLAG_DEFEATED = "M7S03_IncubusKilled"
local DIFFICULTY_EASY, DIFFICULTY_HARD = -1, 1

local FLAG_HIT = 0x4           -- flags: damage is taken at full value
local FLAG_TARGETED = 0x2      -- flags: Harry takes no damage while set
local FLAG_NO_RADIO = 0x100    -- flags: the radio stays silent
local ARMED = 0x4              -- incubus_flags: its effects are drawn while it fights

local CHEST_BONE = 2
local TURN_RATE = 1365         -- 120 degrees per second
local TURN_DEADZONE = 113      -- 10 degrees
local AIM_CONE = 136           -- 12 degrees
local LOOK_AT_SCALE = 2662     -- the camera looks at 0.65 of the chest height
local DEFEAT_HEALTH = 81920    -- 20

local player = record(0)

-- The sound of each keyframe (an index into SOUNDS; 13: none), as runs of keyframes.
local KEYFRAME_SOUND = {}
for _, run in ipairs({
    { 0, 28, 1 }, { 29, 55, 2 }, { 56, 74, 3 }, { 75, 89, 4 }, { 90, 99, 3 }, { 100, 104, 4 },
    { 105, 256, 13 }, { 257, 299, 0 }, { 300, 332, 13 }, { 333, 338, 3 }, { 339, 339, 0 },
}) do
    for k = run[1], run[2] do
        KEYFRAME_SOUND[k] = run[3]
    end
end

local SOUNDS = {
    [0] = function(c) return sound(c, "motion", 1676) end,
    function(c) return sound(c, "motion_2", 1677) end,
    function(c) return sound(c, "motion_3", 1678) end,
    function(c) return sound(c, "motion_4", 1678) end,
    function(c) return sound(c, "motion_5", 1678) end,
    function(c) return sound(c, "motion_6", 1680) end,
    function(c) return sound(c, "motion_7", 1681) end,
    function(c) return sound(c, "motion_8", 1682) end,
    function(c) return sound(c, "motion_9", 1683) end,
    function(c) return sound(c, "motion_10", 1684) end,
    function(c) return sound(c, "motion_11", 1685) end,
    function(c) return sound(c, "motion_12", 1686) end,
    function(c) return sound(c, "motion_13", 1687) end,
}
local SOUND_VOLUME = 255
local NO_SOUND = 13

local function dt()
    return sh.game.delta_time()
end

local function rand_hit(mask)
    return (sh.rng.rand16() & mask) == 0
end

local function hard()
    return sh.save.game_difficulty() == DIFFICULTY_HARD
end

-- The chest bone in the world (Q12).
local function chest(c)
    local x, y, z = sh.entity.bone_point(c.slot, CHEST_BONE, 0, 0, 0)
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

local function init(c)
    c.anim_alpha = 0
    if sh.save.game_difficulty() == DIFFICULTY_EASY then
        c.health = sh.chara_param_q12("Incubus", "health_easy", 122880000)
    else
        c.health = sh.chara_param_q12("Incubus", "health", 163840000)
    end
    c.move_speed = 0
    c.heading_angle = c.rot_y
    c.cylinder_radius = 1228
    sh.entity.set_collision_state(c.slot, 4)
    c.cylinder_offset_x = 0
    c.cylinder_offset_z = 0
    c.flags = c.flags | FLAG_HIT
    c.incubus_defeats = 0
    c.incubus_flags = 0
    c.incubus_fight_timer = hard() and 1228800 or 122880
    -- The cutscene may have asked for a control state before the first tick (in state_step).
    local asked = c.state_step
    if asked ~= 0 then
        c.state_step = 0
        c.control_state = asked
    else
        set_control(c, CONTROL_WAIT)
    end
    anim_set(c, HOVER, 338)
    sh.entity.clip_table_attach(c.slot)
    damage_clear(c)
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
            local health = s32(c.health - c.damage_amount)
            if health < 0 then
                health = 0
            end
            c.health = health
            if health < DEFEAT_HEALTH and sh.entity.player_damage_disable(0) == 0 then
                c.health = 0
                set_control(c, CONTROL_DOWN)
                c.incubus_defeats = c.incubus_defeats + 1
            end
        end
    end
    damage_clear(c)
end

local function rise(c)
    if c.state_step == 0 then
        c.anim_status = RISE
        c.state_step = c.state_step + 1
    end
end

local LIGHTNING_STEPS = { [1] = 6144, [2] = 10240, [3] = 14336, [4] = 18432 }

local function lightning(c)
    if c.state_step == 0 then
        c.anim_status = LIGHTNING
        c.incubus_timer = 0
        c.state_step = c.state_step + 1
        return
    end
    local limit = LIGHTNING_STEPS[c.state_step]
    if limit ~= nil and c.incubus_timer >= limit then
        c.state_step = c.state_step + 1
    end
    if c.anim_status == HOLD then
        set_control(c, CONTROL_HOLD_2)
    end
    c.incubus_timer = c.incubus_timer + dt()
end

local function hold(c)
    if c.state_step == 0 then
        c.anim_status = HOLD
        c.state_step = c.state_step + 1
    end
end

local function tick_timer(c)
    c.incubus_timer = c.incubus_timer - dt()
end

-- Charging (step 1): sparks around the chest until the timer runs out.
local function storm_charge(c, x, y, z, next_timer)
    if rand_hit(0xF0) then
        sh.entity.boss_fx_spark(x, y, z, CHEST_BONE)
    end
    if c.incubus_timer < 0 then
        c.incubus_timer = next_timer
        c.state_step = c.state_step + 1
    end
    tick_timer(c)
end

-- Aiming (step 2) ends once the timer ran out with Harry in front.
local function storm_aimed(c)
    if c.incubus_timer < 0 and abs(angle_to_player(c)) < AIM_CONE then
        c.incubus_timer = 1228
        c.state_step = c.state_step + 1
        return true
    end
    tick_timer(c)
    return false
end

-- The three storms: plain bolts at Harry (6), with fire and aim led by his movement (7), bolts that
-- strike where he will be (8). Without bones (the control pass) only the turn and the last step run.
local function storm(c, kind, has_bones)
    if c.state_step == 0 then
        if kind == CONTROL_STORM_FIRE then
            c.incubus_timer = 6144
            c.flags = c.flags & ~FLAG_HIT
        else
            c.state_step = c.state_step + 1
            c.incubus_timer = 6144
            return
        end
        c.state_step = c.state_step + 1
        return
    end
    if not has_bones then
        turn_to_player(c)
    end
    local step = c.state_step
    if step == 5 then
        set_control(c, CONTROL_HOVER)
        if kind == CONTROL_STORM_FIRE then
            c.flags = c.flags | FLAG_HIT
        end
        return
    end
    if not has_bones or step < 1 or step > 4 then
        return
    end
    local x, y, z = chest(c)
    if step == 1 then
        local next_timer = 10240
        if kind == CONTROL_STORM_FIRE and hard() then
            next_timer = 4096
        end
        storm_charge(c, x, y, z, next_timer)
    elseif step == 2 then
        if rand_hit(0xF0) then
            sh.entity.boss_fx_spark(x, y, z, CHEST_BONE)
        end
        if rand_hit(0x10) then
            if kind == CONTROL_STORM_LEAD then
                sh.entity.boss_fx_arc_wide(x, y, z, x, y, z)
            else
                sh.entity.boss_fx_arc(x, y, z, x, y, z)
                if kind == CONTROL_STORM_FIRE then
                    sh.entity.boss_fx_fire(x, y, z, 0, 0, 18432)
                end
            end
        end
        if rand_hit(0xFC) then
            sh.entity.boss_fx_flash(x, y, z)
        end
        storm_aimed(c)
    else
        -- Steps 3 (the volley) and 4 (the last bolts): the volley is denser.
        local mask
        if step == 3 then
            mask = 0x10
        elseif kind == CONTROL_STORM then
            mask = 0x50
        else
            mask = 0x51
        end
        local lead_x, lead_z = 0, 0
        if kind == CONTROL_STORM_LEAD then
            lead_x, lead_z = sh.entity.boss_aim_lead()
        end
        if rand_hit(mask) then
            if kind == CONTROL_STORM then
                sh.entity.boss_fx_bolt_player(x, y, z, 0, 0, 0)
            elseif kind == CONTROL_STORM_FIRE then
                lead_x, lead_z = sh.entity.boss_aim_lead()
                sh.entity.boss_fx_bolt_player(x, y, z, lead_x, 0, lead_z)
            else
                sh.entity.boss_fx_bolt(x, y, z, s32(player.pos_x + lead_x), player.pos_y, s32(player.pos_z + lead_z))
            end
        end
        if rand_hit(0xFC) then
            sh.entity.boss_fx_flash(x, y, z)
            if step == 3 then
                sh.entity.boss_fx_spark(x, y, z, CHEST_BONE)
            end
        end
        if c.incubus_timer < 0 then
            c.state_step = c.state_step + 1
            if step == 3 then
                if kind == CONTROL_STORM_FIRE and hard() then
                    c.incubus_timer = 12288
                else
                    c.incubus_timer = 8192
                end
            end
        end
        tick_timer(c)
    end
end

local function arm(c)
    sh.entity.boss_fx_attach(c.slot)
    set_control(c, CONTROL_HOVER)
    c.incubus_flags = c.incubus_flags | ARMED
end

local function hover(c)
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
        set_control(c, CONTROL_STORM_FIRE)
    end
    tick_timer(c)
end

local function down(c)
    if c.state_step == 0 then
        if c.anim_status ~= HOVER_LOOP then
            c.anim_status = HOVER
        end
        c.incubus_timer = 2048
        c.state_step = c.state_step + 1
    else
        if c.incubus_timer < 0 then
            sh.flag_set(FLAG_DEFEATED, true)
            set_control(c, CONTROL_DEAD)
        end
        tick_timer(c)
    end
end

local function dead(c)
    if c.state_step == 0 then
        if c.anim_status ~= HOVER_LOOP then
            c.anim_status = HOVER
        end
        c.state_step = c.state_step + 1
    end
end

local function control(c)
    local state = c.control_state
    if state == CONTROL_RISE then
        rise(c)
    elseif state == CONTROL_LIGHTNING then
        lightning(c)
    elseif state == CONTROL_HOLD or state == CONTROL_HOLD_2 then
        hold(c)
    elseif state == CONTROL_STORM or state == CONTROL_STORM_FIRE or state == CONTROL_STORM_LEAD then
        storm(c, state, false)
    elseif state == CONTROL_HOVER then
        hover(c)
    elseif state == CONTROL_ARM then
        arm(c)
    elseif state == CONTROL_DOWN then
        down(c)
    elseif state == CONTROL_DEAD then
        dead(c)
    end
end

-- The effects of the storms (with the bones), then the camera follows the chest.
local function effects(c)
    sh.entity.boss_fx_track()
    local state = c.control_state
    if state == CONTROL_STORM or state == CONTROL_STORM_FIRE or state == CONTROL_STORM_LEAD then
        storm(c, state, true)
    end
    if c.anim_status ~= HOVER_LOOP then
        local _, y = chest(c)
        sh.entity.camera_look_at_y(q12_mul(y, LOOK_AT_SCALE))
    end
end

local function move(c)
    c.fall_speed = c.fall_speed + sh.game.gravity_speed()
    sh.entity.movement_update(c.slot)
    c.rot_y = angle_signed(c.rot_y)
end

local function animate(c)
    sh.entity.root_transform(c.slot)
    local before = KEYFRAME_SOUND[asr(c.anim_time, 12)]
    if c.anim_status ~= anim_status(A.still, false) then
        sh.entity.anim_step(c.slot)
    end
    local now = KEYFRAME_SOUND[asr(c.anim_time, 12)]
    if now ~= NO_SOUND and now ~= before then
        sh.entity.sfx_flags(c.slot, SOUNDS[now](c), SOUND_VOLUME, 0)
    end
end

-- The collision follows the chest while the model is shown; hidden, a box at the feet.
local function collision(c)
    if c.anim_flags & 0x2 ~= 0 then
        local x, y, z = chest(c)
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

local function draw(c)
    local state = c.control_state
    if state ~= CONTROL_RISE and state ~= CONTROL_HOLD and state ~= CONTROL_DEAD and state ~= CONTROL_LIGHTNING and
        state ~= CONTROL_HOLD_2 and c.incubus_flags & ARMED ~= 0 then
        sh.entity.boss_fx_draw(1)
    end
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
        animate(c)
        sh.entity.boss_fx_track()
        draw(c)
    end
end
