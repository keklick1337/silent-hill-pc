-- World objects of map4_s03 (mall hall): the fence falling under Harry, the collision bits of the two hall
-- cells, and (post_camera, called every tick from the engine) the tv wall: its ten screens, their
-- picture programs, the glare over them, the sound and the camera effect switch. post_init and
-- npc_spawn hand the Twinfeeler its update and load the boss's pictures.
-- State is local, so it starts clean whenever the map loads.

local CELL = 163840 -- 40 m in Q12
local function trunc_cell(v)
    if v >= 0 then return v // CELL end
    return -((-v) // CELL)
end

-- The original's 40 m chunk test: cell + a == b for positive coordinates, cell + c == d otherwise.
local function in_chunk(v, a, b, c, d)
    local cell = trunc_cell(v)
    if v > 0 then return cell + a == b end
    return cell + c == d
end

local STEP30 = 136 -- one 30 fps step in Q12

local hit_time = 0   -- time since Harry stepped on the fence
local swing = 0      -- hinge swing speed
local fall = 0       -- fence fall speed
local tilt_acc = 0   -- time banked for the 30 fps hinge steps

local function fence_fall(dt)
    local x, _, z = sh.player_pos()
    if (in_chunk(x, 1, 4, -1, 4) or in_chunk(x, 1, 5, -1, 5)) and in_chunk(z, 1, -3, -1, -3) then
        if sh.flag_get("M4S03_FenceSteppedOn") then
            if hit_time == 0 then
                sh.vec.set(0, 534528, 0, -380928) -- (130.5, 0.0, -93.0) m
                sh.sound.play_3d("mall_fence_fall", "vec:0", 128, 0)
                sh.npc.set_fall_speed(0, sh.npc.fall_speed(0) + 4096)
                fall = fall + 4096
                tilt_acc = 0
            end

            sh.collision.flag_bits_clear(2)
            hit_time = hit_time + dt

            -- The hinge swing is stepped at the 30 fps cadence: the
            -- angle it settles at depends on how many steps fit in half a second.
            tilt_acc = tilt_acc + dt
            local rx, ry, rz = sh.world_object.rotation_get("Fence")
            while tilt_acc >= STEP30 do
                local gravity30 = (STEP30 * 40140) // 4096 -- Q12(9.8)
                tilt_acc = tilt_acc - STEP30
                local var = sh.math.atan2(4096, (gravity30 * sh.math_cos(rx)) // 4096)
                local damp = math.max(0, 4096 - hit_time * 2)
                swing = (damp * (swing + var)) // 4096
                rx = rx + (STEP30 * swing) // 4096
            end

            local g = sh.game.gravity_speed()
            fall = math.min(61440, (fall + g) + (g // 16)) -- Q12(15)

            local px, py, pz = sh.world_object.position_get("Fence")
            sh.world_object.position_set("Fence", px, py + (dt * fall) // 4096, pz)
            if rx > 1024 then rx = 1024 end -- 90 degrees
            sh.world_object.rotation_set("Fence", rx, ry, rz)

            if hit_time > 4096 then
                if sh.map.harry_fall_direction() ~= 0 then
                    sh.flag_set("M4S03_FenceFallAltLanding", true)
                end
                sh.flag_set("M4S03_FenceFallen", true)
            end
        else
            sh.collision.flag_bits_set(2)
        end
    end

    if (in_chunk(x, 1, 3, -1, 3) or in_chunk(x, 1, 4, -1, 4)) and in_chunk(z, 1, 4, -1, 4) then
        if not sh.flag_get("M4S03_TwinfeelerDiveCutsceneDone") then
            sh.collision.flag_bits_set(2)
        else
            sh.collision.flag_bits_clear(2)
        end
    end
end

function update(dt)
    fence_fall(dt)
end

-- ---- The tv wall ----
-- Ten screens at the hall's end: nine small ones in a 3 x 3 block and a big one drawn as 2 x 2 tiles.
-- Each runs a picture program (static, rolling, the picture slides, blank) chosen by the wall's mode:
-- 0 off, 1 waking up (each screen comes on after its own delay and settles on the shaking test pattern),
-- 2 the message (one long scripted program, then mode 3), 3 the loop (random programs).
-- Events ask for a mode through the map variable tv_wall_request (mode + 1); tv_wall_settled is 1
-- once every screen of mode 1 has settled.

local TV_VIEW = {655360, 0, 163840} -- (160, 0, 40) m: the screens' corners are relative to it (Q8)
local TV_REFLECTION_POS = {671744, 0, 212992} -- (164, 0, 52) m
local TV_SHADE = 188
local TV_SHADE_FLICKER = 164

-- Screens 1..9: the small block, row by row; screen 10: the big one (split into tiles).
local TV_SCREENS = {
    {1009, -565, 3018, 782, -394, 3018}, {754, -565, 3018, 526, -394, 3018}, {498, -565, 3018, 269, -394, 3018},
    {1009, -373, 3018, 782, -202, 3018}, {754, -373, 3018, 526, -202, 3018}, {498, -373, 3018, 269, -202, 3018},
    {1009, -181, 3018, 782, -10, 3018}, {754, -181, 3018, 526, -10, 3018}, {498, -181, 3018, 269, -10, 3018},
    {1847, -728, 3423, 1440, -295, 3016, split = true},
}

-- How long each small screen of mode 1 stays dark before it comes on (Q12 s, plus 0.5 s); the big
-- screen takes the entry after the nine.
local TV_WAKE_DELAY = {2867, 7782, 6553, 6963, 9011, 3686, 7782, 6144, 7372, 1228}

-- The picture frames: texture page, palette, the texel rectangle and its random jitter. Frames 0..5
-- are the static and test patterns (TV1.TIM), 6..15 the pictures of TV2.TIM and TV3.TIM.
local TV_FRAMES = {
    [0] = {page = 29, palette = {448, 1}, u = 8, v = 212, w = 1, h = 1, jitter_u = 0, jitter_v = 0},
    {page = 29, palette = {448, 0}, u = 0, v = 0, w = 40, h = 55, jitter_u = 68, jitter_v = 60},
    {page = 29, palette = {448, 1}, u = 0, v = 128, w = 124, h = 62, jitter_u = 4, jitter_v = 1},
    {page = 29, palette = {448, 1}, u = 0, v = 192, w = 124, h = 62, jitter_u = 2, jitter_v = 0},
    {page = 29, palette = {448, 0}, u = 128, v = 128, w = 96, h = 56, jitter_u = 32, jitter_v = 8},
    {page = 12, palette = {448, 5}, u = 128, v = 0, w = 120, h = 62, jitter_u = 2, jitter_v = 0},
    {page = 12, palette = {448, 6}, u = 128, v = 128, w = 125, h = 62, jitter_u = 3, jitter_v = 0},
    {page = 12, palette = {448, 6}, u = 128, v = 192, w = 125, h = 62, jitter_u = 3, jitter_v = 0},
    {page = 13, palette = {448, 6}, u = 0, v = 0, w = 125, h = 62, jitter_u = 3, jitter_v = 0},
    {page = 13, palette = {448, 6}, u = 0, v = 64, w = 125, h = 62, jitter_u = 3, jitter_v = 0},
    {page = 13, palette = {448, 6}, u = 0, v = 128, w = 125, h = 62, jitter_u = 3, jitter_v = 0},
    {page = 13, palette = {448, 6}, u = 0, v = 192, w = 125, h = 62, jitter_u = 3, jitter_v = 0},
    {page = 28, palette = {448, 10}, u = 0, v = 128, w = 125, h = 62, jitter_u = 3, jitter_v = 0},
    {page = 28, palette = {448, 10}, u = 0, v = 192, w = 125, h = 62, jitter_u = 3, jitter_v = 0},
    {page = 28, palette = {448, 11}, u = 128, v = 128, w = 125, h = 62, jitter_u = 3, jitter_v = 0},
    {page = 28, palette = {448, 10}, u = 128, v = 192, w = 122, h = 62, jitter_u = 6, jitter_v = 0},
}

local function s32(v)
    v = v & 0xFFFFFFFF
    if v >= 0x80000000 then return v - 0x100000000 end
    return v
end

local tv_dt = 0 -- the tick's delta time
-- The wall's own random generator (two 32-bit words, the overlay's start values).
local tv_rng_a, tv_rng_b = 0, 500000
local function tv_rand()
    if tv_dt ~= 0 then
        tv_rng_a = s32((s32(tv_rng_b * 0x7169AC35) // 8) ~ 0xA547B39E)
        tv_rng_b = s32((((tv_rng_a * 0x892D719C) & 0xFFFFFFFF) >> 3) ~ 0xC65A4B97)
    end
    return tv_rng_a & 0x7FFF
end

local tv_mode = 0
local tv_flicker = 0 -- 16-bit counter of the glare's flicker
local screens = {}

-- A screen's program restarts; the frame and its offsets stay until the next step sets them.
local function screen_reset(i)
    local c = TV_SCREENS[i]
    local s = screens[i] or {frame = 0, du = 0, dv = 0, dur = 0}
    s.a = {c[1], c[2], c[3]}
    s.b = {c[4], c[5], c[6]}
    s.split = c.split and 1 or 0
    s.settled = 0
    s.step = 0
    s.time = 0
    s.pattern = 0
    s.next_change = 0
    screens[i] = s
end

local function tv_mode_set(mode)
    tv_mode = mode
    for i = 1, #TV_SCREENS do
        screen_reset(i)
    end
    sh.map.set("tv_wall_settled", 0)
end

-- With the wall never set up (the Twinfeeler is beaten), the screens are points at the view's origin
-- until a mode is asked for, and the glare has no model.
local tv_set_up = not sh.flag_get("M4S03_TwinfeelerAttackCutsceneDone")
if tv_set_up then
    tv_mode_set(0)
else
    for i = 1, #TV_SCREENS do
        screens[i] = {a = {0, 0, 0}, b = {0, 0, 0}, split = 0, settled = 0, step = 0, time = 0, pattern = 0,
                      next_change = 0, frame = 0, du = 0, dv = 0, dur = 0}
    end
end
sh.map.set("tv_wall_request", 0)
sh.world_object.visible_set("TvReflection", false)

-- ---- Program steps: each sets the screen's frame for this tick and is done once the step's time
-- (fixed when the step starts) has run out.
local function start(s, dur)
    if s.time == 0 then s.dur = dur end
end

local function blank(s, dur)
    start(s, dur)
    s.frame, s.du, s.dv = 0, 0, 0
    return s.time >= s.dur
end

-- One of the pictures (frame 6 + n).
local function picture(s, n, dur)
    start(s, dur)
    s.frame, s.dv, s.du = n + 6, 0, 0
    return s.time >= s.dur
end

-- Static, its picture offset by kind: 0 none, 1 half, 2 a quarter, 3 sliding up over the step,
-- 4 sliding down over it.
local function static(s, kind, dur)
    start(s, dur)
    s.frame, s.du = 4, 0
    if kind == 4 then
        s.dv = ((math.min(dur, s.time) << 12) // dur) >> 6
    elseif kind == 3 then
        s.dv = 64 - (((((math.min(dur, s.time) << 12) // dur) << 6)) >> 12)
    elseif kind == 1 then
        s.dv = 64
    elseif kind == 2 then
        s.dv = 32
    else
        s.dv = 0
    end
    return s.time >= s.dur
end

-- The picture rolls in (frames 2 and 3), then shows a flash of frame 2 (always, or one time in ten).
local function roll(s, flash_always, dur)
    local flash = flash_always or (tv_rand() & 0xFFF) < 0x199
    start(s, dur)
    s.dv, s.du = 0, 0
    local p0 = math.max(0, math.min(dur - 0x555, 0x111))
    local p1 = math.min((dur * 0x4CC) >> 12, 0x5DD) + p0
    local p2 = p1 + 0x3BB
    local p3 = p1 + 0x45E
    local t = s.time
    if p0 >= t then
        s.frame = 2
        s.dv = ((((t << 12) // p0) * 0x19) >> 12) - 0x20
    elseif t < p1 then
        s.frame = 2
    elseif t < p2 or not flash or t >= p3 then
        s.frame = 3
    else
        s.frame = 2
    end
    return s.time >= dur
end

-- The test pattern (frame 5): kind 0 scrolls it, 1 shakes it sideways.
local function pattern(s, kind, dur)
    start(s, dur)
    s.du = 0
    s.frame = 5
    if kind == 1 then
        s.du = tv_rand() % 6
        s.dv = 32
    else
        s.dv = (((s.time % 2730) << 12) // 2730) >> 6 -- one scroll every 2/3 s
        if s.dv > 24 and s.dv < 29 then s.dv = 32 end
    end
    return s.time >= s.dur
end

-- The bright band (frame 15).
local function band(s, dur)
    start(s, dur)
    s.frame, s.dv, s.du = 15, 0, 0
    return s.time >= s.dur
end

-- Snow (frame 1) and its kinds: 0 plain, 1 and 2 shifted, 3 rolling static, 4 jumping; -1 picks a kind
-- at random every 1..1.5 s.
local function snow_kind(s)
    local pick = false
    if s.time == 0 then
        pick = true
        s.next_change = ((2048 * (tv_rand() & 0xFFF)) >> 12) + 4096
    elseif s.time >= s.next_change then
        pick = true
        s.next_change = s.next_change + ((2048 * (tv_rand() & 0xFFF)) >> 12) + 4096
    end
    if pick then
        local r = tv_rand() & 0xFFF
        if r < 1228 then s.pattern = 0
        elseif r < 2048 then s.pattern = 1
        elseif r < 3153 then s.pattern = 2
        elseif r < 3399 then s.pattern = 3
        else s.pattern = 4 end
    end
    return s.pattern
end

local function snow(s, kind, dur)
    start(s, dur)
    if kind == -1 then kind = snow_kind(s) end
    if kind == 1 then
        s.frame, s.du, s.dv = 1, 50, 24
    elseif kind == 2 then
        s.frame, s.du, s.dv = 1, 128, 0
    elseif kind == 3 then
        s.du, s.frame = 0, 4
        s.dv = (-((((s.time % 6144) << 12) // 6144) * 80)) // 4096 - 108 -- one roll every 1.5 s
    elseif kind == 4 then
        s.frame, s.du = 1, 0
        s.dv = tv_rand() % 320
    else
        s.frame, s.du, s.dv = 1, 0, 0
    end
    return s.time >= s.dur
end

-- ---- The programs. A step is {function, argument, time}; the mode-2 and mode-3 times take a
-- random extra of up to 1 s (drawn every tick, used when the step starts).

-- Mode 1: dark for the screen's delay, then static and pictures; step 13 (the shaking test pattern)
-- never ends and marks the screen settled once its time has run.
local function wake_program(s, i)
    local st = s.step
    local done, settled = false, false
    if st == 1 then done = static(s, 0, 409)
    elseif st == 2 then done = static(s, 1, 409)
    elseif st == 3 then done = static(s, 3, 955)
    elseif st == 4 then done = static(s, 4, 546)
    elseif st == 5 then done = snow(s, 0, 6144)
    elseif st == 6 then done = static(s, 2, 682)
    elseif st == 7 then done = roll(s, true, 8192)
    elseif st == 8 then done = static(s, 3, 1365)
    elseif st == 9 then done = pattern(s, 0, 9011)
    elseif st == 10 then done = static(s, 4, 1365)
    elseif st == 11 then done = band(s, 409)
    elseif st == 12 then done = static(s, 3, 1365)
    elseif st == 13 then settled = pattern(s, 1, 2048)
    else done = blank(s, TV_WAKE_DELAY[i] + 2048) end
    s.time = s.time + tv_dt
    if done then
        s.step = (s.step + 1) & 0xFF
        s.time = 0
    end
    return settled
end

-- Mode 2: the message. {step function, argument, base time}; step 28 ends the program.
local MESSAGE_PROGRAM = {
    [0] = {pattern, 0, 0x1199}, {static, 4, 0x555}, {snow, 1, 0x1800}, {static, 3, 0x555},
    {picture, 0, 0x2AA}, {static, 3, 0x555}, {picture, 1, 0xAAA}, {static, 4, 0x555},
    {picture, 2, 0x1000}, {static, 3, 0x555}, {picture, 3, 0xAAA}, {static, 4, 0x555},
    {picture, 4, 0x2AA}, {picture, 5, 0xAAA}, {static, 3, 0x555}, {picture, 6, 0x2AA},
    {static, 4, 0x555}, {picture, 7, 0xD55}, {static, 3, 0x555}, {picture, 6, 0xAAA},
    {static, 4, 0x555}, {picture, 8, 0x1555}, {static, 4, 0x444}, {static, 3, 0x2AA},
    {snow, 2, 0x1800}, {band, nil, 0x199}, {static, 4, 0x444}, {static, 3, 0x2AA},
    {snow, 0, 0x1800},
}

local function message_program(s)
    local step = MESSAGE_PROGRAM[s.step]
    local done, finished = false, false
    if step then
        local dur = (tv_rand() >> 5) + step[3]
        local r
        if step[1] == band then r = band(s, dur) else r = step[1](s, step[2], dur) end
        if s.step == 28 then finished = r else done = r end
    end
    s.time = s.time + tv_dt
    if done then
        s.time = 0
        s.step = (s.step + 1) & 0xFF
    end
    return finished
end

-- Mode 3: the loop. Steps marked "next" go on to the following step; the others jump to a random
-- multiple of 10 (0..160) when they end. Any step not listed is step 0.
local LOOP_PROGRAM = {
    [0x00] = {snow, -1, 0xAAA}, [0x0A] = {snow, -1, 0xAAA}, [0x14] = {snow, -1, 0xD55},
    [0x1E] = {snow, -1, 0x1000}, [0x28] = {snow, -1, 0x12AA}, [0x32] = {snow, -1, 0x1555},
    [0x3C] = {roll, false, 0x1AAA}, [0x46] = {roll, false, 0x1AAA},
    [0x50] = {static, 4, 0x3BB, next = true}, [0x51] = {picture, 0, 0x2AA},
    [0x5A] = {static, 3, 0x444, next = true}, [0x5B] = {picture, 1, 0xAAA},
    [0x64] = {static, 4, 0x444, next = true}, [0x65] = {picture, 2, 0x1000},
    [0x6E] = {static, 3, 0x444, next = true}, [0x6F] = {picture, 3, 0xAAA},
    [0x78] = {static, 4, 0x444, next = true}, [0x79] = {picture, 4, 0x2AA, next = true},
    [0x7A] = {picture, 5, 0xAAA},
    [0x82] = {static, 3, 0x555, next = true}, [0x83] = {picture, 6, 0x2AA},
    [0x8C] = {static, 4, 0x555, next = true}, [0x8D] = {picture, 7, 0xD55},
    [0x96] = {static, 3, 0x555, next = true}, [0x97] = {picture, 6, 0xAAA},
    [0xA0] = {static, 4, 0x555, next = true}, [0xA1] = {picture, 8, 0x1000},
}

local function loop_program(s)
    local step = LOOP_PROGRAM[s.step] or LOOP_PROGRAM[0]
    local r = step[1](s, step[2], step[3])
    s.time = s.time + tv_dt
    if step.next then
        if r then
            s.step = (s.step + 1) & 0xFF
            s.time = 0
        end
    elseif r then
        s.step = (tv_rand() % 17) * 10
        s.time = 0
    end
end

local function screen_update(s, i)
    if tv_dt == 0 then return end
    if tv_mode == 0 then
        blank(s, 0)
    elseif tv_mode == 1 then
        if wake_program(s, i) then s.settled = 1 end
    elseif tv_mode == 2 then
        if message_program(s) then tv_mode_set(3) end
    elseif tv_mode == 3 then
        loop_program(s)
    end
end

local function screen_draw(s, shade)
    local f = TV_FRAMES[s.frame]
    local u_left = s.du + f.u + tv_rand() % (f.jitter_u + 1)
    local u_right = s.du + f.u + tv_rand() % (f.jitter_u + 1) + f.w - 1
    local v_top = s.dv + f.v + tv_rand() % (f.jitter_v + 1)
    local v_bottom = v_top + f.h - 1
    sh.render.panel(s.a[1], s.a[2], s.a[3], s.b[1], s.b[2], s.b[3], u_left, v_top, u_right, v_bottom, shade, f.page,
                    f.palette[1], f.palette[2], 1, s.split)
end

local function tv_wall_draw()
    sh.render.panel_view(TV_VIEW[1], TV_VIEW[2], TV_VIEW[3])
    local shade = TV_SHADE
    if tv_mode ~= 0 then
        if tv_dt ~= 0 then
            tv_flicker = (tv_flicker + 1) & 0xFFFF
            if tv_flicker & 0xB == 0 then shade = TV_SHADE_FLICKER end
        end
        if tv_set_up then
            sh.world.object_draw("TvReflection", TV_REFLECTION_POS[1], TV_REFLECTION_POS[2], TV_REFLECTION_POS[3],
                                 0, 0, 0)
        end
    end
    for i = 1, #TV_SCREENS do
        screen_update(screens[i], i)
        screen_draw(screens[i], shade)
    end
    local settled = 1
    for i = 1, #TV_SCREENS do
        if screens[i].settled == 0 then settled = 0 end
    end
    sh.map.set("tv_wall_settled", settled)
end

local in_zone_prev = 0
local tv_sound_on = 0
local effects_on = 0

-- Called from the engine every tick, after the cameras move.
function post_camera()
    tv_dt = sh.game.delta_time()
    local request = sh.map.get("tv_wall_request")
    if request ~= 0 then
        sh.map.set("tv_wall_request", 0)
        tv_mode_set(request - 1)
    end

    local x, _, z = sh.player_pos()
    local in_zone = (in_chunk(x, 1, 4, -1, 4) or in_chunk(x, 1, 5, -1, 5)) and in_chunk(z, 1, 2, -1, 2)
    if in_zone then
        tv_wall_draw()
        if in_zone_prev == 0 then
            in_zone_prev = 1
            if sh.flag_get("M4S03_TvMessageCutsceneDone") then
                tv_mode_set(3)
            end
        end

        if sh.flag_get("M4S03_TvScreensOn") then
            if tv_sound_on == 0 then
                sh.sd_call("mall_tv_screens_loop")
                sh.sd_call("mall_tv_screens_loop_second")
                tv_sound_on = 1
            end
            sh.sound.play_3d_falloff("mall_tv_screens_loop", 665600, -4608, 210739, 128, 196608, 0)  -- (162.5, -1.125, 51.45) m
            sh.sound.play_3d_falloff("mall_tv_screens_loop_second", 681660, -8192, 215359, 128, 196608, 0) -- (166.421, -2.0, 52.578) m
        end
    else
        in_zone_prev = 0
        if tv_sound_on ~= 0 then
            sh.sfx_stop("mall_tv_screens_loop")
            sh.sfx_stop("mall_tv_screens_loop_second")
        end
        tv_sound_on = 0
    end

    if (in_chunk(x, 1, 4, -1, 4) or in_chunk(x, 1, 5, -1, 5)) and in_chunk(z, 1, -3, -1, -3) then
        if sh.camera.is_chase() ~= 0 and (x > 515276 or effects_on == 1) then -- Q12(125.8)
            if effects_on ~= 0 then
                sh.world.effects_update(6, 3)
            else
                sh.world.effects_update(6, 20)
            end
            effects_on = effects_on ~ 1
        end
    end
end

-- ---- The Twinfeeler ----
-- The tv pictures: file, texture page, u, v, palette row (palettes at x 448).
local TV_TEXTURES = {
    {"TIM/TV1.TIM", 29, 0, 0, 0}, {"TIM/TV2.TIM", 12, 32, 0, 5}, {"TIM/TV3.TIM", 28, 0, 128, 10},
}

function post_init()
    if not sh.flag_get("M4S03_TwinfeelerAttackCutsceneDone") then
        -- Before the fight the type runs its second update: the small worms in the grating.
        sh.chara.update_variant_set("Twinfeeler", 1)
        for _, t in ipairs(TV_TEXTURES) do
            sh.load.tim_at(t[1], 1, t[2], t[3], t[4], 448, t[5])
        end
        sh.load.wait()
    end
    if sh.flag_get("M4S03_TwinfeelerAttackCutsceneDone") and not sh.flag_get("M4S03_TwinfeelerDiveCutsceneDone") then
        sh.flag_set("M4S03_TwinfeelerFightResumePending", true)
        sh.flag_set("M4S03_TwinfeelerFightAssetsLoaded", false)
    end
end

-- Once the fight is on (M4S03_TwinfeelerArenaEntered), the boss gets its own update and the effect pictures it draws with.
function npc_spawn()
    if sh.flag_get("M4S03_TwinfeelerArenaEntered") and not sh.flag_get("M4S03_TwinfeelerFightAssetsLoaded") then
        sh.chara.update_variant_set("Twinfeeler", 0)
        sh.flag_set("M4S03_TwinfeelerFightAssetsLoaded", true)
        sh.load.tim("TIM/BLOOD.TIM", 1, "effect_tex_img")
        sh.entity.twinfeeler_textures_load()
        sh.load.wait()
    end
end
