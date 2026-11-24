-- Harry's upper body and combat: the upper body clips that follow the legs, getting out of breath and
-- the stumble sounds, raising and lowering the weapon, aiming and the target lock (finding a target,
-- switching it, turning to it), the shots of every gun, the swings of every melee weapon and their
-- follow-ups, the hit window, the dry click, the reload, the PC's alternate cameras' gun machine, and after
-- Harry is drawn: where the weapon is, its effects and the attack handed to the game's hit resolution.
--
-- behavior.lua names this file (-- uses: Harry/combat): it runs first in Harry's Lua state. The engine
-- calls upper_body(0) once per tick after the legs (Player_LogicUpdate) and combat_update(0) after Harry is
-- drawn. Damage to Harry, grabs, death and items are not handled here and meet the combat through the
-- body states (h.lower_body, h.upper_body, h.state).
--
-- Integer rules: common.lua. `c` is Harry's record, `h` the state outside it (sh.entity.harry /
-- set_harry), controls are read live (sh.input.control): the upper body clears some of them during the
-- tick. Weapons by name (gamedata/weapons), clips by name (character.json anims), sounds by slot name
-- (character.json sounds). A mod changing how Harry fights changes the game: "gameplay": true in mod.json.

local c = record(0)
local h = setmetatable({}, {
    __index = function(_, k)
        return sh.entity.harry(k)
    end,
    __newindex = function(_, k, v)
        sh.entity.set_harry(k, v)
    end,
})

local function ctl(name)
    return sh.input.control(name)
end

local function ctl_set(name, v)
    sh.input.set_control(name, v)
end

-- Lower body states (h.lower_body).
local NONE, WALK_FORWARD, RUN_FORWARD, RUN_FORWARD_WALL_STOP, WALK_BACKWARD = 0, 1, 2, 3, 4
local SIDESTEP_RIGHT, SIDESTEP_LEFT, RUN_RIGHT, RUN_LEFT, QUICK_TURN_RIGHT, QUICK_TURN_LEFT = 5, 6, 7, 8, 9, 10
local JUMP_BACKWARD, STUMBLE, RUN_LEFT_WALL_STOP, RUN_RIGHT_WALL_STOP = 11, 12, 13, 14
local RUN_LEFT_STUMBLE, RUN_RIGHT_STUMBLE = 15, 16
local AIM, AIM_WALK_FORWARD, AIM_RUN_LEFT, AIM_QUICK_TURN_RIGHT, AIM_QUICK_TURN_LEFT = 20, 21, 28, 29, 30
local ATTACK, RELOAD = 34, 35

-- Upper body states (h.upper_body).
local U_NONE, U_WALK_FORWARD, U_RUN_FORWARD, U_RUN_WALL_STOP = 0, 1, 2, 3
local U_SIDESTEP_RIGHT, U_SIDESTEP_LEFT, U_RUN_RIGHT, U_RUN_LEFT, U_WALK_BACKWARD = 4, 5, 6, 7, 8
local U_QUICK_TURN_RIGHT, U_QUICK_TURN_LEFT, U_TURN_RIGHT, U_TURN_LEFT, U_RUN_JUMP_BACKWARD = 9, 10, 11, 12, 13
local U_STUMBLE, U_RUN_LEFT_WALL_STOP, U_RUN_RIGHT_WALL_STOP, U_RUN_LEFT_STUMBLE, U_RUN_RIGHT_STUMBLE = 14, 15, 16, 17, 18
local U_AIM, U_AIM_TARGET_LOCK, U_AIM_START, U_AIM_START_TARGET_LOCK, U_AIM_TARGET_LOCK_SWITCH = 19, 20, 21, 22, 23
local U_AIM_STOP, U_ATTACK, U_RELOAD = 24, 25, 26

-- Harry's states (h.state).
local STATE_NONE, STATE_COMBAT, STATE_IDLE, STATE_KICK_ENEMY, STATE_STOMP_ENEMY = 0, 1, 2, 5, 6
local STATE_CUTSCENE_FIRST, STATE_CUTSCENE_END = 7, 51

-- harry_flags bits.
local TARGET_ATTACK = 0x1        -- the attack goes on at the locked target
local SHOOTING = 0x2             -- this attack's shot or swing has gone off
local HIT_DONE = 0x4             -- the hit, motor start or reload sound of this attack is done
local WALL_STOP_RIGHT = 0x8
local ATTACKING = 0x40           -- the attack clip runs (the legs follow it)
local AIM_TURNED = 0x100         -- the lock switch has turned Harry to the new target
local LOCK_ATTACK = 0x200        -- the lock turn ends in an attack
local MELEE_SWING = 0x400
local GUN_FOLLOW_UP = 0x800      -- the next shot plays the short recoil (the gun stays out)
local LOCK_SWITCHING = 0x1000
local SFX_ACTIVE = 0x2000        -- a keyframe sound of the upper body has played

local A = clips("Harry", {
    walk_forward = 2, run_forward = 3, walk_backward = 4, sidestep_left = 5, sidestep_right = 6,
    run_left = 7, run_right = 8, run_forward_wall_stop_right = 9, run_forward_wall_stop_left = 10,
    run_forward_stumble = 11, turn_left = 12, turn_right = 13, quick_turn_right = 14, quick_turn_left = 15,
    jump_backward = 16, run_left_wall_stop = 18, run_left_stumble = 19, run_right_wall_stop = 20,
    run_right_stumble = 21, idle = 26, idle_exhausted = 27, handgun_aim = 28, handgun_aim_start = 29,
    weapon_attack_2 = 30, handgun_recoil = 31, aim_target_lock_start = 32, powered_weapon_idle = 33,
    aim_resume = 34, gun_attack = 36,
})
local function st(clip, active)
    return anim_status(clip, active)
end

local W = {}
for _, name in ipairs({ "KitchenKnife", "SteelPipe", "RockDrill", "Hammer", "Chainsaw", "Katana", "Axe",
                        "Kick", "Stomp", "Handgun", "HuntingRifle", "Shotgun", "HyperBlaster" }) do
    W[name] = sh.entity.weapon_number(name)
end
local TAP, MULTITAP = 0, 2
local GUNS = weapon_attack(W.Handgun, TAP)

-- The weapon of a packed attack (a mod's weapon's row through the engine, else C remainder: -1 stays -1).
local weapon_of = attack_weapon

local function gun(attack)
    return gun_attack(attack, GUNS)
end

-- Per weapon: the sounds (character.json sounds), the keyframes between which a press queues the
-- follow-up swing (melee), the keyframe the aim pose holds on the alternate cameras (guns; first person,
-- other), the items the gun and its ammo are carried as.
local WEAPONS = {
    [W.KitchenKnife] = { attack = sound(c, "kitchen_knife_swing", 1293), follow_up = { 613, 619 } },
    [W.SteelPipe] = { attack = sound(c, "steel_pipe_swing", 1294), follow_up = { 642, 648 } },
    [W.Hammer] = { attack = sound(c, "hammer_swing", 1294), follow_up = { 642, 648 } },
    [W.Axe] = { attack = sound(c, "axe_swing", 1295), follow_up = { 618, 625 } },
    [W.Chainsaw] = { attack = sound(c, "chainsaw_attack", 1301), follow_up = { 624, 630 } },
    [W.RockDrill] = { attack = sound(c, "rock_drill_attack", 1303), follow_up = { 568, 568 } },
    [W.Katana] = { attack = sound(c, "katana_swing", 1298), follow_up = { 612, 619 } },
    [W.Handgun] = { attack = sound(c, "handgun_shot", 1283), reload = sound(c, "handgun_reload", 1284),
                    empty = sound(c, "handgun_empty", 1285), aim_hold = { 592, 588 },
                    item = "Handgun", ammo = "HandgunBullets" },
    [W.HuntingRifle] = { attack = sound(c, "hunting_rifle_shot", 1290), reload = sound(c, "hunting_rifle_reload", 1291),
                         empty = sound(c, "hunting_rifle_empty", 1292), aim_hold = { 597, 587 },
                         item = "HuntingRifle", ammo = "RifleShells" },
    [W.Shotgun] = { attack = sound(c, "shotgun_shot", 1287), reload = sound(c, "shotgun_reload", 1288),
                    empty = sound(c, "shotgun_empty", 1289), aim_hold = { 592, 588 },
                    item = "Shotgun", ammo = "ShotgunShells" },
    [W.HyperBlaster] = { attack = sound(c, "hyper_blaster_shot", 1299), aim_hold = { 574, 574 },
                         item = "HyperBlaster" },
}
local NO_WEAPON = { attack = 0, reload = 0, empty = 0 }
local AIM_HOLD_DEFAULT = 591

-- A mod's weapon has no entry above: its weapon file (gamedata/weapons, rig "sounds" and "anims") names the
-- sounds, the follow-up and aim hold keyframes and the items it and its ammo are; what it does not name comes from
-- the base weapon it is "like" (also the shape of its blade's tip).
local MOD_WEAPONS = {}
local function mod_weapon_entry(number)
    local e = MOD_WEAPONS[number]
    if e == nil then
        local item, ammo, attack, reload, empty = sh.entity.weapon_data(number)
        local follow_from, follow_to, hold_first, hold_other, like = sh.entity.weapon_keyframes(number)
        if like < 0 then
            like = W.SteelPipe
        end
        local base = WEAPONS[like] or {}
        e = { attack = attack, reload = reload, empty = empty, like = like, follow_up = base.follow_up,
              aim_hold = base.aim_hold }
        if follow_from >= 0 then
            e.follow_up = { follow_from, follow_to }
        end
        if hold_first >= 0 then
            e.aim_hold = { hold_first, hold_other }
        end
        if item >= 0 then
            e.item_number = item
        end
        if ammo >= 0 then
            e.ammo_number = ammo
        end
        MOD_WEAPONS[number] = e
    end
    return e
end

-- The entry of a weapon's own (tap) attack; nil for any other attack.
local function weapon_entry(attack)
    local row = mod_attack(attack)
    if row then
        return row.input == 0 and mod_weapon_entry(row.weapon) or nil
    end
    return WEAPONS[attack]
end

-- The equipped weapon's entry (any input kind of its attack).
local function weapon_info()
    local attack = h.weapon_attack
    if attack < 0 then
        return NO_WEAPON
    end
    local row = mod_attack(attack)
    if row then
        return mod_weapon_entry(row.weapon)
    end
    local base = attack >= GUNS and GUNS + (attack - GUNS) % 10 or attack % 10
    return WEAPONS[base] or NO_WEAPON
end

local function weapon_sound(kind)
    return weapon_info()[kind] or 0
end

local dt = 0

local function flags_set(bits)
    c.harry_flags = c.harry_flags | bits
end

local function flags_clear(bits)
    c.harry_flags = c.harry_flags & ~bits
end

local function flag(bits)
    return c.harry_flags & bits ~= 0
end

local function upper_step_reset()
    h.upper_control_state = 0
    h.upper_state_step = 0
end

local function legs_step_reset()
    c.control_state = 0
    c.state_step = 0
end

-- The upper body starts `status` once per state: only while its step is 0, then the step moves on.
local function upper_clip(status)
    if h.upper_state_step == 0 then
        h.upper_anim_status = status
        h.upper_state_step = h.upper_state_step + 1
    end
end

local function upper_pose(status, keyframe)
    h.upper_anim_status = status
    h.upper_anim_keyframe = keyframe
    h.upper_anim_time = keyframe * 4096
end

local function weapon_ready_keyframe(i)
    local _, ready = sh.entity.harry_weapon_clip(i)
    return ready
end

local function weapon_first_keyframe(i)
    local first = sh.entity.harry_weapon_clip(i)
    return first
end

local function clip_keyframes(status)
    local first, last = sh.entity.clip_info(0, status)
    return first, last
end

-- The keyframe a clip ends at in its direction of play (a backward clip ends at its first keyframe), and
-- whether `keyframe` has reached it; the clip must be one of the base table (below 76).
local function clip_done(status, keyframe)
    local first, last, variable, speed = sh.entity.clip_info(0, status)
    local backward = variable == 0 and speed < 0
    local done = backward and first or last
    local reached
    if backward then
        reached = keyframe <= done
    else
        reached = keyframe >= done
    end
    return done, reached
end

local function aim_hold_keyframe()
    local entry = weapon_entry(h.weapon_attack)
    local hold = entry and entry.aim_hold
    if hold == nil then
        return AIM_HOLD_DEFAULT
    end
    return h.fps_camera ~= 0 and hold[1] or hold[2]
end

local function sfx(id, volume, kind)
    sh.entity.sfx_3d(0, id, volume, kind or 0)
end

-- The last NPC index: the original's table holds 6, the engine's holds every slot (spawned enemies stand in
-- the slots after the map's own).
local NPC_LAST = 5
do
    local ok, n = pcall(sh.entity.npc_slot_count)
    if ok and n ~= nil and n > 0 then
        NPC_LAST = n - 1
    end
end

-- The NPC with game index `i` (0..NPC_LAST); a script error for anything else (the engine would read
-- outside the NPC table).
local function npc(i)
    if i < 0 or i > NPC_LAST then
        error("NPC index " .. tostring(i) .. " out of range")
    end
    return record(i + 1)
end

-- The heading (0..4095) from Harry to the middle of NPC `i`.
local function heading_to_npc(i)
    local n = npc(i)
    return sh.math.atan2(s32(n.pos_x + n.box_offset_x - c.pos_x), s32(n.pos_z + n.box_offset_z - c.pos_z))
end

-- Math_ShortestAngleGet on 16-bit angles.
local function shortest_angle(from, to)
    from = s16(from)
    to = s16(to)
    if to > from then
        if to - from < 2048 then
            return s16(to - from)
        end
        return s16(s16(to - 4096) - from)
    end
    if from - to < 2048 then
        return s16(to - from)
    end
    return s16(to - s16(from - 4096))
end

-- The weapon's rounds back into the magazine from the ammo carried (the gun's magazine size from its data).
local function reload_rounds(current, available)
    local _, _, _, magazine = sh.entity.weapon_attack_info(h.weapon_attack & 0xFF)
    local missing = magazine - current
    if missing == 0 then
        return current, available
    end
    if available >= missing then
        return current + missing, available - missing
    end
    return current + available, 0
end

-- The inventory follows the gun: its slot counts the rounds in it, the ammo stack the rounds carried.
local function inventory_sync()
    local info = weapon_info()
    if info.item ~= nil or info.item_number ~= nil then
        sh.entity.inventory_count_set(info.item_number or sh.entity.item_number(info.item), h.ammo)
    end
    if info.ammo ~= nil or info.ammo_number ~= nil then
        sh.entity.inventory_count_set(info.ammo_number or sh.entity.item_number(info.ammo), h.ammo_reserve)
    end
end

local function reload_finish()
    if h.ammo_reserve ~= 0 then
        local current, total = reload_rounds(h.ammo, h.ammo_reserve)
        h.ammo = current
        h.ammo_reserve = total
        inventory_sync()
    end
end

-- One round spent: the gun and its inventory slot.
local function spend_round()
    h.ammo = h.ammo - 1
    local slot = h.weapon_slot
    sh.entity.inventory_slot_count_set(slot, sh.entity.inventory_slot_count(slot) - 1)
end

-- A keyframe sound of the legs' clip, once until the clip goes back below the keyframe.
local function keyframe_sound(status, keyframe, id, breath)
    if c.anim_status ~= status then
        return false
    end
    if c.anim_keyframe >= keyframe then
        if flag(SFX_ACTIVE) then
            return false
        end
        if breath then
            sfx(id, 0x40, 2)
            c.player_noise = 0
        else
            sfx(id, 0x80, 0)
            c.player_noise = 0x40
        end
        flags_set(SFX_ACTIVE)
        return true
    end
    flags_clear(SFX_ACTIVE)
    return false
end

-- -----------------------------------------------------------------------------------------
-- Finding a target (the aim's lock and the melee's victim).

-- The centre of NPC i's body for the aim: position plus its shape offsets.
local function npc_centre(n)
    return s32(n.pos_x + n.box_offset_x), s32(n.pos_y + n.box_offset_y), s32(n.pos_z + n.box_offset_z)
end

local function sq6(x)
    local v = asr(x, 6)
    return s32(v * v)
end

-- Combat_TargetNpcSearch: the NPC (index, -1 none) Harry aims at from the weapon, and the up-down angle
-- to it. mode 0: nearest in angle, 1/2: the next one to the right/left of the current aim, 3: in a circle
-- `span` ahead, 4: weighted by distance and angle, 5: nearest. In modes 0-2 `span` is the half angle and
-- `range` the distance, in 3-5 `span` is how far ahead the circle of radius `range` sits. Returns the new
-- index and pitch, or nil, pitch when none (the index goes to -1 and the pitch stays).
local function target_search(span, range, mode)
    local ox, oy, oz = h.attack_x, h.attack_y, h.attack_z
    local found, angles, pitches, dists = {}, {}, {}, {}
    local yaw = c.rot_y
    span = s16(span)
    for i = 0, NPC_LAST do
        local n = npc(i)
        if n.chara_id ~= 0 and n.health >= 0 then
            local x, y, z = npc_centre(n)
            local keep = true
            local angle, dist = 0, 0
            if mode < 3 then
                if sq6(range) < s32(s32(sq6(ox - x) + sq6(oy - y)) + sq6(oz - z)) then
                    keep = false
                else
                    angle = s16(sh.math.atan2(s32(x - ox), s32(z - oz)))
                    if span < abs(angle_signed(s16(yaw - angle))) then
                        keep = false
                    elseif mode ~= 0 then
                        if sh.entity.harry("target_npc") == i then
                            keep = false
                        else
                            angle = s16(angle_signed(s16(angle - c.angle_to_target)))
                            if (mode == 1 and angle < 0) or (mode == 2 and angle > 0) then
                                keep = false
                            end
                        end
                    end
                end
            else
                local cx = s32(ox + asr(s32(span * sh.math.sin(yaw)), 12))
                local cz = s32(oz + asr(s32(span * sh.math.cos(yaw)), 12))
                if sq6(range) < s32(sq6(cx - x) + sq6(cz - z)) then
                    keep = false
                else
                    angle = s16(angle_signed(s16(sh.math.atan2(s32(x - ox), s32(z - oz)) - yaw)))
                    if mode ~= 3 then
                        dist = s32(sh.math.sqrt(s32(sq6(x - ox) + sq6(z - oz))) << 6)
                    end
                end
            end
            if keep then
                local pitch = s16(sh.math.atan2(s32(sh.math.sqrt(s32(sq6(x - ox) + sq6(z - oz))) << 6), s32(y - oy)))
                if pitch >= 0 and pitch <= 0x800 then
                    if (mode < 1 or mode >= 3) and h.target_npc == i then
                        return i, pitch
                    end
                    found[#found + 1] = i
                    angles[#found] = angle
                    pitches[#found] = pitch
                    dists[#found] = dist
                end
            end
        end
    end
    local count = #found
    if count == 0 then
        return nil
    end
    local pick = count + 1
    for a = 1, count do
        for b = a + 1, count do
            local fa = npc(found[a]).flags & 2 ~= 0
            local fb = npc(found[b]).flags & 2 ~= 0
            local swap = false
            if fa == fb then
                if mode == 0 or mode == 3 then
                    swap = abs(angles[a]) > abs(angles[b])
                elseif mode == 1 then
                    swap = angles[a] > angles[b]
                elseif mode == 2 then
                    swap = angles[a] < angles[b]
                elseif mode == 4 then
                    swap = s32(sq6(dists[a]) * abs(angles[a])) > s32(sq6(dists[b]) * abs(angles[b]))
                elseif mode == 5 then
                    swap = dists[a] > dists[b]
                end
            else
                -- A knocked-down NPC (flag 2) goes behind the standing ones.
                swap = fa
            end
            if swap then
                if mode > 3 and mode < 6 then
                    dists[a], dists[b] = dists[b], dists[a]
                end
                angles[a], angles[b] = s16(angles[b]), s16(angles[a])
                pitches[a], pitches[b] = s16(pitches[b]), s16(pitches[a])
                found[a], found[b] = found[b], found[a]
            end
        end
        local x, y, z = npc_centre(npc(found[a]))
        local hit, slot = sh.entity.ray_trace_slot(0, ox, oy, oz, s32(x - ox), s32(y - oy), s32(z - oz))
        if hit ~= 0 and slot == found[a] + 1 then
            pick = a
            break
        end
    end
    if pick <= count then
        return found[pick], pitches[pick]
    end
    return nil
end

-- The search writing its result like the game: the locked target (or `who`) gets the index or -1, the
-- aim pitch the angle when one was found.
local function search_into(span, range, mode)
    local i, pitch = target_search(span, range, mode)
    if i == nil then
        return -1
    end
    c.harry_aim_pitch = pitch
    return i
end

-- Combat_TargetAnglesCalc: the shot's angles to NPC `i` (pitch, yaw), unless another NPC in sight stands
-- nearer within `cone` of them: then that one is the target. Returns the target index, pitch, yaw; nil
-- for an index outside the NPC table.
local function target_angles(i, cone)
    if i < 0 or i > NPC_LAST then
        return nil
    end
    local ox, oy, oz = h.attack_x, h.attack_y, h.attack_z
    local x, y, z = npc_centre(npc(i))
    local dx, dy, dz = s32(x - ox), s32(y - oy), s32(z - oz)
    local mag0 = sh.math.sqrt(s32(s32(dx * dx) + s32(dz * dz)))
    local yaw0 = s16(sh.math.atan2(dx, dz))
    local pitch0 = s16(sh.math.atan2(mag0, dy))
    local target, pitch, yaw = i, pitch0, yaw0
    for k = 0, NPC_LAST do
        local n = npc(k)
        if n.chara_id ~= 0 and n.health >= 0 and k ~= i then
            local kx, ky, kz = npc_centre(n)
            local ex, ey, ez = s32(kx - ox), s32(ky - oy), s32(kz - oz)
            local yaw2 = s16(sh.math.atan2(ex, ez))
            if not (cone < abs(angle_signed(s16(yaw0 - yaw2)))) then
                local mag1 = sh.math.sqrt(s32(s32(ex * ex) + s32(ez * ez)))
                if not (mag0 < mag1) then
                    local pitch3 = s16(sh.math.atan2(mag1, ey))
                    if not (cone < abs(angle_signed(s16(pitch0 - pitch3)))) then
                        local hit, slot = sh.entity.ray_trace_slot(0, ox, oy, oz, ex, ey, ez)
                        if hit ~= 0 and slot == k + 1 then
                            target, pitch, yaw = k, pitch3, yaw2
                            mag0 = mag1
                        end
                    end
                end
            end
        end
    end
    return target, pitch, yaw
end

-- -----------------------------------------------------------------------------------------
-- The upper body follows the legs.

local LEGS_TO_UPPER = {
    [WALK_FORWARD] = U_WALK_FORWARD, [RUN_FORWARD] = U_RUN_FORWARD, [WALK_BACKWARD] = U_WALK_BACKWARD,
    [SIDESTEP_RIGHT] = U_SIDESTEP_RIGHT, [SIDESTEP_LEFT] = U_SIDESTEP_LEFT, [RUN_RIGHT] = U_RUN_RIGHT,
    [RUN_LEFT] = U_RUN_LEFT, [QUICK_TURN_RIGHT] = U_QUICK_TURN_RIGHT, [QUICK_TURN_LEFT] = U_QUICK_TURN_LEFT,
    [JUMP_BACKWARD] = U_RUN_JUMP_BACKWARD, [RUN_FORWARD_WALL_STOP] = U_RUN_WALL_STOP, [STUMBLE] = U_STUMBLE,
    [RUN_LEFT_WALL_STOP] = U_RUN_LEFT_WALL_STOP, [RUN_RIGHT_WALL_STOP] = U_RUN_RIGHT_WALL_STOP,
    [RUN_LEFT_STUMBLE] = U_RUN_LEFT_STUMBLE, [RUN_RIGHT_STUMBLE] = U_RUN_RIGHT_STUMBLE,
}

-- The upper body state from the legs' (standing: by the turn, `mode` 1 turns either way, 3 keeps a left
-- turn, 4 a right one, 0 and 2 stand). A new state restarts the upper clip; between the walk and the run
-- the upper clip takes over the legs' place in the cycle.
local function follow_legs(state, mode)
    local prev = h.upper_body
    local lower = h.lower_body
    local mapped = LEGS_TO_UPPER[lower]
    if mapped ~= nil then
        h.upper_body = mapped
    elseif lower == NONE then
        if mode == 0 or mode == 2 then
            h.upper_body = U_NONE
        elseif mode == 1 then
            if ctl("turn_right") == 0 then
                if ctl("turn_left") ~= 0 then
                    h.upper_body = U_TURN_LEFT
                end
            else
                h.upper_body = U_TURN_RIGHT
            end
        elseif mode == 3 then
            if ctl("turn_left") ~= 0 then
                h.upper_body = U_TURN_LEFT
            elseif ctl("turn_right") == 0 then
                h.upper_body = U_NONE
            end
        elseif mode == 4 then
            if ctl("turn_right") ~= 0 then
                h.upper_body = U_TURN_RIGHT
            elseif ctl("turn_left") == 0 then
                h.upper_body = U_NONE
            end
        end
    end
    local upper = h.upper_body
    if upper ~= state then
        h.upper_state_step = 0
        h.upper_control_state = 0
    end
    if (prev == U_WALK_FORWARD and upper == U_RUN_FORWARD) or (prev == U_RUN_FORWARD and upper == U_WALK_FORWARD) then
        h.upper_anim_status = c.anim_status
        h.upper_anim_keyframe = c.anim_keyframe
        h.upper_anim_time = c.anim_time
        h.upper_state_step = h.upper_state_step + 1
    end
end

-- -----------------------------------------------------------------------------------------
-- The alternate cameras' gun machine (PC): aim pose held, one recoil per shot, the reload.

local FREE_AIM, FREE_FIRE, FREE_RELOAD = 0, 1, 2
local REFIRE_TIME = 819          -- Q12 0.2 s between shots
local MIN_RELEASE = 204          -- Q12 0.05 s of trigger let go before the next shot

local function free_aim(fresh)
    local recoil = (h.weapon_anim_attack_hold | 1) & 0xFF
    local recoil_first, recoil_last = clip_keyframes(recoil)
    local fire_held = ctl("shoot") ~= 0
    local reload_asked = h.reload_request ~= 0
    local ammo = h.ammo
    local reserve = h.ammo_reserve
    local infinite = h.infinite_ammo ~= 0
    local hyper = h.weapon_attack == weapon_attack(W.HyperBlaster, TAP)
    local recoil_blend, recoil_active = st(A.handgun_recoil, false), st(A.handgun_recoil, true)

    if fresh then
        local status = h.upper_anim_status
        if h.upper_body == U_RELOAD or status == recoil_blend or status == recoil_active then
            h.free_aim_state = FREE_RELOAD
        else
            h.free_aim_state = FREE_AIM
        end
        h.free_aim_stuck = 0
        h.free_aim_fire_held = fire_held and 1 or 0
        if h.free_aim_state == FREE_RELOAD then
            h.reload_request = 0
        end
    end
    -- Damage, grabs, death and the inventory cancel a reload by clearing the upper body state.
    if not fresh and h.free_aim_state == FREE_RELOAD and h.upper_body ~= U_RELOAD then
        h.free_aim_state = FREE_AIM
        h.free_aim_stuck = 0
        flags_clear(HIT_DONE)
    end
    if h.free_aim_refire > 0 then
        h.free_aim_refire = h.free_aim_refire - dt
    end
    if not fire_held then
        h.free_aim_released = h.free_aim_released + dt
    end
    local edge = fire_held and h.free_aim_fire_held == 0 and h.free_aim_refire <= 0 and
        h.free_aim_released >= MIN_RELEASE
    if fire_held then
        h.free_aim_released = 0
    end
    h.free_aim_fire_held = fire_held and 1 or 0

    -- Combat keeps the camera ray's shot in Player_CombatUpdate; no auto-target lock.
    h.state = STATE_COMBAT
    h.locked_target = -1

    local state = h.free_aim_state
    if state == FREE_FIRE then
        h.upper_body = U_ATTACK
        if recoil_last <= 0 or h.upper_anim_keyframe >= recoil_last or h.upper_anim_status ~= recoil then
            h.free_aim_state = FREE_AIM
            h.free_aim_stuck = 0
        else
            h.free_aim_stuck = h.free_aim_stuck + 1
            if h.free_aim_stuck > 180 then
                h.free_aim_state = FREE_AIM
                h.free_aim_stuck = 0
            end
        end
    elseif state == FREE_RELOAD then
        h.upper_body = U_RELOAD
        h.upper_state_step = 1
        if h.upper_anim_status == recoil_blend then
            local _, blend_end = clip_keyframes(recoil_blend)
            if blend_end > 0 and h.upper_anim_keyframe >= blend_end then
                h.upper_anim_status = recoil_active
            end
        end
        local reload_end = h.reload_end_keyframe
        local finished = reload_end > 0 and h.upper_anim_keyframe >= reload_end
        if not finished then
            h.free_aim_stuck = h.free_aim_stuck + 1
            finished = h.free_aim_stuck > 600
        end
        if finished then
            reload_finish()
            flags_clear(HIT_DONE)
            h.upper_body = U_AIM
            upper_pose(st(A.aim_resume, true), aim_hold_keyframe())
            h.reload_request = 0
            h.free_aim_state = FREE_AIM
            h.free_aim_stuck = 0
        end
    else
        -- FREE_AIM (and anything else).
        upper_pose(st(A.aim_resume, true), aim_hold_keyframe())
        h.upper_body = U_AIM
        flags_clear(SHOOTING)
        if not hyper and reserve > 0 and
            (reload_asked or (edge and ammo == 0 and not infinite and h.auto_reload ~= 0)) then
            h.reload_request = 0
            h.upper_body = U_RELOAD
            upper_pose(recoil_blend, h.reload_start_keyframe)
            sfx(weapon_sound("reload"), 128, 0)
            flags_set(HIT_DONE)
            c.player_noise = 0x20
            h.free_aim_state = FREE_RELOAD
            h.free_aim_stuck = 0
            return
        end
        local can_fire
        if hyper then
            can_fire = fire_held and h.free_aim_refire <= 0
        else
            can_fire = edge and (ammo > 0 or infinite)
        end
        if (h.aiming ~= 0 or ctl("aim") ~= 0) and can_fire then
            h.free_aim_refire = REFIRE_TIME
            c.attack_window = 1
            if h.weapon_attack ~= weapon_attack(W.HyperBlaster, TAP) then
                if not infinite then
                    spend_round()
                end
                sfx(weapon_sound("attack"), 128, 0)
            else
                sfx(weapon_sound("attack"), 48, 0)
            end
            c.player_noise = 0xC8
            flags_set(SHOOTING)
            h.upper_body = U_ATTACK
            if recoil_first > 0 then
                upper_pose(recoil, recoil_first)
            end
            h.free_aim_state = FREE_FIRE
            h.free_aim_stuck = 0
        elseif edge and ammo == 0 and not infinite then
            -- The dry click (enemies hear it as in the classic camera).
            sfx(weapon_sound("empty"), 128, 0)
            c.player_noise = 32
        end
    end
end

-- -----------------------------------------------------------------------------------------
-- The attack: the clip, the hit window, the shot or the swing, its end and the melee follow-up.

local enemy_hit = -1   -- the NPC the attack goes at this tick (-1 none)

local function wa()
    return h.weapon_attack
end

local function powered(attack)
    return attack == weapon_attack(W.Chainsaw, TAP) or attack == weapon_attack(W.RockDrill, TAP)
end

-- The hit window of the attack clip in use: from its first keyframe plus the attack row's offset, for the
-- row's length (gamedata/weapons).
local function hit_window_set()
    local first = clip_keyframes(h.attack_clip & 0xFF)
    local _, offset, length = sh.entity.weapon_attack_info(wa())
    h.hit_window_start = first + offset
    h.hit_window_end = first + offset + length
end

-- A gun clip that starts in its blend phase without a first keyframe: start one before the playing
-- phase's first keyframe (`before`) or at it, so the shot's window is not skipped.
local function blend_start_fix(before)
    local status = h.upper_anim_status
    if status > 0 and status < 76 then
        local first = clip_keyframes(status)
        if first <= 0 and status & 1 == 0 then
            local active = (status | 1) & 0xFF
            if active < 76 then
                local active_first = clip_keyframes(active)
                if before then
                    if active_first > 1 then
                        first = active_first - 1
                    end
                else
                    first = active_first
                end
            end
        end
        if first > 0 then
            h.upper_anim_keyframe = first
            h.upper_anim_time = first * 4096
        end
    end
end

-- Search span and range by the light of the area (lit, dark).
local function search_ranges(lit, dark)
    if map_light() & 1 == 0 then
        return lit[1], lit[2]
    end
    return dark[1], dark[2]
end

local function attack_end_to_aim(gun)
    if flag(TARGET_ATTACK) then
        h.state = STATE_COMBAT
        h.upper_body = U_AIM_TARGET_LOCK
        if h.lower_body == ATTACK then
            h.lower_body = AIM
        end
    else
        h.state = STATE_NONE
        h.upper_body = U_AIM
        upper_step_reset()
        if h.lower_body == ATTACK then
            h.lower_body = AIM
            flags_clear(MELEE_SWING)
            if gun then
                legs_step_reset()
            end
        end
    end
end

local function in_window()
    local kf = h.upper_anim_keyframe
    return kf >= h.hit_window_start and h.hit_window_end >= kf
end

local function combat_anim()
    local held_attack = wa() & 0xFF
    local entry = weapon_entry(wa())
    local follow = entry and entry.follow_up
    local follow_from, follow_to = 0, 0
    if follow ~= nil and weapon_of(wa()) == wa() and not gun(wa()) then
        follow_from, follow_to = follow[1], follow[2]
    end

    if h.state == STATE_COMBAT then
        c.harry_attack_time = c.harry_attack_time + dt
        if h.option_weapon_control == 0 then
            sh.input.controls_clear()
        end
    end

    -- The attack kind (tap, hold) and its clip.
    if h.upper_control_state == 0 then
        h.melee_attack_type = 0
        h.multitap = 0
        -- The press that started this swing does not also queue a follow-up.
        if h.melee_click_queue > 0 then
            h.melee_click_queue = h.melee_click_queue - 1
        end
        flags_clear(SHOOTING)
        flags_clear(ATTACKING)
        local hold = h.weapon_anim_attack_hold
        local clip = h.weapon_clip_index >> 4
        if gun(wa()) then
            h.melee_attack_type = 0
            h.attack_clip = hold
            h.weapon_clip = clip
        elseif ctl("attack") ~= 0 and wa() ~= weapon_attack(W.RockDrill, TAP) then
            h.melee_attack_type = 1
            h.attack_clip = hold - 4
            h.weapon_clip = clip - 2
        else
            h.melee_attack_type = 0
            local direction = h.rock_drill_direction
            if wa() ~= weapon_attack(W.RockDrill, TAP) or direction == 0 then
                h.attack_clip = hold
                h.weapon_clip = clip
            elseif direction == -1 then
                h.attack_clip = hold + 4
                h.weapon_clip = clip + 2
            else
                h.attack_clip = hold + 2
                h.weapon_clip = clip + 1
            end
        end
        h.upper_control_state = h.upper_control_state + 1
        if not gun(wa()) then
            h.weapon_attack = weapon_attack(weapon_of(wa()), TAP)
        end
        h.weapon_attack = wa() + h.melee_attack_type * 10
        hit_window_set()
        -- Alternate cameras: the gun fires on the press, the recoil plays out from the shot.
        if h.alt_camera ~= 0 and h.hit_window_start > 0 and gun(wa()) then
            h.upper_anim_keyframe = h.hit_window_start
            h.upper_anim_time = h.hit_window_start * 4096
        end
    end

    if h.target_npc ~= -1 and gun(wa()) then
        if h.option_auto_aim == 0 then
            local range = 40960
            if map_light() & 1 ~= 0 then
                range = 12288
            end
            enemy_hit = search_into(0x238, range, 0)
            local target, pitch, yaw = target_angles(enemy_hit, 227)
            if target ~= nil then
                h.locked_target = target
                h.target_pitch = pitch
                h.target_yaw = yaw
            end
        else
            enemy_hit = h.target_npc
        end
        if enemy_hit == -1 and enemy_hit == h.locked_target then
            h.target_yaw = -1
            h.target_pitch = -1
        end
        if enemy_hit == h.target_npc then
            c.angle_to_target = (heading_to_npc(enemy_hit) + 4096) & 0xFFF
        else
            c.angle_to_target = c.rot_y
        end
        -- A new shot starts only while fire is held.
        if h.upper_state_step == 0 and (ctl("shoot") ~= 0 or ctl("attack") ~= 0) then
            h.upper_anim_status = st(A.weapon_attack_2, false)
            h.upper_state_step = h.upper_state_step + 1
            blend_start_fix(true)
        end
    else
        if h.target_npc ~= -1 and h.option_auto_aim == 0 then
            local span, range = search_ranges({ 12288, 12288 }, { 4096, 4096 })
            enemy_hit = search_into(span, range, 5)
            if enemy_hit == h.target_npc then
                local heading = s16((heading_to_npc(enemy_hit) + 4096) & 0xFFF)
                local delta = shortest_angle(c.rot_y, heading)
                h.turn_speed = dt * 15
                if abs(delta) >= 0x80 then
                    if delta < 0 then
                        h.turn_speed = -h.turn_speed
                    end
                else
                    c.rot_y = heading
                    c.angle_to_target = heading
                    h.turn_speed = 0
                end
            end
        else
            enemy_hit = -1
            c.harry_aim_pitch = 1024
            c.angle_to_target = c.rot_y
        end
        if gun(wa()) then
            if flag(GUN_FOLLOW_UP) or h.alt_camera ~= 0 then
                -- The follow-up shot (and every alternate-camera shot): the short recoil, gun out.
                if h.upper_state_step == 0 then
                    h.upper_anim_status = h.weapon_anim_attack - 12
                    h.upper_state_step = h.upper_state_step + 1
                    blend_start_fix(true)
                end
            elseif h.upper_state_step == 0 then
                h.upper_anim_status = h.weapon_anim_attack
                h.upper_state_step = h.upper_state_step + 1
                blend_start_fix(false)
            end
        elseif ctl("attack") ~= 0 and wa() ~= weapon_attack(W.RockDrill, TAP) then
            upper_clip(h.weapon_anim_attack - 4)
        elseif wa() ~= weapon_attack(W.RockDrill, TAP) or h.rock_drill_direction == 0 then
            upper_clip(h.weapon_anim_attack)
        elseif h.rock_drill_direction == -1 then
            upper_clip(h.weapon_anim_attack + 4)
        else
            upper_clip(h.weapon_anim_attack + 2)
        end
    end

    -- The sound of the swing or the shot, the dry click and the round spent; the powered weapons' motor.
    if not gun(wa()) then
        local id = weapon_of(wa())
        if id ~= W.Chainsaw and id ~= W.RockDrill then
            if in_window() then
                if not flag(HIT_DONE) then
                    c.attack_window = 1
                    sfx(weapon_sound("attack"), 128, 0)
                    c.player_noise = 0x40
                    flags_set(HIT_DONE)
                end
            elseif h.hit_window_end < h.upper_anim_keyframe then
                flags_clear(HIT_DONE)
            end
        elseif c.harry_gas_weapon_timer == 0 then
            if in_window() and not flag(HIT_DONE) then
                c.attack_window = 1
                flags_set(HIT_DONE)
            end
        else
            if c.attack_window <= 0 then
                c.attack_window = 1
            end
            c.player_noise = 0x40
        end
    elseif in_window() and not flag(SHOOTING) then
        flags_set(SHOOTING)
        if h.ammo ~= 0 or h.infinite_ammo ~= 0 then
            c.attack_window = 1
            if wa() ~= weapon_attack(W.HyperBlaster, TAP) then
                if h.infinite_ammo == 0 then
                    spend_round()
                end
                sfx(weapon_sound("attack"), 128, 0)
            else
                sfx(weapon_sound("attack"), 48, 0)
            end
            c.player_noise = 0xC8
        else
            sfx(weapon_sound("empty"), 128, 0)
            c.player_noise = 32
            local kf = weapon_ready_keyframe(h.weapon_clip) - 3
            h.upper_anim_keyframe = kf
            h.upper_anim_time = kf * 4096
            if h.lower_body == AIM then
                c.anim_keyframe = kf
                c.anim_time = kf * 4096
            end
        end
    end

    -- The end of the attack clip, in its direction of play.
    local status = h.upper_anim_status
    local lookup = status
    if (status == st(A.weapon_attack_2, false) or status == st(A.gun_attack, false)) and gun(wa()) then
        lookup = st(A.gun_attack, true)
    end
    local done = false
    if (lookup == st(A.weapon_attack_2, true) or lookup == st(A.gun_attack, true) or
            lookup == st(A.handgun_recoil, true) or lookup == st(A.handgun_aim_start, true) or
            lookup == st(A.aim_resume, true)) and lookup < 76 then
        local end_kf, reached = clip_done(lookup, h.upper_anim_keyframe)
        done = end_kf > 0 and reached
    end
    if not gun(wa()) then
        if (status == st(A.handgun_aim_start, true) or status == st(A.weapon_attack_2, true) or
                status == st(A.handgun_recoil, true)) and done then
            local aim = st(A.handgun_aim, true)
            local _, aim_end = clip_keyframes(aim)
            upper_pose(aim, aim_end)
            attack_end_to_aim(false)
            c.harry_attack_time = 0
            flags_clear(HIT_DONE)
            flags_clear(SHOOTING)
            h.weapon_attack = weapon_attack(weapon_of(wa()), TAP)
            -- One tap, one swing: a press left in the input history does not swing again; a held
            -- button keeps its whole history (letting go then is not a tap).
            if not gun(weapon_of(wa()) & 0xFF) then
                ctl_set("shoot", 0)
                ctl_set("attack", 0)
                h.melee_click_queue = 0
                if ctl("action_held") ~= 0 then
                    ctl_set("hold_attack", 0x1F)
                else
                    ctl_set("hold_attack", 0)
                    h.melee_needs_release = 1
                end
            end
            return true
        end
    elseif done or ((status == st(A.weapon_attack_2, true) or status == st(A.gun_attack, true)) and
            h.upper_anim_keyframe == weapon_ready_keyframe(h.weapon_clip)) then
        attack_end_to_aim(true)
        h.target_yaw = -1
        h.target_pitch = -1
        c.harry_attack_time = 0
        flags_clear(SHOOTING)
        -- Fire still held: the next shot is the short recoil.
        if ctl("attack") ~= 0 or ctl("shoot") ~= 0 then
            flags_set(GUN_FOLLOW_UP)
        end
        return true
    end

    flags_set(ATTACKING)

    -- A press between the follow-up keyframes queues the multitap swing.
    if not gun(wa()) and h.melee_attack_type == 0 and wa() ~= weapon_attack(W.RockDrill, TAP) then
        local kf = h.upper_anim_keyframe
        if kf >= follow_from and kf < follow_to and h.upper_anim_status == st(A.handgun_recoil, true) and
            h.melee_click_queue > 0 and h.multitap == 0 then
            h.multitap = 1
            h.melee_click_queue = h.melee_click_queue - 1
        end
    end
    if h.multitap ~= 0 then
        if h.upper_anim_status == st(A.handgun_recoil, true) and h.upper_anim_keyframe >= follow_to then
            h.upper_state_step = 0
            if h.lower_body == ATTACK then
                c.state_step = 0
            end
            h.attack_clip = h.weapon_anim_attack_hold - 2
            h.weapon_clip = (h.weapon_clip_index >> 4) - 1
            h.melee_attack_type = 2
            h.weapon_attack = weapon_attack(weapon_of(held_attack), MULTITAP)
            upper_clip(h.weapon_anim_attack - 2)
            hit_window_set()
            h.multitap = 0
            flags_clear(HIT_DONE)
        end
    end
    return false
end

-- -----------------------------------------------------------------------------------------
-- The upper body machine.

local IDLE_TIRED = 40960         -- Q12 10 s of running: the tired idle
local HEALTH_TIRED = 122880      -- Q12 30: below it Harry stands tired
local HEALTH_LOOK_AROUND = 245760 -- Q12 60: the idle look-around needs this much health
local AFK_TICKS = 300
local GAS_RUN_TIME = 245760      -- Q12 60 s of the powered weapon's motor

local function clamp_int(x, lo, hi)
    if x < lo then
        return lo
    elseif x > hi then
        return hi
    end
    return x
end

local function standing()
    local status = h.upper_anim_status
    if (status == st(A.walk_forward, true) or status == st(A.run_forward, true)) and h.upper_state_step ~= 0 then
        h.upper_state_step = 0
    end
    if c.harry_exhaustion < IDLE_TIRED and c.health >= HEALTH_TIRED then
        upper_clip(st(A.idle, false))
        follow_legs(U_NONE, 1)
    else
        c.player_afk_timer = 0
        if h.upper_anim_status ~= st(A.idle, true) then
            upper_clip(st(A.idle, false))
            follow_legs(U_NONE, 1)
        else
            h.upper_state_step = 0
            upper_clip(st(A.idle_exhausted, false))
        end
    end
    if h.upper_body ~= U_NONE then
        c.player_afk_timer = 0
    end
    c.angle_to_target = c.rot_y
    if h.upper_body == U_NONE then
        -- The idle timer counts 30 fps ticks whatever the frame rate.
        if c.player_afk_timer == 0 then
            h.afk_fraction = 0
        end
        h.afk_fraction = h.afk_fraction + idiv(4096 * dt, 136)
        while h.afk_fraction >= 4096 do
            h.afk_fraction = h.afk_fraction - 4096
            c.player_afk_timer = c.player_afk_timer + 1
        end
        if h.fps_camera ~= 0 then
            c.player_afk_timer = 0
        end
        if c.player_afk_timer >= AFK_TICKS and c.health >= HEALTH_LOOK_AROUND then
            c.player_afk_timer = 0
            h.state = STATE_IDLE
            legs_step_reset()
            upper_step_reset()
            h.upper_body = U_NONE
            h.lower_body = NONE
            return true
        end
    end
    return false
end

local function aiming()
    h.target_npc = -1
    if powered(wa()) then
        if c.harry_gas_weapon_timer ~= 0 then
            if c.attack_window <= 0 then
                c.attack_window = 1
            end
            upper_clip(st(A.aim_resume, false))
        else
            local kf = h.powered_aim_keyframe
            h.upper_anim_status = st(A.handgun_aim, true)
            h.upper_anim_keyframe = kf
            h.upper_anim_time = kf << 12
        end
    end
    flags_clear(ATTACKING)
    c.angle_to_target = c.rot_y
    if gun(wa()) then
        local status = h.upper_anim_status
        if status ~= st(A.handgun_aim, true) and status ~= st(A.aim_resume, true) then
            if ctl("attack") ~= 0 or ctl("shoot") ~= 0 then
                flags_set(GUN_FOLLOW_UP)
            else
                flags_clear(GUN_FOLLOW_UP)
                upper_clip(st(A.aim_resume, false))
            end
        else
            flags_clear(GUN_FOLLOW_UP)
        end
    end
end

-- The lock lost: back to the plain aim.
local function lock_lost()
    flags_clear(LOCK_SWITCHING)
    c.state_step = 0
    c.harry_aim_pitch = 1024
end

local function target_lock()
    c.harry_attack_time = c.harry_attack_time + dt
    flags_clear(ATTACKING)
    local turn = 0
    if ctl("turn_right") ~= 0 then
        turn = 1
    elseif ctl("turn_left") ~= 0 then
        turn = 2
    end
    -- A switch waits until the aim clip is up.
    local status, kf = h.upper_anim_status, h.upper_anim_keyframe
    if (status ~= st(A.handgun_aim_start, true) or kf < weapon_ready_keyframe(1)) and
        (status ~= st(A.weapon_attack_2, true) or kf < weapon_ready_keyframe(2)) and
        (status ~= st(A.aim_target_lock_start, true) or kf < weapon_ready_keyframe(4)) then
        turn = 0
        c.harry_aim_hold = c.harry_aim_hold + 1
    else
        c.harry_aim_hold = 0
    end
    if turn ~= 0 then
        flags_clear(LOCK_ATTACK)
        c.player_flex_angle_x = h.flex_x
        local span, range = search_ranges({ 2730, 40960 }, { 1365, 12288 })
        h.locked_target = search_into(span, range, turn)
        if h.locked_target == -1 then
            lock_lost()
            h.upper_body = U_AIM
            ctl_set("shoot", 0)
            h.state = STATE_NONE
            ctl_set("attack", 0)
            upper_step_reset()
            if h.lower_body == ATTACK then
                h.lower_body = AIM
                legs_step_reset()
            end
        else
            h.upper_body = U_AIM_TARGET_LOCK_SWITCH
            flags_set(LOCK_SWITCHING)
            upper_step_reset()
        end
        h.target_npc = h.locked_target
    else
        local target = h.target_npc
        if target == -1 then
            -- The locked enemy is gone (killed or removed between two ticks): the original reads the record before
            -- the NPC table here and then drops the lock below, as this does.
            enemy_hit = -1
        elseif h.upper_control_state ~= 0 then
            local n = npc(target)
            if h.target_enemy_x ~= n.pos_x or h.target_enemy_y ~= n.pos_y or h.target_enemy_z ~= n.pos_z or
                h.prev_x ~= c.pos_x or h.prev_y ~= c.pos_y or h.prev_z ~= c.pos_z then
                local span, range = search_ranges({ 0x238, 40960 }, { 0x11C, 12288 })
                enemy_hit = search_into(span, range, 0)
                n = npc(h.target_npc)
                h.target_enemy_x, h.target_enemy_y, h.target_enemy_z = n.pos_x, n.pos_y, n.pos_z
            else
                enemy_hit = target
            end
        else
            enemy_hit = target
            local n = npc(target)
            h.target_enemy_x, h.target_enemy_y, h.target_enemy_z = n.pos_x, n.pos_y, n.pos_z
        end
        if enemy_hit == h.target_npc and enemy_hit ~= -1 then
            c.angle_to_target = (heading_to_npc(enemy_hit) + 4096) & 0xFFF
        else
            lock_lost()
            h.target_npc = -1
            h.state = STATE_NONE
            h.upper_body = U_AIM
            upper_step_reset()
        end
    end
    if h.upper_control_state == 0 then
        h.upper_control_state = h.upper_control_state + 1
    end
end

-- GAMEPLAY CHANGE (aim_resume_after_inventory): in the window the inventory armed (the same weapon was still aimed
-- when it closed) the raise is over at once: its clip stands on the ready keyframe, where the aim is up and a fire
-- press goes through. The original plays the raise again and drops a fire press made before its end.
local function aim_resume_pose(anim, clip)
    if h.aim_resume > 0 then
        upper_pose(st(anim, true), weapon_ready_keyframe(clip))
        h.aim_resume = 0
    end
end

local function aim_start()
    c.angle_to_target = c.rot_y
    local attack = wa()
    if powered(attack) then
        if c.harry_gas_weapon_timer ~= 0 then
            upper_clip(st(A.powered_weapon_idle, false))
        else
            upper_clip(st(A.handgun_aim, false))
        end
        local status, kf = h.upper_anim_status, h.upper_anim_keyframe
        local first0 = weapon_first_keyframe(0)
        if ((attack == weapon_attack(W.Chainsaw, TAP) and status == st(A.handgun_aim, true) and kf >= first0 + 5) or
                (attack == weapon_attack(W.RockDrill, TAP) and status == st(A.handgun_aim, true) and kf >= first0 + 9)) and
            not flag(HIT_DONE) then
            -- The motor starts as the weapon comes up.
            c.harry_gas_weapon_timer = GAS_RUN_TIME
            sh.entity.weapon_model_set(wa(), 0)
            c.player_noise = 0x40
            flags_set(HIT_DONE)
        end
    elseif attack == weapon_attack(W.HuntingRifle, TAP) then
        upper_clip(st(A.handgun_aim_start, false))
        aim_resume_pose(A.handgun_aim_start, 1)
    else
        upper_clip(st(A.handgun_aim, false))
        aim_resume_pose(A.handgun_aim, 0)
    end
    local status, kf = h.upper_anim_status, h.upper_anim_keyframe
    if powered(wa()) then
        local up = false
        if kf == weapon_ready_keyframe(0) or kf == weapon_ready_keyframe(5) then
            if kf == weapon_ready_keyframe(0) then
                sh.entity.weapon_model_set(wa(), 1)
            end
            up = true
        elseif status < 76 then
            local end_kf, reached = clip_done(status, kf)
            if end_kf > 0 and reached then
                if status >> 1 == A.handgun_aim then
                    sh.entity.weapon_model_set(wa(), 1)
                end
                up = true
            end
        end
        if up then
            h.upper_body = U_AIM
            upper_step_reset()
            flags_clear(HIT_DONE)
            if c.harry_gas_weapon_timer ~= 0 then
                c.attack_window = 1
            end
        end
    else
        if kf == weapon_ready_keyframe(0) or kf == weapon_ready_keyframe(1) then
            h.upper_body = U_AIM
        elseif status < 76 then
            local end_kf, reached = clip_done(status, kf)
            if end_kf > 0 and reached then
                h.upper_body = U_AIM
            end
        end
    end
end

-- The turn to the locked target is done: the lock holds, or the attack the lock waited for starts.
local function lock_turned(start_lock)
    if start_lock then
        flags_clear(AIM_TURNED)
    else
        flags_set(AIM_TURNED)
    end
    local ready = h.upper_anim_keyframe >= weapon_ready_keyframe(4)
    if flag(LOCK_ATTACK) then
        if ready then
            h.upper_body = U_ATTACK
            if h.lower_body == AIM then
                h.lower_body = ATTACK
                legs_step_reset()
            end
            flags_clear(LOCK_ATTACK)
            upper_step_reset()
        end
    elseif ready then
        h.upper_body = U_AIM_TARGET_LOCK
        upper_step_reset()
    end
end

local function lock_start()
    local start_lock = h.upper_body == U_AIM_START_TARGET_LOCK
    if start_lock then
        if h.option_auto_aim ~= 0 then
            local span, range = search_ranges({ 0x238, 40960 }, { 0x238, 12288 })
            h.locked_target = search_into(span, range, 0)
        else
            local span, range = search_ranges({ 12288, 28672 }, { 3686, 8601 })
            h.locked_target = search_into(span, range, 4)
        end
        h.target_npc = h.locked_target
    end
    h.weapon_clip = 1
    c.harry_aim_hold = c.harry_aim_hold + 1
    if h.option_weapon_control == 0 then
        sh.input.controls_clear()
    end
    h.upper_control_state = h.upper_control_state + 1
    if h.upper_body == U_AIM_START_TARGET_LOCK then
        upper_clip(st(A.aim_target_lock_start, false))
        aim_resume_pose(A.aim_target_lock_start, 4)
    else
        upper_clip(st(A.handgun_aim_start, false))
    end
    if h.target_npc == -1 then
        c.harry_aim_pitch = 1024
        h.upper_body = U_AIM
        h.state = STATE_NONE
        flags_clear(LOCK_SWITCHING)
        upper_step_reset()
        return
    end
    local heading = (heading_to_npc(h.target_npc) + 4096) & 0xFFF
    if h.option_auto_aim == 0 then
        -- Auto aim: the body turns to the target.
        local status = h.upper_anim_status
        if status == st(A.handgun_aim_start, true) or status == st(A.aim_target_lock_start, true) then
            if h.upper_anim_keyframe >= weapon_ready_keyframe(h.weapon_clip) then
                c.rot_y = heading
            end
        end
        local delta = shortest_angle(c.rot_y, heading)
        local turn = ((h.upper_control_state * 3) + 12) * dt
        turn = clamp_int(turn, 0, 0xFFF)
        h.turn_speed = turn
        if abs(delta) >= 128 then
            if delta < 0 then
                h.turn_speed = -turn
            end
        else
            c.rot_y = heading
            h.turn_speed = 0
            c.angle_to_target = heading
            lock_turned(start_lock)
        end
        c.angle_to_target = c.rot_y
        return
    end
    -- Manual aim: the body keeps its heading, the aim turns within reach of it.
    local delta = clamp_int(shortest_angle(c.rot_y, heading), -0x180, 0x180)
    local step = clamp_int(s16(dt * 15), 0, 0xFFF)
    local aim_delta = shortest_angle(c.angle_to_target, heading)
    if abs(aim_delta) > 128 then
        if aim_delta < 0 then
            step = -step
        end
        c.angle_to_target = ((c.angle_to_target + asr(step, 4)) + 4096) & 0xFFF
    else
        c.angle_to_target = c.rot_y + delta
        lock_turned(start_lock)
    end
end

local function aim_stop()
    h.weapon_clip = h.weapon_clip_index & 0xF
    upper_clip(h.weapon_anim_aim_stop)
    local kf = h.upper_anim_keyframe
    local lower = h.lower_body
    local ready = kf == weapon_first_keyframe(h.weapon_clip) or
        ((lower == RUN_FORWARD or lower == RUN_RIGHT or lower == RUN_LEFT) and kf <= weapon_ready_keyframe(h.weapon_clip))
    local status = h.upper_anim_status
    if not ready and status < 76 then
        local end_kf = clip_done(status, kf)
        if end_kf > 0 and kf == end_kf then
            ready = true
        end
    end
    if ready then
        if lower == RUN_FORWARD then
            h.upper_body = U_RUN_FORWARD
        elseif lower == RUN_RIGHT then
            h.upper_body = U_RUN_RIGHT
        elseif lower == AIM_RUN_LEFT then
            h.upper_body = U_RUN_LEFT
        else
            h.upper_body = U_NONE
        end
        upper_step_reset()
        if lower == NONE then
            legs_step_reset()
        end
        h.target_npc = -1
    end
    c.angle_to_target = c.rot_y
end

local function reloading()
    local blend, active = st(A.handgun_recoil, false), st(A.handgun_recoil, true)
    if h.upper_state_step == 0 then
        h.upper_anim_status = blend
        h.upper_state_step = h.upper_state_step + 1
        local start = h.reload_start_keyframe
        if start > 0 then
            h.upper_anim_keyframe = start
            h.upper_anim_time = start * 4096
        end
    end
    if h.upper_anim_status == blend then
        local _, blend_end = clip_keyframes(blend)
        if blend_end > 0 and h.upper_anim_keyframe >= blend_end then
            h.upper_anim_status = active
        end
    end
    if h.reload_start_keyframe + h.weapon_reload_sound_keyframe <= h.upper_anim_keyframe and not flag(HIT_DONE) then
        sfx(weapon_sound("reload"), 128, 0)
        c.player_noise = 0x20
        flags_set(HIT_DONE)
    end
    local reload_end = h.reload_end_keyframe
    local kf = h.upper_anim_keyframe
    if kf == reload_end or (reload_end > 0 and kf >= reload_end) then
        h.locked_target = -1
        h.upper_body = U_AIM
        h.target_npc = -1
        h.state = STATE_NONE
        flags_clear(HIT_DONE)
        upper_pose(st(A.aim_resume, true), aim_hold_keyframe())
        if h.lower_body == RELOAD then
            h.lower_body = AIM
        end
        reload_finish()
    end
end

local UPPER_LEGS_CLIPS = {
    [U_WALK_FORWARD] = { A.walk_forward, 0, true },
    [U_RUN_FORWARD] = { A.run_forward, 2, true },
    [U_SIDESTEP_RIGHT] = { A.sidestep_right, 0, false },
    [U_SIDESTEP_LEFT] = { A.sidestep_left, 0, false },
    [U_WALK_BACKWARD] = { A.walk_backward, 0, false },
    [U_TURN_RIGHT] = { A.turn_right, 3, false },
    [U_TURN_LEFT] = { A.turn_left, 4, false },
    [U_RUN_JUMP_BACKWARD] = { A.jump_backward, 0, true },
    [U_STUMBLE] = { A.run_forward_stumble, 0, true },
    [U_RUN_LEFT_WALL_STOP] = { A.run_left_wall_stop, 0, false },
    [U_RUN_RIGHT_WALL_STOP] = { A.run_right_wall_stop, 0, false },
    [U_RUN_LEFT_STUMBLE] = { A.run_left_stumble, 0, true },
    [U_RUN_RIGHT_STUMBLE] = { A.run_right_stumble, 0, true },
}

-- Player_UpperBodyMainUpdate: true when Harry left the upper body machine this tick.
local function upper_main()
    -- The melee follow-up queue counts fresh presses while a melee weapon is out and the button is held.
    local attack = wa()
    local melee = attack ~= -1 and not gun(attack)
    local held, pressed
    if h.alt_camera ~= 0 then
        held = h.alt_fire_held ~= 0
        pressed = h.alt_fire_held ~= 0 and h.prev_alt_fire == 0
    else
        held = ctl("action_held") ~= 0
        pressed = ctl("action_pressed") ~= 0
    end
    h.prev_alt_fire = h.alt_fire_held
    if pressed then
        h.melee_needs_release = 0
    end
    if not melee or ctl("run") ~= 0 or not held then
        h.melee_click_queue = 0
    elseif pressed then
        if h.melee_click_queue < 8 then
            h.melee_click_queue = h.melee_click_queue + 1
        end
    end

    enemy_hit = -1
    local upper = h.upper_body
    if upper ~= U_AIM_TARGET_LOCK and upper ~= U_ATTACK then
        c.harry_attack_time = 0
    end

    if upper == U_NONE then
        if standing() then
            return true
        end
    elseif UPPER_LEGS_CLIPS[upper] ~= nil then
        local entry = UPPER_LEGS_CLIPS[upper]
        local clip, mode, follow_time = entry[1], entry[2], entry[3]
        upper_clip(st(clip, false))
        if upper == U_RUN_RIGHT_WALL_STOP or upper == U_RUN_LEFT_WALL_STOP or upper == U_SIDESTEP_RIGHT or
            upper == U_SIDESTEP_LEFT or upper == U_WALK_BACKWARD or upper == U_TURN_RIGHT or upper == U_TURN_LEFT then
            follow_legs(upper, mode)
        elseif follow_time and h.upper_anim_status == st(clip, true) then
            h.upper_anim_time = c.anim_time
            follow_legs(upper, mode)
        else
            follow_legs(upper, mode)
        end
        c.angle_to_target = c.rot_y
    elseif upper == U_RUN_RIGHT or upper == U_RUN_LEFT or upper == U_QUICK_TURN_RIGHT or upper == U_QUICK_TURN_LEFT then
        -- These follow the legs' time after the state update.
        local clip = (upper == U_RUN_RIGHT and A.run_right) or (upper == U_RUN_LEFT and A.run_left) or
            (upper == U_QUICK_TURN_RIGHT and A.quick_turn_right) or A.quick_turn_left
        upper_clip(st(clip, false))
        follow_legs(upper, 0)
        if upper == U_RUN_RIGHT or upper == U_RUN_LEFT then
            c.angle_to_target = c.rot_y
        end
        if h.upper_anim_status == st(clip, true) then
            h.upper_anim_time = c.anim_time
        end
        c.angle_to_target = c.rot_y
    elseif upper == U_RUN_WALL_STOP then
        if flag(WALL_STOP_RIGHT) then
            upper_clip(st(A.run_forward_wall_stop_right, false))
        else
            upper_clip(st(A.run_forward_wall_stop_left, false))
        end
        follow_legs(U_RUN_WALL_STOP, 0)
        follow_legs(U_RUN_WALL_STOP, 0)
        c.angle_to_target = c.rot_y
    elseif upper == U_AIM then
        aiming()
    elseif upper == U_AIM_TARGET_LOCK then
        target_lock()
    elseif upper == U_AIM_START then
        aim_start()
    elseif upper == U_AIM_START_TARGET_LOCK or upper == U_AIM_TARGET_LOCK_SWITCH then
        lock_start()
    elseif upper == U_AIM_STOP then
        aim_stop()
    elseif upper == U_ATTACK then
        if combat_anim() then
            return true
        end
    elseif upper == U_RELOAD then
        reloading()
    end
    return false
end

-- -----------------------------------------------------------------------------------------
-- Raising the weapon, the kick or stomp at a downed enemy, lowering it, the reload button, the attack
-- press (Player_CombatStateUpdate).

local CHARA_AIR_SCREAMER = sh.chara_id("AirScreamer")
local CHARA_NIGHT_FLUTTER = sh.chara_id("NightFlutter")
local CHARA_CREEPER = sh.chara_id("Creeper")
local CHARA_TARGETED = 0x2       -- the NPC is down (the kick and stomp can reach it)

-- True when (bx, bz) is outside `radius` of (ax, az) on the ground (Math_Distance2dCheck).
local function far_from(ax, az, bx, bz, radius)
    local dx = s32(ax - bx)
    if radius < dx or radius < -dx then
        return true
    end
    local dz = s32(az - bz)
    if radius < dz or radius < -dz then
        return true
    end
    local sum = s32(s32((dx * dx) // 4096) + s32((dz * dz) // 4096))
    return sum > s32((radius * radius) // 4096)
end

-- A downed enemy right in front of Harry: the attack button stomps it, the other one kicks it.
local function downed_enemy_ahead()
    if h.walk_run_blend ~= 0 then
        return false
    end
    if h.aiming == 0 or not gun(wa()) then
        for i = 0, NPC_LAST do
            local n = npc(i)
            local id = n.chara_id
            if id ~= 0 then
                local radius = 3481
                if id == CHARA_AIR_SCREAMER or id == CHARA_NIGHT_FLUTTER then
                    radius = 4915
                elseif id == CHARA_CREEPER then
                    radius = 2662
                end
                local x, y, z = s32(n.pos_x + n.box_offset_x), n.pos_y, s32(n.pos_z + n.box_offset_z)
                if not far_from(c.pos_x, c.pos_z, x, z, radius) and abs(s32(y - c.pos_y)) < 1228 and n.health > 0 and
                    n.flags & CHARA_TARGETED ~= 0 then
                    local heading = (sh.math.atan2(s32(x - c.pos_x), s32(z - c.pos_z)) + 4096) & 0xFFF
                    local delta = shortest_angle(c.rot_y, heading) & 0xFFFF
                    if (delta + 1023) & 0xFFFF < 2303 then
                        h.target_npc = i
                        return true
                    end
                end
            end
        end
    end
    h.target_npc = -1
    return false
end

-- Player_ExtraStateSet: Harry leaves the upper body machine for a state of his own.
local function harry_state_set(state)
    h.state = state
    legs_step_reset()
    upper_step_reset()
    h.upper_body = U_NONE
    h.lower_body = NONE
end

local RAISE_FROM = {
    [U_NONE] = true, [U_WALK_FORWARD] = true, [U_SIDESTEP_RIGHT] = true, [U_SIDESTEP_LEFT] = true,
    [U_WALK_BACKWARD] = true, [U_TURN_RIGHT] = true, [U_TURN_LEFT] = true,
}

local function reload_start_pose()
    local first = clip_keyframes(st(A.handgun_recoil, true))
    upper_step_reset()
    if first > 0 then
        h.upper_anim_keyframe = first
        h.upper_anim_time = first * 4096
    end
end

local function legs_to_reload()
    if h.lower_body == AIM or h.lower_body == ATTACK then
        h.lower_body = RELOAD
        legs_step_reset()
    end
end

local function legs_to_attack()
    if h.lower_body == AIM then
        h.lower_body = ATTACK
        legs_step_reset()
    end
end

-- The fire press is let through only where the aim clip is up (or at the end of a playing clip).
local function clip_at_end()
    local status = h.upper_anim_status
    if status & 1 ~= 0 and status < 76 then
        local end_kf, reached = clip_done(status, h.upper_anim_keyframe)
        return end_kf > 0 and reached
    end
    return false
end

local function attack_allowed()
    local status, kf = h.upper_anim_status, h.upper_anim_keyframe
    local r = weapon_ready_keyframe
    if not gun(wa()) then
        if powered(wa()) then
            if (status ~= st(A.handgun_aim, true) or kf ~= r(0)) and
                (status ~= st(A.handgun_aim_start, true) or kf ~= r(1)) and
                (status ~= st(A.aim_resume, true) or kf < weapon_first_keyframe(6) or r(6) < kf) and
                (status ~= st(A.weapon_attack_2, true) or kf ~= r(2)) and
                (status ~= st(A.handgun_recoil, true) or kf ~= r(3)) and
                (status ~= st(A.powered_weapon_idle, true) or kf ~= r(5)) then
                -- Past the ready keyframe counts too (a frame can step over it).
                local gated = true
                if status == st(A.handgun_aim, true) then
                    gated = kf < r(0)
                elseif status == st(A.handgun_aim_start, true) then
                    gated = kf < r(1)
                elseif status == st(A.weapon_attack_2, true) then
                    gated = kf < r(2)
                elseif status == st(A.handgun_recoil, true) then
                    gated = kf < r(3)
                elseif status == st(A.powered_weapon_idle, true) then
                    gated = kf < r(5)
                elseif status == st(A.aim_resume, true) then
                    gated = kf < weapon_first_keyframe(6) or kf > r(6)
                end
                if gated and clip_at_end() then
                    gated = false
                end
                if gated then
                    return false
                end
            end
        elseif (status ~= st(A.handgun_aim, true) or kf ~= r(0)) and
            (status ~= st(A.handgun_aim_start, true) or kf ~= r(1)) and
            (status ~= st(A.weapon_attack_2, true) or kf ~= r(2)) and
            (status ~= st(A.handgun_recoil, true) or kf ~= r(3)) then
            if not clip_at_end() then
                return false
            end
        end
        return true
    end
    local gated = (status ~= st(A.handgun_aim, true) or kf ~= r(0)) and
        (status ~= st(A.handgun_aim_start, true) or kf ~= r(1)) and
        (status ~= st(A.weapon_attack_2, true) or kf ~= r(2)) and
        (status ~= st(A.handgun_recoil, true) or kf ~= r(3)) and
        (status ~= st(A.aim_target_lock_start, true) or kf ~= r(4)) and
        (status ~= st(A.gun_attack, true) or kf ~= r(8)) and
        (status ~= st(A.aim_resume, true) or kf ~= weapon_first_keyframe(6))
    if gated then
        if status == st(A.handgun_aim, true) then
            gated = kf < r(0)
        elseif status == st(A.handgun_aim_start, true) then
            gated = kf < r(1)
        elseif status == st(A.weapon_attack_2, true) then
            gated = kf < r(2)
        elseif status == st(A.handgun_recoil, true) then
            gated = kf < r(3)
        elseif status == st(A.aim_target_lock_start, true) then
            gated = kf < r(4)
        elseif status == st(A.gun_attack, true) then
            gated = kf < r(8)
        elseif status == st(A.aim_resume, true) then
            gated = kf < weapon_first_keyframe(6) or kf > r(6)
        end
    end
    if gated and not clip_at_end() then
        return false
    end
    return true
end

local function combat_state()
    -- Running with the aim held lowers the run to a walk on the PC's 2D and alternate controls.
    local u = h.upper_body
    if (h.alt_camera ~= 0 or (h.control_2d ~= 0 and h.fps_camera == 0 and h.demo == 0)) and ctl("aim") ~= 0 and
        wa() >= weapon_attack(W.KitchenKnife, TAP) and (u == U_RUN_FORWARD or u == U_RUN_RIGHT or u == U_RUN_LEFT) then
        h.upper_body = U_WALK_FORWARD
        if h.alt_camera == 0 and h.lower_body == RUN_FORWARD then
            h.lower_body = WALK_FORWARD
            legs_step_reset()
        end
    end

    -- Raising the weapon.
    if RAISE_FROM[h.upper_body] and h.walk_run_blend == 0 and
        ((ctl("aim") ~= 0 and wa() >= weapon_attack(W.KitchenKnife, TAP)) or h.aiming ~= 0) then
        h.aiming = 1
        if not gun(wa()) then
            h.locked_target = -1
        elseif h.option_auto_aim ~= 0 then
            local span, range = search_ranges({ 0x238, 40960 }, { 0x238, 12288 })
            h.locked_target = search_into(span, range, 0)
        else
            local span, range = search_ranges({ 12288, 28672 }, { 3686, 8601 })
            h.locked_target = search_into(span, range, 4)
        end
        if h.alt_camera ~= 0 and gun(wa()) then
            -- Alternate cameras: straight into the held aim pose, no lock.
            h.target_npc = -1
            h.upper_body = U_AIM
            h.upper_state_step = 1
            h.upper_control_state = 0
            upper_pose(st(A.aim_resume, true), aim_hold_keyframe())
            c.harry_aim_pitch = 1024
        else
            h.target_npc = h.locked_target
            if h.target_npc == -1 then
                h.upper_body = U_AIM_START
                c.harry_aim_pitch = 1024
            else
                h.state = STATE_COMBAT
                h.upper_body = U_AIM_START_TARGET_LOCK
            end
        end
        if h.lower_body == NONE then
            h.lower_body = AIM
            legs_step_reset()
        elseif h.lower_body < AIM then
            h.lower_body = h.lower_body + AIM
        end
        upper_step_reset()
        c.harry_aim_hold = 0
        -- The original fills the gun from the ammo carried as it comes up (auto_reload 2).
        if h.ammo_reserve ~= 0 and h.option_auto_reload == 2 and gun(wa()) then
            local current, total = reload_rounds(h.ammo, h.ammo_reserve)
            h.ammo = current
            h.ammo_reserve = total
        end
        if gun(wa()) then
            inventory_sync()
        end
    end

    -- A downed enemy in front: kick or stomp it.
    u = h.upper_body
    if RAISE_FROM[u] or u == U_AIM then
        if downed_enemy_ahead() then
            -- A touch screen shows its attack button here even with the weapon down.
            sh.input.touch_prompt("attack", "kick")
            if ctl("attack") ~= 0 then
                harry_state_set(STATE_STOMP_ENEMY)
                return
            end
            if ctl("shoot") ~= 0 then
                harry_state_set(STATE_KICK_ENEMY)
                return
            end
        end
    end

    u = h.upper_body
    if u ~= U_AIM and u ~= U_AIM_TARGET_LOCK then
        return
    end
    -- Lowering the weapon (hold-to-aim lets go, toggle-aim presses again).
    local aim = ctl("aim")
    if (h.option_weapon_control ~= 0 and aim == 0) or (h.option_weapon_control == 0 and aim ~= 0) then
        c.player_flex_angle_x = 0
        h.upper_body = U_AIM_STOP
        h.target_npc = -1
        flags_clear(TARGET_ATTACK)
        h.state = STATE_NONE
        h.aiming = 0
        flags_clear(LOCK_ATTACK)
        if h.lower_body == AIM or h.lower_body == ATTACK then
            h.lower_body = NONE
            legs_step_reset()
            upper_step_reset()
            return
        end
        if h.lower_body >= AIM_WALK_FORWARD then
            h.lower_body = h.lower_body - AIM
        end
        upper_step_reset()
        return
    end
    -- The PC's reload button.
    if h.reload_request ~= 0 then
        h.reload_request = 0
        h.upper_body = U_RELOAD
        flags_clear(LOCK_ATTACK)
        reload_start_pose()
        legs_to_reload()
        return
    end
    if not ((ctl("attack") ~= 0 or ctl("shoot") ~= 0) and h.lower_body ~= AIM_QUICK_TURN_RIGHT and
            h.lower_body ~= AIM_QUICK_TURN_LEFT and not (h.melee_needs_release ~= 0 and not gun(wa()))) then
        flags_clear(LOCK_ATTACK)
        return
    end
    if not attack_allowed() then
        return
    end

    -- The attack: at the target the search finds (melee: the nearest in reach; guns: the lock).
    flags_clear(TARGET_ATTACK)
    if not gun(wa()) then
        local span, range = search_ranges({ 12288, 12288 }, { 4096, 4096 })
        h.locked_target = search_into(span, range, 5)
        h.target_npc = h.locked_target
    else
        local span, range = search_ranges({ 28672, 28672 }, { 8601, 8601 })
        h.locked_target = search_into(span, range, 5)
    end
    local locked = h.locked_target
    local gun_locked = false
    if locked ~= -1 and gun(wa()) then
        h.state = STATE_COMBAT
        flags_set(TARGET_ATTACK | LOCK_ATTACK)
        if h.target_npc ~= locked then
            h.upper_body = U_AIM_TARGET_LOCK_SWITCH
        else
            h.upper_body = U_ATTACK
            legs_to_attack()
        end
        h.target_npc = locked
        gun_locked = true
    elseif locked ~= -1 then
        h.upper_body = U_ATTACK
        legs_to_attack()
    end
    if not gun_locked then
        c.harry_aim_pitch = 1024
        h.upper_body = U_ATTACK
        flags_clear(LOCK_ATTACK)
        legs_to_attack()
    end
    if gun(wa()) then
        -- Empty gun, rounds carried: the reload instead of the shot.
        if h.ammo == 0 and h.equipped_gun ~= 0 and h.ammo_reserve ~= 0 and h.auto_reload ~= 0 then
            h.upper_body = U_RELOAD
            flags_clear(LOCK_ATTACK)
            reload_start_pose()
            legs_to_reload()
        end
    else
        h.upper_body = U_ATTACK
        local id = weapon_of(wa())
        if h.lower_body == AIM or id == W.SteelPipe or id == W.Hammer or id == W.RockDrill or id == W.Katana then
            h.lower_body = ATTACK
            legs_step_reset()
        end
    end
    if h.upper_body ~= U_RELOAD then
        upper_step_reset()
    end
end

-- -----------------------------------------------------------------------------------------
-- The tick of the upper body (after the legs).

function upper_body(_)
    dt = sh.game.delta_time()
    if h.aim_resume > 0 then
        h.aim_resume = math.max(0, h.aim_resume - dt)
    end
    -- The alternate cameras' guns run their own machine while aimed (and through a recoil or reload).
    if h.alt_camera ~= 0 and
        (h.aiming ~= 0 or ctl("aim") ~= 0 or h.upper_body == U_RELOAD or h.upper_body == U_ATTACK) and
        gun(wa()) then
        local fresh = h.free_aim_active == 0
        h.free_aim_active = 1
        free_aim(fresh)
        return 0
    end
    h.free_aim_active = 0

    if upper_main() then
        return 0
    end

    local stumble
    if h.wall_ground_type == 10 then
        stumble = sound(c, "stumble_metal_plate", 1333)
    else
        stumble = sound(c, "stumble", 1314)
    end
    local u = h.upper_body
    if u == U_NONE then
        keyframe_sound(st(A.idle_exhausted, true), 551, sound(c, "heavy_breath", 1320), true)
    elseif u == U_STUMBLE then
        if keyframe_sound(st(A.run_forward_stumble, true), 173, stumble, false) then
            sh.pad_effect(19, 0xA0)
        end
    elseif u == U_RUN_LEFT_STUMBLE then
        if keyframe_sound(st(A.run_left_stumble, true), 340, stumble, false) then
            sh.pad_effect(19, 0xA0)
        end
    elseif u == U_RUN_RIGHT_STUMBLE then
        if keyframe_sound(st(A.run_right_stumble, true), 369, stumble, false) then
            sh.pad_effect(19, 0xA0)
        end
    end

    combat_state()
    return 0
end

-- -----------------------------------------------------------------------------------------
-- After Harry is drawn: where the weapon is, its effects, and the attack against the NPCs
-- (Player_CombatUpdate). Bone positions are in geometry units (256 = 1 m).

local BONE_ROOT_SPINE, BONE_RIGHT_HAND, BONE_RIGHT_FOOT = 6, 10, 17
local RUN_EFFECT_SPEED = 13000   -- Q12 3.1739 m/s: the motor's smoke every tick at this speed
local AIM_RANGE = 245760         -- Q12 60 m: the alternate camera's aim ray
local PITCH_LOW, PITCH_HIGH = 384, 1664   -- the effect's pitch: 33.75 to 146.25 degrees

local function bone_world_q12(bone)
    local x, y, z = sh.entity.harry_bone_world(bone)
    return s32(x << 4), s32(y << 4), s32(z << 4)
end

-- The tip of the melee weapon in the hand's space, by its reach (gamedata/weapons attack row).
local function melee_tip(attack, reach)
    local id = weapon_of(attack)
    if mod_attack(attack) then
        id = mod_weapon_entry(id).like
    end
    local function m(k)
        return asr(s32(reach * k), 12)
    end
    if id == W.KitchenKnife then
        return m(15), -asr(s32(reach * 75), 11), asr(m(75), 1)
    elseif id == W.SteelPipe then
        return m(15), -asr(m(225), 1), asr(s32(reach * 45), 10)
    elseif id == W.Chainsaw then
        return asr(m(15), 1), -asr(m(135), 1), asr(m(495), 1)
    elseif id == W.RockDrill then
        return 0, -m(45), asr(s32(reach * 45), 10)
    elseif id == W.Axe then
        -- The reach is read unsigned here.
        return 0, -asr(m(705), 1), (((reach & 0xFFFFFFFF) * 195) & 0xFFFFFFFF) >> 12
    elseif id == W.Hammer then
        return asr(m(15), 1), -m(105), asr(m(315), 1)
    elseif id == W.Katana then
        return asr(m(15), 1), -asr(m(315), 1), m(15)
    end
    return nil
end

local function clamp_pitch(p)
    if p >= PITCH_LOW then
        if p > PITCH_HIGH then
            return PITCH_HIGH
        end
        return p
    end
    return PITCH_LOW
end

-- The alternate camera's shot: along the camera ray to what it meets (or the aim assist's pick).
local function camera_aim(hand_x, hand_y, hand_z)
    local px, py, pz, fx, fy, fz = sh.entity.tps_camera()
    local ox, oy, oz = fx * 60, fy * 60, fz * 60
    local hit, _, tx, ty, tz = sh.entity.ray_trace_slot(0, px, py, pz, ox, oy, oz)
    if hit == 0 then
        local _, wx, wy, wz = sh.entity.ray_world(px, py, pz, s32(px + ox), s32(py + oy), s32(pz + oz))
        tx, ty, tz = wx, wy, wz
    end
    if h.aim_assist ~= 0 and h.fps_camera == 0 then
        local found, ax, ay, az = sh.entity.aim_assist_find(px, py, pz, fx, fy, fz, AIM_RANGE)
        if found ~= -1 then
            tx, ty, tz = ax, ay, az
        end
    end
    local yaw = s16(sh.math.atan2(s32(tx - hand_x), s32(tz - hand_z)))
    local dx6, dz6, dy6 = asr(s32(tx - hand_x), 6), asr(s32(tz - hand_z), 6), asr(s32(ty - hand_y), 6)
    local horiz = sh.math.sqrt(s32(s32(dx6 * dx6) + s32(dz6 * dz6)))
    local pitch = 1024
    if horiz ~= 0 or dy6 ~= 0 then
        pitch = sh.math.atan2(horiz, dy6)
    end
    return yaw, s16(pitch)
end

function combat_update(_)
    local attack = wa()
    local lower = h.lower_body
    local state = h.state
    -- The weapon's place: the right foot unarmed or kicking, else the right hand.
    local wx, wy, wz
    if lower < AIM or attack == -1 or attack == W.Kick or attack == W.Stomp then
        wx, wy, wz = bone_world_q12(BONE_RIGHT_FOOT)
    else
        wx, wy, wz = bone_world_q12(BONE_RIGHT_HAND)
    end
    h.attack_x, h.attack_y, h.attack_z = wx, wy, wz

    if c.harry_gas_weapon_timer ~= 0 then
        h.timer_2c = h.timer_2c + 1
        if c.harry_move_speed >= RUN_EFFECT_SPEED or h.timer_2c & 1 ~= 0 then
            sh.entity.harry_weapon_effect(h.equipped_weapon, 0, 0)
        end
    end

    local fighting = (state >= STATE_NONE and state < STATE_IDLE) or state == STATE_KICK_ENEMY or
        state == STATE_STOMP_ENEMY
    if (state >= STATE_CUTSCENE_FIRST and state < STATE_CUTSCENE_END) or not fighting then
        h.prev_attack_x, h.prev_attack_y, h.prev_attack_z = h.attack_x, h.attack_y, h.attack_z
        return 0
    end

    local yaw, pitch
    if gun(attack) and lower >= AIM then
        if state == STATE_COMBAT and h.locked_target ~= -1 then
            local n = npc(h.locked_target)
            yaw = s16(sh.math.atan2(s32(n.pos_x + n.box_offset_x - wx), s32(n.pos_z + n.box_offset_z - wz)))
        else
            c.angle_to_target = c.rot_y
            yaw = c.angle_to_target
        end
        pitch = c.harry_aim_pitch
        local effect_pitch = clamp_pitch(pitch)
        if h.alt_camera ~= 0 then
            yaw, pitch = camera_aim(wx, wy, wz)
            effect_pitch = clamp_pitch(pitch)
            c.harry_aim_pitch = effect_pitch
            h.target_pitch = -1
            h.target_yaw = -1
        end
        if c.attack_window > 0 then
            sh.entity.harry_weapon_effect(attack, effect_pitch, yaw)
        end
    else
        local bone, tx, ty, tz = BONE_RIGHT_FOOT, 0, 60, 134
        if attack ~= -1 and attack ~= W.Kick and attack ~= W.Stomp and lower >= AIM then
            local reach = sh.entity.weapon_attack_info(attack)
            tx, ty, tz = melee_tip(attack, reach)
            bone = BONE_RIGHT_HAND
            if tx == nil then
                error("no weapon tip for attack " .. tostring(attack))
            end
        end
        local px, py, pz = sh.entity.bone_point(0, bone, tx, ty, tz)
        local ax, ay, az = asr(h.attack_x, 4), asr(h.attack_y, 4), asr(h.attack_z, 4)
        local dx, dz = s32(ax - px), s32(az - pz)
        local horiz = sh.math.sqrt(s32(s32(dx * dx) + s32(dz * dz)))
        yaw = s16(sh.math.atan2(s32(px - ax), s32(pz - az)))
        pitch = s16(sh.math.atan2(horiz, s32(py - ay)))
    end

    -- The Hyper Blaster's beam from the muzzle.
    if attack == weapon_attack(W.HyperBlaster, TAP) and h.aiming ~= 0 and
        h.upper_anim_status >= st(A.handgun_aim, true) and h.upper_anim_keyframe >= 574 and state < STATE_IDLE then
        if state == STATE_NONE and h.target_npc ~= -1 then
            h.target_npc = -1
        end
        local mx, my, mz = sh.entity.bone_point(0, BONE_RIGHT_HAND, 0, -39, 87)
        if h.option_auto_aim ~= 0 then
            yaw = c.angle_to_target
        end
        sh.entity.hyper_blaster_beam(s32(mx << 4), s32(my << 4), s32(mz << 4), yaw, pitch)
    end

    -- The powered weapons' sparks between the hand and the body while they cut.
    if state < STATE_IDLE then
        local kf = h.upper_anim_keyframe
        if (attack == weapon_attack(W.Chainsaw, TAP) and kf >= 572 and kf < 584) or
            (attack == weapon_attack(W.RockDrill, TAP) and c.anim_keyframe >= 577 and kf < 583) then
            local hx, hy, hz = sh.entity.bone_point(0, BONE_RIGHT_HAND, 0, 0, 0)
            local bx, by, bz = sh.entity.bone_point(0, BONE_ROOT_SPINE, 0, 0, 0)
            sh.entity.weapon_beam(s32(hx << 4), s32(hy << 4), s32(hz << 4), s32(bx << 4), s32(by << 4), s32(bz << 4))
        end
    end

    -- The attack: guns from where the weapon was on the previous tick, along the lock's angles when the
    -- shot has them; melee from the weapon's place now. The game resolves what it hits.
    if h.upper_body ~= U_AIM_STOP then
        local window = c.attack_window
        if window > 0 then
            h.post_fire_trace = 8
        end
        if gun(attack) then
            local tp, ty_ = h.target_pitch, h.target_yaw
            local px, py, pz = h.prev_attack_x, h.prev_attack_y, h.prev_attack_z
            if tp ~= -1 or ty_ ~= tp then
                sh.entity.attack(0, window, attack, px, py, pz, 1, ty_, tp)
            else
                sh.entity.attack(0, window, attack, px, py, pz, 1, yaw, pitch)
            end
        else
            sh.entity.attack(0, window, attack, h.attack_x, h.attack_y, h.attack_z, 1, yaw, pitch)
        end
        h.shot_yaw = yaw
        h.shot_pitch = pitch
    end
    h.prev_attack_x, h.prev_attack_y, h.prev_attack_z = h.attack_x, h.attack_y, h.attack_z
    return 0
end
