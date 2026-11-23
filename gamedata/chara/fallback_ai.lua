-- Generic fallback behaviour: what a spawned character runs when its own AI cannot
-- work on the current map (a boss away from its arena, a story character, a type without a behaviour) or
-- when the spawn asks for it (spawn ... ai=fallback). It stands, turns to Harry when he comes near,
-- walks up to him and, if hostile, attacks; it takes damage and goes down when its health is gone.
--
-- The engine runs chara/common.lua, combat.lua, fallbacks.lua, this file, then the character's own
-- chara/<Name>/fallback_ai.lua when it has one, all in one Lua state per character type. That file tunes
-- the table `fallback` below, e.g. (mods/<mod>/chara/Ghost/fallback_ai.lua):
--
--     fallback.hostile = false
--     fallback.walk = { "float" }
--     fallback.speed = 0.6
--
-- Clips are looked up by their names in the character's "anims" (character.json): the first name of each
-- list the character has wins. Distances are metres, speed metres per second, health game points.

local KIND_ENEMY, KIND_BOSS = 0, 1

fallback = {
    hostile = nil,        -- nil: enemies and bosses attack, actors and props do not
    clips_of = nil,       -- another character whose clip names this one shares ("Groaner" for the Wormhead)
    idle = { "stand_idle", "idle", "idle_loop", "stand", "landed_idle", "stand_ready", "stand_watch", "hover",
             "float", "fight_hover", "hold_hover", "sway_loop_a" },
    walk = { "walk_forward", "walk", "chase_run", "run_forward", "run", "walk_alt", "approach" },
    attack = { "attack", "bite_attack", "stand_bite_attack", "grab_attack", "jump_attack", "stand_acid_attack",
               "slash", "strike", "sting" },
    death = { "death", "death_start", "death_front_start", "collapse", "die", "die_back" },
    clip_rate = 15.0,     -- keyframes per second of a clip whose speed its own AI sets every tick
    sight = 12.0,         -- Harry closer than this is noticed
    reach = 1.1,          -- attack (or stop) this close
    give_up = 25.0,       -- farther than this it goes back to standing
    speed = 1.2,
    turn = 0.6,           -- turns per second
    attack_every = 1.6,   -- seconds between swings
    attack_row = "GroanerJumpBite", -- the row of config/attacks.json its swing deals (a torso hit)
    attack_id = nil,      -- older mods: a weapon number whose hold row (number + 10) the swing deals
    health = 200.0,       -- when the character data has no "health_base" / "health"
    radius = 0.35,        -- collision cylinder
    height = 1.6,
}

local IDLE, CHASE, ATTACK, DEAD = 1, 2, 3, 4
-- The attacks that grab Harry (leg grab, Romper pin, nurse grab, neck grab): each needs its grab and release
-- clips on Harry's side. Without them the swing is the plain torso hit, which every hostile
-- spawn makes playable.
local attack_number = sh.entity.attack_number
local GRAB_ATTACKS = {
    [attack_number("StalkerLegGrab")] = true, [attack_number("RomperPin")] = true,
    [attack_number("PuppetNurseGrab")] = true, [attack_number("MonsterCybilNeckGrab")] = true,
}
local PLAIN_HIT = attack_number("GroanerJumpBite")

-- The type's file sets fallback.attack_row after this file ran, so the name is looked up on use (once per name).
local attack_rows = {}
local function swing_attack()
    if fallback.attack_id ~= nil then
        return weapon_attack(fallback.attack_id, 1)
    end
    local name = fallback.attack_row
    local number = attack_rows[name]
    if number == nil then
        number = attack_number(name)
        attack_rows[name] = number
    end
    return number
end
local COLLISION_NPC, COLLISION_OFF = 3, 0
local FLAG_HIT = 0x4

local slots = {}       -- per slot: chosen clips and timers

local function q12(x)
    if x < 0 then
        return math.ceil(x * 4096)
    end
    return math.floor(x * 4096)
end

-- True when the character's clip table plays clip n (a keyframe range of its own) at a speed of its own:
-- a variable-speed clip runs on a function only the character's own behaviour sets up.
local function playable(c, n)
    local ok, first = pcall(sh.entity.clip_start, c.slot, anim_status(n, true))
    if not ok or first < 0 then
        return false
    end
    for _, active in ipairs({ true, false }) do
        local ok_var, var = pcall(sh.entity.clip_variable, c.slot, anim_status(n, active))
        if not ok_var or var ~= 0 then
            return false
        end
    end
    local ok_end, last = pcall(sh.entity.clip_end, c.slot, anim_status(n, true))
    return ok_end and last > first
end

-- The first clip of `names` the character (or fallback.clips_of) has (its anim number), or nil.
local function clip_of(c, names)
    for _, who in ipairs({ c.chara_id, fallback.clips_of }) do
        for _, name in ipairs(names) do
            local n = sh.chara_anim(who, name, -1)
            if n ~= nil and n >= 0 and playable(c, n) then
                return n
            end
        end
    end
    return nil
end

-- Any clip the table plays: the stand-in when no name matches (an unnamed clip set).
local function first_clip(c)
    for n = 1, 40 do
        if playable(c, n) then
            return n
        end
    end
    return nil
end

local function play(c, clip)
    if clip ~= nil and c.anim_status >> 1 ~= clip then
        c.anim_status = anim_status(clip, false)
    end
end

local function hostile(c)
    if fallback.hostile ~= nil then
        return fallback.hostile
    end
    local kind = sh.entity.spawn_kind(c.slot)
    return kind == KIND_ENEMY or kind == KIND_BOSS
end

local function start(c)
    local s = {}
    slots[c.slot] = s
    sh.entity.clip_table_attach(c.slot)
    s.idle = clip_of(c, fallback.idle)
    s.walk = clip_of(c, fallback.walk) or s.idle
    s.idle = s.idle or s.walk or first_clip(c)
    s.walk = s.walk or s.idle
    s.attack = clip_of(c, fallback.attack)
    s.death = clip_of(c, fallback.death)
    s.cooldown = 0
    local hp = sh.chara_param(c.chara_id, "health_base", nil) or sh.chara_param(c.chara_id, "health", nil)
    c.health = q12(hp or fallback.health)
    c.move_speed = 0
    c.fall_speed = 0
    c.heading_angle = c.rot_y
    c.flags = c.flags | FLAG_HIT
    damage_clear(c)
    if hostile(c) then
        -- Harry's reactions to its blows may be another map's clips (a no-op where the map has them).
        pcall(sh.entity.harry_reactions_ensure, c.chara_id, 1)
    end
    sh.entity.set_collision_state(c.slot, COLLISION_NPC)
    c.control_state = IDLE
    c.state_step = 0
    local clip = s.idle or s.walk
    if clip ~= nil then
        anim_set(c, anim_status(clip, true), sh.entity.clip_start(c.slot, anim_status(clip, true)))
    end
end

local function take_damage(c, s)
    if c.damage_amount == 0 then
        return
    end
    c.health = math.max(c.health - c.damage_amount, 0)
    damage_clear(c)
    s.provoked = true
    if c.health == 0 and c.control_state ~= DEAD then
        sh.entity.enemy_defeated(c.slot)
        c.control_state = DEAD
        c.move_speed = 0
        sh.entity.set_collision_state(c.slot, COLLISION_OFF)
        play(c, s.death)
    end
end

-- The swing lands when Harry is within reach in front of it (60 degrees each side). It is written into Harry's
-- record the way the game's direct attacks are (the Larval Stalker's drain, the Unknown23's shock): Harry's own
-- receive_damage then picks the reaction and scales the damage by difficulty. A grab whose clips the map cannot
-- play is the plain torso hit instead.
local function strike(c, p, dist)
    local attack = swing_attack()
    if GRAB_ATTACKS[attack] and sh.entity.grab_playable(attack, 1) == 0 then
        attack = PLAIN_HIT
    end
    if dist > q12(fallback.reach + 0.3) or p.health <= 0 or p.attack_received ~= -1 then
        return
    end
    if abs(angle_signed(heading_to(c.pos_x, c.pos_z, p.pos_x, p.pos_z) - c.rot_y)) > 683 then
        return
    end
    p.attacker_index = npc_index(c.slot)
    p.attack_received = attack
    p.damage_amount = p.damage_amount
        + idiv(s32((sh.entity.weapon_damage(attack) << 12) * ((sh.rng.rand16() & 0x1F) + 0x55)), 100)
end

local function turn_toward(c, heading, dt)
    local step = q12_mul(dt, q12(fallback.turn))
    local d = angle_signed(heading - c.rot_y)
    if abs(d) <= step then
        c.rot_y = heading
    elseif d > 0 then
        c.rot_y = c.rot_y + step
    else
        c.rot_y = c.rot_y - step
    end
    c.heading_angle = c.rot_y
end

local function think(c, s, dt)
    if c.control_state == DEAD then
        return
    end
    local p = record(0)
    local dist = distance_2d(p.pos_x - c.pos_x, p.pos_z - c.pos_z)
    local noticed = s.provoked or dist < q12(fallback.sight)
    if dist > q12(fallback.give_up) then
        s.provoked = false
        noticed = false
    end
    s.cooldown = math.max(s.cooldown - dt, 0)
    if not noticed or p.health <= 0 then
        c.control_state = IDLE
        c.move_speed = 0
        play(c, s.idle)
        return
    end
    turn_toward(c, heading_to(c.pos_x, c.pos_z, p.pos_x, p.pos_z), dt)
    if dist > q12(fallback.reach) then
        c.control_state = CHASE
        c.move_speed = q12(fallback.speed)
        play(c, s.walk)
        return
    end
    c.move_speed = 0
    if not hostile(c) then
        c.control_state = IDLE
        play(c, s.idle)
        return
    end
    c.control_state = ATTACK
    if s.cooldown == 0 then
        s.cooldown = q12(fallback.attack_every)
        play(c, s.attack or s.idle)
        strike(c, p, dist)
    elseif s.attack ~= nil and c.anim_status >> 1 == s.attack and s.cooldown < q12(fallback.attack_every) // 3 then
        play(c, s.idle)
    end
end

local function shape(c)
    c.box_top = -q12(fallback.height)
    c.box_bottom = 0
    c.box_height = 0
    c.box_offset_y = 0
    c.box_offset_x = 0
    c.box_offset_z = 0
    c.cylinder_extent = 0
    c.cylinder_radius = q12(fallback.radius)
    c.cylinder_offset_x = 0
    c.cylinder_offset_z = 0
    sh.entity.shape_offsets_update(c.slot)
end

function update(slot)
    local c = record(slot)
    if c.control_state == 0 or slots[slot] == nil then
        start(c)
    end
    local s = slots[slot]
    local dt = sh.game.delta_time()
    if dt ~= 0 then
        take_damage(c, s)
        think(c, s, dt)
        if c.control_state ~= DEAD then
            c.fall_speed = c.fall_speed + sh.game.gravity_speed()
            sh.entity.move(slot, 0, 0)
        end
    end
    -- A clip whose speed the character's own AI sets each tick has none here: step it at clip_rate.
    if dt ~= 0 and c.anim_status & 1 == 1 then
        local ok, rate = pcall(sh.entity.clip_duration, c.slot, c.anim_status)
        if ok and rate == 0 then
            local ok_start, first = pcall(sh.entity.clip_start, c.slot, c.anim_status)
            local ok_end, last = pcall(sh.entity.clip_end, c.slot, c.anim_status)
            if ok_start and ok_end and last > first then
                local t = c.anim_time + q12_mul(dt, q12(fallback.clip_rate))
                if t >= (last + 1) * 4096 then
                    t = first * 4096
                end
                c.anim_time = t
                c.anim_keyframe = t >> 12
            end
        end
    end
    anim_play(c)
    shape(c)
end
