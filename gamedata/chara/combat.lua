-- Shared helpers of the enemy behaviours.
-- The engine runs chara/common.lua, then this file, then chara/<name>/behavior.lua in the
-- type's Lua state. A mod may replace it (mods/<mod>/chara/combat.lua).
--
-- Integer rules as in common.lua. Products of two Q12 values come in two kinds: q12_mul (64-bit
-- intermediate) and q12_mul32 (the product wraps at 32 bits before the shift), both in common.lua,
-- matching how the game computed each value.

function abs(x)
    return x < 0 and -x or x
end

function min(a, b)
    return a < b and a or b
end

function max(a, b)
    return a > b and a or b
end

-- Moves `current` towards `target` by at most `step`.
function approach(current, target, step)
    if current > target then
        return max(current - step, target)
    end
    return min(target, current + step)
end

-- Speed towards `limit` at `rate` per second (Q12), scaled by the frame time.
function speed_toward(c, rate, limit)
    c.move_speed = approach(c.move_speed, limit, q12_mul(sh.game.delta_time(), rate))
end

-- The angle in [-2048, 2048] (an angle is a 16-bit value; 2048 stays positive).
function angle_signed(a)
    a = a & 0xFFF
    if a > 2048 then
        return a - 4096
    end
    return a
end

-- Heading (Q12 turns) from one point to another on the ground.
function heading_to(from_x, from_z, to_x, to_z)
    return sh.math.atan2(s32(to_x - from_x), s32(to_z - from_z))
end

-- Distance on the ground (Q12), with the components in Q6 so it cannot overflow.
function distance_2d(dx, dz)
    local x = asr(dx, 6)
    local z = asr(dz, 6)
    return s32(sh.math.sqrt(s32(s32(x * x) + s32(z * z))) << 6)
end

-- The smallest turn worth making this frame: (dt / denom) >> shift, plus one.
function turn_step(denom, shift)
    return asr(idiv(sh.game.delta_time(), denom), shift) + 1
end

function damage_clear(c)
    c.damage_amount = 0
    c.damage_z = 0
    c.damage_y = 0
    c.damage_x = 0
end

-- A clip status and keyframe straight away (no blending).
function anim_set(c, status, keyframe)
    c.anim_status = status
    c.anim_time = keyframe * 4096
    c.anim_keyframe = keyframe
end

-- Collision shape of an animation keyframe: {top, bottom, height, offset_y, radius, extent,
-- box_offset_x, box_offset_z, cylinder_offset_x, cylinder_offset_z}.
function collision_from_keyframe(c, k)
    c.box_top = k[1]
    c.box_bottom = k[2]
    c.box_height = k[3]
    c.box_offset_y = k[4]
    c.cylinder_offset_x = k[9]
    c.cylinder_offset_z = k[10]
    c.cylinder_radius = k[5]
    c.box_offset_x = k[7]
    c.box_offset_z = k[8]
    c.cylinder_extent = k[6]
end

-- The shape between two keyframes: by the keyframe fraction while the clip plays, by the blend
-- alpha while it blends in.
function collision_between(c, k0, k1)
    local alpha
    if c.anim_status & 1 ~= 0 then
        alpha = c.anim_time & 0xFFF
    else
        alpha = c.anim_alpha
    end
    local inv = 4096 - alpha
    local function mix(i)
        return asr(s32(k0[i] * inv + k1[i] * alpha), 12)
    end
    c.box_top = mix(1)
    c.box_bottom = mix(2)
    c.box_height = mix(3)
    c.box_offset_y = mix(4)
    c.cylinder_offset_x = mix(9)
    c.cylinder_offset_z = mix(10)
    c.cylinder_radius = mix(5)
    c.box_offset_x = mix(7)
    c.box_offset_z = mix(8)
    c.cylinder_extent = mix(6)
end

-- Root bone at the record's position, then one tick of the clip of the current status.
function anim_play(c)
    sh.entity.root_transform(c.slot)
    sh.entity.anim_step(c.slot)
end

-- The packed weapon attack of an attack kind (WEAPON_ATTACK: id + input * 10).
function weapon_attack(id, input)
    return id + input * 10
end

-- A mod's weapon packs its attacks into rows past the original's table, which the
-- original's arithmetic (melee below the handgun's tap, the weapon as attack % 10) does not decode: the engine
-- names the row's weapon, input and class. false for every attack of the original.
local mod_rows = {}
function mod_attack(attack)
    local row = mod_rows[attack]
    if row == nil then
        local weapon, input, gun = sh.entity.mod_attack(attack)
        row = weapon >= 0 and { weapon = weapon, input = input, gun = gun ~= 0 } or false
        mod_rows[attack] = row
    end
    return row
end

-- A gun's packed attack; `first_gun` is the handgun's tap, where the original's guns start.
function gun_attack(attack, first_gun)
    local row = mod_attack(attack)
    if row then
        return row.gun
    end
    return attack >= first_gun
end

-- The base weapon a mod's weapon borrows the details of (gamedata/weapons "like"; by default the steel pipe or the
-- handgun); -1 when its "like" names no base weapon.
local likes = {}
function weapon_like(number)
    local like = likes[number]
    if like == nil then
        local _
        _, _, _, _, like = sh.entity.weapon_keyframes(number)
        likes[number] = like
    end
    return like
end

-- The weapon of a packed attack (C remainder for the original's: -1 stays -1).
function attack_weapon(attack)
    local row = mod_attack(attack)
    if row then
        return row.weapon
    end
    return attack - idiv(attack, 10) * 10
end

-- The light of the area (sight ranges depend on it): bits 0 and 1 of the map effects.
function map_light()
    return sh.entity.sys("map_effects")
end

function group_flags(group)
    return sh.entity.sys("chara_group_flags", group)
end

function set_group_flags(group, v)
    sh.entity.set_sys("chara_group_flags", v, group)
end

-- NPC index of a slot (Harry: one past the last NPC), as the game numbers its characters.
function npc_index(slot)
    if slot == 0 then
        return 6
    end
    return slot - 1
end

-- A random integer in [low, high] from the game's generator (signed remainder, C rules).
function rng_int(low, high)
    return math.fmod(sh.rng.rand16(), high - low + 1) + low
end

-- True when (bx, bz) is outside `radius` of (ax, az) on the ground (square test first).
function far_2d(ax, az, bx, bz, radius)
    local dx = s32(ax - bx)
    if radius < dx or radius < -dx then
        return true
    end
    local dz = s32(az - bz)
    if radius < dz or radius < -dz then
        return true
    end
    return s32(q12_mul(dx, dx) + q12_mul(dz, dz)) > q12_mul(radius, radius)
end

-- The NPC slots (1..32).
NPC_SLOTS = 32

-- The same with an unsigned remainder (the game's Rng_GenerateUInt): a negative span wraps.
function rng_uint(low, high)
    local span = (high - low + 1) & 0xFFFFFFFF
    return s32((sh.rng.rand16() % span) + low)
end

-- The collision cylinder's offset follows (x, z) (Q12, from the root); while the current offset is
-- more than sqrt(range * 4 / 4096) away it trails 128 behind the new point (the bosses of map7_s03).
function shape_follow(c, x, z, range)
    local ex = s32(c.cylinder_offset_x - x)
    local ez = s32(c.cylinder_offset_z - z)
    local dist_sq = s32(q12_mul(ex, ex) + q12_mul(ez, ez))
    if dist_sq > q12_mul(range, 4) then
        local root = sh.entity.sqrt_q12(dist_sq)
        local scale = idiv(s32((root - 128) * 4096), root)
        local sx = s32(scale * ex)
        if sx < 0 then
            sx = s32(sx + 4095)
        end
        local sz = s32(scale * ez)
        if sz < 0 then
            sz = s32(sz + 4095)
        end
        x = s32(x + asr(sx, 12))
        z = s32(z + asr(sz, 12))
    end
    c.cylinder_offset_x = x
    c.cylinder_offset_z = z
end
