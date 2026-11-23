-- Puppet Nurse and Puppet Doctor (map3_s03, map3_s04, map3_s05, map7_s01; the Dummy variants share
-- this file). Spawned dormant, idling or awake (state_step at spawn packs palette, model variant and
-- the first state); wanders, chases Harry when it sees or hears him, swings or grabs him, staggers
-- after enough damage and goes down at 120 health.

local PUPPET_NURSE = sh.chara_id("PuppetNurse") or 16
local PUPPET_DOCTOR = sh.chara_id("PuppetDoctor") or 18
local DUMMY_DOCTOR = sh.chara_id("DummyDoctor") or 19

-- Every NPC slot: the map's own (32) and those of spawns of the engine after them.
local NPC_COUNT = 32
do
    local ok, n = pcall(sh.entity.npc_slot_count)
    if ok and n ~= nil and n > NPC_COUNT then
        NPC_COUNT = n
    end
end
local HARD = 1
local GRAB_ATTACK = sh.entity.attack_number("PuppetNurseGrab")
-- The hold drains the damage of the grab row every 1.5 s: 0 in the original, so it never takes health.
-- fix_nurse_grab_drain (GAMEPLAY CHANGE) drains the swing's row instead.
local GRAB_DRAIN_ATTACK_FIXED = sh.entity.attack_number("PuppetNurseSwing")
local SWING_NURSE = GRAB_DRAIN_ATTACK_FIXED
local SWING_DOCTOR = sh.entity.attack_number("PuppetDoctorSwing")
local CHAINSAW_ATTACKS = { [5] = true, [15] = true, [25] = true, [2] = true, [12] = true, [22] = true }

local FLAG_ATTACK_HIT = 1
local FLAG_DOWN = 2
local CHARA_FLAG_TARGETED = 2
local CHARA_FLAG_HIT = 4
local ANIM_FLAG_VISIBLE = 2

local BONE_TORSO, BONE_HEAD, BONE_RIGHT_SHIN, BONE_LEFT_SHIN, BONE_HAND = 1, 2, 16, 13, 10

-- Stats entries (the record points at one): health, damage before a stagger, sight range scale, wander
-- speed scale, body scale, sound bank, attack, push scale, clip table. Entry 0 is the doctor's.
local STATS = {
    [0] = { health = 3072000, stagger = 1228800, sight = 4096, wander = 4096, scale = 4485, bank = 1, attack = SWING_DOCTOR, push = 20480, clips = 1 },
    [1] = { health = 1638400, stagger = 1433600, sight = 4096, wander = 4096, scale = 4096, bank = 0, attack = SWING_NURSE, push = 30720, clips = 0 },
    [2] = { health = 2662400, stagger = 614400, sight = 4096, wander = 4096, scale = 4096, bank = 0, attack = SWING_NURSE, push = 30720, clips = 0 },
    [3] = { health = 3276800, stagger = 1433600, sight = 6144, wander = 4096, scale = 4096, bank = 0, attack = SWING_NURSE, push = 30720, clips = 0 },
}

-- Sight and hearing by the map's light: lit, dark, dark with the flashlight off.
local SENSES = {
    [0] = { see_near = 12288, see_far = 12288, hear = 2048 },
    [1] = { see_near = 16384, see_far = 20480, hear = 2048 },
    [2] = { see_near = 2048, see_far = 0, hear = 4915 },
}

-- Sound banks (0 nurse, 1 doctor): slot name of the character data, the game's id, volume. A bank is
-- only ever played by its own type, so both use the same slot names.
local SFX = {
    [0] = {
        [0] = { "idle", 1506, 128 }, { "death", 1507, 128 }, { "stagger", 1508, 128 }, { "hurt", 1509, 128 },
        { "hurt_2", 1510, 160 }, { "hurt_3", 1511, 255 }, { "motion", 1512, 128 }, { "motion_2", 1512, 128 },
        { "motion_3", 1519, 128 },
    },
    [1] = {
        [0] = { "idle", 1513, 128 }, { "death", 1514, 128 }, { "stagger", 1515, 128 }, { "hurt", 1516, 128 },
        { "hurt_2", 1517, 168 }, { "hurt_3", 1518, 255 }, { "motion", 1512, 128 }, { "motion_2", 1512, 128 },
        { "motion_3", 1519, 128 },
    },
}
local NO_SFX = 9
-- The hurt sound by the hit type of the attack received.
local HURT_SFX = { [0] = 3, 3, 4, 5 }
-- Keyframes with a sound (first, last, sound): the swing, then the footsteps of the walk cycles.
local KEYFRAME_SFX = {
    { 16, 36, 8 }, { 363, 368, 6 }, { 369, 382, 7 }, { 383, 384, 6 }, { 419, 445, 6 }, { 446, 456, 7 },
    { 457, 489, 6 }, { 490, 506, 7 }, { 507, 520, 6 }, { 521, 533, 7 }, { 534, 554, 6 }, { 555, 577, 7 },
}

-- Clip statuses the random picks choose from.
local DOWN_STATUS = { [false] = 8, [true] = 6 }
local DOWN_BLOOD = { [false] = 14, [true] = 15 }
local TAUNT_STATUS = { [0] = 14, 16 }
local IDLE_STATUS = { [0] = 22, 22, 24, 26 }
local RELEASE_STATES = { [0] = 9, 9, 9, 6, 6, 9, 7, 7 }

local player = record(0)
local stats

local function cmod(a, b)
    return a - idiv(a, b) * b
end

local function abs(x)
    return x < 0 and -x or x
end

local sound_cache = {}
local function sfx_play(c, idx)
    local e = SFX[stats.bank][idx]
    local key = stats.bank .. e[1]
    local id = sound_cache[key]
    if id == nil then
        id = sh.entity.sound(c.slot, e[1], e[2])
        sound_cache[key] = id
    end
    sh.entity.sfx_flags(c.slot, id, e[3], 0)
end

local function keyframe_sfx_index(frame)
    for _, r in ipairs(KEYFRAME_SFX) do
        if frame >= r[1] and frame <= r[2] then
            return r[3]
        end
    end
    return NO_SFX
end

local function state_set(c, state)
    c.control_state = state
    c.state_step = 0
end

-- Distance to Harry on the ground, through Q6 so the squares fit 32 bits.
local function player_distance(c)
    local dx = asr(s32(player.pos_x - c.pos_x), 6)
    local dz = asr(s32(player.pos_z - c.pos_z), 6)
    return s32(sh.math.sqrt(s32(dx * dx + dz * dz)) << 6)
end

local function player_angle(c)
    return angle_signed(heading_to(c.pos_x, c.pos_z, player.pos_x, player.pos_z) - c.rot_y)
end

local function turn_towards(c, heading)
    local angle = s16(angle_signed(heading - c.rot_y))
    if abs(angle) > 56 then
        if angle > 0 then
            c.rot_y = c.rot_y + q12_mul(sh.game.delta_time(), 1024)
        else
            c.rot_y = c.rot_y - q12_mul(sh.game.delta_time(), 1024)
        end
    end
end

-- Turns faster the further off Harry is.
local function face_player(c, angle, threshold)
    local abs_angle = abs(angle)
    if abs_angle > threshold then
        local rate = s16(abs_angle * 2 + 512)
        local add = q12_mul(sh.game.delta_time(), rate)
        if angle > 0 then
            c.rot_y = c.rot_y + add
        else
            c.rot_y = c.rot_y - add
        end
    end
end

local function idle_status(status)
    return status == 36 or status == 34 or status == 37 or status == 35
end

local function init(c, doctor)
    c.move_speed = 0
    sh.entity.set_collision_state(c.slot, 3)
    c.heading_angle = c.rot_y
    c.puppet_nurse_pos_x = c.pos_x
    c.puppet_nurse_pos_y = c.pos_y
    c.puppet_nurse_pos_z = c.pos_z
    c.cylinder_radius = 1228
    c.puppet_nurse_push_x = 0
    c.puppet_nurse_push_y = 0
    c.puppet_nurse_push_z = 0
    c.puppet_nurse_damage_taken = 0
    c.puppet_nurse_down_stage = 0
    damage_clear(c)
    c.puppet_nurse_flags = 0
    c.puppet_nurse_home_x = c.pos_x
    c.puppet_nurse_home_z = c.pos_z

    local spawn = c.state_step - 1
    local palette = cmod(spawn, 3)
    local rest = idiv(spawn, 3)
    local variant = cmod(rest, 3)
    local first_state = idiv(rest, 3)

    c.puppet_nurse_alert_timer = 0
    c.flags = c.flags | CHARA_FLAG_HIT

    local index
    if not doctor then
        index = palette + 1
        c.puppet_nurse_model_variant = variant + 1
    else
        c.puppet_nurse_model_variant = 1
        palette = 0
        index = 0
    end
    sh.entity.puppet_nurse_stats_set(c.slot, index)
    stats = STATS[index]

    c.health = stats.health
    c.palette = palette

    local state = ({ [0] = 11, 10, 9 })[first_state]
    if state ~= nil then
        c.control_state = state
        c.state_step = 0
        c.anim_time = 459 * 4096
        c.anim_keyframe = 459
    end

    sh.entity.clip_table_attach(c.slot, stats.clips)
    c.anim_status = 34
    c.anim_alpha = 0
    c.puppet_nurse_scale = 4096
end

local function hit_turned_away(c)
    local x, y, z = c.damage_x, c.damage_y, c.damage_z
    local sum = s32(q12_mul(x, x) + q12_mul(y, y) + q12_mul(z, z))
    local angle = abs(angle_signed(sh.math.atan2(x, z) - c.rot_y))
    return sum > 4096 and angle > 113
end

local function damage_handle(c)
    local amount = c.damage_amount
    if amount > 0 then
        local attack = c.attack_received
        if not CHAINSAW_ATTACKS[attack] then
            local _, hit_type = sh.entity.weapon_attack_stats(attack)
            local idx = HURT_SFX[hit_type]
            if idx ~= nil then
                sfx_play(c, idx)
            end
        end

        local stage = c.puppet_nurse_down_stage
        if stage == 0 then
            c.puppet_nurse_push_x = c.damage_x
            c.puppet_nurse_push_y = c.damage_y
            c.puppet_nurse_push_z = c.damage_z
            c.puppet_nurse_hit_amount = amount
            c.puppet_nurse_damage_taken = c.puppet_nurse_damage_taken + amount

            local health = s32(c.health - amount)
            if health < 0 then
                health = 0
            end
            c.health = health

            if health <= 491520 then
                c.puppet_nurse_down_stage = c.puppet_nurse_down_stage + 1
                if not hit_turned_away(c) then
                    state_set(c, 4)
                else
                    state_set(c, 3)
                end
                if c.health == 0 then
                    sh.entity.enemy_defeated(c.slot)
                end
            elseif c.control_state ~= 2 and (stats.stagger < c.puppet_nurse_damage_taken or amount > 1310720) then
                c.puppet_nurse_damage_taken = 0
                state_set(c, 2)
            end
        elseif stage == 1 then
            if c.health ~= 0 then
                sfx_play(c, 1)
                c.health = 0
                sh.entity.enemy_defeated(c.slot)
                if c.anim_status == 29 then
                    c.anim_status = 44
                end
            end
            c.puppet_nurse_down_stage = c.puppet_nurse_down_stage + 1
        end
    end
    damage_clear(c)
end

-- Keeps the swing at arm's length from Harry.
local function attack_steer(c)
    local dist = distance_2d(player.pos_x - c.pos_x, player.pos_z - c.pos_z) - 3112
    local angle = s16(player_angle(c))
    if abs(dist) > 122 then
        speed_toward(c, 16384, dist > 0 and 0 or -81)
    else
        speed_toward(c, 16384, 0)
    end
    face_player(c, angle, 113)
end

local function control_attack(c)
    if c.state_step == 0 then
        if c.puppet_nurse_release_on_free == 0 then
            c.anim_status = 2
        elseif sh.save.game_difficulty() == HARD then
            c.anim_status = 2
        else
            c.anim_status = 42
        end
        c.puppet_nurse_release_on_free = sh.entity.any_npc_active()
        c.state_step = c.state_step + 1
        c.puppet_nurse_timer = 4096
        return
    end

    if c.puppet_nurse_timer < 0 and c.puppet_nurse_flags & FLAG_ATTACK_HIT ~= 0 then
        c.puppet_nurse_timer = 409600
        sfx_play(c, 0)
    end
    if c.anim_status == 36 then
        state_set(c, 9)
    end
    if c.puppet_nurse_release_on_free ~= 0 and player.attack_received == -1 then
        state_set(c, 8)
        return
    end
    attack_steer(c)
    c.puppet_nurse_timer = c.puppet_nurse_timer - sh.game.delta_time()
end

local function control_stagger(c)
    if c.state_step == 0 then
        sfx_play(c, 2)
        c.anim_status = 4
        c.state_step = c.state_step + 1
    end
    if c.anim_status == 36 then
        state_set(c, 9)
    end
    speed_toward(c, 16384, 0)
end

-- `facing_away`: the hit came from behind (state 3), the other fall.
local function control_down(c, facing_away)
    if c.state_step == 0 then
        if c.anim_status == 5 then
            return
        end
        sfx_play(c, 2)
        c.anim_status = DOWN_STATUS[facing_away]
        sh.entity.set_collision_state(c.slot, 4)
        c.flags = c.flags | CHARA_FLAG_TARGETED
        c.state_step = c.state_step + 1
    end

    local status = c.anim_status
    if status == 29 or status == 11 or status == 10 or status == 31 then
        if sh.entity.npc_index(c.slot) ~= sh.entity.sys("target_npc") and c.health == 0 then
            c.health = -1
            sh.entity.set_collision_state(c.slot, 0)
            sh.entity.anim_status_set(c.slot, DOWN_BLOOD[facing_away], 11)
            c.puppet_nurse_flags = c.puppet_nurse_flags | FLAG_DOWN
        elseif c.anim_status == 31 and c.health > 0 then
            c.anim_status = 10
        end
    end
    speed_toward(c, 16384, 0)
end

-- Holds Harry at grab distance; false when he is too far.
local function grab_steer(c)
    local offset = 3194
    local angle = s16(player_angle(c))
    local facing = abs(s16(angle_signed(player.rot_y - c.rot_y)))
    if facing > 1024 then
        offset = 3481
    end
    local dist = s32(player_distance(c) - offset)
    if dist > 2048 then
        return false
    end
    local limit
    if abs(dist) > 819 then
        limit = 819
    elseif abs(dist) > 204 then
        limit = dist > 0 and 2457 or -2457
    else
        limit = 0
    end
    speed_toward(c, 8192, limit)
    face_player(c, angle, 45)
    return true
end

local function control_grab(c)
    if c.state_step == 0 then
        c.anim_status = 12
        c.puppet_nurse_timer = 0
        c.state_step = c.state_step + 1
    end
    grab_steer(c)

    local step = c.state_step
    if step == 1 then
        if c.puppet_nurse_flags & FLAG_ATTACK_HIT ~= 0 then
            c.state_step = 2
        elseif c.puppet_nurse_timer <= 6144 then
            if sh.entity.any_npc_active() ~= 0 then
                c.control_state = step
                c.state_step = 0
                c.anim_status = 36
                return
            end
        else
            state_set(c, 13)
            return
        end
    elseif step == 2 then
        if player.attack_received == -1 then
            state_set(c, 8)
            return
        end
        if c.puppet_nurse_timer >= 6144 then
            local drain = sh.game.fix_nurse_grab_drain() ~= 0 and GRAB_DRAIN_ATTACK_FIXED or GRAB_ATTACK
            local power = sh.entity.weapon_attack_stats(drain)
            player.damage_amount = player.damage_amount + power * 4096
            c.puppet_nurse_timer = 0
        end
    end
    c.puppet_nurse_timer = c.puppet_nurse_timer + sh.game.delta_time()
end

local function control_taunt(c, doctor_clip)
    if c.state_step == 0 then
        sfx_play(c, doctor_clip and 2 or 1)
        c.anim_status = doctor_clip and 16 or 14
        c.state_step = c.state_step + 1
    end
    if c.anim_status == 36 then
        state_set(c, 9)
    end
    speed_toward(c, 16384, 0)
end

local function control_release(c)
    local step = c.state_step
    if step == 0 then
        local angle = angle_signed((player.rot_y - c.rot_y) - 1024)
        c.puppet_nurse_timer = abs(angle)
        c.state_step = c.state_step + 1
    elseif step == 1 then
        if c.puppet_nurse_timer < 0 then
            c.move_speed = -4096
            c.anim_status = 20
            c.state_step = 2
        end
        c.puppet_nurse_timer = c.puppet_nurse_timer - sh.game.delta_time()
    elseif step == 2 then
        if c.anim_status == 36 then
            local state
            if sh.save.game_difficulty() == HARD then
                state = 9
            else
                state = RELEASE_STATES[asr(sh.rng.rand16(), 4) & 7]
            end
            state_set(c, state)
        else
            local speed = sh.rng.uint(0, 511)
            speed_toward(c, speed + 3276, 0)
        end
    end
end

local function senses()
    local flags = sh.entity.sys("map_effects") & 3
    if flags == 0 then
        return SENSES[0]
    elseif flags == 2 then
        return SENSES[1]
    end
    return SENSES[2]
end

local function wander(c)
    speed_toward(c, 5734, q12_mul(stats.wander, 1105))
    if sh.rng.uint(0, 63) == 0 then
        c.puppet_nurse_target_heading = sh.entity.clear_heading(c.slot, 4096, c.rot_y)
    end
    turn_towards(c, c.puppet_nurse_target_heading)
end

local function chase(c)
    speed_toward(c, 5734, q12_mul(stats.wander, 1105))
    sh.rng.rand16()
    c.puppet_nurse_target_heading = sh.entity.heading_toward(c.slot, 4096, player.pos_x, player.pos_z, 4096, 1)
    turn_towards(c, c.puppet_nurse_target_heading)
end

-- Harry seen, or heard within the last second.
local function player_sensed(c, dist)
    local s = senses()
    if c.puppet_nurse_alert_timer > 0 then
        c.puppet_nurse_alert_timer = c.puppet_nurse_alert_timer - sh.game.delta_time()
    end
    if sh.entity.player_noise(c.slot, dist, s.hear) ~= 0 then
        c.puppet_nurse_alert_timer = 4096
    end
    return sh.entity.player_visible(c.slot, 1, q12_mul(s.see_near, stats.sight), q12_mul(s.see_far, stats.sight)) ~= 0
        or c.puppet_nurse_alert_timer > 0
end

local function control_hunt(c)
    local dist = player_distance(c)
    local angle_abs = abs(s16(player_angle(c)))

    if c.state_step == 0 then
        c.puppet_nurse_timer = 0
        c.puppet_nurse_target_heading = sh.entity.clear_heading(c.slot, 4096, c.rot_y)
        c.state_step = c.state_step + 1
    end

    local sensed = player_sensed(c, dist)
    c.attack_window = 0

    local step = c.state_step
    if step == 1 and not sensed then
        wander(c)
    elseif step == 1 or step == 2 then
        if step == 1 then
            c.state_step = 2
        end
        if not sensed then
            c.state_step = 1
        else
            chase(c)
            if dist < q12_mul(stats.sight, 12288) and angle_abs < 682 and player.health > 0 then
                state_set(c, 12)
            end
        end
    end

    if c.state_step ~= 0 and idle_status(c.anim_status) then
        if sh.rng.uint(0, 31) == 0 then
            local rng = sh.rng.rand16() & 0xFFFF
            c.anim_status = TAUNT_STATUS[asr(rng, 7) & 1]
            state_set(c, 6)
            return
        else
            c.anim_status = IDLE_STATUS[asr(sh.rng.rand16(), 2) & 3]
        end
    end
end

-- Closes in to swing range; the distance left.
local function approach_player(c)
    local dist = distance_2d(player.pos_x - c.pos_x, player.pos_z - c.pos_z) - 3112
    if abs(dist) > 122 and c.anim_status ~= 40 then
        local speed, limit
        if dist > 0 then
            speed = 32768
            limit = q12_mul(stats.scale, 7372)
        else
            limit = -81
            speed = 8192
        end
        speed_toward(c, speed, limit)
    else
        speed_toward(c, 12288, 0)
    end
    return dist
end

local function steer_to_player(c)
    if sh.rng.uint(0, 255) >= 128 then
        c.puppet_nurse_target_heading = sh.entity.heading_toward(c.slot, 4096, player.pos_x, player.pos_z, 4096, 1)
    end
    turn_towards(c, c.puppet_nurse_target_heading)
end

-- Math_Distance2dCheck: true when the points are further apart than `radius`.
local function farther_than(x0, z0, x1, z1, radius)
    local dx = x0 - x1
    if radius < dx or radius < -dx then
        return true
    end
    local dz = z0 - z1
    if radius < dz or radius < -dz then
        return true
    end
    return s32(q12_mul(dx, dx) + q12_mul(dz, dz)) > q12_mul(radius, radius)
end

-- Another standing nurse or doctor within 4 m that Harry is not targeting.
local function partner_near(c)
    for slot = 1, NPC_COUNT do
        local o = record(slot)
        local id = o.chara_id
        -- Another nurse: the map's own ones differ by spawn row; spawns of the engine have none, so the slot.
        local other = o.spawn_index ~= c.spawn_index
        if engine_spawn(c) then
            other = slot ~= c.slot
        end
        if (id == PUPPET_NURSE or id == PUPPET_DOCTOR) and o.health > 0 and other
            and not farther_than(c.pos_x, c.pos_z, o.pos_x, o.pos_z, 16384) then
            if o.flags & CHARA_FLAG_TARGETED == 0 then
                return true
            end
        end
    end
    return false
end

local function control_close_in(c)
    local status = c.anim_status
    local angle = player_angle(c)
    local angle_abs = s16(abs(angle))
    local dist = distance_2d(player.pos_x - c.pos_x, player.pos_z - c.pos_z)
    dist = s32(asr(dist * 4505, 12))
    local sensed = player_sensed(c, dist)

    if player.health < 0 or not sensed then
        c.control_state = 9
        c.state_step = 0
        c.anim_status = 36
        return
    end

    local step = c.state_step
    if step == 0 then
        if angle_abs >= 114 then
            local add = q12_mul(sh.game.delta_time(), 512)
            if angle > 0 then
                c.rot_y = c.rot_y + add
            else
                c.rot_y = c.rot_y - add
            end
            speed_toward(c, 4096, 0)
            return
        end
        c.state_step = 1
        step = 1
    end
    if step == 1 then
        if status >= 34 and status < 36 then
            c.anim_status = 18
            c.state_step = c.state_step + 1
        elseif status >= 36 and status < 38 then
            c.anim_status = 40
            c.state_step = c.state_step + 1
        else
            return
        end
        steer_to_player(c)
        step = 2
    end
    if step == 2 then
        c.attack_window = 0
        dist = approach_player(c)
        if sh.entity.any_npc_active() == 0 and partner_near(c) then
            if dist < 409 then
                state_set(c, 5)
            end
        elseif dist < 614 then
            c.puppet_nurse_release_on_free = player.flags & 8 ~= 0 and 1 or 0
            c.control_state = 1
            c.state_step = 0
            c.anim_status = 36
        end
        steer_to_player(c)
    end
end

local function control_idle(c)
    if c.state_step == 0 then
        c.puppet_nurse_timer = 0
        c.anim_status = 38
        c.state_step = c.state_step + 1
    end
    if player_sensed(c, player_distance(c)) then
        state_set(c, 9)
        c.anim_status = (sh.rng.rand16() & 0x80 ~= 0) and 14 or 16
    end
end

local function control_dormant(c)
    if c.state_step == 0 then
        c.puppet_nurse_timer = 0
        c.anim_status = 34
        c.puppet_nurse_target_heading = sh.entity.clear_heading(c.slot, 4096, c.rot_y)
        c.state_step = c.state_step + 1
    end

    if idle_status(c.anim_status) then
        if sh.rng.uint(0, 15) == 0 then
            state_set(c, (sh.rng.rand16() & 0x80 ~= 0) and 6 or 7)
        else
            c.anim_status = IDLE_STATUS[asr(sh.rng.rand16(), 2) & 3]
        end
    end

    if player_sensed(c, player_distance(c)) then
        state_set(c, 9)
        return
    end

    speed_toward(c, 5734, 1105)
    if sh.rng.uint(0, 63) == 0 then
        local x = c.puppet_nurse_home_x + (sh.rng.rand16() % 12288 - 6144)
        local z = c.puppet_nurse_home_z + (sh.rng.rand16() % 12288 - 6144)
        c.puppet_nurse_target_heading = sh.entity.heading_toward(c.slot, 4096, x, z, 4096, 1)
    end
    turn_towards(c, c.puppet_nurse_target_heading)
end

local function control_recover(c)
    if c.state_step == 0 then
        c.anim_status = 20
        c.state_step = c.state_step + 1
    end
    if c.anim_status == 36 then
        state_set(c, 9)
    end
    speed_toward(c, 16384, 0)
end

local CONTROL = {
    [1] = control_attack,
    [2] = control_stagger,
    [3] = function(c) control_down(c, true) end,
    [4] = function(c) control_down(c, false) end,
    [5] = control_grab,
    [6] = function(c) control_taunt(c, false) end,
    [7] = function(c) control_taunt(c, true) end,
    [8] = control_release,
    [9] = control_hunt,
    [10] = control_idle,
    [11] = control_dormant,
    [12] = control_close_in,
    [13] = control_recover,
}

local function control(c)
    local f = CONTROL[c.control_state]
    if f ~= nil then
        f(c)
    end
end

-- The walk plus the decaying push of the last hit.
local function move(c)
    local push = stats.push
    local px, py, pz = c.puppet_nurse_push_x, c.puppet_nurse_push_y, c.puppet_nurse_push_z
    local speed = c.move_speed
    c.puppet_nurse_walk_speed = speed

    local rot = c.rot_y
    local x = s32(q12_mul(push, px) + q12_mul(speed, sh.math.sin(rot)))
    local z = s32(q12_mul(push, pz) + q12_mul(speed, sh.math.cos(rot)))

    c.move_speed = sh.entity.sqrt_q12(s32(q12_mul(x, x) + q12_mul(z, z)))
    c.heading_angle = sh.math.atan2(x, z)
    c.fall_speed = c.fall_speed + sh.game.gravity_speed()
    sh.entity.movement_update(c.slot)

    c.move_speed = c.puppet_nurse_walk_speed
    local vblanks = sh.game.vblanks()
    local function decay(v)
        local r = sh.entity.sqrt_q12(asr(q12_mul(v, v), vblanks))
        if v <= 0 then
            r = -r
        end
        return r
    end
    c.puppet_nurse_push_x = decay(c.puppet_nurse_push_x)
    c.puppet_nurse_push_y = decay(c.puppet_nurse_push_y)
    c.puppet_nurse_push_z = decay(c.puppet_nurse_push_z)
    c.rot_y = angle_signed(c.rot_y)
end

local function anim_update(c)
    local sfx_before = keyframe_sfx_index(asr(c.anim_time, 12))
    sh.held_item_attach(c.chara_id, c.puppet_nurse_model_variant)
    sh.entity.root_transform(c.slot)
    if c.anim_status ~= 0 then
        sh.entity.anim_step(c.slot)
    end
    sh.entity.bone_matrix_scale(c.slot, 0, stats.scale)
    local sfx_after = keyframe_sfx_index(asr(c.anim_time, 12))
    if sfx_after ~= sfx_before and sfx_after ~= NO_SFX then
        sfx_play(c, sfx_after)
    end
end

-- The cylinder offset follows the box offset, at most 1/32 m per tick when it is off.
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

-- The collision shapes from the torso, head and shins.
local function shapes_from_bones(c)
    -- A spawned placeholder model (the Dummy types' 256-byte stub) has none of these bones: keep the shapes.
    if engine_spawn(c) and not pcall(sh.entity.bone_point, c.slot, BONE_RIGHT_SHIN, 0, 0, 0) then
        return
    end
    local tx, ty, tz = sh.entity.bone_point(c.slot, BONE_TORSO, 0, 0, 0)
    local hx, hy, hz = sh.entity.bone_point(c.slot, BONE_HEAD, 0, 0, 0)
    local rx, ry, rz = sh.entity.bone_point(c.slot, BONE_RIGHT_SHIN, 0, 0, 0)
    local lx, ly, lz = sh.entity.bone_point(c.slot, BONE_LEFT_SHIN, 0, 0, 0)

    local pos_y = c.pos_y
    local right_y = s32(ry << 4)
    local left_y = s32(ly << 4)
    c.box_top = s32(hy << 4)
    c.box_top = c.box_top - (pos_y + 1024)
    c.box_bottom = pos_y
    if right_y >= left_y then
        c.box_height = left_y - c.box_bottom
    else
        c.box_height = right_y - c.box_bottom
    end
    c.box_offset_y = idiv(s32((hy << 4) + (ty << 4)), 2) - pos_y

    -- Centre and half the diagonal of the bones' extent on the ground.
    local min_x, max_x, min_z, max_z = tx, tx, tz, tz
    for _, p in ipairs({ { hx, hz }, { rx, rz }, { lx, lz } }) do
        if min_x >= p[1] then min_x = p[1] end
        if p[1] > max_x then max_x = p[1] end
        if min_z >= p[2] then min_z = p[2] end
        if p[2] > max_z then max_z = p[2] end
    end
    local w = s32((max_x - min_x) << 4)
    local d = s32((max_z - min_z) << 4)
    local radius = sh.entity.sqrt_q12(s32(q12_mul(w, w) + q12_mul(d, d)))
    local cx = s32((min_x << 4) + idiv(w, 2))
    local cz = s32((min_z << 4) + idiv(d, 2))
    radius = idiv(radius, 2)

    local box_x = cx - c.pos_x
    local box_z = cz - c.pos_z

    -- The radius follows by at most 1/32 m per tick, capped at 0.35 m.
    local r = radius + 204
    local current = c.cylinder_radius
    local delta = r - current
    if abs(delta) > 128 then
        if delta <= 0 then
            r = current - 128
        else
            r = current + 128
        end
    end
    if r > 1433 then
        r = 1433
    end
    c.cylinder_extent = r - 204
    c.cylinder_radius = r
    c.box_offset_x = box_x
    c.box_offset_z = box_z
    cylinder_follow(c, box_x, box_z)

    c.puppet_nurse_pos_x = c.pos_x
    c.puppet_nurse_pos_z = c.pos_z
end

local function shapes_update(c)
    if c.anim_flags & ANIM_FLAG_VISIBLE ~= 0 then
        shapes_from_bones(c)
        return
    end
    local pos_y = c.pos_y
    c.box_bottom = pos_y
    c.box_height = pos_y
    c.box_top = pos_y - 6963
    c.box_offset_y = pos_y - 4096
end

-- The swing (state 1) and the grab (state 5) reach Harry from the hand.
local function attack_update(c)
    c.puppet_nurse_flags = c.puppet_nurse_flags & ~FLAG_ATTACK_HIT
    local state = c.control_state
    if state == 1 or state == 5 then
        local x, y, z = sh.entity.bone_point(c.slot, BONE_HAND, 0, 0, 0)
        x, y, z = s32(x << 4), s32(y << 4), s32(z << 4)
        c.box_extent_x = c.pos_y - y
        local attack = state == 1 and stats.attack or GRAB_ATTACK
        if sh.entity.attack(c.slot, 1, attack, x, y, z, 0, c.rot_y, 1024) ~= -1 then
            c.puppet_nurse_flags = c.puppet_nurse_flags | FLAG_ATTACK_HIT
        end
    end
end

function update(slot)
    local c = record(slot)
    local id = c.chara_id
    local doctor = id == PUPPET_DOCTOR or id == DUMMY_DOCTOR
    local index = sh.entity.puppet_nurse_stats(slot)
    if c.control_state == 0 or index < 0 then
        init(c, doctor)
    else
        stats = STATS[index]
    end

    if sh.game.delta_time() ~= 0 then
        damage_handle(c)
        control(c)
        if c.state_step == 0 then
            control(c)
        end
        move(c)
        anim_update(c)
        shapes_update(c)
        attack_update(c)
    else
        anim_update(c)
    end
end
