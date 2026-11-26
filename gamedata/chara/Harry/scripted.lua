-- Harry under script control: the player functions every map overlay of the original carries its own
-- copy of (freezing and handing back the control, the scenes' clip requests, the cutscene move, the
-- weapon put aside for a scene). The engine's entries (src/engine/entities/game/player_scene_host.cpp) call the
-- functions below by name; the copies of the maps differ only in the per-map lists below.
--
-- behavior.lua names this file after Harry/cutscene (-- uses: ... Harry/scripted); it shares the Lua
-- state of the other Harry modules: extra_state_set (Harry/state.lua) and cutscene_map_state
-- (Harry/cutscene.lua) are theirs.

local c = record(0)
local h = setmetatable({}, {
    __index = function(_, k)
        return sh.entity.harry(k)
    end,
    __newindex = function(_, k, v)
        sh.entity.set_harry(k, v)
    end,
})

local function state(name, fallback)
    return sh.chara_state("Harry", name, fallback)
end
local NONE = state("none", 0)
local RESET = state("reset", 52)
local WALK_FORWARD, RUN_FORWARD = state("walk_forward", 53), state("run_forward", 54)
local TURN_RIGHT, TURN_LEFT = state("turn_right", 56), state("turn_left", 57)
local CUTSCENE_START = state("cutscene_start", 58)
local COURTYARD_INTRO = state("scene_courtyard_intro", 74)
local HATCH_CLIMB_UP, SCENE_POSE_C = state("hatch_climb_up", 87), state("scene_pose_c", 89)
local LIFT_PLATFORM = state("scene_lift_platform", 106)
local LOCKER_ALT = state("scene_locker_alt", 84)
local FALL_BACKWARD = state("fall_backward", 4)
local DAMAGE_FEET_FRONT = state("damage_feet_front", 27)

local IDLE = sh.chara_anim("Harry", "idle", 26)
local HANDGUN = sh.entity.weapon_number("Handgun")

-- The cutscene's move request (cutscene.lua's MOVE_TO) a scene's state stands for.
local MOVE_OF = { [RUN_FORWARD] = 0, [WALK_FORWARD] = 1, [TURN_RIGHT] = 3, [TURN_LEFT] = 4 }

local function maps(list)
    local set = {}
    for _, name in ipairs(list) do
        set[name] = true
    end
    return set
end

-- Per-map differences of the original copies.
-- Maps whose scenes keep the ground and walls for these states too (when the map has the state).
local GROUND_FOLLOW_MAPS = maps({ "map1_s01", "map1_s02", "map1_s03", "map1_s06", "map2_s00", "map3_s01",
                                  "map3_s03", "map3_s04", "map3_s05", "map4_s03", "map4_s05", "map5_s00",
                                  "map5_s02", "map5_s03", "map7_s01", "map7_s02", "map7_s03" })
local GROUND_FOLLOW_STATES = maps({ "scene_twinfeeler_2", "scene_twinfeeler_3", "scene_cybil_stairs",
                                    "ending_watch_hold", "pickup_crouch", "pickup_stand_up", "burn_vines" })
-- The courtyard intro keeps the ground only in the school (map index 5).
local SCHOOL_MAP_INDEX = 5
-- Maps whose walls report into the overlay's own result block (the others into the shared one).
local OWN_WALL_BLOCK = maps({ "map0_s00", "map2_s01", "map3_s00", "map3_s06", "map5_s02", "map5_s03",
                              "map6_s01", "map6_s04", "map7_s02", "map7_s03" })
-- The prologue maps: the held item stays hidden when the control comes back.
local HELD_ITEM_STAYS_HIDDEN = maps({ "map0_s00", "map0_s01" })
-- The first map keeps the upper body's lean when the control comes back.
local FLEX_KEPT = maps({ "map0_s00" })
-- Map-only scene functions.
local FALL_BACKWARD_MAPS = maps({ "map0_s01" })
local DAMAGE_FEET_FRONT_MAPS = maps({ "map6_s04" })
local SCENE_WEAPON_HIDE_MAPS = maps({ "map0_s01" })

-- PlayerFlag bits (harry_flags) and record flags.
local SPOT_TURN_FLAGS = 0x30           -- the turn-on-the-spot bits a frozen Harry starts with
local UNFREEZE_CLEARED = 0x4 | 0x2000 | 0x4000 | 0x8000
local CHARA_FROZEN = 0x8               -- CharaFlag: the record is held by a scene
local ANIM_UNLOCKED = 0x1

-- Q12 constants.
local CYLINDER_EXTENT = 942            -- 0.23
local BOX_TOP = -6553                  -- -1.6
local BOX_OFFSET_Y = -4505             -- -1.1
local FALL_BACKWARD_SPEED = 9420       -- 2.3
local HALF_TURN = 2048
local IDLE_KEYFRAME = 503
local BODY_FALL_VOLUME = 32            -- Q8 1/8

local ACTIONS = { "shoot", "attack", "hold_attack", "aim", "run", "back", "forward", "step_right_tap",
                  "step_right", "turn_right", "step_left_tap", "step_left", "turn_left" }

local function actions_clear()
    for _, name in ipairs(ACTIONS) do
        sh.input.set_player_flag(name, 0)
    end
end

-- C's % (the remainder keeps the dividend's sign).
local function crem(a, b)
    return a - idiv(a, b) * b
end

-- The cutscene move: Harry goes this tick's speed along his heading. In the scene states that keep the
-- ground, the speed shrinks on slopes and the walls and floor stop him; in the others he slides flat.
-- `turn` is the turn of the tick (cutscene_update's result): the turn speed follows from it.
function cutscene_move(turn)
    local map = sh.map_name()
    local st = h.state
    local follow = st < CUTSCENE_START
    if not follow then
        if map == "map0_s00" then
            follow = st == COURTYARD_INTRO and h.map_index == SCHOOL_MAP_INDEX
        elseif GROUND_FOLLOW_MAPS[map] then
            local entry = cutscene_map_state(st)
            local name = entry ~= nil and entry.name or nil
            if name ~= nil and GROUND_FOLLOW_STATES[name] then
                follow = true
            elseif name == "scene_courtyard_intro" then
                follow = h.map_index == SCHOOL_MAP_INDEX
            end
        end
    end
    if map == "map0_s01" and st == WALK_FORWARD then
        follow = false
    end

    local vx, vz
    if follow then
        local tilt_x, tilt_z = sh.entity.harry_ground_tilt(c.pos_x, c.pos_z)
        local sx = q12_mul32(c.move_speed, sh.math.sin(c.heading_angle))
        local sz = q12_mul32(c.move_speed, sh.math.cos(c.heading_angle))
        local slope_x = s16(sh.math.cos(abs(tilt_x) >> 3))
        local slope_z = s16(sh.math.cos(abs(tilt_z) >> 3))
        vx = s16(q12_mul32(q12_mul32(sx, slope_x), slope_x))
        vz = s16(q12_mul32(q12_mul32(sz, slope_z), slope_z))
    else
        vx = s16(q12_mul32(c.move_speed, sh.math.sin(c.heading_angle)))
        vz = s16(q12_mul32(c.move_speed, sh.math.cos(c.heading_angle)))
    end
    local length = sh.math.sqrt(s32(vx * vx + vz * vz))
    if c.move_speed >= 0 then
        c.move_speed = length
    else
        c.move_speed = s32(-length)
    end

    local dt = sh.game.delta_time()
    local dx, dz = step_along(q12_mul(c.move_speed, dt), c.heading_angle)
    local dy = q12_mul(c.fall_speed, dt)

    if follow then
        local own = OWN_WALL_BLOCK[map] and 1 or 0
        local ox, oy, oz, floor, ground = sh.entity.harry_wall_detect(dx, dy, dz, own)
        c.pos_x = s32(c.pos_x + ox)
        c.pos_y = s32(c.pos_y + oy)
        c.pos_z = s32(c.pos_z + oz)
        if ground == 0 then
            floor = c.player_ground_height
            sh.entity.harry_wall_floor_set(own, floor)
        end
        if c.pos_y > floor then
            c.pos_y = floor
            c.fall_speed = 0
        end
    else
        c.pos_x = s32(c.pos_x + dx)
        c.pos_z = s32(c.pos_z + dz)
        if st < HATCH_CLIMB_UP or (st >= SCENE_POSE_C and st ~= LIFT_PLATFORM) then
            c.pos_y = 0
        end
        c.fall_speed = 0
    end

    if dt == 0 then
        c.harry_rot_speed_y = 0
    else
        c.harry_rot_speed_y = s16(idiv(s32(turn << 8), dt))
    end
    sh.entity.harry_cutscene_root_set(c.pos_x, c.pos_y, c.pos_z)
    return 0
end

-- The player loses control of Harry: he stands, the actions of the pad are dropped, the guns are
-- lowered to their attack number without the input kind.
function control_freeze(_)
    sh.entity.harry_set_weapon_stash(-1)
    h.cutscene_move = 0
    h.path_step = 0
    h.control_disabled = 1
    h.walk_run_blend = 0
    h.stick_hold = 0
    c.player_afk_timer = 0
    c.harry_run_steps = 0
    c.harry_exhaustion = 0
    extra_state_set(RESET)
    actions_clear()
    c.cylinder_extent = 0
    c.harry_flags = c.harry_flags | SPOT_TURN_FLAGS
    c.flags = c.flags | CHARA_FROZEN
    sh.entity.set_collision_state(0, 4)
    local attack = h.weapon_attack
    if not gun_attack(attack, HANDGUN) then
        h.weapon_attack = attack_weapon(attack)
    end
    return 0
end

-- The player gets Harry back (`set_idle` 1: straight into the idle pose, else from no state).
function control_unfreeze(set_idle)
    h.control_disabled = 0
    if set_idle ~= 0 then
        h.state = NONE
        h.upper_body = 0
        h.lower_body = 0
        local status = anim_status(IDLE, true)
        c.anim_status = status
        c.anim_keyframe = IDLE_KEYFRAME
        h.upper_anim_status = status
        h.upper_anim_keyframe = IDLE_KEYFRAME
        c.anim_time = IDLE_KEYFRAME * 4096
        c.anim_alpha = 4096
        h.upper_anim_time = IDLE_KEYFRAME * 4096
        h.upper_anim_alpha = 4096
    else
        extra_state_set(NONE)
    end
    c.attack_received = -1
    c.harry_attacker = -1
    h.grabber_back = -1
    h.grabber_front = -1
    if not HELD_ITEM_STAYS_HIDDEN[sh.map_name()] then
        sh.entity.harry_held_item_show()
    end
    actions_clear()
    h.aiming = 0
    c.cylinder_extent = CYLINDER_EXTENT
    c.box_top = BOX_TOP
    c.box_offset_y = BOX_OFFSET_Y
    c.box_bottom = 0
    c.cylinder_offset_z = 0
    c.cylinder_offset_x = 0
    c.box_offset_z = 0
    c.box_offset_x = 0
    c.harry_flags = c.harry_flags & ~UNFREEZE_CLEARED
    c.flags = c.flags & ~CHARA_FROZEN
    sh.entity.set_collision_state(0, 3)
    sh.entity.harry_anim_flags_set(ANIM_UNLOCKED)
    h.prev_x = c.pos_x
    h.prev_z = c.pos_z
    if not FLEX_KEPT[sh.map_name()] then
        h.flex_rot_y = 0
        h.flex_x = 0
    end
    return 0
end

-- A scene asks Harry for one of his states (a walk, a run, a turn, a clip of the map).
function anim_state_set(target)
    c.harry_move_speed = 0
    h.cutscene_stop = 0
    local move = MOVE_OF[target]
    if move ~= nil then
        h.cutscene_move = move
    end
    extra_state_set(target)
    return 0
end

-- A scene asks the current state to end (cutscene.lua's stop request; counted, a byte).
function anim_reset(_)
    h.cutscene_stop = (h.cutscene_stop + 1) & 0xFF
    return 0
end

-- Harry puts the held weapon away and freezes in the scene's locker clip.
function cutscene_weapon_unequip(_)
    h.weapon_attack = -1
    h.unequipped = 1
    control_freeze(0)
    anim_state_set(LOCKER_ALT)
    return 0
end

function match_arm_anim_disable(_)
    h.match_anim = 0
    return 0
end

function anim_lock(_)
    c.anim_flags = c.anim_flags & ~ANIM_UNLOCKED
    h.upper_anim_flags = h.upper_anim_flags & ~ANIM_UNLOCKED
    return 0
end

-- Not 0 while the clips are locked; the original's value (-1 locked, -2 unlocked) is kept.
function anim_is_locked(_)
    return ~(c.anim_flags & ANIM_UNLOCKED)
end

function anim_unlock(_)
    sh.entity.harry_anim_flags_set(ANIM_UNLOCKED)
    return 0
end

function move_speed_is_zero(_)
    return c.harry_move_speed == 0 and 1 or 0
end

function move_speed_clear(_)
    c.harry_move_speed = 0
    return 0
end

-- 0 Harry fell forward, 1 backward.
function fall_direction(_)
    return c.harry_fall_direction
end

-- The held weapon is put aside (empty hand) and comes back with weapon_restore.
function weapon_stash(_)
    local attack = h.weapon_attack
    h.weapon_attack = -1
    sh.entity.harry_set_weapon_stash(attack & 0xFF)
    sh.entity.harry_held_item_attach(1, 1)
    return 0
end

function weapon_restore(_)
    h.weapon_attack = sh.entity.harry_weapon_stash()
    return 0
end

-- The bar scene's handgun: the held model becomes the handgun, then it is hidden.
function scene_weapon_hide(_)
    if SCENE_WEAPON_HIDE_MAPS[sh.map_name()] then
        h.weapon_attack = HANDGUN
        sh.entity.harry_held_item_previous()
        h.weapon_attack = -1
        sh.entity.harry_held_item_hide()
    end
    return 0
end

-- The cafe: Harry is knocked back onto the floor.
function fall_backward(_)
    if FALL_BACKWARD_MAPS[sh.map_name()] then
        c.harry_move_speed = FALL_BACKWARD_SPEED
        c.player_heading_angle = HALF_TURN
        h.heading_offset = HALF_TURN
        extra_state_set(FALL_BACKWARD)
    end
    return 0
end

-- The floor gives way under Harry's feet.
function damage_feet_front(_)
    if DAMAGE_FEET_FRONT_MAPS[sh.map_name()] then
        sh.entity.sfx_3d(0, sound(c, "body_fall", 1317), BODY_FALL_VOLUME, 0)
        extra_state_set(DAMAGE_FEET_FRONT)
    end
    return 0
end

-- The scripted walk along points (an event's waypoints): Harry turns to the first point, walks or runs
-- `state` through them while his upper body leans into the next one, then turns to the end heading.
-- Called once per tick by the event until it answers 1; h.path_step is the step (0 starts a walk). The original had
-- it only in the overlays of the maps whose events walk Harry; here any map's event (a mod's too) may.
local FACING_CLOSE = 64             -- 5.625 degrees: close enough to snap
local LEAN_LIMIT = 512              -- 45 degrees
local SLOW_TURN, FAST_TURN = 32, 256 -- 2.9 and 22.5 degrees per 30 fps tick
local AT_POINT = 38                 -- Q8 0.15: the first point is already reached
local POINT_REACH = 614             -- Q12 0.15: reach per state number past the walk (walk 0.15 m, run 0.3 m)

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

local function point(i)
    return sh.entity.harry_path_point(i)
end

-- Direction from Harry to point `i` (raw: -2048..2048).
local function toward(i)
    local x, _, z = point(i)
    return sh.math.atan2(s32(x - c.pos_x), s32(z - c.pos_z))
end

-- Distance (Q8 squares, as the original) from Harry to point `i`.
local function distance_to(i)
    local x, _, z = point(i)
    local dx = asr(s32(c.pos_x - x), 4)
    local dz = asr(s32(c.pos_z - z), 4)
    return sh.math.sqrt(s32(dx * dx + dz * dz))
end

-- The upper body leans toward `angle`, at most 45 degrees.
local function lean_toward(angle)
    local a = shortest_angle(c.rot_y, angle)
    if a < -LEAN_LIMIT then
        a = -LEAN_LIMIT
    elseif a > LEAN_LIMIT then
        a = LEAN_LIMIT
    end
    h.flex_rot_y = a
end

-- Turn on the spot toward `angle` (the turn states), the step after it waits for the facing.
local function turn_start(delta)
    if delta < 0 then
        h.cutscene_move = 4
        extra_state_set(TURN_LEFT)
    else
        h.cutscene_move = 3
        extra_state_set(TURN_RIGHT)
    end
end

local point_index, facing, next_facing, last_distance = 0, 0, 0, 0

function path_walk(state, count)
    local final = angle_abs(sh.entity.harry_path_angle())
    h.flex_rot_y = 0
    local step = h.path_step
    if step == 0 then
        point_index = 0
        h.path_step = 1
    elseif step == 1 then
        local x, _, z = point(0)
        if x == c.pos_x and z == c.pos_z then
            h.path_step = 6
            return 0
        end
        facing = s16(angle_abs(toward(0)))
        local delta = shortest_angle(c.rot_y, facing)
        if abs(delta) < FACING_CLOSE then
            c.rot_y = facing
            h.path_step = 3
            return 0
        end
        turn_start(delta)
        h.path_step = 2
    elseif step == 2 then
        if abs(shortest_angle(c.rot_y, facing)) < FACING_CLOSE then
            c.rot_y = facing
            h.path_step = 3
        end
    elseif step == 3 then
        if abs(distance_to(0)) < AT_POINT then
            extra_state_set(RESET)
            h.path_step = 8
            return 0
        end
        extra_state_set(state)
        if state == RUN_FORWARD then
            h.cutscene_move = 0
        elseif state == WALK_FORWARD then
            h.cutscene_move = 1
        end
        h.path_step = 4
    elseif step == 4 then
        facing = s16(angle_abs(toward(point_index)))
        if point_index + 1 < count then
            next_facing = s16(angle_abs(toward(point_index + 1)))
        else
            next_facing = facing
        end
        last_distance = distance_to(point_index)
        h.path_step = 5
        lean_toward(next_facing)
    elseif step == 5 then
        if point_index + 1 < count then
            next_facing = s16(toward(point_index + 1))
        else
            next_facing = s16(toward(point_index))
        end
        next_facing = s16(angle_abs(next_facing))
        local delta = shortest_angle(c.rot_y, next_facing)
        local turn = idiv((delta < LEAN_LIMIT and SLOW_TURN or FAST_TURN) * sh.game.delta_time(), 136)
        if abs(delta) < FACING_CLOSE then
            c.rot_y = next_facing
        elseif delta < 0 then
            c.rot_y = c.rot_y - turn
        else
            c.rot_y = c.rot_y + turn
        end
        c.rot_y = angle_abs(c.rot_y)
        local distance = distance_to(point_index)
        -- The reach grows with the state number (the run's is twice the walk's).
        if distance < asr(s32((state - WALK_FORWARD) * POINT_REACH + POINT_REACH), 4)
            or (last_distance < distance and point_index ~= count - 1) then
            point_index = s16(point_index + 1)
            distance = distance_to(point_index)
            h.path_step = point_index == count and 6 or 4
        end
        last_distance = distance
        lean_toward(next_facing)
    elseif step == 6 then
        local delta = shortest_angle(c.rot_y, final)
        if abs(delta) < FACING_CLOSE then
            c.rot_y = final
            h.path_step = 8
            return 0
        end
        turn_start(delta)
        h.path_step = 7
    elseif step == 7 then
        if abs(shortest_angle(c.rot_y, final)) < FACING_CLOSE then
            c.rot_y = final
            extra_state_set(RESET)
            h.cutscene_move = 0
            h.path_step = 8
        end
    elseif step == 8 then
        h.cutscene_stop = 1
        facing = 0
        h.path_step = 0
        c.harry_move_speed = 0
        return 1
    end
    return 0
end

-- The grab of a pinning monster (the Romper): Harry stops and stands at the hold, the monster's pin
-- point follows Harry where the walls let him be. In front of the monster he is held 0.22 m left and
-- 0.24 m back of the pin, behind it 0.09 m and 0.42 m. In the original each map with Rompers carries this
-- update and only a Romper calls it, so it runs on any map: a Romper spawned off its maps holds Harry the same.
local PIN_FRONT = { 0xCB, -0x3C6 }
local PIN_BEHIND = { 0x16C, 0x6B0 }

-- The pin's offset (side, back) turned by `angle`, Q12 (a C >> 12 of the 32-bit sums).
local function pin_offset(angle, side, back)
    local cos, sin = sh.math.cos(angle), sh.math.sin(angle)
    return asr(s32(cos * side + sin * back), 12), asr(s32(-sin * side + cos * back), 12)
end

function pin_update(_)
    local pin_x, pin_z, angle = sh.entity.harry_pin_get()
    c.harry_move_speed = 0
    c.heading_angle = 0
    local d = angle_abs(angle - c.rot_y)
    local in_front = d < 1024 or d >= 3072
    local ox, oz
    if not in_front then
        ox, oz = pin_offset(angle, -PIN_BEHIND[1], -PIN_BEHIND[2])
    else
        ox, oz = pin_offset(angle, -PIN_FRONT[1], -PIN_FRONT[2])
    end
    local hold_x, hold_z = s32(pin_x + ox), s32(pin_z + oz)
    local dx, _, dz = sh.entity.harry_wall_detect(s32(hold_x - c.pos_x), q12_mul(c.fall_speed, sh.game.delta_time()),
                                                  s32(hold_z - c.pos_z), 0)
    hold_x, hold_z = s32(c.pos_x + dx), s32(c.pos_z + dz)
    h.pin_target_x = hold_x
    h.pin_target_z = hold_z
    if not in_front then
        ox, oz = pin_offset(s16(angle + HALF_TURN), PIN_BEHIND[1], PIN_BEHIND[2])
    else
        ox, oz = pin_offset(angle, PIN_FRONT[1], PIN_FRONT[2])
    end
    sh.entity.harry_pin_set(s32(hold_x + ox), s32(hold_z + oz))
    return 0
end

-- The Split Head's shove (map1_s05): Harry is pushed by (x, z) this tick and hurt by the strength of
-- the shove; a strong one knocks him back or turns him, the gathered shove decides which.
local PUSH_MAPS = maps({ "map1_s05" })
local PUSH_HURTS = 3072           -- Q12 0.75 (squared strength)
local PUSH_KNOCKS = 7168          -- Q12 1.75
local PUSH_VOLUME = 32            -- Q8 1/8
local DAMAGE_TORSO_BACK, DAMAGE_TORSO_FRONT = state("damage_torso_back", 23), state("damage_torso_front", 24)
local DAMAGE_TORSO_RIGHT, DAMAGE_TORSO_LEFT = state("damage_torso_right", 25), state("damage_torso_left", 26)
local DAMAGE_PUSH_BACK, DAMAGE_PUSH_FRONT = state("damage_push_back", 29), state("damage_push_front", 30)
local DAMAGE_PUSH_ALT = state("damage_push_alt", 31)
local FLAG_HIT_REACTION = 0x1000  -- harry_flags bits the shove clears (12 and 9) and sets (14)
local FLAG_AIM_HOLD = 0x200
local FLAG_DAMAGE_RECEIVED = 0x4000
local FLAG_REGEN = 0x4

function push_update(x, z)
    -- The boss map pushes for its boss (NPC 1); off it, an engine-spawned Split Head pushes for itself.
    local pusher = sh.entity.harry_pusher()
    if pusher == 0 and not PUSH_MAPS[sh.map_name()] then
        return 0
    end
    local boss = record(pusher ~= 0 and pusher or 1)
    local vx, vz = sh.entity.harry_push_vector()
    if vx == 0 and vz == 0 then
        if sh.entity.harry_push_total() ~= 0 then
            sh.entity.harry_set_push_total(0)
        end
    else
        c.harry_push_angle = angle_abs(sh.math.atan2(vx, vz))
    end
    if x == 0 and z == 0 then
        return 0
    end
    h.push_x = x
    h.push_z = z
    if boss.health <= 0 or h.walk_run_blend ~= 0 or c.health <= 0 then
        return 0
    end
    local strength2 = s32(vx * vx + vz * vz)
    local dt = sh.game.delta_time()
    local total = s32(sh.entity.harry_push_total() + idiv(s32(sh.math.sqrt(strength2) * dt), 136))
    sh.entity.harry_set_push_total(total)
    if strength2 > PUSH_HURTS then
        local st = h.state
        if st < DAMAGE_PUSH_BACK or st >= DAMAGE_PUSH_ALT then
            local angle = angle_abs(angle_abs(sh.math.atan2(vx, vz)) - (c.rot_y & 0xFFFF))
            if total > PUSH_KNOCKS then
                if st >= DAMAGE_TORSO_BACK and st < DAMAGE_FEET_FRONT then
                    c.state_step = 0
                    c.control_state = 0
                    h.upper_state_step = 0
                    h.upper_control_state = 0
                end
                extra_state_set((angle >= 1024 and angle < 3072) and DAMAGE_PUSH_FRONT or DAMAGE_PUSH_BACK)
                sh.entity.sfx_3d(0, sound(c, "damage", 1326), PUSH_VOLUME, 0)
            elseif (st < DAMAGE_TORSO_BACK or st >= DAMAGE_FEET_FRONT) and st ~= DAMAGE_PUSH_BACK
                and st ~= DAMAGE_PUSH_FRONT then
                local reaction
                if angle >= 512 and angle < 1536 then
                    reaction = DAMAGE_TORSO_LEFT
                elseif angle >= 1536 and angle < 2560 then
                    reaction = DAMAGE_TORSO_FRONT
                elseif angle >= 2560 and angle < 3584 then
                    reaction = DAMAGE_TORSO_RIGHT
                else
                    reaction = DAMAGE_TORSO_BACK
                end
                extra_state_set(reaction)
            end
            c.harry_flags = c.harry_flags & ~FLAG_HIT_REACTION
            c.player_afk_timer = 0
            c.player_flex_angle_x = 0
            h.aiming = 0
            c.harry_attack_marker = -1
            c.harry_flags = c.harry_flags & ~FLAG_AIM_HOLD
        end
    end
    local root = sh.math.sqrt(strength2)
    local tick_damage = s32(strength2 > PUSH_KNOCKS and root * 4 or root * 64)
    c.damage_amount = idiv(s32(tick_damage * dt), 136)
    if c.harry_flags & FLAG_DAMAGE_RECEIVED == 0 and strength2 > PUSH_HURTS and strength2 <= PUSH_KNOCKS then
        sh.entity.sfx_3d(0, sound(c, "damage_type_2", 1327), PUSH_VOLUME, 0)
        c.harry_step_sound = 64
        c.harry_flags = c.harry_flags | FLAG_DAMAGE_RECEIVED
    end
    if c.damage_amount ~= 0 then
        c.harry_flags = c.harry_flags & ~FLAG_REGEN
        c.health = s32(c.health - c.damage_amount)
        sh.entity.harry_damage_rumble(tick_damage)
        c.damage_amount = 0
    end
    return 0
end

-- The padlock of the alley gate (map2_s00, NPC slot 5): only near the gate, while flag M2S00_WornPadlockInspected is set and
-- M2S00_PadlockBroken is not, a held blade or gun makes it a target; breaking it sets flag M2S00_PadlockBroken. Answers 1 near the gate.
local PADLOCK_MAPS = maps({ "map2_s00" })
local PADLOCK_SLOT = 6           -- record of NPC 5
local PADLOCK_X, PADLOCK_Y, PADLOCK_Z = -1073152, -4505, -425984   -- -262, -1.1, -104 m
local GATE_WEST, GATE_SOUTH, GATE_NORTH = -1044480, -450560, -409600  -- -255, -110, -100 m
local PADLOCK_REACH = 8192       -- 2 m
local PADLOCK_HEALTH = 1638400   -- 400
local FLAG_PADLOCK_BROKEN, FLAG_HIT = 0x1, 0x4
local ANIM_VISIBLE = 0x2
local KITCHEN_KNIFE = sh.entity.weapon_number("KitchenKnife")

local function padlock_broken_check(lock)
    if sh.flag_get("M2S00_WornPadlockInspected") and lock.chara_id == sh.chara_id("Padlock") then
        lock.flags = lock.flags | FLAG_PADLOCK_BROKEN
        sh.flag_set("M2S00_PadlockBroken", true)
    end
end

function padlock_update(_)
    if not PADLOCK_MAPS[sh.map_name()] then
        return 0
    end
    local lock = record(PADLOCK_SLOT)
    if c.pos_x < GATE_WEST and c.pos_z > GATE_SOUTH and c.pos_z < GATE_NORTH then
        local dx, dz = s32(PADLOCK_X - c.pos_x), s32(PADLOCK_Z - c.pos_z)
        local distance = sh.math.sqrt(s32(dx * dx + dz * dz))
        if lock.control_state == 0 or lock.state_step == 0 then
            lock.health = PADLOCK_HEALTH
            lock.control_state = lock.control_state + 1
            lock.box_top = -819
            lock.box_bottom = 819
            lock.cylinder_radius = 204
            lock.cylinder_extent = 2048
            lock.attacker_index = 0
            lock.box_offset_y = 0
            lock.box_outer_extent = 0
            lock.cylinder_offset_z = 0
            lock.cylinder_offset_x = 0
            lock.box_offset_z = 0
            lock.box_offset_x = 0
            lock.pos_x = PADLOCK_X
            lock.pos_y = PADLOCK_Y
            lock.pos_z = PADLOCK_Z
            sh.entity.set_collision_state(PADLOCK_SLOT, 3)
            lock.state_step = lock.state_step + 1
            lock.flags = lock.flags | FLAG_HIT
            lock.anim_flags = lock.anim_flags & ~(ANIM_VISIBLE | ANIM_UNLOCKED)
        end
        if lock.health > 0 then
            if distance < PADLOCK_REACH and sh.flag_get("M2S00_WornPadlockInspected") and not sh.flag_get("M2S00_PadlockBroken")
                and h.weapon_attack >= KITCHEN_KNIFE then
                lock.chara_id = sh.chara_id("Padlock")
            else
                if lock.health <= 0 then
                    padlock_broken_check(lock)
                end
                lock.chara_id = 0
            end
        else
            padlock_broken_check(lock)
            lock.chara_id = 0
        end
        if lock.damage_amount ~= 0 then
            if attack_weapon(h.weapon_attack) == KITCHEN_KNIFE then
                lock.damage_amount = asr(lock.damage_amount, 1)
            end
            if gun_attack(h.weapon_attack, HANDGUN) then
                lock.health = -1
            else
                lock.health = s32(lock.health - lock.damage_amount)
            end
            lock.damage_amount = 0
        end
        return 1
    end
    lock.control_state = 0
    lock.state_step = 0
    lock.chara_id = 0
    lock.health = 0
    return 0
end

-- How Harry's current clip plays, for a scene that waits for it: 1 a clip that plays once has reached
-- its end (or, backwards, its start), 0 not yet; -2 a blend, -1 any other clip.
--
-- PC: three stuck nets answer 1 so that a waiting scene goes on: (A) the clip has not
-- moved for 3 s, (B) a clip that plays once has not ended 4 s after its own length (the valve turns of
-- map1_s03 last 5.9 s), (C) any other clip has been waited on for 10 s. They count the game's tick time; a
-- new map starts them over.
local ONCE, BLEND = 1, 2
local STILL_LIMIT, ONCE_LIMIT, OTHER_LIMIT = 3 * 4096, 4 * 4096, 10 * 4096
local nets_map, still_status, still_keyframe, still_time, once_time, other_status, other_time
local nets_logged = { 0, 0, 0 }

local function nets_log(net, limit, text)
    if nets_logged[net] < limit then
        nets_logged[net] = nets_logged[net] + 1
        sh.log(1, "[ANIM-STUCK] " .. text .. " status=" .. c.anim_status .. " kf=" .. c.anim_keyframe)
    end
end

function anim_playback_state(_)
    local map = sh.map_name()
    if map ~= nets_map then
        nets_map = map
        still_status, still_keyframe, still_time, once_time, other_status, other_time = -1, -1, 0, 0, -1, 0
    end
    local status, keyframe = c.anim_status, c.anim_keyframe
    local kind, first, last = sh.entity.harry_clip_playback(status)
    local dt = sh.game.delta_time()
    if status ~= still_status or keyframe ~= still_keyframe then
        still_status, still_keyframe, still_time = status, keyframe, 0
    else
        still_time = s32(still_time + dt)
        if still_time > STILL_LIMIT then
            nets_log(1, 20, "(A) frozen bypass")
            still_time = 0
            return 1
        end
    end
    if kind == ONCE then
        local result
        local speed = sh.entity.harry_clip_duration(status)
        if speed > 0 then
            result = keyframe >= last and 1 or 0
        else
            result = keyframe <= first and 1 or 0
        end
        if result == 1 then
            once_time = 0
        else
            once_time = s32(once_time + dt)
            local length = speed ~= 0 and ((last - first) * 4096 * 4096) // math.abs(speed) or 0
            if once_time > ONCE_LIMIT + length then
                nets_log(2, 10, "(B) end bypass")
                once_time = 0
                return 1
            end
        end
        return result
    end
    if status ~= other_status then
        other_status, other_time = status, 0
    end
    other_time = s32(other_time + dt)
    if other_time > OTHER_LIMIT then
        nets_log(3, 30, "(C) non-once bypass")
        other_time = 0
        return 1
    end
    return kind == BLEND and -2 or -1
end
