-- Harry's extra-state setter, the Lua copy of the original's Player_ExtraStateSet macro: a new
-- state starts with every step and the body states cleared. Shared by damage.lua (the reaction
-- machine) and any later module that starts a state of Harry's own (
-- "Harry's damage and states").
--
-- behavior.lua names this file (-- uses: Harry/state): it runs before the other Harry modules
-- in the same Lua state. The c/h locals below are this module's own, exactly like combat.lua and
-- damage.lua define theirs (they read and write the same live record and state through them).

-- The same locals every Harry module defines for itself: `c` is Harry's record, `h` the
-- state outside it (sh.entity.harry / set_harry), exactly as combat.lua and damage.lua do;
-- common.lua (which defines record) runs before the character's modules.
local c = record(0)
local h = setmetatable({}, {
    __index = function(_, k)
        return sh.entity.harry(k)
    end,
    __newindex = function(_, k, v)
        sh.entity.set_harry(k, v)
    end,
})

function extra_state_set(state)
    h.state = state
    c.state_step = 0
    c.control_state = 0
    h.upper_state_step = 0
    h.upper_control_state = 0
    h.upper_body = 0
    h.lower_body = 0
end

-- The grab and its attacker are let go (the original's Player_CollisionStateReset). With `restore`
-- 1 the standing shapes come back too (radius 0.3, extent 0.23, box top -1.6, bottom 0,
-- offset y -1.1, no shape offsets). `restore` is the original's bool as a number: 0 is false,
-- since the engine passes it as one (sh.entity.harry_collision_state_reset of the map scripts).
function collision_state_reset(restore)
    h.grab_release_timer = 0
    c.harry_attacker = -1
    c.harry_flags = c.harry_flags & ~0x4000 -- the damage of the last hit has been reported
    h.grabber_front = -1
    h.grabber_back = -1
    h.grabber_unused_2 = -1
    h.grabber_unused_3 = -1
    if restore ~= 0 then
        c.cylinder_radius = 1228
        c.cylinder_extent = 942
        c.box_top = -6553
        c.cylinder_offset_z = 0
        c.cylinder_offset_x = 0
        c.box_offset_z = 0
        c.box_offset_x = 0
        c.box_bottom = 0
        c.box_offset_y = -4505
    end
    return 0
end

-- The sound of a keyframe of Harry's clip `status`, once (the original's Player_KeyframeSfxCheck):
-- from keyframe `keyframe` on it plays at Harry with the volume and kind of the sound, and the
-- one-shot flag holds it until the clip is back before that keyframe. Returns whether it played.
local SFX_ACTIVE = 0x2000
local KEYFRAME_SFX = {
    -- sound id = {volume, kind, step pitch class (nil: left as is)}
    [1314] = { 0x80, 0, 0x40 }, -- Stumble0
    [1316] = { 0x80, 0, 0x40 },
    [1318] = { 0x80, 0, 0x40 },
    [1319] = { 0x80, 0, 0x40 },
    [1333] = { 0x80, 0, 0x40 }, -- Stumble1
    [1283] = { 0xC8, 2, 0 },
    [1628] = { 0x40, 1 },
    [1626] = { 0xFF, 1 },
    [1638] = { 0xFF, 2 },
}
local KEYFRAME_SFX_DEFAULT = { 0x40, 2, 0 }

function keyframe_sfx(status, keyframe, sound)
    if c.anim_status ~= status then
        return false
    end
    if c.anim_keyframe >= keyframe then
        if c.harry_flags & SFX_ACTIVE ~= 0 then
            return false
        end
        local row = KEYFRAME_SFX[sound] or KEYFRAME_SFX_DEFAULT
        sh.entity.sfx_3d(0, sound, row[1], row[2])
        if row[3] ~= nil then
            c.harry_step_sound = row[3]
        end
        c.harry_flags = c.harry_flags | SFX_ACTIVE
        return true
    end
    c.harry_flags = c.harry_flags & ~SFX_ACTIVE
    return false
end
