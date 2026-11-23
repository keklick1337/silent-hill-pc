-- World rules of map0_s00, the opening streets (the overlay's Map_WorldObjectsUpdate,
-- Map_WorldObjectsInit and npc spawn event). Per frame, in the original's order:
--   1. Cheryl leads Harry down the first streets (M0S00_CherylSpottedCutsceneDone .. M0S00_CherylThroughAlleyDoor) and the alley camera follows
--      him (M0S00_AlleyCameraActive);
--   2. the alley darkens with Harry's progress along it (M0S00_AlleyEntered .. M0S00_AlleyFullyDark);
--   3. the light flickers in the last alley (M0S00_AlleyGetsDarkerCutsceneDone);
--   4. the wheelchair wheel spins (world objects Wheel1/Wheel2) - here because the steps above may
--      draw random numbers in the same frame and the wheel draws one after them;
--   5. after the draw: the wheelchair squeaks (post_draw).
-- Script state is reset when the map loads, as the original's map data is.
-- Numbers are Q12 (4096 = 1 m, 4096 = 360 degrees); the float constants are truncated in float
-- as the original's Q12() is.

local CELL = 163840 -- 40 m
local WHEELS = {"Wheel1", "Wheel2"}
local CHERYL = 1 -- the character slot of npcs[0]
local HARRY = 0

local SPEED_MAX = 7372 -- 1.8 m/s: Cheryl's base run speed

-- The lit alley (M0S00_AlleyEntered): Harry's distance along this path sets the darkness, 0..50 at its far
-- end. {x, z, progress}.
local LIT_PATH = {
    {-372736, 995328, 0},      -- (-91, 243)
    {-372736, 946176, 76250},  -- (-91, 231) 18.62
    {-385024, 937984, 99200},  -- (-94, 229) 24.22
    {-409600, 937984, 143700}, -- (-100, 229) 35.08
    {-421888, 929792, 166600}, -- (-103, 227) 40.67
    {-446464, 929792, 204800}, -- (-109, 227) 50.0
}
-- The deeper alley (after M0S00_DeeperAlleyEntered): 60 plus up to 20 along this segment.
local DEEP_PATH = {
    {-1105920, 1048576, 0},    -- (-270, 256)
    {-1105920, 1044480, 81920}, -- (-270, 255) 20.0
}
local WHEELCHAIR_SFX_POS = {-1099038, -1802, 1006469} -- (-268.32, -0.44, 245.72)

-- The map effects follow these engine cells (sh.world.effects_track).
local CELL_DARKNESS, CELL_FLICKER = 0, 1

-- State the overlay kept in its data.
local lead_speed = 0
local corner_speed = 0
local warp_camera = true
local darkness = -1 -- NO_VALUE: the darkness is not tracked yet
local flicker_level = -1
-- The flicker's two clocks (s16) and its random rates (u8); all start at 0.
local flicker_clock0, flicker_clock1 = 0, 0
local flicker_rate0, flicker_rate1, flicker_rate2 = 0, 0, 0

-- The wheel's tilt (x, y); its spin about z starts at 0.
local TILT_X, TILT_Y = -489, -1308
local spin = 0

local function s16(v)
    v = v & 0xFFFF
    return v >= 0x8000 and v - 0x10000 or v
end

local function s32(v)
    v = v & 0xFFFFFFFF
    return v >= 0x80000000 and v - 0x100000000 or v
end

-- An arithmetic shift right (the C one on signed values) is a floor division.
local function asr(v, n)
    return v // (1 << n)
end

-- Math_AngleNormalizeSigned: -2047..2048.
local function angle_signed(a)
    a = a & 0xFFF
    return a > 2048 and a - 4096 or a
end

-- Q12_MULT_FLOAT_PRECISE(dt, 0.5f)
local function half(dt)
    return asr(dt * 2048, 12)
end

-- PLAYER_IN_MAP_CHUNK: the C division truncates toward zero.
local function in_chunk(p, x0, x1, x2, x3)
    local c = p // CELL
    if p < 0 and c * CELL ~= p then
        c = c + 1
    end
    return (p > 0 and c + x0 == x1) or (p <= 0 and c + x2 == x3)
end

local function menu_active()
    return sh.game.sys_flags() & 0x80 ~= 0
end

-- Cheryl is out of view (an AABB of 2 x 1.5 x 2 m around her feet).
local function cheryl_unseen()
    local x, y, z = sh.npc.pos_x(CHERYL), sh.npc.pos_y(CHERYL), sh.npc.pos_z(CHERYL)
    return sh.view.aabb_visible(x - 4096, x + 4096, y - 6144, y, z - 4096, z + 4096) == 0
end

-- Cheryl leaves the scene: she is parked at (1, 1) and her animation handed back.
local function cheryl_park()
    sh.npc.set_pos_z(CHERYL, 1)
    sh.npc.set_pos_x(CHERYL, 1)
end

-- Cheryl's run speed eases back to 1.8 m/s at 0.5 m/s per second unless Harry runs faster
-- toward her, then she keeps `lead` ahead of his speed.
local function chase_speed(speed, harry_speed, lead, dt)
    if harry_speed > SPEED_MAX then
        return harry_speed + lead
    elseif speed > SPEED_MAX then
        local v = speed - half(dt)
        return v >= SPEED_MAX and v or SPEED_MAX
    end
    local v = speed + half(dt)
    return v > SPEED_MAX and SPEED_MAX or v
end

-- M0S00_CherylSpottedCutsceneDone .. M0S00_CherylLeadRunEnded: Cheryl runs ahead to (-62, 108) while the camera can see her.
local function cheryl_lead(dt)
    if not sh.flag_get("M0S00_CherylLeadRunStarted") then
        sh.event_waypoint_set(true, 1, 0, 2048, -253952, 442368) -- 180 deg, (-62, 108)
        sh.chara_props_reset(CHERYL)
        sh.flag_set("M0S00_CherylLeadRunStarted", true)
    end
    local cx, _, cz = sh.vc_view_position()
    local nx, nz = sh.npc.pos_x(CHERYL), sh.npc.pos_z(CHERYL)
    if sh.math.vec2_len_q6(nx - cx, nz - cz) <= 67584 and nx >= -237568 then -- 16.5 m, x >= -58
        if menu_active() or not cheryl_unseen() then
            local hx, _, hz = sh.player_pos()
            local heading = sh.npc.heading_angle(HARRY)
            -- Harry's speed toward Cheryl or toward her goal (-62, 106), whichever is nearer
            -- his heading.
            local to_cheryl = angle_signed(sh.math.atan2(nx - hx, nz - hz) - heading)
            local to_goal = angle_signed(sh.math.atan2(-253952 - hx, 434176 - hz) - heading)
            if math.abs(to_cheryl) > math.abs(to_goal) then
                to_cheryl = to_goal
            end
            local toward = asr(s32(s32(sh.npc.move_speed(HARRY) * sh.math.cos(to_cheryl)) * 16), 16)
            lead_speed = chase_speed(lead_speed, toward, 3686, dt) -- 0.9 m/s ahead
            sh.entity.set(CHERYL, "npc_move_distance", lead_speed)
            sh.npc.path_walk_no_wait(CHERYL, 2, 1)
            return
        end
    end
    cheryl_park()
    sh.entity.set(CHERYL, "npc_move_distance", 0)
    sh.map.chara_anim_reset(CHERYL, false)
    sh.flag_set("M0S00_CherylLeadRunEnded", true)
end

-- Distance from Harry to 2 m south of Cheryl.
local function cheryl_gap()
    local hx, _, hz = sh.player_pos()
    return sh.math.vec2_len_q6(sh.npc.pos_x(CHERYL) - hx, (sh.npc.pos_z(CHERYL) - hz) - 8192)
end

-- M0S00_CherylRunsIntoAlleySeen .. M0S00_CherylCornerRunEnded: Cheryl runs on round the corner (z >= 49) while seen.
local function cheryl_corner(dt)
    local gap = cheryl_gap()
    if not sh.flag_get("M0S00_CherylRunsIntoAlleySeen") then
        sh.chara_props_reset(CHERYL)
        return
    end
    if gap <= 59392 and sh.npc.pos_z(CHERYL) >= 200704 then -- 14.5 m, z >= 49
        if menu_active() or not cheryl_unseen() then
            local toward = asr(s32(sh.npc.move_speed(HARRY) * sh.math.cos(sh.npc.heading_angle(HARRY) + 2048)), 12)
            corner_speed = chase_speed(corner_speed, toward, 2457, dt) -- 0.6 m/s ahead
            sh.entity.set(CHERYL, "npc_move_distance", corner_speed)
            sh.npc.path_walk_no_wait(CHERYL, 2, 1)
            return
        end
    end
    cheryl_park()
    sh.map.chara_anim_reset(CHERYL, false)
    sh.flag_set("M0S00_CherylCornerRunEnded", true)
end

-- M0S00_CherylAtAlleyDoor .. M0S00_CherylThroughAlleyDoor: Cheryl waits at the alley door (-61, 48) until Harry is near, then runs
-- through it; the door sounds and she is gone.
local function cheryl_door()
    local gap = cheryl_gap()
    if not sh.flag_get("M0S00_CherylAtAlleyDoor") then
        sh.npc.set_pos_x(CHERYL, -249856) -- -61
        sh.npc.set_pos_z(CHERYL, 196608)  -- 48
        sh.npc.set_rot_y(CHERYL, 1024)    -- 90 deg
        if gap < 60620 then -- 14.8 m
            sh.chara_props_reset(CHERYL)
            sh.flag_set("M0S00_CherylAtAlleyDoor", true)
            sh.event_waypoint_set(true, 1, 0, 1024, -233472, 192512) -- 90 deg, (-57, 47)
        end
        return
    end
    sh.entity.set(CHERYL, "npc_move_distance", SPEED_MAX)
    sh.npc.path_walk_no_wait(CHERYL, 2, 1)
    if sh.npc.pos_x(CHERYL) > -237568 then -- -58
        sh.flag_set("M0S00_CherylThroughAlleyDoor", true)
        sh.npc.set_chara_id(CHERYL, "None")
        sh.sound.play_3d("intro_door_open", {-233472, -4096, 192512}, 128, 0) -- (-57, -1, 47), 0.5
    end
end

-- M0S00_AlleyCameraActive: the camera of the alley entrance and of the street before it.
local function alley_camera()
    if not sh.flag_get("M0S00_AlleyCameraActive") then
        return
    end
    local px, _, pz = sh.player_pos()
    if pz < 196608 and px > -241664 then -- z < 48, x > -59: the fixed shot at the door
        sh.camera.position_now(nil, -248012, -2088, 195092, 0, 0, 0, 0, warp_camera) -- (-60.55, -0.51, 47.63)
        sh.camera.look_at_now(nil, -233103, -5160, 188989, 0, 0, 0, 0, warp_camera) -- (-56.91, -1.26, 46.14)
        warp_camera = false
        return
    end
    if pz < 409600 or (pz < 495616 and px < -237568) then -- z < 100, or z < 121 west of -58
        local cos = sh.math.cos(sh.npc.rot_y(HARRY))
        local y = asr(cos * 819, 12) - 7372 -- 0.2 m bob, 1.8 m up
        local z = pz + asr(cos * 8192, 12) + 16384 -- 2 m swing, 4 m behind
        local x = math.max(math.min(-251904, px), -260096) -- -61.5 .. -63.5
        sh.camera.position_now(nil, x, y, z, 0, 0, 0, 0, not warp_camera)
        sh.camera.look_at_now(nil, px, -2867, pz - 20480, 0, 0, 0, 0, not warp_camera) -- 0.7 m up, 5 m ahead
        warp_camera = true
        return
    end
    sh.camera.auto_return(warp_camera)
    warp_camera = false
end

-- Harry's progress along a path {a, b, ...}: the progress of the nearest segment at his place.
local function progress_on(path, first, last)
    local hx, _, hz = sh.player_pos()
    local best_d, best_t, best_i = 0, 0, first
    for i = first, last - 1 do
        local a, b = path[i], path[i + 1]
        local d, t = sh.math.segment_distance(hx, hz, a[1], a[2], b[1], b[2])
        if i == first or d < best_d then
            best_d, best_t, best_i = d, t, i
        end
    end
    local a, b = path[best_i], path[best_i + 1]
    return asr((b[3] - a[3]) * best_t, 12) + a[3]
end

local function darkness_advance(value, dt)
    if sh.event.sys_state() == 10 then -- an event runs
        if not sh.flag_get("M0S00_AlleyNearlyDark") then
            return value
        end
    elseif not sh.flag_get("M0S00_AlleyNearlyDark") then
        local hx = sh.player_pos()
        local v = 0
        if hx < -819200 then -- x < -200
            v = progress_on(DEEP_PATH, 1, 2) + 245760 -- 60 + 0..20
        end
        return math.max(v, value)
    end
    return asr(dt * 36864, 12) + value -- 9 per second
end

local function darkness_set(v)
    darkness = v
    sh.world.effects_track_set(CELL_DARKNESS, v)
end

-- M0S00_AlleyEntered .. M0S00_AlleyFullyDark: the alley gets darker; 80 sets M0S00_AlleyNearlyDark, 100 M0S00_AlleyFullyDark.
local function alley_darkness(dt)
    if darkness == -1 then
        sh.world.effects_track_set(CELL_DARKNESS, darkness)
        sh.world.effects_track(CELL_DARKNESS, 6, 6, 0, 409600) -- 0 .. 100
        darkness_set(0)
        sh.sound.play("darkness_hum", 0, 224) -- 7/8
    end
    if sh.flag_get("M0S00_DeeperAlleyEntered") then
        if not sh.flag_get("M0S00_DeeperAlleyDarknessStarted") then
            sh.map.weather_density_set(6)
            sh.flag_set("M0S00_DeeperAlleyDarknessStarted", true)
            sh.sound.play("darkness_hum", 0, 192) -- 3/4
            darkness_set(245760) -- 60
        end
        darkness_set(darkness_advance(darkness, dt))
    else
        darkness_set(progress_on(LIT_PATH, 1, #LIT_PATH))
    end
    if darkness >= 327680 then -- 80
        sh.flag_set("M0S00_AlleyNearlyDark", true)
    end
    if darkness >= 409600 then -- 100
        sh.flag_set("M0S00_AlleyFullyDark", true)
    end
end

-- M0S00_AlleyGetsDarkerCutsceneDone: the light of the last alley flickers.
local function alley_flicker(dt)
    if flicker_level == -1 then
        sh.world.effects_track_set(CELL_FLICKER, flicker_level)
        sh.world.effects_track(CELL_FLICKER, 6, 6, 0, 409600)
        flicker_level = 0
        sh.world.effects_track_set(CELL_FLICKER, 0)
    end
    flicker_clock0 = s16(flicker_clock0 + dt)
    if asr(flicker_clock0 * flicker_rate0, 6) >= 4096 then
        flicker_clock0 = 0
        flicker_rate0 = sh.rng.uint(32, 95)
        flicker_rate1 = sh.rng.uint(32, 63)
    end
    local amp = asr(flicker_rate1 * sh.math.sin(flicker_clock1), 12)
    flicker_clock1 = s16(flicker_clock1 + dt)
    local phase = asr(flicker_clock1 * flicker_rate2, 6)
    if phase >= 4096 then
        flicker_clock1 = 0
        flicker_rate2 = sh.rng.uint(32, 95)
        phase = phase & 0xFFF
    end
    local swing = asr(s32((asr(amp * 18432, 6) + 10240) * sh.math.sin(phase)), 12) -- 4.5, 2.5
    flicker_level = math.max(0, swing + 16384) -- around 4.0
    sh.world.effects_track_set(CELL_FLICKER, flicker_level)
end

local function wheel_spin(dt)
    if sh.flag_get("M0S00_AlleyNightmareStarted") then
        return
    end
    local x, _, z = sh.player_pos()
    if not (in_chunk(x, 1, -7, -1, -7) and (in_chunk(z, 1, 6, -1, 6) or in_chunk(z, 1, 7, -1, 7))) then
        return
    end
    -- One Rng_Rand16 per frame; Q12_MULT_PRECISE.
    spin = spin + (dt * (-1024 - (sh.rng_int(0, 0x7FFF) & 0x1FF))) // 4096
    for _, name in ipairs(WHEELS) do
        sh.world_object.rotation_set(name, TILT_X, TILT_Y, spin)
    end
end

-- Map_WorldObjectsInit
for _, name in ipairs(WHEELS) do
    sh.world_object.rotation_set(name, TILT_X, TILT_Y, spin)
end
sh.flag_set("M0S00_WheelchairSqueakStarted", false)

function update(dt)
    if sh.flag_get("M0S00_CherylSpottedCutsceneDone") and not sh.flag_get("M0S00_AlleyEntered") then
        if not sh.flag_get("M0S00_CherylLeadRunEnded") then
            cheryl_lead(dt)
        elseif not sh.flag_get("M0S00_CherylCornerRunEnded") then
            cheryl_corner(dt)
        elseif not sh.flag_get("M0S00_CherylThroughAlleyDoor") then
            cheryl_door()
        end
        alley_camera()
    end
    if sh.flag_get("M0S00_AlleyEntered") and not sh.flag_get("M0S00_AlleyFullyDark") then
        alley_darkness(dt)
    end
    if sh.flag_get("M0S00_AlleyGetsDarkerCutsceneDone") then
        alley_flicker(dt)
    end
    wheel_spin(dt)
end

-- The wheelchair squeaks while Harry is in its cells (x -7, z 5..6) until M0S00_AlleyNightmareStarted; the squeak
-- restarts after the radio (bgm flag 6) has cut it.
function post_draw()
    if sh.flag_get("M0S00_AlleyNightmareStarted") then
        return
    end
    local x, _, z = sh.player_pos()
    if not (in_chunk(x, 1, -7, -1, -7) and (in_chunk(z, 1, 6, -1, 6) or in_chunk(z, 1, 7, -1, 7))) then
        return
    end
    if sh.sound.bgm_flags() & 0x40 ~= 0 then
        sh.flag_set("M0S00_WheelchairSqueakStarted", false)
    end
    if not sh.flag_get("M0S00_WheelchairSqueakStarted") then
        sh.sound.stop("opening_ambience")
        sh.sound.sd_call("alley_nightmare_sfx")
        sh.flag_set("M0S00_WheelchairSqueakStarted", true)
    end
    local p = WHEELCHAIR_SFX_POS
    sh.sound.play_3d_falloff("alley_nightmare_sfx", p[1], p[2], p[3], 64, 32768, 0) -- 0.25, 8 m
end

-- MapEvent_GreyChildrenSpawn: three Grey Children wait in the last alley (cells x -7, z 6)
-- after M0S00_AlleyGetsDarkerCutsceneDone when no one is there yet.
function npc_spawn()
    if not sh.flag_get("M0S00_AlleyGetsDarkerCutsceneDone") or sh.npc.chara_id(CHERYL) ~= 0 then
        return
    end
    local x, _, z = sh.player_pos()
    if in_chunk(x, 1, -7, -1, -7) and in_chunk(z, 1, 7, -1, 7) then
        sh.npc.spawn("GreyChild", 0, -1032192, 913408, 2048, 5) -- (-252, 223), 180 deg
        sh.npc.spawn("GreyChild", 1, -1040384, 905216, 1536, 5) -- (-254, 221), 135 deg
        sh.npc.spawn("GreyChild", 2, -1060864, 950272, 0, 5)    -- (-259, 232), 0 deg
    end
end
