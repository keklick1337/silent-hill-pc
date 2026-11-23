-- Bloodsucker (map3_s03, the hospital's flesh wall; map7_s02, the last nightmare): it does not move.
-- Its tendril sways on a clip chosen by the spawn step and story flag M3S03_BloodPackPoured, strikes at Harry from
-- bone 14, and two looping voices (idle, attack) fade in and out with what it is doing.

local MAP = sh.map_name()
local HOSPITAL = MAP == "map3_s03"

local FLAG_IDLE_VOICE = 0x1
local FLAG_ATTACK_VOICE = 0x2
local BGM_RADIO = 0x40
local STRIKE_BONE = 14
local SHAPE = 204          -- Q12(0.05): half size of the tendril's hit box
local ATTACK = sh.entity.attack_number("BloodsuckerTentacle")

-- Map data (chara/fallbacks.lua). The hospital wall strikes while Harry stands at it (x below -140.75, back off
-- past -140.5) and calms down once he poured the blood pack (story flag M3S03_BloodPackPoured). A spawn of the
-- engine has neither: it strikes while Harry is within 2.5 m (back off past 2.75 m) and is never calmed; it runs
-- the hospital's control code on any map (map7_s02's own code only holds the pose of its scene).
local NEAR, FAR = 10240, 11264        -- Q12(2.5), Q12(2.75)

local function player_distance(c)
    return distance_2d(sh.entity.get(0, "pos_x") - c.pos_x, sh.entity.get(0, "pos_z") - c.pos_z)
end

fallbacks = {
    pack_poured = false,
    player_near = function(c) return player_distance(c) < NEAR end,
    player_far = function(c) return player_distance(c) > FAR end,
}

local function pack_poured(c)
    return map_data(c, "pack_poured", sh.flag_get, "M3S03_BloodPackPoured")
end

local function player_near(c)
    return map_data(c, "player_near", function() return sh.entity.get(0, "pos_x") < -576512 end)   -- Q12(-140.75)
end

local function player_far(c)
    return map_data(c, "player_far", function() return sh.entity.get(0, "pos_x") > -575488 end)    -- Q12(-140.5)
end

-- The hospital's control code (map3_s03), or a spawn of the engine anywhere.
local function hospital_code(c)
    return HOSPITAL or not map_provides(c)
end

local function start(c)
    sh.entity.props_clear(c.slot)
    sh.entity.set_collision_state(c.slot, 4)
    c.heading_angle = c.rot_y

    local step = c.state_step
    if step == 17 then
        c.control_state = 1
        c.state_step = 0
        anim_set(c, anim_status(7, true), 81)
    elseif (MAP ~= "map7_s02" or not map_provides(c)) and step >= 18 and step <= 20 then
        if step == 18 then
            c.health = 1
        end
        c.control_state = 2
        step = (step - 18) & 0xFF
        c.state_step = step
        local status
        if not pack_poured(c) then
            status = step * 2 + 23
        else
            status = step * 2 + 17
        end
        c.anim_status = status
        local first = sh.entity.clip_start(c.slot, c.anim_status)
        c.anim_time = first * 4096
        c.anim_keyframe = first
    end

    sh.entity.clip_table_attach(c.slot)
    damage_clear(c)
end

local function control(c)
    local state = c.control_state
    local step2 = c.state_step * 2
    if state == 1 then
        c.anim_time = 81 * 4096 + c.bloodsucker_anim_offset
    elseif state == 2 then
        if hospital_code(c) then
            if c.anim_status == step2 + 9 then
                c.anim_status = step2 + 22
            end
            if player_near(c) then
                c.control_state = 3
            end
            c.bloodsucker_idle_volume_target = 1228      -- Q12(0.3)
        end
    elseif state == 3 then
        if c.anim_status == step2 + 23 then
            c.anim_status = step2 + 8
        end
        if player_far(c) then
            c.control_state = 2
        end
        c.bloodsucker_idle_volume_target = 4096
    elseif state == 4 then
        if hospital_code(c) then
            local status = c.anim_status
            if status ~= step2 + 2 and status ~= step2 + 3 and status ~= step2 + 16 and status ~= step2 + 17 then
                c.anim_status = step2 + 2
            end
            if c.anim_status & 1 ~= 0 then
                c.bloodsucker_idle_volume_target = 1228
            else
                c.bloodsucker_idle_volume_target = 3072
            end
        end
    end
end

-- The tendril's hit box follows bone 14; the strike comes from 22 units up the bone.
local function strike(c)
    anim_play(c)
    local x, y, z = sh.entity.bone_point(c.slot, STRIKE_BONE, 0, 0, 0)
    x, y, z = s32(x << 4), s32(y << 4), s32(z << 4)
    c.box_top = y - SHAPE
    c.box_bottom = y + SHAPE
    c.box_height = y + SHAPE
    c.box_offset_y = y
    c.box_offset_x = x - c.pos_x
    c.box_offset_z = z - c.pos_z
    c.cylinder_extent = SHAPE
    c.cylinder_offset_x = x - c.pos_x
    c.cylinder_offset_z = z - c.pos_z
    c.cylinder_radius = SHAPE

    local ax, ay, az = sh.entity.bone_point(c.slot, STRIKE_BONE, 0, -22, 0)
    c.attack_window = 1
    sh.entity.attack(c.slot, 1, ATTACK, s32(ax << 4), s32(ay << 4), s32(az << 4), 0, 1024, 1024)
end

local function voices(c)
    local idle = sound(c, "idle_loop", 1525)
    local attack = sound(c, "attack_loop", 1527)
    if c.bloodsucker_flags & FLAG_IDLE_VOICE == 0 then
        sh.sound.sd_call(idle)
        c.bloodsucker_flags = c.bloodsucker_flags | FLAG_IDLE_VOICE
    end
    sh.entity.sfx_falloff(c.slot, idle, asr(c.bloodsucker_idle_volume, 5), 65536, 0)

    local dt = sh.game.delta_time()
    local volume, target = c.bloodsucker_idle_volume, c.bloodsucker_idle_volume_target
    local fade = asr(dt * 2048, 12)                      -- dt * 0.5
    if volume < target then
        volume = volume + fade
        c.bloodsucker_idle_volume = volume > target and target or volume
    elseif volume > target then
        volume = volume - fade
        c.bloodsucker_idle_volume = volume < target and target or volume
    end

    -- The striking clips: 11 and 8 (the two clip sets of flag M3S03_BloodPackPoured).
    local clip = c.anim_status // 2
    if clip == 11 or clip == 8 then
        if c.bloodsucker_flags & FLAG_ATTACK_VOICE == 0 then
            c.bloodsucker_flags = c.bloodsucker_flags | FLAG_ATTACK_VOICE
            sh.sound.sd_call(attack)
        end
        local v = c.bloodsucker_attack_volume + asr(dt * 8192, 12)   -- dt * 2
        c.bloodsucker_attack_volume = v > 4096 and 4096 or v
        sh.entity.sfx_falloff(c.slot, attack, asr(c.bloodsucker_attack_volume, 5), 65536, 0)
    elseif c.bloodsucker_flags & FLAG_ATTACK_VOICE ~= 0 then
        local v = c.bloodsucker_attack_volume - asr(dt * 8192, 12)
        c.bloodsucker_attack_volume = v
        if v < 0 then
            c.bloodsucker_attack_volume = 0
            c.bloodsucker_flags = c.bloodsucker_flags & ~FLAG_ATTACK_VOICE
            sh.sound.stop(attack)
        end
    end
end

function update(slot)
    local c = record(slot)
    if c.control_state == 0 then
        start(c)
    end
    control(c)
    strike(c)

    if sh.sound.bgm_flags() & BGM_RADIO ~= 0 then
        c.bloodsucker_flags = c.bloodsucker_flags & ~FLAG_IDLE_VOICE & ~FLAG_ATTACK_VOICE
    end
    if c.state_step ~= 0 then
        return
    end
    voices(c)
end
