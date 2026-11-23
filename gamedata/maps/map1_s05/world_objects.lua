-- World objects and set-up of map1_s05: the Split Head room.
-- The lamp sphere and the wheel above it turn all the time; the burning lamp is a particle emitter (the map
-- effect "particle_emitter") whose light flickers while M1S05_BossLampFlickering is set and whose hum fades with the flame.
-- Angles are Q3.12 (4096 = a full turn); the engine keeps them 16-bit, so they wrap like the original's
-- SVECTOR fields.

local SPHERE_SPEED = -1024  -- -90 degrees per second
local WHEEL_SPEED = 170     -- 15 degrees per second (Q12_ANGLE truncates 170.67)

-- The lamp at (19.7, -3.0, -19.1) m; its hum plays from (19.7, -1.5, -19.1) m.
local LAMP_X, LAMP_Y, LAMP_Z = 80691, -12288, -78233
local HUM_Y = -6144
local HUM = "boss_room_lamp_hum"

-- The flicker state (s16 clocks, u8 rates and amplitude), reset when the map loads.
local clock = {0, 0}
local rate = {64, 64}
local amplitude = 48

local function s16(v)
    v = v & 0xFFFF
    return v >= 0x8000 and v - 0x10000 or v
end

-- Set-up before the objects (the start of Map_WorldObjectsInit): the flame's particle volume.
sh.map.set("particle_emitter.flags", 9)          -- bits 0 and 3
sh.map.set("particle_emitter.spawn_count", 300)
sh.map.set("particle_emitter.top_y", -11468)     -- -2.8 m
sh.map.set("particle_emitter.bottom_y", -2048)   -- -0.5 m
sh.map.set("particle_emitter.radius", 2560)      -- 0.625 m
sh.map.set("particle_emitter.unread_d", -4096)
sh.map.set("particle_emitter.sway_divisor", 128)
sh.map.set("particle_emitter.unread_b", 196)
sh.map.set("particle_emitter.unread_c", 60)
sh.map.set("particle_emitter.drift_speed", 512)
sh.map.set("particle_emitter.spin_speed", 1024)
sh.map.set("particle_emitter.state", 2)
sh.map.set("particle_emitter.unread_a", 8)
sh.map.set("particle_emitter.spread", 64)
sh.map.set("particle_emitter.center_x", LAMP_X)
sh.map.set("particle_emitter.center_z", LAMP_Z)
if sh.flag_get("M1S05_SplitHeadIntroDone") then
    sh.light_intensity_set(5670) -- 1.3843
    sh.map.set("particle_emitter.state", 0)
end
sh.map.particle_emitter_start()

local function turn(name, speed, dt)
    local x, y, z = sh.world_object.rotation_get(name)
    -- Floor division is the original's 64-bit multiply and arithmetic shift right by 12.
    sh.world_object.rotation_set(name, x, y + (dt * speed) // 4096, z)
end

function update(dt)
    turn("Sphere", SPHERE_SPEED, dt)
    turn("Wheel", WHEEL_SPEED, dt)
end

-- After the objects (the end of Map_WorldObjectsInit): the light hangs at the lamp, pointing down, and the
-- hum starts.
function post_init()
    sh.light_bones_clear()
    sh.light_pos_set(LAMP_X, LAMP_Y, LAMP_Z)
    sh.light_rot_set(-1024, 0, 0) -- -90 degrees about x
    sh.sd_call(HUM)
end

-- After the draws (the rest of Map_WorldObjectsUpdate). The shifts of the original are arithmetic, so
-- they are floor divisions here.
function post_draw()
    if sh.flag_get("M1S05_BossLampFlickering") then
        local dt = sh.game.delta_time()
        clock[1] = s16(clock[1] + dt)
        if clock[1] * rate[1] >= 262144 then -- 8.0 squared
            clock[1] = 0
            rate[1] = sh.rng_uint(32, 95)
            amplitude = sh.rng_uint(32, 63)
        end
        local swing = (amplitude * sh.math.sin(clock[2])) // 4096
        clock[2] = s16(clock[2] + dt)
        local phase = (clock[2] * rate[2]) // 64
        if phase >= 4096 then
            clock[2] = 0
            rate[2] = sh.rng_uint(32, 95)
            phase = phase & 0xFFF
        end
        -- 1.35 + (0.9 + swing * 0.6) * sin(phase) / 8
        sh.light_intensity_set(5529 + ((3686 + (swing * 2457) // 4096) * sh.math.sin(phase)) // 32768)
    end
    -- The hum is quieter the brighter the flame burns (intensity 1.0 silences it).
    local volume = 128 - (sh.map.get("particle_emitter.intensity") * 128) // 4096
    sh.sfx_falloff_pitch_play(HUM, LAMP_X, HUM_Y, LAMP_Z, volume, 65536, 0)
end
