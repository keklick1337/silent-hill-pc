-- World objects of map6_s04: the firework rockets by the lake (cell
-- around x 8, z -28) and the wheel of balls (around x -16, z -64) of the amusement park. pre_draw (below) lets
-- the engine run update and the object draws only outside the carousel's cell, where the original ran them; in
-- the carousel's cell it runs carousel_update instead. The rest of the map's world update (the fight camera, the
-- park music, the Flauros glows) is pre_draw / post_draw, the rest of its set-up post_init.
--
-- The rockets rise on their own timers; a rocket that has waited past one second may take off with the
-- game's random numbers (sh.rng_int(0, 0x7FFF) is one Rng_Rand16), drawn in the original's order
-- (rocket 0 to 3, one number per rocket that is waiting). Both effects start when the map loads
-- (the original kept their state in overlay data that the disc load resets).

local ROCKETS = 5
local WHEEL_BALLS = 12

local rocket_clock = 0 -- Q12 (0.. 307.2)
local rocket_count = 0 -- u8 in the original, only its parity is read
local rocket_pos = {0, 0, 0, 0}
local rocket_vel = {0, 0, 0, 0}
local wheel_t1 = 0
local wheel_t2 = 0

local function tdiv(a, b) -- C integer division: truncates toward zero
    local q = a // b
    if q < 0 and q * b ~= a then
        q = q + 1
    end
    return q
end

local function s16(v)
    v = v & 0xFFFF
    return v >= 0x8000 and v - 0x10000 or v
end

-- PLAYER_NEAR_POS: |player - base| < tolerance (Q12).
local function near(p, base, tol)
    local d = p - base
    if d < 0 then
        d = -d
    end
    return d < tol
end

local function min_offset(x, neg, pos)
    if x - neg <= x + pos then
        return x - neg
    end
    return x + pos
end

local function max_offset(x, neg, pos)
    if x - neg > x + pos then
        return x - neg
    end
    return x + pos
end

local function aabb(x0, x1, y0, y1, z0, z1)
    return sh.entity.aabb_visible(x0, x1, y0, y1, z0, z1) ~= 0
end

local function set_rockets_visible(on)
    for i = 0, ROCKETS - 1 do
        for _, name in ipairs({"FireworkBack1", "FireworkBack2", "FireworkWires", "FireworkBoxA", "FireworkBoxB"}) do
            sh.world_object.visible_set(name .. i, on)
        end
    end
end

local function set_wheel_visible(on)
    for i = 0, WHEEL_BALLS - 1 do
        sh.world_object.visible_set("WheelBall" .. i, on)
    end
end

local function rockets(dt)
    local obj = sh.world_object
    local time = (dt * 186413) // 4096 -- Q12_MULT_PRECISE(dt, Q12(45.511))
    rocket_clock = rocket_clock + time

    if rocket_clock > 1258291 then -- Q12(307.2)
        rocket_clock = rocket_clock - 1258291
        rocket_count = (rocket_count + 1) & 0xFF
        for i = 4, 2, -1 do
            rocket_pos[i] = rocket_pos[i - 1]
            rocket_vel[i] = rocket_vel[i - 1]
        end
        rocket_vel[1] = 0
        rocket_pos[1] = 0
    end

    for i = 1, 4 do
        if rocket_vel[i] == 0 then
            rocket_pos[i] = rocket_pos[i] + time
            if rocket_pos[i] > 186413 and (sh.rng_int(0, 0x7FFF) % 186413) + 186413 < rocket_pos[i] then
                rocket_vel[i] = rocket_vel[i] - 1
            end
        elseif rocket_pos[i] > 0 then
            rocket_vel[i] = rocket_vel[i] + (dt * -279620) // 4096 -- Q12(-68.2667)
            rocket_vel[i] = math.max(rocket_vel[i], -139810) -- Q12(-34.1334)
            rocket_pos[i] = rocket_pos[i] + (dt * rocket_vel[i]) // 4096
            if rocket_pos[i] < 0 then
                if rocket_vel[i] > -93206 then -- Q12(-22.7554)
                    rocket_vel[i] = 0
                    rocket_pos[i] = 0
                else
                    rocket_vel[i] = rocket_vel[i] // 2
                end
            end
        else
            rocket_vel[i] = rocket_vel[i] + (dt * 279620) // 4096
            rocket_vel[i] = math.min(rocket_vel[i], 139810)
            rocket_pos[i] = rocket_pos[i] + (dt * rocket_vel[i]) // 4096
            if rocket_pos[i] > 0 then
                if rocket_vel[i] < 93206 then
                    rocket_vel[i] = 0
                    rocket_pos[i] = 0
                else
                    rocket_vel[i] = rocket_vel[i] // 2
                end
            end
        end
    end

    local start = -266240 -- Q12(-65); rocket i is 27 m (110592) further
    for i = 0, ROCKETS - 1 do
        local ang = s16((rocket_clock // 4096) + tdiv(start + i * 110592, 360))
        local x = 35635 -- Q12(8.7)
        local y = ((sh.math.cos(ang) * 40960) // 4096) - 56524
        local z = ((sh.math.sin(ang) * 40960) // 4096) - 114688
        for _, name in ipairs({"FireworkBack1", "FireworkBack2", "FireworkWires"}) do
            obj.position_set(name .. i, x, y, z)
            obj.rotation_set(name .. i, ang, 0, 0)
            obj.visible_set(name .. i, true)
        end

        local bx = 35635
        local by = ((sh.math.cos(ang) * 46080) // 4096) - 56524 -- radius Q12(11.25)
        local bz = ((sh.math.sin(ang) * 46080) // 4096) - 114688
        -- Arithmetic shifts are floor divisions here (Lua's >> is logical).
        -- The fifth rocket reads the word after the four timers: the first rocket's velocity.
        local spin = i < 4 and rocket_pos[i + 1] or rocket_vel[1]
        local shown = aabb(min_offset(bx, 4096, 4096), max_offset(bx, 4096, 4096),
                           min_offset(by, -8192, -819), max_offset(by, -8192, -819),
                           min_offset(bz, 4096, 4096), max_offset(bz, 4096, 4096))
        local first = ((rocket_count + i) & 1) == 0
        obj.position_set("FireworkBoxA" .. i, bx, by, bz)
        obj.rotation_set("FireworkBoxA" .. i, spin // 4096, 0, 0)
        obj.position_set("FireworkBoxB" .. i, bx, by, bz)
        obj.rotation_set("FireworkBoxB" .. i, spin // 4096, 0, 0)
        obj.visible_set("FireworkBoxA" .. i, shown and first)
        obj.visible_set("FireworkBoxB" .. i, shown and not first)

    end
end

local function wheel(dt)
    local obj = sh.world_object
    wheel_t1 = wheel_t1 + (dt * 807) // 4096 -- Q12(0.197) + 1
    wheel_t2 = wheel_t2 + (dt * 1126) // 4096 -- Q12(0.275)
    for i = 0, WHEEL_BALLS - 1 do
        local angle = s16(tdiv(sh.math.sin(wheel_t1 + (i << 10)), 30))
        local y = ((sh.math.sin(angle) * 16384) // 4096) - 6144
        local radius = (sh.math.cos(angle) * 16384) // 4096
        local turn = s16(wheel_t2 + tdiv(i << 12, 12))
        local x = ((radius * sh.math.sin(turn)) // 4096) - 65536
        local z = ((radius * sh.math.cos(turn)) // 4096) - 262144
        local name = "WheelBall" .. i
        obj.position_set(name, x, y, z)
        obj.rotation_set(name, -angle, turn, 0)
        obj.visible_set(name, true)
    end
end

function update(dt)
    local px, _, pz = sh.player_pos()
    if not sh.flag_get("M6S04_FlaurosRaised") and near(px, 32768, 81920) and near(pz, -114688, 90112)
        and aabb(28672, 45056, -57344, 0, -147456, -81920) then
        rockets(dt)
        set_wheel_visible(false)
    else
        set_rockets_visible(false)
        if not sh.flag_get("M6S04_CutsceneAlessaLooksBackStarted") and near(px, -65536, 81920) and near(pz, -262144, 81920)
            and aabb(-90112, -36864, -32768, 0, -282624, -237568) then
            wheel(dt)
        else
            set_wheel_visible(false)
        end
    end
end

-- The carousel: it turns by the map variable ending_camera_angle (s16, 0..4095
-- turns), its speed is cutscene_unused_value (s32, Q12 per second; outside the map's reset data) plus
-- carousel_drift_speed. Monster Cybil (npcs[0]) rides it: while she fights it speeds up and she moves on
-- the horse sh.npc.ride_horse names (its height goes to sh.npc.set_ride_height). The parts are drawn where
-- the turning carousel puts them, each only when its box is on screen.

local CENTRE = {81920, 0, 409600} -- (20, 0, 100) m
local CYBIL = 1 -- the character slot of npcs[0]
local CYBIL_SFX = "carousel_turning_loop"
local SPIN_LIMIT_FIGHT = -455
local SPIN_LIMIT = -227
-- The ten horses relative to the centre and their heading (from the overlay's data).
local HORSE_X = {16384, 26624, 10240, -16384, -26624, -10240, 3276, 12288, -3276, -12288}
local HORSE_Z = {21299, -3481, -24780, -21299, 3481, 24780, 12492, -3276, -12492, 3276}
local HORSE_ROT = {427, 1109, 1792, 2475, 3157, 3840, 171, 1195, 2219, 3243}
local CAROUSEL_PARTS = {"CarouselHorseA", "CarouselHorseB", "CarouselHouse", "CarouselStructure", "CarouselFenceGate",
                        "CarouselFence", "CarouselCenter", "CarouselCenterLights", "CarouselChair"}

-- The parts are drawn by carousel_update only.
for _, name in ipairs(CAROUSEL_PARTS) do
    sh.world_object.visible_set(name, false)
end
-- The carousel's state outside the overlay data: reset when the map loads.
sh.map.set("carousel_drift_speed", 0)
sh.map.set("carousel_chair_hidden", 0)
sh.map.set("ending_camera_angle", 0)

local function draw(name, x, y, z, ry)
    sh.world.object_draw(name, x, y, z, 0, s16(ry), 0)
end

local function spin_update(dt)
    local spin = sh.map.get("cutscene_unused_value")
    local before = spin
    local angle = s16(sh.map.get("ending_camera_angle") + (dt * spin) // 4096)
    angle = (((dt * sh.map.get("carousel_drift_speed")) // 4096) + angle) & 0xFFF
    sh.map.set("ending_camera_angle", angle)

    if sh.entity.get(CYBIL, "npc_control_state") == 1 then -- the fight
        local health = sh.npc.health(CYBIL)
        if spin == 0 and health > 0 then
            sh.sd_call(CYBIL_SFX)
        end
        spin = health > 0 and spin - 50 or spin + 50
        spin = math.max(SPIN_LIMIT_FIGHT, math.min(spin, 0))
    else
        local fast = sh.entity.get(0, "harry_move_speed") > 12288 -- above 3.0
        if sh.npc.ride_state(CYBIL) == 12 then -- she rides a horse
            spin = spin - 50
            if before == 0 and spin < 0 then
                if fast then
                    sh.player.damage_feet_front()
                end
                sh.sd_call(CYBIL_SFX)
                sh.flag_set("M6S04_CarouselTurning", true)
            end
        else
            spin = spin + 50
            if before < 0 and spin >= 0 then
                if fast then
                    sh.player.damage_feet_front()
                end
                sh.sfx_stop(CYBIL_SFX)
                sh.flag_set("M6S04_CarouselTurning", false)
            end
        end
        spin = math.max(SPIN_LIMIT, math.min(spin, 0))
    end
    sh.map.set("cutscene_unused_value", spin)

    if spin ~= 0 then
        local a = math.abs(spin) >> 2
        sh.sfx_falloff_pitch_play(CYBIL_SFX, 81920, 4096, 409600, a, 65536, a) -- (20, 1, 100) m, heard within 16 m
    end
    return angle
end

local function aabb(x0, x1, y0, y1, z0, z1)
    return sh.entity.aabb_visible(x0, x1, y0, y1, z0, z1) ~= 0
end

-- The ring of n points at radius r around the centre, starting a quarter turn after `rot`.
local function ring(rot, n, step, r)
    local xs, zs = {}, {}
    local cur = 1024
    for i = 0, n - 1 do
        xs[i] = ((sh.math.sin(rot + cur) * r) // 4096) + CENTRE[1]
        zs[i] = ((sh.math.cos(rot + cur) * r) // 4096) + CENTRE[3]
        cur = cur + step
    end
    return xs, zs
end

local function segment_visible(xs, zs, i, n, pad)
    local j = (i + 1) % n
    return aabb(math.min(xs[i], xs[j]) - pad, math.max(xs[i], xs[j]) + pad, -8192, 0,
                math.min(zs[i], zs[j]) - pad, math.max(zs[i], zs[j]) + pad)
end

local function carousel_draw(angle)
    local cx, cz = CENTRE[1], CENTRE[3]
    for i = 0, 9 do
        local x, z = HORSE_X[i + 1] + cx, HORSE_Z[i + 1] + cz
        if aabb(x - 4096, x + 4096, -8192, 0, z - 4096, z + 4096) then
            local s, model
            if i < 6 then
                s = sh.math.sin(angle * 4 + tdiv(4096 * i, 3))
                model = "CarouselHorseB"
            else
                s = sh.math.sin(angle * 4 + tdiv(4096 * i, 2) + 2048)
                model = "CarouselHorseA"
            end
            local y = tdiv(-4096 - s, 5)
            draw(model, x, y, z, HORSE_ROT[i + 1])
            if i == sh.npc.ride_horse(CYBIL) then
                sh.npc.set_ride_height(CYBIL, y)
            end
        end
    end

    local hx = ((sh.math.sin(angle + 2048) * 32768) // 4096) + cx -- 8 m out, the far side
    local hz = ((sh.math.cos(angle + 2048) * 32768) // 4096) + cz
    if aabb(hx - 18432, hx + 18432, -8192, 0, hz - 18432, hz + 18432) then -- 4.5 m
        draw("CarouselHouse", cx, 4096, cz, angle)
        draw("CarouselStructure", cx, 4096, cz, angle + 128) -- 11.25 deg
    end

    -- The fence: eight segments at 7.5 m; the first is the gate, drawn 0.3 m lower.
    local rot = s16(angle + 1024)
    local xs, zs = ring(rot, 8, 512, 30720)
    local y = -1228
    for i = 0, 7 do
        if segment_visible(xs, zs, i, 8, 819) then
            draw(i ~= 0 and "CarouselFence" or "CarouselFenceGate", cx, y, cz, rot)
        end
        y = 0
        rot = s16(rot + 512)
    end

    -- The centre column: four quarters at 0.8 m.
    rot = angle
    xs, zs = ring(rot, 4, 1024, 3276)
    for i = 0, 3 do
        if segment_visible(xs, zs, i, 4, 1638) then
            draw("CarouselCenter", CENTRE[1], CENTRE[2], CENTRE[3], rot)
            draw("CarouselCenterLights", CENTRE[1], CENTRE[2], CENTRE[3], rot)
        end
        rot = s16(rot + 1024)
    end

    if sh.map.get("carousel_chair_hidden") == 0 then
        draw("CarouselChair", 93388, 0, 395673, -481) -- (22.8, 0, 96.6) m
    end
end

function carousel_update()
    if sh.flag_get("M6S04_CybilBossFightStarted") and not sh.flag_get("M6S04_CybilFightSetUp") then
        return 0
    end
    carousel_draw(spin_update(sh.game.delta_time()))
    return 0
end

-- The rest of the map's world update and set-up. C arithmetic:
-- s32 wraps 32-bit products, `// 4096` is the arithmetic shift of a Q12 product (Lua's >> is logical).

local function s32(v)
    v = v & 0xFFFFFFFF
    return v >= 0x80000000 and v - 0x100000000 or v
end

local function mul12(a, b) -- Q12_MULT: a 32-bit product
    return s32(a * b) // 4096
end

local function mul12_wide(a, b) -- Q12_MULT_PRECISE: a 64-bit product
    return s32((a * b) // 4096)
end

-- (x << 20) as a 32-bit word: the sign of an angle difference taken modulo a full turn.
local function turn_sign(x)
    return s32(x << 20)
end

local GLOW = "FlaurosGlow" -- gamedata/effects/glow_fields.json
local PARK_SFX_ROCKETS = "park_rocket_ride_loop"
local PARK_SFX_WHEEL = "park_wheel_loop"
local CELL = 163840 -- 40 m
local CAROUSEL_CELL = 17 + (19 << 8) -- CELL_XZ(17, 19) of the cells shifted by 17 (positive) or 15

-- Reset when the map loads (the original's world init, before the objects).
sh.sound.sfx_mix_reset()
sh.map.set("park_scene_sfx_volume", 0)
sh.map.set("park_scene_sfx_pan_mode", 0)
local park_sfx_on = false
local in_carousel_cell = false
local collision_bits = 0

function post_init()
    sh.map.set("flauros_glow_stage", 0)
    sh.effects.glow_field_clear_emitters(GLOW)
    sh.game.set_npc_flags_id(3)
end

-- The fight camera around the carousel (events.lua calls it through sh.world.script_call):
-- the camera swings on a 5.5 m circle around the carousel's centre, behind Harry, and looks past him; with
-- snap it jumps there and its turning speeds start at zero. Its state: the swing
-- speed and angle, the aim speed and angle (s16 each).
local swing_speed, swing_angle, aim_speed, aim_angle = 0, 0, 0, 0

function cybil_boss_camera(snap)
    snap = snap ~= 0
    local CX, CZ = CENTRE[1], CENTRE[3]
    local dt = sh.game.delta_time()
    local px, _, pz = sh.player_pos()
    local rot = sh.entity.get(0, "rot_y")
    local looking = sh.camera.view_button_active() ~= 0

    local around = sh.math.atan2(px - CX, pz - CZ)
    local side = around + 1024 -- 90 deg
    local target
    if looking then
        target = s16(around + mul12(sh.math.cos(rot - side), -113)) -- -10 deg
    else
        local swing = mul12(sh.math.cos(rot - side), 256) -- 22.5 deg
        if snap or turn_sign(swing_angle - around) > 0 then
            swing = swing + 426 -- 37.5 deg
        else
            swing = swing - 426
        end
        target = s16(around + swing)
    end

    if snap then
        swing_speed = 0
        swing_angle = target
    else
        swing_speed = s16(sh.camera.angle_speed_to_target(swing_speed, swing_angle, target, 2048, 2867, 20480))
        swing_angle = s16(swing_angle + mul12_wide(swing_speed, dt))
    end

    local camx = mul12(sh.math.sin(swing_angle), 22528) + CX -- 5.5 m
    local camz = mul12(sh.math.cos(swing_angle), 22528) + CZ
    local camy = -9830 -- -2.4 m
    local vx, vz
    if snap then
        vx, vz = camx, camz
    else
        vx, _, vz = sh.vc_view_position()
    end

    local aim
    if looking then
        local ax = px + mul12(sh.math.sin(rot), 32768) -- 8 m ahead of Harry
        local az = pz + mul12(sh.math.cos(rot), 32768)
        aim = sh.math.atan2(ax - vx, az - vz)
    else
        vx, vz = camx, camz
        local reach = sh.math.cos(rot - around) + (sh.math.vector_length(px - CX, 0, pz - CZ) - 1638) -- 0.4 m
        local bend = around + mul12(sh.math.cos(rot - side), 284) -- 25 deg
        local ax = mul12(reach, sh.math.sin(bend)) + CX - vx
        local az = mul12(reach, sh.math.cos(bend)) + CZ
        aim = sh.math.atan2(ax, az - vz)
        local from_centre = sh.math.atan2(vx - CX, vz - CZ)
        local off = turn_sign(aim - (from_centre + 1024)) // 1048576 -- Q12_ANGLE_NORM_S
        if turn_sign(swing_angle - around) > 0 then
            if turn_sign(off - 1536) < 0 then -- 135 deg
                aim = from_centre + 2560 -- 225 deg
            end
        elseif turn_sign(off - 512) > 0 then -- 45 deg
            aim = from_centre + 1536
        end
    end

    if snap then
        aim_speed = 0
        aim_angle = s16(aim)
    else
        aim_speed = s16(sh.camera.angle_speed_to_target(aim_speed, aim_angle, aim, 8192, 8192, 32768))
        aim_angle = s16(aim_angle + mul12_wide(aim_speed, dt))
    end

    local watchx = camx + mul12(sh.math.sin(aim_angle), 12288) -- 3 m
    local watchz = camz + mul12(sh.math.cos(aim_angle), 12288)
    local watchy = tdiv(-sh.math.sin(-227) * 12288, sh.math.cos(-227)) - 9830 -- 20 deg down over 3 m

    if looking and sh.camera.self_view_enabled() ~= 0 then
        sh.camera_auto_return(sh.camera.view_button_was_active() == 0)
        return 0
    end
    sh.vc_cam_target(camx, camy, camz, true)
    sh.vc_watch_target(watchx, watchy, watchz, true)
    return 0
end

-- The Flauros glows of the "I figured you'd show" scene (events.lua adds and removes the glowing things and
-- moves the stage on): the field starts once the map's chunks are in, then each stage draws its part.
local function flauros_glow_update()
    local stage = sh.map.get("flauros_glow_stage")
    if stage == 0 then
        local ready = sh.load.chunks()
        if not ready or ready == 0 then
            return
        end
        stage = 1
        sh.map.set("flauros_glow_stage", stage)
        sh.effects.glow_field_reset(GLOW)
    end
    if stage >= 2 and stage <= 4 then
        sh.effects.glow_field_emit(GLOW)
        sh.effects.glow_field_decay(GLOW)
        if stage == 2 then
            sh.effects.glow_field_draw_mesh(GLOW, "FlashPlane")
        elseif stage == 3 then
            sh.effects.glow_field_draw(GLOW)
        else
            sh.effects.glow_field_draw_mesh(GLOW, "RayPlane")
        end
    end
end

-- Before the objects: 0 in the carousel's cell (the engine then skips update and the object draws).
function pre_draw()
    if not sh.flag_get("M6S04_EntryAutosaveTaken") then
        sh.flag_set("M6S04_EntryAutosaveTaken", true)
        sh.game.state_update()
    end
    local px, _, pz = sh.player_pos()
    local cx = tdiv(px, CELL) + (px > 0 and 17 or 15)
    local cz = tdiv(pz, CELL) + (pz > 0 and 17 or 15)
    collision_bits = 0
    in_carousel_cell = cx + (cz << 8) == CAROUSEL_CELL
    if not in_carousel_cell then
        return 1
    end

    carousel_update()
    if sh.flag_get("M6S04_CybilBossFightStarted") then
        collision_bits = 2 -- the object collision group
        if sh.event.sys_state() ~= 10 then -- not SysState_EventCallback
            cybil_boss_camera(0)
        end
    end
    if park_sfx_on then
        sh.sfx_stop(PARK_SFX_ROCKETS)
        sh.sfx_stop(PARK_SFX_WHEEL)
        park_sfx_on = false
    end
    return 0
end

-- After the objects: outside the carousel's cell the small Flauros and the park's two sounds (the music box by
-- the rockets through the engine's mix, the wheel's at a fixed level), then the collision trigger bits and the
-- glows.
function post_draw()
    if not in_carousel_cell then
        if sh.flag_get("M6S04_SmallFlaurosVisible") then
            sh.world.object_add("flauros", "flauros.pos", "flauros.rot")
        end
        if not park_sfx_on then
            sh.sd_call(PARK_SFX_ROCKETS)
            sh.sd_call(PARK_SFX_WHEEL)
            park_sfx_on = true
        end
        sh.sound.sfx_mix_update(PARK_SFX_ROCKETS, 35635, -10240, -114688, -- (8.7, -2.5, -28) m
                                sh.map.get("park_scene_sfx_volume"), 131072, sh.map.get("park_scene_sfx_pan_mode"))
        sh.sfx_falloff_pitch_play(PARK_SFX_WHEEL, -65536, -6144, -262144, 128, 131072, 0) -- (-16, -1.5, -64) m
    end
    sh.collision.flag_bits_clear(0xFFFF)
    sh.collision.flag_bits_set(collision_bits)
    flauros_glow_update()
end
