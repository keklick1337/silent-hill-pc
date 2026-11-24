-- Harry's damage, grabs and deaths: receiving a hit and its reaction (knock-back, stun, throw, fall,
-- get-up), the enemy grabs and breaking free of them (input counts by difficulty), the death and game
-- over sequences, the kick and stomp states, and the tick's own timers (health saturation, the gas
-- weapon's power, the knock-on flags).
--
-- behavior.lua names this file (-- uses: Harry/damage): it runs first in Harry's Lua state. The engine
-- calls, once per Harry tick: receive_damage(0) before the logic (Player_Update), logic_pre(0) at its
-- start, idle(0) in the idle state, state_update(0) in every other state the legs and the upper body do
-- not own (returns 1 when the tick ends there: game over), logic_post(0) at its end (the turn, heading
-- and fall applied to the record).
--
-- Integer rules: common.lua; locals the original kept in 16 bits (angles, the grab distance) wrap
-- here too. `c` is Harry's record, `h` the state outside it (sh.entity.harry / set_harry). Reaction
-- clips are the map's own rows (sh.entity.overlay_anim_start): the packed numbers below are their ids.
-- A mod changing how Harry takes damage changes the game: "gameplay": true in mod.json.

local c = record(0)
local h = setmetatable({}, {
    __index = function(_, k)
        return sh.entity.harry(k)
    end,
    __newindex = function(_, k, v)
        sh.entity.set_harry(k, v)
    end,
})

-- Harry's states (h.state).
local STATE_NONE, STATE_FALL_FORWARD, STATE_FALL_BACKWARD, STATE_KICK_ENEMY, STATE_STOMP_ENEMY = 0, 3, 4, 5, 6
local STATE_THROWN_BY_BOLT, STATE_DEATH, STATE_INSTANT_DEATH = 7, 8, 9
local STATE_GRAB_TORSO_FRONT, STATE_GRAB_TORSO_BACK, STATE_GRAB_LEGS_FRONT, STATE_GRAB_LEGS_BACK = 10, 13, 14, 15
local STATE_RELEASE_UPPER_FRONT, STATE_RELEASE_UPPER_BACK = 16, 20
local STATE_RELEASE_LOWER_FRONT, STATE_RELEASE_LOWER_BACK = 21, 22
local STATE_RELEASE_A, STATE_RELEASE_B, STATE_DAMAGE_HEAD = 17, 18, 19
local STATE_DAMAGE_TORSO_BACK, STATE_DAMAGE_TORSO_FRONT, STATE_DAMAGE_TORSO_RIGHT, STATE_DAMAGE_TORSO_LEFT = 23, 24, 25, 26
local STATE_DAMAGE_FEET_FRONT, STATE_DAMAGE_FEET_BACK, STATE_DAMAGE_PUSH_BACK, STATE_DAMAGE_PUSH_FRONT = 27, 28, 29, 30
local STATE_GRAB_NECK_FRONT, STATE_GRAB_NECK_BACK = 32, 33
local STATE_EATEN = 36
local STATE_PINNED_START_FRONT, STATE_PINNED_START_BACK, STATE_PINNED_FRONT, STATE_PINNED_BACK = 37, 38, 39, 40
local STATE_RELEASE_PINNED_FRONT, STATE_RELEASE_PINNED_BACK, STATE_PINNED_RISE_FRONT, STATE_PINNED_RISE_BACK = 41, 42, 43, 44
local STATE_THROWN_FRONT, STATE_THROWN_BACK, STATE_ON_FLOOR_FRONT, STATE_ON_FLOOR_BACK = 45, 46, 47, 48
local STATE_GET_UP_FRONT, STATE_GET_UP_BACK = 49, 50

-- harry_flags bits.
local HIT_DONE = 0x4             -- the hit of this attack is done (also: a hit on Harry is being reacted to)
local LOCK_ATTACK = 0x200        -- the lock turn ends in an attack
local LOCK_SWITCHING = 0x1000
local DAMAGE_REPORTED = 0x4000   -- the damage sound and rumble of this hit have played

-- Record flags.
local STUN_TIMER_RUNS = 0x8

local MAP_PROLOGUE = 0           -- map0_s00: the bad dream; dying there only ends the dream

local dt = 0

local function ts(x)
    return idiv(x * dt, 136)
end

local function sq(x)
    return s32(x * x)
end

local function clamp(x, lo, hi)
    if x < lo then
        return lo
    elseif x > hi then
        return hi
    end
    return x
end

local function step_sound_set(v)
    c.harry_step_sound = v
end

local function flags_clear(bits)
    c.harry_flags = c.harry_flags & ~bits
end

local function flags_set(bits)
    c.harry_flags = c.harry_flags | bits
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

-- The NPC with game index `i` (0..NPC_LAST). Index -1 (no grabber set) is the record before the table, as
-- the original reads it (the bytes of Harry's own record); other indices outside the table read the first.
local function npc(i)
    if i == -1 then
        return record(-1)
    elseif i < 0 or i > NPC_LAST then
        i = 0
    end
    return record(i + 1)
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

-- Q12_MULT: a 32-bit product, shifted.
local function q12_mult(a, b)
    return asr(s32(a * b), 12)
end

local function active(status)
    return status & 1 ~= 0
end

-- extra_state_set lives in Harry/state.lua (the shared copy of the original's
-- Player_ExtraStateSet macro); behavior.lua loads it first.

local function shape_offsets_clear()
    c.cylinder_offset_z = 0
    c.cylinder_offset_x = 0
    c.box_offset_z = 0
    c.box_offset_x = 0
end

-- The box and cylinder of a standing Harry.
local function stand_shape()
    c.box_top = -6553
    c.box_bottom = 0
    c.box_offset_y = -4505
end

local function st(clip, is_active)
    return anim_status(clip, is_active)
end

-- The map's reaction row in use (h.weapon_clip is its index): where its clip starts and where it ends.
local function row_start()
    local _, _, time = sh.entity.overlay_row(h.weapon_clip)
    return time
end

local function row_end()
    local _, _, _, last = sh.entity.overlay_row(h.weapon_clip)
    return last
end

local function overlay_start(status)
    sh.entity.overlay_anim_start(status)
end

-- Tables of Harry's reaction clips (the root motion of the falls, the pin, the kick and the stomp, the
-- death), one run of 16-bit values: reads past a table land in the next one, as in the original.
local FLAT = {
    -6553, -4505, -6553, -4505, -6553, -4505, -6553, -4505, -6553, -4505, -6553, -4505,
    -6553, -4505, -6471, -4505, -6389, -4423, -6307, -4423, -6225, -4423, -6144, -4177,
    -5898, -4014, -5611, -4014, -5324, -3809, -4300, -2662, -3850, -2457, -2949, -2048,
    -2703, -1884, -2621, -1720, -2539, -1638, -2334, -1474, -2252, -1310, -2252, -1474,
    -2539, -1761, -2662, -2048, -2990, -2293, -3112, -2457, -3358, -2621, -3358, -2621,
    -3522, -2908, -3768, -3031, -4096, -3276, -4751, -3563, -5488, -3850, -5898, -3973,
    -6225, -4136, -6430, -4218, -6553, -4505, -6553, -4505, -6553, -4505, -6471, -4382,
    -5816, -4055, -5816, -4014, -6103, -4218, -5734, -4177, -5652, -4014, -4833, -3072,
    -3850, -2007, -3358, -1638, -2949, -1515, -2703, -1392, -2703, -1392, -2211, -1228,
    -2211, -1146, -2211, -1146, -2048, -1024, -1966, -1024, -1966, -1024, -2457, -1064,
    -2826, -1146, -3112, -1351, -3440, -1515, -3563, -1515, -3768, -1597, -3973, -1966,
    -4628, -2744, -4997, -3072, -5857, -3850, -6389, -4136, -6471, -4423, -6553, -4505,
    -6553, -4505, -6553, -4505, -6553, -4505, -6553, -4505, -6553, -4505, -6553, -4505,
    0, 0, -40, -81, -122, -163, -245, -327, -409, -491, -573, -655,
    -737, -819, -901, -819, -737, -655, -573, -409, -327, -245, -163, -122,
    -81, -40, 0, 0, 0, 0, -40, -122, -204, -286, -368, -450,
    -532, -614, -696, -737, -696, -614, -491, -327, -163, 0, 0, 163,
    327, 491, 655, 819, 983, 1146, 1310, 1474, 1638, 1802, 1966, 2129,
    2293, 2457, 2621, 2785, 2949, 3112, 3276, 3440, 3604, 3768, 3932, 4096,
    4259, 4423, 4587, 4751, 4915, 5079, 5242, 5406, 5570, 5734, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -40, -327, -614, -819, -942,
    -1064, -1187, -1187, -1187, -1187, -1187, -1187, -1187, -1187, -1187, -1187, -1187,
    -1187, -1187, -1187, -1187, -1392, -1433, -1433, -1392, -1392, -1351, -1351, -1351,
    -1310, -1310, -1269, -1187, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -204, -450, -614, -819, -1024, -1228, -1228, -1105, -983, -860,
    -737, -614, -491, -368, -245, -122, 0, 122, 286, 0, -81, -122,
    -122, -81, -40, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1228, 1228,
    1228, 1228, 1228, 1228, 1228, 1228, 1228, 1228, 1228, 1228, 1228, 1228,
    1228, 1228, 1310, 1351, 1392, 1433, 1474, 1515, 1556, 1597, 1638, 1638,
    1638, 1638, 1638, 1638, 1638, 1638, 1638, 1638, 1638, 1638, 1638, 1638,
    1638, 1638, 1638, 1638, 1228, 1228, 1228, 1228, 1228, 1228, 1228, 1228,
    1228, 1228, 1228, 0, 327, 655, 983, 1310, 1638, 1966, 1966, 0,
}

local FALL_FORWARD_AT, FALL_BACKWARD_AT, KICK_AT, STOMP_AT, DEATH_AT, PIN_AT = 0, 78, 156, 184, 202, 400
local function table_at(base, i)
    return FLAT[base + i + 1] or 0
end

-- ---------------------------------------------------------------------------------------------
-- Receiving damage (Player_ReceiveDamage).

local GRAB_STATES = {
    [10] = true, [13] = true, [14] = true, [15] = true, [32] = true, [33] = true, [39] = true, [40] = true,
    [47] = true, [48] = true,
}
-- States that take a hit's knock-back and wait (no new reaction starts).
local WAITING_STATES = { [41] = true, [42] = true, [45] = true, [46] = true, [49] = true, [50] = true }
-- States where a hit clears the knock-back and only an instant death starts something new.
local REACTING_STATES = {
    [7] = true, [16] = true, [17] = true, [18] = true, [19] = true, [20] = true, [21] = true, [22] = true,
    [23] = true, [24] = true, [25] = true, [26] = true, [27] = true, [28] = true, [35] = true, [37] = true,
    [38] = true,
}
-- The enemies' rows of the attack table (config/attacks.json), by name.
local ATK = {}
for _, name in ipairs({
    "AirScreamerStandBite", "AirScreamerHoverBite", "AirScreamerScratch", "GroanerJumpBite",
    "HangedScratcherCeilingSlash", "HangedScratcherSlash", "UnusedAttack46", "SplitHeadSwallow", "StalkerLunge",
    "StalkerLegGrab", "StalkerLegGrabDrain", "LarvalStalkerDrain", "CreeperBite", "RomperPin", "PuppetNurseGrab",
    "PuppetNurseSwing", "PuppetDoctorSwing", "FloatstingerSting", "FloatstingerAcid", "TwinfeelerStrike",
    "TwinfeelerAcid", "CybilHandgun", "MonsterCybilStrike", "MonsterCybilRifleStrike", "MonsterCybilNeckGrab",
    "BossRoomLightning", "Unknown23Shock", "BloodsuckerTentacle",
}) do
    ATK[name] = sh.entity.attack_number(name)
end
-- Attacks that bring Harry down with a damage reaction from the side the blow came.
local SIDE_BLOWS = {}
for _, name in ipairs({
    "AirScreamerStandBite", "GroanerJumpBite", "HangedScratcherCeilingSlash", "HangedScratcherSlash",
    "UnusedAttack46", "StalkerLunge", "StalkerLegGrabDrain", "LarvalStalkerDrain", "PuppetNurseSwing",
    "PuppetDoctorSwing", "FloatstingerSting", "TwinfeelerStrike", "MonsterCybilStrike", "MonsterCybilRifleStrike",
}) do
    SIDE_BLOWS[ATK[name]] = true
end
-- Thrown off by the blow: the shock and the tentacle.
local function pushing_blow(attack)
    return attack == ATK.Unknown23Shock or attack == ATK.BloodsuckerTentacle
end

local function damage_clear()
    c.damage_amount = 0
    c.damage_z = 0
    c.damage_y = 0
    c.damage_x = 0
end

-- The reaction of a blow from `enemy_angle` (0..4095, relative to Harry's facing).
local function torso_state(enemy_angle)
    if enemy_angle >= 512 and enemy_angle < 1536 then
        return STATE_DAMAGE_TORSO_LEFT
    elseif enemy_angle >= 1536 and enemy_angle < 2560 then
        return STATE_DAMAGE_TORSO_FRONT
    elseif enemy_angle >= 2560 and enemy_angle < 3584 then
        return STATE_DAMAGE_TORSO_RIGHT
    end
    return STATE_DAMAGE_TORSO_BACK
end

-- The reaction to the attack Harry received this tick (the default of the state switch). Returns
-- true when the damage code ends there.
-- The reaction states and the row of the map's reaction table each plays (anim << 1 | active).
local STATE_CLIP = {
    [STATE_THROWN_BY_BOLT] = 200, [STATE_INSTANT_DEATH] = 203, [STATE_DAMAGE_HEAD] = 242,
    [STATE_DAMAGE_TORSO_BACK] = 211, [STATE_DAMAGE_TORSO_FRONT] = 210, [STATE_DAMAGE_TORSO_RIGHT] = 212,
    [STATE_DAMAGE_TORSO_LEFT] = 213, [STATE_DAMAGE_FEET_FRONT] = 215, [STATE_DAMAGE_FEET_BACK] = 214,
    [STATE_THROWN_FRONT] = 263, [STATE_THROWN_BACK] = 264,
}

-- Starts the reaction `state`. A state whose clip the map has no row for never ends (its end is the row's
-- last keyframe): a monster spawned away from its maps whose clips could not be loaded
-- gets the torso hit of that side instead, or no reaction when the map cannot play that either. On the
-- maps of the original every reaction of their monsters has its row, so this only ever picks `state` there.
local function reaction_set(state, frontal)
    local id = STATE_CLIP[state]
    if id ~= nil and sh.entity.harry_reaction_row(id) < 0 then
        local torso = frontal and STATE_DAMAGE_TORSO_FRONT or STATE_DAMAGE_TORSO_BACK
        if sh.entity.harry_reaction_row(STATE_CLIP[torso]) < 0 then
            c.attack_received = -1
            return
        end
        state = torso
    end
    extra_state_set(state)
end

local function hit_reaction()
    if h.walk_run_blend ~= 0 then
        h.pending_attack = c.attack_received
        return true
    end

    if h.pending_attack ~= -1 then
        c.attack_received = h.pending_attack
        h.pending_attack = -1
    end

    local attack = c.attack_received
    if attack <= 0 then
        return false
    end

    h.target_npc = -1
    h.weapon_attack = h.unequipped ~= 0 and -1 or h.equipped_weapon
    if h.weapon_attack == sh.entity.weapon_number("RockDrill") then
        sh.entity.weapon_model_set(2, 3)
    end

    local state = h.state
    if state >= STATE_FALL_FORWARD and state < STATE_THROWN_BY_BOLT then
        stand_shape()
        shape_offsets_clear()
    end

    local enemy_angle = s16(npc(c.harry_attacker).rot_y)
    if attack == ATK.MonsterCybilStrike or attack == ATK.MonsterCybilRifleStrike then
        enemy_angle = s16(enemy_angle - 1024)
    elseif attack == ATK.BloodsuckerTentacle then
        enemy_angle = 1024
    elseif attack == ATK.Unknown23Shock then
        enemy_angle = s16(c.damage_y)
    end
    enemy_angle = (s16(enemy_angle - c.rot_y) + 4096) & 0xFFF
    local facing = enemy_angle >= 1024 and enemy_angle < 3072

    if attack == ATK.BossRoomLightning then
        reaction_set(STATE_THROWN_BY_BOLT, facing)
    elseif attack == ATK.CybilHandgun then
        c.harry_move_speed = 6144
        -- The shooter's facing: the boss is the first NPC on her map; a spawned one is the attacker.
        local shooter = record(1)
        if c.harry_attacker >= 0 and engine_spawn(npc(c.harry_attacker)) then
            shooter = npc(c.harry_attacker)
        end
        h.heading_offset = shortest_angle(c.rot_y, shooter.rot_y)
        if facing then
            reaction_set(STATE_THROWN_FRONT, facing)
        else
            reaction_set(STATE_THROWN_BACK, facing)
        end
    elseif attack == ATK.FloatstingerAcid or attack == ATK.TwinfeelerAcid then
        c.damage_amount = 40960
        reaction_set(STATE_DAMAGE_HEAD, facing)
    elseif attack == ATK.AirScreamerHoverBite or attack == ATK.AirScreamerScratch then
        reaction_set(STATE_DAMAGE_HEAD, facing)
    elseif attack == ATK.StalkerLegGrab or attack == ATK.RomperPin or attack == ATK.PuppetNurseGrab
        or attack == ATK.MonsterCybilNeckGrab then
        -- A monster placed in a map that has none of its grab rows cannot hold Harry: the ordinary torso
        -- reaction stands in, or the hit is dropped when the map cannot animate that either.
        local frontal = enemy_angle >= 1024 and enemy_angle < 3072
        if sh.entity.grab_playable(attack, frontal and 1 or 0) == 0 then
            if sh.entity.grab_fallback_playable(frontal and 1 or 0) ~= 0 then
                extra_state_set(frontal and STATE_DAMAGE_TORSO_FRONT or STATE_DAMAGE_TORSO_BACK)
            end
            c.attack_received = -1
        elseif frontal then
            h.grabber_front = c.harry_attacker
            if attack == ATK.RomperPin then
                extra_state_set(STATE_PINNED_START_FRONT)
            elseif attack == ATK.PuppetNurseGrab then
                extra_state_set(STATE_GRAB_TORSO_FRONT)
            elseif attack == ATK.StalkerLegGrab then
                extra_state_set(STATE_GRAB_LEGS_FRONT)
            elseif attack == ATK.MonsterCybilNeckGrab then
                extra_state_set(STATE_GRAB_NECK_FRONT)
            end
        else
            h.grabber_back = c.harry_attacker
            if attack == ATK.RomperPin then
                extra_state_set(STATE_PINNED_START_BACK)
            elseif attack == ATK.PuppetNurseGrab then
                extra_state_set(STATE_GRAB_TORSO_BACK)
            elseif attack == ATK.StalkerLegGrab then
                extra_state_set(STATE_GRAB_LEGS_BACK)
            elseif attack == ATK.MonsterCybilNeckGrab then
                extra_state_set(STATE_GRAB_NECK_BACK)
            end
        end
    elseif attack == ATK.SplitHeadSwallow then
        c.health = -1
        c.cylinder_extent = 0
        reaction_set(STATE_INSTANT_DEATH, facing)
        return true
    elseif pushing_blow(attack) or SIDE_BLOWS[attack] then
        if attack == ATK.BloodsuckerTentacle then
            c.harry_move_speed = 6553
            h.heading_offset = shortest_angle(c.rot_y, 1024)
        end
        if attack == ATK.Unknown23Shock then
            c.harry_move_speed = 16384
            h.heading_offset = shortest_angle(c.rot_y, s16(c.damage_y))
        end
        reaction_set(torso_state(enemy_angle), facing)
    elseif attack == ATK.CreeperBite then
        reaction_set(enemy_angle < 2047 and STATE_DAMAGE_FEET_FRONT or STATE_DAMAGE_FEET_BACK, facing)
    end

    if h.state >= STATE_FALL_FORWARD then
        c.player_afk_timer = 0
        c.player_flex_angle_x = 0
        flags_clear(LOCK_SWITCHING)
        h.aiming = 0
        flags_clear(LOCK_ATTACK)
        c.harry_attack_marker = -1
    end
    return false
end

function receive_damage(_)
    dt = sh.game.delta_time()
    local attack = c.attack_received

    -- The damage sound by the attack's class.
    local sfx_id = sound(c, "damage", 1326)
    if attack ~= -1 then
        local class = sh.entity.attack_sound_class(attack)
        if class == 2 then
            sfx_id = sound(c, "damage_type_2", 1327)
        elseif class == 4 then
            sfx_id = sound(c, "damage_type_4", 1328)
        elseif class == 5 then
            sfx_id = sound(c, "damage_type_5", 1329)
        end
    end

    if sh.entity.player_control_disabled() ~= 0 or h.damage_disabled ~= 0 then
        c.damage_amount = 0
        return
    end

    local state = h.state
    local reaction = false
    if state == STATE_FALL_FORWARD or state == STATE_FALL_BACKWARD or WAITING_STATES[state] then
        -- nothing starts
    elseif state == STATE_DEATH or state == STATE_INSTANT_DEATH then
        return
    elseif GRAB_STATES[state] then
        if c.damage_amount ~= 0 and c.harry_flags & DAMAGE_REPORTED == 0 then
            flags_set(DAMAGE_REPORTED)
            sfx(sfx_id, 32, 0)
            step_sound_set(64)
        end
        if c.damage_amount == 0 then
            flags_clear(DAMAGE_REPORTED)
        end
        sh.pad.effect_level(9, 0xA0)
    elseif REACTING_STATES[state] then
        c.damage_z = 0
        c.damage_y = 0
        c.damage_x = 0
        if attack == ATK.SplitHeadSwallow then
            c.cylinder_extent = 0
            extra_state_set(STATE_INSTANT_DEATH)
            return
        end
        if pushing_blow(attack) then
            c.damage_amount = 0
        end
    else
        reaction = true
    end

    if reaction and hit_reaction() then
        return
    end

    if h.state == STATE_DEATH then
        damage_clear()
        return
    end

    if c.damage_amount ~= 0 then
        -- Mods may change or cancel the hit (player.damage); a cancelled one costs no health and plays no sound.
        sh.entity.harry_damage_hook()
    end
    if c.damage_amount ~= 0 then
        -- God mode absorbs the hit before it reaches the health; the flinch and the sound still play.
        if h.god_mode ~= 0 then
            c.damage_amount = 0
        end
        flags_clear(HIT_DONE)
        if c.harry_flags & DAMAGE_REPORTED == 0 then
            sfx(sfx_id, 32, 0)
            flags_set(DAMAGE_REPORTED)
            step_sound_set(0x40)
        end

        if h.map_index == MAP_PROLOGUE then
            c.health = c.health - s32(c.damage_amount * 2)
        else
            local difficulty = h.difficulty
            if difficulty == -1 then
                c.damage_amount = asr(s32(c.damage_amount * 3), 2)
            elseif difficulty == 1 then
                c.damage_amount = asr(s32(c.damage_amount * 6), 2)
            end
            c.health = c.health - c.damage_amount
        end

        if c.health < 0 then
            c.health = -1
            h.dead = 1
        end

        -- The pad rumbles by how much it hurt.
        local amount = c.damage_amount
        local strength
        if amount > 102400 then
            strength = 200
        elseif amount < 20480 then
            strength = 100
        else
            strength = idiv(amount + 61440, 20480) * 25
        end
        sh.pad.effect_level(10, strength)
        c.damage_amount = 0
    end

    state = h.state
    if c.health <= 0 and state ~= STATE_DEATH and state ~= STATE_EATEN and state ~= STATE_PINNED_FRONT
        and state ~= STATE_PINNED_BACK and state ~= STATE_ON_FLOOR_FRONT and state ~= STATE_ON_FLOOR_BACK
        and h.walk_run_blend == 0 then
        c.harry_attacker = -1
        h.saturation = 0
        h.grabber_front = -1
        h.grabber_back = -1
        h.grabber_unused_2 = -1
        h.grabber_unused_3 = -1
        if c.attack_received == ATK.MonsterCybilNeckGrab then
            extra_state_set(STATE_EATEN)
        else
            extra_state_set(STATE_DEATH)
        end
    end
end

-- ---------------------------------------------------------------------------------------------
-- The start of the tick: timers, saturation and the health it feeds.

function logic_pre(_)
    dt = sh.game.delta_time()
    h.step_speed = 0
    h.turn_speed = 0
    step_sound_set(asr(c.harry_step_sound, 1))

    if c.flags & STUN_TIMER_RUNS ~= 0 then
        c.harry_flag_timer = c.harry_flag_timer + dt
    end
    if c.harry_flag_timer > h.flag_timer_limit then
        c.harry_flag_timer = 0
        c.flags = c.flags & ~STUN_TIMER_RUNS
    end

    if c.harry_gas_weapon_timer ~= 0 then
        c.harry_gas_weapon_timer = c.harry_gas_weapon_timer - dt
    end
    c.harry_gas_weapon_timer = clamp(c.harry_gas_weapon_timer, 0, 245760)

    -- The chainsaw and the rock drill idle or rev by the power left.
    local attack = h.weapon_attack
    if attack == sh.entity.weapon_number("Chainsaw") or attack == sh.entity.weapon_number("RockDrill") then
        sh.entity.weapon_model_set(attack, c.harry_gas_weapon_timer ~= 0 and 4 or 2)
    end

    h.saturation = clamp(h.saturation - dt, 0, sh.entity.max_saturation())
    if h.saturation ~= 0 then
        c.health = clamp(c.health + dt, 0, sh.entity.max_health())
    end
end

-- The idle state: Harry stands looking around until a control is touched. Returns 1 while he stays idle.
function idle(_)
    c.harry_move_speed = 0
    c.harry_quick_turn_heading = sh.entity.harry_light_angle()

    if h.upper_state_step == 0 then
        h.upper_anim_status = st(17, false)
        h.upper_state_step = h.upper_state_step + 1
    end
    if c.state_step == 0 then
        c.anim_status = st(17, false)
        c.state_step = c.state_step + 1
    end

    -- Holding the aim button with a weapon out leaves idle at once (the aim machine runs in the next state).
    local aim_held = sh.input.control("aim_button") ~= 0 and h.rear_look == 0
    local has_weapon = h.weapon_attack ~= -1
    if not aim_held or not has_weapon then
        if sh.input.control("has_move") == 0 and sh.input.control("has_action") == 0 then
            return 1
        end
    end
    extra_state_set(STATE_NONE)
    return 0
end

-- ---------------------------------------------------------------------------------------------
-- The states of the damage, grab, fall and death family (the state switch of Player_LogicUpdate).

-- The pin's root motion and the pull towards the spot the monster holds Harry on.
local function pull_towards_anchor()
    local ax, az = h.pin_target_x, h.pin_target_z
    if math.abs(c.pos_x - ax) <= 204 then
        c.pos_x = ax
    elseif c.pos_x >= ax then
        c.pos_x = c.pos_x - 205
    else
        c.pos_x = c.pos_x + 205
    end
    if math.abs(c.pos_z - az) <= 204 then
        c.pos_z = az
    elseif c.pos_z >= az then
        c.pos_z = c.pos_z - 205
    else
        c.pos_z = c.pos_z + 205
    end
end

-- A turn of 11.25 degrees per tick towards `target` (the monster's facing, or its back).
local function turn_towards(heading, target)
    if math.abs(heading) < 128 then
        c.rot_y = target
    elseif heading > 0 then
        c.rot_y = c.rot_y + 128
    else
        c.rot_y = c.rot_y - 128
    end
end

local function norm(angle)
    return (angle + 4096) & 0xFFF
end

-- The end of the dying: the engine's game over (or, in the prologue, the dream's end), Harry standing again.
local function finish_dying(prologue, reset_gas)
    sh.entity.harry_player_anim_lock()
    if prologue then
        sh.flag_set("M0S00_HarryDiedInNightmare", true)
    else
        sh.entity.harry_game_over()
    end
    sh.entity.harry_state_reset()
    h.upper_control_state = h.upper_control_state + 1
    c.health = sh.entity.max_health()
    c.control_state = c.control_state + 1
    if reset_gas then
        c.harry_gas_weapon_timer = 0
    end
end

-- Pc_ReactionStateStuck: a reaction clip that stops short of its end keyframe (a map without the rows)
-- frees Harry after a second, whatever the clip says; so does a state lasting ten seconds.
local function reaction_stuck(state, body_kf, upper_kf)
    if state ~= h.stuck_state then
        h.stuck_total = 0
        h.stuck_still = 0
    elseif body_kf ~= h.stuck_body_keyframe or upper_kf ~= h.stuck_upper_keyframe then
        h.stuck_still = 0
    else
        h.stuck_still = h.stuck_still + dt
    end
    h.stuck_total = h.stuck_total + dt

    h.stuck_state = state
    h.stuck_body_keyframe = body_kf
    h.stuck_upper_keyframe = upper_kf

    if h.stuck_still <= 4096 and h.stuck_total <= 40960 then
        return false
    end
    h.stuck_state = -1
    h.stuck_still = 0
    h.stuck_total = 0
    return true
end

-- The state thrown by a bolt of light (the end of the game).
local function bolt_thrown()
    overlay_start(st(100, false))
    if c.harry_move_speed ~= 0 then
        c.harry_move_speed = c.harry_move_speed - ts(2048)
        if c.harry_move_speed < 0 then
            c.harry_move_speed = 0
        end
    end
    local attack = c.attack_received
    if not pushing_blow(attack) then
        h.heading_offset = sh.math.atan2(c.damage_x, c.damage_z) - c.rot_y
        c.harry_move_speed = s32(s32(sq(c.damage_x) + sq(c.damage_z)) + sq(c.damage_y))
    end
    if h.upper_anim_keyframe == row_end() then
        c.attack_received = -1
        h.target_npc = -1
        flags_clear(DAMAGE_REPORTED)
        extra_state_set(STATE_NONE)
        c.harry_move_speed = 0
    end
    h.step_speed = c.harry_move_speed
    c.flags = c.flags | STUN_TIMER_RUNS
    c.attack_received = -1
end

-- Thrown to the floor (Cybil's shot).
local function thrown(state)
    local status, floor_state
    if state == STATE_THROWN_FRONT then
        status = st(131, true)
        floor_state = STATE_ON_FLOOR_FRONT
    else
        status = st(132, false)
        floor_state = STATE_ON_FLOOR_BACK
    end
    overlay_start(status)
    if c.anim_keyframe == row_end() then
        extra_state_set(floor_state)
        c.player_afk_timer = 40960
    end
    if c.harry_move_speed ~= 0 then
        c.harry_move_speed = c.harry_move_speed - asr(ts(2048), 3)
        if c.harry_move_speed < 0 then
            c.harry_move_speed = 0
        end
    end
    h.step_speed = c.harry_move_speed
end

-- The Romper pins Harry: he turns to it and lies down under it.
local function pinned_start(state)
    local status, next_state, idx, heading
    if state == STATE_PINNED_START_FRONT then
        status = st(127, true)
        next_state = STATE_PINNED_FRONT
        idx = h.grabber_front
        heading = shortest_angle(c.rot_y, (npc(idx).rot_y + 2048) & 0xFFF)
    else
        status = st(128, true)
        next_state = STATE_PINNED_BACK
        idx = h.grabber_back
        heading = shortest_angle(c.rot_y, norm(npc(idx).rot_y))
    end

    c.harry_move_speed = 0
    overlay_start(status)
    c.cylinder_radius = 1024
    c.cylinder_extent = 0

    if active(c.anim_status) then
        local frame = c.anim_keyframe - row_start()
        -- Without a row for the pin the frame is outside the table: the shape stays on Harry.
        if frame < 0 or frame >= 8 then
            c.box_offset_x = 0
            c.box_offset_z = 0
            c.cylinder_offset_x = 0
            c.cylinder_offset_z = 0
        else
            local offset = -table_at(PIN_AT, frame)
            c.box_offset_x = 0
            c.box_offset_z = 0
            c.cylinder_offset_x = q12_mult(offset, sh.math.sin(c.rot_y))
            c.cylinder_offset_z = q12_mult(offset, sh.math.cos(c.rot_y))
        end
    end

    local n = npc(idx)
    if math.abs(heading) < 128 then
        if state == STATE_PINNED_START_FRONT then
            c.rot_y = n.rot_y + 2048
        else
            c.rot_y = n.rot_y
        end
    elseif heading > 0 then
        c.rot_y = c.rot_y + 128
    else
        c.rot_y = c.rot_y - 128
    end

    heading = shortest_angle(c.rot_y, norm(n.rot_y))
    h.heading_offset = heading

    if c.anim_keyframe == row_end() then
        extra_state_set(next_state)
        c.player_afk_timer = 61440
    end

    if active(c.anim_status) then
        if state >= STATE_PINNED_START_FRONT and state < STATE_PINNED_FRONT then
            local start = sh.entity.map_clip_start(c.anim_status - 76)
            local time = start * 4096 + n.anim_time - 32768
            h.upper_anim_time = time
            c.anim_time = time
            c.anim_keyframe = asr(time, 12)
            h.upper_anim_keyframe = asr(h.upper_anim_time, 12)
        end
    end

    pull_towards_anchor()
end

-- The grabs, the pin and lying on the floor: the escape counts, the monster's hold on Harry's position
-- and facing, and breaking free by mashing the controls.
local function grabbed(state)
    local free_count, release, threshold, status = 0, STATE_NONE, 0, 0
    c.harry_move_speed = 0
    local distance = 0

    -- The floor (Cybil's shot) writes no heading below: the one kept from the throw stays.
    local heading = s16(h.heading_offset)

    if state == STATE_ON_FLOOR_FRONT or state == STATE_ON_FLOOR_BACK then
        free_count = sh.entity.grab_escape_count(0)
        if state == STATE_ON_FLOOR_FRONT then
            status = st(132, true)
            release = STATE_GET_UP_FRONT
        else
            status = st(133, false)
            release = STATE_GET_UP_BACK
        end

        c.cylinder_extent = c.cylinder_extent + asr(dt * 1105, 12)
        c.box_top = c.box_top + asr(dt * 4915, 12)
        c.box_offset_y = c.box_offset_y + asr(dt * 3686, 12)

        c.cylinder_extent = clamp(c.cylinder_extent, 942, 2048)
        c.box_top = clamp(c.box_top, -6553, -1638)
        c.box_offset_y = clamp(c.box_offset_y, -4505, -819)

        if c.health <= 0 and c.player_afk_timer <= 0 then
            sh.entity.harry_player_anim_lock()
            sh.entity.harry_game_over()
            c.health = 409600
            c.harry_gas_weapon_timer = 0
            return true
        end
    elseif state == STATE_PINNED_FRONT or state == STATE_PINNED_BACK then
        threshold = 2662
        if state == STATE_PINNED_FRONT then
            free_count = sh.entity.grab_escape_count(1)
            status = st(128, false)
            release = STATE_RELEASE_PINNED_FRONT
        else
            free_count = sh.entity.grab_escape_count(2)
            status = st(129, false)
            release = STATE_RELEASE_PINNED_BACK
        end

        c.cylinder_radius = 0
        shape_offsets_clear()
        pull_towards_anchor()

        if c.health <= 0 and c.player_afk_timer <= 0 then
            sh.entity.harry_player_anim_lock()
            sh.entity.harry_game_over()
            c.health = 409600
            c.harry_gas_weapon_timer = 0
            return true
        end
    elseif state == STATE_GRAB_TORSO_FRONT then
        threshold = 4096
        free_count = sh.entity.grab_escape_count(3)
        status = st(115, false)
        release = STATE_RELEASE_UPPER_FRONT
    elseif state == STATE_GRAB_TORSO_BACK then
        threshold = 4096
        free_count = sh.entity.grab_escape_count(4)
        status = st(117, false)
        release = STATE_RELEASE_UPPER_BACK
    elseif state == STATE_GRAB_LEGS_FRONT then
        threshold = 3276
        free_count = sh.entity.grab_escape_count(5)
        status = st(117, true)
        release = STATE_RELEASE_LOWER_FRONT
    elseif state == STATE_GRAB_LEGS_BACK then
        threshold = 3276
        free_count = sh.entity.grab_escape_count(6)
        status = st(118, false)
        release = STATE_RELEASE_LOWER_BACK
    elseif state == STATE_GRAB_NECK_FRONT then
        threshold = 6144
        free_count = sh.entity.grab_escape_count(7)
        status = st(125, true)
        release = STATE_RELEASE_UPPER_FRONT
    elseif state == STATE_GRAB_NECK_BACK then
        threshold = 6144
        free_count = sh.entity.grab_escape_count(8)
        status = st(125, true)
        release = STATE_RELEASE_UPPER_BACK
    end

    -- Harry's place and facing against the monster holding him.
    if state == STATE_GRAB_TORSO_FRONT or state == STATE_GRAB_LEGS_FRONT or state == STATE_GRAB_NECK_FRONT
        or state == STATE_PINNED_FRONT then
        local n = npc(h.grabber_front)
        local dx = s32(c.pos_x - n.pos_x)
        local dz = s32(c.pos_z - n.pos_z)
        distance = s16(sh.math.sqrt(s32(sq(dx) + sq(dz))))
        heading = shortest_angle(c.rot_y, (n.rot_y + 2048) & 0xFFF)
        turn_towards(heading, n.rot_y + 2048)
    elseif state == STATE_GRAB_TORSO_BACK or state == STATE_GRAB_LEGS_BACK or state == STATE_GRAB_NECK_BACK
        or state == STATE_PINNED_BACK then
        local n = npc(h.grabber_back)
        local dx = s32(c.pos_x - n.pos_x)
        local dz = s32(c.pos_z - n.pos_z)
        distance = s16(sh.math.sqrt(s32(sq(dx) + sq(dz))))
        heading = shortest_angle(c.rot_y, norm(n.rot_y))
        turn_towards(heading, n.rot_y)
    end

    if state == STATE_PINNED_FRONT then
        heading = shortest_angle(c.rot_y, norm(npc(h.grabber_front).rot_y))
    end
    if state == STATE_PINNED_FRONT or state == STATE_PINNED_BACK then
        heading = shortest_angle(c.rot_y, norm(npc(h.grabber_back).rot_y))
    end

    h.heading_offset = heading
    overlay_start(status)

    -- A touch screen shows its struggle button while Harry can mash free (it has no buttons to mash otherwise).
    if c.health > 0 then
        sh.input.touch_prompt("struggle", "struggle")
    end

    -- Each press counts the same at any frame rate (a press is one 30 fps tick of the mash meter).
    if c.health > 0 and (sh.input.control("has_move") ~= 0 or sh.input.control("has_action") ~= 0) then
        h.grab_release_timer = h.grab_release_timer + 136
    end

    -- Not on the floor: dragged too far from the monster, Harry is free.
    if not (state >= STATE_ON_FLOOR_FRONT and state < STATE_GET_UP_FRONT) then
        if threshold < distance then
            h.grab_release_timer = free_count
            if state == STATE_PINNED_FRONT then
                npc(h.grabber_front).move_speed = 0
            end
            if state == STATE_PINNED_BACK then
                npc(h.grabber_back).move_speed = 0
            end
        end
    end

    if h.grab_release_timer >= free_count then
        collision_state_reset(0)
        extra_state_set(release)
        c.flags = c.flags | STUN_TIMER_RUNS
    end
    return false
end

local RELEASE_CLIPS = {
    [STATE_GET_UP_FRONT] = { 133, true }, [STATE_GET_UP_BACK] = { 134, false },
    [STATE_RELEASE_PINNED_FRONT] = { 129, true }, [STATE_RELEASE_PINNED_BACK] = { 130, false },
    [STATE_RELEASE_UPPER_FRONT] = { 120, false }, [STATE_RELEASE_UPPER_BACK] = { 122, false },
    [STATE_RELEASE_A] = { 120, true }, [STATE_RELEASE_B] = { 121, true },
    [STATE_RELEASE_LOWER_FRONT] = { 122, true }, [STATE_RELEASE_LOWER_BACK] = { 123, false },
    [STATE_DAMAGE_HEAD] = { 121, false }, [STATE_FALL_FORWARD] = { 22, false }, [STATE_FALL_BACKWARD] = { 23, false },
}

-- The fall (a drop, a ledge), the release from a grab, getting up and the head blow.
local function falling_and_release(state)
    if state ~= STATE_FALL_BACKWARD then
        if c.harry_move_speed ~= 0 then
            c.harry_move_speed = c.harry_move_speed - asr(ts(1638), 1)
            if c.harry_move_speed < 0 then
                c.harry_move_speed = 0
            end
        end
    elseif c.harry_move_speed ~= 0 then
        c.harry_move_speed = c.harry_move_speed - asr(ts(1638), 2)
        if c.harry_move_speed < 0 then
            c.harry_move_speed = 0
        end
    end

    local clip = RELEASE_CLIPS[state]
    local status = st(clip[1], clip[2])

    if not (state >= STATE_FALL_FORWARD and state < STATE_KICK_ENEMY) then
        overlay_start(status)
    end

    h.step_speed = c.harry_move_speed
    c.flags = c.flags | STUN_TIMER_RUNS

    if state == STATE_GET_UP_FRONT or state == STATE_GET_UP_BACK then
        c.damage_amount = 0
        c.player_afk_timer = 0
        if c.anim_keyframe == row_end() then
            flags_clear(DAMAGE_REPORTED)
            extra_state_set(STATE_NONE)
            c.cylinder_radius = 1228
            c.cylinder_extent = 942
            c.box_top = -6553
            shape_offsets_clear()
            c.box_bottom = 0
            c.box_offset_y = -4505
        end
        c.attack_received = -1
    elseif state == STATE_RELEASE_PINNED_FRONT or state == STATE_RELEASE_PINNED_BACK then
        c.player_afk_timer = 0
        shape_offsets_clear()
        if active(c.anim_status) then
            c.cylinder_radius = idiv((c.anim_keyframe - row_start()) * 1228, 21)
        else
            c.cylinder_radius = 0
        end
        if c.anim_keyframe == row_end() then
            flags_clear(DAMAGE_REPORTED)
            if state == STATE_RELEASE_PINNED_FRONT then
                extra_state_set(STATE_PINNED_RISE_FRONT)
            else
                extra_state_set(STATE_PINNED_RISE_BACK)
            end
            c.cylinder_radius = 1228
            shape_offsets_clear()
            c.attack_received = -1
        end
    elseif state == STATE_FALL_FORWARD or state == STATE_FALL_BACKWARD then
        if h.upper_state_step == 0 then
            h.upper_anim_status = status
            h.upper_state_step = h.upper_state_step + 1
        end
        if c.state_step == 0 then
            c.anim_status = status
            c.state_step = c.state_step + 1
        end

        -- Landing: the thud and the pad's rumble.
        if h.upper_control_state == 0 and c.pos_y >= c.player_ground_height then
            h.upper_control_state = h.upper_control_state + 1
            sfx(sound(c, "body_fall", 1317), 32, 0)
            c.harry_step_sound = 128
            sh.pad.effect_level(19, 0xA0)
        end

        local forward = state == STATE_FALL_FORWARD
        local base = forward and FALL_FORWARD_AT or FALL_BACKWARD_AT
        local first_keyframe = forward and 379 or 418
        c.harry_fall_direction = forward and 0 or 1
        if active(c.anim_status) then
            local row = c.anim_keyframe - first_keyframe
            c.box_top = table_at(base, row * 2)
            c.box_offset_y = table_at(base, row * 2 + 1)
        end

        local _, last = sh.entity.clip_info(0, forward and 45 or 47)
        if c.anim_keyframe == last then
            -- Down from a height of more than 6.5 m is fatal.
            if c.pos_y > 26624 then
                extra_state_set(STATE_DEATH)
            else
                extra_state_set(STATE_NONE)
            end
            stand_shape()
            c.cylinder_radius = 1228
        end
    else
        -- The releases from a grab and the head blow.
        local row = row_start()
        if c.anim_keyframe == row + 4 then
            c.attack_received = -1
        end
        if c.anim_keyframe == row_end() then
            flags_clear(DAMAGE_REPORTED)
            extra_state_set(STATE_NONE)
            c.cylinder_radius = 1228
            shape_offsets_clear()
        end
    end

    -- The stuck net of the release and get-up family (the falls set their clip directly and never stick).
    if state ~= STATE_FALL_FORWARD and state ~= STATE_FALL_BACKWARD then
        if reaction_stuck(h.state, c.anim_keyframe, h.upper_anim_keyframe) then
            h.target_npc = -1
            flags_clear(DAMAGE_REPORTED)
            extra_state_set(STATE_NONE)
            c.harry_move_speed = 0
            c.cylinder_radius = 1228
            shape_offsets_clear()
        end
    end

    c.attack_received = -1
end

-- Getting up after the pin (the two states at the end of the Romper's attack).
local function pinned_rise(status)
    overlay_start(status)
    if c.anim_keyframe == row_end() then
        extra_state_set(STATE_NONE)
        c.cylinder_extent = 942
    end
end

-- The dying clip: its keyframe sounds and the body's slide along the ground.
local function dying_keyframe_sounds(second)
    if active(c.anim_status) then
        local first = row_start()
        if first + 12 >= c.anim_keyframe then
            keyframe_sfx(c.anim_status, first + 12, 1318)
        else
            keyframe_sfx(c.anim_status, first + second, 1319)
        end
    end
end

-- Swallowed by the Cybil boss... eaten (the Split Head's other death is below).
local function eaten()
    c.attack_received = -1
    overlay_start(st(126, false))
    dying_keyframe_sounds(30)
    if c.anim_keyframe == row_end() then
        finish_dying(false, true)
        return true
    end
    return false
end

local function dying()
    c.attack_received = -1
    -- The step and control state carry over from the previous clip: reset them on the first tick so the
    -- death clip starts (the clip start waits for a control state of 0).
    if c.control_state ~= 0 and c.state_step ~= 2 then
        c.control_state = 0
        c.state_step = 0
        h.upper_control_state = 0
        h.upper_state_step = 0
    end
    overlay_start(st(101, false))

    -- The first tick of the clip: start from its first keyframe (the one left over may be near its end).
    if c.state_step == 1 and h.upper_state_step == 1 then
        local first = row_start()
        c.anim_keyframe = first
        c.anim_time = first * 4096
        h.upper_anim_keyframe = first
        h.upper_anim_time = first * 4096
        sh.entity.harry_anim_flags_set(3)
        c.state_step = 2
        h.upper_state_step = 2
    end
    -- The clip runs on the upper body's time: the legs follow it.
    if c.state_step == 2 then
        sh.entity.harry_anim_flags_set(3)
        if h.upper_anim_keyframe > c.anim_keyframe then
            c.anim_keyframe = h.upper_anim_keyframe
            c.anim_time = h.upper_anim_time
        end
    end
    c.cylinder_extent = 0

    if active(c.anim_status) then
        dying_keyframe_sounds(32)
        local first = row_start()
        local at = c.anim_keyframe - first
        local a = table_at(DEATH_AT, at)
        local b
        if c.anim_keyframe ~= row_end() then
            b = table_at(DEATH_AT, at + 1)
        else
            b = a
        end
        local slide = a + q12_mult(b - a, c.anim_time & 0xFFF)
        c.box_offset_x = 0
        c.box_offset_z = 0
        c.cylinder_offset_x = q12_mult(slide, sh.math.sin(c.rot_y))
        c.cylinder_offset_z = q12_mult(slide, sh.math.cos(c.rot_y))
        c.cylinder_radius = 1228
    end

    if c.anim_keyframe == row_end() then
        finish_dying(h.map_index == MAP_PROLOGUE, true)
        return true
    end
    return false
end

-- The Split Head eats Harry: the one way out is Game Over at its death clip's end.
local function instant_death()
    if h.upper_control_state == 0 then
        sh.sound.sd_call("harry_death_scream_voice")
    end
    overlay_start(st(101, true))
    c.cylinder_extent = 0

    -- Reached or passed the keyframe 25 before the clip's last (a variable frame time steps over one).
    if h.upper_control_state == 1 and c.anim_keyframe >= row_end() - 25 and c.anim_keyframe <= row_end() then
        finish_dying(false, false)
        return true
    end
    return false
end

local DAMAGE_CLIPS = {
    [STATE_DAMAGE_TORSO_BACK] = { 105, true }, [STATE_DAMAGE_TORSO_FRONT] = { 105, false },
    [STATE_DAMAGE_TORSO_RIGHT] = { 106, false }, [STATE_DAMAGE_TORSO_LEFT] = { 106, true },
    [STATE_DAMAGE_FEET_FRONT] = { 107, true }, [STATE_DAMAGE_FEET_BACK] = { 107, false },
    [STATE_DAMAGE_PUSH_BACK] = { 123, true }, [STATE_DAMAGE_PUSH_FRONT] = { 124, false },
}

-- The blows that stagger Harry: torso, feet, the Split Head's push.
local function damaged(state)
    local clip = DAMAGE_CLIPS[state]
    overlay_start(st(clip[1], clip[2]))

    if state == STATE_DAMAGE_PUSH_BACK then
        local angle = shortest_angle(c.rot_y, c.harry_push_angle)
        if math.abs(angle) < 1024 then
            if math.abs(angle) < 64 then
                c.rot_y = c.harry_push_angle
            else
                c.rot_y = c.rot_y + (idiv(angle, math.abs(angle)) << 6)
            end
        end
    elseif state == STATE_DAMAGE_PUSH_FRONT then
        local angle = shortest_angle(c.rot_y, c.harry_push_angle)
        if math.abs(angle) >= 1024 then
            if math.abs(angle) >= 1984 then
                c.rot_y = norm(c.harry_push_angle + 2048)
            else
                c.rot_y = c.rot_y - (idiv(angle, math.abs(angle)) << 6)
            end
        end
    end

    if c.harry_move_speed ~= 0 then
        c.harry_move_speed = c.harry_move_speed - asr(ts(2048), 2)
        if c.harry_move_speed < 0 then
            c.harry_move_speed = 0
        end
    end

    local attack = c.attack_received
    if not pushing_blow(attack) then
        h.heading_offset = sh.math.atan2(c.damage_x, c.damage_z) - c.rot_y
        c.harry_move_speed = s32(s32(sq(c.damage_x) + sq(c.damage_z)) + sq(c.damage_y))
    end

    if h.upper_anim_keyframe == row_end() then
        c.attack_received = -1
        h.target_npc = -1
        flags_clear(DAMAGE_REPORTED)
        extra_state_set(STATE_NONE)
        c.harry_move_speed = 0
    end

    -- The stuck net: a map without the damage rows leaves the clip with no end keyframe.
    if reaction_stuck(h.state, c.anim_keyframe, h.upper_anim_keyframe) then
        c.attack_received = -1
        h.target_npc = -1
        flags_clear(DAMAGE_REPORTED)
        extra_state_set(STATE_NONE)
        c.harry_move_speed = 0
    end

    h.step_speed = c.harry_move_speed
    c.flags = c.flags | STUN_TIMER_RUNS
end

-- Player_AttackAnimSet: the kick and the stomp turn Harry to the target, open the hit window, move the
-- hit box with the clip and end the state with it.
local function attack_anim_set(attack, status)
    if h.upper_state_step == 0 then
        h.upper_anim_status = status - 1
        h.upper_state_step = h.upper_state_step + 1
    end
    if c.state_step == 0 then
        c.anim_status = status - 1
        c.state_step = c.state_step + 1
    end

    local target = npc(h.target_npc)
    local angle = (sh.math.atan2(s32(target.pos_x + target.box_offset_x - h.attack_x),
                                 s32(target.pos_z + target.box_offset_z - h.attack_z)) + 4096) & 0xFFF
    c.angle_to_target = angle
    local turn = shortest_angle(c.rot_y, angle)
    if math.abs(turn) >= 96 then
        if turn < 0 then
            c.rot_y = c.rot_y - 96
        else
            c.rot_y = c.rot_y + 96
        end
    end

    local first, last = sh.entity.clip_info(0, status)
    local _, window_start, window_length = sh.entity.weapon_attack_info(attack)
    if h.upper_anim_keyframe >= first + window_start and first + window_start + window_length >= h.upper_anim_keyframe then
        h.weapon_attack = attack
        if c.harry_flags & HIT_DONE == 0 then
            c.harry_attack_marker = 1
            flags_set(HIT_DONE)
        end
    end

    if status == st(24, true) and active(c.anim_status) then
        local reach = table_at(KICK_AT, c.anim_keyframe - 457)
        c.box_offset_x = q12_mult(reach, sh.math.cos(c.rot_y))
        c.box_offset_z = -q12_mult(reach, sh.math.sin(c.rot_y))
        c.cylinder_offset_x = 0
        c.cylinder_offset_z = 0
    end
    if status == st(25, true) and active(c.anim_status) then
        local reach = table_at(STOMP_AT, c.anim_keyframe - 485)
        c.box_offset_x = q12_mult(reach, sh.math.cos(c.rot_y))
        c.box_offset_z = -q12_mult(reach, sh.math.sin(c.rot_y))
        c.cylinder_offset_x = 0
        c.cylinder_offset_z = 0
    end

    if c.anim_status == status and c.anim_keyframe == last then
        flags_clear(HIT_DONE)
        extra_state_set(STATE_NONE)
        shape_offsets_clear()
        -- The packed attack after a kick is the equipped weapon's tap attack. The original adds twice the first
        -- weapon's item number (256) to it in a signed byte, which leaves it as it is; the engine's attack is no
        -- byte, so the sum would carry.
        h.weapon_attack = h.unequipped ~= 0 and -1 or h.equipped_weapon
        h.target_npc = -1
        h.aiming = 0
    end
end

-- One tick of every state that is not the idle or the legs' and the upper body's own. Returns 1 when the
-- tick ends here (game over): the turn and fall of the record are not applied.
function state_update(_)
    dt = sh.game.delta_time()
    local state = h.state
    local ended = false

    if state == STATE_THROWN_BY_BOLT then
        bolt_thrown()
    elseif state == STATE_THROWN_FRONT or state == STATE_THROWN_BACK then
        thrown(state)
    elseif state == STATE_PINNED_START_FRONT or state == STATE_PINNED_START_BACK then
        pinned_start(state)
    elseif GRAB_STATES[state] then
        ended = grabbed(state)
    elseif RELEASE_CLIPS[state] then
        falling_and_release(state)
    elseif state == STATE_PINNED_RISE_FRONT then
        pinned_rise(st(130, true))
    elseif state == STATE_PINNED_RISE_BACK then
        pinned_rise(st(131, false))
    elseif state == STATE_EATEN then
        ended = eaten()
    elseif state == STATE_DEATH then
        ended = dying()
    elseif state == STATE_INSTANT_DEATH then
        ended = instant_death()
    elseif DAMAGE_CLIPS[state] then
        damaged(state)
    elseif state == STATE_KICK_ENEMY then
        attack_anim_set(sh.entity.weapon_number("Kick"), st(24, true))
    elseif state == STATE_STOMP_ENEMY then
        attack_anim_set(sh.entity.weapon_number("Stomp"), st(25, true))
    end
    return ended and 1 or 0
end

-- The end of the tick: the turn, the heading and the fall applied to the record.
function logic_post(_)
    dt = sh.game.delta_time()
    -- The free-aim cameras face Harry along the camera: no auto turn on top.
    if h.alt_camera ~= 0 then
        h.turn_speed = 0
    end
    c.rot_y = (c.rot_y + asr(h.turn_speed, 4) + 4096) & 0xFFF
    c.heading_angle = (c.rot_y + h.heading_offset + 4096) & 0xFFF
    c.move_speed = h.step_speed
    c.fall_speed = c.fall_speed + sh.game.gravity_speed()

    -- The camera's chase follows how fast Harry turned this tick.
    if h.prev_rot_valid ~= 0 and dt ~= 0 then
        local turned = s16(c.rot_y - h.prev_rot_y)
        c.harry_rot_speed_y = idiv(s32(turned << 8), dt)
    else
        c.harry_rot_speed_y = 0
    end
    h.prev_rot_y = c.rot_y
    h.prev_rot_valid = 1

    sh.entity.harry_pose_matrix_update()
end
