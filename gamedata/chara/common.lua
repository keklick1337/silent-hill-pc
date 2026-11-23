-- Shared helpers of the character behaviours.
-- The engine runs this file first, then chara/<name>/behavior.lua, in one Lua state per type;
-- the type's update(slot) is called once per tick. A mod may replace
-- this file like any other (mods/<mod>/chara/common.lua).
--
-- Integer rules (the behaviour must match the game bit for bit): values are Q12 integers
-- (4096 = 1.0, angles 4096 = 360 degrees); Lua integers are 64-bit, so 32-bit wrap-around is
-- explicit (s32/s16), arithmetic shifts are floor divisions (x // 2^n), C division truncates
-- towards zero (idiv). Random numbers come only from sh.rng.* (the game's generator).

-- 32-bit and 16-bit two's complement wrap.
function s32(x)
    x = x & 0xFFFFFFFF
    if x >= 0x80000000 then
        return x - 0x100000000
    end
    return x
end

function s16(x)
    x = x & 0xFFFF
    if x >= 0x8000 then
        return x - 0x10000
    end
    return x
end

-- C integer division: truncates towards zero.
function idiv(a, b)
    local q = a // b
    if q < 0 and q * b ~= a then
        q = q + 1
    end
    return q
end

-- Arithmetic shift right (sign-preserving), as the game's >> on signed values. The CPU takes
-- only the low 5 bits of the shift count (a frame hitch can make a count like vblanks huge).
function asr(x, n)
    return x // (1 << (n & 31))
end

-- Q12 product with a 64-bit intermediate, as the game multiplies two Q12 values.
function q12_mul(a, b)
    return s32((a * b) // 4096)
end

-- Q12 product whose intermediate wraps at 32 bits first, as the game's Q12_MULT of two 32-bit
-- values does (the product of the two factors is computed in the register width and wraps).
function q12_mul32(a, b)
    return asr(s32(a * b), 12)
end

-- Q12 value scaled by the frame time relative to a 30 fps tick (TIMESTEP_30_FPS = 136).
function timestep_30fps(x)
    return idiv(x * sh.game.delta_time(), 136)
end

-- The angle in [0, 4096) (turns wrap).
function angle_abs(a)
    return (a + 4096) & 0xFFF
end

-- Packed animation status: clip * 2 + phase (0 blending in, 1 playing).
function anim_status(clip, active)
    return clip * 2 + (active and 1 or 0)
end

-- Clip numbers by name in the game's clip order (the character data's "anims"): names[1] is the clip
-- the game numbers `first` (default 0), C[i] the data's number for the clip the game numbers i. A
-- name the data lacks keeps i, so a missing entry plays the same clip as before.
function clip_numbers(chara, names, first)
    local out = {}
    first = first or 0
    for k, name in ipairs(names) do
        local i = first + k - 1
        out[i] = sh.chara_anim(chara, name, i)
    end
    return out
end

-- Clip numbers of a type by name (the character data's "anims"); the game's own number is the
-- fallback, so a missing entry plays the same clip as before.
function clips(chara, names)
    local out = {}
    for name, fallback in pairs(names) do
        out[name] = sh.chara_anim(chara, name, fallback)
    end
    return out
end

-- A sound of the character by slot name (the character data's "sounds"); `fallback` is the game's
-- own id. Looked up once per Lua state: the data does not change while a map runs.
local sound_ids = {}
function sound(c, name, fallback)
    local id = sound_ids[name]
    if id == nil then
        id = sh.entity.sound(c.slot, name, fallback)
        sound_ids[name] = id
    end
    return id
end

-- The character record of a slot as fields: c.move_speed, c.npc_control_state = 0, ...
-- Every access reads or writes the live record.
local record_meta = {
    __index = function(t, k)
        return sh.entity.get(rawget(t, "slot"), k)
    end,
    __newindex = function(t, k, v)
        sh.entity.set(rawget(t, "slot"), k, v)
    end,
}

function record(slot)
    return setmetatable({ slot = slot }, record_meta)
end

-- True for a character the engine created (console, debug menu, a script's create) instead of the map's
-- room spawner: it has no spawn row of the map and no place in the map's story staging.
function engine_spawn(c)
    return sh.entity.engine_spawn(c.slot) ~= 0
end

-- Starts clip `clip` once per control state: only while state_step is 0, then the step moves on.
function anim_status_set(c, clip, active)
    if c.state_step == 0 then
        c.anim_status = anim_status(clip, active)
        c.state_step = c.state_step + 1
    end
end

-- The same, starting at the first keyframe of the clip table entry `entry_offset` (default 0)
-- after the status.
function anim_status_keyframe_set(c, clip, active, entry_offset)
    if c.state_step == 0 then
        local status = anim_status(clip, active)
        c.anim_status = status
        c.state_step = c.state_step + 1
        local start = sh.entity.clip_start(c.slot, status + (entry_offset or 0))
        c.anim_time = start * 4096
        c.anim_keyframe = start
    end
end

-- A cutscene asked the NPC to drop back to its first control state (npc_reset_control_state).
function anim_state_reset(c)
    if c.npc_reset_control_state ~= 0 then
        c.npc_control_state = 0
        c.state_step = 0
        c.npc_reset_control_state = 0
    end
end

-- Plays the clip entry of the current status for one tick unless a cutscene froze the NPC.
function anim_update(c)
    if c.npc_freeze == 0 then
        sh.entity.anim_step(c.slot)
    end
end

-- First tick of a cutscene NPC: no collision shape yet, standing, next control state.
function collision_reset(c)
    c.state_step = 0
    c.box_top = 0
    c.box_bottom = 0
    c.box_offset_y = 0
    c.cylinder_radius = 0
    c.cylinder_extent = 0
    c.cylinder_offset_z = 0
    c.cylinder_offset_x = 0
    c.box_offset_z = 0
    c.box_offset_x = 0
    c.player_ground_height = 0
    c.player_afk_timer = 0
    c.player_ground_height = 0
    c.npc_freeze = 0
    c.player_flex_angle_x = 0
    c.control_state = c.control_state + 1
    sh.entity.set_collision_state(c.slot, 3)
end

-- Step of `distance` (Q12) along `angle`: x = distance * sin, z = distance * cos. A step that does
-- not fit 16 bits is scaled down by 4 before the product and back up after (the game's guard).
function step_along(distance, angle)
    local shift = (((distance + 32767) & 0xFFFFFFFF) >= 65535) and 4 or 0
    local reduce = shift >> 1
    local d = asr(distance, reduce)
    local dx = s32(q12_mul(d, asr(sh.math.sin(angle), reduce)) << shift)
    local dz = s32(q12_mul(d, asr(sh.math.cos(angle), reduce)) << shift)
    return dx, dz
end

-- Cutscene NPC walk without collision: forward by move_speed along heading_angle, height 0, the
-- root bone placed at the new position (geometry units, Q8).
function move_flat(c, root_bone)
    local dx, dz = step_along(q12_mul(c.move_speed, sh.game.delta_time()), c.heading_angle)
    c.pos_x = c.pos_x + dx
    c.pos_y = 0
    c.pos_z = c.pos_z + dz
    sh.entity.bone_translation_set(c.slot, root_bone or 0, asr(c.pos_x, 4), asr(c.pos_y, 4), asr(c.pos_z, 4))
end

-- The common tail of a cutscene NPC's control update: turn by `turn` (Q12 per tick, already
-- scaled; nil: rot_y is left as it is), heading follows the body, speed from the cutscene's
-- npc_move_speed, gravity, root rotation from the record.
function control_tail(c, turn)
    if turn ~= nil then
        c.rot_y = angle_abs(c.rot_y + turn)
    end
    c.heading_angle = c.rot_y
    c.move_speed = c.npc_move_speed
    c.fall_speed = c.fall_speed + sh.game.gravity_speed()
    sh.entity.bone_flag_set(c.slot, 0, 0)
    sh.entity.bone_rotation_set(c.slot, 0, c.rot_x, c.rot_y, c.rot_z)
end

-- Footsteps of a two-step walk cycle: one sound when the keyframe reaches `keyframe1`, one at
-- `keyframe0` (bits 4 and 5 of npc_flags remember which already played this cycle).
function footstep_pair(c, status, keyframe0, keyframe1, sfx, pitch)
    if c.anim_status ~= status then
        return false
    end
    if c.anim_keyframe >= keyframe1 then
        if c.npc_flags & 0x10 == 0 then
            sh.entity.sfx_at(c.slot, sfx, 128, pitch)
            c.npc_flags = c.npc_flags | 0x10
            return true
        end
    else
        c.npc_flags = c.npc_flags & ~0x10
    end
    if c.anim_keyframe >= keyframe0 then
        if c.npc_flags & 0x20 == 0 then
            sh.entity.sfx_at(c.slot, sfx, 128, pitch)
            c.npc_flags = c.npc_flags | 0x20
            return true
        end
    else
        c.npc_flags = c.npc_flags & ~0x20
    end
    return false
end

-- Volumes (Q8) of the keyframe sounds where a map's copy of the code differs from 0.5, and the
-- extra sound flags (2: no stereo balance from distance). Key: map, then sound id.
local keyframe_sfx_volume = {}
local function volumes(maps, entries)
    for _, map in ipairs(maps) do
        keyframe_sfx_volume[map] = entries
    end
end
volumes({ "map6_s04" }, { [1620] = { 64, 0 }, [1621] = { 64, 0 }, [1622] = { 192, 0 }, [1638] = { 192, 0 },
                          [1631] = { 64, 0 }, [1632] = { 64, 0 } })
volumes({ "map3_s00", "map5_s02", "map5_s03" }, { [1492] = { 255, 2 }, [1594] = { 128, 0 } })
volumes({ "map3_s04", "map4_s04", "map7_s00" }, { [1639] = { 64, 0 } })
volumes({ "map0_s01", "map4_s01" }, { [1368] = { 64, 0 }, [1369] = { 64, 0 }, [1370] = { 64, 0 } })
volumes({ "map2_s01", "map3_s06" }, { [1488] = { 64, 0 }, [1496] = { 64, 0 } })
volumes({ "map6_s01" }, { [1488] = { 64, 0 }, [1496] = { 64, 0 }, [1368] = { 64, 0 }, [1369] = { 64, 0 },
                          [1370] = { 64, 0 } })
volumes({ "map7_s03" }, { [1539] = { 32, 0 }, [1674] = { 32, 0 }, [1675] = { 32, 0 }, [1622] = { 192, 0 } })

-- A sound once when the clip `status` reaches `keyframe` (bit 13 of npc_flags until the keyframe
-- goes back below it).
function keyframe_sfx(c, status, keyframe, sfx)
    if c.anim_status ~= status then
        return false
    end
    if c.anim_keyframe >= keyframe then
        if c.npc_flags & 0x2000 == 0 then
            local map = keyframe_sfx_volume[sh.map_name()]
            local entry = map and map[sfx] or { 128, 0 }
            sh.entity.sfx_flags(c.slot, sfx, entry[1], entry[2])
            c.npc_flags = c.npc_flags | 0x2000
            return true
        end
    else
        c.npc_flags = c.npc_flags & ~0x2000
    end
    return false
end
